#include "Minecraft.hpp"

#include <shlobj.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <fstream>
#include <map>
#include <sstream>

#include "Process.hpp"

namespace aero::mc {
namespace {

namespace fs = std::filesystem;

std::wstring EnvVar(const wchar_t* name) {
    wchar_t buf[MAX_PATH * 2]{};
    const DWORD n = GetEnvironmentVariableW(name, buf, static_cast<DWORD>(std::size(buf)));
    return n > 0 && n < std::size(buf) ? std::wstring(buf, n) : std::wstring();
}

std::wstring Lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return s;
}

bool Contains(const std::wstring& text, const wchar_t* needle) { return Lower(text).find(Lower(needle)) != std::wstring::npos; }

std::string ReadAll(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// Java .properties value: backslash escapes (\\, \:, \uXXXX, \t ...).
std::wstring UnescapeProperty(const std::wstring& v) {
    std::wstring out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] != L'\\' || i + 1 >= v.size()) {
            out += v[i];
            continue;
        }
        const wchar_t c = v[++i];
        if (c == L'u' && i + 4 < v.size()) {
            out += static_cast<wchar_t>(std::wcstoul(v.substr(i + 1, 4).c_str(), nullptr, 16));
            i += 4;
        } else if (c == L't') out += L'\t';
        else if (c == L'n') out += L'\n';
        else if (c == L'r') out += L'\r';
        else out += c;
    }
    return out;
}

std::wstring PropertyValue(const fs::path& file, const std::wstring& key) {
    std::istringstream in(ReadAll(file));
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == '!') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (FromUtf8(line.substr(0, eq)) == key) return UnescapeProperty(FromUtf8(line.substr(eq + 1)));
    }
    return {};
}

std::wstring LauncherFromBrand(const std::wstring& brand, const fs::path& dir) {
    if (Contains(brand, L"theseus") || Contains(brand, L"modrinth") || Contains(dir.wstring(), L"ModrinthApp")) return L"Modrinth App";
    if (Contains(brand, L"tlauncher") || Contains(dir.wstring(), L".tlauncher")) return L"TLauncher";
    if (Contains(brand, L"minecraft-launcher")) return L"Minecraft launcher";
    return brand.empty() ? L"Minecraft" : brand;
}

fs::file_time_type LogTime(const fs::path& dir) {
    std::error_code ec;
    const auto t = fs::last_write_time(dir / L"logs" / L"latest.log", ec);
    return ec ? fs::file_time_type::min() : t;
}

bool SamePath(const fs::path& a, const fs::path& b) {
    std::wstring x = Lower(a.wstring()), y = Lower(b.wstring());
    for (std::wstring* s : {&x, &y}) {
        for (wchar_t& c : *s) if (c == L'/') c = L'\\';
        while (!s->empty() && s->back() == L'\\') s->pop_back();
    }
    return x == y;
}

void AddUnique(std::vector<Detected>& list, Detected d) {
    for (const Detected& e : list) if (SamePath(e.dir, d.dir)) return;
    list.push_back(std::move(d));
}

}  // namespace

LaunchArgs ParseLaunchArgs(const std::wstring& cmd) {
    auto value = [&](const std::wstring& flag) -> std::wstring {
        const std::wstring text = cmd;
        for (std::size_t pos = text.find(flag); pos != std::wstring::npos; pos = text.find(flag, pos + 1)) {
            const bool startOk = pos == 0 || iswspace(text[pos - 1]) || text[pos - 1] == L'"';
            const std::size_t after = pos + flag.size();
            if (!startOk || after >= text.size() || !(iswspace(text[after]) || text[after] == L'=')) continue;
            std::size_t i = after;
            if (text[i] == L'=') {
                ++i;
            } else {
                while (i < text.size() && iswspace(text[i])) ++i;
            }
            if (i >= text.size()) return {};
            if (text[i] == L'"') {
                const std::size_t end = text.find(L'"', i + 1);
                return text.substr(i + 1, end == std::wstring::npos ? std::wstring::npos : end - i - 1);
            }
            std::size_t end = i;
            while (end < text.size() && !iswspace(text[end])) ++end;
            return text.substr(i, end - i);
        }
        return {};
    };
    LaunchArgs a;
    a.gameDir = value(L"--gameDir");
    a.version = value(L"--version");
    a.brand = value(L"-Dminecraft.launcher.brand");
    return a;
}

bool LooksLikeMinecraft(const std::wstring& cmd) {
    if (!Contains(cmd, L"--gameDir")) return false;
    return Contains(cmd, L"--assetsDir") || Contains(cmd, L"net.minecraft") || Contains(cmd, L"KnotClient") ||
           Contains(cmd, L"BootstrapLauncher") || Contains(cmd, L"--accessToken");
}

std::vector<Detected> RunningGames(const std::wstring& gameVersion) {
    std::vector<Detected> out;
    for (const wchar_t* exe : {L"javaw.exe", L"java.exe"}) {
        for (const DWORD pid : FindProcessIds(exe)) {
            const std::wstring cmd = ProcessCommandLine(pid);
            if (cmd.empty() || !LooksLikeMinecraft(cmd)) continue;
            const LaunchArgs a = ParseLaunchArgs(cmd);
            if (a.gameDir.empty()) continue;
            Detected d;
            d.dir = fs::path(a.gameDir);
            d.launcher = LauncherFromBrand(a.brand, d.dir);
            d.profile = a.version;
            d.running = true;
            d.pid = pid;
            d.fabric121 = Contains(cmd, L"fabric") && Contains(cmd, gameVersion.c_str());
            out.push_back(std::move(d));
        }
    }
    return out;
}

std::vector<Detected> InstalledGames(const fs::path& modrinthRootOverride, const std::wstring& gameVersion,
                                     const std::vector<fs::path>& extraDirs) {
    std::vector<Detected> out;
    std::error_code ec;
    for (const fs::path& dir : extraDirs) {
        if (!dir.empty() && fs::is_directory(dir, ec)) AddUnique(out, {L"Game folder", dir.filename().wstring(), dir, false, 0, true});
    }

    // Modrinth App: <root>\profiles\<name>; only profiles that carry fabric-api for the wanted version can load the mod.
    const std::wstring appData = EnvVar(L"APPDATA");
    const fs::path root = !modrinthRootOverride.empty() ? modrinthRootOverride : (appData.empty() ? fs::path() : fs::path(appData) / L"ModrinthApp");
    const std::wstring needle = L"+" + Lower(gameVersion);
    std::vector<Detected> modrinth;
    for (fs::directory_iterator it(root / L"profiles", ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_directory(e2)) continue;
        bool hasFabricApi = false;
        for (fs::directory_iterator m(it->path() / L"mods", e2), mend; !e2 && m != mend; m.increment(e2)) {
            const std::wstring file = Lower(m->path().filename().wstring());
            if (file.rfind(L"fabric-api-", 0) == 0 && file.find(needle) != std::wstring::npos && m->path().extension() == L".jar") {
                hasFabricApi = true;
                break;
            }
        }
        if (hasFabricApi) modrinth.push_back({L"Modrinth App", it->path().filename().wstring(), it->path(), false, 0, true});
    }
    std::sort(modrinth.begin(), modrinth.end(), [](const Detected& a, const Detected& b) { return LogTime(a.dir) > LogTime(b.dir); });
    for (Detected& d : modrinth) AddUnique(out, std::move(d));

    // TLauncher: the game folder is set in its properties file; otherwise it uses the plain .minecraft folder.
    if (!appData.empty()) {
        const fs::path tl = fs::path(appData) / L".tlauncher";
        const fs::path dotMc = fs::path(appData) / L".minecraft";
        const bool tlauncher = fs::is_directory(tl, ec) || fs::exists(dotMc / L"tlauncher_profiles.json", ec);
        const std::wstring configured = tlauncher ? PropertyValue(tl / L"tlauncher-2.0.properties", L"minecraft.gamedir") : std::wstring();
        for (const fs::path& dir : {fs::path(configured), dotMc}) {
            if (dir.empty() || !fs::is_directory(dir, ec)) continue;
            if (!fs::is_directory(dir / L"versions", ec) && !fs::is_directory(dir / L"mods", ec)) continue;
            AddUnique(out, {tlauncher ? L"TLauncher" : L"Minecraft launcher", dir.filename().wstring(), dir, false, 0, true});
        }
    }
    return out;
}

bool HasMod(const fs::path& gameDir) {
    std::error_code ec;
    for (fs::directory_iterator it(gameDir / L"mods", ec), end; !ec && it != end; it.increment(ec)) {
        const std::wstring file = Lower(it->path().filename().wstring());
        if (file.rfind(L"aeroclient", 0) == 0 && it->path().extension() == L".jar") return true;
    }
    return false;
}

fs::path DownloadsFolder() {
    PWSTR raw = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &raw)) && raw) result = raw;
    CoTaskMemFree(raw);
    return result;
}

std::wstring SaveToDownloads(const fs::path& jar, fs::path& saved) {
    std::error_code ec;
    if (!fs::is_regular_file(jar, ec)) return L"The mod jar is missing next to the hub: " + jar.wstring();
    const fs::path dir = DownloadsFolder();
    if (dir.empty() || !fs::is_directory(dir, ec)) return L"Windows did not give a Downloads folder";
    const fs::path target = dir / jar.filename();
    fs::copy_file(jar, target, fs::copy_options::overwrite_existing, ec);
    if (ec) return L"Cannot write " + target.wstring() + L" (" + std::to_wstring(ec.value()) + L")";
    saved = target;
    return {};
}

Live ReadLive(const fs::path& gameDir) {
    Live live;
    const fs::path file = gameDir / L"aeroclient-live.txt";
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &info)) return live;
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER written{}, current{};
    written.LowPart = info.ftLastWriteTime.dwLowDateTime;
    written.HighPart = info.ftLastWriteTime.dwHighDateTime;
    current.LowPart = now.dwLowDateTime;
    current.HighPart = now.dwHighDateTime;
    const double age = current.QuadPart > written.QuadPart ? static_cast<double>(current.QuadPart - written.QuadPart) / 1.0e7 : 0.0;
    live.fresh = age <= 15.0;

    std::map<std::wstring, std::wstring> kv;
    std::istringstream in(ReadAll(file));
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[FromUtf8(line.substr(0, eq))] = FromUtf8(line.substr(eq + 1));
    }
    if (const auto it = kv.find(L"pid"); it != kv.end()) live.pid = static_cast<DWORD>(wcstoul(it->second.c_str(), nullptr, 10));
    static const std::pair<const wchar_t*, const wchar_t*> kLabels[] = {
        {L"version", L"AeroClient"}, {L"world", L"In a world"}, {L"dimension", L"Dimension"}, {L"position", L"Position"}, {L"fps", L"FPS"},
        {L"modules", L"Modules on"}, {L"automine", L"AutoMine"}, {L"enabled", L"Enabled"},
    };
    for (const auto& [key, label] : kLabels) {
        if (const auto it = kv.find(key); it != kv.end()) live.rows.emplace_back(label, it->second);
    }
    return live;
}

}  // namespace aero::mc
