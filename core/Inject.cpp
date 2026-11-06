#include "Inject.hpp"

#include <string>

#include "Process.hpp"

namespace aero {

const wchar_t* ToString(InjectStatus s) {
    switch (s) {
        case InjectStatus::Ok: return L"injected";
        case InjectStatus::AlreadyLoaded: return L"module already loaded";
        case InjectStatus::DllMissing: return L"module DLL not found next to the hub";
        case InjectStatus::OpenFailed: return L"OpenProcess failed (run the hub as the same user / elevated if the game is)";
        case InjectStatus::TargetNot64Bit: return L"target process is not 64-bit";
        case InjectStatus::AllocFailed: return L"VirtualAllocEx failed";
        case InjectStatus::WriteFailed: return L"WriteProcessMemory failed";
        case InjectStatus::ThreadFailed: return L"CreateRemoteThread failed";
        case InjectStatus::Timeout: return L"LoadLibrary did not finish in 15 s";
        case InjectStatus::NotLoadedAfterRun: return L"LoadLibraryW returned but the module is not in the target (see module log)";
    }
    return L"?";
}

InjectResult InjectDll(DWORD pid, const std::filesystem::path& dll) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(dll, ec)) return {InjectStatus::DllMissing, 0};
    const std::wstring full = std::filesystem::absolute(dll, ec).wstring();
    const std::wstring file = dll.filename().wstring();

    if (FindModuleBase(pid, file)) return {InjectStatus::AlreadyLoaded, 0};

    UniqueHandle proc(OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                      PROCESS_VM_WRITE | PROCESS_VM_READ,
                                  FALSE, pid));
    if (!proc) return {InjectStatus::OpenFailed, GetLastError()};

    BOOL wow64 = FALSE;
    if (!IsWow64Process(proc.get(), &wow64) || wow64) return {InjectStatus::TargetNot64Bit, GetLastError()};

    const SIZE_T bytes = (full.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(proc.get(), nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) return {InjectStatus::AllocFailed, GetLastError()};

    SIZE_T written = 0;
    if (!WriteProcessMemory(proc.get(), remote, full.c_str(), bytes, &written) || written != bytes) {
        const DWORD err = GetLastError();
        VirtualFreeEx(proc.get(), remote, 0, MEM_RELEASE);
        return {InjectStatus::WriteFailed, err};
    }

    // kernel32 is mapped at the same address in every process of a boot session.
    const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    UniqueHandle thread(CreateRemoteThread(proc.get(), nullptr, 0, loadLibrary, remote, 0, nullptr));
    if (!thread) {
        const DWORD err = GetLastError();
        VirtualFreeEx(proc.get(), remote, 0, MEM_RELEASE);
        return {InjectStatus::ThreadFailed, err};
    }

    if (WaitForSingleObject(thread.get(), 15000) != WAIT_OBJECT_0) {
        // The thread may still read the path buffer, so it is intentionally not freed here.
        return {InjectStatus::Timeout, 0};
    }
    VirtualFreeEx(proc.get(), remote, 0, MEM_RELEASE);

    return FindModuleBase(pid, file) ? InjectResult{InjectStatus::Ok, 0}
                                     : InjectResult{InjectStatus::NotLoadedAfterRun, 0};
}

}  // namespace aero
