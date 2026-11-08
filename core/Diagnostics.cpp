#include "Diagnostics.hpp"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <string>

#include "Process.hpp"

namespace fs = std::filesystem;

namespace aero::diag {
namespace {

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring FromUtf8(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

bool EqNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

template <class Fn>
void ForEachLine(std::string_view text, Fn&& fn) {
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        fn(line);
        if (end == text.size()) break;
        pos = end + 1;
    }
}

// True for a line of the form `EnableVulkan = ...`; `eq` receives the offset of '=' inside the line.
bool IsVulkanKeyLine(std::string_view line, std::size_t& eq) {
    constexpr std::string_view key = "EnableVulkan";
    std::size_t i = 0;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (line.substr(i, key.size()) != key) return false;
    i += key.size();
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i >= line.size() || line[i] != '=') return false;
    eq = i;
    return true;
}

std::optional<std::string> ReadText(const fs::path& p, std::uintmax_t maxBytes) {
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    if (ec || size > maxBytes) return std::nullopt;
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    std::string s(static_cast<std::size_t>(size), '\0');
    f.read(s.data(), static_cast<std::streamsize>(s.size()));
    s.resize(static_cast<std::size_t>(f.gcount()));
    return s;
}

bool IsUtf16Le(std::string_view bytes) {
    return bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE;
}

// Forts writes log.txt as UTF-16 with a BOM; everything else is analysed and bundled as UTF-8.
std::string Utf16LeToUtf8(std::string_view bytes) {
    const int units = static_cast<int>((bytes.size() - 2) / 2);
    if (units <= 0) return {};
    const wchar_t* w = reinterpret_cast<const wchar_t*>(bytes.data() + 2);
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, units, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, units, s.data(), n, nullptr, nullptr);
    return s;
}

// Like ReadText, but a UTF-16 file comes back as UTF-8.
std::optional<std::string> ReadDecoded(const fs::path& p, std::uintmax_t maxBytes) {
    auto raw = ReadText(p, maxBytes);
    if (!raw) return std::nullopt;
    if (IsUtf16Le(*raw)) return Utf16LeToUtf8(*raw);
    return raw;
}

// head + marker + tail of a string; the tail starts on a UTF-8 character boundary.
std::string CapString(const std::string& s, std::uintmax_t head, std::uintmax_t tail) {
    if (s.size() <= head + tail) return s;
    std::size_t tailStart = s.size() - static_cast<std::size_t>(tail);
    while (tailStart < s.size() && (static_cast<unsigned char>(s[tailStart]) & 0xC0) == 0x80) ++tailStart;
    std::string out = s.substr(0, static_cast<std::size_t>(head));
    out += "\n...[" + std::to_string(tailStart - static_cast<std::size_t>(head)) + " bytes skipped by the AeroHub log collector]...\n";
    out += s.substr(tailStart);
    return out;
}

// The first `head` and last `tail` bytes of a file (all of it when it is not larger than head + tail).
std::optional<std::string> ReadCapped(const fs::path& p, std::uintmax_t head, std::uintmax_t tail) {
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    if (ec) return std::nullopt;
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    char bom[2]{};
    f.read(bom, 2);
    if (f.gcount() == 2 && IsUtf16Le(std::string_view(bom, 2))) {
        f.close();
        const auto decoded = ReadDecoded(p, 64u << 20);  // too big to decode: fall through to nullopt
        return decoded ? std::optional<std::string>(CapString(*decoded, head, tail)) : std::nullopt;
    }
    f.clear();
    f.seekg(0);
    auto readRange = [&](std::uintmax_t from, std::uintmax_t len) {
        std::string s(static_cast<std::size_t>(len), '\0');
        f.clear();
        f.seekg(static_cast<std::streamoff>(from));
        f.read(s.data(), static_cast<std::streamsize>(len));
        s.resize(static_cast<std::size_t>(f.gcount()));
        return s;
    };
    if (size <= head + tail) return readRange(0, size);
    std::string out = readRange(0, head);
    out += "\n...[" + std::to_string(size - head - tail) + " bytes skipped by the AeroHub log collector]...\n";
    out += readRange(size - tail, tail);
    return out;
}

std::string Stamp(const char* fmt) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[64]{};
    std::strftime(buf, sizeof(buf), fmt, &tm);
    return buf;
}

std::string WindowsVersion() {
    using Fn = LONG(WINAPI*)(OSVERSIONINFOW*);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto fn = nt ? reinterpret_cast<Fn>(GetProcAddress(nt, "RtlGetVersion")) : nullptr;
    OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (fn && fn(&vi) == 0) return std::to_string(vi.dwMajorVersion) + "." + std::to_string(vi.dwMinorVersion) + "." + std::to_string(vi.dwBuildNumber);
    return "unknown";
}

// TimeDateStamp and SizeOfImage of a PE file on disk.
std::optional<std::pair<std::uint32_t, std::uint32_t>> FileIdentity(const fs::path& exe) {
    std::ifstream f(exe, std::ios::binary);
    if (!f) return std::nullopt;
    IMAGE_DOS_HEADER dos{};
    f.read(reinterpret_cast<char*>(&dos), sizeof(dos));
    if (!f || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > (1 << 20)) return std::nullopt;
    f.seekg(dos.e_lfanew);
    IMAGE_NT_HEADERS64 nt{};
    f.read(reinterpret_cast<char*>(&nt), sizeof(nt));
    if (!f || nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return std::nullopt;
    return std::make_pair(static_cast<std::uint32_t>(nt.FileHeader.TimeDateStamp), static_cast<std::uint32_t>(nt.OptionalHeader.SizeOfImage));
}

std::wstring RunTar(const fs::path& zip, const fs::path& stage) {
    wchar_t sys[MAX_PATH]{};
    GetSystemDirectoryW(sys, MAX_PATH);
    const fs::path tar = fs::path(sys) / L"tar.exe";
    std::wstring cmd = L"\"" + tar.wstring() + L"\" -a -c -f \"" + zip.wstring() + L"\" -C \"" + stage.wstring() + L"\" .";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return L"cannot start tar.exe (GetLastError " + std::to_wstring(GetLastError()) + L"); this Windows has no built-in zip tool";
    UniqueHandle process(pi.hProcess), thread(pi.hThread);
    if (WaitForSingleObject(pi.hProcess, 120000) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        return L"tar.exe did not finish within 2 minutes";
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    return code == 0 ? L"" : L"tar.exe failed with exit code " + std::to_wstring(code);
}

}  // namespace

// ---------------------------------------------------------------------------------- pure helpers

std::vector<std::string> ParseLibraryPaths(std::string_view vdf) {
    std::vector<std::string> tokens;
    for (std::size_t i = 0; i < vdf.size();) {
        if (vdf[i] != '"') {
            ++i;
            continue;
        }
        std::string tok;
        ++i;
        while (i < vdf.size() && vdf[i] != '"') {
            if (vdf[i] == '\\' && i + 1 < vdf.size()) {
                tok += vdf[i + 1];
                i += 2;
            } else {
                tok += vdf[i++];
            }
        }
        ++i;
        tokens.push_back(std::move(tok));
    }
    std::vector<std::string> out;
    for (std::size_t k = 0; k + 1 < tokens.size(); ++k)
        if (EqNoCase(tokens[k], "path") && !tokens[k + 1].empty()) out.push_back(tokens[k + 1]);
    return out;
}

std::vector<std::string> LastModBlock(std::string_view logText) {
    constexpr std::string_view prefix = "Activating mod ";
    std::vector<std::vector<std::string>> groups;
    std::vector<std::string> cur;
    int gap = 0;
    ForEachLine(logText, [&](std::string_view line) {
        if (line.substr(0, prefix.size()) == prefix) {
            cur.emplace_back(line);
            gap = 0;
        } else if (!cur.empty() && ++gap > 3) {
            groups.push_back(std::move(cur));
            cur.clear();
            gap = 0;
        }
    });
    if (!cur.empty()) groups.push_back(std::move(cur));
    return groups.empty() ? std::vector<std::string>{} : groups.back();
}

std::string FirstLineWith(std::string_view text, std::string_view needle) {
    std::string found;
    bool done = false;
    ForEachLine(text, [&](std::string_view line) {
        if (!done && line.find(needle) != std::string_view::npos) {
            found = std::string(Trim(line));
            done = true;
        }
    });
    return found;
}

int CountLinesWith(std::string_view text, std::string_view needle) {
    int n = 0;
    ForEachLine(text, [&](std::string_view line) {
        if (line.find(needle) != std::string_view::npos) ++n;
    });
    return n;
}

std::optional<bool> ParseEnableVulkan(std::string_view luaText) {
    std::optional<bool> result;
    ForEachLine(luaText, [&](std::string_view line) {
        std::size_t eq = 0;
        if (!IsVulkanKeyLine(line, eq)) return;
        const std::string_view rest = Trim(line.substr(eq + 1));
        if (rest.substr(0, 4) == "true") result = true;
        else if (rest.substr(0, 5) == "false") result = false;
    });
    return result;
}

std::optional<std::string> WithOpenGl(std::string_view luaText) {
    std::string out;
    bool found = false;
    for (std::size_t pos = 0; pos < luaText.size();) {
        const std::size_t nl = luaText.find('\n', pos);
        const std::size_t lineEnd = nl == std::string_view::npos ? luaText.size() : nl;
        std::string line(luaText.substr(pos, lineEnd - pos));
        std::size_t eq = 0;
        if (IsVulkanKeyLine(line, eq)) {
            found = true;
            const std::size_t t = line.find("true", eq);
            if (t != std::string::npos) line.replace(t, 4, "false");
        }
        out += line;
        if (nl != std::string_view::npos) out += '\n';
        pos = nl == std::string_view::npos ? luaText.size() : nl + 1;
    }
    if (found) return out;
    const std::size_t close = out.rfind('}');
    if (close == std::string::npos) return std::nullopt;
    const char* eol = out.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::size_t lineStart = out.rfind('\n', close);
    lineStart = lineStart == std::string::npos ? 0 : lineStart + 1;
    out.insert(lineStart, std::string("\tEnableVulkan = false,") + eol);
    return out;
}

// ---------------------------------------------------------------------------------- Windows side

std::optional<fs::path> FindFortsDir() {
    auto looksRight = [](const fs::path& d) {
        std::error_code ec;
        return fs::is_regular_file(d / L"Forts.exe", ec);
    };
    if (const auto pid = FindProcessId(L"Forts.exe")) {
        UniqueHandle h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, *pid));
        if (h) {
            wchar_t buf[MAX_PATH * 4]{};
            DWORD n = static_cast<DWORD>(std::size(buf));
            if (QueryFullProcessImageNameW(h.get(), 0, buf, &n)) {
                const fs::path d = fs::path(std::wstring(buf, n)).parent_path();
                if (looksRight(d)) return d;
            }
        }
    }
    std::vector<fs::path> libs;
    wchar_t steam[MAX_PATH * 2]{};
    DWORD cb = static_cast<DWORD>(sizeof(steam) - sizeof(wchar_t));
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, steam, &cb) == ERROR_SUCCESS) libs.emplace_back(steam);
    libs.emplace_back(L"C:\\Program Files (x86)\\Steam");
    const std::size_t roots = libs.size();
    for (std::size_t i = 0; i < roots; ++i) {
        if (const auto vdf = ReadText(libs[i] / L"steamapps" / L"libraryfolders.vdf", 1 << 20)) {
            for (const std::string& p : ParseLibraryPaths(*vdf)) libs.emplace_back(FromUtf8(p));
        }
    }
    for (const fs::path& lib : libs) {
        const fs::path d = lib / L"steamapps" / L"common" / L"Forts";
        if (looksRight(d)) return d;
    }
    return std::nullopt;
}

GraphicsInfo CheckFortsGraphics(const fs::path& fortsDir) {
    GraphicsInfo info;
    std::error_code ec;
    fs::path best;
    fs::file_time_type bestTime{};
    for (fs::directory_iterator it(fortsDir / L"users", ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_directory(e2)) continue;
        const fs::path f = it->path() / L"settings_hardware.lua";
        const auto t = fs::last_write_time(f, e2);
        if (e2) continue;
        if (best.empty() || t > bestTime) {
            best = f;
            bestTime = t;
        }
    }
    if (best.empty()) return info;
    info.file = best;
    const auto text = ReadText(best, 1 << 20);
    if (!text) return info;
    const auto v = ParseEnableVulkan(*text);
    info.state = !v ? GraphicsState::NotSet : (*v ? GraphicsState::Vulkan : GraphicsState::OpenGl);
    return info;
}

std::wstring SwitchToOpenGl(const fs::path& file) {
    if (FindProcessId(L"Forts.exe")) return L"Close Forts first: the game rewrites this file when it exits.";
    const auto text = ReadText(file, 1 << 20);
    if (!text) return L"Cannot read " + file.wstring();
    const auto fixed = WithOpenGl(*text);
    if (!fixed) return L"The settings file has no table to edit: " + file.wstring();
    if (*fixed == *text) return L"";  // already OpenGL

    fs::path bak = file;
    bak += L".aerohub-bak";
    std::error_code ec;
    if (!fs::exists(bak, ec)) {
        fs::copy_file(file, bak, ec);
        if (ec) return L"Cannot back up the settings file (error " + std::to_wstring(ec.value()) + L")";
    }
    fs::path tmp = file;
    tmp += L".aerohub-tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(fixed->data(), static_cast<std::streamsize>(fixed->size()));
        f.flush();
        if (!f) return L"Cannot write " + tmp.wstring();
    }
    if (!MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD err = GetLastError();
        fs::remove(tmp, ec);
        return L"Cannot replace the settings file (GetLastError " + std::to_wstring(err) + L")";
    }
    const auto back = ReadText(file, 1 << 20);
    const auto v = back ? ParseEnableVulkan(*back) : std::nullopt;
    if (!v || *v) {
        CopyFileW(bak.c_str(), file.c_str(), FALSE);
        return L"The change did not stick; the original file was restored from the backup.";
    }
    return L"";
}

CollectResult CollectFortsLogs(const fs::path& hubExe, const fs::path& moduleLog, const fs::path& outDir) {
    CollectResult res;
    wchar_t tmpDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmpDir);
    const fs::path stage = fs::path(tmpDir) / (L"AeroHubLogs-" + FromUtf8(Stamp("%Y%m%d-%H%M%S")));
    std::error_code ec;
    fs::remove_all(stage, ec);
    fs::create_directories(stage, ec);
    if (ec) {
        res.error = L"Cannot create a temporary folder: " + stage.wstring();
        return res;
    }

    auto put = [&](const std::wstring& name, const std::string& content) {
        const fs::path dst = stage / name;
        std::error_code e;
        fs::create_directories(dst.parent_path(), e);
        std::ofstream f(dst, std::ios::binary | std::ios::trunc);
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
        f.flush();
        if (f) res.files.push_back(name);
    };

    std::string gameLog;  // the biggest user's log.txt, used for the summary
    std::string settingsLine;
    std::string summary;
    const auto fortsDir = FindFortsDir();
    std::vector<std::wstring> notes;

    if (fortsDir) {
        const fs::path users = *fortsDir / L"users";
        for (fs::directory_iterator it(users, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (!it->is_directory(e2)) continue;
            const std::wstring id = it->path().filename().wstring();
            if (const auto log = ReadCapped(it->path() / L"log.txt", 600 * 1024, 1024 * 1024)) {
                put(L"forts\\users\\" + id + L"\\log.txt", *log);
                if (gameLog.empty()) gameLog = ReadDecoded(it->path() / L"log.txt", 16u << 20).value_or(*log);
            }
            if (const auto s = ReadText(it->path() / L"settings_hardware.lua", 1 << 20)) {
                put(L"forts\\users\\" + id + L"\\settings_hardware.lua", *s);
                if (const auto v = ParseEnableVulkan(*s)) settingsLine = *v ? "EnableVulkan = true  (Vulkan: the module cannot work)" : "EnableVulkan = false (OpenGL)";
                else settingsLine = "EnableVulkan is not set in the file";
            }
            // Desync reports: group the folders by session ("desync-139 <date_time>"), take the newest 3 sessions and,
            // from each, the earliest report (that is the first frame where the clients disagreed).
            struct Rep {
                fs::path dir;
                fs::file_time_type time;
            };
            std::map<std::wstring, std::vector<Rep>> sessions;
            std::error_code e3;
            for (fs::directory_iterator d(it->path() / L"desyncs", e3), dend; !e3 && d != dend; d.increment(e3)) {
                std::error_code e4;
                if (!d->is_directory(e4)) continue;
                const std::wstring name = d->path().filename().wstring();
                const std::size_t sp1 = name.find(L' ');
                const std::size_t sp2 = sp1 == std::wstring::npos ? sp1 : name.find(L' ', sp1 + 1);
                const std::wstring key = sp2 == std::wstring::npos ? name : name.substr(0, sp2);
                sessions[key].push_back({d->path(), fs::last_write_time(d->path(), e4)});
            }
            std::vector<std::pair<fs::file_time_type, Rep>> picked;  // (newest time of the session, earliest report)
            for (auto& [key, reps] : sessions) {
                const auto newest = std::max_element(reps.begin(), reps.end(), [](const Rep& a, const Rep& b) { return a.time < b.time; })->time;
                const auto earliest = *std::min_element(reps.begin(), reps.end(), [](const Rep& a, const Rep& b) { return a.time < b.time; });
                picked.emplace_back(newest, earliest);
            }
            std::sort(picked.begin(), picked.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            if (picked.size() > 3) picked.resize(3);
            for (const auto& [t, rep] : picked) {
                const std::wstring dname = rep.dir.filename().wstring();
                for (fs::directory_iterator f(rep.dir, e3), fend; !e3 && f != fend; f.increment(e3)) {
                    const std::wstring fn = f->path().filename().wstring();
                    const bool isChecksum = fn.find(L"data-checksum") != std::wstring::npos;
                    const bool isLog = fn.find(L"-Log-") != std::wstring::npos;
                    if (!isChecksum && !isLog) continue;
                    if (const auto c = ReadCapped(f->path(), 400 * 1024, 700 * 1024)) put(L"forts\\users\\" + id + L"\\desyncs\\" + dname + L"\\" + fn, *c);
                }
            }
        }
    } else {
        notes.push_back(L"The Forts folder was not found (not running, not in a Steam library).");
    }

    if (const auto m = ReadCapped(moduleLog, 0, 2u << 20)) put(L"aerohub\\" + moduleLog.filename().wstring(), *m);
    else notes.push_back(L"No module log yet: " + moduleLog.wstring());

    // summary.txt
    summary += "AeroHub log bundle\r\n";
    summary += "Created:   " + Stamp("%Y-%m-%d %H:%M:%S") + " (local time)\r\n";
    summary += "Windows:   " + WindowsVersion() + "\r\n";
    summary += "Hub:       " + ToUtf8(hubExe.wstring()) + "  (built " + __DATE__ + " " + __TIME__ + ")\r\n";
    if (fortsDir) {
        summary += "Forts dir: " + ToUtf8(fortsDir->wstring()) + "\r\n";
        if (const auto id = FileIdentity(*fortsDir / L"Forts.exe")) {
            char b[96];
            std::snprintf(b, sizeof(b), "Forts.exe: TimeDateStamp 0x%08X, SizeOfImage 0x%X (file on disk)\r\n", id->first, id->second);
            summary += b;
        }
    }
    if (!gameLog.empty()) {
        for (const char* key : {"Forts Version", "Steam App BuildId", "video driver version"}) {
            const std::string l = FirstLineWith(gameLog, key);
            if (!l.empty()) summary += "Game log:  " + l + "\r\n";
        }
        summary += "Game log:  lines mentioning 'out of sync': " + std::to_string(CountLinesWith(gameLog, "out of sync")) + "\r\n";
        const auto mods = LastModBlock(gameLog);
        summary += "Mods in the last activation block of the game log (" + std::to_string(mods.size()) + "):\r\n";
        for (const std::string& m : mods) summary += "  " + m + "\r\n";
    }
    if (!settingsLine.empty()) summary += "settings_hardware.lua: " + settingsLine + "\r\n";
    for (const std::wstring& n : notes) summary += "Note: " + ToUtf8(n) + "\r\n";
    summary += "\r\nThe files below also contain your Windows user name (in paths) and your Steam name and ID.\r\nFiles:\r\n";
    for (const std::wstring& f : res.files) summary += "  " + ToUtf8(f) + "\r\n";
    put(L"summary.txt", summary);

    fs::create_directories(outDir, ec);
    res.zip = outDir / (L"AeroHub-logs-" + FromUtf8(Stamp("%Y%m%d-%H%M%S")) + L".zip");
    res.error = RunTar(res.zip, stage);
    fs::remove_all(stage, ec);
    if (res.error.empty()) {
        std::error_code e5;
        if (!fs::is_regular_file(res.zip, e5)) res.error = L"The zip file was not created: " + res.zip.wstring();
    }
    return res;
}

}  // namespace aero::diag
