#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace text {

inline std::wstring ToLower(std::wstring value) {
    if (!value.empty())
        CharLowerBuffW(value.data(), static_cast<DWORD>(value.size()));
    return value;
}

inline std::wstring Trim(const std::wstring& value) {
    const wchar_t* whitespace = L" \t\r\n\"'";
    const size_t first = value.find_first_not_of(whitespace);
    if (first == std::wstring::npos)
        return std::wstring();
    const size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

inline std::vector<std::wstring> Split(const std::wstring& value, wchar_t separator) {
    std::vector<std::wstring> parts;
    std::wstring current;
    for (wchar_t c : value) {
        if (c == separator) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

inline std::wstring Join(const std::vector<std::wstring>& parts, const wchar_t* separator) {
    std::wstring joined;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0)
            joined += separator;
        joined += parts[i];
    }
    return joined;
}

inline std::wstring Truncate(const std::wstring& value, size_t maxLength) {
    if (value.size() <= maxLength)
        return value;
    return value.substr(0, maxLength - 3) + L"...";
}

}  // namespace text
