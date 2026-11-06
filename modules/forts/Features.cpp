#include "Features.hpp"

#include <windows.h>

#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "Ballistics.hpp"
#include "FortsConfig.hpp"
#include "GameApi.hpp"
#include "Hud.hpp"
#include "IatHook.hpp"
#include "Ipc.hpp"
#include "Log.hpp"
#include "Menu.hpp"
#include "Overlay.hpp"
#include "Ui.hpp"

namespace forts {
namespace {

constexpr int kKeyMenu = VK_INSERT;  // F7/F8 collide with the game's own hotkeys (F8 = FPS counter)
constexpr int kKeyUnlimited = VK_F6;
constexpr int kKeyAllOff = VK_END;
constexpr int kMenuUp = VK_UP, kMenuDown = VK_DOWN, kMenuLeft = VK_LEFT, kMenuRight = VK_RIGHT;
constexpr int kMaxTypeRows = 6, kMaxTargetRows = 6;
constexpr int kRowIdType = 100, kRowIdTarget = 200;  // menu row ids: < 100 are items

struct Settings {
    float gravityMagnitude = cfg::kDefaultGravityY;  // fallback |Physics.Gravity|; the sign is derived from the camera
};

// Maps WorldToScreen output to GL pixels. Derived every frame from the camera focus point, which sits
// at the centre of the viewport: scale = viewport / (2 * screen position of the focus).
struct Calibration {
    float scale = 1.0f;
    float gravitySign = 1.0f;  // +1 when +world Y moves down the screen
};

enum Item {
    // General
    kItemUnlimited, kItemFastBeams, kItemFastDevices, kItemFastReload, kItemCommander, kItemRepair, kItemRepairSpeed,
    kItemRepairWhat,
    // Combat
    kItemTrajectory, kItemImpact, kItemAutoAim,
    // Enemy
    kItemEnemyOverlay, kItemRadar, kItemIntel,
    // Tools
    kItemRecord, kItemDiagnostics, kItemAllOff,
    kItemCount
};
constexpr const char* kItemNames[kItemCount] = {
    "Unlimited resources", "Fast build: beams", "Fast build: tech and guns", "Minigun (fast reload)", "Commander auto-refill",
    "Auto-repair", "Repair speed", "Repair what", "Trajectory overlay", "Impact markers", "Auto-aim at target", "Enemy overlay",
    "Enemy base window (observer)", "Enemy intel (spy panel)", "Record shots to log", "Write diagnostics to log", "All off"};
constexpr aero::ipc::Kind kItemKinds[kItemCount] = {
    aero::ipc::kToggle, aero::ipc::kToggle, aero::ipc::kToggle, aero::ipc::kToggle, aero::ipc::kToggle, aero::ipc::kToggle,
    aero::ipc::kButton, aero::ipc::kButton,  // repair speed / repair what (choice rows)
    aero::ipc::kToggle, aero::ipc::kToggle, aero::ipc::kToggle, aero::ipc::kToggle, aero::ipc::kToggle,
    aero::ipc::kToggle, aero::ipc::kToggle, aero::ipc::kButton, aero::ipc::kButton};
constexpr int kItemTab[kItemCount] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3};

// Repair speed levels: share of the maximum health restored per second. "Instant" restores everything each pass.
constexpr const wchar_t* kRepairSpeedNames[4] = {L"Slow 8%/s", L"Medium 25%/s", L"Fast 60%/s", L"Instant"};
constexpr float kRepairRates[4] = {0.08f, 0.25f, 0.60f, 100.0f};
constexpr const wchar_t* kRepairWhatNames[3] = {L"Everything", L"Devices", L"Beams"};
constexpr const wchar_t* kTabNames[4] = {L"General", L"Combat", L"Enemy", L"Tools"};

struct State {
    bool unlimited = false;
    bool fastBeams = false;    // link (strut) build times
    bool fastDevices = false;  // device / weapon / tech build times
    bool fastReload = false;
    bool commander = false;
    bool repair = false;
    int repairLevel = 1;  // index into kRepairRates
    int repairWhat = 0;   // 0 everything, 1 devices only, 2 beams only
    bool trajectory = false;
    bool impact = false;
    bool autoAim = false;
    bool enemyOverlay = false;
    bool radar = false;
    bool intel = false;
    bool record = false;
    bool menuOpen = false;
    int menuTab = 0;
    int menuSel = 0;
    bool menuKeyboard = false;  // keyboard focus ring visible (cleared by mouse use)
    bool wantDiagnostics = false;
    int framesSinceUnlimited = 0;  // 1000 = apply on the next in-match frame (set by the switch)
    int framesSinceBuild = 1000;
    int framesSinceBeams = 1000;
    int framesSinceCommander = 1000;
    int framesSinceReload = 1000;
    int framesSinceRepair = 1000;
    int commanderCooldown = 0;
    bool beamsApplied = false;
    bool buildSaved = false, buildApplied = false;
    float buildA = 0, buildB = 0;  // original build-override values, restored when the switch goes off
    bool reloadSaved = false, reloadApplied = false;
    float reloadA = 0, reloadB = 0;
    bool warnedMaterials = false, loggedMaterials = false;
    int aimTarget = 0;  // enemy device id chosen in the Enemy tab, 0 = none
    int faults = 0;
    bool loggedCalibration = false;
    bool warnedCalibration = false;
};

GameApi g_api;
Overlay g_overlay;
Menu g_menu;
Settings g_settings;
State g_state;
aero::IatHook g_hook;
aero::ipc::Server g_ipc;
int g_featureId[kItemCount];
BOOL(WINAPI* g_origSwap)(HDC) = nullptr;
constexpr int kMaxFaults = 5;
float g_uiScale = 1.0f;
bool g_inMatch = false;
// Forts multiplayer is lockstep: every client runs the simulation and compares checksums, so a local write to
// simulation state ends in a desync. Online the read-only features run as usual and the AeroHub mod does the rest.
bool g_online = false;

// Link materials whose BuildTime we scaled: original value and the value we wrote (to notice a game reset).
struct SavedMaterial {
    int side, index;
    std::string name;
    float original, applied;
};
std::vector<SavedMaterial> g_materials;

// Weapon types of our own weapons (trajectory filter), projectile gravity and learned air drag per type.
std::map<std::string, int> g_typeCount;
std::map<std::string, bool> g_typeShow;      // missing = shown
std::map<std::string, float> g_typeGravity;  // |gravity| per type, resolved once
std::vector<std::string> g_typeList;         // type rows currently in the menu, in order
struct DragStat {
    double sum = 0;
    int n = 0;
    int savedAtN = 0;
};
std::map<std::string, DragStat> g_drag;

bool ShowType(const std::string& t) {
    const auto it = g_typeShow.find(t);
    return it == g_typeShow.end() || it->second;
}

// What we know about the other teams' devices (refreshed a few times per second).
struct EnemyDev {
    int id = 0;
    Vec3 pos;
    bool weapon = false;
    std::string type;
};
std::vector<EnemyDev> g_enemy;
std::vector<int> g_targets;  // enemy weapon ids offered in the Enemy tab (nearest to the camera first)
HudImage g_radarImage, g_intelImage;
bool g_intelFresh = false;
int g_enemyTeam = 0;
bool g_radarFresh = false;
std::map<std::string, HudImage> g_labels;
std::set<int> g_hitIds;  // enemy devices a drawn trajectory passes through this frame

// While the cursor is over the open menu the game must not see it: its mouse polling (GetCursorPos /
// GetKeyState, both imported by Forts.exe) is answered with a neutral position and "button up".
aero::IatHook g_hookCursor, g_hookKey;
BOOL(WINAPI* g_origGetCursorPos)(LPPOINT) = nullptr;
SHORT(WINAPI* g_origGetKeyState)(int) = nullptr;
volatile LONG g_blockMouse = 0;
POINT g_neutralPt{};

BOOL WINAPI HkGetCursorPos(LPPOINT p) {
    const BOOL ok = g_origGetCursorPos(p);
    if (ok && p && g_blockMouse) *p = g_neutralPt;
    return ok;
}

SHORT WINAPI HkGetKeyState(int vk) {
    if (g_blockMouse && (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON)) return 0;
    return g_origGetKeyState(vk);
}

std::wstring IniPath() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(g_self, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return {};
    wcscpy_s(slash + 1, path + MAX_PATH - (slash + 1), L"FortsModule.ini");
    return path;
}

std::wstring DragPath() {  // separate file: the build overwrites FortsModule.ini in the output folder
    std::wstring p = IniPath();
    const std::size_t slash = p.find_last_of(L'\\');
    return slash == std::wstring::npos ? p : p.substr(0, slash + 1) + L"FortsDrag.ini";
}

void LoadSettings() {
    const std::wstring path = IniPath();
    if (path.empty()) return;
    wchar_t buf[64]{};
    GetPrivateProfileStringW(L"Forts", L"GravityMagnitude", L"", buf, 64, path.c_str());
    if (buf[0]) {
        const float g = std::fabs(static_cast<float>(_wtof(buf)));
        if (std::isfinite(g) && g > 0.0f) g_settings.gravityMagnitude = g;
    }
    // Drag values learned in earlier sessions: "type=value" lines, counted as 8 samples so they adapt quickly.
    std::vector<wchar_t> section(8192);
    GetPrivateProfileSectionW(L"Drag", section.data(), static_cast<DWORD>(section.size()), DragPath().c_str());
    for (const wchar_t* p = section.data(); *p; p += wcslen(p) + 1) {
        const wchar_t* eq = wcschr(p, L'=');
        if (!eq) continue;
        const std::wstring name(p, eq);
        const double k = _wtof(eq + 1);
        if (!(k >= 0.0 && k < 0.5)) continue;
        DragStat st;
        st.n = 8;
        st.sum = k * st.n;
        st.savedAtN = st.n;
        std::string key;
        for (const wchar_t c : name) key.push_back(static_cast<char>(c));
        g_drag[key] = st;
    }
    Logf("settings: GravityMagnitude=%.1f, learned drag for %zu weapon type(s)", g_settings.gravityMagnitude, g_drag.size());
}

void SaveDrag(const std::string& type, const DragStat& st) {
    const std::wstring path = DragPath();
    if (path.empty() || st.n <= 0) return;
    wchar_t value[32];
    swprintf_s(value, L"%.4f", st.sum / st.n);
    WritePrivateProfileStringW(L"Drag", std::wstring(type.begin(), type.end()).c_str(), value, path.c_str());
}

float DragFor(const std::string& type) {
    const auto it = g_drag.find(type);
    if (it != g_drag.end() && it->second.n >= 4) return static_cast<float>(it->second.sum / it->second.n);
    return cfg::kDefaultDrag;
}

bool Calibrate(int w, int h, Calibration& c) {
    Vec3 focus, center, below;
    if (!g_api.CameraFocus(focus) || !g_api.ToScreen(focus, center)) return false;
    if (!g_api.ToScreen({focus.x, focus.y + 1000.0f, focus.z}, below)) return false;
    if (center.x < 1.0f || center.y < 1.0f) return false;
    const float kx = (static_cast<float>(w) * 0.5f) / center.x;
    const float ky = (static_cast<float>(h) * 0.5f) / center.y;
    if (std::fabs(kx - ky) > 0.02f * kx) return false;  // focus is not at the viewport centre
    const float dy = below.y - center.y;
    if (std::fabs(dy) < 1.0f) return false;
    c.scale = kx;
    // Screen Y is assumed to grow downward. If +world Y moves down the screen, gravity points to +Y.
    c.gravitySign = dy > 0.0f ? 1.0f : -1.0f;
    return true;
}

bool GameWindowFocused() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

bool Pressed(int vk) {
    static bool prev[256]{};
    const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool edge = down && !prev[vk];
    prev[vk] = down;
    return edge;
}

double NowSeconds() {
    static LARGE_INTEGER freq{};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart) / static_cast<double>(freq.QuadPart);
}

// ---------------------------------------------------------------- feature switches
bool RefusedOnline(bool on, const char* name) {
    if (!on || !g_online) return false;
    Logf("%s refused: it would desync a network game", name);
    return true;
}

void SetUnlimited(bool on) {
    g_state.unlimited = on;
    g_state.framesSinceUnlimited = 1000;  // apply on this frame
    Logf("unlimited resources: %d", on ? 1 : 0);
}

void SetFastBeams(bool on) {
    if (RefusedOnline(on, "fast build (beams)")) return;
    g_state.fastBeams = on;
    g_state.framesSinceBeams = 1000;
    Logf("fast build (beams): %d", on ? 1 : 0);
}

void SetFastDevices(bool on) {
    g_state.fastDevices = on;
    g_state.framesSinceBuild = 1000;
    Logf("fast build (tech and guns): %d", on ? 1 : 0);
}

void SetFastReload(bool on) {
    g_state.fastReload = on;
    g_state.framesSinceReload = 1000;
    Logf("minigun (fast reload): %d", on ? 1 : 0);
}

void SetCommander(bool on) {
    g_state.commander = on;
    g_state.framesSinceCommander = 1000;
    Logf("commander auto-refill: %d", on ? 1 : 0);
}

void SetRepair(bool on) {
    g_state.repair = on;
    g_state.framesSinceRepair = 1000;
    Logf("auto-repair: %d", on ? 1 : 0);
}

void SetTrajectory(bool on) {
    g_state.trajectory = on;
    Logf("trajectory overlay: %d", on ? 1 : 0);
}

void SetImpact(bool on) {
    g_state.impact = on;
    Logf("impact markers: %d", on ? 1 : 0);
}

void SetAutoAim(bool on) {
    if (RefusedOnline(on, "auto-aim")) return;
    g_state.autoAim = on;
    Logf("auto-aim: %d (target %d)", on ? 1 : 0, g_state.aimTarget);
}

void SetEnemyOverlay(bool on) {
    g_state.enemyOverlay = on;
    Logf("enemy overlay: %d", on ? 1 : 0);
}

void SetRadar(bool on) {
    g_state.radar = on;
    Logf("enemy radar: %d", on ? 1 : 0);
}

void SetIntel(bool on) {
    g_state.intel = on;
    Logf("enemy intel: %d", on ? 1 : 0);
}

void SetRecord(bool on) {
    g_state.record = on;
    Logf("record: %d", on ? 1 : 0);
}

void AllOff() {
    g_state.trajectory = false;
    g_state.record = false;
    g_state.impact = false;
    g_state.autoAim = false;
    g_state.enemyOverlay = false;
    g_state.radar = false;
    g_state.intel = false;
    if (g_state.unlimited) SetUnlimited(false);
    if (g_state.fastBeams) SetFastBeams(false);
    if (g_state.fastDevices) SetFastDevices(false);
    if (g_state.fastReload) SetFastReload(false);
    if (g_state.commander) SetCommander(false);
    if (g_state.repair) SetRepair(false);
}

// A network game started: drop everything that writes simulation state without calling back into the game. The
// team overrides belong to the previous world; material times are restored by ApplyFastBeams on the next match frame.
void UpdateOnline(bool online) {
    if (online == g_online) return;
    g_online = online;
    Logf("network game: %d", online ? 1 : 0);
    if (!online) return;
    g_state.unlimited = g_state.fastBeams = g_state.fastDevices = g_state.fastReload = false;
    g_state.commander = g_state.repair = g_state.autoAim = false;
    g_state.framesSinceUnlimited = 0;
    g_state.buildApplied = g_state.buildSaved = false;
    g_state.reloadApplied = g_state.reloadSaved = false;
}

// Online these switches run in the AeroHub mod: its AeroCommand has no input of its own, so nothing happens without
// this module. The event reaches every client on the same frame; a client without the function ignores it.
struct ModSwitch {
    int feature;  // matches the feature numbers in mods/aerohub/script.lua
    bool State::*on;
};
constexpr int kModSwitchCount = 5;
constexpr ModSwitch kModSwitches[kModSwitchCount] = {
    {1, &State::unlimited}, {2, &State::fastDevices}, {3, &State::fastReload}, {4, &State::commander}, {5, &State::repair}};
constexpr int kModResendFrames = 300;  // a restarted match starts with empty mod state
bool g_modSent[kModSwitchCount] = {};
int g_modFrames = 0;

int g_themeFrames = kModResendFrames;
int g_syncTeam = -1;  // GetLocalTeamId is still the side (1) until the match gives the player its team code

// The multiplayer flag reads 0 for about two seconds while a network match loads, so a 0 only counts as offline after
// it has held for a while. Until then nothing may write simulation state directly.
constexpr int kOfflineSettleFrames = 300;
int g_offlineFrames = 0;

void ResetModSwitches() {
    for (bool& sent : g_modSent) sent = false;
    g_modFrames = 0;
    g_themeFrames = kModResendFrames;
    g_offlineFrames = 0;
}

// Blue HUD panel for this player only; the game rebuilds its screens, so the colour is sent again now and then.
void SyncTheme(int team) {
    if (++g_themeFrames < kModResendFrames) return;
    g_themeFrames = 0;
    char params[16];
    std::snprintf(params, sizeof(params), "%d", team);
    if (!g_api.SendScriptEvent("AeroTheme", params)) Logf("SendScriptEvent faulted (theme)");
    static int loggedTeam = -1;
    if (team != loggedTeam) Logf("theme sent for team %d", team);
    loggedTeam = team;
}

void SyncMod(int team) {
    const int side = team % 100;
    if (side != 1 && side != 2) return;  // observers have no team
    const bool resend = ++g_modFrames >= kModResendFrames;
    if (resend) g_modFrames = 0;
    for (int i = 0; i < kModSwitchCount; ++i) {
        const bool want = g_state.*kModSwitches[i].on;
        if (want == g_modSent[i] && !(want && resend)) continue;
        char params[32];
        std::snprintf(params, sizeof(params), "%d, %d, %s", team, kModSwitches[i].feature, want ? "true" : "false");
        if (!g_api.SendScriptEvent("AeroCommand", params)) {
            Logf("SendScriptEvent faulted (%s)", params);
            continue;
        }
        if (want != g_modSent[i]) Logf("mod command sent: %s", params);
        g_modSent[i] = want;
    }
}

// ---------------------------------------------------------------- build, reload, commander, repair
// Link (strut) build times live in the material records, not in the per-team override. While Fast build
// (beams) is on, every material's BuildTime is multiplied by kFastMaterialFactor; originals are kept and
// written back by name-checked writes when it goes off, and re-scaled if the game reloads its data.
void ApplyFastMaterials() {
    std::vector<MaterialBuild> current;
    if (!g_api.MaterialBuildTimes(current)) {
        if (!g_state.warnedMaterials) {
            g_state.warnedMaterials = true;
            Logf("fast build: material records not readable; structures keep their normal build time");
        }
        return;
    }
    for (const MaterialBuild& m : current) {
        SavedMaterial* saved = nullptr;
        for (SavedMaterial& s : g_materials)
            if (s.side == m.side && s.index == m.index && s.name == m.name) saved = &s;
        if (!saved) {
            g_materials.push_back({m.side, m.index, m.name, m.buildTime, m.buildTime * cfg::kFastMaterialFactor});
            saved = &g_materials.back();
        } else if (std::fabs(m.buildTime - saved->applied) < 1e-4f) {
            continue;  // still our value
        } else {
            saved->original = m.buildTime;  // the game reset it (new match) or something else changed it
            saved->applied = m.buildTime * cfg::kFastMaterialFactor;
        }
        if (!g_state.loggedMaterials)
            Logf("fast build: material side=%d index=%d '%s' buildTime %.3f -> %.3f", m.side, m.index, m.name.c_str(), saved->original,
                 saved->applied);
        g_api.SetMaterialBuildTime(m.side, m.index, m.name, saved->applied);
    }
    g_state.loggedMaterials = true;
}

void RestoreMaterials() {
    int restored = 0;
    for (const SavedMaterial& s : g_materials)
        if (g_api.SetMaterialBuildTime(s.side, s.index, s.name, s.original)) ++restored;
    Logf("fast build: restored %d of %zu material(s)", restored, g_materials.size());
    g_materials.clear();
    g_state.loggedMaterials = false;
}

void ApplyFastBeams() {
    if (g_state.fastBeams) {
        if (g_state.framesSinceBeams < 60) {
            ++g_state.framesSinceBeams;
            return;
        }
        g_state.framesSinceBeams = 0;
        ApplyFastMaterials();
        g_state.beamsApplied = true;
    } else if (g_state.beamsApplied) {
        g_state.beamsApplied = false;
        RestoreMaterials();
    }
}

// Fast build, tech and guns: SetOverrideBuildTime(team, seconds, duration). The original values are read first and
// restored exactly when the switch goes off.
void ApplyFastDevices(int team) {
    if (g_state.fastDevices) {
        if (g_state.framesSinceBuild < 60) {
            ++g_state.framesSinceBuild;
            return;
        }
        g_state.framesSinceBuild = 0;
        if (!g_state.buildSaved) {
            float a = 0, b = 0;
            if (!g_api.ReadBuildOverride(team, a, b)) {
                Logf("fast build (tech and guns) refused: cannot read the original build override of team %d", team);
                g_state.fastDevices = false;
                return;
            }
            g_state.buildA = a;
            g_state.buildB = b;
            g_state.buildSaved = true;
            Logf("fast build (tech and guns): original override a=%.4f b=%.4f, applying %.3f", a, b, cfg::kFastBuildValue);
        }
        if (!g_api.SetBuildOverride(team, cfg::kFastBuildValue, cfg::kOverrideNoExpiry)) Logf("SetOverrideBuildTime faulted");
        g_state.buildApplied = true;
    } else if (g_state.buildApplied) {
        if (g_state.buildSaved) g_api.SetBuildOverride(team, g_state.buildA, g_state.buildB);
        Logf("fast build (tech and guns) restored: a=%.4f b=%.4f", g_state.buildA, g_state.buildB);
        g_state.buildApplied = false;
    }
}

// Minigun: the per-team reload override, same scheme (originals saved, restored exactly).
void ApplyFastReload(int team) {
    if (g_state.fastReload) {
        if (g_state.framesSinceReload < 60) {
            ++g_state.framesSinceReload;
            return;
        }
        g_state.framesSinceReload = 0;
        if (!g_state.reloadSaved) {
            float a = 0, b = 0;
            if (!g_api.ReadReloadOverride(team, a, b)) {
                Logf("minigun refused: cannot read the original reload override of team %d", team);
                g_state.fastReload = false;
                return;
            }
            g_state.reloadA = a;
            g_state.reloadB = b;
            g_state.reloadSaved = true;
            Logf("minigun: original override a=%.4f b=%.4f, applying %.3f", a, b, cfg::kFastReloadValue);
        }
        if (!g_api.SetReloadOverride(team, cfg::kFastReloadValue, cfg::kOverrideNoExpiry)) Logf("SetOverrideReloadTime faulted");
        g_state.reloadApplied = true;
    } else if (g_state.reloadApplied) {
        if (g_state.reloadSaved) g_api.SetReloadOverride(team, g_state.reloadA, g_state.reloadB);
        Logf("minigun restored: a=%.4f b=%.4f", g_state.reloadA, g_state.reloadB);
        g_state.reloadApplied = false;
    }
}

// Commander gauge is ready at 1.0. Top it up only when the ability is NOT running and with a cooldown:
// topping it up every few frames while the ability drains it re-triggers the "gauge filled" sound in a loop.
void ApplyCommander(int team) {
    if (!g_state.commander) return;
    if (g_state.commanderCooldown > 0) {
        --g_state.commanderCooldown;
        return;
    }
    if (g_state.framesSinceCommander < 12) {
        ++g_state.framesSinceCommander;
        return;
    }
    g_state.framesSinceCommander = 0;
    bool active = false;
    if (!g_api.CommanderActive(team, active) || active) return;
    float points = 0;
    if (!g_api.CommanderPoints(team, points)) return;
    static bool logged = false;
    if (!logged) {
        logged = true;
        Logf("commander gauge reads %.3f (full = 1.0)", points);
    }
    if (points >= 0.0f && points < 0.95f) {
        g_api.AddCommanderPoints(team, 1.0f - points);
        g_state.commanderCooldown = 180;  // about three seconds before the next top-up
    }
}

// Auto-repair: about five times a second, restore a share of the maximum health of every damaged own device
// and/or beam (finished links only). The share per second is the "Repair speed" setting; the game's own
// repair (cost, animation) is bypassed because health is written directly.
double g_repairLast = 0;
bool g_repairLogged = false;

void ApplyRepair(int team) {
    if (!g_state.repair) {
        g_repairLast = 0;
        return;
    }
    const double now = NowSeconds();
    if (g_repairLast == 0) {
        g_repairLast = now;
        return;
    }
    const double dt = now - g_repairLast;
    if (dt < 0.2) return;
    g_repairLast = now;
    const float rate = kRepairRates[std::clamp(g_state.repairLevel, 0, 3)];
    const float fraction = std::min(1.0f, static_cast<float>(rate * std::min(dt, 1.0)));
    HealResult r;
    const bool devices = g_state.repairWhat != 2, links = g_state.repairWhat != 1;
    if (!g_api.HealTeam(team, fraction, devices, links, r)) return;
    if (!g_repairLogged && (r.devices || r.links)) {
        g_repairLogged = true;
        Logf("auto-repair: healed %d device(s) and %d link(s) (%d finished links examined), %.1f%% of max per pass", r.devices,
             r.links, r.linksSeen, fraction * 100.0f);
    }
}
// ---------------------------------------------------------------- weapon types, gravity, learned drag
void RefreshTypes(int team) {
    static int frame = 0;
    if (frame++ % 10 != 0) return;
    std::vector<int> ids;
    if (!g_api.OwnWeapons(team, ids)) return;
    std::map<std::string, int> counts;
    for (int id : ids) {
        std::string t;
        if (g_api.DeviceTypeName(id, t)) ++counts[t];
    }
    g_typeCount.swap(counts);
}

// |gravity| of the projectile this weapon type fires. Resolved once per type through the game's own
// type tables; falls back to Physics.Gravity when the chain does not give a plausible number.
float TypeGravity(int weaponId, int team, const std::string& type) {
    const auto it = g_typeGravity.find(type);
    if (it != g_typeGravity.end()) return it->second;
    float magnitude = g_settings.gravityMagnitude;
    std::string projectile;
    float g = 0;
    const bool chain = g_api.WeaponProjectile(weaponId, team, projectile, g);
    if (chain && std::fabs(g) < 0.001f) {  // beams and lasers: no gravity at all, a straight line
        magnitude = 0.0f;
        Logf("gravity: weapon type '%s' fires '%s' without gravity (straight beam)", type.c_str(), projectile.c_str());
    } else if (chain && std::fabs(g) >= 10.0f && std::fabs(g) <= 5000.0f) {
        magnitude = std::fabs(g);
        Logf("gravity: weapon type '%s' fires '%s' with gravity %.1f", type.c_str(), projectile.c_str(), g);
    } else {
        Logf("gravity: chain failed for '%s' (projectile '%s', value %.1f); using %.1f", type.c_str(), projectile.c_str(), g, magnitude);
    }
    g_typeGravity[type] = magnitude;
    return magnitude;
}

// Learns the air drag of every weapon type from live shots: a new projectile is matched to the weapon whose
// aim (direction and speed) it left, then its horizontal speed decay between frames gives k = -ln(vx1/vx0)/dt.
struct FireSnapshot {
    std::string type;
    float vx = 0, vy = 0;
};
struct ProjTrack {
    std::string type;
    double t = 0;
    float vx = 0, vy = 0;
};
std::map<int, FireSnapshot> g_prevFire;
std::map<int, ProjTrack> g_tracks;

std::string MatchWeaponType(float vx, float vy) {
    const float speed = std::sqrt(vx * vx + vy * vy);
    const float ang = std::atan2(vy, vx);
    float best = 0.09f;  // 5 degrees
    std::string type;
    for (const auto& [id, f] : g_prevFire) {
        const float ws = std::sqrt(f.vx * f.vx + f.vy * f.vy);
        if (ws < 1.0f || std::fabs(speed / ws - 1.0f) > 0.08f) continue;
        const float d = std::fabs(ang - std::atan2(f.vy, f.vx));
        if (d < best) {
            best = d;
            type = f.type;
        }
    }
    return type;
}

void LearnPhysics(int team, const std::vector<int>& weapons) {
    std::map<int, FireSnapshot> cur;
    for (int id : weapons) {
        Vec3 v;
        std::string t;
        if (!g_api.FireVelocity(id, v) || !g_api.DeviceTypeName(id, t)) continue;
        cur[id] = {t, v.x, v.y};
    }
    const double now = NowSeconds();
    std::vector<int> ids;
    std::set<int> alive;
    if (g_api.ProjectileIds(team, ids)) {
        for (int id : ids) {
            Vec3 v;
            if (!g_api.NodeVel(id, v)) continue;
            alive.insert(id);
            const auto it = g_tracks.find(id);
            if (it == g_tracks.end()) {
                g_tracks[id] = {MatchWeaponType(v.x, v.y), now, v.x, v.y};
                continue;
            }
            ProjTrack& tr = it->second;
            const double dt = now - tr.t;
            if (dt > 0.004 && dt < 0.2 && !tr.type.empty() && std::fabs(tr.vx) > 800.0f && tr.vx * v.x > 0.0f) {
                const double ratio = static_cast<double>(v.x) / static_cast<double>(tr.vx);
                if (ratio > 0.5 && ratio < 1.0001) {
                    const double k = -std::log(ratio) / dt;
                    if (k >= 0.0 && k < 0.5) {
                        DragStat& st = g_drag[tr.type];
                        st.sum += k;
                        ++st.n;
                        if (st.n >= 5 && st.n - st.savedAtN >= 200) {
                            st.savedAtN = st.n;
                            SaveDrag(tr.type, st);
                            Logf("drag: weapon type '%s' k=%.4f from %d samples", tr.type.c_str(), st.sum / st.n, st.n);
                        }
                    }
                }
            }
            tr.t = now;
            tr.vx = v.x;
            tr.vy = v.y;
        }
    }
    for (auto it = g_tracks.begin(); it != g_tracks.end();) it = alive.count(it->first) ? std::next(it) : g_tracks.erase(it);
    g_prevFire.swap(cur);
}

// ---------------------------------------------------------------- logging helpers
// 10 Hz samples of what the game reports per weapon and what live projectiles actually do.
void RecordSamples(int team) {
    static ULONGLONG last = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - last < 100) return;
    last = now;

    float aimAngle = 0, aimSpeed = 0;
    if (g_api.AimInput(aimAngle, aimSpeed)) Logf("rec t=%llu aimInput angle=%.4f speed=%.1f", now, aimAngle, aimSpeed);

    std::vector<int> ids;
    if (g_api.OwnWeapons(team, ids)) {
        for (int id : ids) {
            Vec3 v;
            if (!g_api.FireVelocity(id, v)) continue;
            Logf("rec t=%llu wpn id=%d fireVel=(%.1f, %.1f) speed=%.1f angleDeg=%.2f", now, id, v.x, v.y,
                 std::sqrt(v.x * v.x + v.y * v.y), std::atan2(v.y, v.x) * 57.29578f);
        }
    }
    int logged = 0;
    for (int t = 1; t <= 4 && logged < 40; ++t) {
        std::vector<int> proj;
        if (!g_api.ProjectileIds(t, proj)) continue;
        for (int id : proj) {
            Vec3 p, v;
            float grav = 0;
            if (!g_api.NodePos(id, p) || !g_api.NodeVel(id, v)) continue;
            g_api.ProjectileGravity(id, grav);
            Logf("rec t=%llu proj team=%d id=%d pos=(%.1f, %.1f) vel=(%.1f, %.1f) gravity=%.1f", now, t, id, p.x, p.y,
                 v.x, v.y, grav);
            if (++logged >= 40) break;
        }
    }
}

void Diagnostics(int team) {
    TeamResources r;
    if (g_api.Resources(team, r))
        Logf("diag: localTeam=%d resources slot(+0x58)=%.1f slot(+0x50)=%.1f third=%.1f", team, r.slotFromOff58,
             r.slotFromOff50, r.third);
    float cp = 0;
    if (g_api.CommanderPoints(team, cp)) Logf("diag: commander gauge=%.3f", cp);
    float a = 0, b = 0;
    if (g_api.ReadBuildOverride(team, a, b)) Logf("diag: build override a=%.4f b=%.4f", a, b);
    if (g_api.ReadReloadOverride(team, a, b)) Logf("diag: reload override a=%.4f b=%.4f", a, b);
    bool commanderActive = false;
    if (g_api.CommanderActive(team, commanderActive)) Logf("diag: commander active=%d", commanderActive ? 1 : 0);
    std::vector<MaterialBuild> materials;
    if (g_api.MaterialBuildTimes(materials))
        for (const MaterialBuild& m : materials) Logf("diag: material side=%d index=%d name=%s buildTime=%.3f", m.side, m.index, m.name.c_str(), m.buildTime);
    else
        Logf("diag: material records not readable");
    for (int t = 1; t <= 4; ++t) {
        std::vector<int> devs;
        if (g_api.DeviceIds(t, devs)) Logf("diag: GetDeviceCount(%d) = %zu", t, devs.size());
    }
    std::vector<RawDevice> raw;
    if (g_api.RawDevices(raw)) {
        std::map<std::pair<int, int>, int> groups;  // (team code, state) -> count
        for (const RawDevice& d : raw) ++groups[{d.teamCode, d.state}];
        Logf("diag: raw device list has %zu device(s)", raw.size());
        for (const auto& [key, n] : groups) Logf("diag: raw devices teamCode=%d state=%d count=%d", key.first, key.second, n);
    } else {
        Logf("diag: raw device list not readable");
    }
    for (const auto& [type, st] : g_drag) Logf("diag: drag '%s' k=%.4f (%d samples)", type.c_str(), st.n ? st.sum / st.n : 0.0, st.n);
    std::vector<int> ids;
    if (!g_api.OwnWeapons(team, ids)) {
        Logf("diag: OwnWeapons failed");
        return;
    }
    Logf("diag: %zu own weapon(s), fallback gravity=%.1f", ids.size(), g_settings.gravityMagnitude);
    for (std::size_t i = 0; i < ids.size() && i < 8; ++i) {
        Vec3 p, v, s;
        const bool okP = g_api.BarrelPosition(ids[i], p), okV = g_api.FireVelocity(ids[i], v);
        const bool okS = okP && g_api.ToScreen(p, s);
        const float speed = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        const float angle = std::atan2(v.y, v.x) * 57.29578f;
        std::string type = "?", proj = "?";
        float g = 0;
        g_api.DeviceTypeName(ids[i], type);
        const bool okG = g_api.WeaponProjectile(ids[i], team, proj, g);
        Logf("diag: weapon id=%d type=%s projectile=%s gravity(%d)=%.1f drag=%.4f speed=%.1f angleDeg=%.2f barrel(%d)=(%.1f, %.1f) fireVel(%d)=(%.1f, %.1f) screen(%d)=(%.2f, %.2f)",
             ids[i], type.c_str(), proj.c_str(), okG, g, DragFor(type), speed, angle, okP, p.x, p.y, okV, v.x, v.y, okS, s.x, s.y);
    }
}

// ---------------------------------------------------------------- the other teams' devices
void RefreshWorld(int team, bool wantRadar) {
    static int frame = 0;
    if (frame++ % 20 != 0) return;
    g_enemy.clear();
    int deviceCount = 0;
    // Devices come straight from the game's own list. Ours carry the local team code; every other device whose
    // code is a player side (1 or 2 modulo 100) belongs to an opponent. Devices in state 5 are ignored, as the
    // game's own handlers do.
    std::vector<RawDevice> raw;
    if (!g_api.RawDevices(raw)) return;
    std::map<int, std::set<int>> weaponsByCode;  // team code -> weapon device ids
    for (const RawDevice& d : raw) {
        if (d.state == 5 || weaponsByCode.count(d.teamCode)) continue;
        std::vector<int> weapons;
        g_api.OwnWeapons(d.teamCode, weapons);  // GetWeaponCount/GetWeaponId work for any team code
        weaponsByCode[d.teamCode] = std::set<int>(weapons.begin(), weapons.end());
    }
    static bool logged = false;
    for (const RawDevice& d : raw) {
        if (d.state == 5 || deviceCount >= 1500) continue;
        const bool own = d.teamCode == team;
        const int side = ((d.teamCode % 100) + 100) % 100;
        if (!own && side != 1 && side != 2) continue;  // neutral / map devices
        Vec3 p;
        if (!g_api.DevicePosition(d.id, p)) continue;
        const bool isWeapon = weaponsByCode[d.teamCode].count(d.id) != 0;
        EnemyDev e;
        e.id = d.id;
        e.pos = p;
        e.weapon = isWeapon;
        if (!own) g_api.DeviceTypeName(d.id, e.type);
        ++deviceCount;
        if (own) continue;
        g_enemy.push_back(std::move(e));
    }
    // Spy-style intel on the opposing team (what the Spy commander shows): resources, commander gauge, reactor,
    // the newest device and the size of the base. The enemy team is the most common code among its devices.
    {
        std::map<int, int> perCode;
        for (const RawDevice& d : raw) {
            const int side = ((d.teamCode % 100) + 100) % 100;
            if (d.state != 5 && d.teamCode != team && (side == 1 || side == 2)) ++perCode[d.teamCode];
        }
        g_enemyTeam = 0;
        int best = 0;
        for (const auto& [code, n] : perCode)
            if (n > best) {
                best = n;
                g_enemyTeam = code;
            }
        if (g_enemyTeam != 0 && g_state.intel) {
            std::vector<PanelLine> lines;
            TeamResources res;
            auto fmt = [](float v) {
                wchar_t b[32];
                if (v > 1.0e9f) swprintf_s(b, L"unlimited");
                else swprintf_s(b, L"%.0f", v);
                return std::wstring(b);
            };
            if (g_api.Resources(g_enemyTeam, res)) {
                lines.push_back({L"Resource A", fmt(res.slotFromOff58), 0});
                lines.push_back({L"Resource B", fmt(res.slotFromOff50), 0});
            }
            float gauge = 0;
            if (g_api.CommanderPoints(g_enemyTeam, gauge)) {
                wchar_t b[32];
                swprintf_s(b, L"%.0f%%", gauge * 100.0f);
                lines.push_back({L"Commander gauge", b, gauge >= 0.999f ? aero::ui::kWarn : 0});
            }
            int guns = 0, lastId = 0;
            const EnemyDev* reactor = nullptr;
            const EnemyDev* last = nullptr;
            for (const EnemyDev& e : g_enemy) {
                guns += e.weapon ? 1 : 0;
                if (e.type == "reactor") reactor = &e;
                if (e.id > lastId) {
                    lastId = e.id;
                    last = &e;
                }
            }
            if (reactor) {
                float hp = 0, maxHp = 0;
                wchar_t b[32] = L"present";
                if (g_api.DeviceHealth(reactor->id, hp, maxHp)) swprintf_s(b, L"%.0f%%", 100.0f * hp / maxHp);
                lines.push_back({L"Reactor", b, aero::ui::kWarn});
            } else {
                lines.push_back({L"Reactor", L"not seen", 0});
            }
            lines.push_back({L"Last built", last && !last->type.empty() ? std::wstring(last->type.begin(), last->type.end()) : std::wstring(L"-"), 0});
            lines.push_back({L"Devices / guns", std::to_wstring(g_enemy.size()) + L" / " + std::to_wstring(guns), 0});
            RenderPanel(L"Enemy intel", lines, g_uiScale, g_intelImage);
            g_intelFresh = true;
        }
    }    if (!logged && !g_enemy.empty()) {
        logged = true;
        std::set<int> codes;
        for (const RawDevice& d : raw)
            if (d.teamCode != team) codes.insert(d.teamCode);
        std::string list;
        for (int c : codes) list += std::to_string(c) + " ";
        Logf("world: local team code %d, other team codes seen: %s; %zu enemy device(s)", team, list.c_str(), g_enemy.size());
    }    Vec3 cam;
    const bool hasCam = g_api.CameraFocus(cam);
    // Target list for auto-aim: enemy weapons nearest to the camera focus.
    std::vector<std::pair<float, int>> byDistance;
    for (const EnemyDev& e : g_enemy) {
        if (!e.weapon) continue;
        const float dx = hasCam ? e.pos.x - cam.x : 0.0f, dy = hasCam ? e.pos.y - cam.y : 0.0f;
        byDistance.push_back({dx * dx + dy * dy, e.id});
    }
    std::sort(byDistance.begin(), byDistance.end());
    g_targets.clear();
    for (const auto& [d, id] : byDistance) {
        if (static_cast<int>(g_targets.size()) >= kMaxTargetRows) break;
        g_targets.push_back(id);
    }
    if (g_state.aimTarget != 0 && std::none_of(g_enemy.begin(), g_enemy.end(), [](const EnemyDev& e) { return e.id == g_state.aimTarget; })) {
        g_state.aimTarget = 0;  // target destroyed
        Logf("auto-aim: target gone");
    }
    if (wantRadar && g_enemyTeam != 0) {
        std::vector<ViewSegment> segments;
        std::vector<BaseSegment> beams;
        if (g_api.BaseSegments(g_enemyTeam, beams))
            for (const BaseSegment& s : beams) segments.push_back({s.x1, s.y1, s.x2, s.y2, s.health, s.building});
        std::vector<ViewDevice> devices;
        for (const EnemyDev& e : g_enemy) devices.push_back({e.pos.x, e.pos.y, e.type == "reactor" ? 2 : (e.weapon ? 1 : 0), e.type});
        static bool loggedView = false;
        if (!loggedView) {
            loggedView = true;
            Logf("observer view: %zu beam(s) and %zu device(s) of team code %d", segments.size(), devices.size(), g_enemyTeam);
        }
        RenderBaseView(segments, devices, g_uiScale, g_radarImage);
        g_radarFresh = true;
    }
}

const EnemyDev* FindEnemy(int id) {
    for (const EnemyDev& e : g_enemy)
        if (e.id == id) return &e;
    return nullptr;
}

// Auto-aim: the game's own aim update (UpdateWeaponAim) towards the chosen enemy device, for the guns whose
// type is switched on in the Combat tab.
void ApplyAutoAim(int team) {
    if (!g_state.autoAim || g_state.aimTarget == 0) return;
    const EnemyDev* target = FindEnemy(g_state.aimTarget);
    if (!target) return;
    Vec3 pos;
    if (!g_api.DevicePosition(target->id, pos)) return;
    std::vector<int> ids;
    if (!g_api.OwnWeapons(team, ids)) return;
    for (int id : ids) {
        std::string type;
        if (g_api.DeviceTypeName(id, type) && !ShowType(type)) continue;
        g_api.AimWeaponAt(id, pos);
    }
}

// ---------------------------------------------------------------- drawing: trajectories, enemies, radar
Calibration g_cal;
bool g_calOk = false;

bool PrepareCalibration(int w, int h) {
    g_calOk = Calibrate(w, h, g_cal);
    if (!g_calOk) {
        if (!g_state.warnedCalibration) {
            g_state.warnedCalibration = true;
            Logf("overlay: camera calibration failed (focus not at viewport centre?)");
        }
        return false;
    }
    if (!g_state.loggedCalibration) {
        g_state.loggedCalibration = true;
        Logf("overlay: calibration scale=%.4f gravitySign=%.0f viewport=%dx%d", g_cal.scale, g_cal.gravitySign, w, h);
    }
    return true;
}

bool WorldToPixels(const Vec3& world, float& x, float& y) {
    Vec3 s;
    if (!g_api.ToScreen(world, s)) return false;
    x = s.x * g_cal.scale;
    y = s.y * g_cal.scale;
    return true;
}

void DrawTrajectories(int team) {
    std::vector<int> ids;
    if (!g_api.OwnWeapons(team, ids) || ids.empty()) return;
    constexpr int kCap = 128;
    Vec2f pts[kCap];
    float line[2 * kCap];
    for (int id : ids) {
        std::string type = "?";
        g_api.DeviceTypeName(id, type);
        if (!ShowType(type)) continue;
        Vec3 p, v;
        if (!g_api.BarrelPosition(id, p) || !g_api.FireVelocity(id, v)) continue;
        const float gravityMag = TypeGravity(id, team, type);
        const bool beam = gravityMag == 0.0f;
        const float gravityY = g_cal.gravitySign * gravityMag;
        // Beams are drawn as a straight ray for one second of beam travel; shells until they fall well below the gun.
        const int n = beam ? PredictTrajectory({p.x, p.y}, {v.x, v.y}, 0.0f, 0.0f, 0.05f, 1.0f, 1.0e9f, pts, kCap)
                           : PredictTrajectory({p.x, p.y}, {v.x, v.y}, gravityY, DragFor(type), 0.08f, 10.0f, 1500.0f, pts, kCap);
        int m = 0;
        for (int i = 0; i < n; ++i) {
            float x, y;
            if (!WorldToPixels({pts[i].x, pts[i].y, p.z}, x, y)) continue;
            line[2 * m] = x;
            line[2 * m + 1] = y;
            ++m;
        }
        if (beam) g_overlay.Line(line, m, 0.4f, 0.9f, 1.0f, 0.9f, 2.0f);
        else g_overlay.Line(line, m, 1.0f, 0.85f, 0.1f, 0.9f, 2.0f);
        if (g_state.impact) {
            for (const EnemyDev& e : g_enemy)
                if (DistanceToPolyline({e.pos.x, e.pos.y}, pts, n) < 70.0f) g_hitIds.insert(e.id);
        }
    }
}

void Ring(float cx, float cy, float radius, float r, float g, float b, float a, float width) {
    float pts[2 * 25];
    for (int i = 0; i <= 24; ++i) {
        const float t = static_cast<float>(i) * 6.2831853f / 24.0f;
        pts[2 * i] = cx + std::cos(t) * radius;
        pts[2 * i + 1] = cy + std::sin(t) * radius;
    }
    g_overlay.Line(pts, 25, r, g, b, a, width);
}

void DrawEnemies(int vw, int vh) {
    const float s = g_uiScale;
    for (const EnemyDev& e : g_enemy) {
        float x, y;
        if (!WorldToPixels(e.pos, x, y)) continue;
        if (x < -80 || y < -80 || x > static_cast<float>(vw) + 80 || y > static_cast<float>(vh) + 80) continue;
        const bool hit = g_hitIds.count(e.id) != 0;
        const bool target = e.id == g_state.aimTarget;
        const bool reactor = e.type == "reactor";
        if (!e.weapon && !reactor) {
            g_overlay.Rect(x - 3 * s, y - 3 * s, 6 * s, 6 * s, 0.97f, 0.44f, 0.44f, 0.55f);
            if (hit) Ring(x, y, 12 * s, 1.0f, 0.3f, 0.3f, 1.0f, 2.5f);
            continue;
        }
        const float half = 17 * s;
        const float line[2 * 5] = {x - half, y - half, x + half, y - half, x + half, y + half, x - half, y + half, x - half, y - half};
        if (reactor) g_overlay.Line(line, 5, 0.98f, 0.75f, 0.14f, 0.95f, 2.5f);
        else g_overlay.Line(line, 5, 0.97f, 0.44f, 0.44f, 0.95f, 2.0f);
        float hp = 0, maxHp = 0;
        if (g_api.DeviceHealth(e.id, hp, maxHp)) {  // health bar above the box
            const float ratio = std::clamp(hp / maxHp, 0.0f, 1.0f);
            g_overlay.Rect(x - half, y - half - 8 * s, 2 * half, 5 * s, 0.05f, 0.06f, 0.08f, 0.85f);
            g_overlay.Rect(x - half, y - half - 8 * s, 2 * half * ratio, 5 * s, ratio > 0.6f ? 0.2f : (ratio > 0.3f ? 0.98f : 0.97f),
                           ratio > 0.6f ? 0.83f : (ratio > 0.3f ? 0.75f : 0.35f), ratio > 0.6f ? 0.6f : 0.15f, 1.0f);
        }
        if (!e.type.empty()) {
            auto it = g_labels.find(e.type);
            if (it == g_labels.end()) {
                HudImage img;
                RenderLabel(std::wstring(e.type.begin(), e.type.end()), aero::ui::kBad, s, img);
                it = g_labels.emplace(e.type, std::move(img)).first;
            }
            g_overlay.Image(("lbl:" + e.type).c_str(), it->second.px.data(), it->second.w, it->second.h, false,
                            x - static_cast<float>(it->second.w) / 2, y + half + 4 * s);
        }
        if (hit) Ring(x, y, 26 * s, 1.0f, 0.3f, 0.3f, 1.0f, 3.0f);
        if (target) Ring(x, y, 30 * s, 0.38f, 0.65f, 0.98f, 1.0f, 3.0f);
    }
}

void DrawIntel(int vw) {
    if (g_intelImage.px.empty()) return;
    // Under the radar when it is on, otherwise in the radar's place.
    const float below = g_state.radar && !g_radarImage.px.empty() ? static_cast<float>(g_radarImage.h) + 12.0f * g_uiScale : 0.0f;
    g_overlay.Image("intel", g_intelImage.px.data(), g_intelImage.w, g_intelImage.h, g_intelFresh,
                    static_cast<float>(vw - g_intelImage.w) - 24.0f * g_uiScale, 100.0f * g_uiScale + below);
    g_intelFresh = false;
}

void DrawRadar(int vw) {
    if (g_radarImage.px.empty()) return;
    g_overlay.Image("radar", g_radarImage.px.data(), g_radarImage.w, g_radarImage.h, g_radarFresh,
                    static_cast<float>(vw - g_radarImage.w) - 24.0f * g_uiScale, 100.0f * g_uiScale);
    g_radarFresh = false;
}

// ---------------------------------------------------------------- menu
bool ItemOn(int item) {
    switch (item) {
        case kItemUnlimited: return g_state.unlimited;
        case kItemFastBeams: return g_state.fastBeams;
        case kItemFastDevices: return g_state.fastDevices;
        case kItemFastReload: return g_state.fastReload;
        case kItemCommander: return g_state.commander;
        case kItemRepair: return g_state.repair;
        case kItemTrajectory: return g_state.trajectory;
        case kItemImpact: return g_state.impact;
        case kItemAutoAim: return g_state.autoAim;
        case kItemEnemyOverlay: return g_state.enemyOverlay;
        case kItemRadar: return g_state.radar;
        case kItemIntel: return g_state.intel;
        case kItemRecord: return g_state.record;
        default: return false;
    }
}

void ActivateItem(int item) {
    switch (item) {
        case kItemUnlimited: SetUnlimited(!g_state.unlimited); break;
        case kItemFastBeams: SetFastBeams(!g_state.fastBeams); break;
        case kItemFastDevices: SetFastDevices(!g_state.fastDevices); break;
        case kItemFastReload: SetFastReload(!g_state.fastReload); break;
        case kItemCommander: SetCommander(!g_state.commander); break;
        case kItemRepair: SetRepair(!g_state.repair); break;
        case kItemRepairSpeed:
            g_state.repairLevel = (g_state.repairLevel + 1) % 4;
            Logf("repair speed: %ls", kRepairSpeedNames[g_state.repairLevel]);
            break;
        case kItemRepairWhat:
            g_state.repairWhat = (g_state.repairWhat + 1) % 3;
            Logf("repair what: %ls", kRepairWhatNames[g_state.repairWhat]);
            break;
        case kItemTrajectory: SetTrajectory(!g_state.trajectory); break;
        case kItemImpact: SetImpact(!g_state.impact); break;
        case kItemAutoAim: SetAutoAim(!g_state.autoAim); break;
        case kItemEnemyOverlay: SetEnemyOverlay(!g_state.enemyOverlay); break;
        case kItemRadar: SetRadar(!g_state.radar); break;
        case kItemIntel: SetIntel(!g_state.intel); break;
        case kItemRecord: SetRecord(!g_state.record); break;
        case kItemDiagnostics: g_state.wantDiagnostics = true; break;
        case kItemAllOff: AllOff(); break;
    }
}

void ActivateRowId(int id) {
    if (id >= 0 && id < kItemCount) {
        ActivateItem(id);
    } else if (id >= kRowIdType && id < kRowIdType + 100) {
        const std::size_t t = static_cast<std::size_t>(id - kRowIdType);
        if (t < g_typeList.size()) g_typeShow[g_typeList[t]] = !ShowType(g_typeList[t]);
    } else if (id >= kRowIdTarget && id < kRowIdTarget + 100) {
        const std::size_t t = static_cast<std::size_t>(id - kRowIdTarget);
        if (t < g_targets.size()) {
            g_state.aimTarget = g_state.aimTarget == g_targets[t] ? 0 : g_targets[t];
            Logf("auto-aim target: %d", g_state.aimTarget);
        }
    }
}

std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// Rows of the selected tab. Combat adds one switch per own weapon type, Enemy one row per aim target.
void BuildRows(std::vector<MenuRow>& rows) {
    rows.clear();
    for (int i = 0; i < kItemCount; ++i) {
        if (kItemTab[i] != g_state.menuTab) continue;
        MenuRow r;
        r.name = Widen(kItemNames[i]);
        r.toggle = kItemKinds[i] == aero::ipc::kToggle;
        r.on = ItemOn(i);
        r.id = i;
        if (g_online && (i == kItemFastBeams || i == kItemAutoAim)) {
            r.toggle = false;
            r.value = L"Offline only";
        } else if (g_online && (i == kItemRepairSpeed || i == kItemRepairWhat)) {
            r.toggle = false;
            r.value = i == kItemRepairSpeed ? L"Game speed" : L"Everything";
        } else if (i == kItemRepairSpeed) {
            r.toggle = false;
            r.value = kRepairSpeedNames[std::clamp(g_state.repairLevel, 0, 3)];
        } else if (i == kItemRepairWhat) {
            r.toggle = false;
            r.value = kRepairWhatNames[std::clamp(g_state.repairWhat, 0, 2)];
        }
        rows.push_back(std::move(r));
    }
    if (g_online && g_state.menuTab == 0) {
        MenuRow section;
        section.name = L"ONLINE: SWITCHES ABOVE NEED THE AEROHUB MOD IN THE LOBBY";
        section.section = true;
        rows.push_back(std::move(section));
    }
    g_typeList.clear();
    if (g_state.menuTab == 1 && !g_typeCount.empty()) {
        MenuRow section;
        section.name = L"GUNS SHOWN (TRAJECTORY AND AUTO-AIM)";
        section.section = true;
        rows.push_back(std::move(section));
        for (const auto& [name, count] : g_typeCount) {
            if (static_cast<int>(g_typeList.size()) >= kMaxTypeRows) break;
            MenuRow r;
            r.name = Widen(name) + L"  x" + std::to_wstring(count);
            r.on = ShowType(name);
            r.id = kRowIdType + static_cast<int>(g_typeList.size());
            g_typeList.push_back(name);
            rows.push_back(std::move(r));
        }
    }
    if (g_state.menuTab == 2) {
        MenuRow section;
        section.name = g_targets.empty() ? L"AUTO-AIM TARGET (NO ENEMY GUNS SEEN YET)" : L"AUTO-AIM TARGET (NEAREST TO CAMERA)";
        section.section = true;
        rows.push_back(std::move(section));
        Vec3 cam;
        const bool hasCam = g_api.CameraFocus(cam);
        for (std::size_t i = 0; i < g_targets.size(); ++i) {
            const EnemyDev* e = FindEnemy(g_targets[i]);
            if (!e) continue;
            MenuRow r;
            wchar_t dist[32] = L"";
            if (hasCam) {
                const float dx = e->pos.x - cam.x, dy = e->pos.y - cam.y;
                swprintf_s(dist, L"  %.1fk", std::sqrt(dx * dx + dy * dy) / 1000.0f);
            }
            r.name = Widen(e->type.empty() ? "gun" : e->type) + L" #" + std::to_wstring(e->id) + dist;
            r.on = g_state.aimTarget == e->id;
            r.id = kRowIdTarget + static_cast<int>(i);
            rows.push_back(std::move(r));
        }
    }
}

void StepSelection(int dir, const std::vector<MenuRow>& rows) {
    const int n = static_cast<int>(rows.size());
    if (n <= 0) return;
    int sel = g_state.menuSel;
    for (int i = 0; i < n; ++i) {
        sel = (sel + dir + n) % n;
        if (!rows[static_cast<std::size_t>(sel)].section) break;
    }
    g_state.menuSel = sel;
    g_state.menuKeyboard = true;
}

void HandleKeys() {
    if (Pressed(kKeyAllOff)) AllOff();
    if (Pressed(kKeyUnlimited)) SetUnlimited(!g_state.unlimited);
    if (Pressed(kKeyMenu)) {
        g_state.menuOpen = !g_state.menuOpen;
        Logf("menu: %d", g_state.menuOpen ? 1 : 0);
    }
}

// Lays out, handles the mouse and keyboard for, and paints the in-game menu. Called inside an overlay pass.
void RunMenu(HDC dc, int vw, int vh) {
    MenuView view;
    BuildRows(view.rows);
    if (g_state.menuSel >= static_cast<int>(view.rows.size())) g_state.menuSel = 0;

    // Keyboard: arrows move inside the tab, Left/Right switch the row.
    if (GameWindowFocused()) {
        if (Pressed(kMenuUp)) StepSelection(-1, view.rows);
        if (Pressed(kMenuDown)) StepSelection(+1, view.rows);
        const bool left = Pressed(kMenuLeft), right = Pressed(kMenuRight);
        if ((left || right) && g_state.menuSel < static_cast<int>(view.rows.size())) {
            g_state.menuKeyboard = true;
            ActivateRowId(view.rows[static_cast<std::size_t>(g_state.menuSel)].id);
            BuildRows(view.rows);
        }
    }
    g_menu.Layout(vw, vh, view.rows, 4);

    HWND wnd = WindowFromDC(dc);
    RECT rc{};
    POINT screenPt{}, pt{};
    const bool havePointer = wnd && GetForegroundWindow() == wnd && GetClientRect(wnd, &rc) && rc.right > 0 && rc.bottom > 0 &&
                             GetCursorPos(&screenPt) && (pt = screenPt, ScreenToClient(wnd, &pt));
    bool cursorInside = false;
    float px = 0, py = 0;
    if (havePointer) {
        static float lastX = -1, lastY = -1;
        static POINT lastOutside{-1, -1};
        px = static_cast<float>(pt.x) * static_cast<float>(vw) / static_cast<float>(rc.right);
        py = static_cast<float>(pt.y) * static_cast<float>(vh) / static_cast<float>(rc.bottom);
        const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        if (down || std::fabs(px - lastX) > 2.0f || std::fabs(py - lastY) > 2.0f) g_state.menuKeyboard = false;
        lastX = px;
        lastY = py;

        MenuHit hit = MenuHit::None;
        int index = -1;
        if (g_menu.Pointer(px, py, down, hit, index)) {
            if (hit == MenuHit::Close) {
                g_state.menuOpen = false;
            } else if (hit == MenuHit::Tab) {
                g_state.menuTab = std::clamp(index, 0, 3);
                g_state.menuSel = 0;
                BuildRows(view.rows);
            } else if (hit == MenuHit::Row && index >= 0 && index < static_cast<int>(view.rows.size())) {
                g_state.menuSel = index;
                ActivateRowId(view.rows[static_cast<std::size_t>(index)].id);
            }
        }
        cursorInside = g_state.menuOpen && (g_menu.Inside(px, py) || g_menu.Pressing());
        // The game keeps seeing the last position outside the menu while the cursor is over it.
        if (!cursorInside || lastOutside.x < 0) lastOutside = screenPt;
        g_neutralPt = lastOutside;
        InterlockedExchange(&g_blockMouse, cursorInside ? 1 : 0);
    } else {
        g_menu.ClearPointer();
    }
    if (!g_state.menuOpen) return;

    BuildRows(view.rows);  // states may have changed by the click above
    g_menu.Layout(vw, vh, view.rows, 4);
    for (const wchar_t* name : kTabNames) view.tabs.push_back(name);
    view.tab = g_state.menuTab;
    view.subtitle = L"Forts";
    view.pill = g_online ? L"Online" : (g_inMatch ? L"In match" : L"Not in match");
    view.pillColor = g_online ? aero::ui::kWarn : (g_inMatch ? aero::ui::kOk : aero::ui::kDim);
    view.keyboardFocus = g_state.menuKeyboard ? g_state.menuSel : -1;
    const bool changed = g_menu.Render(view);
    g_overlay.Image("menu", g_menu.Pixels(), g_menu.Width(), g_menu.Height(), changed, g_menu.X(), g_menu.Y());

    if (cursorInside) {  // the game hides its own cursor over the menu, so draw one
        int cw = 0, ch = 0;
        const void* sprite = g_menu.CursorPixels(cw, ch);
        g_overlay.Image("cursor", sprite, cw, ch, false, px - 2.0f * g_menu.Scale(), py - 2.0f * g_menu.Scale());
    }
}

// ---------------------------------------------------------------- hub channel
void ApplyToggle(LONG request, bool current, void (*set)(bool)) {
    if (request == aero::ipc::kSetOn) set(true);
    else if (request == aero::ipc::kSetOff) set(false);
    else if (request == aero::ipc::kPress) set(!current);
}

// Requests sent from the hub window; processed every frame, even while the game is unfocused.
void ApplyRequests() {
    if (!g_ipc.Ready()) return;
    void (*const setters[kItemCount])(bool) = {SetUnlimited, SetFastBeams, SetFastDevices, SetFastReload, SetCommander, SetRepair,
                                                nullptr, nullptr, SetTrajectory, SetImpact, SetAutoAim, SetEnemyOverlay, SetRadar, SetIntel, SetRecord,
                                                nullptr, nullptr};
    for (int i = 0; i < kItemCount; ++i) {
        const LONG r = g_ipc.TakeRequest(g_featureId[i]);
        if (!r) continue;
        if (setters[i]) ApplyToggle(r, ItemOn(i), setters[i]);
        else ActivateItem(i);
    }
}

void PublishState() {
    for (int i = 0; i < kItemCount; ++i)
        if (kItemKinds[i] == aero::ipc::kToggle) g_ipc.SetState(g_featureId[i], ItemOn(i));
}

void FormatResource(char* out, std::size_t cap, float v) {
    if (v > 1.0e9f) std::snprintf(out, cap, "unlimited");
    else std::snprintf(out, cap, "%.0f", v);
}

void UpdateTelemetry(bool inWorld, bool haveTeam, int team) {
    static int calls = 0;
    if (++calls % 20 != 1) return;  // about three times a second
    g_ipc.SetTelemetry(0, "Game state", haveTeam ? "in match" : (inWorld ? "no local team" : "menu / loading"));
    if (!haveTeam) return;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d", team);
    g_ipc.SetTelemetry(1, "Local team", buf);
    TeamResources r;
    if (g_api.Resources(team, r)) {
        FormatResource(buf, sizeof(buf), r.slotFromOff58);
        g_ipc.SetTelemetry(2, "Resource A (team +0x58)", buf);
        FormatResource(buf, sizeof(buf), r.slotFromOff50);
        g_ipc.SetTelemetry(3, "Resource B (team +0x50)", buf);
    }
    std::vector<int> ids;
    if (g_api.OwnWeapons(team, ids)) {
        std::snprintf(buf, sizeof(buf), "%zu", ids.size());
        g_ipc.SetTelemetry(4, "Own weapons", buf);
    }
    float cp = 0;
    if (g_api.CommanderPoints(team, cp)) {
        std::snprintf(buf, sizeof(buf), "%.0f%%", cp * 100.0f);
        g_ipc.SetTelemetry(5, "Commander gauge", buf);
    }
    int enemyGuns = 0;
    for (const EnemyDev& e : g_enemy) enemyGuns += e.weapon ? 1 : 0;
    std::snprintf(buf, sizeof(buf), "%d guns / %zu devices", enemyGuns, g_enemy.size());
    g_ipc.SetTelemetry(6, "Enemy base", buf);
}

void Tick(HDC dc) {
    if (g_state.faults >= kMaxFaults) return;
    InterlockedExchange(&g_blockMouse, 0);  // RunMenu re-arms it every frame the cursor is over the menu
    g_ipc.Beat();
    bool online = false;
    if (g_api.Multiplayer(online)) UpdateOnline(online);
    ApplyRequests();
    if (GameWindowFocused()) HandleKeys();
    PublishState();

    const bool inWorld = g_api.InWorld();
    int team = 0;
    const bool haveTeam = inWorld && g_api.LocalTeam(team);
    UpdateTelemetry(inWorld, haveTeam, team);
    g_inMatch = haveTeam;
    if (!haveTeam) {  // outside a match only the menu is drawn
        ResetModSwitches();
        int w = 0, h = 0;
        if (g_state.menuOpen && g_overlay.Begin(dc, w, h)) {
            g_uiScale = std::clamp(static_cast<float>(h) / 1080.0f, 0.8f, 2.5f);
            RunMenu(dc, w, h);
            g_overlay.End();
        }
        return;
    }

    if (team != g_syncTeam) {
        g_syncTeam = team;
        ResetModSwitches();
    }
    g_offlineFrames = g_online ? 0 : std::min(g_offlineFrames + 1, kOfflineSettleFrames);
    const bool offline = g_offlineFrames >= kOfflineSettleFrames;

    if (g_online) SyncMod(team);
    SyncTheme(team);

    // SetUnlimitedResources is idempotent in the game (it early-outs when the flag is unchanged),
    // so re-applying about once a second keeps it on across match restarts.
    if (offline && (g_state.framesSinceUnlimited >= 1000 || (g_state.unlimited && ++g_state.framesSinceUnlimited > 60))) {
        g_state.framesSinceUnlimited = 0;
        if (!g_api.SetUnlimited(team, g_state.unlimited)) Logf("SetUnlimitedResources faulted (team %d)", team);
    }
    if (offline || g_online) ApplyFastBeams();  // online this only puts the original material times back
    if (offline) {
        ApplyFastDevices(team);
        ApplyFastReload(team);
        ApplyCommander(team);
        ApplyRepair(team);
    }

    const bool needWorld = g_state.enemyOverlay || g_state.radar || g_state.intel || g_state.autoAim || (g_state.menuOpen && g_state.menuTab == 2);
    if (needWorld) RefreshWorld(team, g_state.radar);
    if (offline) ApplyAutoAim(team);
    if (g_state.record) RecordSamples(team);
    if (g_state.wantDiagnostics) {
        g_state.wantDiagnostics = false;
        Diagnostics(team);
    }

    const bool draw = g_state.trajectory || g_state.menuOpen || g_state.enemyOverlay || g_state.radar || g_state.intel;
    if (!draw) return;
    if (g_state.trajectory || g_state.menuOpen) RefreshTypes(team);
    if (g_state.trajectory) {
        std::vector<int> own;
        if (g_api.OwnWeapons(team, own)) LearnPhysics(team, own);
    }
    int w = 0, h = 0;
    if (!g_overlay.Begin(dc, w, h)) return;
    g_uiScale = std::clamp(static_cast<float>(h) / 1080.0f, 0.8f, 2.5f);
    g_hitIds.clear();
    if ((g_state.trajectory || g_state.enemyOverlay) && PrepareCalibration(w, h)) {
        if (g_state.trajectory) DrawTrajectories(team);
        if (g_state.enemyOverlay) DrawEnemies(w, h);
    }
    if (g_state.radar) DrawRadar(w);
    if (g_state.intel) DrawIntel(w);
    if (g_state.menuOpen) RunMenu(dc, w, h);
    g_overlay.End();
}

void SafeTick(HDC dc) {
    __try {
        Tick(dc);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ++g_state.faults;
        Logf("exception in frame tick (%d/%d)", g_state.faults, kMaxFaults);
    }
}

BOOL WINAPI HkSwapBuffers(HDC dc) {
    SafeTick(dc);
    return g_origSwap(dc);
}

}  // namespace

bool InstallFeatures(std::string& error) {
    for (int& id : g_featureId) id = -1;
    HMODULE exe = GetModuleHandleW(nullptr);
    if (!g_api.Init(exe, error)) return false;
    LoadSettings();

    static ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartupInput gdiInput;
    if (Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, nullptr) != Gdiplus::Ok) {
        error = "GDI+ startup failed";
        return false;
    }

    std::uintptr_t slotRva = 0;
    if (!g_hook.Install(exe, "gdi32.dll", "SwapBuffers", reinterpret_cast<void*>(&HkSwapBuffers),
                        reinterpret_cast<void**>(&g_origSwap), &slotRva)) {
        error = "SwapBuffers import not found in the exe";
        return false;
    }
    if (slotRva != cfg::kSwapBuffersIatRva) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "SwapBuffers IAT slot RVA 0x%llX differs from measured 0x%llX",
                      static_cast<unsigned long long>(slotRva), static_cast<unsigned long long>(cfg::kSwapBuffersIatRva));
        error = buf;
        g_hook.Remove();  // roll back: nothing stays patched when a check fails
        return false;
    }
    // Optional: keep clicks on the menu away from the game. Without these the menu still works.
    const bool cursorOk = g_hookCursor.Install(exe, "user32.dll", "GetCursorPos", reinterpret_cast<void*>(&HkGetCursorPos),
                                               reinterpret_cast<void**>(&g_origGetCursorPos));
    const bool keyOk = g_hookKey.Install(exe, "user32.dll", "GetKeyState", reinterpret_cast<void*>(&HkGetKeyState),
                                         reinterpret_cast<void**>(&g_origGetKeyState));
    Logf("menu input isolation: GetCursorPos hook=%d GetKeyState hook=%d (DirectInput mouse reads are not intercepted)",
         cursorOk ? 1 : 0, keyOk ? 1 : 0);

    // Hook is live; only now expose the hub control channel.
    if (g_ipc.Create()) {
        for (int i = 0; i < kItemCount; ++i) g_featureId[i] = g_ipc.AddFeature(kItemNames[i], kItemKinds[i]);
    } else {
        Logf("hub control channel unavailable (in-game menu still works)");
    }
    return true;
}

}  // namespace forts
