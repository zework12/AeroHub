#pragma once
#include <windows.h>

#include <cstdint>
#include <optional>

namespace aero {

struct ImageIdentity {
    std::uint32_t timeDateStamp{};
    std::uint32_t sizeOfImage{};
    std::uint16_t machine{};
};

// Reads the PE header of a module loaded in another process (or this one).
// Returns nullopt on any partial read or malformed header.
std::optional<ImageIdentity> ReadImageIdentity(HANDLE process, std::uintptr_t moduleBase);

}  // namespace aero
