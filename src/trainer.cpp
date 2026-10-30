#include "protocol.h"
#include "preferences.h"
#include "profile.h"
#include "discovery.h"
#include <tlhelp32.h>
#include <shellapi.h>
#include <commctrl.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <memory>
#include <stdexcept>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <iterator>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
namespace fs = std::filesystem;
namespace {
fs::path AppDir() { wchar_t path[32768]; GetModuleFileNameW(nullptr, path, DWORD(std::size(path))); return fs::path(path).parent_path(); }
std::wstring Error(const wchar_t* action) {
    DWORD code = GetLastError(); wchar_t* message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::wstring text = action; text += L"："; text += message ? message : L"系统错误";
    if (message) LocalFree(message); return text;
}
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete; Handle& operator=(const Handle&) = delete;
};
struct Connection {
    HANDLE game = nullptr, mapping = nullptr;
    ds::Shared* data = nullptr;
    DWORD pid = 0;
    ds::launcher::Process identity;
    std::wstring error, backup;
    ~Connection() { ds::Disable(data); if (data) UnmapViewOfFile(data); if (mapping) CloseHandle(mapping); if (game) CloseHandle(game); }
};
std::wstring Sha256(const fs::path& path) {
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) throw std::runtime_error("SHA256 unavailable");
    ULONG size = 0, got = 0; BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&size), sizeof(size), &got, 0);
    std::vector<unsigned char> object(size); unsigned char digest[32];
    if (BCryptCreateHash(alg, &hash, object.data(), size, nullptr, 0, 0) < 0) { BCryptCloseAlgorithmProvider(alg,0); throw std::runtime_error("SHA256 unavailable"); }
    std::ifstream stream(path, std::ios::binary); char buffer[65536]; bool ok = bool(stream);
    while (stream) {
        stream.read(buffer, sizeof(buffer)); auto count = stream.gcount();
        if (count && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer), ULONG(count), 0) < 0) { ok = false; break; }
    }
    if (stream.bad()) ok = false;
    if (BCryptFinishHash(hash, digest, sizeof(digest), 0) < 0) ok = false;
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) throw std::runtime_error("Cannot read game files");
    wchar_t hex[65]; for (size_t i = 0; i < 32; ++i) swprintf_s(hex + i*2, 65-i*2, L"%02x", digest[i]);
    return hex;
}
bool Verify(const fs::path& dir, std::wstring& error) {
    try {
        if (!fs::is_regular_file(dir/L"dungeonslasher.exe") || !fs::is_regular_file(AppDir()/L"DungeonSlasher.Runtime.dll")) {
            error = L"找不到游戏或运行模块。请保留 EXE 和 DLL 在同一文件夹，并检查游戏目录。"; return false;
        }
        if (Sha256(dir/L"GameAssembly.dll") != ds::AssemblySha256 ||
            Sha256(dir/L"dungeonslasher_Data"/L"il2cpp_data"/L"Metadata"/L"global-metadata.dat") != ds::MetadataSha256) {
            error = L"游戏版本尚未适配。当前支持 Steam Build 26261290；未注入或修改游戏。"; return false;
        }
    } catch (...) { error = L"无法读取游戏文件，请检查目录和文件权限。"; return false; }
    return true;
}
std::wstring EscapeReg(std::wstring value) {
    std::wstring result; for (wchar_t c : value) { if (c == L'\\' || c == L'"') result += L'\\'; result += c; } return result;
}
std::wstring Backup(DWORD pid) {
    wchar_t local[32768]; DWORD count = GetEnvironmentVariableW(L"USERPROFILE", local, DWORD(std::size(local)));
    if (!count || count >= std::size(local)) throw std::runtime_error("User profile unavailable");
    fs::path source = fs::path(local)/L"AppData"/L"LocalLow"/L"nspgames"/L"dungeonslasher";
    SYSTEMTIME time{}; GetLocalTime(&time); wchar_t name[80];
    swprintf_s(name, L"%04u%02u%02u-%02u%02u%02u-PID%lu", time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,pid);
    fs::path destination = AppDir()/L"backups"/name; fs::create_directories(destination);
    if (fs::exists(source)) {
        for (auto& entry : fs::recursive_directory_iterator(source)) {
            if (!entry.is_regular_file() || entry.path().extension() == L".log") continue;
            fs::path out = destination/L"LocalLow"/fs::relative(entry.path(),source);
            fs::create_directories(out.parent_path()); fs::copy_file(entry.path(),out,fs::copy_options::skip_existing);
        }
    }
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\nspgames\\dungeonslasher",0,KEY_READ,&key) == ERROR_SUCCESS) {
        std::wstring text = L"Windows Registry Editor Version 5.00\r\n\r\n[HKEY_CURRENT_USER\\Software\\nspgames\\dungeonslasher]\r\n";
        DWORD maxName = 0,maxData = 0;
        LONG status = RegQueryInfoKeyW(key,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,&maxName,&maxData,nullptr,nullptr);
        if (status != ERROR_SUCCESS) { RegCloseKey(key); throw std::runtime_error("Cannot back up PlayerPrefs"); }
        std::vector<wchar_t> names(maxName+2); std::vector<BYTE> bytes(std::max<DWORD>(maxData,1));
        for (DWORD i=0;;++i) {
            DWORD len=DWORD(names.size()),size=DWORD(bytes.size()),type=0;
            status = RegEnumValueW(key,i,names.data(),&len,nullptr,&type,bytes.data(),&size);
            if (status == ERROR_NO_MORE_ITEMS) break;
            if (status != ERROR_SUCCESS) { RegCloseKey(key); throw std::runtime_error("Cannot back up PlayerPrefs"); }
            wchar_t typeText[32]; swprintf_s(typeText,L"=hex(%x):",type);
            text += len ? L"\""+EscapeReg(std::wstring(names.data(),len))+L"\"" : L"@";
            text += type == REG_BINARY ? L"=hex:" : typeText;
            for(DWORD j=0;j<size;++j) { wchar_t item[4]; swprintf_s(item,L"%02x",bytes[j]); if(j && j%24!=0)text+=L","; text+=item; if((j+1)%24==0 && j+1<size)text+=L",\\\r\n  "; }
            text+=L"\r\n";
        }
        RegCloseKey(key); std::ofstream out(destination/L"PlayerPrefs.reg",std::ios::binary);
        const unsigned char bom[2]={0xff,0xfe}; out.write(reinterpret_cast<const char*>(bom),2);
        out.write(reinterpret_cast<const char*>(text.data()),std::streamsize(text.size()*sizeof(wchar_t)));
        if(!out)throw std::runtime_error("Cannot write PlayerPrefs backup");
    }
    std::ofstream note(destination/L"backup-info.txt"); note << "Backup made before trainer connection. Game PID: " << pid << "\nLocalLow: nspgames/dungeonslasher (excludes .log)\nPlayerPrefs: HKCU/Software/nspgames/dungeonslasher\nRestore only while the game is fully closed.\n";
    if (!note) throw std::runtime_error("Cannot write backup");
    return destination.wstring();
}
bool HasModule(DWORD pid, const wchar_t* module) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid));
    MODULEENTRY32W entry{}; entry.dwSize=sizeof(entry);
    if(Module32FirstW(snapshot.value,&entry))do { if(_wcsicmp(entry.szModule,module)==0)return true; }while(Module32NextW(snapshot.value,&entry));
    return false;
}
bool HasRuntime(DWORD pid) { return HasModule(pid,L"DungeonSlasher.Runtime.dll"); }
LPTHREAD_START_ROUTINE RemoteLoadLibrary(DWORD pid) {
    auto local=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"LoadLibraryW"); HMODULE owner=nullptr;
    if(!local || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(local),&owner))return nullptr;
    wchar_t path[32768]; GetModuleFileNameW(owner,path,DWORD(std::size(path))); auto name=fs::path(path).filename().wstring();
    uintptr_t offset=reinterpret_cast<uintptr_t>(local)-reinterpret_cast<uintptr_t>(owner);
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid)); MODULEENTRY32W entry{}; entry.dwSize=sizeof(entry);
    if(Module32FirstW(snapshot.value,&entry))do { if(_wcsicmp(entry.szModule,name.c_str())==0 && offset<entry.modBaseSize)return reinterpret_cast<LPTHREAD_START_ROUTINE>(entry.modBaseAddr+offset); }while(Module32NextW(snapshot.value,&entry));
    return nullptr;
}
std::unique_ptr<Connection> Connect(const ds::launcher::Process& target) {
    auto result=std::make_unique<Connection>();
    result->identity=target; result->pid=target.pid;
    result->game=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_CREATE_THREAD|PROCESS_VM_OPERATION|PROCESS_VM_READ|PROCESS_VM_WRITE|SYNCHRONIZE,FALSE,result->pid);
    if(!result->game) { result->error=Error(L"无法连接游戏进程"); return result; }
    ds::launcher::Process actual;
    if(!ds::launcher::Identify(result->game,actual) || !actual.SameInstance(target)) { result->error=L"检测到的游戏进程已经退出或变化。正在等待新的游戏进程。"; return result; }
    const fs::path dir=actual.executable.parent_path();
    if(!Verify(dir,result->error))return result;
    // Allow opening the trainer while Steam/Unity is still starting. Do not
    // inject into a bootstrap process that never loads the game assembly.
    bool assemblyReady=false;
    for(int i=0;i<240;++i) {
        if(WaitForSingleObject(result->game,0)!=WAIT_TIMEOUT) { result->error=L"游戏启动进程已退出，等待新的游戏进程。"; return result; }
        if(HasModule(result->pid,L"GameAssembly.dll")) { assemblyReady=true; break; }
        Sleep(250);
    }
    if(!assemblyReady) { result->error=L"游戏启动超时。进入主菜单后点击“重新检测”重试。"; return result; }
    try { result->backup=Backup(result->pid); } catch (...) { result->error=L"存档备份未完成。请检查修改器文件夹是否可写，再连接。"; return result; }
    bool loaded=HasRuntime(result->pid); wchar_t name[96]; ds::MappingName(name,result->pid);
    if(loaded) {
        Handle existing(OpenFileMappingW(FILE_MAP_READ,FALSE,name));
        if(!existing.value) { result->error=L"游戏仍加载着旧版修改器。请完全退出游戏及旧修改器，再启动本修复版。"; return result; }
    }
    result->mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(ds::Shared),name);
    DWORD existed=GetLastError();
    if(!result->mapping) { result->error=Error(L"创建控制通道失败"); return result; }
    result->data=static_cast<ds::Shared*>(MapViewOfFile(result->mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(ds::Shared)));
    if(!result->data) { result->error=Error(L"打开控制通道失败"); return result; }
    if(existed!=ERROR_ALREADY_EXISTS) {
        ZeroMemory(result->data,sizeof(ds::Shared)); result->data->magic=ds::Magic; result->data->version=ds::Version;
        result->data->size=sizeof(ds::Shared); result->data->gamePid=result->pid;
        ds::Set(result->data->damageMultiplier,2000); ds::Set(result->data->goldMultiplier,2000); ds::Set(result->data->rewardMultiplier,2000);
        ds::Set(result->data->runePowderMultiplier,2000);ds::Set(result->data->diamondMultiplier,2000);
        ds::Set(result->data->sinPoints,ds::MaxSinPoints);
        ds::Set(result->data->artifactSlots,12);
        ds::Set(result->data->skillInventorySlots,12);ds::Set(result->data->inventorySlots,12);ds::Set(result->data->skillSlots,12);
    } else if(result->data->magic!=ds::Magic || result->data->version!=ds::Version || result->data->size!=sizeof(ds::Shared) || result->data->gamePid!=result->pid) {
        UnmapViewOfFile(result->data);result->data=nullptr; result->error=L"控制通道版本不一致，请完全重启游戏。"; return result;
    }
    ds::Disable(result->data); ds::Set(result->data->ownerPid,LONG(GetCurrentProcessId())); ds::Set(result->data->clientHeartbeat,LONG(GetTickCount()));
    if(!loaded) {
        auto library=fs::absolute(AppDir()/L"DungeonSlasher.Runtime.dll").wstring(); SIZE_T bytes=(library.size()+1)*sizeof(wchar_t),written=0;
        void* remote=VirtualAllocEx(result->game,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        if(!remote) { result->error=Error(L"分配模块路径失败"); return result; }
        auto load=RemoteLoadLibrary(result->pid);
        if(!load || !WriteProcessMemory(result->game,remote,library.c_str(),bytes,&written) || written!=bytes) {
            result->error=Error(L"写入模块路径失败"); VirtualFreeEx(result->game,remote,0,MEM_RELEASE); return result;
        }
        Handle thread(CreateRemoteThread(result->game,nullptr,0,load,remote,0,nullptr));
        if(!thread.value) { result->error=Error(L"加载运行模块失败"); VirtualFreeEx(result->game,remote,0,MEM_RELEASE); return result; }
        DWORD wait=WaitForSingleObject(thread.value,15000);
        // On timeout, retain the allocation until the game exits: the remote thread may still read it.
        if(wait!=WAIT_OBJECT_0) { result->error=L"游戏加载模块超时。请完全重启游戏后重试。"; return result; }
        VirtualFreeEx(result->game,remote,0,MEM_RELEASE);
        if(!HasRuntime(result->pid)) { result->error=L"游戏未加载运行模块，请检查 DLL 是否完整。"; return result; }
    }
    for(int i=0;i<150;++i) {
        ds::Set(result->data->clientHeartbeat,LONG(GetTickCount()));
        LONG state=ds::Read(result->data->state);
        if(state==ds::Ready && ds::Fresh(GetTickCount(),DWORD(ds::Read(result->data->runtimeHeartbeat))))return result;
        if(state==ds::Failed) { result->error=result->data->message; return result; }
        if(WaitForSingleObject(result->game,0)==WAIT_OBJECT_0) { result->error=L"游戏已退出。"; return result; }
        Sleep(100);
    }
    result->error=L"运行模块未就绪。请进入主菜单，完全重启游戏后重试。"; return result;
}

constexpr ds::Feature FeatureIds[]={ds::God,ds::Damage,ds::Gold,ds::Reward,ds::Characters,ds::Skins,ds::Weapons,ds::Pets,ds::Sin,ds::InfiniteDash,ds::InfiniteJump,ds::ArtifactCapacity,ds::ArtifactInventoryCapacity,ds::SkillCapacity};
HWND window=nullptr,pathEdit=nullptr,processLabel=nullptr,connectButton=nullptr,startButton=nullptr,statusLabel=nullptr,statsLabel=nullptr,backupLabel=nullptr;
HWND toggles[std::size(FeatureIds)]{}, multiplierEdits[3]{}, panelButton=nullptr, sinEdit=nullptr, sinApplyButton=nullptr;
ds::Preferences remembered;
bool preferencesLive=false;
HWND unlockButton=nullptr;
LONG lastUnlockStatus=0;
HWND artifactEdit=nullptr,artifactApplyButton=nullptr,artifactHint=nullptr;
HWND capacityEdits[2]{},capacityButtons[2]{};int syncedCapacity[2]{-1,-1};
HFONT normalFont=nullptr,headingFont=nullptr; HBRUSH background=nullptr;
std::unique_ptr<Connection> connection;
bool busy=false, hotkeysRegistered=false;
ds::launcher::AutoConnectGate autoConnect;
ULONGLONG lastScan=0;
std::wstring statusText=L"正在自动查找游戏进程，无需设置目录。连接后恢复上次功能和参数。";
int syncedFactors[3]{};
int syncedCurrencyFactors[2]{-1,-1};
int syncedSkillInventory=-1;
int syncedSinPoints = -1;
int syncedArtifactSlots = -1;
HWND Control(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int width,int height,int id=0) {
    HWND handle=CreateWindowExW(cls==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,width,height,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(handle,WM_SETFONT,reinterpret_cast<WPARAM>(normalFont),TRUE); return handle;
}
void Status(const std::wstring& text) { if(statusText==text)return; statusText=text; if(statusLabel)SetWindowTextW(statusLabel,text.c_str()); }
void RememberConnection() {
    if(!preferencesLive || !connection || !connection->data)return;
    auto* state=connection->data;
    if(ds::Read(state->state)!=ds::Ready || !ds::Fresh(GetTickCount(),DWORD(ds::Read(state->runtimeHeartbeat))) ||
       !ds::Fresh(GetTickCount(),DWORD(ds::Read(state->clientHeartbeat))))return;
    auto next=ds::SnapshotPreferences(state);
    if(!(next==remembered)) {
        if(ds::WritePreferences((AppDir()/L"trainer.ini").c_str(),next))remembered=next;
        else Status(L"设置保存失败，请将修改器解压到可写入的目录后重试。");
    }
}
void Off(bool remember=true) {
    if(!remember) { RememberConnection();preferencesLive=false; }
    if(connection)ds::Disable(connection->data);
    if(remember) {
        if(preferencesLive)RememberConnection();
        else {auto next=remembered;next.enabled=0;if(ds::WritePreferences((AppDir()/L"trainer.ini").c_str(),next))remembered=next;}
    }
    for(auto toggle:toggles)SendMessageW(toggle,BM_SETCHECK,BST_UNCHECKED,0);
}
void EnableControls(bool enabled) { for(auto toggle:toggles)EnableWindow(toggle,enabled); for(auto edit:multiplierEdits)EnableWindow(edit,enabled); EnableWindow(panelButton,enabled); EnableWindow(unlockButton,enabled);EnableWindow(sinEdit,enabled); EnableWindow(sinApplyButton,enabled);EnableWindow(artifactEdit,enabled);EnableWindow(artifactApplyButton,enabled);for(int i=0;i<2;++i){EnableWindow(capacityEdits[i],enabled);EnableWindow(capacityButtons[i],enabled);} }
void BeginConnect(const ds::launcher::Process& target) {
    Off(false); connection.reset(); EnableControls(false); busy=true;
    EnableWindow(connectButton,FALSE); EnableWindow(startButton,FALSE);
    SetWindowTextW(processLabel,(L"已自动识别游戏  ·  PID "+std::to_wstring(target.pid)).c_str());
    SetWindowTextW(pathEdit,target.executable.parent_path().c_str());
    Status(L"正在核对版本、等待游戏初始化、备份存档并自动连接……");
    try {
        std::thread([target,hwnd=window]() {
            std::unique_ptr<Connection> result;
            try { result=Connect(target); }
            catch (...) { result=std::make_unique<Connection>(); result->identity=target; result->pid=target.pid; result->error=L"连接失败，请检查文件与权限。"; }
            auto* raw=result.release(); if(!PostMessageW(hwnd,WM_APP+1,0,reinterpret_cast<LPARAM>(raw)))delete raw;
        }).detach();
    } catch (...) {
        busy=false; EnableWindow(connectButton,TRUE); EnableWindow(startButton,TRUE); autoConnect.Failed(target);
        Status(L"无法创建连接线程。点击“重新检测”重试。");
    }
}
void ScanGames(bool retry=false) {
    if(busy)return;
    if(connection && connection->data) {
        if(retry && WaitForSingleObject(connection->game,0)==WAIT_TIMEOUT &&
            ds::Fresh(GetTickCount(),DWORD(ds::Read(connection->data->runtimeHeartbeat)))) {
            ds::Set(connection->data->panelCommand,2); Status(L"已连接；游戏内面板已请求打开。F2 显示或隐藏。");
        }
        return;
    }
    if(retry)autoConnect.Retry();
    auto discovery=ds::launcher::FindGames(); lastScan=GetTickCount64(); autoConnect.Observe(discovery);
    if(!discovery.error.empty()) { Status(discovery.error); return; }
    if(discovery.games.empty()) {
        SetWindowTextW(processLabel,L"游戏进程：等待启动（自动检测）"); SetWindowTextW(pathEdit,L"游戏启动后自动识别安装目录");
        Status(L"等待游戏启动。进入游戏后将自动连接并打开面板，无需再次点击。"); return;
    }
    if(discovery.games.size()!=1) {
        SetWindowTextW(processLabel,L"检测到多个游戏进程"); SetWindowTextW(pathEdit,L"只保留一个游戏进程后自动识别");
        Status(L"检测到多个游戏进程。只保留一个后将自动连接。"); return;
    }
    if(autoConnect.CanAttempt(discovery))BeginConnect(discovery.games.front());
}
void ReturnToGame(HWND hwnd,DWORD pid) {
    const bool foreground=GetForegroundWindow()==hwnd;
    struct MainWindow { DWORD pid; HWND handle=nullptr; LONGLONG area=0; } selected{pid};
    if(foreground)EnumWindows([](HWND candidate,LPARAM parameter)->BOOL {
        auto* selected=reinterpret_cast<MainWindow*>(parameter); DWORD candidatePid=0;
        GetWindowThreadProcessId(candidate,&candidatePid); RECT client{};
        if(candidatePid==selected->pid && IsWindowVisible(candidate) && !GetWindow(candidate,GW_OWNER) && GetClientRect(candidate,&client)) {
            LONGLONG area=LONGLONG(client.right-client.left)*(client.bottom-client.top);
            if(area>selected->area) { selected->handle=candidate; selected->area=area; }
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&selected));
    if(selected.handle) { if(IsIconic(selected.handle))ShowWindow(selected.handle,SW_RESTORE); SetForegroundWindow(selected.handle); }
    ShowWindow(hwnd,SW_MINIMIZE);
}
bool ApplySin() {
    if (!connection || !connection->data || ds::Read(connection->data->state)!=ds::Ready) return false;
    wchar_t text[64]; GetWindowTextW(sinEdit,text,64); wchar_t* end=nullptr;
    long long points=wcstoll(text,&end,10); while(end && iswspace(*end))++end;
    if (end==text || (end && *end) || points<0 || points>ds::MaxSinPoints) {
        ds::SetFeature(connection->data,ds::Sin,false); SendMessageW(toggles[8],BM_SETCHECK,BST_UNCHECKED,0);
        Status(L"罪恶总点数请输入 0–140 的整数。"); return false;
    }
    ds::Set(connection->data->sinPoints,LONG(points));
    RememberConnection();
    Status(L"罪恶总点数已应用。勾选自定义后，重开游戏的罪恶分配页。"); return true;
}
bool ApplyArtifactSlots() {
    if(!connection || !connection->data || ds::Read(connection->data->state)!=ds::Ready)return false;
    wchar_t text[64];GetWindowTextW(artifactEdit,text,64);wchar_t* end=nullptr;
    long long slots=wcstoll(text,&end,10);while(end && iswspace(*end))++end;
    if(end==text || (end && *end) || slots<ds::MinArtifactSlots || slots>ds::MaxArtifactSlots) {
        Status(L"圣器装备上限请输入 6–50 的整数。");return false;
    }
    ds::Set(connection->data->artifactSlots,LONG(slots));
    RememberConnection();
    Status(L"圣器上限已设置；勾选启用，关闭角色 / 圣器窗口后应用。超额需先拆卸。");return true;
}
bool ApplyExtraCapacity(int index) {
    if(!connection||!connection->data||ds::Read(connection->data->state)!=ds::Ready)return false;
    wchar_t text[64];GetWindowTextW(capacityEdits[index],text,64);wchar_t* end=nullptr;
    long value=wcstol(text,&end,10);
    const int maximum=index?ds::MaxSkillSlots:ds::MaxInventorySlots;
    if(end==text||*end||value<6||value>maximum){Status(index?L"普通技能装备上限请输入 6–50 的整数。":L"圣器背包上限请输入 6–50 的整数。");return false;}
    ds::Set(index?connection->data->skillSlots:connection->data->inventorySlots,LONG(value));
    RememberConnection();
    Status(L"上限已设置；勾选启用，关闭原生窗口后应用。缩小前先移走超额项。");return true;
}
bool Apply() {
    if(!connection || !connection->data || ds::Read(connection->data->state)!=ds::Ready)return false;
    int factors[3];
    for(int i=0;i<3;++i) {
        wchar_t text[64]; GetWindowTextW(multiplierEdits[i],text,64); wchar_t* end=nullptr; double value=wcstod(text,&end);
        while(end && iswspace(*end))++end;
        if(end==text || (end && *end) || !std::isfinite(value) || value<0.1 || value>1000) {
            Off(); Status(L"倍率请输入 0.1 至 1000 的数字。为避免误操作，所有功能已关闭。"); return false;
        }
        factors[i]=int(std::lround(value*1000));
    }
    ds::Set(connection->data->damageMultiplier,factors[0]); ds::Set(connection->data->goldMultiplier,factors[1]); ds::Set(connection->data->rewardMultiplier,factors[2]);
    LONG enabled=ds::Read(connection->data->enabled);
    RememberConnection();

    Status(enabled?L"已应用。功能即时生效；奖励需在领取结算前开启。":L"已连接，所有功能已关闭。"); return true;
}
void Tick() {
    if(!connection || !connection->data) { if(!busy && GetTickCount64()-lastScan>=1000)ScanGames(); return; }
    auto* data=connection->data;
    if(WaitForSingleObject(connection->game,0)==WAIT_OBJECT_0) {
        Off(false); connection.reset(); EnableControls(false); Status(L"游戏已退出，等待下次启动后自动连接。"); SetWindowTextW(statsLabel,L"等待自动连接");
        SetWindowTextW(processLabel,L"游戏进程：等待启动（自动检测）"); SetWindowTextW(pathEdit,L"游戏启动后自动识别安装目录");
        lastScan=0; return;
    }
    if(!ds::Fresh(GetTickCount(),DWORD(ds::Read(data->runtimeHeartbeat)))) {
        Off(false); EnableControls(false); Status(L"运行模块心跳中断，修改已关闭。请重启游戏。"); return;
    }
    ds::Set(data->clientHeartbeat,LONG(GetTickCount()));
    RememberConnection();
    // Both windows edit the same atomic state. Reflect in-game edits without
    // replacing a number the user is currently typing in the launcher.
    LONG flags=ds::Read(data->enabled);
    for(size_t i=0;i<std::size(FeatureIds);++i)SendMessageW(toggles[i],BM_SETCHECK,(flags&FeatureIds[i])?BST_CHECKED:BST_UNCHECKED,0);
    int points=ds::Read(data->sinPoints);
    if (syncedSinPoints!=points) {
        std::wstring number=std::to_wstring(points);
        if(GetFocus()!=sinEdit) { SetWindowTextW(sinEdit,number.c_str()); syncedSinPoints=points; }
    }
    int slots=ds::Read(data->artifactSlots);
    if(syncedArtifactSlots!=slots) {
        std::wstring number=std::to_wstring(slots);
        if(GetFocus()!=artifactEdit){SetWindowTextW(artifactEdit,number.c_str());syncedArtifactSlots=slots;}
    }
    int factors[]={ds::Read(data->damageMultiplier),ds::Read(data->goldMultiplier),ds::Read(data->rewardMultiplier)};
    int skillBackpack=ds::Read(data->skillInventorySlots);
    syncedSkillInventory=skillBackpack;
    int currencyFactors[]={ds::Read(data->runePowderMultiplier),ds::Read(data->diamondMultiplier)};
    for(int i=0;i<2;++i)if(syncedCurrencyFactors[i]!=currencyFactors[i]) {
        wchar_t number[32];swprintf_s(number,L"%.3f",currencyFactors[i]/1000.0);
        syncedCurrencyFactors[i]=currencyFactors[i];
    }
    for(int i=0;i<2;++i){int value=ds::Read(i?data->skillSlots:data->inventorySlots);if(value!=syncedCapacity[i]){
        std::wstring number=std::to_wstring(value);if(GetFocus()!=capacityEdits[i]){SetWindowTextW(capacityEdits[i],number.c_str());syncedCapacity[i]=value;}
        }}
    for(int i=0;i<3;++i)if(syncedFactors[i]!=factors[i]) {
        wchar_t number[32];swprintf_s(number,L"%.3f",factors[i]/1000.0);
        if(GetFocus()!=multiplierEdits[i]) { SetWindowTextW(multiplierEdits[i],number); syncedFactors[i]=factors[i]; }
    }
    wchar_t text[400]; swprintf_s(text,L"无敌拦截 %lld 次    伤害调整 %lld 次    金币获取 %lld 次    奖励计算 %lld 次\r\n最近一次：伤害 %ld → %ld    金币 %ld → %ld    奖励 %ld → %ld",
        ds::Read64(data->godHits),ds::Read64(data->damageHits),ds::Read64(data->goldHits),ds::Read64(data->rewardHits),
        ds::Read(data->lastDamageBefore),ds::Read(data->lastDamageAfter),ds::Read(data->lastGoldBefore),ds::Read(data->lastGoldAfter),ds::Read(data->lastRewardBefore),ds::Read(data->lastRewardAfter));
    SetWindowTextW(statsLabel,text);
    const LONG unlockStatus=ds::Read(data->unlockStatus);
    EnableWindow(unlockButton,preferencesLive && unlockStatus!=1 && unlockStatus!=2);
    if(unlockStatus!=lastUnlockStatus) {
        lastUnlockStatus=unlockStatus;
        if(unlockStatus==1 || unlockStatus==2)Status(L"正在等待游戏就绪并解锁全部技能和圣器……");
        if(unlockStatus==3)Status(L"已解锁技能 "+std::to_wstring(ds::Read(data->unlockedSkills))+L" 个、圣器 "+std::to_wstring(ds::Read(data->unlockedArtifacts))+L" 个并保存。重新进入图鉴 / 选择界面查看。");
        if(unlockStatus<0) {
            wchar_t reason[256]{};const LONG error=ds::Read(data->unlockError);
            MultiByteToWideChar(CP_UTF8,0,ds::UnlockErrorLabel(error),-1,reason,256);
            Status(L"解锁失败："+std::wstring(reason)+L"（代码 "+std::to_wstring(error)+L"）；技能 "+
                std::to_wstring(ds::Read(data->unlockedSkills))+L" / 圣器 "+std::to_wstring(ds::Read(data->unlockedArtifacts))+L"。");
        }
    }
    int artifactState=ds::Read(data->artifactStatus);
    if(artifactState==2)SetWindowTextW(artifactHint,L"等待拆卸超额圣器；保持当前容量，原物品不会删除。");
    else if(artifactState==3)SetWindowTextW(artifactHint,L"等待关闭角色 / 圣器选择窗口，随后自动应用新上限。");
    else if(artifactState==-2)SetWindowTextW(artifactHint,L"续局读取失败，已暂停自动存档；重启并连接修改器后继续。");
    else if(artifactState<0)SetWindowTextW(artifactHint,L"圣器适配失败；关闭原生窗口重试，仍失败请重启游戏。");
    else {wchar_t line[128];swprintf_s(line,L"6–50；当前装备 %ld / %ld。缩小前拆卸超额项，关闭原生窗口。",ds::Read(data->artifactEquipped),ds::Read(data->artifactAllowed));SetWindowTextW(artifactHint,line);}
}
LRESULT CALLBACK WindowProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam) {
    switch(message) {
    case WM_CREATE: {
        window=hwnd;
        remembered=ds::ReadPreferences((AppDir()/L"trainer.ini").c_str());
        HWND title=Control(L"STATIC",L"DUNGEON SLASHER",0,28,24,560,36); SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(headingFont),TRUE);
        Control(L"STATIC",L"独立版 r20：新角色 / 技能无冷却 / 静默重试 · Build 26261290",0,30,65,580,24);
        processLabel=Control(L"STATIC",L"游戏进程：自动检测 PID 和安装目录",0,30,105,590,24);
        pathEdit=Control(L"EDIT",L"游戏启动后自动识别安装目录",ES_AUTOHSCROLL|ES_READONLY|WS_TABSTOP,30,132,590,28,101);
        connectButton=Control(L"BUTTON",L"重新检测",BS_PUSHBUTTON|WS_TABSTOP,30,175,140,34,102);
        startButton=Control(L"BUTTON",L"启动游戏",BS_PUSHBUTTON|WS_TABSTOP,185,175,140,34,103);
        Control(L"BUTTON",L"全部关闭  F8",BS_PUSHBUTTON|WS_TABSTOP,340,175,140,34,104);
        Control(L"BUTTON",L"使用说明",BS_PUSHBUTTON|WS_TABSTOP,495,175,125,34,105);
        const wchar_t* labels[]={L"无敌",L"伤害倍率",L"金币获取倍率",L"结算奖励倍率"};
        const wchar_t* hints[]={L"抵消攻击、事件伤害及持续伤害",L"放大怪物实际扣血，并同步常规伤害浮字",L"只放大新增金币，消费保持正常",L"三种石头和本局宝石；已取消 100 万限幅"};
        for(int i=0;i<4;++i) {
            int y=222+i*44; toggles[i]=Control(L"BUTTON",labels[i],BS_AUTOCHECKBOX|WS_TABSTOP,30,y,300,26,110+i);
            Control(L"STATIC",hints[i],0,50,y+27,525,18);
            if(i>0) { multiplierEdits[i-1]=Control(L"EDIT",L"2",ES_AUTOHSCROLL|WS_TABSTOP,435,y,125,28,120+i); Control(L"STATIC",L"倍",0,575,y+3,30,23);
                const wchar_t* keys[]={L"Damage",L"Gold",L"Reward"}; wchar_t value[64]; GetPrivateProfileStringW(L"Multipliers",keys[i-1],L"2",value,64,(AppDir()/L"trainer.ini").c_str()); SetWindowTextW(multiplierEdits[i-1],value); }
        }
        Control(L"BUTTON",L"应用倍率",BS_PUSHBUTTON|WS_TABSTOP,435,403,185,32,106);
        Control(L"STATIC",L"倍率 0.1–1000；改数字后点击“应用倍率”。",0,30,407,390,27);
        toggles[8]=Control(L"BUTTON",L"自定义罪恶总点数",BS_AUTOCHECKBOX|WS_TABSTOP,30,441,300,26,118);
        sinEdit=Control(L"EDIT",L"140",ES_AUTOHSCROLL|WS_TABSTOP,435,441,80,28,130);
        wchar_t sinValue[64]; GetPrivateProfileStringW(L"Sin",L"Total",L"140",sinValue,64,(AppDir()/L"trainer.ini").c_str()); SetWindowTextW(sinEdit,sinValue);
        sinApplyButton=Control(L"BUTTON",L"应用点数",BS_PUSHBUTTON|WS_TABSTOP,530,441,90,28,108);
        Control(L"STATIC",L"0–140；开启后重开罪恶分配页，各项最多 20 点。",0,50,471,570,25);
        toggles[9]=Control(L"BUTTON",L"无限冲刺",BS_AUTOCHECKBOX|WS_TABSTOP,30,505,280,26,119);
        toggles[10]=Control(L"BUTTON",L"无限跳跃",BS_AUTOCHECKBOX|WS_TABSTOP,330,505,280,26,120);
        toggles[11]=Control(L"BUTTON",L"自定义圣器装备上限",BS_AUTOCHECKBOX|WS_TABSTOP,30,544,370,26,131);
        artifactEdit=Control(L"EDIT",L"12",ES_AUTOHSCROLL|WS_TABSTOP,435,544,80,28,132);
        wchar_t artifactValue[64];GetPrivateProfileStringW(L"Artifacts",L"Slots",L"12",artifactValue,64,(AppDir()/L"trainer.ini").c_str());SetWindowTextW(artifactEdit,artifactValue);
        artifactApplyButton=Control(L"BUTTON",L"应用上限",BS_PUSHBUTTON|WS_TABSTOP,530,544,90,28,109);
        const wchar_t* capacityLabels[]={L"自定义圣器背包上限",L"自定义普通技能装备上限"};
        for(int i=0;i<2;++i){int y=582+i*38;
            toggles[12+i]=Control(L"BUTTON",capacityLabels[i],BS_AUTOCHECKBOX|WS_TABSTOP,30,y,370,26,133+i*2);
            capacityEdits[i]=Control(L"EDIT",L"12",ES_AUTOHSCROLL|WS_TABSTOP,435,y,80,28,134+i*2);
            wchar_t value[64];GetPrivateProfileStringW(i?L"Skills":L"ArtifactInventory",L"Slots",L"12",value,64,(AppDir()/L"trainer.ini").c_str());SetWindowTextW(capacityEdits[i],value);
            capacityButtons[i]=Control(L"BUTTON",L"应用上限",BS_PUSHBUTTON|WS_TABSTOP,530,y,90,28,137+i);
        }
        artifactHint=Control(L"STATIC",L"装备 / 背包 6–50；增益技能6格在 F2 面板设置。",0,50,654,570,36);
        panelButton=Control(L"BUTTON",L"打开 F2 面板（技能无冷却 / 同步重试 / 其他设置）",BS_PUSHBUTTON|WS_TABSTOP,30,699,590,34,107);
        const wchar_t* unlocks[]={L"本地解锁全部角色",L"本地解锁全部皮肤",L"本地解锁全部武器",L"本地解锁全部宠物"};
        for(int i=0;i<4;++i)toggles[4+i]=Control(L"BUTTON",unlocks[i],BS_AUTOCHECKBOX|WS_TABSTOP,30+(i%2)*300,749+(i/2)*32,280,26,114+i);
        unlockButton=Control(L"BUTTON",L"一键解锁全部技能和圣器",BS_PUSHBUTTON|WS_TABSTOP,30,815,290,29,139);
        Control(L"STATIC",L"写入本地存档；重进图鉴查看。",0,330,818,290,25);
        statusLabel=Control(L"STATIC",statusText.c_str(),0,30,847,590,40);
        statsLabel=Control(L"STATIC",L"等待连接",0,30,891,590,40);
        backupLabel=Control(L"STATIC",L"连接前自动备份存档及 PlayerPrefs。",0,30,936,590,26);
        EnableControls(false); SetTimer(hwnd,1,250,nullptr); return 0;
    }
    case WM_COMMAND: {
        int id=LOWORD(wParam);
        if(id==102 && !busy)ScanGames(true);
        else if(id==103 && !busy) {
            auto discovery=ds::launcher::FindGames();
            if(!discovery.error.empty())Status(discovery.error);
            else if(!discovery.games.empty())ScanGames(true);
            else {
                auto launched=ShellExecuteW(hwnd,L"open",L"steam://rungameid/2712460",nullptr,nullptr,SW_SHOWNORMAL);
                Status(reinterpret_cast<INT_PTR>(launched)>32?L"已请求 Steam 启动游戏，随后将自动连接并打开面板。":L"无法请求 Steam 启动，请从 Steam 库启动游戏；修改器会自动检测。");
            }
        } else if(id==104) { Off(); Status(L"所有功能已关闭，游戏恢复正常计算。"); }
        else if(id==105) { ShellExecuteW(hwnd,L"open",(AppDir()/L"使用说明.txt").c_str(),nullptr,nullptr,SW_SHOWNORMAL); }
        else if(id==106)Apply();
        else if(id==108)ApplySin();
        else if(id==109)ApplyArtifactSlots();
        else if(id==137||id==138)ApplyExtraCapacity(id-137);
        else if(id==139 && connection && connection->data) {ds::RequestUnlockAll(connection->data);Tick();}
        else if(id==107 && connection && connection->data)ds::Set(connection->data->panelCommand,1);
        else if(((id>=110 && id<=120) || id==131 || id==133 || id==135) && connection && connection->data) {
            int index=id==131?11:id==133?12:id==135?13:id-110; bool on=SendMessageW(toggles[index],BM_GETCHECK,0,0)==BST_CHECKED;
            bool valid=index==8?(!on || ApplySin()):index==11?(!on || ApplyArtifactSlots()):index>=12?(!on||ApplyExtraCapacity(index-12)):true;
            if(valid){ds::SetFeature(connection->data,FeatureIds[index],on);RememberConnection();}
        }
        return 0;
    }
    case WM_APP+1: {
        busy=false; EnableWindow(connectButton,TRUE); EnableWindow(startButton,TRUE); connection.reset(reinterpret_cast<Connection*>(lParam));
        if(!connection->error.empty()) { autoConnect.Failed(connection->identity); Status(connection->error+L"\r\n处理后点击“重新检测”重试。"); connection.reset(); EnableControls(false); }
        else { autoConnect.Retry(); EnableControls(true); std::wstring text=L"已自动连接 PID "+std::to_wstring(connection->pid)+L"；已恢复上次开关和参数，F2 显示或隐藏面板。";
            if(ds::Read(connection->data->overlayState)<0)text=L"功能已连接，游戏内面板安装失败。请重启游戏。"; Status(text);
            SetWindowTextW(backupLabel,(L"备份："+connection->backup).c_str());
            syncedFactors[0]=syncedFactors[1]=syncedFactors[2]=0;
            syncedSinPoints=-1;
            syncedArtifactSlots=-1;
            syncedCapacity[0]=syncedCapacity[1]=-1;
            syncedCurrencyFactors[0]=syncedCurrencyFactors[1]=-1;
            syncedSkillInventory=-1;
            ds::RestorePreferences(connection->data,remembered);
            preferencesLive=true;
            lastUnlockStatus=0;
            Status(text);
            ds::Set(connection->data->panelCommand,2);
            if(!hotkeysRegistered) { hotkeysRegistered=RegisterHotKey(hwnd,8,MOD_NOREPEAT,VK_F8)!=FALSE; }
            Tick();
            if(connection && ds::Read(connection->data->overlayState)>=0)ReturnToGame(hwnd,connection->pid); }
        return 0;
    }
    case WM_APP+2: ScanGames(); return 0;
    case WM_HOTKEY: {
        if(wParam==8) { Off(); Status(L"F8：所有功能已关闭。"); }
        return 0;
    }
    case WM_TIMER: Tick(); return 0;
    case WM_CTLCOLORSTATIC: SetBkColor(reinterpret_cast<HDC>(wParam),RGB(245,247,251)); SetTextColor(reinterpret_cast<HDC>(wParam),RGB(36,47,63)); return reinterpret_cast<LRESULT>(background);
    case WM_CTLCOLORBTN: return reinterpret_cast<LRESULT>(background);
    case WM_CLOSE: if(busy) { Status(L"连接正在进行，完成后可以关闭窗口。"); return 0; } Off(false); DestroyWindow(hwnd); return 0;
    case WM_DESTROY: Off(false); connection.reset(); KillTimer(hwnd,1); UnregisterHotKey(hwnd,8); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,message,wParam,lParam);
}
void Output(const std::wstring& text) {
    HANDLE out=GetStdHandle(STD_OUTPUT_HANDLE); if(!out || out==INVALID_HANDLE_VALUE)return;
    int count=WideCharToMultiByte(CP_UTF8,0,text.c_str(),int(text.size()),nullptr,0,nullptr,nullptr); std::string bytes(count,'\0');
    WideCharToMultiByte(CP_UTF8,0,text.c_str(),int(text.size()),bytes.data(),count,nullptr,nullptr); DWORD written; WriteFile(out,bytes.data(),DWORD(bytes.size()),&written,nullptr);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    int argc=0; auto** args=CommandLineToArgvW(GetCommandLineW(),&argc); fs::path dir, report; bool check=false,probe=false;
    for(int i=1;i<argc;++i) { if(wcscmp(args[i],L"--check")==0)check=true; else if(wcscmp(args[i],L"--probe")==0)probe=true; else if(wcscmp(args[i],L"--game-dir")==0 && i+1<argc)dir=args[++i]; else if(wcscmp(args[i],L"--report")==0 && i+1<argc)report=args[++i]; }
    LocalFree(args);
    if(check) {
        std::wstring error, automaticTarget;
        if(dir.empty()) {
            auto discovery=ds::launcher::FindGames();
            if(!discovery.error.empty())error=discovery.error;
            else if(discovery.games.size()==1) {
                dir=discovery.games.front().executable.parent_path();
                automaticTarget=L" PID="+std::to_wstring(discovery.games.front().pid)+L"; directory="+dir.wstring()+L";";
            }
            else error=discovery.games.empty()?L"没有正在运行的游戏。请启动游戏，或使用 --check --game-dir 指定只读检查目录。":L"检测到多个游戏进程，请只保留一个。";
        }
        bool ok=error.empty() && Verify(dir,error);
        Output(ok?L"OK:"+automaticTarget+L" Steam Build 26261290; assembly + metadata SHA256 matched; runtime present.\n":error+L"\n"); return ok?0:1;
    }
    Handle single(CreateMutexW(nullptr,TRUE,L"Local\\DungeonSlasherTrainer.Controller.v1"));
    if(GetLastError()==ERROR_ALREADY_EXISTS) { if(!probe)MessageBoxW(nullptr,L"修改器已经运行，请使用已打开的窗口。",L"DUNGEON SLASHER",MB_OK); return 2; }
    if(probe) {
        auto discovery=ds::launcher::FindGames(); auto result=std::make_unique<Connection>();
        if(!discovery.error.empty())result->error=discovery.error;
        else if(discovery.games.size()!=1)result->error=discovery.games.empty()?L"请先启动游戏。":L"检测到多个游戏进程，请只保留一个。";
        else {
            std::error_code pathError;
            if(!dir.empty() && !fs::equivalent(dir/L"dungeonslasher.exe",discovery.games.front().executable,pathError))result->error=L"正在运行的游戏与 --game-dir 不一致，或指定的目录不可读取。";
            else result=Connect(discovery.games.front());
        }
        std::wstring text=result->error.empty()?L"OK: "+std::to_wstring(std::size(ds::Targets))+L" game hooks + "+std::to_wstring(std::size(ds::Helpers))+L" helpers + "+std::to_wstring(std::size(ds::FontCallSites)+std::size(ds::RewardMetadataSites)+std::size(ds::ArtifactSaveSites)+std::size(ds::ArtifactInventoryCalls)+std::size(ds::SkillEquipmentCalls)+std::size(ds::GemSaveSites)+std::size(ds::SkillNormalCalls)+std::size(ds::SkillInventoryCalls))+L" call sites + 67 skill operands verified; available="+std::to_wstring(ds::Read(result->data->available))+L"; enabled=0; overlay="+std::to_wstring(ds::Read(result->data->overlayState))+L"; PID="+std::to_wstring(result->pid)+L"; directory="+result->identity.executable.parent_path().wstring():result->error;
        Output(text+L"\n"); if(!report.empty()) { std::ofstream file(report,std::ios::binary); int size=WideCharToMultiByte(CP_UTF8,0,text.c_str(),int(text.size()),nullptr,0,nullptr,nullptr); std::string utf8(size,'\0'); WideCharToMultiByte(CP_UTF8,0,text.c_str(),int(text.size()),utf8.data(),size,nullptr,nullptr); file<<utf8<<"\n"; }
        return result->error.empty()?0:3;
    }
    SetProcessDPIAware(); INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
    normalFont=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    headingFont=CreateFontW(-28,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    background=CreateSolidBrush(RGB(245,247,251));
    WNDCLASSW cls{}; cls.lpfnWndProc=WindowProc; cls.hInstance=instance; cls.lpszClassName=L"DungeonSlasherTrainerWindow";
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hIcon=LoadIconW(nullptr,IDI_APPLICATION); cls.hbrBackground=background; RegisterClassW(&cls);
    RECT rect{0,0,650,972}; DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX; AdjustWindowRect(&rect,style,FALSE);
    HWND hwnd=CreateWindowExW(0,cls.lpszClassName,L"DUNGEON SLASHER 单机修改器",style,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);
    if(!hwnd)return 4; ShowWindow(hwnd,show); UpdateWindow(hwnd); PostMessageW(hwnd,WM_APP+2,0,0); MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0) { if(!IsDialogMessageW(hwnd,&message)) { TranslateMessage(&message); DispatchMessageW(&message); } }
    DeleteObject(normalFont); DeleteObject(headingFont); DeleteObject(background); return int(message.wParam);
}
