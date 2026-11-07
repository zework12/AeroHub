#pragma once
#include <windows.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aero {

struct HandleCloser {
    void operator()(void* h) const noexcept {
        if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

// First process whose exe name matches (case-insensitive).
std::optional<DWORD> FindProcessId(std::wstring_view exeName);

// Every process whose exe name matches (case-insensitive).
std::vector<DWORD> FindProcessIds(std::wstring_view exeName);

// Command line of a process, or empty when it cannot be read (other user, protected process).
std::wstring ProcessCommandLine(DWORD pid);

// Base address of a module (case-insensitive file name) inside process `pid`.
std::optional<std::uintptr_t> FindModuleBase(DWORD pid, std::wstring_view moduleName);

}  // namespace aero
