#include "settings.h"

#include <windows.h>

#include <string>
#include <vector>

#include "hotkey.h"
#include "text.h"

namespace {

constexpr wchar_t kAppFolder[] = L"ForceKill";
constexpr wchar_t kConfigFile[] = L"config.ini";
constexpr DWORD kStringBuffer = 8192;

std::wstring ReadString(const std::wstring& path, const wchar_t* section, const wchar_t* key,
                        const std::wstring& fallback) {
    std::wstring buffer(kStringBuffer, L'\0');
    GetPrivateProfileStringW(section, key, fallback.c_str(), buffer.data(), kStringBuffer, path.c_str());
    buffer.resize(wcslen(buffer.c_str()));
    return buffer;
}

int ReadInt(const std::wstring& path, const wchar_t* section, const wchar_t* key, int fallback) {
    return static_cast<int>(GetPrivateProfileIntW(section, key, fallback, path.c_str()));
}

void WriteInt(const std::wstring& path, const wchar_t* section, const wchar_t* key, int value) {
    WritePrivateProfileStringW(section, key, value ? L"1" : L"0", path.c_str());
}

}  // namespace

std::wstring settings::Directory() {
    wchar_t buffer[32768]{};
    DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, 32768);
    if (length == 0 || length >= 32768) {
        length = GetEnvironmentVariableW(L"APPDATA", buffer, 32768);
        if (length == 0 || length >= 32768)
            return std::wstring();
    }

    std::wstring directory(buffer, length);
    directory += L"\\";
    directory += kAppFolder;
    CreateDirectoryW(directory.c_str(), nullptr);
    return directory;
}

std::wstring settings::ConfigPath() {
    const std::wstring directory = Directory();
    if (directory.empty())
        return std::wstring();
    return directory + L"\\" + kConfigFile;
}

std::vector<std::wstring> settings::DefaultProtectedApps() {
    return {L"explorer.exe", L"dwm.exe"};
}

Settings settings::Load() {
    Settings value;
    value.protectedApps = DefaultProtectedApps();

    const std::wstring path = ConfigPath();
    if (path.empty())
        return value;

    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Save(value);
        return value;
    }

    value.ctrl = ReadInt(path, L"hotkey", L"ctrl", 1) != 0;
    value.alt = ReadInt(path, L"hotkey", L"alt", 1) != 0;
    value.shift = ReadInt(path, L"hotkey", L"shift", 0) != 0;
    value.win = ReadInt(path, L"hotkey", L"win", 0) != 0;

    const std::wstring keyName = ReadString(path, L"hotkey", L"key", L"F4");
    const UINT vk = hotkey::NameToVirtualKey(keyName);
    if (vk != 0)
        value.vk = vk;

    const std::wstring apps = ReadString(path, L"safety", L"protected", std::wstring());
    if (!apps.empty()) {
        value.protectedApps.clear();
        for (const std::wstring& raw : text::Split(apps, L';')) {
            const std::wstring entry = text::Trim(raw);
            if (!entry.empty())
                value.protectedApps.push_back(entry);
        }
    }

    return value;
}

bool settings::Save(const Settings& value) {
    const std::wstring path = ConfigPath();
    if (path.empty())
        return false;

    WriteInt(path, L"hotkey", L"ctrl", value.ctrl ? 1 : 0);
    WriteInt(path, L"hotkey", L"alt", value.alt ? 1 : 0);
    WriteInt(path, L"hotkey", L"shift", value.shift ? 1 : 0);
    WriteInt(path, L"hotkey", L"win", value.win ? 1 : 0);
    WritePrivateProfileStringW(L"hotkey", L"key", hotkey::VirtualKeyToName(value.vk).c_str(), path.c_str());

    std::wstring joined;
    for (size_t i = 0; i < value.protectedApps.size(); ++i) {
        if (i > 0)
            joined += L';';
        joined += value.protectedApps[i];
    }
    WritePrivateProfileStringW(L"safety", L"protected", joined.c_str(), path.c_str());
    return true;
}
