#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <vector>
#include <string>
#include <optional>
#include <cwchar>
#include <iterator>

namespace ds::launcher {
struct Process {
    DWORD pid = 0;
    ULONGLONG started = 0;
    std::filesystem::path executable;
    bool SameInstance(const Process& other) const {
        return pid == other.pid && started == other.started &&
            _wcsicmp(executable.c_str(), other.executable.c_str()) == 0;
    }
};
// Read identity from a live handle, not just a PID that Windows can reuse.
inline bool Identify(HANDLE handle, Process& result, const wchar_t* image = L"dungeonslasher.exe") {
    if (!handle || handle == INVALID_HANDLE_VALUE || WaitForSingleObject(handle, 0) != WAIT_TIMEOUT) return false;
    wchar_t executable[32768]; DWORD size = DWORD(std::size(executable));
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!QueryFullProcessImageNameW(handle, 0, executable, &size) ||
        !GetProcessTimes(handle, &created, &exited, &kernel, &user)) return false;
    std::filesystem::path path(std::wstring(executable, size));
    if (_wcsicmp(path.filename().c_str(), image) != 0 || WaitForSingleObject(handle, 0) != WAIT_TIMEOUT) return false;
    result = {GetProcessId(handle), (ULONGLONG(created.dwHighDateTime) << 32) | created.dwLowDateTime, std::move(path)};
    return result.pid != 0;
}
struct Discovery {
    std::vector<Process> games;
    std::wstring error;
};
inline Discovery FindGames(const wchar_t* image = L"dungeonslasher.exe") {
    Discovery result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        result.error = L"无法扫描游戏进程，点击“重新检测”重试。"; return result;
    }
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    BOOL found = Process32FirstW(snapshot, &entry);
    if (!found && GetLastError() != ERROR_NO_MORE_FILES) result.error = L"无法读取游戏进程列表。";
    for (; found; found = Process32NextW(snapshot, &entry)) {
        // A terminated Steam bootstrap can remain in the snapshot with no
        // threads while other programs hold handles to it. OpenProcess may
        // return ACCESS_DENIED for it, which must not block a live game.
        if (!entry.cntThreads || _wcsicmp(entry.szExeFile, image) != 0) continue;
        HANDLE candidate = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, entry.th32ProcessID);
        if (!candidate) {
            if (GetLastError() == ERROR_ACCESS_DENIED) result.error = L"无法读取游戏进程。请让游戏和修改器使用相同权限运行，再点击“重新检测”。";
            continue;
        }
        Process identity;
        if (Identify(candidate, identity, image)) result.games.push_back(std::move(identity));
        else if (WaitForSingleObject(candidate, 0) == WAIT_TIMEOUT)
            result.error = L"无法确认正在运行的游戏路径，请点击“重新检测”重试。";
        CloseHandle(candidate);
    }
    if (GetLastError() != ERROR_NO_MORE_FILES) result.error = L"无法完整读取游戏进程列表，请点击“重新检测”重试。";
    CloseHandle(snapshot);
    return result;
}
// A failed connection is retried only on request or for a new game instance.
// This prevents repeating backups/injection once per scan on an unsupported build.
class AutoConnectGate {
    std::optional<Process> blocked;
public:
    void Observe(const Discovery& discovery) {
        if (!blocked || !discovery.error.empty()) return;
        for (const auto& game : discovery.games) if (game.SameInstance(*blocked)) return;
        blocked.reset();
    }
    bool CanAttempt(const Discovery& discovery) const {
        return discovery.error.empty() && discovery.games.size() == 1 &&
            (!blocked || !discovery.games.front().SameInstance(*blocked));
    }
    void Failed(const Process& process) { blocked = process; }
    void Retry() { blocked.reset(); }
};
}
