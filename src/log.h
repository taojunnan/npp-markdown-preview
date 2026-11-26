// Tiny file logger shared by the plugin and the WebView2 host.
// Header-only so every translation unit (and the test harness) gets the same
// instance without touching the build scripts.
#pragma once

#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <string>

namespace mdlog {

inline bool& enabled() {
    static bool value = false;
    return value;
}

inline std::wstring& filePath() {
    static std::wstring value;
    return value;
}

inline void Line(const wchar_t* fmt, ...) {
    if (!enabled() || filePath().empty()) return;

    wchar_t body[2048] = {};
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(body, _countof(body), _TRUNCATE, fmt, ap);
    va_end(ap);

    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t line[2300] = {};
    _snwprintf_s(line, _countof(line), _TRUNCATE, L"%02u:%02u:%02u.%03u  %s\r\n", st.wHour, st.wMinute,
                 st.wSecond, st.wMilliseconds, body);

    char utf8[8192] = {};
    int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, (int)sizeof(utf8) - 1, nullptr, nullptr);
    if (n <= 1) return;

    FILE* f = _wfopen(filePath().c_str(), L"ab");
    if (!f) return;
    fwrite(utf8, 1, (size_t)(n - 1), f);
    fclose(f);
}

// dir: plugin config directory.  Turning it on truncates the log when it grows
// past ~512 KB so it never becomes a problem of its own.
inline void Init(const std::wstring& dir, bool on) {
    if (dir.empty()) return;
    std::wstring p = dir + L"\\preview.log";
    if (on) {
        WIN32_FILE_ATTRIBUTE_DATA fad = {};
        if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad)) {
            ULARGE_INTEGER size = {};
            size.LowPart = fad.nFileSizeLow;
            size.HighPart = fad.nFileSizeHigh;
            if (size.QuadPart > 512 * 1024) DeleteFileW(p.c_str());
        }
    }
    filePath() = p;
    enabled() = on;
}

inline ULONGLONG Now() { return GetTickCount64(); }

}  // namespace mdlog
