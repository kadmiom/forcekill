#pragma once

#include <string>

namespace applog {

// Opens %LOCALAPPDATA%\ForceKill\forcekill.log (rotated at 1 MB).
void Init();
void Close();

// printf-style, wide format string; use %ls for wide strings.
void Write(const wchar_t* format, ...);

std::wstring Path();

}  // namespace applog
