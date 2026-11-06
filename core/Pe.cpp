#include "Pe.hpp"

namespace aero {
namespace {

template <class T>
bool ReadExact(HANDLE process, std::uintptr_t address, T& out) {
    SIZE_T got = 0;
    return ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), &out, sizeof(T), &got) &&
           got == sizeof(T);
}

}  // namespace

std::optional<ImageIdentity> ReadImageIdentity(HANDLE process, std::uintptr_t moduleBase) {
    IMAGE_DOS_HEADER dos{};
    if (!ReadExact(process, moduleBase, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE) return std::nullopt;
    if (dos.e_lfanew < 0x40 || dos.e_lfanew > 0x1000) return std::nullopt;

    IMAGE_NT_HEADERS64 nt{};
    if (!ReadExact(process, moduleBase + static_cast<std::uintptr_t>(dos.e_lfanew), nt)) return std::nullopt;
    if (nt.Signature != IMAGE_NT_SIGNATURE) return std::nullopt;
    if (nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return std::nullopt;

    return ImageIdentity{nt.FileHeader.TimeDateStamp, nt.OptionalHeader.SizeOfImage, nt.FileHeader.Machine};
}

}  // namespace aero
