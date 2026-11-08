// AeroHub: control panel for game cheat modules.
//  - sidebar with every game profile and its state
//  - header with the selected game's status and one primary action (Inject / Launch)
//  - live telemetry once the module is loaded (shared-memory control channel); the cheats themselves
//    are switched only in the game's own menu
//  - live module log
// Everything is painted with GDI+ into one window (DPI-aware, double-buffered); the two log views are
// native read-only edit controls placed inside the log card.
// Design tokens follow the ui-ux-pro-max recommendation: dark (OLED) surfaces, calm blue accent,
// semantic status colors always paired with text, visible hover / pressed / focus / disabled states.
#include <windows.h>
#include <windowsx.h>

#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Diagnostics.hpp"
#include "Inject.hpp"
#include "Ipc.hpp"
#include "Minecraft.hpp"
#include "Pe.hpp"
#include "Process.hpp"
#include "Profile.hpp"
#include "Ui.hpp"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace {

using Gdiplus::Color;
using Gdiplus::RectF;

// ---------------------------------------------------------------- design tokens
using namespace aero::ui;

constexpr UINT_PTR kTimer = 1;
constexpr UINT_PTR kEditHub = 301, kEditModule = 302;
constexpr double kMinProcessAgeForAutoInject = 8.0;  // seconds; lets the game finish resolving its imports
constexpr int kStaleTicksBeforeWarning = 3;

enum class Level { Idle, Ready, Active, Bad };

struct Status {
    std::wstring text;  // long explanation
    std::wstring word;  // short state for the sidebar and the pill; empty = the generic word for the level
    Level level = Level::Idle;
    bool canInject = false;
    DWORD pid = 0;
};

struct InfoRow {
    std::wstring key, value;
};
struct Link {
    std::wstring text = L"Offline";
    Level level = Level::Idle;
};

enum class Act { None, SelectGame, Primary, Secondary, Refresh, AutoSwitch, TabModule, TabHub, CollectLogs, SwitchOpenGl };
struct Hit {
    RectF r;
    Act act = Act::None;
    int idx = 0;
    bool enabled = true;
};

// ---------------------------------------------------------------- state
HWND g_hwnd, g_editHub, g_editModule;
HBRUSH g_surfaceBrush;
HFONT g_editFont;
int g_dpi = 96;
std::filesystem::path g_dir, g_settingsFile;
std::vector<aero::GameProfile> g_profiles;
std::filesystem::path g_modrinthRoot;                 // AeroHub.ini [Hub] ModrinthRoot: a Modrinth data folder in a non-default place
std::vector<std::filesystem::path> g_extraInstances;  // AeroHub.ini [Hub] MinecraftInstances: extra game folders, ';' separated

// The Minecraft tab: what auto-detect found and where "Install mod" put the jar.
struct McState {
    bool found = false;
    aero::mc::Detected game;
    bool waiting = false;  // nothing found on disk: the hub watches for a running Minecraft process
    std::wstring message;
    std::filesystem::path savedJar;
} g_mc;
std::vector<Status> g_status;
std::vector<DWORD> g_autoTriedPid;
int g_sel = 0;
bool g_autoInject = false;
int g_tab = 0;  // 0 module log, 1 hub log
bool g_demo = false;

aero::ipc::Client g_client;
DWORD g_clientPid = 0;
LONG g_lastBeat = -1;
int g_staleTicks = 0;
std::vector<InfoRow> g_infoRows;
Link g_link;

// Forts reads settings_hardware.lua at start; the module only works when EnableVulkan is false (OpenGL).
std::optional<std::filesystem::path> g_fortsDir;
ULONGLONG g_fortsDirTried = 0;  // GetTickCount64 of the last folder search; the search is repeated every 10 s while nothing is found
aero::diag::GraphicsInfo g_gfx;

int g_tailProfile = -1;
std::filesystem::path g_tailPath;
std::uintmax_t g_tailPos = 0;

std::vector<Hit> g_hits;
Act g_hoverAct = Act::None, g_pressAct = Act::None;
int g_hoverIdx = -1, g_pressIdx = -1;

ULONG_PTR g_gdiToken;
std::unique_ptr<Gdiplus::Font> g_fTitle, g_fH2, g_fBody, g_fBodyBold, g_fSmall, g_fMono, g_fBrand;

float S(float v) { return v * static_cast<float>(g_dpi) / 96.0f; }

COLORREF LevelColor(Level l) {
    switch (l) {
        case Level::Ready: return kWarn;
        case Level::Active: return kOk;
        case Level::Bad: return kBad;
        default: return kDim;
    }
}

const wchar_t* LevelWord(Level l) {
    switch (l) {
        case Level::Ready: return L"Ready to inject";
        case Level::Active: return L"Active";
        case Level::Bad: return L"Needs attention";
        default: return L"Not running";
    }
}

std::wstring StatusWord(std::size_t i);

// ---------------------------------------------------------------- helpers (data)
std::wstring StatusWord(std::size_t i) {
    return i < g_status.size() && !g_status[i].word.empty() ? g_status[i].word : std::wstring(LevelWord(g_status[i].level));
}

std::wstring Hex(std::uint32_t v) {
    wchar_t b[16];
    swprintf_s(b, L"0x%X", v);
    return b;
}

std::wstring FromUtf8(const char* s, std::size_t maxLen) {
    std::size_t n = 0;
    while (n < maxLen && s[n]) ++n;
    if (n == 0) return {};
    const int w = MultiByteToWideChar(CP_UTF8, 0, s, static_cast<int>(n), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(w), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, static_cast<int>(n), out.data(), w);
    return out;
}

void AppendText(HWND edit, const std::wstring& text) {
    const int len = GetWindowTextLengthW(edit);
    SendMessageW(edit, EM_SETSEL, len, len);
    SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
}

void Log(const std::wstring& line) { AppendText(g_editHub, line + L"\r\n"); }

double ProcessAgeSeconds(DWORD pid) {
    aero::UniqueHandle h(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    if (!h) return 0.0;
    FILETIME created, exited, kernel, user, nowFt;
    if (!GetProcessTimes(h.get(), &created, &exited, &kernel, &user)) return 0.0;
    GetSystemTimeAsFileTime(&nowFt);
    ULARGE_INTEGER a{}, b{};
    a.LowPart = created.dwLowDateTime;
    a.HighPart = created.dwHighDateTime;
    b.LowPart = nowFt.dwLowDateTime;
    b.HighPart = nowFt.dwHighDateTime;
    return b.QuadPart > a.QuadPart ? static_cast<double>(b.QuadPart - a.QuadPart) / 1.0e7 : 0.0;
}

std::wstring LowerSlashes(std::wstring s) {
    for (wchar_t& c : s) c = c == L'/' ? L'\\' : static_cast<wchar_t>(towlower(c));
    while (!s.empty() && s.back() == L'\\') s.pop_back();
    return s;
}

bool ProcessAlive(DWORD pid) {
    aero::UniqueHandle h(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    return h && WaitForSingleObject(h.get(), 0) == WAIT_TIMEOUT;
}

std::wstring GameLabel(const aero::mc::Detected& d) {
    return d.profile.empty() ? d.launcher : d.launcher + L" \u00B7 " + d.profile;
}

// The Minecraft tab. Nothing is injected: the hub finds the game, reads the status file the mod writes, and says what is missing.
Status EvaluateMinecraft(const aero::GameProfile& p) {
    Status s;
    const auto jar = g_dir / p.modFile;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(jar, ec)) {
        s.level = Level::Bad;
        s.word = L"Jar missing";
        s.text = L"aeroclient.jar is missing next to the hub";
        return s;
    }
    if (!g_mc.found) {
        s.level = g_mc.waiting ? Level::Ready : Level::Idle;
        s.word = g_mc.waiting ? L"Waiting" : L"Not detected";
        s.text = g_mc.message.empty() ? L"Press Auto-detect Minecraft" : g_mc.message;
        return s;
    }
    const aero::mc::Live live = aero::mc::ReadLive(g_mc.game.dir);
    const bool reporting = live.fresh && live.pid != 0 && ProcessAlive(live.pid);
    DWORD runPid = 0;
    bool compatible = true;
    for (const aero::mc::Detected& r : aero::mc::RunningGames(p.gameVersion)) {
        if (LowerSlashes(r.dir.wstring()) == LowerSlashes(g_mc.game.dir.wstring())) {
            runPid = r.pid;
            compatible = r.fabric121;
            break;
        }
    }
    s.pid = reporting ? live.pid : runPid;
    if (reporting) {
        s.level = Level::Active;
        s.text = L"AeroClient is loaded in the running game";
        return s;
    }
    const bool hasMod = aero::mc::HasMod(g_mc.game.dir);
    if (runPid != 0) {
        if (!compatible) {
            s.level = Level::Ready;
            s.word = L"Not Fabric " + p.gameVersion;
            s.text = L"This game is not Fabric " + p.gameVersion + L": the mod will not load";
        } else if (!hasMod) {
            s.level = Level::Ready;
            s.word = L"No mod";
            s.text = L"Running without the mod: add it, then restart";
        } else if (ProcessAgeSeconds(runPid) < 120.0) {
            s.word = L"Starting";
            s.text = L"The game is starting";
        } else {
            s.level = Level::Ready;
            s.word = L"Restart needed";
            s.text = L"The mod is in the mods folder but not loaded: restart";
        }
        return s;
    }
    if (hasMod) {
        s.word = L"Mod added";
        s.text = L"The mod is in the mods folder: start Minecraft";
    } else {
        s.level = Level::Ready;
        s.word = L"No mod";
        s.text = L"Install the mod and drop it into the mods folder";
    }
    return s;
}

Status Evaluate(const aero::GameProfile& p) {
    if (p.kind == aero::GameKind::FabricMod) return EvaluateMinecraft(p);
    Status s;
    const auto dll = g_dir / p.moduleDll;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(dll, ec)) {
        s.level = Level::Bad;
        s.text = L"Module DLL missing: " + dll.wstring();
        return s;
    }
    const auto pid = aero::FindProcessId(p.processName);
    if (!pid) {
        s.text = L"The game is not running.";
        return s;
    }
    s.pid = *pid;
    aero::UniqueHandle proc(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, *pid));
    if (!proc) {
        s.level = Level::Bad;
        s.text = L"Cannot open the game process. Run the hub as the same user as the game.";
        return s;
    }
    const auto base = aero::FindModuleBase(*pid, p.processName);
    const auto id = base ? aero::ReadImageIdentity(proc.get(), *base) : std::nullopt;
    if (!id) {
        s.level = Level::Bad;
        s.text = L"Cannot read the game's PE header.";
        return s;
    }
    if (id->timeDateStamp != p.timeDateStamp || id->sizeOfImage != p.sizeOfImage) {
        s.level = Level::Bad;
        s.text = L"Unsupported game build: stamp " + Hex(id->timeDateStamp) + L", size " + Hex(id->sizeOfImage) +
                 L" (profile expects " + Hex(p.timeDateStamp) + L" / " + Hex(p.sizeOfImage) + L").";
        return s;
    }
    const std::wstring build = L"Build matches the profile (" + Hex(p.timeDateStamp) + L" / " + Hex(p.sizeOfImage) + L").";
    if (aero::FindModuleBase(*pid, dll.filename().wstring())) {
        s.level = Level::Active;
        s.text = build + L" Module loaded.";
        return s;
    }
    s.level = Level::Ready;
    s.text = build + L" Module not loaded yet.";
    s.canInject = true;
    return s;
}

int SelectedProfile() { return g_sel >= 0 && g_sel < static_cast<int>(g_profiles.size()) ? g_sel : -1; }

void Redraw() { InvalidateRect(g_hwnd, nullptr, FALSE); }

void FillDemoData() {
    g_infoRows = {{L"Game state", L"in match"}, {L"Local team", L"1"}, {L"Resource A (team +0x58)", L"unlimited"},
                  {L"Resource B (team +0x50)", L"unlimited"}, {L"Own weapons", L"5"}};
    g_link = {L"Live", Level::Active};
}

// Keeps g_client attached to the selected game's running process and mirrors its features / telemetry.
void UpdateChannel() {
    if (g_demo) {
        FillDemoData();
        return;
    }
    const int sel = SelectedProfile();
    if (sel >= 0 && g_profiles[sel].kind == aero::GameKind::FabricMod) {
        g_client.Close();
        g_clientPid = 0;
        g_infoRows.clear();
        g_link = {L"Offline", Level::Idle};
        if (g_mc.found) {
            const aero::mc::Live live = aero::mc::ReadLive(g_mc.game.dir);
            if (live.fresh) {
                g_link = {L"Live", Level::Active};
                for (const auto& [k, v] : live.rows) g_infoRows.push_back({k, v});
            }
        }
        return;
    }
    const bool active = sel >= 0 && g_status[sel].level == Level::Active;
    g_link = {L"Offline", Level::Idle};

    if (!active) {
        g_client.Close();
        g_clientPid = 0;
    } else {
        const DWORD pid = g_status[sel].pid;
        if (!g_client.Connected() || g_clientPid != pid) {
            if (g_client.Open(pid)) {
                g_clientPid = pid;
                g_lastBeat = -1;
                g_staleTicks = 0;
                Log(g_profiles[sel].name + L": control channel connected");
            } else {
                g_clientPid = 0;
            }
        }
        if (!g_client.Connected()) {
            g_link = {L"No channel", Level::Bad};
        } else {
            const LONG beat = g_client.View()->heartbeat;
            g_staleTicks = (beat == g_lastBeat) ? g_staleTicks + 1 : 0;
            g_lastBeat = beat;
            g_link = g_staleTicks >= kStaleTicksBeforeWarning ? Link{L"No frames", Level::Ready} : Link{L"Live", Level::Active};
        }
    }

    g_infoRows.clear();
    if (const aero::ipc::Block* b = g_client.Connected() ? g_client.View() : nullptr) {
        const int nt = std::clamp<int>(b->telemetryCount, 0, aero::ipc::kMaxTelemetry);
        for (int i = 0; i < nt; ++i)
            g_infoRows.push_back({FromUtf8(b->telemetry[i].name, sizeof(b->telemetry[i].name)),
                                  FromUtf8(b->telemetry[i].value, sizeof(b->telemetry[i].value))});
    }
}

const aero::GameProfile* McProfile() {
    for (const aero::GameProfile& p : g_profiles)
        if (p.kind == aero::GameKind::FabricMod) return &p;
    return nullptr;
}

void SetDetected(const aero::mc::Detected& d, const wchar_t* how) {
    g_mc.found = true;
    g_mc.game = d;
    g_mc.waiting = false;
    g_mc.message.clear();
    g_tailProfile = -2;  // makes the module log reopen on the new folder
    WritePrivateProfileStringW(L"Minecraft", L"GameDir", d.dir.c_str(), g_settingsFile.c_str());
    WritePrivateProfileStringW(L"Minecraft", L"Launcher", d.launcher.c_str(), g_settingsFile.c_str());
    WritePrivateProfileStringW(L"Minecraft", L"Profile", d.profile.c_str(), g_settingsFile.c_str());
    Log(L"Minecraft: found " + GameLabel(d) + L" (" + how + L") in " + d.dir.wstring());
}

void LoadMinecraftSettings() {
    wchar_t dir[2048]{}, launcher[256]{}, profile[256]{};
    GetPrivateProfileStringW(L"Minecraft", L"GameDir", L"", dir, static_cast<DWORD>(std::size(dir)), g_settingsFile.c_str());
    GetPrivateProfileStringW(L"Minecraft", L"Launcher", L"", launcher, static_cast<DWORD>(std::size(launcher)), g_settingsFile.c_str());
    GetPrivateProfileStringW(L"Minecraft", L"Profile", L"", profile, static_cast<DWORD>(std::size(profile)), g_settingsFile.c_str());
    std::error_code ec;
    if (dir[0] && std::filesystem::is_directory(dir, ec)) {
        g_mc.found = true;
        g_mc.game.dir = dir;
        g_mc.game.launcher = launcher;
        g_mc.game.profile = profile;
    }
}

// A running game is the most exact answer (it is the folder the game really uses); otherwise the Modrinth App,
// TLauncher and .minecraft are searched. When nothing is found the hub asks for the game to be started and keeps
// looking at the running processes.
void AutoDetect() {
    const aero::GameProfile* p = McProfile();
    if (!p) return;
    const auto running = aero::mc::RunningGames(p->gameVersion);
    if (!running.empty()) {
        const aero::mc::Detected* pick = &running[0];
        for (const auto& r : running) {
            if (r.fabric121) {
                pick = &r;
                break;
            }
        }
        SetDetected(*pick, L"running game");
        if (running.size() > 1) Log(L"Minecraft: " + std::to_wstring(running.size()) + L" games are running; using the first one that is Fabric " + p->gameVersion);
        return;
    }
    const auto installed = aero::mc::InstalledGames(g_modrinthRoot, p->gameVersion, g_extraInstances);
    if (!installed.empty()) {
        SetDetected(installed[0], L"on disk");
        for (std::size_t i = 1; i < installed.size(); ++i) Log(L"Minecraft: also found " + GameLabel(installed[i]) + L" in " + installed[i].dir.wstring());
        return;
    }
    g_mc.found = false;
    g_mc.waiting = true;
    g_mc.message = L"Not found: start Minecraft, the hub will find it";
    Log(L"Minecraft: nothing found in the Modrinth App, TLauncher or .minecraft. Start Minecraft and the hub will find it through its process.");
}

void PollWaiting() {
    if (!g_mc.waiting) return;
    const aero::GameProfile* p = McProfile();
    if (!p) return;
    const auto running = aero::mc::RunningGames(p->gameVersion);
    if (running.empty()) return;
    const aero::mc::Detected* pick = &running[0];
    for (const auto& r : running) {
        if (r.fabric121) {
            pick = &r;
            break;
        }
    }
    SetDetected(*pick, L"running game");
}

// "Install mod": the jar goes to the Downloads folder; the user drops it into the game's mods folder.
void SaveMod() {
    const aero::GameProfile* p = McProfile();
    if (!p) return;
    std::filesystem::path saved;
    const std::wstring err = aero::mc::SaveToDownloads(g_dir / p->modFile, saved);
    if (!err.empty()) {
        Log(L"Minecraft: " + err);
        return;
    }
    g_mc.savedJar = saved;
    Log(L"Minecraft: the mod is saved as " + saved.wstring() + L". Drop it into the mods folder of your game" +
        (g_mc.found ? L": " + (g_mc.game.dir / L"mods").wstring() : std::wstring(L".")));
    ShellExecuteW(g_hwnd, L"open", L"explorer.exe", (L"/select,\"" + saved.wstring() + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------- Forts: graphics setting and log bundle
bool IsForts(const aero::GameProfile& p) {
    return p.kind == aero::GameKind::NativeModule && _wcsicmp(p.processName.c_str(), L"Forts.exe") == 0;
}

void UpdateGraphics() {
    const int sel = SelectedProfile();
    if (sel < 0 || !IsForts(g_profiles[sel])) return;
    if (g_demo) {  // preview of the warning; the actions are disabled in demo mode
        g_gfx.state = aero::diag::GraphicsState::Vulkan;
        g_gfx.file = L"<demo>\\settings_hardware.lua";
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (!g_fortsDir && now - g_fortsDirTried > 10000) {
        g_fortsDirTried = now;
        g_fortsDir = aero::diag::FindFortsDir();
    }
    if (g_fortsDir) g_gfx = aero::diag::CheckFortsGraphics(*g_fortsDir);
}

void DoSwitchOpenGl() {
    if (g_demo) {
        Log(L"demo: the settings file is not changed");
        return;
    }
    if (g_gfx.file.empty()) return;
    const bool vulkan = g_gfx.state == aero::diag::GraphicsState::Vulkan;
    const std::wstring text =
        (vulkan ? L"Forts is set to Vulkan, and the AeroHub module only works with OpenGL.\n\nSet EnableVulkan = false in\n"
                : L"The Forts settings file has no EnableVulkan entry, so the game picks the graphics API itself. The AeroHub module only works with OpenGL.\n\nAdd EnableVulkan = false to\n") +
        g_gfx.file.wstring() +
        L"\n\nA backup copy (settings_hardware.lua.aerohub-bak) is kept next to it. Forts must be closed; the change applies the next time the game starts.";
    if (MessageBoxW(g_hwnd, text.c_str(), L"Switch to OpenGL", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    const std::wstring err = aero::diag::SwitchToOpenGl(g_gfx.file);
    if (!err.empty()) {
        Log(L"Switch to OpenGL failed: " + err);
        MessageBoxW(g_hwnd, err.c_str(), L"Switch to OpenGL", MB_OK | MB_ICONWARNING);
        return;
    }
    Log(L"Forts is now set to OpenGL (EnableVulkan = false). Backup: " + g_gfx.file.wstring() + L".aerohub-bak. Start the game to apply it.");
    UpdateGraphics();
}

void DoCollectLogs() {
    const int sel = SelectedProfile();
    if (sel < 0 || !IsForts(g_profiles[sel])) return;
    if (g_demo) {
        Log(L"demo: logs are not collected");
        return;
    }
    const int answer = MessageBoxW(
        g_hwnd,
        L"AeroHub will pack the Forts game log, the newest desync reports, the AeroHub module log and a short summary into one zip file on your Desktop.\n\n"
        L"The files contain your Windows user name (in file paths) and your Steam name and ID. Nothing is uploaded: you send the zip yourself.\n\nCreate the zip?",
        L"Collect logs", MB_YESNO | MB_ICONQUESTION);
    if (answer != IDYES) return;
    std::filesystem::path outDir = g_dir;
    PWSTR desktop = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop))) outDir = desktop;
    CoTaskMemFree(desktop);
    std::filesystem::path moduleLog = g_dir / g_profiles[sel].moduleDll;
    moduleLog.replace_extension(L".log");
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const HCURSOR previous = SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    const auto r = aero::diag::CollectFortsLogs(exe, moduleLog, outDir);
    SetCursor(previous);
    if (!r.error.empty()) {
        Log(L"Collect logs failed: " + r.error);
        MessageBoxW(g_hwnd, (L"Could not create the zip:\n" + r.error).c_str(), L"Collect logs", MB_OK | MB_ICONERROR);
        return;
    }
    Log(L"Logs packed into " + r.zip.wstring() + L" (" + std::to_wstring(r.files.size()) + L" files). Send this file.");
    ShellExecuteW(g_hwnd, L"open", L"explorer.exe", (L"/select,\"" + r.zip.wstring() + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}

void Refresh() {
    g_status.clear();
    for (const auto& p : g_profiles) g_status.push_back(Evaluate(p));
    UpdateGraphics();
    if (g_demo && !g_status.empty()) {
        g_status[0].level = Level::Active;
        g_status[0].text = L"Build matches the profile (demo). Module loaded.";
        g_status[0].pid = 1;
    }
    if (g_sel >= static_cast<int>(g_profiles.size())) g_sel = 0;
    g_autoTriedPid.resize(g_profiles.size(), 0);
    UpdateChannel();
    Redraw();
}

void Inject(std::size_t i, bool automatic) {
    const Status fresh = Evaluate(g_profiles[i]);  // re-check right before acting
    if (!fresh.canInject) {
        if (!automatic) Log(g_profiles[i].name + L": not injecting - " + fresh.text);
        return;
    }
    const auto r = aero::InjectDll(fresh.pid, g_dir / g_profiles[i].moduleDll);
    Log(g_profiles[i].name + (automatic ? L" (auto): " : L": ") + aero::ToString(r.status) +
        (r.lastError ? L" (GetLastError " + std::to_wstring(r.lastError) + L")" : L""));
}

void AutoInject() {
    if (!g_autoInject) return;
    for (std::size_t i = 0; i < g_profiles.size(); ++i) {
        if (!g_status[i].canInject || g_autoTriedPid[i] == g_status[i].pid) continue;
        if (ProcessAgeSeconds(g_status[i].pid) < kMinProcessAgeForAutoInject) continue;
        g_autoTriedPid[i] = g_status[i].pid;  // one attempt per game process, success or not
        Inject(i, true);
    }
}

struct PrimaryState {
    std::wstring label;
    bool enabled;
};

PrimaryState GetPrimary() {
    const int sel = SelectedProfile();
    if (sel < 0) return {L"No game", false};
    const Status& s = g_status[sel];
    if (g_profiles[sel].kind == aero::GameKind::FabricMod) return {L"Auto-detect Minecraft", true};
    if (s.canInject) return {L"Inject module", true};
    if (s.level == Level::Idle && s.pid == 0 && !g_profiles[sel].steamAppId.empty()) return {L"Launch game", true};
    if (s.level == Level::Active) return {L"Module active", false};
    return {L"Unavailable", false};
}

void DoSecondary() {
    const int sel = SelectedProfile();
    if (sel < 0 || g_profiles[sel].kind != aero::GameKind::FabricMod) return;
    SaveMod();
}

void DoPrimary() {
    const int sel = SelectedProfile();
    if (sel < 0) return;
    if (g_profiles[sel].kind == aero::GameKind::FabricMod) {
        AutoDetect();
        Refresh();
        return;
    }
    if (g_status[sel].canInject) {
        Inject(static_cast<std::size_t>(sel), false);
        Refresh();
    } else if (g_status[sel].level == Level::Idle && !g_profiles[sel].steamAppId.empty()) {
        ShellExecuteW(g_hwnd, L"open", (L"steam://rungameid/" + g_profiles[sel].steamAppId).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        Log(g_profiles[sel].name + L": launching through Steam");
    }
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    std::wstring out;
    out.reserve(w.size() + 16);
    for (std::size_t i = 0; i < w.size(); ++i) {
        if (w[i] == L'\n' && (i == 0 || w[i - 1] != L'\r')) out += L'\r';
        out += w[i];
    }
    return out;
}

void PollModuleLog() {
    const int sel = SelectedProfile();
    if (sel != g_tailProfile) {
        g_tailProfile = sel;
        SetWindowTextW(g_editModule, L"");
        g_tailPos = 0;
        g_tailPath.clear();
        if (sel >= 0) {
            if (g_profiles[sel].kind == aero::GameKind::FabricMod) {
                g_tailPath = g_mc.found ? g_mc.game.dir / L"logs" / L"latest.log" : std::filesystem::path();
            } else {
                g_tailPath = g_dir / g_profiles[sel].moduleDll;
                g_tailPath.replace_extension(L".log");
            }
            std::error_code ec;
            const auto size = std::filesystem::file_size(g_tailPath, ec);
            if (!ec && size > 6000) g_tailPos = size - 6000;  // start with the most recent lines
        }
    }
    if (g_tailPath.empty()) return;
    std::error_code ec;
    const auto size = std::filesystem::file_size(g_tailPath, ec);
    if (ec) return;
    if (size < g_tailPos) {  // file was recreated
        g_tailPos = 0;
        SetWindowTextW(g_editModule, L"");
    }
    if (size == g_tailPos) return;
    std::ifstream f(g_tailPath, std::ios::binary);
    if (!f) return;
    f.seekg(static_cast<std::streamoff>(g_tailPos));
    std::string chunk(static_cast<std::size_t>(std::min<std::uintmax_t>(size - g_tailPos, 1 << 16)), '\0');
    f.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    chunk.resize(static_cast<std::size_t>(f.gcount()));
    g_tailPos += chunk.size();
    AppendText(g_editModule, Utf8ToWide(chunk));
}

void SaveSettings() {
    WritePrivateProfileStringW(L"Hub", L"AutoInject", g_autoInject ? L"1" : L"0", g_settingsFile.c_str());
}

// ---------------------------------------------------------------- layout
struct Lay {
    RectF sidebar, header, guide, live, log, logTabs, logBody;
    float margin = 0, gap = 0;
};

Lay Compute(int w, int h) {
    Lay L;
    const float W = static_cast<float>(w), H = static_cast<float>(h);
    const float sb = S(244), m = S(24), gap = S(16);
    L.margin = m;
    L.gap = gap;
    L.sidebar = RectF(0, 0, sb, H);
    const float x0 = sb + m, cw = max(S(200), W - sb - 2 * m);
    float y = m;
    L.header = RectF(x0, y, cw, S(84));
    y += S(84) + S(12);
    float logH = std::clamp(H * 0.32f, S(190), S(300));
    float cardsH = H - y - m - gap - logH;
    if (cardsH < S(210)) {
        cardsH = S(210);
        logH = max(S(120), H - y - m - gap - cardsH);
    }
    const float leftW = cw * 0.58f - gap / 2;
    L.guide = RectF(x0, y, leftW, cardsH);
    L.live = RectF(x0 + leftW + gap, y, cw - leftW - gap, cardsH);
    y += cardsH + gap;
    L.log = RectF(x0, y, cw, max(S(120), H - y - m));
    L.logTabs = RectF(L.log.X + S(16), L.log.Y + S(12), S(260), S(32));
    L.logBody = RectF(L.log.X + S(1), L.log.Y + S(56), L.log.Width - S(2), L.log.Height - S(57));
    return L;
}

void PositionLogs() {
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    const Lay L = Compute(rc.right, rc.bottom);
    const int x = static_cast<int>(L.logBody.X + S(12)), y = static_cast<int>(L.logBody.Y),
              w = static_cast<int>(L.logBody.Width - S(24)), h = static_cast<int>(L.logBody.Height - S(8));
    MoveWindow(g_editModule, x, y, w, h, TRUE);
    MoveWindow(g_editHub, x, y, w, h, TRUE);
    ShowWindow(g_editModule, g_tab == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_editHub, g_tab == 1 ? SW_SHOW : SW_HIDE);
}

void MakeFonts() {
    auto mk = [](const wchar_t* fam, float px, int style) {
        return std::make_unique<Gdiplus::Font>(fam, S(px), style, Gdiplus::UnitPixel);
    };
    g_fTitle = mk(L"Segoe UI", 24, Gdiplus::FontStyleBold);
    g_fH2 = mk(L"Segoe UI", 15, Gdiplus::FontStyleBold);
    g_fBody = mk(L"Segoe UI", 14, Gdiplus::FontStyleRegular);
    g_fBodyBold = mk(L"Segoe UI", 14, Gdiplus::FontStyleBold);
    g_fSmall = mk(L"Segoe UI", 12, Gdiplus::FontStyleRegular);
    g_fMono = mk(L"Consolas", 13, Gdiplus::FontStyleRegular);
    g_fBrand = mk(L"Segoe UI", 18, Gdiplus::FontStyleBold);
    if (g_editFont) DeleteObject(g_editFont);
    g_editFont = CreateFontW(-static_cast<int>(S(13)), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    if (g_editHub) SendMessageW(g_editHub, WM_SETFONT, reinterpret_cast<WPARAM>(g_editFont), TRUE);
    if (g_editModule) SendMessageW(g_editModule, WM_SETFONT, reinterpret_cast<WPARAM>(g_editFont), TRUE);
}

// ---------------------------------------------------------------- drawing primitives
void AddHit(Act act, int idx, const RectF& r, bool enabled = true) { g_hits.push_back({r, act, idx, enabled}); }

bool IsHover(Act a, int idx) { return g_hoverAct == a && g_hoverIdx == idx; }
bool IsPressed(Act a, int idx) { return g_pressAct == a && g_pressIdx == idx && IsHover(a, idx); }

// Thin wrappers: the drawing itself lives in core/Ui.hpp and is shared with the in-game menu.
float DrawPill(Gdiplus::Graphics& g, float x, float y, const std::wstring& text, COLORREF c) {
    return aero::ui::DrawPill(g, S(1), *g_fSmall, x, y, text, c);
}

void DrawButton(Gdiplus::Graphics& g, const RectF& r, const std::wstring& label, bool primary, bool enabled, bool hover,
                bool pressed, bool focus = false) {
    aero::ui::DrawButton(g, S(1), *g_fBodyBold, r, label, primary, enabled, hover, pressed, focus);
}

void DrawSwitch(Gdiplus::Graphics& g, const RectF& r, bool on, bool enabled, bool hover) {
    aero::ui::DrawSwitch(g, S(1), r, on, enabled, hover);
}
void DrawCard(Gdiplus::Graphics& g, const RectF& r) {
    FillRR(g, r, S(12), kSurface);
    StrokeRR(g, r, S(12), kBorder);
}

void DrawEmptyState(Gdiplus::Graphics& g, const RectF& r, const std::wstring& title, const std::wstring& hint) {
    const float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2 - S(20);
    Gdiplus::Pen pen(Col(kBorder), S(2));
    g.DrawEllipse(&pen, cx - S(18), cy - S(34), S(36), S(36));
    FillDot(g, cx, cy - S(16), S(4), kDim);
    DrawStr(g, title, *g_fH2, RectF(r.X + S(16), cy + S(12), r.Width - S(32), S(24)), kText, Gdiplus::StringAlignmentCenter);
    DrawStr(g, hint, *g_fSmall, RectF(r.X + S(16), cy + S(38), r.Width - S(32), S(22)), kDim, Gdiplus::StringAlignmentCenter);
}

// ---------------------------------------------------------------- screen
void DrawSidebar(Gdiplus::Graphics& g, const Lay& L) {
    FillRR(g, RectF(0, 0, 1, 1), 0, kSidebar, 0);  // keep GDI+ path warm; no visual effect
    Gdiplus::SolidBrush sb(Col(kSidebar));
    g.FillRectangle(&sb, L.sidebar);
    Gdiplus::Pen edge(Col(kBorder), 1.0f);
    g.DrawLine(&edge, L.sidebar.Width - 0.5f, 0.0f, L.sidebar.Width - 0.5f, L.sidebar.Height);

    const float x = S(20);
    float y = S(24);
    FillRR(g, RectF(x, y, S(36), S(36)), S(10), kAccent);
    DrawStr(g, L"A", *g_fBrand, RectF(x, y, S(36), S(36)), RGB(255, 255, 255), Gdiplus::StringAlignmentCenter);
    DrawStr(g, L"AeroHub", *g_fH2, RectF(x + S(48), y - S(2), S(150), S(22)), kText);
    DrawStr(g, L"Game tool hub", *g_fSmall, RectF(x + S(48), y + S(18), S(150), S(18)), kDim);
    y += S(64);
    DrawStr(g, L"GAMES", *g_fSmall, RectF(x, y, S(120), S(18)), kDim);
    y += S(26);

    for (std::size_t i = 0; i < g_profiles.size(); ++i) {
        const RectF r(S(12), y, L.sidebar.Width - S(24), S(58));
        const bool sel = static_cast<int>(i) == g_sel, hov = IsHover(Act::SelectGame, static_cast<int>(i));
        if (sel) FillRR(g, r, S(10), kSurface2);
        else if (hov) FillRR(g, r, S(10), kSurface2, 120);
        if (sel) FillRR(g, RectF(r.X, r.Y + S(12), S(3), r.Height - S(24)), S(1.5f), kAccentRing);
        const COLORREF c = LevelColor(g_status[i].level);
        FillDot(g, r.X + S(22), r.Y + r.Height / 2, S(5), c);
        DrawStr(g, g_profiles[i].name, *g_fBodyBold, RectF(r.X + S(40), r.Y + S(8), r.Width - S(48), S(22)), kText);
        DrawStr(g, StatusWord(i), *g_fSmall, RectF(r.X + S(40), r.Y + S(30), r.Width - S(48), S(20)), kDim);
        AddHit(Act::SelectGame, static_cast<int>(i), r);
        y += S(62);
    }

    // Auto-inject switch pinned to the bottom.
    const float by = L.sidebar.Height - S(104);
    Gdiplus::Pen div(Col(kBorder), 1.0f);
    g.DrawLine(&div, S(20), by, L.sidebar.Width - S(20), by);
    DrawStr(g, L"Auto-inject", *g_fBodyBold, RectF(x, by + S(14), S(150), S(22)), kText);
    DrawStr(g, L"Loads the module a few seconds", *g_fSmall, RectF(x, by + S(36), S(190), S(18)), kDim);
    DrawStr(g, L"after the game starts", *g_fSmall, RectF(x, by + S(52), S(190), S(18)), kDim);
    const RectF sw(L.sidebar.Width - S(20) - S(44), by + S(16), S(44), S(24));
    DrawSwitch(g, sw, g_autoInject, true, IsHover(Act::AutoSwitch, 0));
    AddHit(Act::AutoSwitch, 0, RectF(x, by + S(8), L.sidebar.Width - 2 * x, S(70)));
}

void DrawHeader(Gdiplus::Graphics& g, const Lay& L) {
    const RectF& h = L.header;
    const int sel = SelectedProfile();
    if (sel < 0) {
        DrawStr(g, L"No game profiles", *g_fTitle, RectF(h.X, h.Y, h.Width, S(36)), kText);
        DrawStr(g, L"Add a profile .ini to the profiles folder next to the hub.", *g_fBody, RectF(h.X, h.Y + S(40), h.Width, S(24)), kDim);
        return;
    }
    const Status& st = g_status[sel];
    DrawStr(g, g_profiles[sel].name, *g_fTitle, RectF(h.X, h.Y, h.Width * 0.5f, S(36)), kText);

    // Actions on the right. Minecraft has two (auto-detect, install mod); every other game has its primary action and Refresh.
    const PrimaryState prim = GetPrimary();
    const float bh = S(40);
    float leftmost;
    if (g_profiles[sel].kind == aero::GameKind::FabricMod) {
        const RectF install(h.X + h.Width - S(150), h.Y + S(2), S(150), bh);
        const RectF detect(install.X - S(12) - S(210), h.Y + S(2), S(210), bh);
        DrawButton(g, detect, prim.label, true, prim.enabled, IsHover(Act::Primary, 0), IsPressed(Act::Primary, 0));
        DrawButton(g, install, L"Install mod", false, true, IsHover(Act::Secondary, 0), IsPressed(Act::Secondary, 0));
        AddHit(Act::Primary, 0, detect, prim.enabled);
        AddHit(Act::Secondary, 0, install);
        leftmost = detect.X;
    } else {
        const RectF refresh(h.X + h.Width - S(96), h.Y + S(2), S(96), bh);
        const RectF primary(refresh.X - S(12) - S(176), h.Y + S(2), S(176), bh);
        DrawButton(g, primary, prim.label, true, prim.enabled, IsHover(Act::Primary, 0), IsPressed(Act::Primary, 0));
        DrawButton(g, refresh, L"Refresh", false, true, IsHover(Act::Refresh, 0), IsPressed(Act::Refresh, 0));
        AddHit(Act::Primary, 0, primary, prim.enabled);
        AddHit(Act::Refresh, 0, refresh);
        leftmost = primary.X;
        if (IsForts(g_profiles[sel])) {
            const RectF logs(primary.X - S(12) - S(150), h.Y + S(2), S(150), bh);
            DrawButton(g, logs, L"Collect logs", false, true, IsHover(Act::CollectLogs, 0), IsPressed(Act::CollectLogs, 0));
            AddHit(Act::CollectLogs, 0, logs);
            leftmost = logs.X;
        }
    }

    // Status pill and details under the title.
    const float py = h.Y + S(44);
    const float pw = DrawPill(g, h.X, py, StatusWord(static_cast<std::size_t>(sel)), LevelColor(st.level));
    DrawStr(g, st.text, *g_fSmall, RectF(h.X + pw + S(14), py, max(S(100), leftmost - (h.X + pw + S(14)) - S(12)), S(26)), kDim);
}

void DrawGuide(Gdiplus::Graphics& g, const Lay& L) {
    const RectF& c = L.guide;
    DrawCard(g, c);
    DrawStr(g, L"In-game menu", *g_fH2, RectF(c.X + S(20), c.Y + S(14), c.Width - S(40), S(24)), kText);
    const int sel = SelectedProfile();
    const bool active = sel >= 0 && g_status[sel].level == Level::Active;
    const bool minecraft = sel >= 0 && g_profiles[sel].kind == aero::GameKind::FabricMod;
    const std::wstring lead = minecraft ? (active ? L"AeroClient is loaded. Everything is switched in the game itself."
                                          : g_mc.found ? L"Install the mod, drop it into the mods folder and start the game."
                                                       : L"Press Auto-detect Minecraft. If it finds nothing, start the game.")
                              : active   ? L"Cheats are switched in the game itself. Open its menu during a match."
                                         : L"Launch the game and load the module; cheats are then switched in the game's own menu.";
    // The module hooks OpenGL's SwapBuffers, so a Forts set to Vulkan never shows the menu: say so and offer the fix.
    float shift = 0;
    if (sel >= 0 && IsForts(g_profiles[sel]) &&
        (g_gfx.state == aero::diag::GraphicsState::Vulkan || g_gfx.state == aero::diag::GraphicsState::NotSet)) {
        shift = S(58);
        const RectF strip(c.X + S(20), c.Y + S(46), c.Width - S(40), S(48));
        FillRR(g, strip, S(8), kWarn, 34);
        StrokeRR(g, strip, S(8), kWarn, 1.0f, 150);
        const RectF btn(strip.X + strip.Width - S(160) - S(6), strip.Y + S(6), S(160), strip.Height - S(12));
        const bool vulkan = g_gfx.state == aero::diag::GraphicsState::Vulkan;
        DrawStr(g, vulkan ? L"Forts is on Vulkan: needs OpenGL" : L"Graphics API not set: needs OpenGL", *g_fSmall,
                RectF(strip.X + S(14), strip.Y, strip.Width - btn.Width - S(30), strip.Height), kText);
        DrawButton(g, btn, L"Switch to OpenGL", true, true, IsHover(Act::SwitchOpenGl, 0), IsPressed(Act::SwitchOpenGl, 0));
        AddHit(Act::SwitchOpenGl, 0, btn);
    }
    DrawStr(g, lead, *g_fBody, RectF(c.X + S(20), c.Y + S(50) + shift, c.Width - S(40), S(24)), kText);

    if (sel < 0) return;
    // One line per "|"-separated hint. The Minecraft tab starts with what it knows about the game and the mod file.
    std::wstring hk = g_profiles[sel].hotkeys;
    if (minecraft) {
        std::wstring info = g_mc.found ? L"Game: " + GameLabel(g_mc.game) + L"  |  Folder: " + g_mc.game.dir.wstring() + L"  |  Mod: " +
                                             (aero::mc::HasMod(g_mc.game.dir) ? L"in the mods folder" : L"not in the mods folder yet")
                                       : std::wstring(L"Game: not detected yet");
        info += L"  |  Mod file: " + (g_mc.savedJar.empty() ? std::wstring(L"press Install mod to put it in Downloads") : g_mc.savedJar.wstring());
        hk = info + (hk.empty() ? L"" : L"  |  " + hk);
    }
    if (hk.empty()) return;
    float y = c.Y + S(92) + shift;
    const float lh = S(34);
    Gdiplus::Pen sep(Col(kBorder), 1.0f);
    for (std::size_t start = 0; start <= hk.size();) {
        std::size_t end = hk.find(L'|', start);
        if (end == std::wstring::npos) end = hk.size();
        std::wstring part = hk.substr(start, end - start);
        start = end + 1;
        const auto b = part.find_first_not_of(L' ');
        const auto e = part.find_last_not_of(L' ');
        if (b == std::wstring::npos) continue;
        part = part.substr(b, e - b + 1);
        if (y + lh > c.Y + c.Height - S(10)) break;
        FillDot(g, c.X + S(26), y + lh / 2, S(3), kAccentRing);
        DrawStr(g, part, *g_fSmall, RectF(c.X + S(40), y, c.Width - S(60), lh), kDim);
        g.DrawLine(&sep, c.X + S(20), y + lh - 0.5f, c.X + c.Width - S(20), y + lh - 0.5f);
        y += lh;
    }
}
void DrawLive(Gdiplus::Graphics& g, const Lay& L) {
    const RectF& c = L.live;
    DrawCard(g, c);
    DrawStr(g, L"Live info", *g_fH2, RectF(c.X + S(20), c.Y + S(14), c.Width - S(160), S(24)), kText);
    // Link pill on the right.
    {
        const float w = S(26) + S(8) * 2 + MeasureW(g, g_link.text, *g_fSmall) + S(8);
        DrawPill(g, c.X + c.Width - S(20) - w, c.Y + S(13), g_link.text, LevelColor(g_link.level));
    }
    if (g_infoRows.empty()) {
        DrawEmptyState(g, RectF(c.X, c.Y + S(40), c.Width, c.Height - S(40)), L"No live data", L"Values appear while the module is active in a match.");
        return;
    }
    float y = c.Y + S(52);
    const float rowH = S(36);
    Gdiplus::Pen sep(Col(kBorder), 1.0f);
    for (const InfoRow& r : g_infoRows) {
        if (y + rowH > c.Y + c.Height - S(8)) break;
        DrawStr(g, r.key, *g_fSmall, RectF(c.X + S(20), y, c.Width * 0.55f, rowH), kDim);
        DrawStr(g, r.value, *g_fMono, RectF(c.X + c.Width * 0.5f, y, c.Width * 0.5f - S(20), rowH), kText, Gdiplus::StringAlignmentFar);
        g.DrawLine(&sep, c.X + S(20), y + rowH - 0.5f, c.X + c.Width - S(20), y + rowH - 0.5f);
        y += rowH;
    }
}

void DrawLog(Gdiplus::Graphics& g, const Lay& L) {
    DrawCard(g, L.log);
    const RectF& t = L.logTabs;
    FillRR(g, t, S(8), kBg);
    const float w1 = S(132), w2 = S(100);
    const RectF tab1(t.X + S(3), t.Y + S(3), w1, t.Height - S(6)), tab2(tab1.X + w1 + S(4), t.Y + S(3), w2, t.Height - S(6));
    auto drawTab = [&](const RectF& r, const wchar_t* text, bool active, Act act) {
        if (active) FillRR(g, r, S(6), kSurface2);
        else if (IsHover(act, 0)) FillRR(g, r, S(6), kSurface2, 110);
        DrawStr(g, text, *g_fSmall, r, active ? kText : kDim, Gdiplus::StringAlignmentCenter);
        AddHit(act, 0, r);
    };
    drawTab(tab1, L"Module log", g_tab == 0, Act::TabModule);
    drawTab(tab2, L"Hub log", g_tab == 1, Act::TabHub);
    DrawStr(g, L"live", *g_fSmall, RectF(L.log.X + L.log.Width - S(70), L.log.Y + S(12), S(54), S(32)), kDim, Gdiplus::StringAlignmentFar);
    Gdiplus::Pen sep(Col(kBorder), 1.0f);
    g.DrawLine(&sep, L.log.X + S(1), L.log.Y + S(55), L.log.X + L.log.Width - S(1), L.log.Y + S(55));
}

void Render(Gdiplus::Graphics& g, int w, int h) {
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    g.Clear(Col(kBg));
    g_hits.clear();
    const Lay L = Compute(w, h);
    DrawSidebar(g, L);
    DrawHeader(g, L);
    DrawGuide(g, L);
    DrawLive(g, L);
    DrawLog(g, L);
}

void Paint(HDC dc, int w, int h) {
    if (w <= 0 || h <= 0) return;
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bm = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ old = SelectObject(mem, bm);
    {
        Gdiplus::Graphics g(mem);
        Render(g, w, h);
    }
    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bm);
    DeleteDC(mem);
}

// ---------------------------------------------------------------- input
bool HitAt(int x, int y, Act& act, int& idx, bool& enabled) {
    const Gdiplus::PointF p(static_cast<float>(x), static_cast<float>(y));
    for (auto it = g_hits.rbegin(); it != g_hits.rend(); ++it) {
        if (it->r.Contains(p)) {
            act = it->act;
            idx = it->idx;
            enabled = it->enabled;
            return true;
        }
    }
    return false;
}

void Perform(Act act, int idx) {
    switch (act) {
        case Act::SelectGame:
            if (idx != g_sel) {
                g_sel = idx;
                UpdateChannel();
                PollModuleLog();
            }
            break;
        case Act::Primary: DoPrimary(); break;
        case Act::Secondary: DoSecondary(); break;
        case Act::Refresh: Refresh(); break;
        case Act::AutoSwitch: g_autoInject = !g_autoInject; SaveSettings(); break;
        case Act::TabModule: g_tab = 0; PositionLogs(); break;
        case Act::TabHub: g_tab = 1; PositionLogs(); break;
        case Act::CollectLogs: DoCollectLogs(); break;
        case Act::SwitchOpenGl: DoSwitchOpenGl(); break;
        case Act::None: break;
    }
    Redraw();
}

void OnKey(WPARAM key) {
    switch (key) {
        case VK_PRIOR:
        case VK_NEXT:
            if (g_profiles.size() > 1) Perform(Act::SelectGame, (g_sel + (key == VK_NEXT ? 1 : static_cast<int>(g_profiles.size()) - 1)) % static_cast<int>(g_profiles.size()));
            break;
        case VK_F5: Refresh(); break;
        default: return;
    }
    Redraw();
}
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            g_hwnd = hwnd;
            const BOOL dark = TRUE;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            g_dpi = static_cast<int>(GetDpiForWindow(hwnd));
            const DWORD style = WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL;
            g_editModule = CreateWindowExW(0, L"EDIT", L"", style | WS_VISIBLE, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(kEditModule), nullptr, nullptr);
            g_editHub = CreateWindowExW(0, L"EDIT", L"", style, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(kEditHub), nullptr, nullptr);
            for (HWND e : {g_editModule, g_editHub}) {
                SendMessageW(e, EM_SETLIMITTEXT, 1 << 20, 0);
                SetWindowTheme(e, L"DarkMode_Explorer", nullptr);
            }
            MakeFonts();

            g_autoInject = GetPrivateProfileIntW(L"Hub", L"AutoInject", 0, g_settingsFile.c_str()) != 0;
            std::vector<std::wstring> errors;
            g_profiles = aero::LoadProfiles(g_dir / L"profiles", errors);
            wchar_t buf[2048]{};
            GetPrivateProfileStringW(L"Hub", L"ModrinthRoot", L"", buf, static_cast<DWORD>(std::size(buf)), g_settingsFile.c_str());
            g_modrinthRoot = buf;
            GetPrivateProfileStringW(L"Hub", L"MinecraftInstances", L"", buf, static_cast<DWORD>(std::size(buf)), g_settingsFile.c_str());
            for (std::size_t start = 0, len = wcslen(buf); start < len;) {
                std::size_t end = std::wstring_view(buf, len).find(L';', start);
                if (end == std::wstring_view::npos) end = len;
                if (end > start) g_extraInstances.emplace_back(std::wstring(buf + start, end - start));
                start = end + 1;
            }
            Log(L"Loaded " + std::to_wstring(g_profiles.size()) + L" game profile(s).");
            for (const auto& e : errors) Log(L"profile skipped: " + e);
            LoadMinecraftSettings();
            if (!g_mc.found) AutoDetect();
            Refresh();
            PositionLogs();
            SetTimer(hwnd, kTimer, 1000, nullptr);
            return 0;
        }
        case WM_SIZE: PositionLogs(); Redraw(); return 0;
        case WM_DPICHANGED: {
            g_dpi = HIWORD(wp);
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            MakeFonts();
            PositionLogs();
            Redraw();
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize = {static_cast<LONG>(S(980)), static_cast<LONG>(S(700))};
            return 0;
        }
        case WM_TIMER:
            PollWaiting();
            Refresh();
            AutoInject();
            PollModuleLog();
            return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            Paint(dc, rc.right, rc.bottom);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, kText);
            SetBkColor(dc, kSurface);
            return reinterpret_cast<LRESULT>(g_surfaceBrush);
        }
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&t);
            Act a = Act::None;
            int idx = -1;
            bool en = false;
            if (!HitAt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), a, idx, en) || !en) { a = Act::None; idx = -1; }
            if (a != g_hoverAct || idx != g_hoverIdx) {
                g_hoverAct = a;
                g_hoverIdx = idx;
                Redraw();
            }
            return 0;
        }
        case WM_MOUSELEAVE: g_hoverAct = Act::None; g_hoverIdx = -1; Redraw(); return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && g_hoverAct != Act::None) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
            break;
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            Act a = Act::None;
            int idx = -1;
            bool en = false;
            if (HitAt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), a, idx, en) && en) { g_pressAct = a; g_pressIdx = idx; }
            Redraw();
            return 0;
        }
        case WM_LBUTTONUP: {
            Act a = Act::None;
            int idx = -1;
            bool en = false;
            const bool hit = HitAt(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), a, idx, en) && en;
            const Act pa = g_pressAct;
            const int pi = g_pressIdx;
            g_pressAct = Act::None;
            g_pressIdx = -1;
            if (hit && a == pa && idx == pi) Perform(a, idx);
            else Redraw();
            return 0;
        }
        case WM_KEYDOWN: OnKey(wp); return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kTimer);
            g_client.Close();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_demo = cmdLine && std::wstring(cmdLine).find(L"--demo") != std::wstring::npos;  // preview with fake data

    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g_dir = std::filesystem::path(exe).parent_path();
    g_settingsFile = g_dir / L"AeroHub.ini";

    Gdiplus::GdiplusStartupInput gin;
    Gdiplus::GdiplusStartup(&g_gdiToken, &gin, nullptr);
    g_surfaceBrush = CreateSolidBrush(kSurface);
    g_dpi = static_cast<int>(GetDpiForSystem());

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"AeroHubWnd";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"AeroHub", WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                              static_cast<int>(S(1200)), static_cast<int>(S(780)), nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    ShowWindow(hwnd, show);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    g_fTitle.reset(); g_fH2.reset(); g_fBody.reset(); g_fBodyBold.reset(); g_fSmall.reset(); g_fMono.reset(); g_fBrand.reset();
    Gdiplus::GdiplusShutdown(g_gdiToken);
    return static_cast<int>(m.wParam);
}
