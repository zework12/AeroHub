#pragma once
#include <windows.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace aero {

struct HandleCloser {
    void operator()(void* h) const noexcept {
        if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

// First process whose exe name matches (case-insensitive).
std::optional<DWORD> FindProcessId(std::wstring_view exeName);

// Base address of a module (case-insensitive file name) inside process `pid`.
std::optional<std::uintptr_t> FindModuleBase(DWORD pid, std::wstring_view moduleName);

}  // namespace aero
