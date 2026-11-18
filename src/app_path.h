#pragma once

#include <windows.h>

#include <string>

// Full path of the running executable; empty on failure.
inline std::wstring ExecutablePath() {
    constexpr DWORD kMaxPath = 32768;
    wchar_t buffer[kMaxPath]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, kMaxPath);
    if (length == 0 || length >= kMaxPath)
        return std::wstring();
    return std::wstring(buffer, length);
}
