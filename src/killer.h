#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct KillOutcome {
    bool ok = false;
    DWORD pid = 0;
    std::wstring name;
    std::wstring message;  // user-facing, shown in the tray balloon
};

// Resolves the foreground window, applies every safety guard, then terminates
// the owning process and its children.
KillOutcome KillForegroundWindow(const std::vector<std::wstring>& protectedApps);
