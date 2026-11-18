#include "killer.h"

#include <tlhelp32.h>
#include <windows.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "log.h"
#include "text.h"

namespace {

// Interactive-session processes Windows cannot lose. Killing these crashes or
// logs the user out, so they are refused regardless of settings.
const wchar_t* const kAlwaysProtected[] = {
    L"system",        L"registry",   L"smss.exe",     L"csrss.exe",   L"wininit.exe",
    L"winlogon.exe",  L"services.exe", L"lsass.exe",  L"lsaiso.exe",  L"fontdrvhost.exe",
    L"memcompression",
};

using IsProcessCriticalFn = BOOL(WINAPI*)(HANDLE, PBOOL);

IsProcessCriticalFn CriticalProcessFn() {
    static const IsProcessCriticalFn fn = []() -> IsProcessCriticalFn {
        HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        if (!kernel)
            return nullptr;
        return reinterpret_cast<IsProcessCriticalFn>(
            reinterpret_cast<void*>(GetProcAddress(kernel, "IsProcessCritical")));
    }();
    return fn;
}

std::wstring BaseName(const std::wstring& path) {
    const size_t position = path.find_last_of(L"\\/");
    return position == std::wstring::npos ? path : path.substr(position + 1);
}

bool QueryImagePath(DWORD pid, std::wstring* path) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;

    std::vector<wchar_t> buffer(1024);
    bool ok = false;
    for (;;) {
        DWORD size = static_cast<DWORD>(buffer.size());
        if (QueryFullProcessImageNameW(process, 0, buffer.data(), &size)) {
            path->assign(buffer.data(), size);
            ok = true;
            break;
        }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || buffer.size() >= 65536)
            break;
        buffer.resize(buffer.size() * 2);
    }

    CloseHandle(process);
    return ok;
}

bool IsAlwaysProtected(const std::wstring& processName) {
    const std::wstring lowered = text::ToLower(processName);
    for (const wchar_t* entry : kAlwaysProtected) {
        if (lowered == entry)
            return true;
    }
    return false;
}

bool IsCriticalProcess(DWORD pid) {
    const IsProcessCriticalFn fn = CriticalProcessFn();
    if (!fn)
        return false;

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;

    BOOL critical = FALSE;
    const BOOL queried = fn(process, &critical);
    CloseHandle(process);
    return queried && critical;
}

bool MatchesProtectedEntry(const std::wstring& entry, const std::wstring& processName,
                           const std::wstring& processPath, DWORD pid) {
    std::wstring value = text::ToLower(text::Trim(entry));
    if (value.empty())
        return false;

    if (value.find(L'\\') != std::wstring::npos || value.find(L'/') != std::wstring::npos)
        return !processPath.empty() && text::ToLower(processPath) == value;

    if (value.find(L'.') == std::wstring::npos)
        value += L".exe";

    if (!processName.empty() && text::ToLower(processName) == value)
        return true;

    // Entries may also be written as a bare pid for one-off protection.
    if (value.size() > 1 && value[0] == L'#' ) {
        const std::wstring digits = value.substr(1);
        return wcstoul(digits.c_str(), nullptr, 10) == pid;
    }
    return false;
}

// Breadth-first walk of the process snapshot, children before grandchildren.
std::vector<DWORD> Descendants(DWORD root) {
    std::vector<std::pair<DWORD, DWORD>> parentChild;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return {};

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            parentChild.emplace_back(entry.th32ProcessID, entry.th32ParentProcessID);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    std::vector<DWORD> result;
    std::vector<DWORD> frontier{root};
    while (!frontier.empty()) {
        std::vector<DWORD> next;
        for (DWORD parent : frontier) {
            for (const auto& pair : parentChild) {
                if (pair.second != parent || pair.first == parent || pair.first == 0)
                    continue;
                if (std::find(result.begin(), result.end(), pair.first) != result.end())
                    continue;
                result.push_back(pair.first);
                next.push_back(pair.first);
            }
        }
        frontier.swap(next);
    }
    return result;
}

bool TerminatePid(DWORD pid, DWORD* errorCode) {
    HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!process) {
        if (errorCode)
            *errorCode = GetLastError();
        return false;
    }

    const BOOL terminated = TerminateProcess(process, 1);
    if (errorCode)
        *errorCode = terminated ? ERROR_SUCCESS : GetLastError();
    CloseHandle(process);
    return terminated != FALSE;
}

}  // namespace

KillOutcome KillForegroundWindow(const std::vector<std::wstring>& protectedApps) {
    KillOutcome outcome;

    const HWND target = GetForegroundWindow();
    if (!target) {
        outcome.message = L"No foreground window to terminate.";
        return outcome;
    }
    if (target == GetShellWindow() || target == GetDesktopWindow()) {
        outcome.message = L"The desktop is not a killable window.";
        return outcome;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(target, &pid);
    outcome.pid = pid;

    std::wstring processPath;
    const bool havePath = pid > 0 && QueryImagePath(pid, &processPath);
    outcome.name = havePath ? BaseName(processPath) : (L"PID " + std::to_wstring(pid));

    if (pid == 0 || pid == 4) {
        outcome.message = L"Refusing to touch a Windows kernel process.";
        return outcome;
    }
    if (pid == GetCurrentProcessId()) {
        outcome.message = L"ForceKill does not terminate itself.";
        return outcome;
    }
    if (havePath && IsAlwaysProtected(outcome.name)) {
        outcome.message = outcome.name + L" is a Windows system process - nothing was killed.";
        return outcome;
    }
    if (IsCriticalProcess(pid)) {
        outcome.message = outcome.name + L" is marked critical by Windows - nothing was killed.";
        return outcome;
    }
    for (const std::wstring& entry : protectedApps) {
        if (MatchesProtectedEntry(entry, outcome.name, processPath, pid)) {
            outcome.message = outcome.name + L" is on your protected list - nothing was killed.";
            return outcome;
        }
        if (text::ToLower(entry) == L"forcekill.exe") {
            outcome.message = L"ForceKill does not terminate itself.";
            return outcome;
        }
    }

    for (DWORD child : Descendants(pid))
        TerminatePid(child, nullptr);

    DWORD errorCode = ERROR_SUCCESS;
    if (!TerminatePid(pid, &errorCode)) {
        if (errorCode == ERROR_ACCESS_DENIED) {
            outcome.message = outcome.name +
                              L" - access denied. Try \"Restart as administrator\" in the tray menu.";
        } else if (errorCode == ERROR_INVALID_PARAMETER) {
            outcome.message = outcome.name + L" had already exited.";
        } else {
            outcome.message = outcome.name + L" could not be terminated (error " +
                              std::to_wstring(errorCode) + L").";
        }
        return outcome;
    }

    outcome.ok = true;
    outcome.message = L"Terminated " + outcome.name + L" (PID " + std::to_wstring(pid) + L").";
    return outcome;
}
