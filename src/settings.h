#pragma once

#include <windows.h>

#include <string>
#include <vector>

struct Settings {
    UINT vk = VK_F4;
    bool ctrl = true;
    bool alt = true;
    bool shift = false;
    bool win = false;
    std::vector<std::wstring> protectedApps;
    bool reopenAfterKill = false;
};

namespace settings {

// %LOCALAPPDATA%\ForceKill, created on first use. Empty string on failure.
std::wstring Directory();
std::wstring ConfigPath();
std::vector<std::wstring> DefaultProtectedApps();

// Missing config files are created with the defaults on first load.
Settings Load();
bool Save(const Settings& value);

}  // namespace settings
