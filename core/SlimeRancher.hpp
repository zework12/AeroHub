#pragma once
// Slime Rancher (part 1) and Slime Rancher 2 for the hub: find the Steam install of the chosen part.
// ParseManifest is a pure text function so it can be checked without Steam or files.
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace aero::sr {

struct Manifest {
    std::string appId;       // "appid"
    std::string name;        // "name"
    std::string installDir;  // "installdir" (folder name under steamapps\common)
};

// Reads the three keys from the text of a Steam appmanifest_<id>.acf (VDF: lines of "key" "value"). Missing keys stay empty.
Manifest ParseManifest(std::string_view acf);

// The Steam title of a part (1 = "Slime Rancher", 2 = "Slime Rancher 2"). The titles are an assumption about the store names;
// a manifest is accepted only when its "name" equals the title exactly.
const wchar_t* Title(int part);

struct Install {
    std::filesystem::path dir;  // folder that holds the game files
    std::wstring appId;         // Steam app id from the manifest; empty when only the folder was found
};

// Looks through every Steam library (registry SteamPath, the default folder, libraryfolders.vdf) for the part.
std::optional<Install> Find(int part);

}  // namespace aero::sr
