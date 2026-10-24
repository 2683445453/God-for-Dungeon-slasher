#include "discovery.h"
#include <cassert>
#include <iostream>
#include <cstring>
#pragma comment(lib,"advapi32.lib")

namespace {
namespace fs = std::filesystem;
using ds::launcher::Process;
struct DebugPrivilegeScope {
    HANDLE token=nullptr;
    TOKEN_PRIVILEGES previous{};
    DebugPrivilegeScope() {
        // Hosted Windows runners may enable SeDebugPrivilege, bypassing the
        // empty DACL fixture. Disable it only on this test process and restore
        // its original state afterward; production privilege handling stays intact.
        assert(OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&token));
        TOKEN_PRIVILEGES disabled{};disabled.PrivilegeCount=1;
        assert(LookupPrivilegeValueW(nullptr,SE_DEBUG_NAME,&disabled.Privileges[0].Luid));
        DWORD length=sizeof(previous);SetLastError(ERROR_SUCCESS);
        assert(AdjustTokenPrivileges(token,FALSE,&disabled,sizeof(previous),&previous,&length));
        const DWORD error=GetLastError();assert(error==ERROR_SUCCESS||error==ERROR_NOT_ALL_ASSIGNED);
    }
    ~DebugPrivilegeScope() {
        if(previous.PrivilegeCount)assert(AdjustTokenPrivileges(token,FALSE,&previous,0,nullptr,nullptr));
        CloseHandle(token);
    }
};
struct Fixture {
    HANDLE event=nullptr;
    PROCESS_INFORMATION child{};
    Fixture(const fs::path& image,int index,HANDLE job) {
        std::wstring name=L"Local\\DungeonSlasherDiscoveryTest-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(index);
        event=CreateEventW(nullptr,TRUE,FALSE,name.c_str()); assert(event);
        std::wstring command=L"\""+image.wstring()+L"\" --discovery-fixture "+name;
        STARTUPINFOW startup{}; startup.cb=sizeof(startup);
        assert(CreateProcessW(image.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,nullptr,&startup,&child));
        assert(AssignProcessToJobObject(job,child.hProcess));
        assert(ResumeThread(child.hThread)!=DWORD(-1));
    }
    void Stop() { assert(SetEvent(event)); assert(WaitForSingleObject(child.hProcess,5000)==WAIT_OBJECT_0); }
    ~Fixture() { CloseHandle(child.hThread); CloseHandle(child.hProcess); CloseHandle(event); }
};
void DenyNewProcessHandles(HANDLE process) {
    // Keep our original handle, but reproduce ACCESS_DENIED for later scans.
    // This descriptor is applied only to the test's own child process.
    ACL empty{}; SECURITY_DESCRIPTOR descriptor{};
    assert(InitializeAcl(&empty,sizeof(empty),ACL_REVISION));
    assert(InitializeSecurityDescriptor(&descriptor,SECURITY_DESCRIPTOR_REVISION));
    assert(SetSecurityDescriptorDacl(&descriptor,TRUE,&empty,FALSE));
    assert(SetKernelObjectSecurity(process,DACL_SECURITY_INFORMATION,&descriptor));
    HANDLE denied=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,GetProcessId(process));
    assert(!denied && GetLastError()==ERROR_ACCESS_DENIED);
}
}
int RunDiscoveryFixture(const char* eventName) {
    std::wstring name; for(const char* at=eventName;*at;++at)name+=wchar_t(*at);
    HANDLE event=OpenEventW(SYNCHRONIZE,FALSE,name.c_str()); if(!event)return 7;
    DWORD wait=WaitForSingleObject(event,15000); CloseHandle(event); return wait==WAIT_OBJECT_0?0:8;
}
void DiscoveryTests() {
    using namespace ds::launcher;
    DebugPrivilegeScope debugPrivilege;
    wchar_t ownImage[32768]; assert(GetModuleFileNameW(nullptr,ownImage,DWORD(std::size(ownImage))));
    const fs::path root=fs::path(ownImage).parent_path();
    const std::wstring name=L"DSDiscoveryFixture-"+std::to_wstring(GetCurrentProcessId())+L".exe";
    const fs::path a=root/(L"自动识别 路径 A "+std::to_wstring(GetCurrentProcessId()))/name,
        b=root/(L"自动识别 路径 B "+std::to_wstring(GetCurrentProcessId()))/name;
    fs::create_directories(a.parent_path()); fs::create_directories(b.parent_path());
    fs::copy_file(ownImage,a,fs::copy_options::overwrite_existing); fs::copy_file(ownImage,b,fs::copy_options::overwrite_existing);
    HANDLE job=CreateJobObjectW(nullptr,nullptr); assert(job);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    assert(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)));
    AutoConnectGate gate;
    auto empty=FindGames(name.c_str()); assert(empty.games.empty() && empty.error.empty() && !gate.CanAttempt(empty));
    {
        Fixture first(a,1,job);
        Process identity; assert(Identify(first.child.hProcess,identity,name.c_str()));
        assert(identity.pid==first.child.dwProcessId && identity.started && fs::equivalent(identity.executable,a));
        assert(fs::equivalent(identity.executable.parent_path(),a.parent_path()));
        Process wrong; assert(!Identify(first.child.hProcess,wrong,L"dungeonslasher.exe"));
        auto one=FindGames(name.c_str()); assert(one.error.empty() && one.games.size()==1 && one.games[0].SameInstance(identity));
        assert(gate.CanAttempt(one)); gate.Failed(identity); assert(!gate.CanAttempt(one));
        gate.Observe(one); assert(!gate.CanAttempt(one));
        Discovery inaccessible{{},L"access denied"}; gate.Observe(inaccessible); assert(!gate.CanAttempt(inaccessible) && !gate.CanAttempt(one));
        gate.Retry(); assert(gate.CanAttempt(one)); gate.Failed(identity);
        // Even the same PID is a new target if its creation time or image changes.
        auto reused=one; ++reused.games[0].started; assert(gate.CanAttempt(reused) && !identity.SameInstance(reused.games[0]));
        auto moved=one; moved.games[0].executable=b; assert(!identity.SameInstance(moved.games[0]));
        {
            Fixture second(b,2,job);
            auto two=FindGames(name.c_str()); assert(two.error.empty() && two.games.size()==2 && !gate.CanAttempt(two));
            gate.Observe(two); assert(!gate.CanAttempt(one));
            DenyNewProcessHandles(first.child.hProcess);
            auto protectedLive=FindGames(name.c_str());
            assert(!protectedLive.error.empty() && protectedLive.games.size()==1 && !gate.CanAttempt(protectedLive));
            first.Stop(); assert(!Identify(first.child.hProcess,wrong,name.c_str()));
            auto surviving=FindGames(name.c_str()); assert(surviving.error.empty() && surviving.games.size()==1);
            assert(surviving.games[0].pid==second.child.dwProcessId && fs::equivalent(surviving.games[0].executable,b));
            gate.Observe(surviving); assert(gate.CanAttempt(surviving));
            gate.Failed(surviving.games[0]); second.Stop();
        }
    }
    auto exited=FindGames(name.c_str()); assert(exited.error.empty() && exited.games.empty());
    gate.Observe(exited); assert(gate.CanAttempt(Discovery{{Process{99,1,a}},L""}));
    CloseHandle(job);
    // Remove only the two exact fixture files, then their now-empty directories.
    fs::remove(a); fs::remove(b);
    std::error_code cleanupError; fs::remove(a.parent_path(),cleanupError); fs::remove(b.parent_path(),cleanupError);
    std::cout<<"PASS: real Windows PID/image discovery in Unicode + space paths, unique/multiple processes, zero-thread ACCESS_DENIED remnants skipped, inaccessible live processes still rejected, creation-time identity, failure retry suppression, explicit retry and reconnect after process exit; isolated fixtures only\n";
}
