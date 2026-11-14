#include "overlay.h"
#include "MinHook.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "vendor/imgui_impl_dx11.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <mutex>
#include <deque>
#include <unordered_map>
#include <cmath>
#include <iterator>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
using Microsoft::WRL::ComPtr;
#include "native-skin.h"
namespace {
using Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Resize = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using Present1 = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
Present originalPresent = nullptr;
Resize originalResize = nullptr;
Present1 originalPresent1 = nullptr;
void* presentAddress = nullptr;
void* resizeAddress = nullptr;
void* present1Address = nullptr;
thread_local bool presenting = false;
ds::Shared* control = nullptr;
std::recursive_mutex mutex;
ComPtr<ID3D11Device> device;
ComPtr<ID3D11DeviceContext> context;
ComPtr<ID3D11RenderTargetView> target;
// Keep a reference while using this swap chain. No arbitrary hot-unload.
ComPtr<IDXGISwapChain> activeChain;
ImGuiContext* gui = nullptr;
ImVec2 panelScreen{};
HWND gameWindow = nullptr;
std::unordered_map<HWND,WNDPROC> previousProcs;
struct Message { HWND window; UINT message; WPARAM w; LPARAM l; };
std::deque<Message> input;
bool f2Down = false;
bool wasVisible = false, releasedClip = false;
RECT oldClip{};
void RestoreClip() {
    if (releasedClip && GetForegroundWindow() == gameWindow) ClipCursor(&oldClip);
    releasedClip=false;
}

bool Connected() {
    return control && ds::Read(control->state) == ds::Ready &&
        ds::Fresh(GetTickCount(), DWORD(ds::Read(control->clientHeartbeat)));
}
bool Visible() { return Connected() && ds::Read(control->panelVisible) != 0; }
bool IsInput(UINT m) {
    return (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) || (m >= WM_KEYFIRST && m <= WM_KEYLAST) ||
        m == WM_INPUT || m == WM_SETCURSOR;
}
LRESULT CALLBACK GameProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    WNDPROC next;
    bool capture;
    {
        std::lock_guard lock(mutex);
        auto found=previousProcs.find(hwnd);next=found==previousProcs.end()?nullptr:found->second;
        capture = hwnd==gameWindow && Visible() && IsInput(message);
        // Queue backend input on the render thread; ImGui is not thread-safe.
        if (hwnd==gameWindow && gui && Visible() && message != WM_INPUT && input.size() < 1024)
            input.push_back({hwnd, message, w, l});
        if (capture && message == WM_SETCURSOR && LOWORD(l) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW)); return TRUE;
        }
    }
    // WM_INPUT requires default processing to release the system's input handle.
    if (capture) { if (message == WM_INPUT) return DefWindowProcW(hwnd, message, w, l); return 0; }
    LRESULT result=next ? CallWindowProcW(next, hwnd, message, w, l) : DefWindowProcW(hwnd, message, w, l);
    if(message==WM_NCDESTROY) {std::lock_guard lock(mutex);previousProcs.erase(hwnd);}
    return result;
}
bool MakeTarget(IDXGISwapChain* chain) {
    ComPtr<ID3D11Texture2D> buffer;
    return SUCCEEDED(chain->GetBuffer(0, IID_PPV_ARGS(&buffer))) &&
        SUCCEEDED(device->CreateRenderTargetView(buffer.Get(), nullptr, &target));
}
void Cleanup() {
    RestoreClip(); wasVisible=false;
    auto previous=previousProcs.find(gameWindow);
    if (gameWindow && previous!=previousProcs.end() && IsWindow(gameWindow) &&
        reinterpret_cast<WNDPROC>(GetWindowLongPtrW(gameWindow, GWLP_WNDPROC)) == GameProc) {
        SetWindowLongPtrW(gameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previous->second));
        previousProcs.erase(previous);
    }
    // If another overlay subclasses after us, keep forwarding its call chain.
    // Erase that entry when its window is destroyed instead of losing game input.
    gameWindow = nullptr;
    if (gui) {
        ImGui::SetCurrentContext(gui);
        ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(gui); gui = nullptr;
    }
    nativeui::Clear();panelScreen=ImVec2();
    input.clear(); target.Reset(); context.Reset(); device.Reset(); activeChain.Reset();
}
bool Initialize(IDXGISwapChain* chain) {
    DXGI_SWAP_CHAIN_DESC desc{}; DWORD pid = 0;
    if (FAILED(chain->GetDesc(&desc)) || !desc.OutputWindow) return false;
    GetWindowThreadProcessId(desc.OutputWindow, &pid);
    if (pid != GetCurrentProcessId()) return false;
    if (FAILED(chain->GetDevice(IID_PPV_ARGS(&device)))) return false;
    device->GetImmediateContext(&context);
    if (!MakeTarget(chain)) { context.Reset(); device.Reset(); return false; }
    gui = ImGui::CreateContext(); ImGui::SetCurrentContext(gui);
    auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Never change the game's OS cursor. Draw a cursor only while the panel is open.
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    if(nativeui::Load(device.Get())) {
        ImFontConfig config;config.FontDataOwnedByAtlas=false;
        io.Fonts->AddFontFromMemoryTTF(nativeui::font.data(),int(nativeui::font.size()),24.0f,&config,io.Fonts->GetGlyphRangesChineseFull());
    } else {
        wchar_t windows[32768];char fontPath[32768];
        if(GetWindowsDirectoryW(windows,DWORD(std::size(windows)))) {
            wcscat_s(windows,L"\\Fonts\\msyhbd.ttc");
            WideCharToMultiByte(CP_UTF8,0,windows,-1,fontPath,int(std::size(fontPath)),nullptr,nullptr);
            io.Fonts->AddFontFromFileTTF(fontPath,24.0f,nullptr,io.Fonts->GetGlyphRangesChineseFull());
        }
    }
    if(io.Fonts->Fonts.empty())io.Fonts->AddFontDefault();
    ImGui::StyleColorsDark(); auto& style = ImGui::GetStyle();
    style.WindowRounding = 0; style.FrameRounding = 0; style.GrabRounding = 0;
    style.TabRounding = 0; style.ScrollbarRounding = 0;
    style.WindowPadding = ImVec2(24, 18); style.FramePadding = ImVec2(9, 7);
    style.ItemSpacing = ImVec2(12, 10); style.WindowBorderSize = 2; style.FrameBorderSize = 1;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(.025f,.022f,.035f,.96f);
    style.Colors[ImGuiCol_Text] = ImVec4(.96f,.95f,.91f,1);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(.62f,.60f,.58f,1);
    style.Colors[ImGuiCol_Border] = ImVec4(.72f,.70f,.65f,1);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(.95f,.80f,.40f,1);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(.08f,.07f,.10f,1);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(.24f,.20f,.12f,1);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(.34f,.27f,.12f,1);
    style.Colors[ImGuiCol_Button] = ImVec4(.09f,.08f,.10f,1);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(.32f,.26f,.13f,1);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(.44f,.34f,.14f,1);
    style.Colors[ImGuiCol_Tab] = ImVec4(.08f,.07f,.10f,1);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(.32f,.26f,.13f,1);
    style.Colors[ImGuiCol_TabSelected] = ImVec4(.38f,.29f,.12f,1);
    style.Colors[ImGuiCol_Separator] = ImVec4(.65f,.60f,.45f,.65f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(.025f,.022f,.035f,1);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(.48f,.44f,.34f,1);
    gameWindow = desc.OutputWindow;
    if (!ImGui_ImplWin32_Init(gameWindow)) {
        ImGui::DestroyContext(gui); gui = nullptr; target.Reset(); context.Reset(); device.Reset(); gameWindow = nullptr; return false;
    }
    if (!ImGui_ImplDX11_Init(device.Get(), context.Get())) {
        ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(gui); gui = nullptr;
        target.Reset(); context.Reset(); device.Reset(); gameWindow = nullptr; return false;
    }
    if(previousProcs.find(gameWindow)==previousProcs.end()) {
        SetLastError(0);
        auto original=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(gameWindow,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&GameProc)));
        if(!original) { Cleanup(); return false; }
        previousProcs.emplace(gameWindow,original);
    }
    activeChain = chain; ds::Set(control->overlayState, 2); return true;
}
void Frame(IDXGISwapChain* chain) {
    std::lock_guard lock(mutex);
    if (!Connected()) { if (control) ds::Set(control->panelVisible, 0); input.clear(); RestoreClip(); wasVisible=false; return; }
    if (activeChain && activeChain.Get() != chain) {
        DXGI_SWAP_CHAIN_DESC incoming{};
        // Accept a replacement for the same game window; ignore other windows.
        if (IsWindow(gameWindow) && device->GetDeviceRemovedReason() == S_OK &&
            (FAILED(chain->GetDesc(&incoming)) || incoming.OutputWindow!=gameWindow)) return;
        Cleanup();
    }
    if (!gui && !Initialize(chain)) return;
    ImGui::SetCurrentContext(gui);
    LONG command = InterlockedExchange(&control->panelCommand, 0);
    if (command == 1) ds::Set(control->panelVisible, !ds::Read(control->panelVisible));
    else if (command == 2) ds::Set(control->panelVisible, 1);
    else if (command == 3) ds::Set(control->panelVisible, 0);
    bool down = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
    ds::PanelKey(control,down,GetForegroundWindow() == gameWindow,f2Down);
    if ((GetAsyncKeyState(VK_F8)&0x8000) && GetForegroundWindow() == gameWindow) ds::Disable(control);
    if (!Visible()) { input.clear(); RestoreClip(); wasVisible=false; return; }
    if (!target && !MakeTarget(chain)) return;
    if (!wasVisible) {
        // Key-up messages while hidden do not enter the backend. Discard old
        // mouse/key state when reopening, so no button remains stuck down.
        auto& io=ImGui::GetIO();io.ClearEventsQueue();io.ClearInputKeys();io.ClearInputMouse();
        wasVisible=true;
    }
    while (!input.empty()) {
        auto event = input.front(); input.pop_front();
        ImGui_ImplWin32_WndProcHandler(event.window, event.message, event.w, event.l);
    }
    ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame();
    // Unity can confine the pointer while playing; let the visible menu use it.
    if (GetForegroundWindow() == gameWindow) {
        if(!releasedClip)releasedClip=GetClipCursor(&oldClip)!=FALSE;
        ClipCursor(nullptr);
    } else { releasedClip=false; }
    ImGui::GetIO().MouseDrawCursor = true;
    ImGui::NewFrame(); ds::DrawPanel(control); ImGui::Render();
    // Preserve every output slot plus stages the standard backend clears.
    ID3D11RenderTargetView* oldTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
    ComPtr<ID3D11DepthStencilView> depth;
    context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldTargets, &depth);
    ComPtr<ID3D11HullShader> hull; ComPtr<ID3D11DomainShader> domain; ComPtr<ID3D11ComputeShader> compute;
    ID3D11ClassInstance* hc[256]{}, *dc[256]{}, *cc[256]{}; UINT hn=256,dn=256,cn=256;
    context->HSGetShader(&hull,hc,&hn); context->DSGetShader(&domain,dc,&dn); context->CSGetShader(&compute,cc,&cn);
    ID3D11RenderTargetView* view = target.Get(); context->OMSetRenderTargets(1, &view, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, oldTargets, depth.Get());
    context->HSSetShader(hull.Get(),hc,hn); context->DSSetShader(domain.Get(),dc,dn); context->CSSetShader(compute.Get(),cc,cn);
    for (auto* t : oldTargets) if (t) t->Release();
    for (UINT i=0;i<hn;++i)hc[i]->Release(); for(UINT i=0;i<dn;++i)dc[i]->Release(); for(UINT i=0;i<cn;++i)cc[i]->Release();
    InterlockedIncrement64(&control->overlayFrames);
}
HRESULT STDMETHODCALLTYPE OnPresent(IDXGISwapChain* chain, UINT sync, UINT flags) {
    if (presenting) return originalPresent(chain,sync,flags);
    presenting=true;
    if (!(flags & DXGI_PRESENT_TEST)) Frame(chain);
    HRESULT result = originalPresent(chain, sync, flags);
    presenting=false;
    if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET) {
        std::lock_guard lock(mutex); if (chain == activeChain.Get()) Cleanup();
    }
    return result;
}
HRESULT STDMETHODCALLTYPE OnPresent1(IDXGISwapChain1* chain, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* parameters) {
    if(presenting)return originalPresent1(chain,sync,flags,parameters);
    presenting=true;
    if (!(flags & DXGI_PRESENT_TEST)) Frame(chain);
    HRESULT result=originalPresent1(chain,sync,flags,parameters);presenting=false;
    if(result==DXGI_ERROR_DEVICE_REMOVED || result==DXGI_ERROR_DEVICE_RESET) {
        std::lock_guard lock(mutex);if(chain==activeChain.Get())Cleanup();
    }
    return result;
}
HRESULT STDMETHODCALLTYPE OnResize(IDXGISwapChain* chain, UINT count, UINT width, UINT height, DXGI_FORMAT format, UINT flags) {
    std::lock_guard lock(mutex);
    if (chain == activeChain.Get()) target.Reset();
    return originalResize(chain, count, width, height, format, flags);
}
void Toggle(ds::Shared* state,const char* label,ds::Feature feature) {
    bool on=(ds::Read(state->enabled)&feature)!=0;
    bool available=(ds::Read(state->available)&feature)!=0;
    ImGui::BeginDisabled(!available);
    const ImVec2 at=ImGui::GetCursorScreenPos();
    const float side=ImGui::GetFrameHeight();
    nativeui::Image(nativeui::Slot,at,ImVec2(at.x+side,at.y+side));
    if(on)nativeui::Image(nativeui::Check,ImVec2(at.x+nativeui::S(6),at.y+nativeui::S(6)),
        ImVec2(at.x+side-nativeui::S(6),at.y+side-nativeui::S(6)));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_CheckMark,ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_CheckboxSelectedBg,ImVec4(0,0,0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);
    if(ImGui::Checkbox(label,&on))ds::SetFeature(state,feature,on);
    ImGui::PopStyleVar();ImGui::PopStyleColor(3);ImGui::EndDisabled();
}
void Multiplier(ds::Shared* state,const char* label,ds::Feature feature,volatile LONG& field) {
    Toggle(state,label,feature);ImGui::SameLine(ImGui::GetWindowWidth()-nativeui::S(270));ImGui::PushID(label);
    float value=ds::Read(field)/1000.0f;ImGui::SetNextItemWidth(nativeui::S(205));
    if(ImGui::InputFloat("##factor",&value,.5f,10,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue)&&std::isfinite(value))
        ds::Set(field,ds::NormalizeMultiplier(int(std::lround(std::clamp(value,.1f,1000.0f)*1000))));
    ImGui::SameLine();ImGui::TextUnformatted("倍");ImGui::PopID();
}
void Capacity(ds::Shared* state,const char* label,ds::Feature feature,volatile LONG& value,
              volatile LONG& occupied,volatile LONG& allowed,volatile LONG& status) {
    Toggle(state,label,feature);ImGui::SameLine(ImGui::GetWindowWidth()-nativeui::S(270));
    ImGui::PushID(label);int count=ds::Read(value);ImGui::SetNextItemWidth(nativeui::S(205));
    if(ImGui::InputInt("##slots",&count,1,5,ImGuiInputTextFlags_EnterReturnsTrue))ds::Set(value,std::clamp(count,6,50));
    ImGui::TextDisabled("已使用 %ld / %ld",ds::Read(occupied),ds::Read(allowed));
    if(ds::Read(status)==2)ImGui::TextWrapped("请先移走超额物品。");
    if(ds::Read(status)==3)ImGui::TextWrapped("关闭游戏角色或装备窗口后应用。");
    if(ds::Read(status)<0)ImGui::TextWrapped("容量读取失败，请重启游戏后先注入再继续。");
    ImGui::PopID();
}

}
namespace ds {
void DrawPanel(Shared* state) {
    using namespace nativeui;
    auto& io=ImGui::GetIO();scale=std::clamp(io.DisplaySize.y/1080.0f,.8f,1.65f);
    const ImVec2 size(std::min(S(1080),io.DisplaySize.x-S(32)),std::min(S(780),io.DisplaySize.y-S(40)));
    const bool resized=panelScreen.x!=io.DisplaySize.x||panelScreen.y!=io.DisplaySize.y;
    panelScreen=io.DisplaySize;
    ImGui::SetNextWindowPos(ImVec2((io.DisplaySize.x-size.x)*.5f,(io.DisplaySize.y-size.y)*.5f),resized?ImGuiCond_Always:ImGuiCond_FirstUseEver);
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0,0),io.DisplaySize,IM_COL32(0,0,0,115));
    ImGui::SetNextWindowSize(size,ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,Size(26,22));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,Size(9,8));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,Size(14,14));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    ImGui::PushFont(nullptr,S(30));
    bool open=true;
    if(ImGui::Begin("DUNGEON SLASHER  |  F2##trainer",nullptr,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoScrollbar)) {
        Image(SlotBold,ImGui::GetWindowPos(),ImVec2(ImGui::GetWindowPos().x+size.x,ImGui::GetWindowPos().y+size.y));
        Corners(ImGui::GetWindowPos(),ImVec2(ImGui::GetWindowPos().x+size.x,ImGui::GetWindowPos().y+size.y));
        const ImVec2 heading=ImGui::GetCursorScreenPos();
        const float width=ImGui::GetContentRegionAvail().x;
        auto* draw=ImGui::GetWindowDrawList();
        draw->AddRectFilledMultiColor(heading,ImVec2(heading.x+width,heading.y+S(70)),
            IM_COL32(167,119,30,170),IM_COL32(45,43,52,0),IM_COL32(45,43,52,0),IM_COL32(167,119,30,100));
        Text(ImVec2(heading.x+S(18),heading.y+S(7)),"冒险设置",S(44));
        ImGui::SetCursorPosX(ImGui::GetWindowWidth()-S(164));
        if(Button("返回  F2",Size(138,54)))open=false;
        ImGui::SetCursorScreenPos(ImVec2(heading.x,heading.y+S(86)));
        const char* pages[]={"战斗与收益","装备与背包","角色与解锁","符文调整","货币编辑"};
        int page=std::clamp(ImGui::GetStateStorage()->GetInt(ImGui::GetID("Page"),0),0,4);
        ImGui::BeginChild("Navigation",ImVec2(S(208),ImGui::GetContentRegionAvail().y-S(6)),ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar);
        for(int i=0;i<5;++i) {
            ImGui::PushID(i);
            if(Button(pages[i],Size(196,64),page==i))page=i;
            ImGui::PopID();
        }
        ImGui::EndChild();ImGui::SameLine();
        ImGui::GetStateStorage()->SetInt(ImGui::GetID("Page"),page);
        ImGui::BeginGroup();
        ImGui::PushFont(nullptr,S(28));ImGui::TextUnformatted(pages[page]);ImGui::PopFont();
        ImGui::Separator();
        if(page==4) {
            const char* warning="直接编辑货币可能造成账号封禁，请谨慎使用。";
            const float padding=S(14),wrap=ImGui::GetContentRegionAvail().x-padding*2;
            const float height=padding*2+ImGui::GetTextLineHeight()+ImGui::GetStyle().ItemSpacing.y+ImGui::CalcTextSize(warning,nullptr,false,wrap).y;
            ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(.24f,.065f,.04f,1));
            ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(1,.46f,.16f,1));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(padding,padding));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,S(8));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize,S(2));
            ImGui::BeginChild("WalletRisk",ImVec2(0,height),ImGuiChildFlags_Borders,ImGuiWindowFlags_NoScrollbar);
            ImGui::TextColored(ImVec4(1,.65f,.22f,1),"账号封禁风险");
            ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.9f,.76f,1));
            ImGui::TextWrapped("%s",warning);
            ImGui::PopStyleColor();ImGui::EndChild();
            ImGui::PopStyleVar(3);ImGui::PopStyleColor(2);
        }
        ImGui::BeginChild("FeatureContent",ImVec2(0,-S(104)));
        if(page==0) {
            Toggle(state,"无敌",God);Toggle(state,"技能无冷却",NoCooldown);
            Multiplier(state,"伤害倍率",Damage,state->damageMultiplier);
            Multiplier(state,"金币获取倍率",Gold,state->goldMultiplier);
            Multiplier(state,"结算奖励倍率",Reward,state->rewardMultiplier);
            Multiplier(state,"符文粉末倍率",RunePowder,state->runePowderMultiplier);
            Multiplier(state,"钻石领取倍率",Diamond,state->diamondMultiplier);
            Toggle(state,"无限冲刺",InfiniteDash);Toggle(state,"无限跳跃",InfiniteJump);
            Toggle(state,"连接失败自动重试",AutoRetry);
        } else if(page==1) {
            Capacity(state,"圣器装备格数",ArtifactCapacity,state->artifactSlots,state->artifactEquipped,state->artifactAllowed,state->artifactStatus);
            Capacity(state,"圣器背包格数",ArtifactInventoryCapacity,state->inventorySlots,state->inventoryOccupied,state->inventoryAllowed,state->inventoryStatus);
            Capacity(state,"普通技能装备格数",SkillCapacity,state->skillSlots,state->skillEquipped,state->skillAllowed,state->skillStatus);
            Capacity(state,"技能背包格数",SkillInventoryCapacity,state->skillInventorySlots,state->skillInventoryOccupied,state->skillInventoryAllowed,state->skillInventoryStatus);
            Toggle(state,"增益技能扩展至 6 格",BuffCapacity);
            if(Read(state->buffStatus)==2)ImGui::TextWrapped("请先卸下超额增益技能。");
            if(Read(state->buffStatus)==3)ImGui::TextWrapped("关闭游戏角色或技能窗口后应用。");
            if(Read(state->buffStatus)<0)ImGui::TextWrapped("增益技能容量读取失败。");
        } else if(page==2) {
            Toggle(state,"解锁全部角色",Characters);Toggle(state,"解锁角色皮肤与大厅背景",Skins);
            Toggle(state,"解锁全部武器",Weapons);Toggle(state,"解锁全部宠物",Pets);
            ImGui::Separator();Toggle(state,"自定义罪恶总点数",Sin);
            int points=Read(state->sinPoints);ImGui::SetNextItemWidth(S(260));
            if(ImGui::InputInt("总点数",&points,1,10,ImGuiInputTextFlags_EnterReturnsTrue))Set(state->sinPoints,std::clamp(points,0,MaxSinPoints));
            ImGui::Separator();const LONG status=Read(state->unlockStatus);
            ImGui::BeginDisabled(status==1||status==2);
            if(Button("解锁全部技能和圣器",Size(480,58),true))RequestUnlockAll(state);
            ImGui::EndDisabled();
            if(status==1||status==2)ImGui::TextUnformatted("正在解锁……");
            if(status==3)ImGui::TextWrapped("已解锁：技能 %ld / 圣器 %ld",Read(state->unlockedSkills),Read(state->unlockedArtifacts));
            if(status<0)ImGui::TextWrapped("解锁失败：%s",UnlockErrorLabel(Read(state->unlockError)));
        } else if(page==3) {
            Toggle(state,"符文重铸与精炼最高值",RuneMaximum);
            ImGui::Separator();Toggle(state,"自定义挑战符文掉落率",ChallengeRate);
            float normal=Read(state->challengeNormalChance)/100.0f,boss=Read(state->challengeBossChance)/100.0f;
            ImGui::SetNextItemWidth(S(260));
            if(ImGui::InputFloat("普通怪 (%)",&normal,1,10,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue)&&std::isfinite(normal))Set(state->challengeNormalChance,LONG(std::lround(std::clamp(normal,0.0f,100.0f)*100)));
            ImGui::SetNextItemWidth(S(260));
            if(ImGui::InputFloat("首领 (%)",&boss,1,10,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue)&&std::isfinite(boss))Set(state->challengeBossChance,LONG(std::lround(std::clamp(boss,0.0f,100.0f)*100)));
            ImGui::Separator();Toggle(state,"增加符文掉落数量",ChallengeQuantity);
            int quantity=Read(state->challengeQuantity);ImGui::SetNextItemWidth(S(260));
            if(ImGui::InputInt("数量倍率",&quantity,1,5,ImGuiInputTextFlags_EnterReturnsTrue))Set(state->challengeQuantity,std::clamp(quantity,1,MaxChallengeQuantity));
        } else {
            int selected=std::clamp(int(Read(state->walletSelection)),0,WalletCount-1);
            ImGui::SetNextItemWidth(-S(18));
            if(ImGui::BeginCombo("##wallet",Wallets[selected].label)) {
                for(int i=0;i<WalletCount;++i) {
                    if(ImGui::Selectable(Wallets[i].label,i==selected)){Set(state->walletSelection,i);selected=i;}
                    if(i==selected)ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::Spacing();
            if(Read(state->walletReadIndex)==selected&&Read(state->walletReadStatus)==1)ImGui::Text("当前余额：%ld",Read(state->walletBalance));
            else ImGui::TextDisabled("等待游戏读取余额");
            int64_t balance=Read(state->walletTargets[selected]);const int64_t step=1,fast=100;
            ImGui::SetNextItemWidth(S(330));
            if(ImGui::InputScalar("目标余额",ImGuiDataType_S64,&balance,&step,&fast,"%lld",ImGuiInputTextFlags_EnterReturnsTrue))Set(state->walletTargets[selected],LONG(std::clamp<int64_t>(balance,0,INT_MAX)));
            ImGui::Spacing();const LONG status=Read(state->walletStatus);
            ImGui::BeginDisabled(status==1||status==2);
            if(Button("应用货币余额",Size(380,64),true))RequestWallet(state);
            ImGui::EndDisabled();
            if(status==1||status==2)ImGui::TextUnformatted("正在应用……");
            if(status==3&&ValidWallet(Read(state->walletRequestIndex)))ImGui::TextWrapped("已应用：%s → %ld",Wallets[Read(state->walletRequestIndex)].label,Read(state->walletRequestTarget));
            if(status<0)ImGui::TextWrapped("应用失败：%s",WalletErrorLabel(Read(state->walletError)));
        }
        ImGui::EndChild();
        ImGui::Separator();
        const float buttonWidth=(ImGui::GetContentRegionAvail().x-S(14))*.5f;
        if(Button("全部关闭  F8",ImVec2(buttonWidth,S(56))))Disable(state);
        ImGui::SameLine();if(Button("返回游戏  F2",ImVec2(buttonWidth,S(56))))open=false;
        if(Read(state->preferencesError))ImGui::TextColored(ImVec4(1,.6f,.3f,1),"设置保存失败，请检查目录权限。");
        ImGui::EndGroup();
    }
    ImGui::End();ImGui::PopFont();ImGui::PopStyleVar(4);
    if(!open)Set(state->panelVisible,0);
}
bool InstallOverlay(Shared* state) {
    control = state;
    WNDCLASSW cls{}; cls.hInstance = GetModuleHandleW(nullptr); cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = L"DungeonSlasherTrainer.D3D11Probe";
    ATOM atom = RegisterClassW(&cls);
    if (!atom) return false;
    HWND dummy = CreateWindowExW(0,cls.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,cls.hInstance,nullptr);
    DXGI_SWAP_CHAIN_DESC desc{}; desc.BufferCount=1; desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.OutputWindow=dummy; desc.SampleDesc.Count=1; desc.Windowed=TRUE;
    ComPtr<IDXGISwapChain> chain; ComPtr<ID3D11Device> probeDevice;
    HRESULT result = dummy ? D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&chain,&probeDevice,nullptr,nullptr) : E_FAIL;
    if (FAILED(result) && dummy) result = D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&desc,&chain,&probeDevice,nullptr,nullptr);
    if (SUCCEEDED(result)) {
        void** table = *reinterpret_cast<void***>(chain.Get()); presentAddress=table[8]; resizeAddress=table[13];
        bool created = MH_CreateHook(presentAddress,reinterpret_cast<void*>(&OnPresent),reinterpret_cast<void**>(&originalPresent)) == MH_OK;
        if (created && MH_CreateHook(resizeAddress,reinterpret_cast<void*>(&OnResize),reinterpret_cast<void**>(&originalResize)) == MH_OK &&
            MH_QueueEnableHook(presentAddress)==MH_OK && MH_QueueEnableHook(resizeAddress)==MH_OK) {
            ComPtr<IDXGISwapChain1> modern;
            if(SUCCEEDED(chain.As(&modern))) {
                auto* modernTable=*reinterpret_cast<void***>(modern.Get());
                void* address=modernTable[22];
                if(MH_CreateHook(address,reinterpret_cast<void*>(&OnPresent1),reinterpret_cast<void**>(&originalPresent1))==MH_OK) {
                    if(MH_QueueEnableHook(address)==MH_OK)present1Address=address;
                    else MH_RemoveHook(address);
                }
            }
            Set(state->overlayState,1);
        } else { if (created) MH_RemoveHook(presentAddress); MH_RemoveHook(resizeAddress); result=E_FAIL; }
    }
    chain.Reset(); probeDevice.Reset(); if(dummy)DestroyWindow(dummy); UnregisterClassW(cls.lpszClassName,cls.hInstance);
    return SUCCEEDED(result);
}
void RemoveOverlayHooks() {
    if(presentAddress) { MH_DisableHook(presentAddress); MH_RemoveHook(presentAddress); }
    if(resizeAddress) { MH_DisableHook(resizeAddress); MH_RemoveHook(resizeAddress); }
    if(present1Address) { MH_DisableHook(present1Address); MH_RemoveHook(present1Address); }
    std::lock_guard lock(mutex); Cleanup(); control=nullptr; presentAddress=nullptr; resizeAddress=nullptr;present1Address=nullptr;
}
}
