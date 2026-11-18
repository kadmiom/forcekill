#include <windows.h>
#include <shellapi.h>

#include <string>

#include "app_path.h"
#include "autostart.h"
#include "hotkey.h"
#include "killer.h"
#include "log.h"
#include "resource.h"
#include "settings.h"
#include "settings_ui.h"
#include "tray.h"

namespace {

constexpr wchar_t kWindowClass[] = L"ForceKillHiddenWindow";
constexpr wchar_t kMutexName[] = L"Local\\ForceKill-SingleInstance";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kShowSettingsMessage = WM_APP + 2;

Settings g_settings;
HWND g_window = nullptr;
HANDLE g_instanceMutex = nullptr;
UINT g_taskbarCreatedMessage = 0;
bool g_hotkeyRegistered = false;
bool g_paused = false;

bool IsElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;

    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    const bool elevated = GetTokenInformation(token, TokenElevation, &elevation, size, &size) &&
                          elevation.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
}

void UpdateTrayTip() {
    tray::UpdateTip(g_window, hotkey::Describe(g_settings), g_paused);
}

void UnbindHotkey() {
    if (!g_hotkeyRegistered)
        return;
    UnregisterHotKey(g_window, hotkey::kHotkeyId);
    g_hotkeyRegistered = false;
}

// Rebinds unconditionally: the previous registration is released first.
bool BindHotkey(const Settings& value) {
    UnbindHotkey();
    if (!RegisterHotKey(g_window, hotkey::kHotkeyId, hotkey::Modifiers(value), value.vk))
        return false;
    g_hotkeyRegistered = true;
    return true;
}

void WarnHotkeyUnavailable() {
    const std::wstring label = hotkey::Describe(g_settings);
    applog::Write(L"RegisterHotKey failed for %ls", label.c_str());
    tray::Balloon(g_window, L"Hotkey unavailable",
                  L"Could not register " + label +
                      L". Another app probably owns that combination - choose another in Settings.",
                  NIIF_WARNING);
}

void OpenSettings() {
    Settings candidate = g_settings;
    if (!settings_ui::Show(g_window, candidate))
        return;

    if (!BindHotkey(candidate)) {
        if (!g_paused)
            BindHotkey(g_settings);
        MessageBoxW(g_window,
                    (L"Windows refused " + hotkey::Describe(candidate) +
                     L": another program already uses that combination.\r\n\r\nNothing was changed.")
                        .c_str(),
                    L"ForceKill", MB_OK | MB_ICONERROR);
        UpdateTrayTip();
        return;
    }

    if (g_paused)
        UnbindHotkey();

    g_settings = candidate;
    settings::Save(g_settings);
    applog::Write(L"Settings saved: hotkey=%ls protected=%lu", hotkey::Describe(g_settings).c_str(),
               static_cast<unsigned long>(g_settings.protectedApps.size()));
    UpdateTrayTip();
}

void TogglePause() {
    g_paused = !g_paused;
    if (g_paused) {
        UnbindHotkey();
        applog::Write(L"Hotkey paused.");
    } else {
        if (!BindHotkey(g_settings))
            WarnHotkeyUnavailable();
        applog::Write(L"Hotkey resumed.");
    }
    UpdateTrayTip();
}

void ToggleAutostart() {
    const bool enable = !autostart::IsEnabled();
    if (!autostart::SetEnabled(enable)) {
        MessageBoxW(g_window,
                    enable ? L"Could not add ForceKill to the per-user Run key."
                           : L"Could not remove ForceKill from the per-user Run key.",
                    L"ForceKill", MB_OK | MB_ICONERROR);
        return;
    }

    applog::Write(L"Autostart %ls.", enable ? L"enabled" : L"disabled");
    tray::Balloon(g_window, L"ForceKill",
                  enable ? L"ForceKill will start when you sign in."
                         : L"ForceKill will no longer start when you sign in.",
                  NIIF_INFO);
}

void RestartElevated() {
    const std::wstring path = ExecutablePath();
    if (path.empty())
        return;

    const HINSTANCE result = ShellExecuteW(nullptr, L"runas", path.c_str(), L"--takeover", nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) > 32) {
        applog::Write(L"Restarting with administrator rights.");
        DestroyWindow(g_window);
    } else {
        MessageBoxW(g_window, L"Elevation was cancelled.", L"ForceKill", MB_OK | MB_ICONWARNING);
    }
}

void HandleKill() {
    const KillOutcome outcome = KillForegroundWindow(g_settings.protectedApps);
    applog::Write(L"Kill: ok=%ls pid=%lu %ls", outcome.ok ? L"yes" : L"no", static_cast<unsigned long>(outcome.pid),
               outcome.message.c_str());
    tray::Balloon(g_window, outcome.ok ? L"ForceKill" : L"Nothing killed", outcome.message,
                  outcome.ok ? NIIF_INFO : NIIF_WARNING);
}

void HandleCommand(HWND hwnd, UINT id) {
    switch (id) {
        case tray::kMenuSettings:
            OpenSettings();
            break;
        case tray::kMenuOpenLog:
            ShellExecuteW(nullptr, L"open", applog::Path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case tray::kMenuPause:
            TogglePause();
            break;
        case tray::kMenuAutostart:
            ToggleAutostart();
            break;
        case tray::kMenuElevate:
            RestartElevated();
            break;
        case tray::kMenuExit:
            DestroyWindow(hwnd);
            break;
        default:
            break;
    }
}

void HandleTrayEvent(HWND hwnd, UINT event) {
    switch (event) {
        case WM_LBUTTONDBLCLK:
            OpenSettings();
            break;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            tray::ShowMenu(hwnd, g_paused, autostart::IsEnabled(), IsElevated(), hotkey::Describe(g_settings));
            break;
        default:
            break;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (g_taskbarCreatedMessage != 0 && message == g_taskbarCreatedMessage) {
        tray::Add(hwnd, kTrayMessage, hotkey::Describe(g_settings));
        UpdateTrayTip();
        return 0;
    }

    switch (message) {
        case WM_HOTKEY:
            if (wParam == hotkey::kHotkeyId)
                HandleKill();
            return 0;
        case kShowSettingsMessage:
            OpenSettings();
            return 0;
        case kTrayMessage:
            HandleTrayEvent(hwnd, LOWORD(lParam));
            return 0;
        case WM_COMMAND:
            HandleCommand(hwnd, LOWORD(wParam));
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool RegisterAppWindowClass(HINSTANCE instance) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = static_cast<HICON>(LoadIconW(instance, MAKEINTRESOURCEW(IDI_APPICON)));
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.lpszClassName = kWindowClass;
    return RegisterClassExW(&windowClass) != FALSE;
}

// Takes the single-instance lock. A "--takeover" launch (elevation restart)
// waits for the old instance to release it instead of deferring to it.
bool AcquireInstance() {
    const bool takeover = wcsstr(GetCommandLineW(), L"--takeover") != nullptr;

    for (int attempt = 0; attempt < 100; ++attempt) {
        HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
        if (!mutex)
            return true;
        if (GetLastError() != ERROR_ALREADY_EXISTS) {
            g_instanceMutex = mutex;
            return true;
        }

        CloseHandle(mutex);
        if (!takeover)
            return false;
        Sleep(100);
    }

    applog::Write(L"Previous instance did not release the lock; continuing anyway.");
    return true;
}

int Shutdown(int code) {
    UnbindHotkey();
    tray::Remove(g_window);
    if (g_instanceMutex) {
        ReleaseMutex(g_instanceMutex);
        CloseHandle(g_instanceMutex);
        g_instanceMutex = nullptr;
    }
    applog::Close();
    return code;
}

}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
    applog::Init();
    applog::Write(L"ForceKill starting (elevated=%ls).", IsElevated() ? L"yes" : L"no");

    if (!AcquireInstance()) {
        if (const HWND existing = FindWindowW(kWindowClass, nullptr))
            PostMessageW(existing, kShowSettingsMessage, 0, 0);
        applog::Write(L"Another instance is already running; exiting.");
        return Shutdown(0);
    }

    if (!RegisterAppWindowClass(instance)) {
        applog::Write(L"RegisterClassExW failed (%lu).", static_cast<unsigned long>(GetLastError()));
        return Shutdown(1);
    }

    g_window = CreateWindowExW(0, kWindowClass, L"ForceKill", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!g_window) {
        applog::Write(L"CreateWindowExW failed (%lu).", static_cast<unsigned long>(GetLastError()));
        return Shutdown(1);
    }

    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

    g_settings = settings::Load();
    applog::Write(L"Config: %ls (hotkey %ls)", settings::ConfigPath().c_str(), hotkey::Describe(g_settings).c_str());

    tray::Add(g_window, kTrayMessage, hotkey::Describe(g_settings));

    if (!BindHotkey(g_settings))
        WarnHotkeyUnavailable();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    applog::Write(L"ForceKill exiting.");
    return Shutdown(0);
}
