#include "settings_ui.h"

#include <windows.h>

#include <string>
#include <vector>

#include "hotkey.h"
#include "settings.h"
#include "text.h"

namespace settings_ui {
namespace {

constexpr wchar_t kDialogClass[] = L"ForceKillSettingsWindow";
constexpr wchar_t kHotkeyBoxClass[] = L"ForceKillHotkeyBox";

constexpr int kIdCtrl = 2002;
constexpr int kIdAlt = 2003;
constexpr int kIdShift = 2004;
constexpr int kIdWin = 2005;
constexpr int kIdProtected = 2006;

constexpr int kClientWidth = 470;
constexpr int kClientHeight = 432;

struct State {
    Settings edited;
    bool accepted = false;
    bool closed = false;
    bool fontOwned = false;
    HWND hotkeyBox = nullptr;
    HWND ctrl = nullptr;
    HWND alt = nullptr;
    HWND shift = nullptr;
    HWND win = nullptr;
    HWND protectedEdit = nullptr;
    HFONT font = nullptr;
};

void AttachState(HWND hwnd, LPARAM createParam) {
    const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(createParam);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
}

State* From(HWND hwnd) {
    return reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

HWND AddControl(HWND parent, const State* state, const wchar_t* className, const std::wstring& text, DWORD style,
                DWORD exStyle, int id, int x, int y, int width, int height, void* createParam = nullptr) {
    HWND control = CreateWindowExW(exStyle, className, text.c_str(), style, x, y, width, height, parent,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, createParam);
    if (state->font)
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
    return control;
}

void AddLabel(HWND parent, const State* state, const std::wstring& text, int x, int y, int width) {
    AddControl(parent, state, L"STATIC", text, WS_CHILD | WS_VISIBLE, 0, 0, x, y, width, 18);
}

HWND AddCheckbox(HWND parent, const State* state, int id, const std::wstring& text, int x, int y, int width) {
    return AddControl(parent, state, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, id, x, y,
                      width, 22);
}

void AddButton(HWND parent, const State* state, int id, const std::wstring& text, int x, int width, bool isDefault) {
    AddControl(parent, state, L"BUTTON", text,
               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | (isDefault ? BS_DEFPUSHBUTTON : 0), 0, id, x, 390,
               width, 28);
}

void SyncCheckboxes(const State* state) {
    SendMessageW(state->ctrl, BM_SETCHECK, state->edited.ctrl ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(state->alt, BM_SETCHECK, state->edited.alt ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(state->shift, BM_SETCHECK, state->edited.shift ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(state->win, BM_SETCHECK, state->edited.win ? BST_CHECKED : BST_UNCHECKED, 0);
}

void ReadCheckboxes(State* state) {
    state->edited.ctrl = SendMessageW(state->ctrl, BM_GETCHECK, 0, 0) == BST_CHECKED;
    state->edited.alt = SendMessageW(state->alt, BM_GETCHECK, 0, 0) == BST_CHECKED;
    state->edited.shift = SendMessageW(state->shift, BM_GETCHECK, 0, 0) == BST_CHECKED;
    state->edited.win = SendMessageW(state->win, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void PaintHotkeyBox(State* state, HWND hwnd) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(hwnd, &paint);

    RECT client{};
    GetClientRect(hwnd, &client);
    FillRect(dc, &client, GetSysColorBrush(COLOR_WINDOW));

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));

    HGDIOBJ previous = SelectObject(dc, state->font);
    RECT text = client;
    text.left += 7;
    DrawTextW(dc, hotkey::Describe(state->edited).c_str(), -1, &text,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    SelectObject(dc, previous);

    if (GetFocus() == hwnd) {
        RECT focus = client;
        InflateRect(&focus, -1, -1);
        DrawFocusRect(dc, &focus);
    }

    EndPaint(hwnd, &paint);
}

void CaptureKey(State* state, UINT vk) {
    state->edited.vk = vk;
    state->edited.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    state->edited.alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    state->edited.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    state->edited.win = ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) != 0;
    SyncCheckboxes(state);
    InvalidateRect(state->hotkeyBox, nullptr, TRUE);
}

LRESULT CALLBACK HotkeyBoxProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        AttachState(hwnd, lParam);
        return TRUE;
    }

    State* state = From(hwnd);

    switch (message) {
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS | DLGC_WANTARROWS;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            PaintHotkeyBox(state, hwnd);
            return 0;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        case WM_LBUTTONDOWN:
            SetFocus(hwnd);
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN: {
            const UINT vk = static_cast<UINT>(wParam);
            if (!hotkey::IsPureModifier(vk) && vk != VK_ESCAPE)
                CaptureKey(state, vk);
            return 0;
        }
        default:
            break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

std::wstring ReadControlText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0)
        return std::wstring();

    std::wstring buffer(static_cast<size_t>(length) + 1, L'\0');
    buffer.resize(static_cast<size_t>(GetWindowTextW(control, buffer.data(), length + 1)));
    return buffer;
}

bool ContainsIgnoreCase(const std::vector<std::wstring>& values, const std::wstring& candidate) {
    const std::wstring lowered = text::ToLower(candidate);
    for (const std::wstring& value : values) {
        if (text::ToLower(value) == lowered)
            return true;
    }
    return false;
}

void CollectProtectedApps(State* state) {
    state->edited.protectedApps.clear();
    for (const std::wstring& line : text::Split(ReadControlText(state->protectedEdit), L'\n')) {
        const std::wstring entry = text::Trim(line);
        if (entry.empty() || entry[0] == L'#')
            continue;
        if (!ContainsIgnoreCase(state->edited.protectedApps, entry))
            state->edited.protectedApps.push_back(entry);
    }
}

void Finish(State* state, HWND hwnd, bool accepted) {
    if (accepted) {
        ReadCheckboxes(state);
        CollectProtectedApps(state);
    }
    state->accepted = accepted;
    state->closed = true;
    DestroyWindow(hwnd);
}

LRESULT CALLBACK DialogProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        AttachState(hwnd, lParam);
        return TRUE;
    }

    State* state = From(hwnd);

    switch (message) {
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK:
                    Finish(state, hwnd, true);
                    return 0;
                case IDCANCEL:
                    Finish(state, hwnd, false);
                    return 0;
                case kIdCtrl:
                case kIdAlt:
                case kIdShift:
                case kIdWin:
                    ReadCheckboxes(state);
                    InvalidateRect(state->hotkeyBox, nullptr, TRUE);
                    return 0;
                default:
                    break;
            }
            return 0;
        case WM_CLOSE:
            Finish(state, hwnd, false);
            return 0;
        case WM_DESTROY:
            if (state->fontOwned)
                DeleteObject(state->font);
            state->font = nullptr;
            state->fontOwned = false;
            state->closed = true;
            return 0;
        default:
            break;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool EnsureClasses() {
    static const bool registered = []() {
        WNDCLASSEXW dialog{};
        dialog.cbSize = sizeof(dialog);
        dialog.lpfnWndProc = DialogProc;
        dialog.hInstance = GetModuleHandleW(nullptr);
        dialog.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        dialog.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        dialog.lpszClassName = kDialogClass;
        if (!RegisterClassExW(&dialog))
            return false;

        WNDCLASSEXW box{};
        box.cbSize = sizeof(box);
        box.lpfnWndProc = HotkeyBoxProc;
        box.hInstance = dialog.hInstance;
        box.hCursor = dialog.hCursor;
        box.lpszClassName = kHotkeyBoxClass;
        return RegisterClassExW(&box) != FALSE;
    }();
    return registered;
}

HFONT CreateUiFont(bool* owned) {
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0)) {
        if (HFONT font = CreateFontIndirectW(&metrics.lfMessageFont)) {
            *owned = true;
            return font;
        }
    }
    *owned = false;
    return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

void CenterOnOwner(HWND window, HWND owner) {
    RECT area{};
    if (!(owner && IsWindowVisible(owner) && GetWindowRect(owner, &area)) &&
        !SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0))
        return;

    RECT frame{};
    GetWindowRect(window, &frame);
    const int x = area.left + ((area.right - area.left) - (frame.right - frame.left)) / 2;
    const int y = area.top + ((area.bottom - area.top) - (frame.bottom - frame.top)) / 2;
    SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

void BuildControls(HWND window, State* state) {
    AddLabel(window, state, L"Hotkey", 16, 16, 200);
    state->hotkeyBox = AddControl(window, state, kHotkeyBoxClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                  WS_EX_CLIENTEDGE, 0, 16, 38, 200, 26, state);

    state->ctrl = AddCheckbox(window, state, kIdCtrl, L"Ctrl", 16, 74, 64);
    state->alt = AddCheckbox(window, state, kIdAlt, L"Alt", 86, 74, 60);
    state->shift = AddCheckbox(window, state, kIdShift, L"Shift", 152, 74, 64);
    state->win = AddCheckbox(window, state, kIdWin, L"Win", 224, 74, 60);
    SyncCheckboxes(state);

    AddLabel(window, state, L"Click the box above, then press the key combination you want.", 16, 104, 438);

    AddLabel(window, state, L"Never kill these apps (one exe name or full path per line):", 16, 132, 438);
    state->protectedEdit = AddControl(window, state, L"EDIT",
                                      text::Join(state->edited.protectedApps, L"\r\n"),
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_LEFT | ES_MULTILINE |
                                          ES_AUTOVSCROLL | ES_WANTRETURN,
                                      WS_EX_CLIENTEDGE, kIdProtected, 16, 154, 438, 186);

    AddLabel(window, state, L"System and critical Windows processes are always protected.", 16, 350, 438);

    AddButton(window, state, IDOK, L"OK", 270, 88, true);
    AddButton(window, state, IDCANCEL, L"Cancel", 366, 88, false);
}

}  // namespace

bool Show(HWND owner, Settings& value) {
    if (!EnsureClasses())
        return false;

    State state;
    state.edited = value;
    state.font = CreateUiFont(&state.fontOwned);

    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT frame{0, 0, kClientWidth, kClientHeight};
    AdjustWindowRectEx(&frame, style, FALSE, 0);

    const HWND window = CreateWindowExW(0, kDialogClass, L"ForceKill settings", style, CW_USEDEFAULT, CW_USEDEFAULT,
                                        frame.right - frame.left, frame.bottom - frame.top, owner, nullptr,
                                        GetModuleHandleW(nullptr), &state);
    if (!window) {
        if (state.fontOwned)
            DeleteObject(state.font);
        return false;
    }

    BuildControls(window, &state);
    CenterOnOwner(window, owner);

    if (owner)
        EnableWindow(owner, FALSE);

    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    SetFocus(state.hotkeyBox);

    MSG message{};
    while (!state.closed && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (owner)
        EnableWindow(owner, TRUE);

    if (state.accepted)
        value = state.edited;
    return state.accepted;
}

}  // namespace settings_ui
