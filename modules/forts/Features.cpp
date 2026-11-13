#include "Features.hpp"

#include <windows.h>

#include <GL/gl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
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
#include "Tactics.hpp"
#include "Ui.hpp"

namespace forts {
namespace {

constexpr int kKeyMenu = VK_INSERT;  // F7/F8 collide with the game's own hotkeys (F8 = FPS counter)
constexpr int kMenuUp = VK_UP, kMenuDown = VK_DOWN, kMenuLeft = VK_LEFT, kMenuRight = VK_RIGHT;
constexpr int kMaxTypeRows = 6, kMaxTargetRows = 6;
constexpr int kRowIdType = 100, kRowIdTarget = 200, kRowIdLanguage = 300, kRowIdScale = 301;  // menu row ids: < 100 are items

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
    kItemTrajectory, kItemImpact, kItemThreats, kItemAimCalc, kItemAutoAim,
    // Enemy
    kItemEnemyOverlay, kItemRadar, kItemIntel, kItemTimers, kItemWeakEnemy, kItemWeakOwn,
    // Tools
    kItemStats, kItemRecord, kItemDiagnostics, kItemAllOff,
    // Added later (kept at the end so the hub channel ids of the older items do not move); shown in the Combat tab
    kItemPickTarget,
    // Battery: guns selected in the game pile up in one group of any size (the game's own stops at three)
    kItemBattery, kItemBatteryTarget,
    kItemCount
};
constexpr const char* kItemNames[kItemCount] = {
    "Unlimited resources", "Fast build: beams", "Fast build: tech and guns", "Minigun (fast reload)", "Commander auto-refill",
    "Auto-repair", "Repair speed", "Repair what", "Trajectory overlay", "Impact markers", "Incoming shells warning",
    "Aim calculator (needs a target)", "Auto-aim at target", "Enemy overlay", "Enemy base window (observer)", "Enemy intel (spy panel)",
    "Enemy gun timers", "Weak links: enemy base", "Weak links: your base", "Battle stats panel", "Record shots to log",
    "Write diagnostics to log", "All off", "Pick target under cursor",
    "Battery: on", "Battery: target + fire"};
constexpr aero::ipc::Kind kItemKinds[kItemCount] = {
    aero::ipc::kToggle, aero::ipc::kToggle, aero::ipc::kToggle, aero::ipc::kToggle, aero::ipc::kToggle, aero::ipc::kToggle,
    aero::ipc::kButton, aero::ipc::kButton,  // repair speed / repair what (choice rows)
    aero::ipc::kToggle, aero::ipc::kToggle, aero::ipc::kToggle, aero::ipc::kToggle, aero::ipc::kToggle,  // combat
    aero::ipc::kToggle, aero::ipc::kToggle, aero::ipc::kToggle, aero::ipc::kToggle, aero::ipc::kToggle, aero::ipc::kToggle,  // enemy
    aero::ipc::kToggle, aero::ipc::kToggle, aero::ipc::kButton, aero::ipc::kButton,  // tools
    aero::ipc::kButton,                                                               // pick target
    aero::ipc::kToggle, aero::ipc::kButton};                                          // battery: on, target + fire
constexpr int kItemTab[kItemCount] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 1, 1, 1};
static_assert(kItemCount <= aero::ipc::kMaxFeatures, "the hub channel has room for kMaxFeatures features");

// Repair speed levels: share of the maximum health restored per second. "Instant" restores everything each pass.
constexpr const wchar_t* kRepairSpeedNames[4] = {L"Slow 8%/s", L"Medium 25%/s", L"Fast 60%/s", L"Instant"};
constexpr float kRepairRates[4] = {0.08f, 0.25f, 0.60f, 100.0f};
constexpr const wchar_t* kRepairWhatNames[3] = {L"Everything", L"Devices", L"Beams"};
constexpr int kTabCount = 5;  // the fifth tab (Settings) has no feature items: language, menu size
constexpr const wchar_t* kTabNames[kTabCount] = {L"General", L"Combat", L"Enemy", L"Tools", L"Settings"};

// Menu language: 0 English, 1 Russian. Saved in FortsLang.ini next to the module (see LangPath).
int g_lang = 0;
bool g_selfTest = false;  // a file FortsSelfTest.txt next to the module turns the passive self-test on (see SelfTestTick)
const wchar_t* Tr(const wchar_t* en, const wchar_t* ru) { return g_lang == 1 ? ru : en; }

constexpr const wchar_t* kItemNamesRu[kItemCount] = {
    L"Бесконечные ресурсы", L"Быстрая постройка: балки", L"Быстрая постройка: техи и орудия", L"Миниган (быстрая перезарядка)",
    L"Автозаряд командира", L"Авторемонт", L"Скорость ремонта", L"Что ремонтировать", L"Траектория выстрела", L"Метки попадания",
    L"Предупреждение о снарядах", L"Расчёт прицела (нужна цель)", L"Автонаведение на цель", L"Оверлей врага",
    L"Окно базы врага (наблюдатель)", L"Разведка врага (панель шпиона)", L"Таймеры орудий врага", L"Слабые звенья: база врага",
    L"Слабые звенья: моя база", L"Панель статистики боя", L"Запись выстрелов в лог", L"Записать диагностику в лог", L"Выключить всё",
    L"Выбрать цель под курсором",
    L"Батарея: включить", L"Батарея: цель и огонь"};
constexpr const wchar_t* kItemDescEn[kItemCount] = {
    L"Metal and energy never run out",
    L"Struts build instantly (offline only)",
    L"Devices and guns build much faster",
    L"Guns reload almost instantly",
    L"Keeps the commander gauge full",
    L"Repairs damaged devices and beams over time",
    L"How fast auto-repair works (click to change)",
    L"What auto-repair fixes (click to change)",
    L"Draws the flight arc of your guns' shells",
    L"Rings where shells land, with flight time",
    L"Warns about enemy shells heading for your base",
    L"Angle to hit the target with your selected gun",
    L"Turns your guns onto the target (offline only)",
    L"Type, health and reload over enemy devices",
    L"Window with the enemy base (observer)",
    L"Spy panel with information about the enemy",
    L"Reload bar on every enemy gun",
    L"Beams whose loss would split the enemy base",
    L"Beams whose loss would split your base",
    L"Shots, hits and damage (estimates)",
    L"Writes shot data to the log for calibration",
    L"Writes a state report to the log once",
    L"Switches every feature off",
    L"Target = the enemy device nearest to the mouse",
    L"Every gun you select in the game joins one group, no limit of 3. Off = the group is emptied (offline only)",
    L"Target = the point under the mouse: the whole group aims there and fires a salvo (offline only)"};
constexpr const wchar_t* kItemDescRu[kItemCount] = {
    L"Металл и энергия не заканчиваются",
    L"Балки строятся мгновенно (только офлайн)",
    L"Устройства и орудия строятся быстрее",
    L"Орудия перезаряжаются почти мгновенно",
    L"Шкала командира всегда полная",
    L"Со временем чинит устройства и балки",
    L"Скорость авторемонта (клик — сменить)",
    L"Что чинит авторемонт (клик — сменить)",
    L"Рисует дугу полёта снарядов ваших орудий",
    L"Кольца точек падения и время полёта",
    L"Предупреждает о снарядах, летящих в вашу базу",
    L"Угол для попадания из выбранного орудия",
    L"Наводит орудия на цель (только офлайн)",
    L"Тип, здоровье и перезарядка врага",
    L"Окно с базой врага (для наблюдателя)",
    L"Панель шпиона со сведениями о враге",
    L"Полоса перезарядки на орудиях врага",
    L"Балки, без которых база врага развалится",
    L"Балки, без которых ваша база развалится",
    L"Выстрелы, попадания и урон (оценка)",
    L"Пишет данные выстрелов в лог для калибровки",
    L"Один раз пишет отчёт о состоянии в лог",
    L"Выключает все функции",
    L"Цель — вражеское устройство ближе всего к мыши",
    L"Каждое орудие, выбранное в игре, входит в одну группу, без лимита в 3. Выкл = группа очищается (только офлайн)",
    L"Цель = точка под мышью: вся группа наводится туда и даёт залп (только офлайн)"};
constexpr const wchar_t* kRepairSpeedRu[4] = {L"Медл. 8%/с", L"Сред. 25%/с", L"Быстр. 60%/с", L"Мгновенно"};
constexpr const wchar_t* kRepairWhatRu[3] = {L"Всё", L"Устройства", L"Балки"};
constexpr const wchar_t* kTabNamesRu[kTabCount] = {L"Общее", L"Бой", L"Враг", L"Инструменты", L"Настройки"};

// ---- key binds: one key per action; "toggle" flips the action on each press, "hold" keeps a switch on only while the key is down.
struct Bind {
    int vk = 0;  // 0 = none
    bool hold = false;
};
Bind g_binds[kItemCount];
bool g_holdDown[kItemCount]{}, g_holdPrev[kItemCount]{}, g_keyPrev[kItemCount]{};
int g_bindCapture = -1;  // the item whose key is being chosen (-1 = none)
float g_menuScale = 1.0f;  // the user's menu size multiplier (Settings tab)

void DefaultBinds() {
    for (Bind& b : g_binds) b = Bind{};
    g_binds[kItemUnlimited] = {VK_F6, false};
    g_binds[kItemAllOff] = {VK_END, false};
    g_binds[kItemPickTarget] = {VK_F9, false};
}

std::wstring KeyName(int vk) {
    if (vk == VK_MBUTTON) return L"Mouse 3";
    if (vk == VK_XBUTTON1) return L"Mouse 4";
    if (vk == VK_XBUTTON2) return L"Mouse 5";
    UINT scan = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
    LONG lparam = static_cast<LONG>(scan << 16);
    switch (vk) {  // extended keys need bit 24 for GetKeyNameText to give the right name
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_NUMLOCK: case VK_RCONTROL: case VK_RMENU: case VK_DIVIDE:
            lparam |= 1 << 24;
            break;
        default: break;
    }
    wchar_t buf[40]{};
    if (scan && GetKeyNameTextW(lparam, buf, 40) > 0) return buf;
    return L"Key " + std::to_wstring(vk);
}

std::wstring BindLabel(int item) {
    if (item < 0 || item >= kItemCount || g_binds[item].vk == 0) return Tr(L"[set a key in the menu]", L"[назначьте клавишу в меню]");
    return L"[" + KeyName(g_binds[item].vk) + L"]";
}

// Caption shown above an item (starts a group of related rows); nullptr = no caption.
const wchar_t* SectionBefore(int item) {
    switch (item) {
        case kItemUnlimited: return Tr(L"RESOURCES AND SPEED", L"РЕСУРСЫ И СКОРОСТЬ");
        case kItemRepair: return Tr(L"REPAIR", L"РЕМОНТ");
        case kItemTrajectory: return Tr(L"MY SHOTS", L"МОИ ВЫСТРЕЛЫ");
        case kItemThreats: return Tr(L"DEFENCE", L"ЗАЩИТА");
        case kItemAimCalc: return Tr(L"AIMING", L"НАВЕДЕНИЕ");
        case kItemBattery: return Tr(L"BATTERY (ANY NUMBER OF GUNS, ONE PLACE)", L"БАТАРЕЯ (ЛЮБОЕ ЧИСЛО ОРУДИЙ, ОДНО МЕСТО)");
        case kItemEnemyOverlay: return Tr(L"ENEMY INFORMATION", L"ИНФОРМАЦИЯ О ВРАГЕ");
        case kItemWeakEnemy: return Tr(L"WEAK LINKS", L"СЛАБЫЕ ЗВЕНЬЯ");
        case kItemStats: return Tr(L"STATISTICS", L"СТАТИСТИКА");
        case kItemRecord: return Tr(L"LOGS (FOR THE DEVELOPER)", L"ЛОГИ (ДЛЯ РАЗРАБОТЧИКА)");
        case kItemAllOff: return Tr(L"RESET", L"СБРОС");
        default: return nullptr;
    }
}

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
    bool threats = false;    // incoming enemy shells: where they land, when, what they threaten
    bool aimCalc = false;    // elevation needed to hit the target chosen in the Enemy tab
    bool timers = false;     // reload state of enemy guns
    bool weakEnemy = false;  // links whose loss cuts a part of the enemy base off from the ground
    bool weakOwn = false;
    bool stats = false;      // battle statistics panel (estimates)
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
    std::vector<int> group;  // own guns chosen as one battery; the group target is groupPoint, else the aimTarget device
    bool battery = false;  // on: guns selected in the game pile up in `group`, and are kept aimed at the target
    int lastSelected = 0;
    bool groupPointSet = false;
    Vec3 groupPoint{};
    int framesSincePrune = 0;
    int faults = 0;
    bool loggedCalibration = false;
    bool warnedCalibration = false;
};

GameApi g_api;
Overlay g_overlay;
Menu g_menu;
Settings g_settings;
State g_state;
Calibration g_cal;  // camera / gravity direction, re-derived every frame in PrepareCalibration
bool g_calOk = false;
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
    bool timerOk = false;   // the fields below were read this refresh
    bool ready = false;     // IsWeaponReadyToFire
    float reloadValue = 0;  // GetWeaponReloadTime as reported
    float reloadPeriod = 0;
};
std::vector<EnemyDev> g_enemy;
// Our own devices (same refresh) and the team codes of the opponents, for the warnings and the support analysis.
struct OwnDev {
    int id = 0;
    Vec3 pos;
    std::string type;
    bool weapon = false;
};
std::vector<OwnDev> g_own;
std::vector<int> g_enemyCodes;
std::map<int, std::string> g_typeById;  // device id -> type name (a device never changes type)
bool g_worldFresh = false;              // set by every RefreshWorld pass, consumed by the damage statistics

const std::string& TypeOf(int deviceId) {
    auto it = g_typeById.find(deviceId);
    if (it == g_typeById.end()) {
        if (g_typeById.size() > 6000) g_typeById.clear();
        std::string t;
        g_api.DeviceTypeName(deviceId, t);
        it = g_typeById.emplace(deviceId, std::move(t)).first;
    }
    return it->second;
}
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
    // v2: the first versions measured drag against wall-clock time while the game changes velocity only on its 0.04 s physics frames;
    // those values were about 28% too low, so the old FortsDrag.ini is not read any more.
    return slash == std::wstring::npos ? p : p.substr(0, slash + 1) + L"FortsDrag.v2.ini";
}

std::wstring LangPath() {  // separate file for the same reason as DragPath
    std::wstring p = IniPath();
    const std::size_t slash = p.find_last_of(L'\\');
    return slash == std::wstring::npos ? p : p.substr(0, slash + 1) + L"FortsLang.ini";
}

void SaveLanguage() {
    const std::wstring path = LangPath();
    if (!path.empty()) WritePrivateProfileStringW(L"Menu", L"Language", g_lang == 1 ? L"ru" : L"en", path.c_str());
}

void SaveMenuScale() {
    const std::wstring path = LangPath();
    wchar_t v[16];
    swprintf_s(v, L"%.2f", g_menuScale);
    if (!path.empty()) WritePrivateProfileStringW(L"Menu", L"Scale", v, path.c_str());
}

std::wstring BindsPath() {
    std::wstring p = IniPath();
    const std::size_t slash = p.find_last_of(L'\\');
    return slash == std::wstring::npos ? p : p.substr(0, slash + 1) + L"FortsBinds.ini";
}

void SaveBinds() {
    const std::wstring path = BindsPath();
    if (path.empty()) return;
    for (int i = 0; i < kItemCount; ++i) {
        wchar_t key[16], value[24];
        swprintf_s(key, L"item%d", i);
        swprintf_s(value, L"%d,%d", g_binds[i].vk, g_binds[i].hold ? 1 : 0);
        WritePrivateProfileStringW(L"Binds", key, value, path.c_str());
    }
}

// "item<N>=vk,hold" lines; an item without a line keeps its default, "0,0" means the user removed the key.
void LoadBinds() {
    DefaultBinds();
    const std::wstring path = BindsPath();
    if (path.empty()) return;
    for (int i = 0; i < kItemCount; ++i) {
        wchar_t key[16], value[24]{};
        swprintf_s(key, L"item%d", i);
        GetPrivateProfileStringW(L"Binds", key, L"", value, 24, path.c_str());
        if (!value[0]) continue;
        int vk = 0, hold = 0;
        if (swscanf_s(value, L"%d,%d", &vk, &hold) < 1) continue;
        if (vk < 0 || vk > 254 || vk == VK_LBUTTON || vk == VK_RBUTTON || vk == kKeyMenu) vk = 0;
        g_binds[i] = {vk, hold != 0 && kItemKinds[i] == aero::ipc::kToggle};
    }
}

// ---------------------------------------------------------------- movable panels
// Every HUD panel (aim calculator, weak links, battle stats, enemy intel, radar) can be dragged with the left mouse button while the
// menu is open; a right click on a panel puts it back to its default place. Positions are viewport pixels, kept in FortsPanels.ini.
struct PanelSpot {
    float x = 0, y = 0;        // saved position (valid when moved)
    float curX = 0, curY = 0;  // where it was drawn this frame
    float w = 0, h = 0;
    bool moved = false, shown = false;
};
std::map<std::string, PanelSpot> g_spots;
std::vector<std::string> g_spotOrder;  // draw order, later = on top
std::string g_dragSpot;
float g_dragGrabX = 0, g_dragGrabY = 0;
int g_viewW = 0, g_viewH = 0;
constexpr const char* kSpotKeys[] = {"aimcalc", "weak", "stats", "intel", "radar"};

std::wstring PanelsPath() {
    std::wstring p = IniPath();
    const std::size_t slash = p.find_last_of(L'\\');
    return slash == std::wstring::npos ? p : p.substr(0, slash + 1) + L"FortsPanels.ini";
}

void SavePanelSpot(const std::string& key) {
    const std::wstring path = PanelsPath();
    if (path.empty()) return;
    const std::wstring wkey(key.begin(), key.end());
    const PanelSpot& s = g_spots[key];
    wchar_t value[40];
    swprintf_s(value, L"%.0f,%.0f", s.x, s.y);
    WritePrivateProfileStringW(L"Panels", wkey.c_str(), s.moved ? value : nullptr, path.c_str());  // nullptr removes the line
}

void LoadPanelSpots() {
    const std::wstring path = PanelsPath();
    if (path.empty()) return;
    for (const char* k : kSpotKeys) {
        const std::wstring wkey(k, k + std::strlen(k));
        wchar_t value[40]{};
        GetPrivateProfileStringW(L"Panels", wkey.c_str(), L"", value, 40, path.c_str());
        float x = 0, y = 0;
        if (value[0] && swscanf_s(value, L"%f,%f", &x, &y) == 2 && std::isfinite(x) && std::isfinite(y) && x > -20000.0f && x < 20000.0f &&
            y > -20000.0f && y < 20000.0f) {
            PanelSpot& s = g_spots[k];
            s.x = x;
            s.y = y;
            s.moved = true;
        }
    }
}

// Returns where the panel is drawn: its saved position if it was moved (kept inside the viewport), else the default.
void PlacePanel(const char* key, float defX, float defY, int w, int h, float& x, float& y) {
    PanelSpot& s = g_spots[key];
    if (std::find(g_spotOrder.begin(), g_spotOrder.end(), key) == g_spotOrder.end()) g_spotOrder.emplace_back(key);
    x = s.moved ? s.x : defX;
    y = s.moved ? s.y : defY;
    if (g_viewW > 0 && g_viewH > 0) {
        x = std::clamp(x, 0.0f, std::max(0.0f, static_cast<float>(g_viewW - w)));
        y = std::clamp(y, 0.0f, std::max(0.0f, static_cast<float>(g_viewH - h)));
    }
    s.curX = x;
    s.curY = y;
    s.w = static_cast<float>(w);
    s.h = static_cast<float>(h);
    s.shown = true;
}

// Feed the pointer once per frame while the menu is open. `menuTakes` = the pointer is on the menu window. Returns true while the
// pointer is over a panel or dragging one, so the game must not see the mouse.
bool PanelPointer(float px, float py, bool down, bool menuTakes) {
    static bool wasDown = false, wasRight = false;
    const bool right = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    std::string hit;
    if (!menuTakes) {
        for (auto it = g_spotOrder.rbegin(); it != g_spotOrder.rend(); ++it) {
            const PanelSpot& s = g_spots[*it];
            if (s.shown && px >= s.curX && px < s.curX + s.w && py >= s.curY && py < s.curY + s.h) {
                hit = *it;
                break;
            }
        }
    }
    if (!g_dragSpot.empty()) {
        PanelSpot& s = g_spots[g_dragSpot];
        if (down) {
            s.x = std::clamp(px - g_dragGrabX, 0.0f, std::max(0.0f, static_cast<float>(g_viewW) - s.w));
            s.y = std::clamp(py - g_dragGrabY, 0.0f, std::max(0.0f, static_cast<float>(g_viewH) - s.h));
            s.moved = true;
            s.curX = s.x;
            s.curY = s.y;
        } else {
            SavePanelSpot(g_dragSpot);
            g_dragSpot.clear();
        }
    } else if (!hit.empty()) {
        PanelSpot& s = g_spots[hit];
        if (down && !wasDown) {
            g_dragSpot = hit;
            g_dragGrabX = px - s.curX;
            g_dragGrabY = py - s.curY;
        } else if (right && !wasRight && s.moved) {
            s.moved = false;
            SavePanelSpot(hit);
        }
    }
    wasDown = down;
    wasRight = right;
    return !g_dragSpot.empty() || !hit.empty();
}

void LoadSettings() {
    const std::wstring path = IniPath();
    if (path.empty()) return;
    {
        wchar_t lang[16]{};
        GetPrivateProfileStringW(L"Menu", L"Language", L"en", lang, 16, LangPath().c_str());
        g_lang = _wcsicmp(lang, L"ru") == 0 ? 1 : 0;
        wchar_t scale[16]{};
        GetPrivateProfileStringW(L"Menu", L"Scale", L"1.00", scale, 16, LangPath().c_str());
        const float sc = static_cast<float>(_wtof(scale));
        g_menuScale = sc >= 0.75f && sc <= 2.5f ? sc : 1.0f;
        LoadBinds();
        LoadPanelSpots();
        std::wstring flag = path;
        const std::size_t slash = flag.find_last_of(L'\\');
        if (slash != std::wstring::npos) {
            flag = flag.substr(0, slash + 1) + L"FortsSelfTest.txt";
            g_selfTest = GetFileAttributesW(flag.c_str()) != INVALID_FILE_ATTRIBUTES;
            if (g_selfTest) Logf("selftest: enabled by FortsSelfTest.txt");
        }
    }
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

// The linear drag of a weapon type's shells (per second). Order of trust: the value measured on live shots (exact, see
// EstimateDragFrames), then k = kDragFactor * ProjectileDrag from the game's data, and 0 when neither is known.
std::map<std::string, float> g_typeDragFormula;  // type -> kDragFactor * ProjectileDrag
std::set<std::string> g_propelled;               // types whose shells fly under thrust: the frame model does not describe them

float DragFor(const std::string& type) {
    const auto it = g_drag.find(type);
    if (it != g_drag.end() && it->second.n >= 2) return static_cast<float>(it->second.sum / it->second.n);
    const auto f = g_typeDragFormula.find(type);
    return f != g_typeDragFormula.end() ? f->second : 0.0f;
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

void SetBattery(bool on) {
    if (RefusedOnline(on, "battery")) return;
    g_state.battery = on;
    if (!on) {
        g_state.group.clear();
        g_state.groupPointSet = false;
        g_state.lastSelected = 0;
    }
    Logf("battery: %d", on ? 1 : 0);
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

void SetThreats(bool on) {
    g_state.threats = on;
    Logf("incoming shells warning: %d", on ? 1 : 0);
}

void SetAimCalc(bool on) {
    g_state.aimCalc = on;
    Logf("aim calculator: %d (target %d)", on ? 1 : 0, g_state.aimTarget);
}

void SetTimers(bool on) {
    g_state.timers = on;
    Logf("enemy gun timers: %d", on ? 1 : 0);
}

void SetWeakEnemy(bool on) {
    g_state.weakEnemy = on;
    Logf("weak links (enemy base): %d", on ? 1 : 0);
}

void SetWeakOwn(bool on) {
    g_state.weakOwn = on;
    Logf("weak links (own base): %d", on ? 1 : 0);
}

void SetStats(bool on) {
    g_state.stats = on;
    Logf("battle stats panel: %d", on ? 1 : 0);
}

void AllOff() {
    g_state.trajectory = false;
    g_state.record = false;
    g_state.impact = false;
    g_state.autoAim = false;
    if (g_state.battery) SetBattery(false);
    g_state.enemyOverlay = false;
    g_state.radar = false;
    g_state.intel = false;
    g_state.threats = g_state.aimCalc = g_state.timers = false;
    g_state.weakEnemy = g_state.weakOwn = g_state.stats = false;
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
    g_state.commander = g_state.repair = g_state.autoAim = g_state.battery = false;
    g_state.group.clear();
    g_state.groupPointSet = false;
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
    if (chain) {  // the drag of the same projectile, from the game's data
        float pd = 0;
        if (!g_api.ProjectileTypeDrag(projectile, team, pd)) {
            Logf("drag: ProjectileDrag of '%s' is unreadable; the drag comes from live shots only", projectile.c_str());
        } else if (pd >= cfg::kPropelledDrag) {
            g_propelled.insert(type);
            Logf("drag: weapon type '%s' fires '%s' with ProjectileDrag %.3f: thrust-driven, its path is not predicted", type.c_str(), projectile.c_str(), pd);
        } else {
            g_typeDragFormula[type] = cfg::kDragFactor * pd;
            Logf("drag: weapon type '%s' fires '%s' with ProjectileDrag %.4f -> k = %.4f per second from the game's data", type.c_str(), projectile.c_str(), pd,
                 cfg::kDragFactor * pd);
        }
    }
    return magnitude;
}

// Learns the air drag of every weapon type from live shots: a new projectile is matched to the weapon whose
// aim (direction and speed) it left, then its horizontal speed decay between frames gives k = -ln(vx1/vx0)/dt.
struct FireSnapshot {
    std::string type;
    float vx = 0, vy = 0;
    Vec3 barrel;
    bool hasBarrel = false;
};
struct ProjTrack {
    std::string type;
    double t = 0;
    float vx = 0, vy = 0;
};
std::map<int, FireSnapshot> g_prevFire;
std::map<int, ProjTrack> g_tracks;

std::string MatchWeaponType(float vx, float vy, int* weaponId = nullptr) {
    const float speed = std::sqrt(vx * vx + vy * vy);
    const float ang = std::atan2(vy, vx);
    float best = 0.09f;  // 5 degrees
    std::string type;
    if (weaponId) *weaponId = 0;
    for (const auto& [id, f] : g_prevFire) {
        const float ws = std::sqrt(f.vx * f.vx + f.vy * f.vy);
        if (ws < 1.0f || std::fabs(speed / ws - 1.0f) > 0.08f) continue;
        const float d = std::fabs(ang - std::atan2(f.vy, f.vx));
        if (d < best) {
            best = d;
            type = f.type;
            if (weaponId) *weaponId = id;
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
        FireSnapshot snap{t, v.x, v.y, {}, false};
        snap.hasBarrel = g_api.BarrelPosition(id, snap.barrel);
        cur[id] = std::move(snap);
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
                int weaponId = 0;
                const std::string type = MatchWeaponType(v.x, v.y, &weaponId);
                g_tracks[id] = {type, now, v.x, v.y};
                // Launch record for calibrating where a shell appears relative to the barrel: what the gun reported one refresh before,
                // and the first state of the shell (the log line is `launch:`; 40 per session).
                static int launches = 0;
                const auto snap = g_prevFire.find(weaponId);
                if (launches < 40 && !type.empty() && snap != g_prevFire.end() && snap->second.hasBarrel) {
                    ++launches;
                    float grav = 0;
                    Vec3 first{};
                    g_api.ProjectileGravity(id, grav);
                    g_api.NodePos(id, first);
                    Logf("launch: type=%s weapon=%d barrel=(%.2f, %.2f) fireVel=(%.2f, %.2f) first=(%.2f, %.2f) vel=(%.2f, %.2f) gravity=%.1f", type.c_str(),
                         weaponId, snap->second.barrel.x, snap->second.barrel.y, snap->second.vx, snap->second.vy, first.x, first.y, v.x, v.y, grav);
                }
                continue;
            }
            ProjTrack& tr = it->second;
            // Exact drag: the velocity only changes on the game's 0.04 s physics frames, so the estimate works on pairs of
            // observations with the number of frames between them recovered from the vertical velocity (EstimateDragFrames).
            if (!tr.type.empty() && !g_propelled.count(tr.type)) {
                float grav = 0;
                if (g_api.ProjectileGravity(id, grav) && std::fabs(grav) > 1.0f) {
                    const DragEstimate e = EstimateDragFrames(tr.vx, tr.vy, v.x, v.y, g_cal.gravitySign * std::fabs(grav));
                    if (e.ok) {
                        DragStat& st = g_drag[tr.type];
                        st.sum += e.k;
                        ++st.n;
                        if (st.n >= 3 && st.n - st.savedAtN >= 50) {
                            st.savedAtN = st.n;
                            SaveDrag(tr.type, st);
                            const auto formula = g_typeDragFormula.find(tr.type);
                            Logf("drag: weapon type '%s' measured k=%.5f from %d shell samples (data formula %s%.5f)", tr.type.c_str(), st.sum / st.n, st.n,
                                 formula == g_typeDragFormula.end() ? "unknown " : "", formula == g_typeDragFormula.end() ? 0.0f : formula->second);
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
// Reload state of enemy guns. GetWeaponReloadTime gives the reload period times a fraction; whether that fraction is the part that
// is left or the part that has elapsed is not visible in the disassembly, so it is learned from the first reloads seen to the end:
// the last value before a gun turns ready is close to 0 when it counts down and close to the period when it counts up.
struct TimerPrev {
    bool have = false, ready = false;
    float value = 0, period = 0;
};
std::map<int, TimerPrev> g_timerPrev;
int g_reloadSemantics = 0;  // 0 unknown, 1 the value is the time left, 2 the value is the time elapsed
int g_semLeft = 0, g_semElapsed = 0;

void ReadTimers(EnemyDev& e) {
    bool ready = false;
    float value = 0, period = 0;
    if (!g_api.WeaponReady(e.id, ready) || !g_api.WeaponReload(e.id, value, period)) return;
    e.timerOk = true;
    e.ready = ready;
    e.reloadValue = value;
    e.reloadPeriod = period;
    if (g_timerPrev.size() > 4000) g_timerPrev.clear();
    TimerPrev& pv = g_timerPrev[e.id];
    if (pv.have && !pv.ready && ready && pv.period > 1.0f) {  // a reload just finished: what did the value say a moment ago?
        const float f = pv.value / pv.period;
        if (f < 0.25f) ++g_semLeft;
        else if (f > 0.75f) ++g_semElapsed;
        if (g_reloadSemantics == 0 && (g_semLeft >= 3 || g_semElapsed >= 3) && (g_semLeft == 0 || g_semElapsed == 0)) {
            g_reloadSemantics = g_semLeft > 0 ? 1 : 2;
            Logf("timers: the reload value counts %s (learned from %d reload(s))", g_reloadSemantics == 1 ? "down (time left)" : "up (time elapsed)",
                 g_semLeft + g_semElapsed);
        }
    }
    pv.have = true;
    pv.ready = ready;
    pv.value = value;
    pv.period = period;
    static int logged = 0;
    if (logged < 12) {
        ++logged;
        Logf("timers: weapon id=%d ready=%d reload value=%.3f period=%.3f", e.id, ready ? 1 : 0, value, period);
    }
}

void RefreshWorld(int team, bool wantRadar) {
    static int frame = 0;
    if (frame++ % 20 != 0) return;
    g_enemy.clear();
    g_own.clear();
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
        if (own) {
            OwnDev o;
            o.id = d.id;
            o.pos = p;
            o.weapon = isWeapon;
            o.type = TypeOf(d.id);
            g_own.push_back(std::move(o));
            continue;
        }
        if (isWeapon && g_state.timers) ReadTimers(e);
        g_enemy.push_back(std::move(e));
    }
    g_worldFresh = true;
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
        g_enemyCodes.clear();
        for (const auto& [code, n] : perCode) g_enemyCodes.push_back(code);
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
            if (g_state.timers) {
                int known = 0, ready = 0;
                for (const EnemyDev& e : g_enemy)
                    if (e.weapon && e.timerOk) {
                        ++known;
                        ready += e.ready ? 1 : 0;
                    }
                lines.push_back({L"Guns ready", known > 0 ? std::to_wstring(ready) + L" / " + std::to_wstring(known) : std::wstring(L"-"),
                                 known > 0 && ready == known ? aero::ui::kWarn : 0});
            }
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

// ---------------------------------------------------------------- gun group
// A battery of own guns of any size (the game's own selection stops at three). Every id comes from the game itself
// (GetLocalSelectedDeviceId / GetWeaponId); ids of guns that no longer exist are dropped. Target: the point set with
// "Group: target = cursor point", otherwise the enemy device chosen as the auto-aim target.
constexpr std::size_t kMaxGroup = 32;

bool GroupTarget(Vec3& out) {
    if (g_state.groupPointSet) {
        out = g_state.groupPoint;
        return true;
    }
    return g_state.aimTarget != 0 && g_api.DevicePosition(g_state.aimTarget, out);
}

bool InGroup(int id) { return std::find(g_state.group.begin(), g_state.group.end(), id) != g_state.group.end(); }

void PruneGroup(int team) {
    if (g_state.group.empty()) return;
    std::vector<int> own;
    if (!g_api.OwnWeapons(team, own)) return;  // a failed read says nothing about the guns
    const std::size_t before = g_state.group.size();
    g_state.group.erase(std::remove_if(g_state.group.begin(), g_state.group.end(),
                                       [&](int id) { return std::find(own.begin(), own.end(), id) == own.end(); }),
                        g_state.group.end());
    if (g_state.group.size() != before) Logf("group: %zu gun(s) gone, %zu left", before - g_state.group.size(), g_state.group.size());
}

// Battery on: the gun the player has selected in the game joins the group (checked whenever the selection changes). Vanished guns are
// dropped twice a second, and every group gun is kept turned onto the target while one is set.
void UpdateGroup(int team, bool offline) {
    if (!g_state.battery) return;
    int selected = 0;
    if (g_api.SelectedDevice(selected) && selected > 0 && selected != g_state.lastSelected) {
        g_state.lastSelected = selected;
        std::vector<int> own;
        if (!InGroup(selected) && g_api.OwnWeapons(team, own) && std::find(own.begin(), own.end(), selected) != own.end()) {
            if (g_state.group.size() < kMaxGroup) {
                g_state.group.push_back(selected);
                Logf("battery: gun %d joined, %zu in the group", selected, g_state.group.size());
            } else {
                Logf("battery: full (%zu guns)", kMaxGroup);
            }
        }
    } else if (selected <= 0) {
        g_state.lastSelected = 0;  // nothing selected: the same gun selected again counts as new
    }
    if (g_state.group.empty()) return;
    if (++g_state.framesSincePrune >= 30) {
        g_state.framesSincePrune = 0;
        PruneGroup(team);
    }
    if (!offline) return;
    Vec3 pos;
    if (!GroupTarget(pos)) return;
    for (int id : g_state.group) g_api.AimWeaponAt(id, pos);
}

// FireWeapon (the game's own call, as the AI scripts use it) for every ready gun of the group, in one frame, at the same point.
// Beam guns are skipped: they need EndFireWeapon after a hold time, which is not handled here.
void GroupFire() {
    if (g_online) {
        Logf("group fire refused: it would desync a network game");
        return;
    }
    int team = 0;
    if (!g_api.LocalTeam(team)) {
        Logf("group fire: no local team (not in a match)");
        return;
    }
    PruneGroup(team);
    if (g_state.group.empty()) {
        Logf("group fire: the group is empty");
        return;
    }
    Vec3 target;
    if (!GroupTarget(target)) {
        Logf("group fire: no target (set a point under the cursor, or pick an enemy target)");
        return;
    }
    int fired = 0, notReady = 0, beams = 0, faulted = 0;
    std::string codes;
    for (int id : g_state.group) {
        bool ready = false;
        if (g_api.WeaponReady(id, ready) && !ready) {
            ++notReady;
            continue;
        }
        std::string projectile;
        float gravity = 0.0f;
        if (g_api.WeaponProjectile(id, team, projectile, gravity) && std::fabs(gravity) < 0.001f) {
            ++beams;
            continue;
        }
        int result = 0, contextBefore = 0;
        if (!g_api.FireWeaponAt(id, target, cfg::kFireFlagNormal, result, contextBefore)) {
            ++faulted;
            continue;
        }
        ++fired;
        codes += " " + std::to_string(id) + ":" + std::to_string(result);
        if (fired == 1) codes += "(script team context was " + std::to_string(contextBefore) + ")";
    }
    Logf("group fire at (%.0f, %.0f): %d called, %d not ready, %d beam gun(s) skipped, %d faulted; raw FIRE_* codes (gun:code)%s",
         target.x, target.y, fired, notReady, beams, faulted, codes.c_str());
}

// "Battery: target + fire": the point under the mouse becomes the target of the whole group, which fires a salvo at it. Turns the
// battery on when it is off (the group then starts empty).
void BatteryTarget() {
    if (g_online) {
        Logf("battery target refused: it would desync a network game");
        return;
    }
    if (!g_state.battery) SetBattery(true);
    Vec3 screen{}, world{};
    if (!g_api.MouseWorld(screen, world)) {
        Logf("battery: the mouse position could not be read");
        return;
    }
    g_state.groupPoint = world;
    g_state.groupPointSet = true;
    Logf("battery: target point (%.0f, %.0f), %zu gun(s) in the group", world.x, world.y, g_state.group.size());
    GroupFire();
}

// ---------------------------------------------------------------- drawing: trajectories, enemies, radar
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

void Ring(float cx, float cy, float radius, float r, float g, float b, float a, float width) {
    float pts[2 * 25];
    for (int i = 0; i <= 24; ++i) {
        const float t = static_cast<float>(i) * 6.2831853f / 24.0f;
        pts[2 * i] = cx + std::cos(t) * radius;
        pts[2 * i + 1] = cy + std::sin(t) * radius;
    }
    g_overlay.Line(pts, 25, r, g, b, a, width);
}

void Cross(float cx, float cy, float half, float r, float g, float b, float a, float width) {
    const float l1[4] = {cx - half, cy - half, cx + half, cy + half}, l2[4] = {cx - half, cy + half, cx + half, cy - half};
    g_overlay.Line(l1, 2, r, g, b, a, width);
    g_overlay.Line(l2, 2, r, g, b, a, width);
}

// A caption pill centred on (x, y), rendered once per distinct text and kept as a texture.
void DrawLabel(const std::wstring& text, COLORREF accent, float x, float y) {
    std::string key = "t:" + std::to_string(accent) + ":";
    for (wchar_t c : text) key += c < 0x80 ? static_cast<char>(c) : '?';
    auto it = g_labels.find(key);
    if (it == g_labels.end()) {
        if (g_labels.size() > 300) g_labels.clear();
        HudImage img;
        RenderLabel(text, accent, g_uiScale, img);
        it = g_labels.emplace(key, std::move(img)).first;
    }
    g_overlay.Image(("lbl:" + key).c_str(), it->second.px.data(), it->second.w, it->second.h, false, x - static_cast<float>(it->second.w) / 2, y);
}

// Width (unscaled pixels) of a key / value panel that shows its longest value in full (Consolas 13 px is about 7.7 px per character).
float PanelWidth(const std::vector<PanelLine>& lines) {
    std::size_t longest = 0;
    for (const PanelLine& l : lines) longest = std::max(longest, l.value.size());
    return std::clamp(160.0f + 7.7f * static_cast<float>(longest), 300.0f, 700.0f);
}

// ---------------------------------------------------------------- terrain and water
// Terrain is asked through the game's own CastGroundRay; water is a separate surface (GetWaterLevel(x): the shell is in the
// water when its y is at least the level, deeper = larger y), so a shell over the sea ends on the surface, not on the sea bed.
unsigned long long g_rayCalls = 0, g_rayTicks = 0;
bool g_rayLogged1 = false, g_rayLogged2 = false;

bool GroundSegment(Vec2f a, Vec2f b, Vec2f& hit) {
    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    bool touched = false;
    Vec3 p;
    const bool ok = g_api.GroundRay({a.x, a.y, 0.0f}, {b.x, b.y, 0.0f}, touched, p);
    QueryPerformanceCounter(&t1);
    g_rayTicks += static_cast<unsigned long long>(t1.QuadPart - t0.QuadPart);
    ++g_rayCalls;
    if ((!g_rayLogged1 && g_rayCalls >= 2000) || (!g_rayLogged2 && g_rayCalls >= 200000)) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        Logf("terrain ray: %llu casts, %.2f us per cast", g_rayCalls,
             1.0e6 * static_cast<double>(g_rayTicks) / static_cast<double>(f.QuadPart) / static_cast<double>(g_rayCalls));
        if (g_rayCalls >= 200000) g_rayLogged2 = true;
        else g_rayLogged1 = true;
    }
    if (!ok || !touched) return false;
    hit = {p.x, p.y};
    return true;
}

constexpr float kNoWater = 2.14748365e9f;  // what GetWaterLevel returns on a map without water
float WaterAt(float x) {
    float level = 0;
    return g_api.WaterLevelAt(x, level) ? level : kNoWater;
}
float NoWaterAt(float) { return kNoWater; }

constexpr int kTraceCap = 521;  // 20 s of physics frames + the start
constexpr float kTraceMaxTime = 20.0f, kTraceGrace = 30.0f;
constexpr int kTraceChord = 3;  // physics frames per terrain ray (TraceShot): the chord leaves the true path by at most 1.8 units

// Shot traces of our own guns are cached: while a gun does not move, the same arc is asked every frame.
struct CachedTrace {
    float key[6] = {};
    bool beam = false;
    TraceResult res;
    std::vector<Vec2f> pts;
};
std::vector<CachedTrace> g_traceCache;
std::size_t g_traceNext = 0;

const CachedTrace& TraceCached(Vec2f p, Vec2f v, float gravityY, float drag, bool beam) {
    const float key[6] = {p.x, p.y, v.x, v.y, gravityY, drag};
    for (const CachedTrace& c : g_traceCache)
        if (c.beam == beam && std::memcmp(c.key, key, sizeof(key)) == 0) return c;
    if (g_traceCache.capacity() < 160) g_traceCache.reserve(160);
    CachedTrace* slot;
    if (g_traceCache.size() < 160) {
        g_traceCache.emplace_back();
        slot = &g_traceCache.back();
    } else {
        slot = &g_traceCache[g_traceNext++ % g_traceCache.size()];
    }
    std::memcpy(slot->key, key, sizeof(key));
    slot->beam = beam;
    slot->pts.assign(kTraceCap, Vec2f{});
    // Beams (gravity 0) are drawn for one second of beam travel and stop at terrain only; shells fly until terrain or water.
    slot->res = beam ? TraceShot(p, v, 0.0f, 0.0f, 1.0f, kTraceGrace, kTraceChord, GroundSegment, NoWaterAt, slot->pts.data(), kTraceCap)
                     : TraceShot(p, v, gravityY, drag, kTraceMaxTime, kTraceGrace, kTraceChord, GroundSegment, WaterAt, slot->pts.data(), kTraceCap);
    slot->pts.resize(static_cast<std::size_t>(slot->res.n));
    return *slot;
}

void DrawTrajectories(int team) {
    std::vector<int> ids;
    if (!g_api.OwnWeapons(team, ids) || ids.empty()) return;
    float line[2 * kTraceCap];
    int labels = 0;
    const float s = g_uiScale;
    for (int id : ids) {
        std::string type = "?";
        g_api.DeviceTypeName(id, type);
        if (!ShowType(type)) continue;
        Vec3 p, v;
        if (!g_api.BarrelPosition(id, p) || !g_api.FireVelocity(id, v)) continue;
        const float gravityMag = TypeGravity(id, team, type);
        if (g_propelled.count(type)) continue;  // thrust-driven shells (missiles): the ballistic model does not describe them
        const bool beam = gravityMag == 0.0f;
        const float gravityY = g_cal.gravitySign * gravityMag;
        const CachedTrace& tr = TraceCached({p.x, p.y}, {v.x, v.y}, gravityY, beam ? 0.0f : DragFor(type), beam);
        const int n = tr.res.n;
        int m = 0;
        for (int i = 0; i < n; ++i) {
            float x, y;
            if (!WorldToPixels({tr.pts[static_cast<std::size_t>(i)].x, tr.pts[static_cast<std::size_t>(i)].y, p.z}, x, y)) continue;
            line[2 * m] = x;
            line[2 * m + 1] = y;
            ++m;
        }
        if (beam) g_overlay.Line(line, m, 0.4f, 0.9f, 1.0f, 0.9f, 2.0f);
        else g_overlay.Line(line, m, 1.0f, 0.85f, 0.1f, 0.9f, 2.0f);
        // Landing point: yellow on terrain, blue on water. The flight time is written for the first few guns only.
        if (tr.res.kind != ImpactKind::None) {
            float x, y;
            if (WorldToPixels({tr.res.point.x, tr.res.point.y, p.z}, x, y)) {
                const bool water = tr.res.kind == ImpactKind::Water;
                const float cr = water ? 0.35f : 1.0f, cg = water ? 0.72f : 0.85f, cb = water ? 1.0f : 0.1f;
                Ring(x, y, 8.0f * s, cr, cg, cb, 1.0f, 2.5f);
                Cross(x, y, 4.0f * s, cr, cg, cb, 1.0f, 2.0f);
                if (!beam && labels < 3) {
                    ++labels;
                    wchar_t t[24];
                    swprintf_s(t, L"%.1f s%s", tr.res.time, water ? L" (water)" : L"");
                    DrawLabel(t, water ? aero::ui::kAccent : aero::ui::kWarn, x, y + 12.0f * s);
                }
            }
        }
        if (g_state.impact) {
            for (const EnemyDev& e : g_enemy)
                if (DistanceToPolyline({e.pos.x, e.pos.y}, tr.pts.data(), n) < 70.0f) g_hitIds.insert(e.id);
        }
    }
}

// Group markers: a numbered ring on every group gun, a thin line from each to the group target, a cross on the target.
void DrawGroup() {
    if (g_state.group.empty() && !g_state.groupPointSet) return;
    Vec3 target{};
    float tx = 0, ty = 0;
    const bool targetShown = GroupTarget(target) && WorldToPixels(target, tx, ty);
    int n = 0;
    for (int id : g_state.group) {
        ++n;
        Vec3 pos;
        float x = 0, y = 0;
        if (!g_api.DevicePosition(id, pos) || !WorldToPixels(pos, x, y)) continue;
        Ring(x, y, 14.0f * g_uiScale, 0.2f, 0.9f, 0.6f, 0.95f, 2.0f);
        DrawLabel(L"G" + std::to_wstring(n), aero::ui::kOk, x, y + 18.0f * g_uiScale);
        if (targetShown) {
            const float line[4] = {x, y, tx, ty};
            g_overlay.Line(line, 2, 0.2f, 0.9f, 0.6f, 0.35f, 1.0f);
        }
    }
    if (targetShown) {
        Cross(tx, ty, 16.0f * g_uiScale, 1.0f, 0.35f, 0.3f, 1.0f, 2.5f);
        Ring(tx, ty, 26.0f * g_uiScale, 1.0f, 0.35f, 0.3f, 0.9f, 2.0f);
    }
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
        if (g_state.timers && e.weapon && e.timerOk) {  // reload bar along the bottom edge of the box
            const float by = y + half - 5.0f * s, bh = 4.0f * s;
            g_overlay.Rect(x - half, by, 2 * half, bh, 0.05f, 0.06f, 0.08f, 0.85f);
            if (e.ready) {
                g_overlay.Rect(x - half, by, 2 * half, bh, 0.2f, 0.83f, 0.6f, 1.0f);
            } else if (g_reloadSemantics != 0 && e.reloadPeriod > 0.05f) {
                const float ratio = e.reloadValue / e.reloadPeriod;
                const float frac = std::clamp(g_reloadSemantics == 1 ? 1.0f - ratio : ratio, 0.0f, 1.0f);
                g_overlay.Rect(x - half, by, 2 * half * frac, bh, 0.38f, 0.65f, 0.98f, 1.0f);
            } else {
                g_overlay.Rect(x - half, by, 2 * half, bh, 0.98f, 0.75f, 0.14f, 0.6f);  // reloading, progress direction not learned yet
            }
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
    float x = 0, y = 0;
    PlacePanel("intel", static_cast<float>(vw - g_intelImage.w) - 24.0f * g_uiScale, 100.0f * g_uiScale + below, g_intelImage.w, g_intelImage.h, x, y);
    g_overlay.Image("intel", g_intelImage.px.data(), g_intelImage.w, g_intelImage.h, g_intelFresh, x, y);
    g_intelFresh = false;
}

void DrawRadar(int vw) {
    if (g_radarImage.px.empty()) return;
    float x = 0, y = 0;
    PlacePanel("radar", static_cast<float>(vw - g_radarImage.w) - 24.0f * g_uiScale, 100.0f * g_uiScale, g_radarImage.w, g_radarImage.h, x, y);
    g_overlay.Image("radar", g_radarImage.px.data(), g_radarImage.w, g_radarImage.h, g_radarFresh, x, y);
    g_radarFresh = false;
}

// ---------------------------------------------------------------- incoming shells
// Every enemy shell in the air is followed to its landing point (terrain or water); if that point is close to one of our
// devices the shell is a threat. The radius is NOT calibrated against the real splash of each ammunition.
constexpr float kThreatRadius = 90.0f;
struct Threat {
    Vec2f impact, device;
    float eta = 0;
    ImpactKind kind = ImpactKind::Ground;
    int danger = 1;  // 1 any device, 2 a weapon, 3 the reactor
    std::vector<Vec2f> arc;
};
std::vector<Threat> g_threats;
struct ThreatTrack {
    float vx = 0, vy = 0;
    double t = 0;
    float k = -1;           // exact drag measured on this very shell, -1 = not yet (no drag is assumed until then)
    int good = 0, bad = 0;  // velocity pairs that follow / break the frame model (thrust, shields, collisions)
};
std::map<int, ThreatTrack> g_threatTracks;
ULONGLONG g_lastBeep = 0;

void UpdateThreats() {
    static ULONGLONG last = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - last < 100) return;
    last = now;
    g_threats.clear();
    std::vector<Vec2f> ownPos;
    ownPos.reserve(g_own.size());
    for (const OwnDev& o : g_own) ownPos.push_back({o.pos.x, o.pos.y});
    if (ownPos.empty()) return;
    const double tnow = NowSeconds();
    std::set<int> seen;
    int processed = 0;
    Vec2f pts[320];  // 12 s of physics frames
    for (int code : g_enemyCodes) {
        std::vector<int> ids;
        if (!g_api.ProjectileIds(code, ids)) continue;
        for (int id : ids) {
            if (!seen.insert(id).second) continue;  // the game may list a side's shells under every team code of that side
            if (++processed > 40) break;
            Vec3 p, v;
            float grav = 0;
            if (!g_api.NodePos(id, p) || !g_api.NodeVel(id, v)) continue;
            g_api.ProjectileGravity(id, grav);
            ThreatTrack& tt = g_threatTracks[id];
            const float gMag = std::fabs(grav);
            const bool beam = gMag < 0.001f;
            if (tt.t > 0 && !beam && (v.x != tt.vx || v.y != tt.vy)) {  // the velocity only changes on physics frames
                const DragEstimate e = EstimateDragFrames(tt.vx, tt.vy, v.x, v.y, g_cal.gravitySign * gMag);
                if (e.ok) {
                    tt.k = e.k;
                    ++tt.good;
                } else {
                    ++tt.bad;
                }
            }
            tt.vx = v.x;
            tt.vy = v.y;
            tt.t = tnow;
            if (tt.bad >= 3 && tt.bad > tt.good) continue;  // thrust-driven or deflected: the frame model does not describe it
            const float drag = beam ? 0.0f : std::max(tt.k, 0.0f);
            const TraceResult r = TraceShot({p.x, p.y}, {v.x, v.y}, g_cal.gravitySign * gMag, drag, 12.0f, 0.0f, 4, GroundSegment,
                                            beam ? NoWaterAt : WaterAt, pts, 320);
            if (r.kind == ImpactKind::None) continue;  // does not land within 12 s
            float d = 0;
            const int idx = NearestWithin(r.point, ownPos, kThreatRadius, d);
            if (idx < 0) continue;
            const OwnDev& o = g_own[static_cast<std::size_t>(idx)];
            Threat th;
            th.impact = r.point;
            th.device = {o.pos.x, o.pos.y};
            th.eta = r.time;
            th.kind = r.kind;
            th.danger = o.type == "reactor" ? 3 : (o.weapon ? 2 : 1);
            th.arc.assign(pts, pts + r.n);
            g_threats.push_back(std::move(th));
        }
    }
    for (auto it = g_threatTracks.begin(); it != g_threatTracks.end();) it = seen.count(it->first) ? std::next(it) : g_threatTracks.erase(it);
    std::sort(g_threats.begin(), g_threats.end(), [](const Threat& a, const Threat& b) {
        if (a.danger != b.danger) return a.danger > b.danger;
        return a.eta < b.eta;
    });
    if (g_threats.size() > 12) g_threats.resize(12);
    if (!g_threats.empty() && g_threats[0].danger == 3 && g_threats[0].eta < 4.0f && now - g_lastBeep > 1500) {
        g_lastBeep = now;
        MessageBeep(MB_ICONEXCLAMATION);  // the reactor is about to be hit
    }
}

void DrawThreats(int vw) {
    const float s = g_uiScale;
    float line[2 * 330];
    for (const Threat& t : g_threats) {
        const bool reactor = t.danger == 3, weapon = t.danger == 2;
        const float cr = 1.0f, cg = reactor ? 0.25f : (weapon ? 0.55f : 0.85f), cb = reactor ? 0.25f : (weapon ? 0.15f : 0.2f);
        int m = 0;
        for (const Vec2f& q : t.arc) {
            float x, y;
            if (!WorldToPixels({q.x, q.y, 0.0f}, x, y)) continue;
            line[2 * m] = x;
            line[2 * m + 1] = y;
            if (++m >= 330) break;
        }
        g_overlay.Line(line, m, cr, cg, cb, 0.55f, 1.5f);
        float ix, iy;
        if (WorldToPixels({t.impact.x, t.impact.y, 0.0f}, ix, iy)) {
            float radius = 18.0f * s, ex, ey;
            if (WorldToPixels({t.impact.x + kThreatRadius, t.impact.y, 0.0f}, ex, ey)) radius = std::max(8.0f * s, std::fabs(ex - ix));
            Ring(ix, iy, radius, cr, cg, cb, 0.9f, 2.5f);
            Cross(ix, iy, 6.0f * s, cr, cg, cb, 1.0f, 2.5f);
            wchar_t txt[24];
            swprintf_s(txt, L"%.1f s", t.eta);
            DrawLabel(txt, reactor ? aero::ui::kBad : aero::ui::kWarn, ix, iy - radius - 22.0f * s);
        }
        float dx, dy;
        if (WorldToPixels({t.device.x, t.device.y, 0.0f}, dx, dy)) Ring(dx, dy, 16.0f * s, cr, cg, cb, 1.0f, 3.0f);
    }
    if (g_threats.empty()) return;
    const Threat& first = g_threats[0];
    wchar_t banner[64];
    swprintf_s(banner, L"%s  %.1f s  (%zu incoming)", first.danger == 3 ? L"REACTOR UNDER FIRE" : (first.danger == 2 ? L"GUN UNDER FIRE" : L"INCOMING"),
               first.eta, g_threats.size());
    DrawLabel(banner, first.danger == 3 ? aero::ui::kBad : aero::ui::kWarn, static_cast<float>(vw) / 2.0f, 70.0f * s);
}

// ---------------------------------------------------------------- aim calculator
// For the enemy device chosen in the Enemy tab: the elevation each shown weapon type needs (flat arc and lob), whether the arc is
// free of terrain and water on the way, and the arc itself. Speed, gravity and drag are the ones used for the trajectory overlay.
struct AimLine {
    std::string type;
    bool nowOk = false;
    float nowDeg = 0;
    int n = 0;
    AimSolution sol[2];
};
std::vector<AimLine> g_aimLines;
std::vector<Vec2f> g_aimGhost;
HudImage g_aimImage;
std::wstring g_aimSig;
bool g_aimFresh = false;
constexpr double kDegToRad = 0.017453292519943295;

void UpdateAimCalc(int team) {
    static ULONGLONG last = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - last < 120) return;
    last = now;
    g_aimLines.clear();
    g_aimGhost.clear();
    std::vector<int> ids;
    if (!g_api.OwnWeapons(team, ids)) return;
    // The gun the player has selected (clicked) is the one the angle is computed for; with no gun selected every shown type is listed.
    int selected = 0;
    const bool gunMode = g_api.SelectedDevice(selected) && selected > 0 && std::find(ids.begin(), ids.end(), selected) != ids.end();
    if (gunMode) ids.assign(1, selected);
    Vec3 tpos;
    if (g_state.aimTarget == 0 || !g_api.DevicePosition(g_state.aimTarget, tpos)) {
        std::vector<PanelLine> hint;
        hint.push_back({Tr(L"Target", L"Цель"), Tr(L"not chosen", L"не выбрана"), aero::ui::kWarn});
        hint.push_back({Tr(L"How", L"Как"), (std::wstring(Tr(L"point at an enemy gun and press ", L"наведите мышь на вражеское орудие и нажмите ")) + BindLabel(kItemPickTarget)).c_str(), 0});
        hint.push_back({Tr(L"Gun", L"Орудие"), gunMode ? Tr(L"the selected one", L"выбранное вами") : Tr(L"select a gun first", L"сначала выберите орудие"), 0});
        std::wstring sig;
        for (const PanelLine& pl : hint) sig += pl.key + L"|" + pl.value + L";";
        if (sig != g_aimSig) {
            g_aimSig = sig;
            RenderPanel(Tr(L"Aim calculator", L"Калькулятор прицела"), hint, g_uiScale, g_aimImage, PanelWidth(hint));
            g_aimFresh = true;
        }
        return;
    }
    std::set<std::string> done;
    for (int id : ids) {
        std::string type;
        if (!g_api.DeviceTypeName(id, type) || (!gunMode && !ShowType(type)) || !done.insert(type).second) continue;
        Vec3 p, v;
        if (!g_api.BarrelPosition(id, p) || !g_api.FireVelocity(id, v)) continue;
        const float gMag = TypeGravity(id, team, type);
        if (gMag == 0.0f || g_propelled.count(type)) continue;  // beams fly straight, missiles are thrust-driven: nothing to solve
        const float gY = g_cal.gravitySign * gMag, drag = DragFor(type);
        const float speed = std::sqrt(v.x * v.x + v.y * v.y);
        const float dirX = tpos.x >= p.x ? 1.0f : -1.0f;
        AimLine L;
        L.type = type;
        L.nowOk = v.x * dirX > 0.0f;
        L.nowDeg = ElevationDeg({v.x, v.y}, gY);
        const double up = gY >= 0.0f ? -1.0 : 1.0;
        auto velocityFor = [&](double e) {
            return Vec2f{static_cast<float>(dirX * speed * std::cos(e)), static_cast<float>(up * speed * std::sin(e))};
        };
        auto clear = [&](double e, double t) {
            Vec2f buf[kTraceCap];
            const TraceResult r = TraceShot({p.x, p.y}, velocityFor(e), gY, drag, std::max(0.2f, static_cast<float>(t) - 0.1f), kTraceGrace, kTraceChord,
                                            GroundSegment, WaterAt, buf, kTraceCap);
            return r.kind == ImpactKind::None;
        };
        L.n = SolveAim({p.x, p.y}, speed, gY, drag, {tpos.x, tpos.y}, clear, L.sol);
        if (g_aimGhost.empty() && L.n > 0) {  // the arc of the first weapon type, preferring a free one
            const AimSolution* best = &L.sol[0];
            for (int i = 0; i < L.n; ++i)
                if (L.sol[i].clear) {
                    best = &L.sol[i];
                    break;
                }
            Vec2f buf[kTraceCap];
            const TraceResult r = TraceShot({p.x, p.y}, velocityFor(best->elevationDeg * kDegToRad), gY, drag, best->time + 0.15f, kTraceGrace, kTraceChord,
                                            GroundSegment, WaterAt, buf, kTraceCap);
            g_aimGhost.assign(buf, buf + r.n);
        }
        g_aimLines.push_back(std::move(L));
        if (g_aimLines.size() >= 4) break;
    }

    std::vector<PanelLine> lines;
    wchar_t b[96];
    {
        const std::string& tt = TypeOf(g_state.aimTarget);
        swprintf_s(b, L"%s #%d", std::wstring(tt.begin(), tt.end()).c_str(), g_state.aimTarget);
        lines.push_back({Tr(L"Target", L"Цель"), b, 0});
    }
    if (gunMode) {
        const std::wstring selName(L"#" + std::to_wstring(selected));
        lines.push_back({Tr(L"Selected gun", L"Выбранное орудие"), selName, 0});
    } else {
        lines.push_back({Tr(L"Tip", L"Совет"), Tr(L"select a gun to see only its angle", L"выберите орудие, чтобы видеть только его угол"), 0});
    }
    if (g_aimLines.empty()) lines.push_back({Tr(L"Guns", L"Орудия"), Tr(L"no arc weapons", L"нет орудий с дугой"), 0});
    const wchar_t* blocked = Tr(L"  blocked", L"  мешает рельеф");
    for (const AimLine& L : g_aimLines) {
        const std::wstring name(L.type.begin(), L.type.end());
        if (L.n == 0) {
            lines.push_back({name, Tr(L"out of range", L"вне дальности"), aero::ui::kBad});
            continue;
        }
        for (int i = 0; i < L.n; ++i) {
            if (i == 0) {
                if (L.nowOk) swprintf_s(b, L"%.1f° (%s %.1f°)%s", L.sol[i].elevationDeg, Tr(L"now", L"сейчас"), L.nowDeg, L.sol[i].clear ? L"" : blocked);
                else swprintf_s(b, L"%.1f° (%s)%s", L.sol[i].elevationDeg, Tr(L"aim the other way", L"развернитесь"), L.sol[i].clear ? L"" : blocked);
                lines.push_back({name, b, L.sol[i].clear ? 0 : aero::ui::kBad});
            } else {
                swprintf_s(b, L"%.1f°  %.1f s%s", L.sol[i].elevationDeg, L.sol[i].time, L.sol[i].clear ? L"" : blocked);
                lines.push_back({Tr(L"  lob", L"  навесом"), b, L.sol[i].clear ? 0 : aero::ui::kBad});
            }
        }
    }
    std::wstring sig;
    for (const PanelLine& pl : lines) sig += pl.key + L"|" + pl.value + L";";
    if (sig != g_aimSig) {
        g_aimSig = sig;
        RenderPanel(Tr(L"Aim calculator", L"Калькулятор прицела"), lines, g_uiScale, g_aimImage, PanelWidth(lines));
        g_aimFresh = true;
    }
}

void DrawAimGhost() {
    if (g_aimGhost.size() < 2) return;
    float line[2 * kTraceCap];
    int m = 0;
    for (const Vec2f& q : g_aimGhost) {
        float x, y;
        if (!WorldToPixels({q.x, q.y, 0.0f}, x, y)) continue;
        line[2 * m] = x;
        line[2 * m + 1] = y;
        if (++m >= kTraceCap) break;
    }
    g_overlay.Line(line, m, 0.35f, 0.95f, 0.5f, 0.95f, 2.5f);
}

// ---------------------------------------------------------------- weak links of a base
// The nodes a base is anchored by (the terrain block recorded in each node), and the links between them, form a support graph.
// A link whose loss leaves a piece with no path to the ground is a weak link; the weight of what hangs on it is estimated from
// the devices sitting on it (reactor 1000, weapon 120, anything else 10: priorities, not measured values).
struct WeakDraw {
    Vec2f a, b;
    double value = 0;
    int links = 0;
    std::wstring what;
};
std::vector<WeakDraw> g_weakEnemy, g_weakOwn;
std::wstring g_weakNoteEnemy, g_weakNoteOwn;
HudImage g_weakImage;
std::wstring g_weakSig;
bool g_weakFresh = false;

struct BaseDev {
    Vec2f pos;
    std::string type;
    bool weapon = false;
};

double DeviceWeight(const std::string& type, bool weapon) { return type == "reactor" ? 1000.0 : (weapon ? 120.0 : 10.0); }

std::wstring Describe(int reactors, int guns, int others) {
    std::wstring s;
    auto add = [&](const std::wstring& part) { s += (s.empty() ? L"" : L" + ") + part; };
    if (reactors) add(L"reactor");
    if (guns) add(std::to_wstring(guns) + (guns == 1 ? L" gun" : L" guns"));
    if (others) add(std::to_wstring(others) + (others == 1 ? L" device" : L" devices"));
    return s.empty() ? std::wstring(L"structure only") : s;
}

void AnalyseBase(int code, const std::vector<BaseDev>& devs, std::vector<WeakDraw>& out, std::wstring& note) {
    out.clear();
    note.clear();
    BaseGraph graph;
    if (code == 0 || !g_api.BaseGraphOf(code, graph)) {
        note = L"base not readable";
        return;
    }
    const int n = static_cast<int>(graph.nodes.size());
    std::vector<char> anchored(static_cast<std::size_t>(n), 0);
    int anchoredCount = 0;
    for (int i = 0; i < n; ++i) {
        anchored[static_cast<std::size_t>(i)] = graph.nodes[static_cast<std::size_t>(i)].anchored ? 1 : 0;
        anchoredCount += anchored[static_cast<std::size_t>(i)];
    }
    if (anchoredCount == 0) {
        note = L"no ground-anchored nodes found";  // the anchor field of the nodes is an unverified assumption
        return;
    }
    std::vector<std::pair<int, int>> edges;
    edges.reserve(graph.edges.size());
    for (const GraphEdge& e : graph.edges) edges.push_back({e.a, e.b});
    std::vector<double> weight(edges.size(), 0.0);
    std::vector<int> attach(devs.size(), -1);
    for (std::size_t i = 0; i < devs.size(); ++i) {  // a device hangs on the nearest link (within 60 units)
        float best = 60.0f;
        int bi = -1;
        for (std::size_t j = 0; j < edges.size(); ++j) {
            const GraphNode& na = graph.nodes[static_cast<std::size_t>(edges[j].first)];
            const GraphNode& nb = graph.nodes[static_cast<std::size_t>(edges[j].second)];
            const float d = DistanceToSegment(devs[i].pos, {na.x, na.y}, {nb.x, nb.y});
            if (d < best) {
                best = d;
                bi = static_cast<int>(j);
            }
        }
        attach[i] = bi;
        if (bi >= 0) weight[static_cast<std::size_t>(bi)] += DeviceWeight(devs[i].type, devs[i].weapon);
    }
    for (const WeakLink& w : FindWeakLinks(n, anchored, edges, weight, 5)) {
        const std::vector<char> lost = LostLinks(n, anchored, edges, w.edge);
        int reactors = 0, guns = 0, others = 0;
        for (std::size_t i = 0; i < devs.size(); ++i) {
            if (attach[i] < 0 || !lost[static_cast<std::size_t>(attach[i])]) continue;
            if (devs[i].type == "reactor") ++reactors;
            else if (devs[i].weapon) ++guns;
            else ++others;
        }
        const GraphEdge& e = graph.edges[static_cast<std::size_t>(w.edge)];
        WeakDraw d;
        d.a = {graph.nodes[static_cast<std::size_t>(e.a)].x, graph.nodes[static_cast<std::size_t>(e.a)].y};
        d.b = {graph.nodes[static_cast<std::size_t>(e.b)].x, graph.nodes[static_cast<std::size_t>(e.b)].y};
        d.value = w.value;
        d.links = w.links;
        d.what = Describe(reactors, guns, others) + L", " + std::to_wstring(w.links) + (w.links == 1 ? L" link" : L" links");
        out.push_back(std::move(d));
    }
    if (out.empty()) note = L"no single link cuts anything";
}

void UpdateWeak(int team) {
    static ULONGLONG last = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - last < 1500) return;
    last = now;
    if (g_state.weakEnemy) {
        std::vector<BaseDev> devs;
        for (const EnemyDev& e : g_enemy) devs.push_back({{e.pos.x, e.pos.y}, e.type, e.weapon});
        AnalyseBase(g_enemyTeam, devs, g_weakEnemy, g_weakNoteEnemy);
    } else {
        g_weakEnemy.clear();
        g_weakNoteEnemy.clear();
    }
    if (g_state.weakOwn) {
        std::vector<BaseDev> devs;
        for (const OwnDev& o : g_own) devs.push_back({{o.pos.x, o.pos.y}, o.type, o.weapon});
        AnalyseBase(team, devs, g_weakOwn, g_weakNoteOwn);
    } else {
        g_weakOwn.clear();
        g_weakNoteOwn.clear();
    }
    std::vector<PanelLine> lines;
    auto section = [&](const wchar_t* who, const std::vector<WeakDraw>& list, const std::wstring& note) {
        for (std::size_t i = 0; i < list.size(); ++i) lines.push_back({std::wstring(who) + L" #" + std::to_wstring(i + 1), list[i].what, i == 0 ? aero::ui::kWarn : 0});
        if (list.empty() && !note.empty()) lines.push_back({who, note, 0});
    };
    if (g_state.weakEnemy) section(L"Enemy", g_weakEnemy, g_weakNoteEnemy);
    if (g_state.weakOwn) section(L"Yours", g_weakOwn, g_weakNoteOwn);
    std::wstring sig;
    for (const PanelLine& pl : lines) sig += pl.key + L"|" + pl.value + L";";
    if (lines.empty()) {
        g_weakImage = HudImage{};
        g_weakSig.clear();
    } else if (sig != g_weakSig) {
        g_weakSig = sig;
        RenderPanel(L"Weak links", lines, g_uiScale, g_weakImage, PanelWidth(lines));
        g_weakFresh = true;
    }
}

void DrawWeak() {
    const float s = g_uiScale;
    auto draw = [&](const std::vector<WeakDraw>& list, float r, float g, float b) {
        for (std::size_t i = 0; i < list.size(); ++i) {
            float x1, y1, x2, y2;
            if (!WorldToPixels({list[i].a.x, list[i].a.y, 0.0f}, x1, y1) || !WorldToPixels({list[i].b.x, list[i].b.y, 0.0f}, x2, y2)) continue;
            const float seg[4] = {x1, y1, x2, y2};
            g_overlay.Line(seg, 2, r, g, b, i == 0 ? 1.0f : 0.8f, i == 0 ? 6.0f : 4.0f);
            DrawLabel(L"#" + std::to_wstring(i + 1), aero::ui::kWarn, (x1 + x2) / 2.0f, (y1 + y2) / 2.0f - 26.0f * s);
        }
    };
    if (g_state.weakEnemy) draw(g_weakEnemy, 1.0f, 0.2f, 0.85f);
    if (g_state.weakOwn) draw(g_weakOwn, 0.2f, 0.85f, 1.0f);
}

// ---------------------------------------------------------------- battle statistics (estimates)
// Shots are counted from our own projectiles appearing in the air; a shot is a "hit" when its last known point was within 90 units
// of an enemy device. Damage is the loss of hit points of devices between two refreshes (the game offers no damage counter),
// so repairs, rebuilt devices and damage by teammates are not told apart.
struct Stats {
    bool active = false;
    double start = 0;
    int shots = 0, hits = 0, misses = 0;
    double dealt = 0, taken = 0;
    int enemyLost = 0, ownLost = 0;
};
Stats g_stats;
struct ShotTrack {
    Vec2f pos, vel;
};
std::map<int, ShotTrack> g_shots;
std::map<int, float> g_hpEnemy, g_hpOwn;
HudImage g_statsImage;
std::wstring g_statsSig;
bool g_statsFresh = false;

void ResetStats() {
    g_stats = Stats{};
    g_stats.active = true;
    g_stats.start = NowSeconds();
    g_shots.clear();
    g_hpEnemy.clear();
    g_hpOwn.clear();
    g_statsSig.clear();
}

void TrackShots(int team) {
    std::vector<int> ids;
    if (!g_api.ProjectileIds(team, ids)) return;
    std::vector<Vec2f> enemyPos;
    enemyPos.reserve(g_enemy.size());
    for (const EnemyDev& e : g_enemy) enemyPos.push_back({e.pos.x, e.pos.y});
    std::set<int> alive;
    for (int id : ids) {
        Vec3 p, v;
        if (!g_api.NodePos(id, p) || !g_api.NodeVel(id, v)) continue;
        alive.insert(id);
        const auto it = g_shots.find(id);
        if (it == g_shots.end()) ++g_stats.shots;
        g_shots[id] = {{p.x, p.y}, {v.x, v.y}};
    }
    for (auto it = g_shots.begin(); it != g_shots.end();) {
        if (alive.count(it->first)) {
            ++it;
            continue;
        }
        const Vec2f end{it->second.pos.x + it->second.vel.x * 0.016f, it->second.pos.y + it->second.vel.y * 0.016f};  // about one frame further
        float d = 0;
        if (NearestWithin(end, enemyPos, 90.0f, d) >= 0) ++g_stats.hits;
        else ++g_stats.misses;
        it = g_shots.erase(it);
    }
}

void UpdateDamage() {
    if (!g_worldFresh) return;
    g_worldFresh = false;
    std::map<int, float> curEnemy, curOwn;
    for (const EnemyDev& e : g_enemy) {
        float hp = 0, maxHp = 0;
        if (!g_api.DeviceHealth(e.id, hp, maxHp)) continue;
        curEnemy[e.id] = hp;
        const auto it = g_hpEnemy.find(e.id);
        if (it != g_hpEnemy.end() && hp < it->second) g_stats.dealt += it->second - hp;
    }
    for (const OwnDev& o : g_own) {
        float hp = 0, maxHp = 0;
        if (!g_api.DeviceHealth(o.id, hp, maxHp)) continue;
        curOwn[o.id] = hp;
        const auto it = g_hpOwn.find(o.id);
        if (it != g_hpOwn.end() && hp < it->second) g_stats.taken += it->second - hp;
    }
    if (!g_hpEnemy.empty() && !curEnemy.empty())
        for (const auto& [id, hp] : g_hpEnemy)
            if (!curEnemy.count(id) && hp > 0.0f) ++g_stats.enemyLost;
    if (!g_hpOwn.empty() && !curOwn.empty())
        for (const auto& [id, hp] : g_hpOwn)
            if (!curOwn.count(id) && hp > 0.0f) ++g_stats.ownLost;
    g_hpEnemy.swap(curEnemy);
    g_hpOwn.swap(curOwn);
}

void RefreshStatsPanel() {
    if (!g_stats.active) return;
    const int secs = static_cast<int>(NowSeconds() - g_stats.start);
    wchar_t b[64];
    std::vector<PanelLine> lines;
    swprintf_s(b, L"%d:%02d", secs / 60, secs % 60);
    lines.push_back({L"Time", b, 0});
    lines.push_back({L"Shots fired", std::to_wstring(g_stats.shots), 0});
    const int done = g_stats.hits + g_stats.misses;
    if (done > 0) swprintf_s(b, L"%d  (%.0f%%)", g_stats.hits, 100.0 * g_stats.hits / done);
    else swprintf_s(b, L"-");
    lines.push_back({L"Hits (est.)", b, 0});
    swprintf_s(b, L"%.0f", g_stats.dealt);
    lines.push_back({L"Damage dealt (HP)", b, 0});
    swprintf_s(b, L"%.0f", g_stats.taken);
    lines.push_back({L"Damage taken (HP)", b, g_stats.taken > g_stats.dealt ? aero::ui::kBad : 0});
    lines.push_back({L"Enemy devices lost", std::to_wstring(g_stats.enemyLost), 0});
    lines.push_back({L"Your devices lost", std::to_wstring(g_stats.ownLost), 0});
    std::wstring sig;
    for (const PanelLine& pl : lines) sig += pl.key + L"|" + pl.value + L";";
    if (sig != g_statsSig) {
        g_statsSig = sig;
        RenderPanel(L"Battle stats (estimates)", lines, g_uiScale, g_statsImage, PanelWidth(lines));
        g_statsFresh = true;
    }
}

std::wstring StatsPath() {
    std::wstring p = IniPath();
    const std::size_t slash = p.find_last_of(L'\\');
    return slash == std::wstring::npos ? p : p.substr(0, slash + 1) + L"FortsStats.csv";
}

// Appends one line per match to FortsStats.csv next to the module.
void ExportStats() {
    if (!g_stats.active || (g_stats.shots == 0 && g_stats.dealt == 0 && g_stats.taken == 0)) {
        g_stats.active = false;
        return;
    }
    g_stats.active = false;
    const std::wstring path = StatsPath();
    FILE* f = nullptr;
    if (path.empty() || _wfopen_s(&f, path.c_str(), L"ab") != 0 || !f) return;
    std::fseek(f, 0, SEEK_END);
    if (std::ftell(f) == 0) std::fputs("time,duration_s,shots,hits_est,misses_est,damage_dealt_hp,damage_taken_hp,enemy_devices_lost,own_devices_lost\r\n", f);
    char stamp[32] = "";
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_s(&tmv, &t);
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tmv);
    std::fprintf(f, "%s,%.0f,%d,%d,%d,%.0f,%.0f,%d,%d\r\n", stamp, NowSeconds() - g_stats.start, g_stats.shots, g_stats.hits, g_stats.misses, g_stats.dealt,
                 g_stats.taken, g_stats.enemyLost, g_stats.ownLost);
    std::fclose(f);
    Logf("stats exported: %d shots, %d hits (est.), damage dealt %.0f, taken %.0f", g_stats.shots, g_stats.hits, g_stats.dealt, g_stats.taken);
}

// The panels stack in the left column.
void DrawLeftPanels() {
    const float s = g_uiScale;
    float y = 100.0f * s;
    const float x = 24.0f * s;
    auto put = [&](const char* key, HudImage& img, bool& fresh) {
        if (img.px.empty()) return;
        float ox = 0, oy = 0;
        PlacePanel(key, x, y, img.w, img.h, ox, oy);
        g_overlay.Image(key, img.px.data(), img.w, img.h, fresh, ox, oy);
        fresh = false;
        y += static_cast<float>(img.h) + 10.0f * s;
    };
    if (g_state.aimCalc) put("aimcalc", g_aimImage, g_aimFresh);
    if (g_state.weakEnemy || g_state.weakOwn) put("weak", g_weakImage, g_weakFresh);
    if (g_state.stats) put("stats", g_statsImage, g_statsFresh);
}

// ---------------------------------------------------------------- self-test
// Passive: it never fires, aims or changes anything. Once per match it logs what the game calls return (water, terrain ray cost,
// selected device, base graph anchors, weapon timers). For every shell that appears (ours, the AI's, other players') it predicts the
// path from the second observation on, and when the shell disappears it logs how far its last seen point was from that path and
// how much of the path was still left (a shell is last seen up to one physics frame, |v|*0.04, before it hits).
struct EvalShell {
    float vx = 0, vy = 0, k = -1;
    int good = 0, bad = 0, samples = 0, code = 0;
    bool predicted = false;
    std::vector<Vec2f> pts;
    ImpactKind kind = ImpactKind::None;
    float eta = 0;
    Vec2f first{}, last{}, lastVel{};
    double firstT = 0, lastT = 0, sumPath = 0, maxPath = 0;
    int pathN = 0;
    float grav = 0;
};
std::map<int, EvalShell> g_eval;
struct EvalSummary {
    int shells = 0, followed = 0, unmodelled = 0, noPrediction = 0;
    double sumPath = 0, worstPath = 0, sumRemain = 0;
    int landedWithinFrame = 0;
} g_evalSum;
int g_selfTestTeam = -1, g_selfTestTicks = 0, g_selfTestEnvAt = 0;  // EnvAt: 0 = not run yet, >0 = tick of the first run, -1 = both runs done

float PathDistance(const std::vector<Vec2f>& pts, Vec2f p, std::size_t* segment = nullptr) {
    float best = 1.0e30f;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        const float d = DistanceToSegment(p, pts[i], pts[i + 1]);
        if (d < best) {
            best = d;
            if (segment) *segment = i;
        }
    }
    return best;
}

// Length of the predicted path that lies after the projection of p on it.
float PathRemaining(const std::vector<Vec2f>& pts, Vec2f p, std::size_t seg) {
    if (seg + 1 >= pts.size()) return 0.0f;
    const Vec2f a = pts[seg], b = pts[seg + 1];
    const double dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
    const double t = len2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0) : 1.0;
    double rest = (1.0 - t) * std::sqrt(len2);
    for (std::size_t i = seg + 1; i + 1 < pts.size(); ++i) rest += detail::Dist(pts[i], pts[i + 1]);
    return static_cast<float>(rest);
}

void SelfTestEnvironment(int team) {
    Logf("selftest: ===== environment (team %d, online=%d) =====", team, g_online ? 1 : 0);
    Vec3 cam{};
    const bool haveCam = g_api.CameraFocus(cam);
    if (haveCam) {
        float level = 0;
        const bool ok = g_api.WaterLevelAt(cam.x, level);
        Logf("selftest: camera focus (%.0f, %.0f); water level at its x: %s%.1f%s", cam.x, cam.y, ok ? "" : "call failed ", ok ? level : 0.0f,
             ok && level > 1.0e9f ? " (= no water on this map)" : "");
        Vec3 scr{}, back{}, mouseScr{}, mouseWorld{};
        const Vec3 probe{cam.x + 700.0f, cam.y - 300.0f, cam.z};
        if (g_api.ToScreen(probe, scr) && g_api.ScreenToWorldPoint(scr, back))
            Logf("selftest: WorldToScreen -> ScreenToWorld round trip: world (%.1f, %.1f) -> screen (%.4f, %.4f) -> world (%.1f, %.1f)", probe.x, probe.y,
                 scr.x, scr.y, back.x, back.y);
        else
            Logf("selftest: WorldToScreen/ScreenToWorld round trip failed");
        if (g_api.MouseWorld(mouseScr, mouseWorld))
            Logf("selftest: mouse screen (%.4f, %.4f) -> world (%.1f, %.1f)", mouseScr.x, mouseScr.y, mouseWorld.x, mouseWorld.y);
        else
            Logf("selftest: GetMousePos failed");
        // Cost and sanity of the terrain ray: 300 horizontal 400-unit segments spread around the camera height.
        LARGE_INTEGER f, t0, t1;
        QueryPerformanceFrequency(&f);
        int hits = 0, fails = 0;
        QueryPerformanceCounter(&t0);
        for (int i = 0; i < 300; ++i) {
            bool hit = false;
            Vec3 p;
            const float y = cam.y - 1500.0f + 10.0f * static_cast<float>(i);
            if (!g_api.GroundRay({cam.x - 200.0f, y, 0.0f}, {cam.x + 200.0f, y, 0.0f}, hit, p)) ++fails;
            else if (hit) ++hits;
        }
        QueryPerformanceCounter(&t1);
        Logf("selftest: terrain ray: 300 casts, %d hit terrain, %d failed, %.2f us per cast", hits, fails,
             1.0e6 * static_cast<double>(t1.QuadPart - t0.QuadPart) / static_cast<double>(f.QuadPart) / 300.0);
    } else {
        Logf("selftest: camera focus not readable");
    }
    int selected = -2;
    if (g_api.SelectedDevice(selected)) {
        std::string type = "?";
        g_api.DeviceTypeName(selected, type);
        Logf("selftest: GetLocalSelectedDeviceId = %d (type %s)", selected, type.c_str());
    } else {
        Logf("selftest: GetLocalSelectedDeviceId faulted");
    }
    std::string types;
    for (const auto& [name, n] : g_typeCount) types += name + " x" + std::to_string(n) + "  ";
    Logf("selftest: own weapon types: %s", types.empty() ? "(none)" : types.c_str());
    Logf("selftest: own devices %zu, enemy devices %zu, enemy team codes %zu", g_own.size(), g_enemy.size(), g_enemyCodes.size());
    int shown = 0;
    for (const OwnDev& o : g_own) {
        if (!o.weapon || shown >= 4) continue;
        ++shown;
        bool ready = false;
        float value = 0, period = 0;
        const bool okR = g_api.WeaponReady(o.id, ready), okT = g_api.WeaponReload(o.id, value, period);
        Logf("selftest: own weapon id=%d type=%s ready=%d(%d) reload value=%.3f period=%.3f(%d)", o.id, o.type.c_str(), ready ? 1 : 0, okR ? 1 : 0, value,
             period, okT ? 1 : 0);
    }
    auto graph = [&](const char* name, int code) {
        BaseGraph bg;
        if (!g_api.BaseGraphOf(code, bg)) {
            Logf("selftest: base graph of %s (code %d): not readable or empty", name, code);
            return;
        }
        std::map<int, int> values;
        int anchored = 0;
        for (const GraphNode& n : bg.nodes) {
            ++values[n.ground];
            if (n.anchored) ++anchored;
        }
        std::string dist;
        int shownValues = 0;
        for (const auto& [value, n] : values) {
            if (++shownValues > 6) break;
            dist += std::to_string(value) + ":" + std::to_string(n) + " ";
        }
        Logf("selftest: base graph of %s (code %d): %zu nodes, %zu links, %d anchored; node+0x18 values (value:count) %s", name, code, bg.nodes.size(),
             bg.edges.size(), anchored, dist.c_str());
    };
    graph("own base", team);
    for (int code : g_enemyCodes) graph("enemy base", code);
    Logf("selftest: ===== end of environment =====");
}

void SelfTestFinish(int id, EvalShell& e) {
    ++g_evalSum.shells;
    if (e.samples < 3 || !e.predicted) {
        ++g_evalSum.noPrediction;
        return;
    }
    if (e.bad >= 3 && e.bad > e.good) {
        ++g_evalSum.unmodelled;
        return;
    }
    std::size_t seg = 0;
    PathDistance(e.pts, e.last, &seg);
    const float remaining = PathRemaining(e.pts, e.last, seg);
    const float frameStep = std::sqrt(e.lastVel.x * e.lastVel.x + e.lastVel.y * e.lastVel.y) * static_cast<float>(kSimFrame);
    const bool within = remaining <= frameStep * 1.5f + 5.0f;
    ++g_evalSum.followed;
    g_evalSum.sumPath += e.sumPath / std::max(1, e.pathN);
    g_evalSum.worstPath = std::max(g_evalSum.worstPath, e.maxPath);
    g_evalSum.sumRemain += remaining;
    if (within) ++g_evalSum.landedWithinFrame;
    Logf("selftest: shell id=%d code=%d flight=%.2fs k=%.4f predicted %s at (%.0f, %.0f) after %.2fs; last seen (%.0f, %.0f); off path: mean %.2f max %.2f; path left %.1f (frame step %.1f) -> %s",
         id, e.code, e.lastT - e.firstT, e.k, e.kind == ImpactKind::Water ? "water" : (e.kind == ImpactKind::Ground ? "ground" : "no landing"),
         e.pts.empty() ? 0.0f : e.pts.back().x, e.pts.empty() ? 0.0f : e.pts.back().y, e.eta, e.last.x, e.last.y,
         e.sumPath / std::max(1, e.pathN), e.maxPath, remaining, frameStep, within ? "ends where predicted" : "ENDED EARLY/LATE");
    if (g_evalSum.followed % 10 == 0)
        Logf("selftest: summary %d shells followed (%d unmodelled, %d too short): mean off-path %.2f, worst %.2f, %d of %d ended within one frame of the predicted landing",
             g_evalSum.followed, g_evalSum.unmodelled, g_evalSum.noPrediction, g_evalSum.sumPath / g_evalSum.followed, g_evalSum.worstPath,
             g_evalSum.landedWithinFrame, g_evalSum.followed);
}

void SelfTestTick(int team) {
    if (team != g_selfTestTeam) {
        g_selfTestTeam = team;
        g_selfTestTicks = 0;
        g_selfTestEnvAt = 0;
        g_eval.clear();
        g_evalSum = EvalSummary{};
    }
    // The environment block runs once the own/enemy lists are filled (at the latest after ~30 s) and again a minute later,
    // when the player has had time to select a gun.
    ++g_selfTestTicks;
    if (g_selfTestTicks == 1) g_selfTestEnvAt = 0;
    if (g_selfTestEnvAt == 0 && g_selfTestTicks >= 240 && ((!g_own.empty() && !g_enemyCodes.empty()) || g_selfTestTicks >= 1800)) {
        SelfTestEnvironment(team);
        g_selfTestEnvAt = g_selfTestTicks;
    } else if (g_selfTestEnvAt > 0 && g_selfTestEnvAt != -1 && g_selfTestTicks == g_selfTestEnvAt + 3600) {
        SelfTestEnvironment(team);
        g_selfTestEnvAt = -1;
    }
    std::vector<int> codes{team};
    for (int c : g_enemyCodes) codes.push_back(c);
    std::set<int> alive;
    const double tnow = NowSeconds();
    Vec2f pts[kTraceCap];
    for (int code : codes) {
        std::vector<int> ids;
        if (!g_api.ProjectileIds(code, ids)) continue;
        for (int id : ids) {
            if (g_eval.size() > 200 || !alive.insert(id).second) continue;
            Vec3 p, v;
            float grav = 0;
            if (!g_api.NodePos(id, p) || !g_api.NodeVel(id, v)) continue;
            g_api.ProjectileGravity(id, grav);
            const bool fresh = g_eval.find(id) == g_eval.end();
            EvalShell& e = g_eval[id];
            if (fresh) {
                e.first = {p.x, p.y};
                e.firstT = tnow;
                e.code = code;
            }
            const float gMag = std::fabs(grav);
            const bool beam = gMag < 0.001f;
            if (e.samples > 0 && !beam && (v.x != e.vx || v.y != e.vy)) {
                const DragEstimate de = EstimateDragFrames(e.vx, e.vy, v.x, v.y, g_cal.gravitySign * gMag);
                if (de.ok) {
                    e.k = de.k;
                    ++e.good;
                } else {
                    ++e.bad;
                }
            }
            e.vx = v.x;
            e.vy = v.y;
            e.grav = gMag;
            ++e.samples;
            if (!e.predicted && !beam && e.k >= 0.0f) {  // the drag is now measured on this very shell: predict the rest of the flight
                const TraceResult r = TraceShot({p.x, p.y}, {v.x, v.y}, g_cal.gravitySign * gMag, e.k, kTraceMaxTime, 0.0f, kTraceChord, GroundSegment,
                                                WaterAt, pts, kTraceCap);
                e.pts.assign(pts, pts + r.n);
                e.kind = r.kind;
                e.eta = r.time;
                e.predicted = r.n >= 2;
            } else if (e.predicted) {
                const float d = PathDistance(e.pts, {p.x, p.y});
                e.sumPath += d;
                ++e.pathN;
                e.maxPath = std::max<double>(e.maxPath, d);
            }
            e.last = {p.x, p.y};
            e.lastVel = {v.x, v.y};
            e.lastT = tnow;
        }
    }
    for (auto it = g_eval.begin(); it != g_eval.end();) {
        if (alive.count(it->first)) {
            ++it;
            continue;
        }
        SelfTestFinish(it->first, it->second);
        it = g_eval.erase(it);
    }
}

// ---------------------------------------------------------------- target under the mouse
// The aim calculator's target: the enemy device nearest to the world point under the mouse (a second press on the same one clears it).
void PickTargetUnderMouse() {
    if (!g_state.aimCalc) SetAimCalc(true);
    Vec3 screen{}, world{};
    if (!g_api.MouseWorld(screen, world)) {
        Logf("pick target: the mouse position could not be read");
        return;
    }
    const EnemyDev* best = nullptr;
    double bestD = 1.0e30;
    for (const EnemyDev& e : g_enemy) {
        const double dx = e.pos.x - world.x, dy = e.pos.y - world.y, d = std::sqrt(dx * dx + dy * dy);
        if (d < bestD) {
            bestD = d;
            best = &e;
        }
    }
    constexpr double kPickRadius = 600.0;  // world units; about the size of a gun cluster
    if (!best || bestD > kPickRadius) {
        Logf("pick target: no enemy device within %.0f units of the mouse (%.0f, %.0f); %zu enemy devices known", kPickRadius, world.x, world.y, g_enemy.size());
        return;
    }
    g_state.aimTarget = g_state.aimTarget == best->id ? 0 : best->id;
    g_state.groupPointSet = false;  // an enemy device picked now is the group's target too
    Logf("pick target: %s #%d at distance %.0f from the mouse -> target %d", best->type.c_str(), best->id, bestD, g_state.aimTarget);
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
        case kItemThreats: return g_state.threats;
        case kItemAimCalc: return g_state.aimCalc;
        case kItemAutoAim: return g_state.autoAim;
        case kItemBattery: return g_state.battery;
        case kItemEnemyOverlay: return g_state.enemyOverlay;
        case kItemRadar: return g_state.radar;
        case kItemIntel: return g_state.intel;
        case kItemTimers: return g_state.timers;
        case kItemWeakEnemy: return g_state.weakEnemy;
        case kItemWeakOwn: return g_state.weakOwn;
        case kItemStats: return g_state.stats;
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
        case kItemThreats: SetThreats(!g_state.threats); break;
        case kItemAimCalc: SetAimCalc(!g_state.aimCalc); break;
        case kItemAutoAim: SetAutoAim(!g_state.autoAim); break;
        case kItemEnemyOverlay: SetEnemyOverlay(!g_state.enemyOverlay); break;
        case kItemRadar: SetRadar(!g_state.radar); break;
        case kItemIntel: SetIntel(!g_state.intel); break;
        case kItemTimers: SetTimers(!g_state.timers); break;
        case kItemWeakEnemy: SetWeakEnemy(!g_state.weakEnemy); break;
        case kItemWeakOwn: SetWeakOwn(!g_state.weakOwn); break;
        case kItemStats: SetStats(!g_state.stats); break;
        case kItemRecord: SetRecord(!g_state.record); break;
        case kItemDiagnostics: g_state.wantDiagnostics = true; break;
        case kItemAllOff: AllOff(); break;
        case kItemPickTarget: PickTargetUnderMouse(); break;
        case kItemBattery: SetBattery(!g_state.battery); break;
        case kItemBatteryTarget: BatteryTarget(); break;
    }
}

void ActivateRowId(int id) {
    if (id >= 0 && id < kItemCount) {
        ActivateItem(id);
    } else if (id == kRowIdLanguage) {
        g_lang = g_lang == 1 ? 0 : 1;
        SaveLanguage();
        Logf("menu language: %s", g_lang == 1 ? "ru" : "en");
    } else if (id == kRowIdScale) {
        static const float steps[] = {0.85f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
        std::size_t next = 0;
        for (std::size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i)
            if (g_menuScale >= steps[i] - 0.01f) next = (i + 1) % (sizeof(steps) / sizeof(steps[0]));
        g_menuScale = steps[next];
        SaveMenuScale();
        Logf("menu scale: %.2f", g_menuScale);
    } else if (id >= kRowIdType && id < kRowIdType + 100) {
        const std::size_t t = static_cast<std::size_t>(id - kRowIdType);
        if (t < g_typeList.size()) g_typeShow[g_typeList[t]] = !ShowType(g_typeList[t]);
    } else if (id >= kRowIdTarget && id < kRowIdTarget + 100) {
        const std::size_t t = static_cast<std::size_t>(id - kRowIdTarget);
        if (t < g_targets.size()) {
            g_state.aimTarget = g_state.aimTarget == g_targets[t] ? 0 : g_targets[t];
            g_state.groupPointSet = false;
            Logf("auto-aim target: %d", g_state.aimTarget);
        }
    }
}

std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// Rows of the selected tab. Combat adds one switch per own weapon type, Enemy one row per aim target.
void BuildRows(std::vector<MenuRow>& rows) {
    rows.clear();
    auto languageRow = [] {
        MenuRow r;
        r.name = L"Language / Язык";
        r.desc = Tr(L"Language of the menu and panels", L"Язык меню и панелей");
        r.toggle = false;
        r.value = g_lang == 1 ? L"Русский" : L"English";
        r.id = kRowIdLanguage;
        return r;
    };
    if (g_state.menuTab == 0) rows.push_back(languageRow());  // the language switch is the first thing on the first tab
    if (g_state.menuTab == 4) {
        rows.push_back(languageRow());
        MenuRow scale;
        scale.name = Tr(L"Menu size", L"Размер меню");
        scale.desc = Tr(L"Click to make the menu bigger or smaller", L"Клик — сделать меню крупнее или мельче");
        scale.toggle = false;
        wchar_t pct[16];
        swprintf_s(pct, L"%d%%", static_cast<int>(g_menuScale * 100.0f + 0.5f));
        scale.value = pct;
        scale.id = kRowIdScale;
        rows.push_back(std::move(scale));
        MenuRow section;
        section.name = Tr(L"HOW TO USE", L"КАК ПОЛЬЗОВАТЬСЯ");
        section.section = true;
        rows.push_back(std::move(section));
        for (const wchar_t* line : {Tr(L"Insert opens and closes this menu. Drag the title bar to move it.", L"Insert открывает и закрывает меню. Двигайте его за заголовок."),
                                    Tr(L"Every function has a key box: click it, then press the key you want.", L"У каждой функции есть окошко клавиши: кликните и нажмите нужную."),
                                    Tr(L"Esc in that box removes the key. Toggle: a press switches the function.", L"Esc в окошке убирает клавишу. Переключатель: нажатие включает/выключает."),
                                    Tr(L"Hold: the function works only while the key is held down.", L"Удержание: функция работает, пока клавиша зажата.")}) {
            MenuRow note;
            note.name = line;
            note.section = true;
            rows.push_back(std::move(note));
        }
        return;
    }
    for (int i = 0; i < kItemCount; ++i) {
        if (kItemTab[i] != g_state.menuTab) continue;
        if (const wchar_t* caption = SectionBefore(i)) {
            MenuRow section;
            section.name = caption;
            section.section = true;
            rows.push_back(std::move(section));
        }
        MenuRow r;
        r.name = g_lang == 1 ? kItemNamesRu[i] : Widen(kItemNames[i]);
        r.desc = g_lang == 1 ? kItemDescRu[i] : kItemDescEn[i];
        r.toggle = kItemKinds[i] == aero::ipc::kToggle;
        r.on = ItemOn(i);
        r.id = i;
        r.bindable = true;
        r.bindText = g_binds[i].vk ? KeyName(g_binds[i].vk) : std::wstring();
        r.bindHold = g_binds[i].hold;
        r.capturing = g_bindCapture == i;
        if (g_online && (i == kItemFastBeams || i == kItemAutoAim || i == kItemBattery || i == kItemBatteryTarget)) {
            r.toggle = false;
            r.value = Tr(L"Offline only", L"Только офлайн");
        } else if (g_online && (i == kItemRepairSpeed || i == kItemRepairWhat)) {
            r.toggle = false;
            r.value = i == kItemRepairSpeed ? Tr(L"Game speed", L"Скорость игры") : Tr(L"Everything", L"Всё");
        } else if (i == kItemBatteryTarget) {
            r.toggle = false;
            r.value = std::to_wstring(g_state.group.size()) + Tr(L" guns", L" орудий");
        } else if (i == kItemRepairSpeed) {
            r.toggle = false;
            const int level = std::clamp(g_state.repairLevel, 0, 3);
            r.value = g_lang == 1 ? kRepairSpeedRu[level] : kRepairSpeedNames[level];
        } else if (i == kItemRepairWhat) {
            r.toggle = false;
            const int what = std::clamp(g_state.repairWhat, 0, 2);
            r.value = g_lang == 1 ? kRepairWhatRu[what] : kRepairWhatNames[what];
        }
        rows.push_back(std::move(r));
    }
    if (g_online && g_state.menuTab == 0) {
        MenuRow section;
        section.name = Tr(L"ONLINE: SWITCHES ABOVE NEED THE AEROHUB MOD IN THE LOBBY", L"ОНЛАЙН: ДЛЯ ЭТОГО НУЖЕН МОД AEROHUB В ЛОББИ");
        section.section = true;
        rows.push_back(std::move(section));
    }
    g_typeList.clear();
    if (g_state.menuTab == 1 && !g_typeCount.empty()) {
        MenuRow section;
        section.name = Tr(L"GUNS SHOWN (TRAJECTORY AND AUTO-AIM)", L"ОРУДИЯ ДЛЯ ТРАЕКТОРИИ И АВТОНАВЕДЕНИЯ");
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
        section.name = g_targets.empty() ? Tr(L"AUTO-AIM TARGET (NO ENEMY GUNS SEEN YET)", L"ЦЕЛЬ АВТОНАВЕДЕНИЯ (ВРАЖЕСКИХ ОРУДИЙ ЕЩЁ НЕТ)")
                                         : Tr(L"AUTO-AIM TARGET (NEAREST TO CAMERA)", L"ЦЕЛЬ АВТОНАВЕДЕНИЯ (БЛИЖАЙШИЕ К КАМЕРЕ)");
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

bool g_capPrev[256]{};

// Remember which keys are down now, so a key that is already held when the capture starts is not taken as the answer.
void ArmCapture() {
    for (int vk = 1; vk < 255; ++vk) g_capPrev[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// While a key box waits for a key: the first newly pressed key is the answer (Esc removes the bind). Insert stays the menu key.
void PollCapture() {
    if (g_bindCapture < 0) return;
    for (int vk = 1; vk < 255; ++vk) {
        const bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;
        const bool edge = down && !g_capPrev[vk];
        g_capPrev[vk] = down;
        if (!edge || vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == kKeyMenu) continue;
        const int item = g_bindCapture;
        g_bindCapture = -1;
        if (vk == VK_ESCAPE) {
            g_binds[item].vk = 0;
            g_binds[item].hold = false;
            Logf("bind: item %d key removed", item);
        } else {
            for (int other = 0; other < kItemCount; ++other)
                if (other != item && g_binds[other].vk == vk) g_binds[other] = Bind{};  // a key serves one action
            g_binds[item].vk = vk;
            Logf("bind: item %d -> %ls", item, KeyName(vk).c_str());
        }
        SaveBinds();
        return;
    }
}

// Runs the key binds. Toggle mode: each press activates the action. Hold mode (switches only): on while the key is down, back to
// the state it had before when it is released.
void RunBinds() {
    if (g_bindCapture >= 0) return;  // keys are being chosen; none of them may fire anything
    for (int i = 0; i < kItemCount; ++i) {
        const Bind& b = g_binds[i];
        if (b.vk == 0) {
            g_keyPrev[i] = false;
            continue;
        }
        const bool down = (GetAsyncKeyState(b.vk) & 0x8000) != 0;
        if (b.hold && kItemKinds[i] == aero::ipc::kToggle) {
            if (down && !g_holdDown[i]) {
                g_holdDown[i] = true;
                g_holdPrev[i] = ItemOn(i);
                if (!g_holdPrev[i]) ActivateItem(i);
            } else if (!down && g_holdDown[i]) {
                g_holdDown[i] = false;
                if (!g_holdPrev[i] && ItemOn(i)) ActivateItem(i);
            }
        } else if (down && !g_keyPrev[i]) {
            ActivateItem(i);
        }
        g_keyPrev[i] = down;
    }
}

// The game lost focus (or the bind changed): a hold-mode function must not stay on.
void ReleaseHolds() {
    for (int i = 0; i < kItemCount; ++i) {
        if (!g_holdDown[i]) continue;
        g_holdDown[i] = false;
        if (!g_holdPrev[i] && ItemOn(i)) ActivateItem(i);
    }
}

void HandleKeys() {
    if (Pressed(kKeyMenu)) {
        g_state.menuOpen = !g_state.menuOpen;
        g_bindCapture = -1;
        Logf("menu: %d", g_state.menuOpen ? 1 : 0);
    }
    RunBinds();
}

// Lays out, handles the mouse and keyboard for, and paints the in-game menu. Called inside an overlay pass.
void RunMenu(HDC dc, int vw, int vh) {
    MenuView view;
    BuildRows(view.rows);
    if (g_state.menuSel >= static_cast<int>(view.rows.size())) g_state.menuSel = 0;

    // Keyboard: arrows move inside the tab, Left/Right switch the row.
    if (GameWindowFocused()) PollCapture();
    if (GameWindowFocused() && g_bindCapture < 0) {
        if (Pressed(kMenuUp)) StepSelection(-1, view.rows);
        if (Pressed(kMenuDown)) StepSelection(+1, view.rows);
        const bool left = Pressed(kMenuLeft), right = Pressed(kMenuRight);
        if ((left || right) && g_state.menuSel < static_cast<int>(view.rows.size())) {
            g_state.menuKeyboard = true;
            ActivateRowId(view.rows[static_cast<std::size_t>(g_state.menuSel)].id);
            BuildRows(view.rows);
        }
    }
    BuildRows(view.rows);  // PollCapture may have changed a key box
    g_menu.Layout(vw, vh, view.rows, kTabCount, g_menuScale);

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
                g_state.menuTab = std::clamp(index, 0, kTabCount - 1);
                g_state.menuSel = 0;
                g_bindCapture = -1;
                BuildRows(view.rows);
            } else if (index >= 0 && index < static_cast<int>(view.rows.size())) {
                const int rowId = view.rows[static_cast<std::size_t>(index)].id;
                g_state.menuSel = index;
                if (hit == MenuHit::Key && rowId >= 0 && rowId < kItemCount) {  // choose a key for this action
                    g_bindCapture = g_bindCapture == rowId ? -1 : rowId;
                    ArmCapture();
                } else if (hit == MenuHit::Mode && rowId >= 0 && rowId < kItemCount) {  // toggle <-> hold
                    g_binds[rowId].hold = !g_binds[rowId].hold;
                    if (!g_binds[rowId].hold && g_holdDown[rowId]) ReleaseHolds();
                    SaveBinds();
                    Logf("bind: item %d mode %s", rowId, g_binds[rowId].hold ? "hold" : "toggle");
                } else if (hit == MenuHit::Row) {
                    g_bindCapture = -1;
                    ActivateRowId(rowId);
                }
            }
        }
        const bool onMenu = g_state.menuOpen && (g_menu.Inside(px, py) || g_menu.Pressing());
        const bool onPanel = g_state.menuOpen && PanelPointer(px, py, down, onMenu);
        cursorInside = onMenu || onPanel;
        // The game keeps seeing the last position outside the menu while the cursor is over it.
        if (!cursorInside || lastOutside.x < 0) lastOutside = screenPt;
        g_neutralPt = lastOutside;
        InterlockedExchange(&g_blockMouse, cursorInside ? 1 : 0);
    } else {
        g_menu.ClearPointer();
    }
    if (!g_state.menuOpen) return;

    BuildRows(view.rows);  // states may have changed by the click above
    g_menu.Layout(vw, vh, view.rows, kTabCount, g_menuScale);
    for (int t = 0; t < kTabCount; ++t) view.tabs.push_back(g_lang == 1 ? kTabNamesRu[t] : kTabNames[t]);
    view.tab = g_state.menuTab;
    view.subtitle = L"Forts";
    view.noKey = L"—";
    view.pressKey = Tr(L"Press a key...", L"Нажмите клавишу...");
    view.modeToggle = Tr(L"Toggle", L"Переключ.");
    view.modeHold = Tr(L"Hold", L"Удержание");
    if (g_lang == 1) {
        view.on = L"ВКЛ";
        view.off = L"ВЫКЛ";
        view.run = L"Запуск";
        view.footer = L"Кликните окошко клавиши и нажмите кнопку (Esc — убрать). Переключ. — нажатием, Удержание — пока зажата.";
    }
    view.pill = g_online ? Tr(L"Online", L"Онлайн") : (g_inMatch ? Tr(L"In match", L"В матче") : Tr(L"Not in match", L"Не в матче"));
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
                                                nullptr, nullptr,  // repair speed / what
                                                SetTrajectory, SetImpact, SetThreats, SetAimCalc, SetAutoAim,
                                                SetEnemyOverlay, SetRadar, SetIntel, SetTimers, SetWeakEnemy, SetWeakOwn,
                                                SetStats, SetRecord, nullptr, nullptr, nullptr,  // diagnostics, all off, pick target
                                                SetBattery, nullptr};  // battery: on, target + fire
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
    else ReleaseHolds();
    PublishState();

    const bool inWorld = g_api.InWorld();
    int team = 0;
    const bool haveTeam = inWorld && g_api.LocalTeam(team);
    UpdateTelemetry(inWorld, haveTeam, team);
    g_inMatch = haveTeam;
    if (!haveTeam) {  // outside a match only the menu is drawn
        if (g_stats.active) ExportStats();
        g_traceCache.clear();
        g_threats.clear();
        ResetModSwitches();
        int w = 0, h = 0;
        for (auto& entry : g_spots) entry.second.shown = false;
        if (g_state.menuOpen && g_overlay.Begin(dc, w, h)) {
            g_uiScale = std::clamp(static_cast<float>(h) / 1080.0f, 0.8f, 2.5f);
            RunMenu(dc, w, h);
            g_overlay.End();
        }
        return;
    }

    if (team != g_syncTeam) {
        g_syncTeam = team;
        if (g_stats.active) ExportStats();  // a new match: the previous one is written out
        g_traceCache.clear();
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

    const bool needWorld = g_state.enemyOverlay || g_state.radar || g_state.intel || g_state.autoAim || g_state.threats || g_state.aimCalc ||
                           g_state.timers || g_state.weakEnemy || g_state.weakOwn || g_state.stats || (g_state.menuOpen && g_state.menuTab == 2) ||
                           g_selfTest;
    if (needWorld) RefreshWorld(team, g_state.radar);
    if (g_selfTest) SelfTestTick(team);
    if (g_state.threats) UpdateThreats();
    else g_threats.clear();
    if (g_state.aimCalc) UpdateAimCalc(team);
    if (g_state.weakEnemy || g_state.weakOwn) UpdateWeak(team);
    if (g_state.stats) {
        if (!g_stats.active) ResetStats();
        TrackShots(team);
        UpdateDamage();
        RefreshStatsPanel();
    } else if (g_stats.active) {
        ExportStats();
    }
    if (offline) ApplyAutoAim(team);
    UpdateGroup(team, offline);
    if (g_state.record) RecordSamples(team);
    if (g_state.wantDiagnostics) {
        g_state.wantDiagnostics = false;
        Diagnostics(team);
    }

    const bool worldDraw = g_state.trajectory || g_state.enemyOverlay || g_state.threats || g_state.aimCalc || g_state.weakEnemy || g_state.weakOwn ||
                           !g_state.group.empty() || g_state.groupPointSet;
    const bool draw = worldDraw || g_state.menuOpen || g_state.radar || g_state.intel || g_state.stats;
    if (!draw) return;
    if (g_state.trajectory || g_state.menuOpen) RefreshTypes(team);
    if (g_state.trajectory) {
        std::vector<int> own;
        if (g_api.OwnWeapons(team, own)) LearnPhysics(team, own);
    }
    int w = 0, h = 0;
    if (!g_overlay.Begin(dc, w, h)) return;
    g_uiScale = std::clamp(static_cast<float>(h) / 1080.0f, 0.8f, 2.5f);
    g_viewW = w;
    g_viewH = h;
    for (auto& entry : g_spots) entry.second.shown = false;
    g_hitIds.clear();
    if (worldDraw && PrepareCalibration(w, h)) {
        if (g_state.weakEnemy || g_state.weakOwn) DrawWeak();
        if (g_state.trajectory) DrawTrajectories(team);
        if (g_state.aimCalc) DrawAimGhost();
        DrawGroup();
        if (g_state.enemyOverlay) DrawEnemies(w, h);
        if (g_state.threats) DrawThreats(w);
    }
    if (g_state.radar) DrawRadar(w);
    if (g_state.intel) DrawIntel(w);
    DrawLeftPanels();
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
