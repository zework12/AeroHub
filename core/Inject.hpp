#pragma once
#include <windows.h>

#include <filesystem>

namespace aero {

enum class InjectStatus {
    Ok,
    AlreadyLoaded,
    DllMissing,
    OpenFailed,
    TargetNot64Bit,
    AllocFailed,
    WriteFailed,
    ThreadFailed,
    Timeout,
    NotLoadedAfterRun,
};

struct InjectResult {
    InjectStatus status{};
    DWORD lastError{};
};

const wchar_t* ToString(InjectStatus s);

// LoadLibraryW in the target through a remote thread. Success is confirmed by finding the module
// in the target afterwards, not by the thread exit code (which is truncated to 32 bits).
InjectResult InjectDll(DWORD pid, const std::filesystem::path& dll);

}  // namespace aero
