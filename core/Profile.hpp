#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace aero {

// One supported game build. Everything build-dependent lives in the profile .ini and in the module's
// own config header; the hub only uses the identity fields to decide whether injecting is allowed.
struct GameProfile {
    std::wstring name;
    std::wstring processName;  // e.g. Forts.exe
    std::wstring moduleDll;    // path relative to the hub directory
    std::uint32_t timeDateStamp{};
    std::uint32_t sizeOfImage{};
    std::wstring note;
    std::wstring hotkeys;  // optional, shown in the hub
    std::wstring steamAppId;  // optional, enables the hub's Launch button
    std::filesystem::path file;
};

// Loads every *.ini in `dir`. Files with a missing or malformed required key are skipped and
// described in `errors`.
std::vector<GameProfile> LoadProfiles(const std::filesystem::path& dir, std::vector<std::wstring>& errors);

}  // namespace aero
