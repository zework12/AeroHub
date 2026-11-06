#include "IatHook.hpp"

#include <cstring>

namespace aero {
namespace {

bool WriteSlot(void** slot, void* value) {
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    InterlockedExchangePointer(slot, value);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(void*), old, &ignored);
    return true;
}

}  // namespace

bool IatHook::Install(HMODULE module, const char* importDll, const char* function, void* detour, void** original,
                      std::uintptr_t* slotRva) {
    if (slot_) return false;
    const auto base = reinterpret_cast<std::uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    const DWORD imageSize = nt->OptionalHeader.SizeOfImage;
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir.VirtualAddress == 0 || dir.VirtualAddress >= imageSize) return false;

    const auto* desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (; desc->Name != 0 && desc->Name < imageSize; ++desc) {
        if (_stricmp(reinterpret_cast<const char*>(base + desc->Name), importDll) != 0) continue;
        if (desc->OriginalFirstThunk == 0 || desc->FirstThunk == 0) return false;

        const auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + desc->OriginalFirstThunk);
        auto* slots = reinterpret_cast<void**>(base + desc->FirstThunk);
        for (std::size_t i = 0; names[i].u1.AddressOfData != 0; ++i) {
            if (IMAGE_SNAP_BY_ORDINAL64(names[i].u1.Ordinal)) continue;
            const ULONGLONG nameRva = names[i].u1.AddressOfData;
            if (nameRva >= imageSize) return false;
            const auto* byName = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + nameRva);
            if (std::strcmp(byName->Name, function) != 0) continue;

            void* current = slots[i];
            if (!WriteSlot(&slots[i], detour)) return false;
            slot_ = &slots[i];
            original_ = current;
            detour_ = detour;
            if (original) *original = current;
            if (slotRva) *slotRva = reinterpret_cast<std::uint8_t*>(&slots[i]) - base;
            return true;
        }
        return false;
    }
    return false;
}

bool IatHook::Remove() {
    if (!slot_) return true;
    if (*slot_ != detour_) return false;  // somebody chained on top of us; leave their pointer alone
    const bool ok = WriteSlot(slot_, original_);
    if (ok) slot_ = nullptr;
    return ok;
}

}  // namespace aero
