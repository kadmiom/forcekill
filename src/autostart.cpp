#include "autostart.h"

#include <windows.h>
#include <wchar.h>

#include <string>

#include "app_path.h"
#include "text.h"

namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"ForceKill";  // same value the installer manages

std::wstring StripQuotes(const std::wstring& value) {
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"')
        return value.substr(1, value.size() - 2);
    return value;
}

}  // namespace

bool autostart::IsEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;

    wchar_t buffer[32768]{};
    DWORD type = 0;
    DWORD bytes = static_cast<DWORD>((ARRAYSIZE(buffer) - 1) * sizeof(wchar_t));
    const LSTATUS status =
        RegQueryValueExW(key, kValueName, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &bytes);
    RegCloseKey(key);

    if (status != ERROR_SUCCESS || type != REG_SZ)
        return false;

    // RegQueryValueExW counts the terminator in `bytes`, so trim at the first NUL.
    const std::wstring command = StripQuotes(std::wstring(buffer, wcsnlen(buffer, ARRAYSIZE(buffer))));
    return text::ToLower(command) == text::ToLower(ExecutablePath());
}

bool autostart::SetEnabled(bool enable) {
    const std::wstring path = enable ? ExecutablePath() : std::wstring();
    if (enable && path.empty())
        return false;

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS)
        return false;

    LSTATUS status = ERROR_SUCCESS;
    if (enable) {
        const std::wstring command = L"\"" + path + L"\"";
        status = RegSetValueExW(key, kValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, kValueName);
        if (status == ERROR_FILE_NOT_FOUND)
            status = ERROR_SUCCESS;
    }

    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}
