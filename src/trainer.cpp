#include "protocol.h"
#include "preferences.h"
#include "profile.h"
#include "discovery.h"
#include <tlhelp32.h>
#include <shellapi.h>
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
    bool handedOff = false;
    ~Connection() { if (!handedOff) ds::Disable(data); if (data) UnmapViewOfFile(data); if (mapping) CloseHandle(mapping); if (game) CloseHandle(game); }
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
            error = L"游戏版本尚未适配。当前支持 Steam Build 26534205；未注入或修改游戏。"; return false;
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
    if(!assemblyReady) { result->error=L"游戏启动超时，请进入主菜单后重新运行修改器。"; return result; }
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
    if (loaded && ds::Read(result->data->state)==ds::Ready &&
        ds::Read(result->data->ownerPid)==LONG(result->pid)) {
        result->handedOff=true;
        return result;
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

void Output(const std::wstring& text) {
    HANDLE out=GetStdHandle(STD_OUTPUT_HANDLE); if(!out || out==INVALID_HANDLE_VALUE)return;
    int count=WideCharToMultiByte(CP_UTF8,0,text.c_str(),int(text.size()),nullptr,0,nullptr,nullptr); std::string bytes(count,'\0');
    WideCharToMultiByte(CP_UTF8,0,text.c_str(),int(text.size()),bytes.data(),count,nullptr,nullptr); DWORD written; WriteFile(out,bytes.data(),DWORD(bytes.size()),&written,nullptr);
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int) {
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
        Output(ok?L"OK:"+automaticTarget+L" Steam Build 26534205; assembly + metadata SHA256 matched; runtime present.\n":error+L"\n"); return ok?0:1;
    }
    Handle single(CreateMutexW(nullptr,TRUE,L"Local\\DungeonSlasherTrainer.Controller.v1"));
    if(GetLastError()==ERROR_ALREADY_EXISTS) { if(!probe)MessageBoxW(nullptr,L"修改器正在注入，请稍候。",L"DUNGEON SLASHER",MB_OK); return 2; }
    if(probe) {
        auto discovery=ds::launcher::FindGames(); auto result=std::make_unique<Connection>();
        if(!discovery.error.empty())result->error=discovery.error;
        else if(discovery.games.size()!=1)result->error=discovery.games.empty()?L"请先启动游戏。":L"检测到多个游戏进程，请只保留一个。";
        else {
            std::error_code pathError;
            if(!dir.empty() && !fs::equivalent(dir/L"dungeonslasher.exe",discovery.games.front().executable,pathError))result->error=L"正在运行的游戏与 --game-dir 不一致，或指定的目录不可读取。";
            else result=Connect(discovery.games.front());
        }
        std::wstring text=result->error.empty()?L"OK: "+std::to_wstring(std::size(ds::Targets))+L" game hooks + "+std::to_wstring(std::size(ds::Helpers))+L" helpers + "+std::to_wstring(std::size(ds::FontCallSites)+std::size(ds::RewardMetadataSites)+std::size(ds::ArtifactSaveSites)+std::size(ds::ArtifactInventoryCalls)+std::size(ds::SkillEquipmentCalls)+std::size(ds::GemSaveSites)+std::size(ds::SkillNormalCalls)+std::size(ds::SkillInventoryCalls))+L" call sites + 67 skill operands verified; available="+std::to_wstring(ds::Read(result->data->available))+L"; enabled="+std::to_wstring(ds::Read(result->data->enabled))+L"; overlay="+std::to_wstring(ds::Read(result->data->overlayState))+L"; PID="+std::to_wstring(result->pid)+L"; directory="+result->identity.executable.parent_path().wstring():result->error;
        Output(text+L"\n"); if(!report.empty()) { std::ofstream file(report,std::ios::binary); int size=WideCharToMultiByte(CP_UTF8,0,text.c_str(),int(text.size()),nullptr,0,nullptr,nullptr); std::string utf8(size,'\0'); WideCharToMultiByte(CP_UTF8,0,text.c_str(),int(text.size()),utf8.data(),size,nullptr,nullptr); file<<utf8<<"\n"; }
        return result->error.empty()?0:3;
    }
    auto discovery=ds::launcher::FindGames();
    std::wstring error=discovery.error;
    if(error.empty() && discovery.games.size()!=1)
        error=discovery.games.empty()?L"请先启动游戏并进入主菜单，再运行修改器。":L"请只保留一个游戏进程。";
    std::unique_ptr<Connection> result;
    if(error.empty()) {
        try { result=Connect(discovery.games.front()); error=result->error; }
        catch (...) { error=L"注入失败，请检查文件与权限。"; }
    }
    if(error.empty() && ds::Read(result->data->overlayState)<0)
        error=L"游戏内面板初始化失败，请重启游戏后重试。";
    if(!error.empty()) { MessageBoxW(nullptr,error.c_str(),L"DUNGEON SLASHER",MB_OK|MB_ICONERROR); return 3; }
    if(!result->handedOff) {
        ds::RestorePreferences(result->data,ds::ReadPreferences((AppDir()/L"trainer.ini").c_str()));
        // Transfer control to the resident worker before releasing launcher resources.
        ds::Set(result->data->ownerPid,LONG(result->pid));
        result->handedOff=true;
    }
    ds::Set(result->data->panelCommand,2);
    return 0;
}
