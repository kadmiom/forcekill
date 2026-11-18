#pragma once

#include <windows.h>

#include <string>

#include "settings.h"

namespace hotkey {

constexpr int kHotkeyId = 1;

UINT Modifiers(const Settings& value);
std::wstring Describe(const Settings& value);

// "F4", "A", "PrintScreen", "0x2E"
std::wstring VirtualKeyToName(UINT vk);
UINT NameToVirtualKey(const std::wstring& name);

// Ctrl/Alt/Shift/Win alone must never become the hotkey.
bool IsPureModifier(UINT vk);

}  // namespace hotkey
