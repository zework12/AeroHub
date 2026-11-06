#pragma once
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace forts {

inline HMODULE g_self = nullptr;  // set by DllMain

// Appends to FortsModule.log next to the DLL.
inline void Logf(const char* fmt, ...) {
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(g_self, path, MAX_PATH)) return;
    wchar_t* dot = wcsrchr(path, L'.');
    if (!dot) return;
    wcscpy_s(dot, path + MAX_PATH - dot, L".log");
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"a") != 0 || !f) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(f, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    std::vfprintf(f, fmt, args);
    va_end(args);
    std::fputc('\n', f);
    std::fclose(f);
}

}  // namespace forts
