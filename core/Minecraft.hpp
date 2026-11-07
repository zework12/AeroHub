#pragma once
// Minecraft (Java) side of the hub: finds the game (a running process, then the Modrinth App, TLauncher and the
// plain .minecraft folder), saves the AeroClient jar to the Downloads folder, and reads the status file the mod
// writes while the game runs.
#include <windows.h>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace aero::mc {

struct Detected {
    std::wstring launcher;  // "Modrinth App", "TLauncher", "Minecraft launcher" or the brand the game reports
    std::wstring profile;   // Modrinth profile / folder name, or the --version of a running game
    std::filesystem::path dir;  // the game folder: holds mods, logs, config
    bool running = false;
    DWORD pid = 0;
    bool fabric121 = true;  // for a running game: Fabric and the wanted Minecraft version both appear on its command line
};

struct LaunchArgs {
    std::wstring gameDir, version, brand;
};

// Pure text parsing of a game command line: the values of --gameDir, --version and -Dminecraft.launcher.brand
// (quoted values may contain spaces).
LaunchArgs ParseLaunchArgs(const std::wstring& commandLine);

// True for the command line of a Minecraft client (has --gameDir and the usual client arguments or main classes).
bool LooksLikeMinecraft(const std::wstring& commandLine);

// Every running Minecraft client found through java.exe / javaw.exe command lines.
std::vector<Detected> RunningGames(const std::wstring& gameVersion);

// Games found on disk. Order: folders in `extraDirs`, Modrinth profiles with fabric-api for `gameVersion`
// (most recently played first), then TLauncher / .minecraft.
std::vector<Detected> InstalledGames(const std::filesystem::path& modrinthRootOverride, const std::wstring& gameVersion,
                                     const std::vector<std::filesystem::path>& extraDirs);

// Whether an AeroClient jar sits in <gameDir>\mods.
bool HasMod(const std::filesystem::path& gameDir);

// The user's Downloads folder (empty when Windows cannot say).
std::filesystem::path DownloadsFolder();

// Copies the jar into Downloads as its own file name, replacing an older copy. Returns an error text, or empty and
// the saved path in `saved`.
std::wstring SaveToDownloads(const std::filesystem::path& jar, std::filesystem::path& saved);

struct Live {
    bool fresh = false;  // the file exists and was written within the last 15 seconds
    DWORD pid = 0;
    std::vector<std::pair<std::wstring, std::wstring>> rows;  // label, value for the hub's live card
};

Live ReadLive(const std::filesystem::path& gameDir);

}  // namespace aero::mc
