#pragma once
// Forts helpers for the hub: find the install folder, check/fix the OpenGL setting, pack the logs into a zip.
// The text-parsing parts are pure functions (no files, no Windows calls) so they can be checked on their own.
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace aero::diag {

// ---- pure text helpers -------------------------------------------------------------------------

// Every "path" value of Steam's libraryfolders.vdf (UTF-8, backslash escapes resolved).
std::vector<std::string> ParseLibraryPaths(std::string_view vdf);

// The last block of consecutive "Activating mod ..." lines of a Forts log (up to 3 other lines may sit between two of them).
std::vector<std::string> LastModBlock(std::string_view logText);

// First line containing `needle` (trimmed), or an empty string.
std::string FirstLineWith(std::string_view text, std::string_view needle);

// Number of lines containing `needle`.
int CountLinesWith(std::string_view text, std::string_view needle);

// settings_hardware.lua: the value of `EnableVulkan = true|false,`. nullopt when the key is absent or unreadable.
std::optional<bool> ParseEnableVulkan(std::string_view luaText);

// Returns the text with EnableVulkan set to false (the value is replaced in place; a missing key is added before the final
// closing brace). nullopt when the text has no table to put it in.
std::optional<std::string> WithOpenGl(std::string_view luaText);

// ---- Windows side ------------------------------------------------------------------------------

// Folder that holds Forts.exe: the running game first, then every Steam library. nullopt when not found.
std::optional<std::filesystem::path> FindFortsDir();

enum class GraphicsState {
    Unknown,   // no Forts folder or no settings file yet
    OpenGl,    // EnableVulkan = false
    Vulkan,    // EnableVulkan = true: the module cannot work
    NotSet,    // the file exists but has no EnableVulkan key (the game's default is unknown)
};

struct GraphicsInfo {
    GraphicsState state = GraphicsState::Unknown;
    std::filesystem::path file;  // the settings_hardware.lua that was read
};

// Reads users\<id>\settings_hardware.lua (the most recently written one) of the Forts folder.
GraphicsInfo CheckFortsGraphics(const std::filesystem::path& fortsDir);

// Sets EnableVulkan = false in `file`. Keeps a one-time backup (file + ".aerohub-bak"), writes through a temp file and reads the
// result back. Refuses while Forts is running (the game rewrites the file when it exits). Returns an empty string on success,
// otherwise the reason.
std::wstring SwitchToOpenGl(const std::filesystem::path& file);

struct CollectResult {
    std::filesystem::path zip;
    std::vector<std::wstring> files;  // names inside the zip
    std::wstring error;               // empty on success
};

// Packs summary.txt, the Forts game log(s), the newest desync reports and the module log into <outDir>\AeroHub-logs-<time>.zip.
CollectResult CollectFortsLogs(const std::filesystem::path& hubExe, const std::filesystem::path& moduleLog, const std::filesystem::path& outDir);

}  // namespace aero::diag
