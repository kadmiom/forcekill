#pragma once

#include <windows.h>

#include "settings.h"

namespace settings_ui {

// Modal settings window. Returns true when the user accepted the changes and
// writes them into `value`.
bool Show(HWND owner, Settings& value);

}  // namespace settings_ui
