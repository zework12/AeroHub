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

#include <cmath>
#include <cstdio>
#include <ctime>
#include <map>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "Diagnostics.hpp"
#include "Inject.hpp"
#include "Ipc.hpp"
#include "License.hpp"
#include "Minecraft.hpp"
#include "SlimeRancher.hpp"
#include "Pe.hpp"
#include "Process.hpp"
#include "Profile.hpp"
#include "Ui.hpp"
#include "Update.hpp"
#include "Version.hpp"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace {

using Gdiplus::Color;
using Gdiplus::RectF;

// ---------------------------------------------------------------- design tokens
using namespace aero::ui;

constexpr UINT_PTR kTimer = 1;
constexpr UINT kMsgUpdate = WM_APP + 1;  // wParam: 1 = the version check finished, 2 = the installer download finished
constexpr UINT_PTR kEditHub = 301, kEditModule = 302, kEditKey = 303;
constexpr UINT kMsgLicense = WM_APP + 2;  // the licence check finished
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

enum class Act { None, SelectGame, Primary, Secondary, Refresh, AutoSwitch, TabModule, TabHub, CollectLogs, SwitchOpenGl, Update, LangEn, LangRu, SrPart, LogToggle, WinMin, WinMax, WinClose, LicenseActivate, LicenseSite, LicenseAccount, LicenseSignOut };
struct Hit {
    RectF r;
    Act act = Act::None;
    int idx = 0;
    bool enabled = true;
};

// ---------------------------------------------------------------- state
HWND g_hwnd, g_editHub, g_editModule, g_editKey;
HBRUSH g_surfaceBrush;
HFONT g_editFont;
int g_dpi = 96;
std::filesystem::path g_dir, g_settingsFile;
std::vector<aero::GameProfile> g_profiles;
std::vector<aero::GameProfile> g_allProfiles;  // every installed profile; g_profiles holds the ones the licence opens
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

// The Slime Rancher tab: which part is chosen (1 or 2) and what the Steam search found for it.
struct SrState {
    int part = 1;
    bool found = false;
    aero::sr::Install install;
    int ticksSinceSearch = 0;
} g_sr;
std::vector<Status> g_status;
std::vector<DWORD> g_autoTriedPid;
int g_sel = 0;
bool g_autoInject = false;
int g_tab = 0;  // 0 module log, 1 hub log
bool g_demo = false;
bool g_demoVulkan = false;  // --demo-vulkan: the demo also shows the "Forts is on Vulkan" warning

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

// Interface language: 0 = English, 1 = Russian (AeroHub.ini [Hub] Language=en|ru; the first run follows the Windows UI language).
// The Hub log and the module's own lines stay English on purpose: they are what a bug report shows.
int g_lang = 0;
const wchar_t* Tr(const wchar_t* en, const wchar_t* ru) { return g_lang == 1 ? ru : en; }

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
        case Level::Ready: return Tr(L"Ready to inject", L"Готов к внедрению");
        case Level::Active: return Tr(L"Active", L"Активен");
        case Level::Bad: return Tr(L"Needs attention", L"Нужно внимание");
        default: return Tr(L"Not running", L"Не запущена");
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

// ---------------------------------------------------------------- updates
// The hub asks GitHub for the latest release (at start and on the sidebar button). Installing is always the user's click: the installer
// is downloaded, checked against the SHA-256 GitHub reports for that file, and then started while the hub closes.
enum class UpState { Idle, Checking, UpToDate, Available, Downloading, Failed };
struct UpdateState {
    UpState state = UpState::Idle;
    aero::update::Release release;
    std::wstring message;
} g_up;
std::mutex g_upLock;
bool g_checkUpdates = true;  // AeroHub.ini [Hub] CheckUpdates=0 turns the check at start off

UpState UpdateNow() {
    std::lock_guard<std::mutex> lock(g_upLock);
    return g_up.state;
}

std::filesystem::path UpdateInstallerPath() {
    wchar_t tmp[MAX_PATH + 2]{};
    const DWORD n = GetTempPathW(static_cast<DWORD>(std::size(tmp)), tmp);
    return (n > 0 && n < std::size(tmp) ? std::filesystem::path(tmp) : std::filesystem::path(L"C:\\Windows\\Temp")) / L"AeroHub-Update" / L"AeroHub-Setup.exe";
}

void StartVersionCheck() {
    if (g_demo) return;
    {
        std::lock_guard<std::mutex> lock(g_upLock);
        if (g_up.state == UpState::Checking || g_up.state == UpState::Downloading) return;
        g_up.state = UpState::Checking;
        g_up.message.clear();
    }
    const HWND hwnd = g_hwnd;
    std::thread([hwnd] {
        aero::update::Release rel = aero::update::FetchLatest();
        {
            std::lock_guard<std::mutex> lock(g_upLock);
            if (!rel.ok) {
                g_up.state = UpState::Failed;
                g_up.message = rel.error;
            } else {
                const bool newer = aero::update::IsNewer(aero::update::ParseVersion(rel.tag), aero::update::ParseVersion(aero::kAppVersion));
                g_up.release = rel;
                g_up.state = newer ? UpState::Available : UpState::UpToDate;
            }
        }
        PostMessageW(hwnd, kMsgUpdate, 1, 0);
    }).detach();
}

void StartInstallerDownload() {
    aero::update::Release rel;
    {
        std::lock_guard<std::mutex> lock(g_upLock);
        if (g_up.state != UpState::Available) return;
        g_up.state = UpState::Downloading;
        rel = g_up.release;
    }
    const HWND hwnd = g_hwnd;
    std::thread([hwnd, rel] {
        std::wstring error;
        const bool ok = aero::update::Download(rel, UpdateInstallerPath(), error);
        {
            std::lock_guard<std::mutex> lock(g_upLock);
            g_up.state = UpState::Available;  // a failed download can be retried with the same button
            g_up.message = error;
        }
        PostMessageW(hwnd, kMsgUpdate, 2, ok ? 1 : 0);
    }).detach();
}

// Starts the downloaded installer (after a short pause, so this process has exited and its exe is not locked) and closes the hub.
bool LaunchInstallerAndQuit() {
    const std::wstring path = UpdateInstallerPath().wstring();
    std::wstring cmd;
    DWORD flags = 0;
    if (path.find_first_of(L"%^&!\"") == std::wstring::npos) {
        cmd = L"cmd.exe /d /s /c \"ping -n 3 127.0.0.1 >nul & \"" + path + L"\"\"";
        flags = CREATE_NO_WINDOW;
    } else {
        cmd = L"\"" + path + L"\"";
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, flags | DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
        Log(L"Update: cannot start the installer (error " + std::to_wstring(GetLastError()) + L")");
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    DestroyWindow(g_hwnd);
    return true;
}

void DoUpdate() {
    const UpState st = UpdateNow();
    if (st == UpState::Idle || st == UpState::UpToDate || st == UpState::Failed) {
        StartVersionCheck();
        return;
    }
    if (st != UpState::Available) return;
    for (const Status& s : g_status) {
        if (s.level == Level::Active) {
            MessageBoxW(g_hwnd, Tr(L"The game is running with the module loaded. Close the game first: the update replaces the module file.",
                           L"Игра запущена с загруженным модулем. Сначала закройте игру: обновление заменяет файл модуля."),
                        Tr(L"Update AeroHub", L"Обновление AeroHub"), MB_OK | MB_ICONINFORMATION);
            return;
        }
    }
    std::wstring tag;
    {
        std::lock_guard<std::mutex> lock(g_upLock);
        tag = g_up.release.tag;
    }
    std::wstring text = std::wstring(Tr(L"Install AeroHub ", L"Установить AeroHub ")) + tag + L"?\n\n" +
                        Tr(L"The hub downloads the installer (SHA-256 is checked), closes, and the installer replaces the program files. "
                           L"Your settings, key binds and panel positions are kept. Start AeroHub again from the Start menu afterwards.",
                           L"Хаб скачает установщик (проверяется SHA-256), закроется, и установщик заменит файлы программы. "
                           L"Настройки, бинды и позиции панелей сохранятся. После этого запустите AeroHub снова из меню «Пуск».");
    wchar_t local[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local))) > 0) {
        const std::filesystem::path installDir = std::filesystem::path(local) / L"Programs" / L"AeroHub";
        std::error_code ec;
        if (!std::filesystem::equivalent(installDir, g_dir, ec))
            text += std::wstring(L"\n\n") + Tr(L"Note: this copy runs from ", L"Внимание: эта копия запущена из ") + g_dir.wstring() +
                    Tr(L"; the update installs to ", L"; обновление устанавливается в ") + installDir.wstring() + L".";
    }
    if (MessageBoxW(g_hwnd, text.c_str(), Tr(L"Update AeroHub", L"Обновление AeroHub"), MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    Log(L"Update: downloading " + tag + L" ...");
    StartInstallerDownload();
}

void OnUpdateMessage(WPARAM stage, LPARAM ok) {
    std::wstring tag, message;
    UpState st;
    {
        std::lock_guard<std::mutex> lock(g_upLock);
        st = g_up.state;
        tag = g_up.release.tag;
        message = g_up.message;
    }
    if (stage == 1) {
        if (st == UpState::Available) Log(L"Update available: " + tag + L" (this is v" + aero::kAppVersion + L")");
        else if (st == UpState::UpToDate) Log(L"AeroHub is up to date (v" + std::wstring(aero::kAppVersion) + L")");
        else if (st == UpState::Failed) Log(L"Update check failed: " + message);
    } else if (stage == 2) {
        if (ok) {
            Log(L"Update: " + tag + L" downloaded and verified, starting the installer");
            if (!LaunchInstallerAndQuit()) MessageBoxW(g_hwnd, Tr(L"Could not start the installer.", L"Не удалось запустить установщик."), Tr(L"Update AeroHub", L"Обновление AeroHub"), MB_OK | MB_ICONERROR);
        } else {
            Log(L"Update: download failed: " + message);
            MessageBoxW(g_hwnd, (std::wstring(Tr(L"The update could not be downloaded:\n", L"Не удалось скачать обновление:\n")) + message).c_str(),
                        Tr(L"Update AeroHub", L"Обновление AeroHub"), MB_OK | MB_ICONERROR);
        }
    }
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

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
        s.word = Tr(L"Jar missing", L"Нет jar");
        s.text = Tr(L"aeroclient.jar is missing next to the hub", L"aeroclient.jar не найден рядом с хабом");
        return s;
    }
    if (!g_mc.found) {
        s.level = g_mc.waiting ? Level::Ready : Level::Idle;
        s.word = g_mc.waiting ? Tr(L"Waiting", L"Ожидание") : Tr(L"Not detected", L"Не найден");
        s.text = g_mc.message.empty() ? Tr(L"Press Auto-detect Minecraft", L"Нажмите «Найти Minecraft»") : g_mc.message;
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
        s.text = Tr(L"AeroClient is loaded in the running game", L"AeroClient загружен в запущенной игре");
        return s;
    }
    const bool hasMod = aero::mc::HasMod(g_mc.game.dir);
    if (runPid != 0) {
        if (!compatible) {
            s.level = Level::Ready;
            s.word = std::wstring(Tr(L"Not Fabric ", L"Не Fabric ")) + p.gameVersion;
            s.text = std::wstring(Tr(L"This game is not Fabric ", L"Эта игра не Fabric ")) + p.gameVersion + Tr(L": the mod will not load", L": мод не загрузится");
        } else if (!hasMod) {
            s.level = Level::Ready;
            s.word = Tr(L"No mod", L"Нет мода");
            s.text = Tr(L"Running without the mod: add it, then restart", L"Игра запущена без мода: добавьте его и перезапустите");
        } else if (ProcessAgeSeconds(runPid) < 120.0) {
            s.word = Tr(L"Starting", L"Запуск");
            s.text = Tr(L"The game is starting", L"Игра запускается");
        } else {
            s.level = Level::Ready;
            s.word = Tr(L"Restart needed", L"Нужен перезапуск");
            s.text = Tr(L"The mod is in the mods folder but not loaded: restart", L"Мод в папке mods, но не загружен: перезапустите игру");
        }
        return s;
    }
    if (hasMod) {
        s.word = Tr(L"Mod added", L"Мод добавлен");
        s.text = Tr(L"The mod is in the mods folder: start Minecraft", L"Мод в папке mods: запустите Minecraft");
    } else {
        s.level = Level::Ready;
        s.word = Tr(L"No mod", L"Нет мода");
        s.text = Tr(L"Install the mod and drop it into the mods folder", L"Установите мод и положите его в папку mods");
    }
    return s;
}

// The Slime Rancher tab: no module yet. It says whether the chosen part is installed and that the cheats are not built.
Status EvaluateSlimeRancher() {
    Status s;
    const std::wstring title = aero::sr::Title(g_sr.part);
    if (g_sr.found) {
        s.word = Tr(L"Game found", L"Игра найдена");
        s.text = title + Tr(L" is installed. Cheats are not built yet: they need the game to be measured.", L" установлена. Читов пока нет: их нужно делать на самой игре.");
    } else {
        s.word = Tr(L"Not found", L"Не найдена");
        s.text = title + Tr(L" was not found in your Steam libraries.", L" не найдена в библиотеках Steam.");
    }
    return s;
}

Status Evaluate(const aero::GameProfile& p) {
    if (p.kind == aero::GameKind::FabricMod) return EvaluateMinecraft(p);
    if (p.kind == aero::GameKind::SlimeRancher) return EvaluateSlimeRancher();
    Status s;
    const auto dll = g_dir / p.moduleDll;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(dll, ec)) {
        s.level = Level::Bad;
        s.text = std::wstring(Tr(L"Module DLL missing: ", L"Не найден файл модуля: ")) + dll.wstring();
        return s;
    }
    const auto pid = aero::FindProcessId(p.processName);
    if (!pid) {
        s.text = Tr(L"The game is not running.", L"Игра не запущена.");
        return s;
    }
    s.pid = *pid;
    aero::UniqueHandle proc(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, *pid));
    if (!proc) {
        s.level = Level::Bad;
        s.text = Tr(L"Cannot open the game process. Run the hub as the same user as the game.", L"Не удаётся открыть процесс игры. Запустите хаб от того же пользователя, что и игру.");
        return s;
    }
    const auto base = aero::FindModuleBase(*pid, p.processName);
    const auto id = base ? aero::ReadImageIdentity(proc.get(), *base) : std::nullopt;
    if (!id) {
        s.level = Level::Bad;
        s.text = Tr(L"Cannot read the game's PE header.", L"Не удаётся прочитать PE-заголовок игры.");
        return s;
    }
    if (id->timeDateStamp != p.timeDateStamp || id->sizeOfImage != p.sizeOfImage) {
        s.level = Level::Bad;
        s.text = std::wstring(Tr(L"Unsupported game build: stamp ", L"Неподдерживаемая версия игры: штамп ")) + Hex(id->timeDateStamp) + Tr(L", size ", L", размер ") +
                 Hex(id->sizeOfImage) + Tr(L" (profile expects ", L" (профиль ожидает ") + Hex(p.timeDateStamp) + L" / " + Hex(p.sizeOfImage) + L").";
        return s;
    }
    const std::wstring build = std::wstring(Tr(L"Build matches the profile (", L"Версия совпадает с профилем (")) + Hex(p.timeDateStamp) + L" / " + Hex(p.sizeOfImage) + L").";
    if (aero::FindModuleBase(*pid, dll.filename().wstring())) {
        s.level = Level::Active;
        s.text = build + Tr(L" Module loaded.", L" Модуль загружен.");
        return s;
    }
    s.level = Level::Ready;
    s.text = build + Tr(L" Module not loaded yet.", L" Модуль ещё не загружен.");
    s.canInject = true;
    return s;
}

int SelectedProfile() { return g_sel >= 0 && g_sel < static_cast<int>(g_profiles.size()) ? g_sel : -1; }

void Redraw() { InvalidateRect(g_hwnd, nullptr, FALSE); }

void FillDemoData() {
    g_infoRows = {{L"Game state", L"in match"}, {L"Local team", L"1"}, {L"Resource A (team +0x58)", L"unlimited"},
                  {L"Resource B (team +0x50)", L"unlimited"}, {L"Own weapons", L"5"}};
    g_link = {Tr(L"Live", L"Онлайн"), Level::Active};
}

// Keeps g_client attached to the selected game's running process and mirrors its features / telemetry.
void UpdateChannel() {
    if (g_demo) {
        FillDemoData();
        return;
    }
    const int sel = SelectedProfile();
    if (sel >= 0 && g_profiles[sel].kind == aero::GameKind::SlimeRancher) {
        g_client.Close();
        g_clientPid = 0;
        g_infoRows.clear();
        g_link = {Tr(L"Offline", L"Не в сети"), Level::Idle};
        return;
    }
    if (sel >= 0 && g_profiles[sel].kind == aero::GameKind::FabricMod) {
        g_client.Close();
        g_clientPid = 0;
        g_infoRows.clear();
        g_link = {Tr(L"Offline", L"Не в сети"), Level::Idle};
        if (g_mc.found) {
            const aero::mc::Live live = aero::mc::ReadLive(g_mc.game.dir);
            if (live.fresh) {
                g_link = {Tr(L"Live", L"Онлайн"), Level::Active};
                for (const auto& [k, v] : live.rows) g_infoRows.push_back({k, v});
            }
        }
        return;
    }
    const bool active = sel >= 0 && g_status[sel].level == Level::Active;
    g_link = {Tr(L"Offline", L"Не в сети"), Level::Idle};

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
            g_link = {Tr(L"No channel", L"Нет канала"), Level::Bad};
        } else {
            const LONG beat = g_client.View()->heartbeat;
            g_staleTicks = (beat == g_lastBeat) ? g_staleTicks + 1 : 0;
            g_lastBeat = beat;
            g_link = g_staleTicks >= kStaleTicksBeforeWarning ? Link{Tr(L"No frames", L"Нет кадров"), Level::Ready} : Link{Tr(L"Live", L"Онлайн"), Level::Active};
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
    g_mc.message = Tr(L"Not found: start Minecraft, the hub will find it", L"Не найден: запустите Minecraft, хаб найдёт его");
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
    if (g_demo) {  // fake data; the actions are disabled in demo mode
        g_gfx.state = g_demoVulkan ? aero::diag::GraphicsState::Vulkan : aero::diag::GraphicsState::OpenGl;
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
        std::wstring(vulkan ? Tr(L"Forts is set to Vulkan, and the AeroHub module only works with OpenGL.\n\nSet EnableVulkan = false in\n",
                                 L"Forts настроен на Vulkan, а модуль AeroHub работает только с OpenGL.\n\nЗадайте EnableVulkan = false в файле\n")
                            : Tr(L"The Forts settings file has no EnableVulkan entry, so the game picks the graphics API itself. The AeroHub module only works with OpenGL.\n\nAdd EnableVulkan = false to\n",
                                 L"В файле настроек Forts нет записи EnableVulkan, поэтому игра сама выбирает графический API. Модуль AeroHub работает только с OpenGL.\n\nДобавьте EnableVulkan = false в файл\n")) +
        g_gfx.file.wstring() +
        Tr(L"\n\nA backup copy (settings_hardware.lua.aerohub-bak) is kept next to it. Forts must be closed; the change applies the next time the game starts.",
           L"\n\nРезервная копия (settings_hardware.lua.aerohub-bak) останется рядом. Forts должен быть закрыт; изменение применится при следующем запуске игры.");
    if (MessageBoxW(g_hwnd, text.c_str(), Tr(L"Switch to OpenGL", L"Переключить на OpenGL"), MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    const std::wstring err = aero::diag::SwitchToOpenGl(g_gfx.file);
    if (!err.empty()) {
        Log(L"Switch to OpenGL failed: " + err);
        MessageBoxW(g_hwnd, err.c_str(), Tr(L"Switch to OpenGL", L"Переключить на OpenGL"), MB_OK | MB_ICONWARNING);
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
        Tr(L"AeroHub will pack the Forts game log, the newest desync reports, the AeroHub module log and a short summary into one zip file on your Desktop.\n\n"
           L"The files contain your Windows user name (in file paths) and your Steam name and ID. Nothing is uploaded: you send the zip yourself.\n\nCreate the zip?",
           L"AeroHub упакует лог игры Forts, последние отчёты о рассинхронизации, лог модуля AeroHub и краткую сводку в один zip-файл на рабочем столе.\n\n"
           L"В файлах есть имя пользователя Windows (в путях) и ваше имя и ID в Steam. Ничего не отправляется: файл вы пересылаете сами.\n\nСоздать zip?"),
        Tr(L"Collect logs", L"Сбор логов"), MB_YESNO | MB_ICONQUESTION);
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
        MessageBoxW(g_hwnd, (std::wstring(Tr(L"Could not create the zip:\n", L"Не удалось создать zip:\n")) + r.error).c_str(), Tr(L"Collect logs", L"Сбор логов"), MB_OK | MB_ICONERROR);
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
    if (sel < 0) return {Tr(L"No game", L"Нет игры"), false};
    const Status& s = g_status[sel];
    if (g_profiles[sel].kind == aero::GameKind::FabricMod) return {Tr(L"Auto-detect Minecraft", L"Найти Minecraft"), true};
    if (g_profiles[sel].kind == aero::GameKind::SlimeRancher) return {Tr(L"Find game", L"Найти игру"), true};
    if (s.canInject) return {Tr(L"Inject module", L"Внедрить модуль"), true};
    if (s.level == Level::Idle && s.pid == 0 && !g_profiles[sel].steamAppId.empty()) return {Tr(L"Launch game", L"Запустить игру"), true};
    if (s.level == Level::Active) return {Tr(L"Module active", L"Модуль активен"), false};
    return {Tr(L"Unavailable", L"Недоступно"), false};
}

void DetectSlimeRancher() {
    const auto found = aero::sr::Find(g_sr.part);
    g_sr.found = found.has_value();
    g_sr.install = found.value_or(aero::sr::Install{});
    g_sr.ticksSinceSearch = 0;
    Log(std::wstring(L"Slime Rancher: ") + aero::sr::Title(g_sr.part) + (found ? L" found in " + found->dir.wstring() : std::wstring(L" not found in the Steam libraries")));
}

// Not found yet: look again every 15 seconds, so a game installed while the hub is open shows up.
void PollSlimeRancher() {
    if (g_sr.found || ++g_sr.ticksSinceSearch < 15) return;
    DetectSlimeRancher();
}

void DoSecondary() {
    const int sel = SelectedProfile();
    if (sel < 0) return;
    if (g_profiles[sel].kind == aero::GameKind::SlimeRancher) {
        if (g_sr.found && !g_sr.install.appId.empty()) {
            ShellExecuteW(g_hwnd, L"open", (L"steam://rungameid/" + g_sr.install.appId).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            Log(std::wstring(L"Slime Rancher: launching ") + aero::sr::Title(g_sr.part) + L" through Steam");
        }
        return;
    }
    if (g_profiles[sel].kind != aero::GameKind::FabricMod) return;
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
    if (g_profiles[sel].kind == aero::GameKind::SlimeRancher) {
        DetectSlimeRancher();
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
            if (g_profiles[sel].kind == aero::GameKind::SlimeRancher) {
                g_tailPath.clear();  // no module, no log
            } else if (g_profiles[sel].kind == aero::GameKind::FabricMod) {
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

std::unique_ptr<Gdiplus::Font> g_fSmallBold, g_fMonoSmall, g_fHero, g_fTile;

// ---------------------------------------------------------------- theme
// Palette taken from the app icon: near-black navy surfaces, blue-to-violet accent, cyan highlight.
namespace th {
// Graphite ramp for every surface, border and text; one amber accent (the aim dot of the icon) for the primary action,
// the selection and live states. Colour otherwise lives only in the game art.
constexpr COLORREF bg = RGB(14, 15, 17), side = RGB(18, 19, 22), card = RGB(23, 24, 27), card2 = RGB(31, 33, 37),
                   line = RGB(40, 42, 47), lineHi = RGB(56, 59, 66), text = RGB(236, 237, 239), dim = RGB(155, 158, 166),
                   faint = RGB(106, 109, 118), accent = RGB(245, 184, 61), accentHi = RGB(250, 199, 94), onAccent = RGB(24, 20, 12),
                   ok = RGB(94, 196, 140), warn = RGB(232, 176, 72), bad = RGB(228, 112, 104), logBg = RGB(17, 18, 21);
constexpr COLORREF blue = accent, violet = accent, cyan = accent;  // former hues, now the single accent
}

COLORREF Mix(COLORREF a, COLORREF b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto ch = [&](int x, int y) { return static_cast<BYTE>(x + (y - x) * t + 0.5f); };
    return RGB(ch(GetRValue(a), GetRValue(b)), ch(GetGValue(a), GetGValue(b)), ch(GetBValue(a), GetBValue(b)));
}
BYTE A(float a) { return static_cast<BYTE>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f); }

COLORREF StatusColor(Level l) {
    switch (l) {
        case Level::Active: return th::ok;
        case Level::Ready: return th::accent;
        case Level::Bad: return th::bad;
        default: return th::faint;
    }
}

// Two colors per game for its tile and banner.
void GameHue(const aero::GameProfile& p, COLORREF& c1, COLORREF& c2) {
    if (p.kind == aero::GameKind::FabricMod) { c1 = RGB(34, 197, 94); c2 = RGB(13, 148, 136); return; }
    if (p.kind == aero::GameKind::SlimeRancher) { c1 = RGB(236, 72, 153); c2 = RGB(168, 85, 247); return; }
    if (_wcsicmp(p.processName.c_str(), L"Forts.exe") == 0) { c1 = RGB(249, 115, 22); c2 = RGB(220, 38, 38); return; }
    c1 = th::blue;
    c2 = th::violet;
}

// ---------------------------------------------------------------- animation
// Every animated value is a key -> eased float. A frame advances them all by the real elapsed time; while any value has
// not settled the window keeps a ~60 fps timer, otherwise the timer stops and the hub idles at zero cost.
constexpr UINT_PTR kAnimTimer = 2;
double g_now = 0, g_lastFrame = 0, g_appStart = 0, g_pageStart = -10;
float g_dt = 1.0f / 60.0f;
bool g_animating = false, g_pulsing = false;
UINT g_animInterval = 0;
bool g_logOpen = true;
std::map<std::uint64_t, float> g_springs;

double NowSec() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / static_cast<double>(f.QuadPart);
}

std::uint64_t Key(int kind, int a = 0, int b = 0) {
    return (static_cast<std::uint64_t>(kind) << 48) | (static_cast<std::uint64_t>(a & 0xFFFFFF) << 24) | static_cast<std::uint64_t>(b & 0xFFFFFF);
}

// Moves the value toward `target` (exponential ease; `rate` is how fast, per second). A new key starts at `start`.
float Ease(std::uint64_t key, float target, float rate = 14.0f, float start = -1.0f) {
    auto it = g_springs.find(key);
    if (it == g_springs.end()) it = g_springs.emplace(key, start < 0 ? target : start).first;
    float& v = it->second;
    v += (target - v) * (1.0f - std::exp(-rate * g_dt));
    if (std::fabs(target - v) < 0.0015f) v = target;
    else g_animating = true;
    return v;
}

float Smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
float EaseOutCubic(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

// 0..1 progress of something that started at `start` and lasts `len` seconds (keeps frames coming until done).
float Progress(double start, double len) {
    const float t = static_cast<float>((g_now - start) / len);
    if (t < 1.0f) g_animating = true;
    return std::clamp(t, 0.0f, 1.0f);
}

bool IsHover(Act a, int idx) { return g_hoverAct == a && g_hoverIdx == idx; }
bool IsPressed(Act a, int idx) { return g_pressAct == a && g_pressIdx == idx && IsHover(a, idx); }
float HoverT(Act a, int idx) { return Ease(Key(1, static_cast<int>(a), idx + 1), IsHover(a, idx) ? 1.0f : 0.0f, 16.0f); }
float PressT(Act a, int idx) { return Ease(Key(2, static_cast<int>(a), idx + 1), IsPressed(a, idx) ? 1.0f : 0.0f, 28.0f); }

void AddHit(Act act, int idx, const RectF& r, bool enabled = true) { g_hits.push_back({r, act, idx, enabled}); }

// ---------------------------------------------------------------- primitives
void Text(Gdiplus::Graphics& g, const std::wstring& s, const Gdiplus::Font& f, const RectF& r, COLORREF c, float alpha = 1.0f,
          Gdiplus::StringAlignment h = Gdiplus::StringAlignmentNear, Gdiplus::StringAlignment v = Gdiplus::StringAlignmentCenter,
          bool wrap = false) {
    if (alpha <= 0.01f) return;
    Gdiplus::StringFormat fmt(wrap ? 0 : Gdiplus::StringFormatFlagsNoWrap);
    fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    fmt.SetAlignment(h);
    fmt.SetLineAlignment(v);
    Gdiplus::SolidBrush b(Col(c, A(alpha)));
    g.DrawString(s.c_str(), -1, &f, r, &fmt, &b);
}

RectF Inflate(const RectF& r, float d) { return RectF(r.X - d, r.Y - d, r.Width + 2 * d, r.Height + 2 * d); }

void Fill(Gdiplus::Graphics& g, const RectF& r, float rad, COLORREF c, float alpha = 1.0f) {
    if (alpha > 0.003f) FillRR(g, r, rad, c, A(alpha));
}

void Stroke(Gdiplus::Graphics& g, const RectF& r, float rad, COLORREF c, float alpha = 1.0f, float width = 1.0f) {
    if (alpha > 0.003f) StrokeRR(g, r, rad, c, width, A(alpha));
}

void Grad(Gdiplus::Graphics& g, const RectF& r, float rad, COLORREF c1, COLORREF c2, float alpha = 1.0f, float angle = 20.0f) {
    if (alpha <= 0.003f || r.Width <= 0 || r.Height <= 0) return;
    Gdiplus::GraphicsPath p;
    RoundPath(p, r, rad);
    Gdiplus::LinearGradientBrush b(Inflate(r, 1), Col(c1, A(alpha)), Col(c2, A(alpha)), angle, TRUE);
    g.FillPath(&b, &p);
}

// Soft glow / shadow: rings of decreasing opacity around a rounded rectangle. Rendered once per size and color into a
// bitmap, then blitted (with the requested opacity), because dozens of anti-aliased path strokes per frame are slow.
void Glow(Gdiplus::Graphics& g, const RectF& r, float rad, COLORREF c, float alpha, float spread) {
    if (alpha <= 0.01f) return;
    static std::map<std::tuple<int, int, int, int, COLORREF>, std::unique_ptr<Gdiplus::Bitmap>> cache;
    const int w = static_cast<int>(r.Width + 0.5f), h = static_cast<int>(r.Height + 0.5f), sp = static_cast<int>(spread + 0.5f);
    if (w <= 0 || h <= 0) return;
    const auto key = std::make_tuple(w, h, static_cast<int>(rad + 0.5f), sp, c);
    auto it = cache.find(key);
    if (it == cache.end()) {
        if (cache.size() > 64) cache.clear();
        const int pad = sp + 2;
        auto bmp = std::make_unique<Gdiplus::Bitmap>(w + pad * 2, h + pad * 2, PixelFormat32bppPARGB);
        Gdiplus::Graphics bg(bmp.get());
        bg.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const RectF inner(static_cast<float>(pad), static_cast<float>(pad), static_cast<float>(w), static_cast<float>(h));
        const int steps = 6;
        for (int i = 1; i <= steps; ++i) {
            const float t = static_cast<float>(i) / steps;
            StrokeRR(bg, Inflate(inner, spread * t), rad + spread * t, c, spread / steps + 1.0f, A((1 - t) * 0.35f));
        }
        it = cache.emplace(key, std::move(bmp)).first;
    }
    Gdiplus::Bitmap* bmp = it->second.get();
    const float pad = static_cast<float>(sp + 2);
    const RectF dst(r.X - pad, r.Y - pad, static_cast<float>(bmp->GetWidth()), static_cast<float>(bmp->GetHeight()));
    if (alpha >= 0.995f) {
        g.DrawImage(bmp, dst.X, dst.Y, dst.Width, dst.Height);
        return;
    }
    Gdiplus::ColorMatrix cm = {{{1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, std::min(alpha, 1.0f), 0}, {0, 0, 0, 0, 1}}};
    Gdiplus::ImageAttributes ia;
    ia.SetColorMatrix(&cm);
    g.DrawImage(bmp, dst, 0, 0, dst.Width, dst.Height, Gdiplus::UnitPixel, &ia);
}

void Radial(Gdiplus::Graphics& g, float cx, float cy, float rx, float ry, COLORREF c, BYTE alpha) {
    Gdiplus::GraphicsPath p;
    p.AddEllipse(cx - rx, cy - ry, rx * 2, ry * 2);
    Gdiplus::PathGradientBrush b(&p);
    b.SetCenterColor(Col(c, alpha));
    Gdiplus::Color edge = Col(c, 0);
    int n = 1;
    b.SetSurroundColors(&edge, &n);
    g.FillPath(&b, &p);
}

// App icon (resource 1) at a pixel size.
void DrawLogo(Gdiplus::Graphics& g, float x, float y, float size) {
    static HICON icon = nullptr;
    static int iconSize = 0;
    const int px = static_cast<int>(size + 0.5f);
    if (px != iconSize) {
        if (icon) DestroyIcon(icon);
        icon = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, px, px, 0));
        iconSize = px;
    }
    if (!icon) return;
    const HDC dc = g.GetHDC();
    DrawIconEx(dc, static_cast<int>(x + 0.5f), static_cast<int>(y + 0.5f), icon, px, px, 0, nullptr, DI_NORMAL);
    g.ReleaseHDC(dc);
}

enum class BtnKind { Primary, Secondary, Ghost };

// Button with hover lift, press sink, gradient and glow for the primary kind.
void Button(Gdiplus::Graphics& g, const RectF& r0, const std::wstring& label, BtnKind kind, bool enabled, Act act, int idx = 0,
            float alpha = 1.0f) {
    AddHit(act, idx, r0, enabled);
    const float h = enabled ? HoverT(act, idx) : 0.0f, p = enabled ? PressT(act, idx) : 0.0f;
    RectF r = r0;
    r.Y += -S(1.5f) * h + S(1.0f) * p;
    const float rad = S(10);
    const float a = alpha * (enabled ? 1.0f : 0.45f);
    if (kind == BtnKind::Primary) {
        Fill(g, r, rad, Mix(th::accent, th::accentHi, h), a);
        Fill(g, r, rad, RGB(0, 0, 0), a * 0.15f * p);
        Text(g, label, *g_fBodyBold, r, th::onAccent, a, Gdiplus::StringAlignmentCenter);
    } else if (kind == BtnKind::Secondary) {
        Fill(g, r, rad, Mix(th::card2, th::lineHi, 0.35f * h), a);
        Stroke(g, r, rad, Mix(th::line, th::lineHi, h), a);
        Fill(g, r, rad, RGB(0, 0, 0), a * 0.15f * p);
        Text(g, label, *g_fBodyBold, r, th::text, a, Gdiplus::StringAlignmentCenter);
    } else {
        Fill(g, r, rad, th::card2, a * h);
        Text(g, label, *g_fBody, r, Mix(th::dim, th::text, h), a, Gdiplus::StringAlignmentCenter);
    }
}

// Switch whose knob slides and whose track fills with the accent gradient.
void Toggle(Gdiplus::Graphics& g, const RectF& r, bool on, Act act, int idx = 0) {
    const float t = Ease(Key(3, static_cast<int>(act), idx), on ? 1.0f : 0.0f, 12.0f);
    const float h = HoverT(act, idx);
    Fill(g, r, r.Height / 2, Mix(th::line, th::lineHi, h * 0.6f));
    if (t > 0.01f) {
        Fill(g, r, r.Height / 2, th::accent, t);
    }
    const float d = r.Height - S(6);
    const float cx = r.X + S(3) + d / 2 + (r.Width - S(6) - d) * EaseOutCubic(t);
    const float grow = S(1.5f) * h;
    FillDot(g, cx, r.Y + r.Height / 2, d / 2 + grow, Mix(th::text, th::onAccent, t * 0.0f));
}

// Segmented control with a thumb that slides to the chosen segment.
void Segmented(Gdiplus::Graphics& g, const RectF& r, const std::vector<std::wstring>& labels, int selected, Act act, int idxBase,
               std::uint64_t key, float alpha = 1.0f) {
    const int n = static_cast<int>(labels.size());
    if (n == 0) return;
    Fill(g, r, S(10), th::bg, alpha);
    Stroke(g, r, S(10), th::line, alpha);
    const float pad = S(3), w = (r.Width - pad * 2) / n;
    const float pos = Ease(key, static_cast<float>(selected), 13.0f);
    const RectF thumb(r.X + pad + w * pos, r.Y + pad, w, r.Height - pad * 2);
    Fill(g, thumb, S(8), th::card2, alpha);
    Stroke(g, thumb, S(8), th::lineHi, alpha);
    for (int i = 0; i < n; ++i) {
        const RectF cell(r.X + pad + w * i, r.Y + pad, w, r.Height - pad * 2);
        const float h = HoverT(act, idxBase + i);
        const float on = std::clamp(1.0f - std::fabs(pos - i), 0.0f, 1.0f);
        Text(g, labels[static_cast<std::size_t>(i)], *g_fSmallBold, cell, Mix(Mix(th::faint, th::dim, h), th::text, on), alpha,
             Gdiplus::StringAlignmentCenter);
        AddHit(act, idxBase + i, cell);
    }
}

// Status chip: dot (pulsing while the module is active) and word.
float Chip(Gdiplus::Graphics& g, float x, float y, const std::wstring& word, Level level, float alpha = 1.0f) {
    const COLORREF c = StatusColor(level);
    const float hgt = S(28), dot = S(4);
    const float w = S(14) * 2 + dot * 2 + S(9) + MeasureW(g, word, *g_fSmallBold);
    const RectF r(x, y, w, hgt);
    Fill(g, r, hgt / 2, c, 0.12f * alpha);
    Stroke(g, r, hgt / 2, c, 0.35f * alpha);
    const float cx = x + S(14) + dot, cy = y + hgt / 2;
    if (level == Level::Active) {
        g_pulsing = true;
        const float ph = static_cast<float>(std::fmod(g_now * 0.9, 1.0));
        FillDot(g, cx, cy, dot + S(7) * ph, c, A(alpha * 0.45f * (1 - ph)));
    }
    FillDot(g, cx, cy, dot, c, A(alpha));
    Text(g, word, *g_fSmallBold, RectF(cx + dot + S(9), y, w, hgt), c, alpha);
    return w;
}

void Card(Gdiplus::Graphics& g, const RectF& r, float alpha = 1.0f) {
    Fill(g, r, S(16), th::card, alpha);
    Stroke(g, r, S(16), th::line, alpha);
}

void CardTitle(Gdiplus::Graphics& g, const RectF& c, const std::wstring& title, COLORREF, float alpha) {
    Text(g, title, *g_fH2, RectF(c.X + S(24), c.Y + S(16), c.Width - S(48), S(30)), th::text, alpha);
}

// Key hint "Insert = open the menu": the key in a keycap, the action after it.
void HintRow(Gdiplus::Graphics& g, const RectF& r, const std::wstring& line, float alpha) {
    const std::size_t eq = line.find(L" = ");
    if (eq == std::wstring::npos) {
        FillDot(g, r.X + S(6), r.Y + r.Height / 2, S(3), th::cyan, A(alpha));
        Text(g, line, *g_fSmall, RectF(r.X + S(20), r.Y, r.Width - S(20), r.Height), th::dim, alpha);
        return;
    }
    const std::wstring key = line.substr(0, eq), what = line.substr(eq + 3);
    const float kw = std::min(r.Width * 0.45f, MeasureW(g, key, *g_fMonoSmall) + S(20));
    const RectF cap(r.X, r.Y + (r.Height - S(26)) / 2, kw, S(26));
    Fill(g, RectF(cap.X, cap.Y + S(2), cap.Width, cap.Height), S(7), RGB(0, 0, 0), alpha * 0.5f);
    Fill(g, cap, S(7), th::card2, alpha);
    Stroke(g, cap, S(7), th::lineHi, alpha);
    Text(g, key, *g_fMonoSmall, cap, th::text, alpha, Gdiplus::StringAlignmentCenter);
    Text(g, what, *g_fSmall, RectF(cap.X + kw + S(12), r.Y, r.Width - kw - S(12), r.Height), th::dim, alpha);
}

// ---------------------------------------------------------------- layout
struct Lay {
    RectF sidebar, content, hero, guide, live, log, logBody;
};

float LogHeightOpen(float H) { return std::clamp(H * 0.30f, S(180), S(290)); }

Lay Compute(int w, int h, float logOpenT) {
    Lay L;
    const float W = static_cast<float>(w), H = static_cast<float>(h);
    const float sb = S(264), m = S(26), gap = S(18);
    L.sidebar = RectF(0, 0, sb, H);
    const float x0 = sb + m, cw = std::max(S(300), W - sb - 2 * m);
    L.content = RectF(x0, 0, cw, H);
    float y = S(44);
    L.hero = RectF(x0, y, cw, S(176));
    y += S(176) + gap;
    const float logH = S(58) + (LogHeightOpen(H) - S(58)) * logOpenT;
    const float cardsH = std::max(S(170), H - y - m - gap - logH);
    const float leftW = cw * 0.58f - gap / 2;
    L.guide = RectF(x0, y, leftW, cardsH);
    L.live = RectF(x0 + leftW + gap, y, cw - leftW - gap, cardsH);
    y += cardsH + gap;
    L.log = RectF(x0, y, cw, std::max(S(58), H - y - m));
    L.logBody = RectF(L.log.X + S(14), L.log.Y + S(58), L.log.Width - S(28), L.log.Height - S(70));
    return L;
}

float g_logOpenT = 1.0f;  // eased 0..1, read by PositionLogs between frames

// ---------------------------------------------------------------- licence
// The hub works only with a key that the site confirms for this machine. A confirmed key stays valid offline for
// kLicenseGraceSec (the site may be unreachable for a while); the check is repeated every kLicenseRecheckSec.
enum class Lic { Checking, Ok, Need };
Lic g_lic = Lic::Checking;
std::wstring g_licKey, g_licMsg, g_licUser;
long long g_licExpiresMs = 0;  // 0 = forever
std::wstring g_licGames;  // comma-separated game ids from the site, e.g. "forts,minecraft"

// Show only the games the account owns (the site adds the gifts). The demo shows everything.
void ApplyLicensedGames() {
    std::vector<std::wstring> allowed;
    for (std::size_t start = 0; start <= g_licGames.size();) {
        std::size_t end = g_licGames.find(L',', start);
        if (end == std::wstring::npos) end = g_licGames.size();
        if (end > start) allowed.push_back(g_licGames.substr(start, end - start));
        start = end + 1;
    }
    g_profiles.clear();
    for (const auto& p : g_allProfiles) {
        std::wstring id = p.name;
        for (auto& c : id) c = static_cast<wchar_t>(towlower(c));
        if (g_demo || std::find(allowed.begin(), allowed.end(), id) != allowed.end()) g_profiles.push_back(p);
    }
    g_sel = 0;
    g_autoTriedPid.clear();
}
bool g_licBusy = false;
double g_licLastCheck = 0;
aero::license::Result g_licResult;
std::mutex g_licLock;
constexpr long long kLicenseGraceSec = 3 * 86400;
constexpr double kLicenseRecheckSec = 6 * 3600;

long long UnixNow() { return static_cast<long long>(std::time(nullptr)); }

void StartLicenseCheck(const std::wstring& key) {
    if (g_licBusy) return;
    g_licBusy = true;
    const HWND hwnd = g_hwnd;
    std::thread([hwnd, key] {
        aero::license::Result r;
        const std::string id = aero::license::MachineId();
        if (id.empty()) r.error = L"cannot read the machine id";
        else r = aero::license::Check(key, id);
        {
            std::lock_guard<std::mutex> lock(g_licLock);
            g_licResult = r;
        }
        PostMessageW(hwnd, kMsgLicense, 0, 0);
    }).detach();
}

std::wstring LicenseReasonText(const std::string& reason) {
    if (reason == "not redeemed") return Tr(L"This key is not activated yet: activate it on the site first.", L"Ключ ещё не активирован: активируйте его на сайте.");
    if (reason == "expired") return Tr(L"This key has expired.", L"Срок действия ключа закончился.");
    if (reason == "revoked") return Tr(L"This key was revoked.", L"Ключ отозван.");
    if (reason == "other machine") return Tr(L"This key is bound to another computer. Reset it on the site.", L"Ключ привязан к другому компьютеру. Сбросьте привязку на сайте.");
    if (reason == "rate") return Tr(L"Too many attempts, try again in a few minutes.", L"Слишком много попыток, попробуйте через несколько минут.");
    return Tr(L"Key not found.", L"Ключ не найден.");
}

void PositionLogs();

void OnLicenseMessage() {
    g_licBusy = false;
    g_licLastCheck = NowSec();
    aero::license::Result r;
    {
        std::lock_guard<std::mutex> lock(g_licLock);
        r = g_licResult;
    }
    const Lic before = g_lic;
    if (r.reached && r.ok) {
        long long until = UnixNow() + kLicenseGraceSec;
        if (r.expiresMs > 0) until = std::min(until, r.expiresMs / 1000);
        WritePrivateProfileStringW(L"License", L"Key", g_licKey.c_str(), g_settingsFile.c_str());
        WritePrivateProfileStringW(L"License", L"OkUntil", std::to_wstring(until).c_str(), g_settingsFile.c_str());
        g_licUser.assign(r.username.begin(), r.username.end());
        std::wstring games;
        for (const auto& id : r.games) games += (games.empty() ? L"" : L",") + std::wstring(id.begin(), id.end());
        WritePrivateProfileStringW(L"License", L"Games", games.c_str(), g_settingsFile.c_str());
        if (games != g_licGames || before != Lic::Ok) {
            g_licGames = games;
            ApplyLicensedGames();
            Refresh();
        }
        g_licExpiresMs = r.expiresMs;
        WritePrivateProfileStringW(L"License", L"User", g_licUser.c_str(), g_settingsFile.c_str());
        WritePrivateProfileStringW(L"License", L"Expires", std::to_wstring(g_licExpiresMs).c_str(), g_settingsFile.c_str());
        g_lic = Lic::Ok;
        g_licMsg.clear();
        if (before != Lic::Ok) Log(L"licence: key confirmed by the site");
    } else if (r.reached) {
        WritePrivateProfileStringW(L"License", L"OkUntil", L"0", g_settingsFile.c_str());
        g_lic = Lic::Need;
        g_licMsg = LicenseReasonText(r.reason);
        Log(L"licence: refused (" + std::wstring(r.reason.begin(), r.reason.end()) + L")");
    } else {
        wchar_t buf[32]{};
        GetPrivateProfileStringW(L"License", L"OkUntil", L"0", buf, static_cast<DWORD>(std::size(buf)), g_settingsFile.c_str());
        const long long until = _wtoi64(buf);
        if (until > UnixNow() && !g_licKey.empty()) {
            g_lic = Lic::Ok;  // offline grace
            if (before != Lic::Ok) Log(L"licence: site unreachable (" + r.error + L"), using the last confirmation");
        } else {
            g_lic = Lic::Need;
            g_licMsg = Tr(L"The site cannot be reached. Check the internet connection and try again.", L"Не удаётся связаться с сайтом. Проверьте интернет и попробуйте ещё раз.");
            Log(L"licence: site unreachable (" + r.error + L")");
        }
    }
    if (g_lic != before) {
        g_pageStart = NowSec();
        PositionLogs();
    }
    Redraw();
}

// Sign out on this computer: the site forgets this machine for the key, and the hub forgets the key.
void SignOut() {
    if (MessageBoxW(g_hwnd, Tr(L"Remove the key from this computer? The key stays yours: you can use it on another computer.",
                               L"Убрать ключ с этого компьютера? Ключ останется вашим — его можно будет ввести на другом компьютере."),
                    L"AeroHub", MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;
    const std::wstring key = g_licKey;
    std::thread([key] {
        const std::string id = aero::license::MachineId();
        if (!id.empty()) aero::license::Release(key, id);
    }).detach();
    for (const wchar_t* k : {L"Key", L"OkUntil", L"User", L"Expires", L"Games"}) WritePrivateProfileStringW(L"License", k, nullptr, g_settingsFile.c_str());
    g_licGames.clear();
    ApplyLicensedGames();
    g_licKey.clear();
    g_licUser.clear();
    g_licExpiresMs = 0;
    g_licMsg.clear();
    SetWindowTextW(g_editKey, L"");
    g_lic = Lic::Need;
    g_pageStart = NowSec();
    Log(L"licence: signed out on this computer");
    PositionLogs();
}

std::wstring LicenseLeftText() {
    if (g_licExpiresMs <= 0) return Tr(L"Forever", L"Навсегда");
    const long long left = g_licExpiresMs / 1000 - UnixNow();
    if (left <= 0) return Tr(L"Expired", L"Истёк");
    const long long days = left / 86400, hours = (left % 86400) / 3600;
    if (days >= 1) return std::to_wstring(days) + Tr(L" d left", L" дн. осталось");
    return std::to_wstring(std::max(1LL, hours)) + Tr(L" h left", L" ч осталось");
}

void ActivateFromEdit() {
    wchar_t buf[96]{};
    GetWindowTextW(g_editKey, buf, static_cast<int>(std::size(buf)));
    const std::wstring key = aero::license::NormalizeKey(buf);
    if (key.empty()) {
        g_licMsg = Tr(L"A key looks like AERO-XXXX-XXXX-XXXX-XXXX.", L"Ключ выглядит так: AERO-XXXX-XXXX-XXXX-XXXX.");
        return;
    }
    g_licKey = key;
    g_licMsg.clear();
    StartLicenseCheck(key);
}

void PositionLogs() {
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    if (g_lic != Lic::Ok) {
        ShowWindow(g_editModule, SW_HIDE);
        ShowWindow(g_editHub, SW_HIDE);
        const float cw = S(480), ch = S(330);
        const float cx = (rc.right - cw) / 2, cy = (rc.bottom - ch) / 2;
        MoveWindow(g_editKey, static_cast<int>(cx + S(40)), static_cast<int>(cy + S(150)), static_cast<int>(cw - S(80)), static_cast<int>(S(24)), TRUE);
        ShowWindow(g_editKey, g_lic == Lic::Need ? SW_SHOW : SW_HIDE);
        return;
    }
    ShowWindow(g_editKey, SW_HIDE);
    const Lay L = Compute(rc.right, rc.bottom, g_logOpenT);
    const bool show = L.logBody.Height > S(30);
    const int x = static_cast<int>(L.logBody.X + S(6)), y = static_cast<int>(L.logBody.Y),
              w = static_cast<int>(L.logBody.Width - S(12)), h = static_cast<int>(std::max(0.0f, L.logBody.Height));
    MoveWindow(g_editModule, x, y, w, h, TRUE);
    MoveWindow(g_editHub, x, y, w, h, TRUE);
    ShowWindow(g_editModule, show && g_tab == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_editHub, show && g_tab == 1 ? SW_SHOW : SW_HIDE);
}

void MakeFonts() {
    auto family = [](std::initializer_list<const wchar_t*> names) {
        for (const wchar_t* n : names) {
            Gdiplus::FontFamily f(n);
            if (f.IsAvailable()) return std::wstring(n);
        }
        return std::wstring(L"Segoe UI");
    };
    const std::wstring ui = L"Segoe UI";
    const std::wstring semi = family({L"Segoe UI Semibold", L"Segoe UI"});
    const std::wstring mono = family({L"Cascadia Mono", L"Consolas"});
    auto mk = [](const std::wstring& fam, float px, int style) {
        return std::make_unique<Gdiplus::Font>(fam.c_str(), S(px), style, Gdiplus::UnitPixel);
    };
    g_fHero = mk(semi, 32, Gdiplus::FontStyleRegular);
    g_fTitle = mk(semi, 24, Gdiplus::FontStyleRegular);
    g_fH2 = mk(semi, 16, Gdiplus::FontStyleRegular);
    g_fBody = mk(ui, 14, Gdiplus::FontStyleRegular);
    g_fBodyBold = mk(semi, 14, Gdiplus::FontStyleRegular);
    g_fSmall = mk(ui, 12.5f, Gdiplus::FontStyleRegular);
    g_fSmallBold = mk(semi, 12.5f, Gdiplus::FontStyleRegular);
    g_fMono = mk(mono, 13, Gdiplus::FontStyleRegular);
    g_fMonoSmall = mk(mono, 12, Gdiplus::FontStyleRegular);
    g_fBrand = mk(semi, 19, Gdiplus::FontStyleRegular);
    g_fTile = mk(semi, 18, Gdiplus::FontStyleRegular);
    if (g_editFont) DeleteObject(g_editFont);
    g_editFont = CreateFontW(-static_cast<int>(S(13)), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH, mono.c_str());
    if (g_editHub) SendMessageW(g_editHub, WM_SETFONT, reinterpret_cast<WPARAM>(g_editFont), TRUE);
    if (g_editModule) SendMessageW(g_editModule, WM_SETFONT, reinterpret_cast<WPARAM>(g_editFont), TRUE);
    if (g_editKey) SendMessageW(g_editKey, WM_SETFONT, reinterpret_cast<WPARAM>(g_editFont), TRUE);
}

// ---------------------------------------------------------------- background (cached per window size)
std::unique_ptr<Gdiplus::Bitmap> g_bgCache;
int g_bgW = 0, g_bgH = 0, g_bgDpi = 0;

Gdiplus::Bitmap* Background(int w, int h) {
    if (!g_bgCache || g_bgW != w || g_bgH != h || g_bgDpi != g_dpi) {
        g_bgCache = std::make_unique<Gdiplus::Bitmap>(w, h, PixelFormat32bppPARGB);
        g_bgW = w;
        g_bgH = h;
        g_bgDpi = g_dpi;
        Gdiplus::Graphics g(g_bgCache.get());
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.Clear(Col(th::bg));
        const float H = static_cast<float>(h), sb = S(264);
        Gdiplus::SolidBrush side(Col(th::side));
        g.FillRectangle(&side, RectF(0, 0, sb, H));
        Gdiplus::SolidBrush edge(Col(th::line));
        g.FillRectangle(&edge, RectF(sb - 1, 0, 1, H));
    }
    return g_bgCache.get();
}

void DrawBackgroundRegion(Gdiplus::Graphics& g, const RectF& r, float alpha) {
    if (!g_bgCache || alpha <= 0.01f) return;
    Gdiplus::ColorMatrix cm = {{{1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, alpha, 0}, {0, 0, 0, 0, 1}}};
    Gdiplus::ImageAttributes ia;
    ia.SetColorMatrix(&cm);
    g.DrawImage(g_bgCache.get(), r, r.X, r.Y, r.Width, r.Height, Gdiplus::UnitPixel, &ia);
}

// Game art from <hub>/art/<name>.jpg (generated banners); nullptr when the file is missing.
Gdiplus::Bitmap* GameArt(const aero::GameProfile& p, bool icon = false) {
    static std::map<std::wstring, std::unique_ptr<Gdiplus::Bitmap>> cache;
    std::wstring name;
    if (p.kind == aero::GameKind::FabricMod) name = L"minecraft";
    else if (_wcsicmp(p.processName.c_str(), L"Forts.exe") == 0) name = L"forts";
    else return nullptr;
    if (icon) name += L"_icon";
    auto it = cache.find(name);
    if (it == cache.end()) {
        // Copied into memory so the file is not kept open (a running hub must not block an update of the art).
        std::unique_ptr<Gdiplus::Bitmap> bmp;
        {
            Gdiplus::Bitmap file((g_dir / L"art" / (name + L".jpg")).c_str());
            if (file.GetLastStatus() == Gdiplus::Ok && file.GetWidth() > 0) {
                bmp = std::make_unique<Gdiplus::Bitmap>(file.GetWidth(), file.GetHeight(), PixelFormat32bppPARGB);
                Gdiplus::Graphics copy(bmp.get());
                copy.DrawImage(&file, 0, 0, file.GetWidth(), file.GetHeight());
            }
        }
        it = cache.emplace(name, std::move(bmp)).first;
    }
    return it->second.get();
}

// Draws `img` to cover `r` (cropped around `focusX` of the image width), clipped to a rounded rectangle. The scaled crop is
// cached per size, so a still frame costs one plain blit instead of a high-quality resample of the whole picture.
void DrawCover(Gdiplus::Graphics& g, Gdiplus::Bitmap* img, const RectF& r, float rad, float alpha, float focusX = 0.5f) {
    if (!img || alpha <= 0.01f || r.Width < 1 || r.Height < 1) return;
    struct Entry {
        std::unique_ptr<Gdiplus::Bitmap> bmp;
        ULONGLONG used = 0;
    };
    static std::map<std::tuple<const void*, int, int, int, int>, Entry> cache;
    const int w = static_cast<int>(r.Width + 0.5f), h = static_cast<int>(r.Height + 0.5f);
    const auto key = std::make_tuple(static_cast<const void*>(img), w, h, static_cast<int>(rad * 10), static_cast<int>(focusX * 100));
    auto it = cache.find(key);
    if (it == cache.end()) {
        if (cache.size() > 24) cache.clear();  // sizes change while the window is resized
        auto bmp = std::make_unique<Gdiplus::Bitmap>(w, h, PixelFormat32bppPARGB);
        Gdiplus::Graphics bg(bmp.get());
        bg.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        bg.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        bg.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        const float iw = static_cast<float>(img->GetWidth()), ih = static_cast<float>(img->GetHeight());
        const float scale = std::max(w / iw, h / ih);
        const float sw = w / scale, sh = h / scale;
        const float sx = std::clamp(iw * focusX - sw / 2, 0.0f, iw - sw), sy = (ih - sh) / 2;
        if (rad > 0) {
            Gdiplus::GraphicsPath clip;
            RoundPath(clip, RectF(0, 0, static_cast<float>(w), static_cast<float>(h)), rad);
            bg.SetClip(&clip);
        }
        bg.DrawImage(img, RectF(0, 0, static_cast<float>(w), static_cast<float>(h)), sx, sy, sw, sh, Gdiplus::UnitPixel);
        it = cache.emplace(key, Entry{std::move(bmp), 0}).first;
    }
    Gdiplus::Bitmap* scaled = it->second.bmp.get();
    if (alpha >= 0.995f) {
        g.DrawImage(scaled, r.X, r.Y, static_cast<float>(w), static_cast<float>(h));
        return;
    }
    Gdiplus::ColorMatrix cm = {{{1, 0, 0, 0, 0}, {0, 1, 0, 0, 0}, {0, 0, 1, 0, 0}, {0, 0, 0, alpha, 0}, {0, 0, 0, 0, 1}}};
    Gdiplus::ImageAttributes ia;
    ia.SetColorMatrix(&cm);
    g.DrawImage(scaled, RectF(r.X, r.Y, static_cast<float>(w), static_cast<float>(h)), 0, 0, static_cast<float>(w), static_cast<float>(h),
                Gdiplus::UnitPixel, &ia);
}

// Minimize / maximize / close, drawn by the app because the system title bar is removed.
void DrawCaptionButtons(Gdiplus::Graphics& g, float W) {
    const float bw = S(46), bh = S(34);
    const Act acts[3] = {Act::WinMin, Act::WinMax, Act::WinClose};
    for (int i = 0; i < 3; ++i) {
        const RectF r(W - bw * (3 - i), 0, bw, bh);
        AddHit(acts[i], 0, r);
        const float h = HoverT(acts[i], 0);
        if (h > 0.01f) {
            Gdiplus::SolidBrush b(Col(i == 2 ? RGB(232, 17, 35) : th::card2, A(h)));
            g.FillRectangle(&b, r);
        }
        Gdiplus::Pen pen(Col(Mix(th::dim, RGB(255, 255, 255), h)), S(1));
        const float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2, d = S(5);
        if (i == 0) {
            g.DrawLine(&pen, cx - d, cy, cx + d, cy);
        } else if (i == 1) {
            if (IsZoomed(g_hwnd)) {
                g.DrawRectangle(&pen, cx - d + S(2), cy - d, d * 2 - S(2), d * 2 - S(2));
                g.DrawRectangle(&pen, cx - d, cy - d + S(2), d * 2 - S(2), d * 2 - S(2));
            } else {
                g.DrawRectangle(&pen, cx - d, cy - d, d * 2, d * 2);
            }
        } else {
            g.DrawLine(&pen, cx - d, cy - d, cx + d, cy + d);
            g.DrawLine(&pen, cx - d, cy + d, cx + d, cy - d);
        }
    }
}

// ---------------------------------------------------------------- screen
void DrawSidebar(Gdiplus::Graphics& g, const Lay& L) {
    const float x = S(22);
    const float intro = EaseOutCubic(Progress(g_appStart, 0.6));
    DrawLogo(g, x, S(26) - S(8) * (1 - intro), S(42));
    Text(g, L"AeroHub", *g_fBrand, RectF(x + S(54), S(24), S(170), S(26)), th::text, intro);
    Text(g, Tr(L"Game tool hub", L"Хаб для игр"), *g_fSmall, RectF(x + S(54), S(48), S(170), S(20)), th::dim, intro);

    float y = S(100);
    Text(g, Tr(L"GAMES", L"ИГРЫ"), *g_fSmallBold, RectF(x, y, S(150), S(20)), th::faint, intro);
    y += S(28);

    const float itemH = S(66), step = S(72);
    // Selection highlight glides between items.
    if (!g_profiles.empty()) {
        const float selY = Ease(Key(10), y + step * static_cast<float>(std::max(0, g_sel)), 14.0f);
        const RectF hl(S(12), selY, L.sidebar.Width - S(24), itemH);
        Fill(g, hl, S(12), th::card2);
        Stroke(g, hl, S(12), th::line);
        Fill(g, RectF(hl.X + S(1), hl.Y + S(20), S(3), itemH - S(40)), S(1.5f), th::accent);
    }
    for (std::size_t i = 0; i < g_profiles.size(); ++i) {
        const int ii = static_cast<int>(i);
        const float appear = EaseOutCubic(Progress(g_appStart + 0.12 + 0.07 * ii, 0.45));
        const RectF r(S(12) + S(18) * (1 - appear), y, L.sidebar.Width - S(24), itemH);
        const float h = HoverT(Act::SelectGame, ii);
        const bool sel = ii == g_sel;
        if (!sel) Fill(g, r, S(12), th::card2, 0.6f * h * appear);
        COLORREF c1, c2;
        GameHue(g_profiles[i], c1, c2);
        const RectF tile(r.X + S(12), r.Y + (itemH - S(42)) / 2 - S(1.5f) * h, S(42), S(42));
        Grad(g, tile, S(12), c1, c2, appear, 45.0f);
        if (Gdiplus::Bitmap* art = GameArt(g_profiles[i], true)) {
            DrawCover(g, art, tile, S(12), appear, 0.5f);
            Stroke(g, tile, S(12), RGB(255, 255, 255), 0.10f * appear);
        } else {
            Fill(g, RectF(tile.X + S(1), tile.Y + S(1), tile.Width - S(2), tile.Height * 0.45f), S(11), RGB(255, 255, 255), 0.12f * appear);
            const std::wstring letter = g_profiles[i].name.empty() ? L"?" : g_profiles[i].name.substr(0, 1);
            Text(g, letter, *g_fTile, tile, RGB(255, 255, 255), appear, Gdiplus::StringAlignmentCenter);
        }
        Text(g, g_profiles[i].name, *g_fBodyBold, RectF(tile.X + S(54), r.Y + S(12), r.Width - S(80), S(22)),
             Mix(th::dim, th::text, sel ? 1.0f : 0.6f + 0.4f * h), appear);
        const COLORREF sc = StatusColor(g_status[i].level);
        FillDot(g, tile.X + S(58), r.Y + S(44), S(3.5f), sc, A(appear));
        Text(g, StatusWord(i), *g_fSmall, RectF(tile.X + S(68), r.Y + S(34), r.Width - S(90), S(20)), th::dim, appear);
        AddHit(Act::SelectGame, ii, r);
        y += step;
    }

    // Bottom panel: auto-inject, language, version and update.
    const float pw = L.sidebar.Width - S(24);
    {  // account: who owns the key, how long it lasts, account page and sign-out
        const RectF acc(S(12), L.sidebar.Height - S(196) - S(12) - S(112), pw, S(112));
        Fill(g, acc, S(16), th::card, 0.9f * intro);
        Stroke(g, acc, S(16), th::line, intro);
        const RectF av(acc.X + S(14), acc.Y + S(14), S(36), S(36));
        Fill(g, av, S(18), th::card2, intro);
        Stroke(g, av, S(18), th::lineHi, intro);
        const std::wstring who = g_licUser.empty() ? std::wstring(Tr(L"Account", L"Аккаунт")) : g_licUser;
        Text(g, who.substr(0, 1), *g_fBodyBold, av, th::accent, intro, Gdiplus::StringAlignmentCenter);
        Text(g, who, *g_fBodyBold, RectF(av.X + S(46), acc.Y + S(12), pw - S(76), S(22)), th::text, intro);
        const bool soon = g_licExpiresMs > 0 && g_licExpiresMs / 1000 - UnixNow() < 3 * 86400;
        FillDot(g, av.X + S(50), acc.Y + S(43), S(3.5f), soon ? th::warn : th::ok, A(intro));
        Text(g, LicenseLeftText(), *g_fSmall, RectF(av.X + S(60), acc.Y + S(33), pw - S(90), S(20)), th::dim, intro);
        const float bw = (pw - S(28) - S(8)) / 2;
        Button(g, RectF(acc.X + S(14), acc.Y + S(64), bw, S(34)), Tr(L"Account", L"Кабинет"), BtnKind::Secondary, true, Act::LicenseAccount, 0, intro);
        Button(g, RectF(acc.X + S(14) + bw + S(8), acc.Y + S(64), bw, S(34)), Tr(L"Sign out", L"Выйти"), BtnKind::Ghost, !g_demo, Act::LicenseSignOut, 0, intro);
    }
    const RectF panel(S(12), L.sidebar.Height - S(196), pw, S(182));
    Fill(g, panel, S(16), th::card, 0.9f * intro);
    Stroke(g, panel, S(16), th::line, intro);
    const float px = panel.X + S(16);
    Text(g, Tr(L"Auto-inject", L"Автовнедрение"), *g_fBodyBold, RectF(px, panel.Y + S(14), S(160), S(22)), th::text, intro);
    Text(g, Tr(L"Loads the module after start", L"Модуль сам после запуска"), *g_fSmall, RectF(px, panel.Y + S(36), S(170), S(18)), th::dim, intro);
    Toggle(g, RectF(panel.X + pw - S(16) - S(46), panel.Y + S(20), S(46), S(26)), g_autoInject, Act::AutoSwitch);
    AddHit(Act::AutoSwitch, 0, RectF(panel.X, panel.Y + S(6), pw, S(56)));

    Segmented(g, RectF(px, panel.Y + S(70), pw - S(32), S(34)), {L"English", L"Русский"}, g_lang, Act::LangEn, 0, Key(11));

    std::wstring label = Tr(L"Check updates", L"Обновления");
    BtnKind kind = BtnKind::Secondary;
    bool enabled = !g_demo;
    switch (UpdateNow()) {
        case UpState::Checking: label = Tr(L"Checking…", L"Проверка…"); enabled = false; break;
        case UpState::UpToDate: label = Tr(L"Up to date", L"Последняя"); break;
        case UpState::Available: {
            std::lock_guard<std::mutex> lock(g_upLock);
            label = std::wstring(Tr(L"Update ", L"Обновить ")) + g_up.release.tag;
            kind = BtnKind::Primary;
            break;
        }
        case UpState::Downloading: label = Tr(L"Downloading…", L"Загрузка…"); enabled = false; break;
        case UpState::Failed: label = Tr(L"Retry check", L"Повторить"); break;
        case UpState::Idle: break;
    }
    Text(g, std::wstring(L"v") + aero::kAppVersion, *g_fMonoSmall, RectF(px, panel.Y + S(124), S(70), S(34)), th::faint, intro);
    Button(g, RectF(panel.X + pw - S(16) - S(138), panel.Y + S(124), S(138), S(34)), label, kind, enabled, Act::Update, 0, intro);
}

void DrawHero(Gdiplus::Graphics& g, const RectF& h, float a) {
    const int sel = SelectedProfile();
    Card(g, h, a);
    if (sel < 0) {
        Text(g, Tr(L"No game profiles", L"Нет профилей игр"), *g_fHero, RectF(h.X + S(28), h.Y + S(26), h.Width, S(44)), th::text, a);
        Text(g, Tr(L"Add a profile .ini to the profiles folder next to the hub.", L"Добавьте профиль .ini в папку profiles рядом с хабом."),
             *g_fBody, RectF(h.X + S(28), h.Y + S(78), h.Width, S(24)), th::dim, a);
        return;
    }
    const aero::GameProfile& p = g_profiles[sel];
    COLORREF c1, c2;
    GameHue(p, c1, c2);
    // Banner: game colors fading out to the right, a large faint initial, a thin light line on top.
    {
        Gdiplus::GraphicsPath clip;
        RoundPath(clip, h, S(16));
        g.SetClip(&clip);
        if (Gdiplus::Bitmap* art = GameArt(p)) {
            // Slow drift of the picture while the page settles.
            const float drift = 1.0f - EaseOutCubic(Progress(g_pageStart, 1.6));
            const RectF zoom(h.X - S(30) * drift, h.Y - S(10) * drift, h.Width + S(60) * drift, h.Height + S(20) * drift);
            DrawCover(g, art, drift > 0.001f ? zoom : h, 0, a, 0.55f);
            Gdiplus::LinearGradientBrush shade(h, Col(th::bg, 0), Col(th::bg, 0), 0.0f, TRUE);
            Gdiplus::REAL pos[3] = {0.0f, 0.55f, 1.0f};
            Gdiplus::Color cols[3] = {Col(th::bg, A(0.94f * a)), Col(th::bg, A(0.55f * a)), Col(th::bg, A(0.12f * a))};
            shade.SetInterpolationColors(cols, pos, 3);
            g.FillRectangle(&shade, h);
            const RectF lower(h.X, h.Y + h.Height * 0.5f, h.Width, h.Height * 0.5f);
            Gdiplus::LinearGradientBrush bottom(Inflate(lower, 1), Col(th::bg, 0), Col(th::bg, A(0.55f * a)), 90.0f, TRUE);
            g.FillRectangle(&bottom, lower);
        } else {
            Gdiplus::Font big(L"Segoe UI Semibold", S(190), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
            Text(g, p.name.substr(0, 1), big, RectF(h.X + h.Width - S(260), h.Y - S(44), S(240), S(250)), RGB(255, 255, 255), 0.05f * a,
                 Gdiplus::StringAlignmentFar);
        }
        g.ResetClip();
    }
    const Status& st = g_status[sel];
    Text(g, p.name, *g_fHero, RectF(h.X + S(28), h.Y + S(22), h.Width * 0.55f, S(46)), th::text, a);
    const float cw = Chip(g, h.X + S(28), h.Y + S(76), StatusWord(static_cast<std::size_t>(sel)), st.level, a);
    Text(g, st.text, *g_fSmall, RectF(h.X + S(28) + cw + S(12), h.Y + S(76), h.Width * 0.62f - cw - S(40), S(28)), th::dim, a);
    if (!p.note.empty()) Text(g, p.note, *g_fSmall, RectF(h.X + S(28), h.Y + S(112), h.Width * 0.55f, S(22)), th::faint, a);

    // Actions on the right, stacked from the right edge.
    const PrimaryState prim = GetPrimary();
    const float bh = S(42), by = h.Y + (h.Height - bh) / 2;
    float right = h.X + h.Width - S(24);
    auto place = [&](float w) {
        right -= w;
        const RectF r(right, by, w, bh);
        right -= S(10);
        return r;
    };
    if (p.kind == aero::GameKind::SlimeRancher) {
        const bool canLaunch = g_sr.found && !g_sr.install.appId.empty();
        Button(g, place(S(160)), Tr(L"Launch game", L"Запустить игру"), BtnKind::Secondary, canLaunch, Act::Secondary, 0, a);
        Button(g, place(S(150)), prim.label, BtnKind::Primary, prim.enabled, Act::Primary, 0, a);
    } else if (p.kind == aero::GameKind::FabricMod) {
        Button(g, place(S(190)), Tr(L"Download mod", L"Скачать мод"), BtnKind::Primary, true, Act::Secondary, 0, a);
    } else {
        Button(g, place(S(104)), Tr(L"Refresh", L"Обновить"), BtnKind::Ghost, true, Act::Refresh, 0, a);
        Button(g, place(S(184)), prim.label, BtnKind::Primary, prim.enabled, Act::Primary, 0, a);
        if (IsForts(p)) Button(g, place(S(150)), Tr(L"Collect logs", L"Собрать логи"), BtnKind::Secondary, true, Act::CollectLogs, 0, a);
    }
}

void DrawGuide(Gdiplus::Graphics& g, const RectF& c, float a) {
    Card(g, c, a);
    const int sel = SelectedProfile();
    const bool slime = sel >= 0 && g_profiles[sel].kind == aero::GameKind::SlimeRancher;
    COLORREF c1 = th::blue, c2 = th::violet;
    if (sel >= 0) GameHue(g_profiles[sel], c1, c2);
    CardTitle(g, c, slime ? Tr(L"Game part", L"Часть игры") : Tr(L"How to use", L"Как пользоваться"), c1, a);
    const bool active = sel >= 0 && g_status[sel].level == Level::Active;
    const bool minecraft = sel >= 0 && g_profiles[sel].kind == aero::GameKind::FabricMod;
    const std::wstring lead = minecraft ? (active ? Tr(L"AeroClient is loaded. Everything is switched in the game itself.", L"AeroClient загружен. Всё переключается в игре.")
                                          : g_mc.found ? Tr(L"Download the mod, drop it into the mods folder and start the game.", L"Скачайте мод, положите в папку mods и запустите игру.")
                                                       : Tr(L"Download the mod and drop it into the mods folder of Fabric 1.21.1.", L"Скачайте мод и положите его в папку mods у Fabric 1.21.1."))
                              : active   ? Tr(L"Cheats are switched in the game itself. Open its menu during a match.", L"Функции включаются в самой игре: откройте её меню.")
                                         : Tr(L"Launch the game and load the module; cheats are then switched in the game's own menu.", L"Запустите игру и модуль, функции включаются в игре.");
    float y = c.Y + S(58);
    const float x = c.X + S(24), w = c.Width - S(48);
    // Forts on Vulkan never shows the menu (the module hooks OpenGL): say so and offer the fix.
    if (sel >= 0 && IsForts(g_profiles[sel]) &&
        (g_gfx.state == aero::diag::GraphicsState::Vulkan || g_gfx.state == aero::diag::GraphicsState::NotSet)) {
        const RectF strip(x, y, w, S(50));
        Fill(g, strip, S(12), th::warn, 0.12f * a);
        Stroke(g, strip, S(12), th::warn, 0.45f * a);
        const RectF btn(strip.X + strip.Width - S(156) - S(7), strip.Y + S(7), S(156), strip.Height - S(14));
        const bool vulkan = g_gfx.state == aero::diag::GraphicsState::Vulkan;
        Text(g, vulkan ? Tr(L"Forts is on Vulkan: needs OpenGL", L"Forts на Vulkan: нужен OpenGL") : Tr(L"Graphics API not set: needs OpenGL", L"Графический API не задан: нужен OpenGL"),
             *g_fSmallBold, RectF(strip.X + S(14), strip.Y, strip.Width - btn.Width - S(30), strip.Height), th::warn, a);
        Button(g, btn, Tr(L"Switch to OpenGL", L"На OpenGL"), BtnKind::Primary, true, Act::SwitchOpenGl, 0, a);
        y += S(62);
    }
    if (slime) {
        Segmented(g, RectF(x, y, std::min(w, S(400)), S(40)), {aero::sr::Title(1), aero::sr::Title(2)}, g_sr.part - 1, Act::SrPart, 1, Key(12), a);
        y += S(54);
    } else {
        Text(g, lead, *g_fBody, RectF(x, y, w, S(44)), th::text, a, Gdiplus::StringAlignmentNear, Gdiplus::StringAlignmentNear, true);
        y += S(50);
    }
    if (sel < 0) return;
    std::wstring hk = g_profiles[sel].hotkeys;
    if (g_lang == 1) {
        static const std::pair<const wchar_t*, const wchar_t*> kHints[] = {
            {L"Insert = open the cheat menu (click rows, or use the arrow keys)", L"Insert = открыть меню читов (клик по строкам или стрелки)"},
            {L"F6 = unlimited resources on / off", L"F6 = бесконечные ресурсы вкл / выкл"},
            {L"End = everything off", L"End = всё выключить"},
            {L"Right Shift or Insert = open the module menu", L"Right Shift или Insert = открыть меню модуля"},
            {L".mine diamond_ore = auto-mine an ore", L".mine diamond_ore = автодобыча руды"},
            {L".help = chat commands", L".help = команды чата"}};
        for (const auto& hint : kHints) {
            const std::size_t at = hk.find(hint.first);
            if (at != std::wstring::npos) hk.replace(at, wcslen(hint.first), hint.second);
        }
    }
    if (slime) {
        hk = std::wstring(Tr(L"Game: ", L"Игра: ")) + aero::sr::Title(g_sr.part) +
             (g_sr.found ? Tr(L"  |  Folder: ", L"  |  Папка: ") + g_sr.install.dir.wstring() : std::wstring(Tr(L"  |  Not found in the Steam libraries", L"  |  Не найдена в библиотеках Steam"))) +
             Tr(L"  |  Cheats: not built yet (they need the game to be measured)", L"  |  Читы: пока нет (их делают на самой игре)");
    }
    if (minecraft) {
        std::wstring info = g_mc.found ? std::wstring(Tr(L"Game: ", L"Игра: ")) + GameLabel(g_mc.game) + Tr(L"  |  Folder: ", L"  |  Папка: ") + g_mc.game.dir.wstring() +
                                             Tr(L"  |  Mod: ", L"  |  Мод: ") +
                                             (aero::mc::HasMod(g_mc.game.dir) ? Tr(L"in the mods folder", L"в папке mods") : Tr(L"not in the mods folder yet", L"ещё не в папке mods"))
                                       : std::wstring(Tr(L"Game: not detected yet", L"Игра: ещё не найдена"));
        info += std::wstring(Tr(L"  |  Mod file: ", L"  |  Файл мода: ")) +
                (g_mc.savedJar.empty() ? std::wstring(Tr(L"press Download mod, it goes to Downloads", L"нажмите «Скачать мод», файл попадёт в Загрузки")) : g_mc.savedJar.wstring());
        hk = info + (hk.empty() ? L"" : L"  |  " + hk);
    }
    const float lh = S(38);
    int n = 0;
    for (std::size_t start = 0; start <= hk.size() && !hk.empty();) {
        std::size_t end = hk.find(L'|', start);
        if (end == std::wstring::npos) end = hk.size();
        std::wstring part = hk.substr(start, end - start);
        start = end + 1;
        const auto b = part.find_first_not_of(L' ');
        const auto e = part.find_last_not_of(L' ');
        if (b == std::wstring::npos) continue;
        part = part.substr(b, e - b + 1);
        if (y + lh > c.Y + c.Height - S(12)) break;
        const float rowA = a * EaseOutCubic(Progress(g_pageStart + 0.08 + 0.05 * n, 0.35));
        HintRow(g, RectF(x + S(10) * (1 - rowA / std::max(a, 0.01f)), y, w, lh), part, rowA);
        y += lh;
        ++n;
    }
}

void DrawLive(Gdiplus::Graphics& g, const RectF& c, float a) {
    Card(g, c, a);
    CardTitle(g, c, Tr(L"Live info", L"Данные игры"), th::cyan, a);
    {
        const float w = S(30) + MeasureW(g, g_link.text, *g_fSmallBold) + S(28);
        Chip(g, c.X + c.Width - S(20) - w, c.Y + S(17), g_link.text, g_link.level, a);
    }
    if (g_infoRows.empty()) {
        const float cx = c.X + c.Width / 2, cy = c.Y + c.Height / 2 + S(4);
        const float spin = 300.0f;
        Gdiplus::Pen ring(Col(th::line, A(a)), S(3));
        g.DrawEllipse(&ring, cx - S(20), cy - S(42), S(40), S(40));
        Gdiplus::Pen arc(Col(th::cyan, A(0.8f * a)), S(3));
        arc.SetStartCap(Gdiplus::LineCapRound);
        arc.SetEndCap(Gdiplus::LineCapRound);
        g.DrawArc(&arc, cx - S(20), cy - S(42), S(40), S(40), spin, 70.0f);
        Text(g, Tr(L"Waiting for the game", L"Ждём игру"), *g_fBodyBold, RectF(c.X, cy + S(8), c.Width, S(24)), th::text, a, Gdiplus::StringAlignmentCenter);
        Text(g, Tr(L"Values appear while the module is active in a match.", L"Данные появятся, когда модуль активен в матче."), *g_fSmall,
             RectF(c.X + S(16), cy + S(32), c.Width - S(32), S(22)), th::dim, a, Gdiplus::StringAlignmentCenter);
        return;
    }
    float y = c.Y + S(60);
    const float rowH = S(38);
    int i = 0;
    for (const InfoRow& r : g_infoRows) {
        if (y + rowH > c.Y + c.Height - S(10)) break;
        const RectF row(c.X + S(14), y, c.Width - S(28), rowH - S(4));
        if (i % 2 == 0) Fill(g, row, S(9), th::card2, 0.45f * a);
        Text(g, r.key, *g_fSmall, RectF(row.X + S(12), y, row.Width * 0.55f, rowH - S(4)), th::dim, a);
        Text(g, r.value, *g_fMono, RectF(row.X + row.Width * 0.45f, y, row.Width * 0.55f - S(12), rowH - S(4)), th::text, a,
             Gdiplus::StringAlignmentFar);
        y += rowH;
        ++i;
    }
}

void DrawLog(Gdiplus::Graphics& g, const Lay& L) {
    Card(g, L.log);
    const RectF& c = L.log;
    CardTitle(g, c, Tr(L"Journal", L"Журнал"), th::violet, 1.0f);
    Segmented(g, RectF(c.X + S(140), c.Y + S(13), S(230), S(34)), {Tr(L"Module", L"Модуль"), Tr(L"Hub", L"Хаб")}, g_tab, Act::TabModule, 0, Key(13));
    // Collapse / expand chevron.
    const RectF chev(c.X + c.Width - S(54), c.Y + S(12), S(38), S(36));
    AddHit(Act::LogToggle, 0, chev);
    const float h = HoverT(Act::LogToggle, 0);
    Fill(g, chev, S(10), th::card2, 0.4f + 0.6f * h);
    const float ang = 180.0f * g_logOpenT;
    Gdiplus::Pen pen(Col(Mix(th::dim, th::text, h)), S(2));
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    Gdiplus::GraphicsState s = g.Save();
    g.TranslateTransform(chev.X + chev.Width / 2, chev.Y + chev.Height / 2);
    g.RotateTransform(ang);
    const Gdiplus::PointF pts[3] = {{-S(6), -S(3)}, {0, S(3)}, {S(6), -S(3)}};
    g.DrawLines(&pen, pts, 3);
    g.Restore(s);
    if (L.logBody.Height > S(8)) {
        Fill(g, L.logBody, S(12), th::logBg, std::clamp(L.logBody.Height / S(40), 0.0f, 1.0f));
        Stroke(g, L.logBody, S(12), th::line, std::clamp(L.logBody.Height / S(40), 0.0f, 1.0f));
    }
}

void DrawLicenseGate(Gdiplus::Graphics& g, int w, int h);

void Render(Gdiplus::Graphics& g, int w, int h) {
    g_now = NowSec();
    if (g_appStart == 0) g_appStart = g_now;
    const double gap = g_now - g_lastFrame;
    g_dt = gap > 0.1 ? 1.0f / 60.0f : static_cast<float>(gap);
    g_lastFrame = g_now;
    g_animating = false;
    g_pulsing = false;

    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g_hits.clear();

    const float openT = EaseOutCubic(Ease(Key(14), g_logOpen ? 1.0f : 0.0f, 9.0f));
    if (std::fabs(openT - g_logOpenT) > 0.0005f) {
        g_logOpenT = openT;
        PositionLogs();
    }
    if (g_lic != Lic::Ok) {
        DrawLicenseGate(g, w, h);
        DrawCaptionButtons(g, static_cast<float>(w));
        return;
    }
    const Lay L = Compute(w, h, g_logOpenT);
    DrawSidebar(g, L);

    // Page transition when another game is chosen: content slides up and fades in, cards one after another.
    const float intro = EaseOutCubic(Progress(g_appStart + 0.1, 0.5));
    auto page = [&](double delay) { return std::min(intro, EaseOutCubic(Progress(g_pageStart + delay, 0.38))); };
    const float a1 = page(0.0), a2 = page(0.06), a3 = page(0.12);
    auto lift = [&](RectF r, float t) { r.Y += S(18) * (1 - t); return r; };
    DrawHero(g, lift(L.hero, a1), a1);
    DrawGuide(g, lift(L.guide, a2), a2);
    DrawLive(g, lift(L.live, a3), a3);
    DrawLog(g, L);
    DrawCaptionButtons(g, static_cast<float>(w));
}

void DrawLicenseGate(Gdiplus::Graphics& g, int w, int h) {
    const float a = EaseOutCubic(Progress(g_pageStart, 0.4));
    const float cw = S(480), ch = S(330);
    RectF c((w - cw) / 2, (h - ch) / 2 + S(14) * (1 - a), cw, ch);
    Card(g, c, a);
    const RectF mark(c.X + S(40), c.Y + S(36), S(40), S(40));
    Fill(g, mark, S(11), th::card2, a);
    Stroke(g, mark, S(11), th::lineHi, a);
    Text(g, L"A", *g_fTitle, mark, th::text, a, Gdiplus::StringAlignmentCenter);
    Fill(g, RectF(mark.X + S(29), mark.Y + S(29), S(6), S(6)), S(3), th::accent, a);
    Text(g, Tr(L"Activate AeroHub", L"Активация AeroHub"), *g_fTitle, RectF(c.X + S(96), c.Y + S(34), cw - S(130), S(30)), th::text, a);
    Text(g, Tr(L"Enter the key from your account on the site", L"Введите ключ из личного кабинета на сайте"), *g_fSmall,
         RectF(c.X + S(96), c.Y + S(62), cw - S(130), S(20)), th::dim, a);
    if (g_lic == Lic::Checking) {
        Text(g, Tr(L"Checking the key…", L"Проверяем ключ…"), *g_fBody, RectF(c.X + S(40), c.Y + S(140), cw - S(80), S(40)), th::dim, a);
        return;
    }
    Text(g, Tr(L"Licence key", L"Ключ"), *g_fSmallBold, RectF(c.X + S(40), c.Y + S(112), cw - S(80), S(20)), th::dim, a);
    const RectF field(c.X + S(28), c.Y + S(138), cw - S(56), S(48));
    Fill(g, field, S(12), th::logBg, a);
    Stroke(g, field, S(12), GetFocus() == g_editKey ? th::accent : th::lineHi, a);
    if (!g_licMsg.empty())
        Text(g, g_licMsg, *g_fSmall, RectF(c.X + S(40), c.Y + S(192), cw - S(80), S(36)), th::bad, a, Gdiplus::StringAlignmentNear,
             Gdiplus::StringAlignmentNear, true);
    const float bw = (cw - S(56) - S(12)) / 2, by = c.Y + ch - S(28) - S(46);
    Button(g, RectF(c.X + S(28), by, bw, S(46)), g_licBusy ? Tr(L"Checking…", L"Проверка…") : Tr(L"Activate", L"Активировать"), BtnKind::Primary,
           !g_licBusy, Act::LicenseActivate, 0, a);
    Button(g, RectF(c.X + S(28) + bw + S(12), by, bw, S(46)), Tr(L"Open the site", L"Открыть сайт"), BtnKind::Secondary, true, Act::LicenseSite, 0, a);
}

void Paint(HDC dc, int w, int h) {
    if (w <= 0 || h <= 0) return;
    HDC mem = CreateCompatibleDC(dc);
    // A 32-bit DIB back buffer: GDI+ draws on it far faster than on a device-dependent bitmap.
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bm);
    {  // the static background is a plain GDI blit (a full-window GDI+ DrawImage costs ~30 ms)
        static HBITMAP bgHbm = nullptr;
        static const Gdiplus::Bitmap* bgFor = nullptr;
        Gdiplus::Bitmap* bgBmp = Background(w, h);
        if (bgFor != bgBmp || !bgHbm) {
            if (bgHbm) DeleteObject(bgHbm);
            bgHbm = nullptr;
            bgBmp->GetHBITMAP(Gdiplus::Color(255, 0, 0, 0), &bgHbm);
            bgFor = bgBmp;
        }
        HDC src = CreateCompatibleDC(dc);
        HGDIOBJ o2 = SelectObject(src, bgHbm);
        BitBlt(mem, 0, 0, w, h, src, 0, 0, SRCCOPY);
        SelectObject(src, o2);
        DeleteDC(src);
    }
    {
        Gdiplus::Graphics g(mem);
        Render(g, w, h);
    }
    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bm);
    DeleteDC(mem);
    // Keep frames coming while something moves (60 fps), slower for the idle pulse, none when still.
    const UINT want = g_animating ? 15 : (g_pulsing ? 50 : 0);
    if (want != g_animInterval) {
        if (want) SetTimer(g_hwnd, kAnimTimer, want, nullptr);
        else KillTimer(g_hwnd, kAnimTimer);
        g_animInterval = want;
    }
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

// Changing the language rebuilds the texts that were computed from it (statuses and the link pill).
void SetLanguage(int lang) {
    if (g_lang == lang) return;
    g_lang = lang;
    WritePrivateProfileStringW(L"Hub", L"Language", g_lang == 1 ? L"ru" : L"en", g_settingsFile.c_str());
    Refresh();
}

void Perform(Act act, int idx) {
    if (g_lic != Lic::Ok && act != Act::WinMin && act != Act::WinMax && act != Act::WinClose && act != Act::LicenseActivate && act != Act::LicenseSite)
        return;
    switch (act) {
        case Act::LicenseActivate: ActivateFromEdit(); break;
        case Act::LicenseSite: ShellExecuteW(nullptr, L"open", aero::license::kSiteUrl, nullptr, nullptr, SW_SHOWNORMAL); break;
        case Act::LicenseAccount:
            ShellExecuteW(nullptr, L"open", (std::wstring(aero::license::kSiteUrl) + L"/account").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case Act::LicenseSignOut: SignOut(); break;
        case Act::SelectGame:
            if (idx != g_sel) {
                g_sel = idx;
                g_pageStart = NowSec();
                UpdateChannel();
                PollModuleLog();
            }
            break;
        case Act::SrPart:
            if ((idx == 1 || idx == 2) && idx != g_sr.part) {
                g_sr.part = idx;
                WritePrivateProfileStringW(L"SlimeRancher", L"Part", idx == 2 ? L"2" : L"1", g_settingsFile.c_str());
                DetectSlimeRancher();
                Refresh();
            }
            break;
        case Act::Primary: DoPrimary(); break;
        case Act::Secondary: DoSecondary(); break;
        case Act::Refresh: Refresh(); break;
        case Act::AutoSwitch: g_autoInject = !g_autoInject; SaveSettings(); break;
        case Act::TabModule: g_tab = idx == 1 ? 1 : 0; PositionLogs(); break;
        case Act::TabHub: g_tab = 1; PositionLogs(); break;
        case Act::CollectLogs: DoCollectLogs(); break;
        case Act::SwitchOpenGl: DoSwitchOpenGl(); break;
        case Act::Update: DoUpdate(); break;
        case Act::LangEn: SetLanguage(idx == 1 ? 1 : 0); break;
        case Act::LangRu: SetLanguage(1); break;
        case Act::WinMin: ShowWindow(g_hwnd, SW_MINIMIZE); break;
        case Act::WinMax: ShowWindow(g_hwnd, IsZoomed(g_hwnd) ? SW_RESTORE : SW_MAXIMIZE); break;
        case Act::WinClose: PostMessageW(g_hwnd, WM_CLOSE, 0, 0); break;
        case Act::LogToggle:
            g_logOpen = !g_logOpen;
            WritePrivateProfileStringW(L"Hub", L"LogOpen", g_logOpen ? L"1" : L"0", g_settingsFile.c_str());
            break;
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
            {  // explicit big and small icons, so the taskbar takes them from the window and not from a cached copy
                const HINSTANCE inst = GetModuleHandleW(nullptr);
                const auto big = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
                const auto smallIcon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
                if (big) SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
                if (smallIcon) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
            }
            const BOOL dark = TRUE;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            {  // Windows 11: title bar and border in the app's own colors
                const COLORREF caption = th::side, captionText = th::text, border = th::line;
                DwmSetWindowAttribute(hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof(caption));
                DwmSetWindowAttribute(hwnd, 36 /*DWMWA_TEXT_COLOR*/, &captionText, sizeof(captionText));
                DwmSetWindowAttribute(hwnd, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof(border));
            }
            g_dpi = static_cast<int>(GetDpiForWindow(hwnd));
            const DWORD style = WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL;
            g_editModule = CreateWindowExW(0, L"EDIT", L"", style | WS_VISIBLE, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(kEditModule), nullptr, nullptr);
            g_editHub = CreateWindowExW(0, L"EDIT", L"", style, 0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(kEditHub), nullptr, nullptr);
            for (HWND e : {g_editModule, g_editHub}) {
                SendMessageW(e, EM_SETLIMITTEXT, 1 << 20, 0);
                SetWindowTheme(e, L"DarkMode_Explorer", nullptr);
            }
            g_editKey = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_UPPERCASE, 0, 0, 0, 0, hwnd,
                                        reinterpret_cast<HMENU>(kEditKey), nullptr, nullptr);
            SendMessageW(g_editKey, EM_SETLIMITTEXT, 40, 0);
            SendMessageW(g_editKey, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"AERO-XXXX-XXXX-XXXX-XXXX"));
            MakeFonts();

            g_autoInject = GetPrivateProfileIntW(L"Hub", L"AutoInject", 0, g_settingsFile.c_str()) != 0;
            g_logOpen = GetPrivateProfileIntW(L"Hub", L"LogOpen", 1, g_settingsFile.c_str()) != 0;
            g_logOpenT = g_logOpen ? 1.0f : 0.0f;
            g_springs[Key(14)] = g_logOpenT;
            {
                wchar_t lang[16]{};
                GetPrivateProfileStringW(L"Hub", L"Language", L"", lang, static_cast<DWORD>(std::size(lang)), g_settingsFile.c_str());
                g_lang = lang[0] ? (_wcsicmp(lang, L"ru") == 0 ? 1 : 0) : (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN ? 1 : 0);
            }
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
            g_sr.part = GetPrivateProfileIntW(L"SlimeRancher", L"Part", 1, g_settingsFile.c_str()) == 2 ? 2 : 1;
            DetectSlimeRancher();
            Refresh();
            PositionLogs();
            g_checkUpdates = GetPrivateProfileIntW(L"Hub", L"CheckUpdates", 1, g_settingsFile.c_str()) != 0;
            Log(std::wstring(L"AeroHub v") + aero::kAppVersion);
            if (g_checkUpdates) StartVersionCheck();
            {
                wchar_t key[96]{};
                GetPrivateProfileStringW(L"License", L"Key", L"", key, static_cast<DWORD>(std::size(key)), g_settingsFile.c_str());
                g_licKey = aero::license::NormalizeKey(key);
                GetPrivateProfileStringW(L"License", L"User", L"", key, static_cast<DWORD>(std::size(key)), g_settingsFile.c_str());
                g_licUser = key;
                GetPrivateProfileStringW(L"License", L"Expires", L"0", key, static_cast<DWORD>(std::size(key)), g_settingsFile.c_str());
                g_licExpiresMs = _wtoi64(key);
                GetPrivateProfileStringW(L"License", L"Games", L"", key, static_cast<DWORD>(std::size(key)), g_settingsFile.c_str());
                g_licGames = key;
                g_allProfiles = g_profiles;
                ApplyLicensedGames();
                Refresh();
                if (g_demo) g_lic = Lic::Ok;
                else if (g_licKey.empty()) g_lic = Lic::Need;
                else StartLicenseCheck(g_licKey);
                SetWindowTextW(g_editKey, g_licKey.c_str());
                PositionLogs();
            }
            SetTimer(hwnd, kTimer, 1000, nullptr);
            return 0;
        }
        case WM_NCCALCSIZE:
            if (wp) {  // no system title bar: the client area takes the whole window (the frame keeps resize and snap)
                auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
                if (IsZoomed(hwnd)) {
                    const UINT dpi = GetDpiForWindow(hwnd);
                    const int f = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    p->rgrc[0].left += f;
                    p->rgrc[0].top += f;
                    p->rgrc[0].right -= f;
                    p->rgrc[0].bottom -= f;
                }
                return 0;
            }
            break;
        case WM_NCHITTEST: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            RECT rc;
            GetClientRect(hwnd, &rc);
            const int b = static_cast<int>(S(6));
            if (!IsZoomed(hwnd)) {
                const bool l = pt.x < b, r = pt.x >= rc.right - b, t = pt.y < b, d = pt.y >= rc.bottom - b;
                if (t && l) return HTTOPLEFT;
                if (t && r) return HTTOPRIGHT;
                if (d && l) return HTBOTTOMLEFT;
                if (d && r) return HTBOTTOMRIGHT;
                if (l) return HTLEFT;
                if (r) return HTRIGHT;
                if (t) return HTTOP;
                if (d) return HTBOTTOM;
            }
            if (pt.y < S(40)) {
                Act a = Act::None;
                int idx = -1;
                bool en = false;
                if (!HitAt(pt.x, pt.y, a, idx, en)) return HTCAPTION;  // drag the window by its top strip
            }
            return HTCLIENT;
        }
        case WM_SIZE: PositionLogs(); Redraw(); return 0;
        case WM_KILLFOCUS: g_hoverAct = Act::None; g_hoverIdx = -1; Redraw(); break;
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
            if (wp == kAnimTimer) {
                Redraw();
                return 0;
            }
            if (g_lic != Lic::Ok) return 0;
            if (!g_demo && !g_licBusy && NowSec() - g_licLastCheck > kLicenseRecheckSec) StartLicenseCheck(g_licKey);
            PollWaiting();
            PollSlimeRancher();
            Refresh();
            AutoInject();
            PollModuleLog();
            return 0;
        case kMsgUpdate: OnUpdateMessage(wp, lp); return 0;
        case kMsgLicense: OnLicenseMessage(); return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == kEditKey && (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS)) Redraw();
            break;
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
            SetTextColor(dc, reinterpret_cast<HWND>(lp) == g_editKey ? th::text : RGB(196, 198, 204));
            SetBkColor(dc, th::logBg);
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
    g_demoVulkan = cmdLine && std::wstring(cmdLine).find(L"--demo-vulkan") != std::wstring::npos;

    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g_dir = std::filesystem::path(exe).parent_path();
    g_settingsFile = g_dir / L"AeroHub.ini";

    Gdiplus::GdiplusStartupInput gin;
    Gdiplus::GdiplusStartup(&g_gdiToken, &gin, nullptr);
    g_surfaceBrush = CreateSolidBrush(th::logBg);
    g_dpi = static_cast<int>(GetDpiForSystem());

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"AeroHubWnd";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"AeroHub", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                              static_cast<int>(S(1200)), static_cast<int>(S(780)), nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(hwnd, show);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    g_bgCache.reset(); g_fSmallBold.reset(); g_fMonoSmall.reset(); g_fHero.reset(); g_fTile.reset();
    g_fTitle.reset(); g_fH2.reset(); g_fBody.reset(); g_fBodyBold.reset(); g_fSmall.reset(); g_fMono.reset(); g_fBrand.reset();
    Gdiplus::GdiplusShutdown(g_gdiToken);
    return static_cast<int>(m.wParam);
}
