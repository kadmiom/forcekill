#include "tray.h"

#include <shellapi.h>
#include <windows.h>

#include <string>

#include "resource.h"
#include "text.h"

namespace {

bool g_iconAdded = false;
UINT g_callbackMessage = 0;

NOTIFYICONDATAW BaseData(HWND owner) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = owner;
    data.uID = 1;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = g_callbackMessage;
    data.hIcon = static_cast<HICON>(LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON)));
    return data;
}

std::wstring TipText(const std::wstring& hotkeyLabel, bool paused) {
    if (paused)
        return L"ForceKill - paused (" + hotkeyLabel + L")";
    return L"ForceKill - " + hotkeyLabel;
}

void FillTip(NOTIFYICONDATAW* data, const std::wstring& text) {
    wcsncpy(data->szTip, text.c_str(), ARRAYSIZE(data->szTip) - 1);
    data->szTip[ARRAYSIZE(data->szTip) - 1] = L'\0';
}

}  // namespace

bool tray::Add(HWND owner, UINT callbackMessage, const std::wstring& hotkeyLabel) {
    g_callbackMessage = callbackMessage;

    NOTIFYICONDATAW data = BaseData(owner);
    FillTip(&data, TipText(hotkeyLabel, false));
    if (g_iconAdded)
        return Shell_NotifyIconW(NIM_MODIFY, &data) != FALSE;

    g_iconAdded = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
    if (g_iconAdded)
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    return g_iconAdded;
}

void tray::Remove(HWND owner) {
    if (!g_iconAdded)
        return;
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = owner;
    data.uID = 1;
    Shell_NotifyIconW(NIM_DELETE, &data);
    g_iconAdded = false;
}

bool tray::UpdateTip(HWND owner, const std::wstring& hotkeyLabel, bool paused) {
    if (!g_iconAdded)
        return false;
    NOTIFYICONDATAW data = BaseData(owner);
    data.uFlags = NIF_TIP;
    FillTip(&data, TipText(hotkeyLabel, paused));
    return Shell_NotifyIconW(NIM_MODIFY, &data) != FALSE;
}

void tray::Balloon(HWND owner, const std::wstring& title, const std::wstring& text, DWORD flags) {
    if (!g_iconAdded)
        return;

    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = owner;
    data.uID = 1;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = flags;
    wcsncpy(data.szInfoTitle, text::Truncate(title, 60).c_str(), ARRAYSIZE(data.szInfoTitle) - 1);
    data.szInfoTitle[ARRAYSIZE(data.szInfoTitle) - 1] = L'\0';
    wcsncpy(data.szInfo, text::Truncate(text, 250).c_str(), ARRAYSIZE(data.szInfo) - 1);
    data.szInfo[ARRAYSIZE(data.szInfo) - 1] = L'\0';
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void tray::ShowMenu(HWND owner, bool paused, bool autostartEnabled, bool elevated,
                    const std::wstring& hotkeyLabel) {
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;

    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, (L"ForceKill - " + hotkeyLabel).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuSettings, L"Settings...");
    AppendMenuW(menu, MF_STRING, kMenuOpenLog, L"Open log file");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuPause, paused ? L"Resume hotkey" : L"Pause hotkey");
    AppendMenuW(menu, MF_STRING | (autostartEnabled ? MF_CHECKED : MF_UNCHECKED), kMenuAutostart,
                L"Start with Windows");
    AppendMenuW(menu, MF_STRING | (elevated ? MF_GRAYED : 0), kMenuElevate, L"Restart as administrator");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(owner);
    const UINT command = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, cursor.x,
                                        cursor.y, 0, owner, nullptr);
    DestroyMenu(menu);
    PostMessageW(owner, WM_NULL, 0, 0);

    if (command != 0)
        PostMessageW(owner, WM_COMMAND, command, 0);
}
