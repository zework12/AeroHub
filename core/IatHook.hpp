#pragma once
#include <windows.h>

#include <cstdint>

namespace aero {

// Replaces one slot of a module's import address table. No trampoline and no code patching:
// the whole change is a single pointer write, undone by Remove().
class IatHook {
public:
    IatHook() = default;
    IatHook(const IatHook&) = delete;
    IatHook& operator=(const IatHook&) = delete;

    // `slotRva` (optional) receives the RVA of the patched slot so callers can cross-check it
    // against an RVA measured offline.
    bool Install(HMODULE module, const char* importDll, const char* function, void* detour, void** original,
                 std::uintptr_t* slotRva = nullptr);

    // Restores the original pointer only if the slot still holds our detour.
    bool Remove();

    bool Installed() const { return slot_ != nullptr; }

private:
    void** slot_{};
    void* original_{};
    void* detour_{};
};

}  // namespace aero
