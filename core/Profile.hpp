#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace aero {

// One supported game build. Everything build-dependent lives in the profile .ini and in the module's
// own config header; the hub only uses the identity fields to decide whether injecting is allowed.
enum class GameKind {
    NativeModule,  // a DLL injected into the game process
    FabricMod,     // a mod jar the hub installs into a Minecraft (Fabric) instance
};

struct GameProfile {
    GameKind kind = GameKind::NativeModule;
    std::wstring name;
    std::wstring processName;  // e.g. Forts.exe
    std::wstring moduleDll;    // path relative to the hub directory
    std::uint32_t timeDateStamp{};
    std::uint32_t sizeOfImage{};
    std::wstring note;
    std::wstring hotkeys;  // optional, shown in the hub
    std::wstring steamAppId;  // optional, enables the hub's Launch button
    std::filesystem::path file;

    // FabricMod only. modFile is relative to the hub directory. The instance fields are filled by the hub
    // when it expands the profile into one entry per Minecraft instance it finds.
    std::wstring modFile;
    std::wstring gameVersion;
    std::wstring instanceName;
    std::filesystem::path instanceDir;
};

// Loads every *.ini in `dir`. Files with a missing or malformed required key are skipped and
// described in `errors`.
std::vector<GameProfile> LoadProfiles(const std::filesystem::path& dir, std::vector<std::wstring>& errors);

}  // namespace aero
