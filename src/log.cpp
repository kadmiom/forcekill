#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <string>

#include "settings.h"

namespace {

HANDLE g_file = INVALID_HANDLE_VALUE;
CRITICAL_SECTION g_lock;
bool g_lockReady = false;

constexpr ULONGLONG kMaxLogBytes = 1024ull * 1024ull;

void RotateIfTooLarge(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return;

    ULARGE_INTEGER size{};
    size.LowPart = data.nFileSizeLow;
    size.HighPart = data.nFileSizeHigh;
    if (size.QuadPart <= kMaxLogBytes)
        return;

    const std::wstring rotated = path + L".old";
    DeleteFileW(rotated.c_str());
    MoveFileW(path.c_str(), rotated.c_str());
}

}  // namespace

std::wstring applog::Path() {
    const std::wstring directory = settings::Directory();
    if (directory.empty())
        return std::wstring();
    return directory + L"\\forcekill.log";
}

void applog::Init() {
    if (!g_lockReady) {
        InitializeCriticalSection(&g_lock);
        g_lockReady = true;
    }
    if (g_file != INVALID_HANDLE_VALUE)
        return;

    const std::wstring path = Path();
    if (path.empty())
        return;

    RotateIfTooLarge(path);
    g_file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void applog::Close() {
    if (g_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_file);
        g_file = INVALID_HANDLE_VALUE;
    }
}

void applog::Write(const wchar_t* format, ...) {
    if (g_file == INVALID_HANDLE_VALUE || !g_lockReady || !format)
        return;

    wchar_t body[2048]{};
    va_list args;
    va_start(args, format);
    vswprintf(body, 2048, format, args);
    va_end(args);

    SYSTEMTIME now{};
    GetLocalTime(&now);

    wchar_t line[2300]{};
    swprintf(line, 2300, L"%04u-%02u-%02u %02u:%02u:%02u  %ls\r\n", now.wYear, now.wMonth, now.wDay,
             now.wHour, now.wMinute, now.wSecond, body);

    char utf8[4600]{};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, static_cast<int>(sizeof(utf8)), nullptr,
                                          nullptr);
    if (bytes <= 1)
        return;

    EnterCriticalSection(&g_lock);
    DWORD written = 0;
    WriteFile(g_file, utf8, static_cast<DWORD>(bytes - 1), &written, nullptr);
    LeaveCriticalSection(&g_lock);
}
