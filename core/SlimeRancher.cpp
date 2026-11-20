#include "SlimeRancher.hpp"

#include <windows.h>

#include <fstream>
#include <vector>

#include "Diagnostics.hpp"

namespace aero::sr {
namespace {

namespace fs = std::filesystem;

// First two quoted strings of a line: "key" "value". False when the line does not have both.
bool QuotedPair(std::string_view line, std::string& key, std::string& value) {
    std::string parts[2];
    std::size_t at = 0;
    for (int i = 0; i < 2; ++i) {
        const std::size_t open = line.find('"', at);
        if (open == std::string_view::npos) return false;
        const std::size_t close = line.find('"', open + 1);
        if (close == std::string_view::npos) return false;
        parts[i] = std::string(line.substr(open + 1, close - open - 1));
        at = close + 1;
    }
    key = parts[0];
    value = parts[1];
    return true;
}

std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// Whole file up to `limit` bytes; nullopt when it cannot be read or is larger.
std::optional<std::string> ReadText(const fs::path& file, std::uintmax_t limit) {
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec || size > limit) return std::nullopt;
    std::ifstream f(file, std::ios::binary);
    if (!f) return std::nullopt;
    std::string text(static_cast<std::size_t>(size), '\0');
    f.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(f.gcount()));
    return text;
}

std::vector<fs::path> SteamLibraries() {
    std::vector<fs::path> libs;
    wchar_t steam[MAX_PATH * 2]{};
    DWORD cb = static_cast<DWORD>(sizeof(steam) - sizeof(wchar_t));
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, steam, &cb) == ERROR_SUCCESS) libs.emplace_back(steam);
    libs.emplace_back(L"C:\\Program Files (x86)\\Steam");
    const std::size_t roots = libs.size();
    for (std::size_t i = 0; i < roots; ++i) {
        if (const auto vdf = ReadText(libs[i] / L"steamapps" / L"libraryfolders.vdf", 1 << 20)) {
            for (const std::string& p : diag::ParseLibraryPaths(*vdf)) libs.emplace_back(FromUtf8(p));
        }
    }
    return libs;
}

}  // namespace

Manifest ParseManifest(std::string_view acf) {
    Manifest m;
    std::size_t start = 0;
    while (start <= acf.size()) {
        std::size_t end = acf.find('\n', start);
        if (end == std::string_view::npos) end = acf.size();
        std::string key, value;
        if (QuotedPair(acf.substr(start, end - start), key, value)) {
            if (key == "appid" && m.appId.empty()) m.appId = value;
            else if (key == "name" && m.name.empty()) m.name = value;
            else if (key == "installdir" && m.installDir.empty()) m.installDir = value;
        }
        start = end + 1;
    }
    return m;
}

const wchar_t* Title(int part) { return part == 2 ? L"Slime Rancher 2" : L"Slime Rancher"; }

std::optional<Install> Find(int part) {
    const std::wstring title = Title(part);
    std::string titleUtf8;  // the titles are ASCII
    for (wchar_t c : title) titleUtf8 += static_cast<char>(c);
    for (const fs::path& lib : SteamLibraries()) {
        const fs::path apps = lib / L"steamapps";
        std::error_code ec;
        for (fs::directory_iterator it(apps, ec), end; !ec && it != end; it.increment(ec)) {
            const fs::path& f = it->path();
            if (f.extension() != L".acf" || f.filename().wstring().rfind(L"appmanifest_", 0) != 0) continue;
            const auto text = ReadText(f, 1 << 20);
            if (!text) continue;
            const Manifest m = ParseManifest(*text);
            if (m.name != titleUtf8 || m.installDir.empty()) continue;
            const fs::path dir = apps / L"common" / FromUtf8(m.installDir);
            std::error_code dec;
            if (fs::is_directory(dir, dec)) return Install{dir, FromUtf8(m.appId)};
        }
        // No manifest (a manually copied game): the default folder name under common.
        const fs::path dir = apps / L"common" / title;
        std::error_code dec;
        if (fs::is_directory(dir, dec)) return Install{dir, L""};
    }
    return std::nullopt;
}

}  // namespace aero::sr
