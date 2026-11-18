#include "hotkey.h"

#include <cstdlib>
#include <cwctype>
#include <string>

#include "text.h"

namespace {

struct KeyName {
    UINT vk;
    const wchar_t* name;
};

const KeyName kKeyNames[] = {
    {VK_INSERT, L"Insert"},
    {VK_DELETE, L"Delete"},
    {VK_HOME, L"Home"},
    {VK_END, L"End"},
    {VK_PRIOR, L"PageUp"},
    {VK_NEXT, L"PageDown"},
    {VK_PAUSE, L"Pause"},
    {VK_SNAPSHOT, L"PrintScreen"},
    {VK_SPACE, L"Space"},
    {VK_TAB, L"Tab"},
};

}  // namespace

UINT hotkey::Modifiers(const Settings& value) {
    UINT modifiers = MOD_NOREPEAT;
    if (value.ctrl)
        modifiers |= MOD_CONTROL;
    if (value.alt)
        modifiers |= MOD_ALT;
    if (value.shift)
        modifiers |= MOD_SHIFT;
    if (value.win)
        modifiers |= MOD_WIN;
    return modifiers;
}

std::wstring hotkey::Describe(const Settings& value) {
    std::wstring label;
    if (value.ctrl)
        label += L"Ctrl+";
    if (value.alt)
        label += L"Alt+";
    if (value.shift)
        label += L"Shift+";
    if (value.win)
        label += L"Win+";
    label += VirtualKeyToName(value.vk);
    return label;
}

std::wstring hotkey::VirtualKeyToName(UINT vk) {
    if (vk >= VK_F1 && vk <= VK_F24)
        return L"F" + std::to_wstring(vk - VK_F1 + 1);

    const UINT letterA = static_cast<UINT>(L'A');
    const UINT digit0 = static_cast<UINT>(L'0');
    if ((vk >= letterA && vk <= letterA + 25) || (vk >= digit0 && vk <= digit0 + 9))
        return std::wstring(1, static_cast<wchar_t>(vk));

    for (const KeyName& entry : kKeyNames) {
        if (entry.vk == vk)
            return entry.name;
    }

    wchar_t buffer[16]{};
    swprintf(buffer, 16, L"0x%02X", vk);
    return buffer;
}

UINT hotkey::NameToVirtualKey(const std::wstring& name) {
    const std::wstring lowered = text::ToLower(text::Trim(name));
    if (lowered.empty())
        return 0;

    if (lowered.size() == 1) {
        const wchar_t upper = static_cast<wchar_t>(towupper(lowered[0]));
        if ((upper >= L'A' && upper <= L'Z') || (upper >= L'0' && upper <= L'9'))
            return static_cast<UINT>(upper);
    }

    if (lowered.size() >= 2 && lowered[0] == L'f') {
        const long index = wcstol(lowered.c_str() + 1, nullptr, 10);
        if (index >= 1 && index <= 24)
            return VK_F1 + static_cast<UINT>(index) - 1;
    }

    for (const KeyName& entry : kKeyNames) {
        if (lowered == text::ToLower(entry.name))
            return entry.vk;
    }

    if (lowered.rfind(L"0x", 0) == 0)
        return static_cast<UINT>(wcstoul(lowered.c_str() + 2, nullptr, 16));

    return 0;
}

bool hotkey::IsPureModifier(UINT vk) {
    switch (vk) {
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
        case VK_LWIN:
        case VK_RWIN:
            return true;
        default:
            return false;
    }
}
