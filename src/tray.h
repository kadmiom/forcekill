#pragma once

#include <windows.h>

#include <string>

namespace tray {

constexpr UINT kMenuSettings = 40001;
constexpr UINT kMenuOpenLog = 40002;
constexpr UINT kMenuPause = 40003;
constexpr UINT kMenuElevate = 40004;
constexpr UINT kMenuExit = 40005;
constexpr UINT kMenuAutostart = 40006;
constexpr UINT kMenuReopen = 40007;

bool Add(HWND owner, UINT callbackMessage, const std::wstring& hotkeyLabel);
void Remove(HWND owner);
bool UpdateTip(HWND owner, const std::wstring& hotkeyLabel, bool paused);

void Balloon(HWND owner, const std::wstring& title, const std::wstring& text, DWORD flags);
void ShowMenu(HWND owner, bool paused, bool autostartEnabled, bool reopenEnabled, bool elevated,
              const std::wstring& hotkeyLabel);

}  // namespace tray
