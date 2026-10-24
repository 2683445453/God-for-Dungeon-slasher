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
    wchar_t windows[32768]; char font[32768];
    if (GetWindowsDirectoryW(windows, DWORD(std::size(windows)))) {
        wcscat_s(windows, L"\\Fonts\\msyh.ttc");
        WideCharToMultiByte(CP_UTF8, 0, windows, -1, font, int(std::size(font)), nullptr, nullptr);
        io.Fonts->AddFontFromFileTTF(font, 19.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
    }
    if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();
    ImGui::StyleColorsDark(); auto& style = ImGui::GetStyle();
    style.WindowRounding = 12; style.FrameRounding = 6; style.GrabRounding = 6;
    style.WindowPadding = ImVec2(22, 18); style.FramePadding = ImVec2(9, 7);
    style.ItemSpacing = ImVec2(12, 10); style.WindowBorderSize = 1;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(.045f,.065f,.10f,.98f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(.24f,.78f,1,.95f);
    style.Colors[ImGuiCol_Button] = ImVec4(.12f,.31f,.48f,1);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(.18f,.45f,.64f,1);
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
void Toggle(ds::Shared* state, const char* label, ds::Feature feature) {
    bool on = (ds::Read(state->enabled) & feature) != 0;
    if (ImGui::Checkbox(label, &on)) ds::SetFeature(state, feature, on);
}
void Multiplier(ds::Shared* state, const char* label, ds::Feature feature, volatile LONG& field) {
    Toggle(state, label, feature); ImGui::SameLine(300); ImGui::PushID(label);
    float value = ds::Read(field) / 1000.0f;
    ImGui::SetNextItemWidth(150);
    if (ImGui::InputFloat("##factor", &value, .5f, 10, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue) && std::isfinite(value))
        ds::Set(field, ds::NormalizeMultiplier(int(std::lround(std::clamp(value,.1f,1000.0f) * 1000))));
    ImGui::SameLine(); ImGui::TextUnformatted("倍"); ImGui::PopID();
}
}
namespace ds {
void DrawPanel(Shared* state) {
    auto& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(30,30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(std::min(580.0f,io.DisplaySize.x-16), std::min(790.0f,io.DisplaySize.y-60)), ImGuiCond_Always);
    bool open = true;
    if (ImGui::Begin("DUNGEON SLASHER  |  F2##trainer", &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
        ImGui::TextColored(ImVec4(.25f,.8f,1,1), "单机修改面板");
        ImGui::SameLine(); ImGui::TextDisabled("Build 26103048 / r19");
        ImGui::TextWrapped("F2 显示 / 隐藏；关闭面板后功能继续生效。");
        ImGui::TextWrapped("开关和参数自动保存，下次连接时恢复。");
        const float footerReserve=ImGui::GetStateStorage()->GetInt(ImGui::GetID("运行统计"),0)?190.0f:100.0f;
        if(ImGui::BeginTabBar("Features")) {
        if(ImGui::BeginTabItem("常用功能")) {
        ImGui::BeginChild("CommonControls",ImVec2(0,-footerReserve));
        ImGui::SeparatorText("战斗与奖励");
        Toggle(state, "无敌", God);
        ImGui::SameLine(300);Toggle(state,"无技能冷却时间",NoCooldown);
        Multiplier(state,"伤害倍率", Damage, state->damageMultiplier);
        Multiplier(state,"金币获取倍率", Gold, state->goldMultiplier);
        Multiplier(state,"结算奖励倍率", Reward, state->rewardMultiplier);
        Multiplier(state,"符文分解粉末倍率", RunePowder, state->runePowderMultiplier);
        Multiplier(state,"钻石领取倍率", Diamond, state->diamondMultiplier);
        ImGui::TextDisabled("倍率 0.1–1000；输入后回车，+/− 按钮即时应用。");
        ImGui::TextWrapped("钻石独立倍率开启时优先于结算倍率；消费不变。粉末包含手动及溢出分解。");
        ImGui::SeparatorText("移动");
        Toggle(state,"无限冲刺", InfiniteDash); ImGui::SameLine(300); Toggle(state,"无限跳跃",InfiniteJump);
        ImGui::TextWrapped("使用原冲刺 / 跳跃按键；补足冲刺次数，空中可重复跳跃。");
        Toggle(state,"连接错误静默自动重试",AutoRetry);
        ImGui::TextDisabled("后台重试：等待 %ld 项 / 已发起 %ld 次",Read(state->syncPending),Read(state->syncRetries));
        ImGui::TextWrapped("连接超时在后台延时重试，保留原生保存及完成回调。关闭后待重试项恢复手动提示。");
        ImGui::SeparatorText("圣器背包");
        Toggle(state,"自定义背包上限",ArtifactInventoryCapacity);ImGui::SameLine(300);
        int backSlots=Read(state->inventorySlots);ImGui::SetNextItemWidth(150);
        if(ImGui::InputInt("##inventorySlots",&backSlots,1,5,ImGuiInputTextFlags_EnterReturnsTrue))Set(state->inventorySlots,std::clamp(backSlots,MinInventorySlots,MaxInventorySlots));
        ImGui::TextDisabled("6–50；背包圣器 %ld / %ld",Read(state->inventoryOccupied),Read(state->inventoryAllowed));
        if(Read(state->inventoryStatus)==2)ImGui::TextWrapped("等待移走或拆卸超额背包物品，现有物品会保留。");
        if(Read(state->inventoryStatus)==3)ImGui::TextWrapped("关闭角色 / 圣器选择窗口后自动应用。");
        ImGui::SeparatorText("技能装备");
        Toggle(state,"自定义普通技能上限",SkillCapacity);ImGui::SameLine(300);
        int skillSlots=Read(state->skillSlots);ImGui::SetNextItemWidth(150);
        if(ImGui::InputInt("##skillSlots",&skillSlots,1,5,ImGuiInputTextFlags_EnterReturnsTrue))Set(state->skillSlots,std::clamp(skillSlots,MinSkillSlots,MaxSkillSlots));
        ImGui::TextDisabled("6–50；普通技能 %ld / %ld",Read(state->skillEquipped),Read(state->skillAllowed));
        if(Read(state->skillStatus)==2)ImGui::TextWrapped("等待卸下超额普通技能，增益技能和背包会保留。");
        if(Read(state->skillStatus)==3)ImGui::TextWrapped("关闭角色 / 技能窗口后自动应用。");
        if(Read(state->skillStatus)<0)ImGui::TextWrapped("技能适配失败；续局读取失败时已暂停自动存档，请重启并先连接修改器。");
        Toggle(state,"增益技能6格",BuffCapacity);
        ImGui::TextDisabled("增益技能 %ld / %ld",Read(state->buffEquipped),Read(state->buffAllowed));
        if(Read(state->buffStatus)==2)ImGui::TextWrapped("等待卸下第3–6格增益技能，现有技能会保留。");
        if(Read(state->buffStatus)==3)ImGui::TextWrapped("关闭角色 / 技能窗口后自动应用。");
        if(Read(state->buffStatus)<0)ImGui::TextWrapped("增益技能适配失败，请关闭原生窗口后重试。");
        ImGui::SeparatorText("技能背包");
        Toggle(state,"自定义技能背包上限",SkillInventoryCapacity);ImGui::SameLine(300);
        int skillBackSlots=Read(state->skillInventorySlots);ImGui::SetNextItemWidth(150);
        if(ImGui::InputInt("##skillInventorySlots",&skillBackSlots,1,5,ImGuiInputTextFlags_EnterReturnsTrue))Set(state->skillInventorySlots,std::clamp(skillBackSlots,MinInventorySlots,MaxInventorySlots));
        ImGui::TextDisabled("6–50；背包技能 %ld / %ld",Read(state->skillInventoryOccupied),Read(state->skillInventoryAllowed));
        if(Read(state->skillInventoryStatus)==2)ImGui::TextWrapped("等待移走或用作材料消耗超额技能，现有技能会保留。");
        if(Read(state->skillInventoryStatus)==3)ImGui::TextWrapped("关闭角色 / 技能窗口后自动应用。");
        if(Read(state->skillInventoryStatus)<0)ImGui::TextWrapped("技能背包适配失败，请关闭原生窗口后重试。");
        ImGui::SeparatorText("圣器装备");
        Toggle(state,"自定义装备上限", ArtifactCapacity);ImGui::SameLine(300);
        int slots=Read(state->artifactSlots);ImGui::SetNextItemWidth(150);
        if(ImGui::InputInt("##artifactSlots",&slots,1,5,ImGuiInputTextFlags_EnterReturnsTrue))Set(state->artifactSlots,std::clamp(slots,MinArtifactSlots,MaxArtifactSlots));
        ImGui::TextWrapped("6–50；关闭角色 / 圣器窗口后应用。保留原生替换、拆卸和换位。");
        ImGui::TextDisabled("当前装备 %ld / %ld；物理格子 %ld",Read(state->artifactEquipped),Read(state->artifactAllowed),Read(state->artifactPhysicalSlots));
        if(Read(state->artifactStatus)==2)ImGui::TextColored(ImVec4(1,.7f,.3f,1),"等待拆卸超额圣器，已装备物品会保留。");
        if(Read(state->artifactStatus)==3)ImGui::TextColored(ImVec4(1,.7f,.3f,1),"请关闭游戏的角色 / 圣器选择窗口后应用。");
        if(Read(state->artifactStatus)==-1)ImGui::TextColored(ImVec4(1,.4f,.4f,1),"圣器界面适配失败，请关闭原生窗口后重试。");
        if(Read(state->artifactStatus)==-2)ImGui::TextWrapped("续局圣器读取失败，已暂停自动存档。请重启游戏、连接修改器后再继续。");
        ImGui::SeparatorText("罪恶点数");
        Toggle(state,"自定义罪恶总点数", Sin); ImGui::SameLine(300);
        int points = Read(state->sinPoints);
        ImGui::SetNextItemWidth(150);
        if (ImGui::InputInt("##sinPoints", &points, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue))
            Set(state->sinPoints, std::clamp(points, 0, MaxSinPoints));
        ImGui::TextWrapped("0–140 点；回车应用后重开罪恶分配页。各项最多 20 点。");
        ImGui::SeparatorText("本地解锁");
        Toggle(state,"全部角色", Characters); ImGui::SameLine(300); Toggle(state,"全部皮肤",Skins);
        Toggle(state,"全部武器", Weapons); ImGui::SameLine(300); Toggle(state,"全部宠物",Pets);
        ImGui::TextWrapped("开启后重新进入选择界面。仅解锁已实装资源，关闭后恢复原有归属判断。");
        ImGui::TextDisabled("本地解锁不会在商店中生成购买记录。");
        const LONG unlockStatus=Read(state->unlockStatus);
        ImGui::BeginDisabled(unlockStatus==1 || unlockStatus==2);
        if(ImGui::Button("一键解锁全部技能和圣器",ImVec2(490,36)))RequestUnlockAll(state);
        ImGui::EndDisabled();
        ImGui::TextWrapped("写入本地解锁存档；完成后重新进入图鉴 / 选择界面查看。");
        if(unlockStatus==1 || unlockStatus==2)ImGui::TextWrapped("正在等待游戏就绪并处理，请稍候……");
        if(unlockStatus==3)ImGui::TextWrapped("完成：技能 %ld 个，圣器 %ld 个（含原已解锁项）。",Read(state->unlockedSkills),Read(state->unlockedArtifacts));
        if(unlockStatus<0) {
            ImGui::TextWrapped("解锁失败：%s（代码 %ld）。",UnlockErrorLabel(Read(state->unlockError)),Read(state->unlockError));
            ImGui::TextWrapped("已处理技能 %ld / 圣器 %ld；修正原因后可重试。",Read(state->unlockedSkills),Read(state->unlockedArtifacts));
        }
        ImGui::EndChild();ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("符文重铸与掉落")) {
            ImGui::BeginChild("RuneControls",ImVec2(0,-footerReserve));
            ImGui::SeparatorText("精炼 / 重铸");
            Toggle(state,"重铸属性取当前品质最高值",RuneMaximum);
            ImGui::TextWrapped("保留品质和锁定属性，材料正常消耗。");
            ImGui::SeparatorText("挑战模式符文");
            Toggle(state,"自定义符文掉落率",ChallengeRate);
            float normal=Read(state->challengeNormalChance)/100.0f,boss=Read(state->challengeBossChance)/100.0f;
            ImGui::SetNextItemWidth(220);
            if(ImGui::InputFloat("普通怪概率 (%)",&normal,1,10,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue) && std::isfinite(normal))Set(state->challengeNormalChance,LONG(std::lround(std::clamp(normal,0.0f,100.0f)*100)));
            ImGui::SetNextItemWidth(220);
            if(ImGui::InputFloat("首领概率 (%)",&boss,1,10,"%.2f",ImGuiInputTextFlags_EnterReturnsTrue) && std::isfinite(boss))Set(state->challengeBossChance,LONG(std::lround(std::clamp(boss,0.0f,100.0f)*100)));
            Toggle(state,"增加每次掉落符文数量",ChallengeQuantity);
            int quantity=Read(state->challengeQuantity);ImGui::SetNextItemWidth(220);
            if(ImGui::InputInt("数量倍率",&quantity,1,5,ImGuiInputTextFlags_EnterReturnsTrue))Set(state->challengeQuantity,std::clamp(quantity,1,MaxChallengeQuantity));
            ImGui::TextDisabled("概率 0–100%%；数量 1–20 倍。输入后回车应用。");
            if(ImGui::CollapsingHeader("说明与数量限制")) {
                ImGui::TextWrapped("精炼数值和重铸新属性取当前品质最高档位，保持颜色和锁定属性。原生通常为普通怪 0.02%%、首领 1%%，保留品质权重和掉落资格。");
                ImGui::TextWrapped("先成功掉落，再生成额外符文。待结算达到 2000 个时停止额外生成；背包溢出仍按原生规则分解。");
                ImGui::TextDisabled("属性调整 %lld 次；额外符文 %lld 个",Read64(state->runeMaxDraws),Read64(state->challengeExtraRunes));
            }
            ImGui::EndChild();ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("所有货币")) {
            ImGui::BeginChild("WalletControls",ImVec2(0,-footerReserve));
            ImGui::TextWrapped("选择货币，输入目标余额，再点击应用。仅记住输入值，下次连接不会自动修改余额。");
            int selected=std::clamp(int(Read(state->walletSelection)),0,WalletCount-1);
            ImGui::SetNextItemWidth(490);
            if(ImGui::BeginCombo("##wallet",Wallets[selected].label)) {
                for(int i=0;i<WalletCount;++i) {
                    if(ImGui::Selectable(Wallets[i].label,i==selected)) {Set(state->walletSelection,i);selected=i;}
                    if(i==selected)ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if(Read(state->walletReadIndex)==selected && Read(state->walletReadStatus)==1)ImGui::Text("当前余额：%ld",Read(state->walletBalance));
            else ImGui::TextDisabled("等待游戏读取余额（本局金币需进入关卡）");
            if(Read(state->walletReadIndex)==selected && Read(state->walletReadStatus)==1 && Read(state->walletBalance)<0)
                ImGui::TextWrapped("检测到负余额，可直接输入正常目标值并应用修复。");
            int64_t target=Read(state->walletTargets[selected]);const int64_t step=1,fastStep=100;ImGui::SetNextItemWidth(300);
            if(ImGui::InputScalar("目标余额",ImGuiDataType_S64,&target,&step,&fastStep,"%lld",ImGuiInputTextFlags_EnterReturnsTrue))Set(state->walletTargets[selected],LONG(std::clamp<int64_t>(target,0,INT_MAX)));
            ImGui::TextDisabled("范围 0–2,147,483,647；输入后回车确认。");
            const LONG status=Read(state->walletStatus);
            ImGui::BeginDisabled(status==1 || status==2);
            if(ImGui::Button("应用所选货币余额",ImVec2(300,36)))RequestWallet(state);
            ImGui::EndDisabled();
            if(status==1 || status==2)ImGui::TextWrapped("正在游戏主线程处理……");
            if(status==3 && ValidWallet(Read(state->walletRequestIndex)))ImGui::TextWrapped("已应用：%s → %ld",Wallets[Read(state->walletRequestIndex)].label,Read(state->walletRequestTarget));
            if(status<0)ImGui::TextWrapped("应用失败：%s",WalletErrorLabel(Read(state->walletError)));
            ImGui::TextWrapped("通过游戏原生接口更新并写入本地存档；手动余额不叠加领取倍率。金币仅属于本局，活动币按年份分别保存，过期活动不会因此重新开放。");
            ImGui::EndChild();ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        }
        ImGui::Separator();
        if (ImGui::Button("全部关闭  F8",ImVec2(235,36))) Disable(state);
        ImGui::SameLine(); if (ImGui::Button("隐藏面板  F2",ImVec2(235,36))) open=false;
        if(ImGui::CollapsingHeader("运行统计")) {
        ImGui::TextDisabled("拦截受伤 %lld 次   调整伤害 %lld 次", Read64(state->godHits), Read64(state->damageHits));
        ImGui::TextDisabled("金币 %ld → %ld   奖励 %ld → %ld",Read(state->lastGoldBefore),Read(state->lastGoldAfter),Read(state->lastRewardBefore),Read(state->lastRewardAfter));
        ImGui::TextDisabled("本地解锁判断 %lld 次",Read64(state->unlockHits));
        ImGui::TextDisabled("补充冲刺次数 %lld 次   补充跳跃次数 %lld 次",Read64(state->dashRefills),Read64(state->jumpRefills));
        }
    }
    ImGui::End(); if (!open) Set(state->panelVisible, 0);
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
