#include "Process.hpp"

#include <tlhelp32.h>

#include <cwchar>
#include <string>

namespace aero {
namespace {

bool EqualNoCase(std::wstring_view a, const wchar_t* b) {
    const std::wstring tmp(a);
    return _wcsicmp(tmp.c_str(), b) == 0;
}

UniqueHandle Snapshot(DWORD flags, DWORD pid) {
    // A module snapshot of a process that is still starting can fail with ERROR_BAD_LENGTH; retry.
    for (int attempt = 0; attempt < 10; ++attempt) {
        HANDLE h = CreateToolhelp32Snapshot(flags, pid);
        if (h != INVALID_HANDLE_VALUE) return UniqueHandle(h);
        if (GetLastError() != ERROR_BAD_LENGTH) break;
    }
    return UniqueHandle(nullptr);
}

}  // namespace

std::optional<DWORD> FindProcessId(std::wstring_view exeName) {
    UniqueHandle snap = Snapshot(TH32CS_SNAPPROCESS, 0);
    if (!snap) return std::nullopt;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(snap.get(), &pe); ok; ok = Process32NextW(snap.get(), &pe)) {
        if (EqualNoCase(exeName, pe.szExeFile)) return pe.th32ProcessID;
    }
    return std::nullopt;
}

std::optional<std::uintptr_t> FindModuleBase(DWORD pid, std::wstring_view moduleName) {
    UniqueHandle snap = Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (!snap) return std::nullopt;
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    for (BOOL ok = Module32FirstW(snap.get(), &me); ok; ok = Module32NextW(snap.get(), &me)) {
        if (EqualNoCase(moduleName, me.szModule)) return reinterpret_cast<std::uintptr_t>(me.modBaseAddr);
    }
    return std::nullopt;
}

}  // namespace aero
