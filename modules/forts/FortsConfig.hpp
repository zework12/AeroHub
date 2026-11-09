#pragma once
// Every build-dependent value of the Forts module lives here.
//
// Provenance of everything below: static disassembly of
//   D:\SteamLibrary\steamapps\common\Forts\Forts.exe   (Steam buildid 26181732, appmanifest_410900.acf)
// Addresses are RVAs (image base 0x140000000 subtracted). They were NOT yet confirmed in a running
// game; the module therefore validates identity and every function prologue before using anything.
#include <array>
#include <cstddef>
#include <cstdint>

namespace forts::cfg {

// PE header of the supported Forts.exe.
inline constexpr std::uint32_t kTimeDateStamp = 0x6ADC9FAD;
inline constexpr std::uint32_t kSizeOfImage = 0x9D3000;

// .data pointer to the game singleton (every Lua API handler loads it first).
inline constexpr std::uintptr_t kGameGlobalRva = 0x91D230;
// Offsets inside the game singleton (seen in the handlers below).
inline constexpr std::ptrdiff_t kGameTeamsOff = 0x1CD0;   // team manager; local team id at +0xFC0 of it
inline constexpr std::ptrdiff_t kGameSimOff = 0x1CD8;     // owns the device list ([+0x6C0]..[+0x6C8])
inline constexpr std::ptrdiff_t kGameCameraOff = 0x1D00;  // camera: focus Vec3 at +0x190, zoom float at +0x1A8
inline constexpr std::ptrdiff_t kCameraFocusOff = 0x190;
// Byte, non-zero in a network game: the Lua handler GetGameMode (RVA 0xC8800) tests it first and returns "Multiplayer".
inline constexpr std::ptrdiff_t kGameMultiplayerFlagOff = 0x1BE1;

// gdi32!SwapBuffers slot in the exe's import table (Forts renders through OpenGL).
inline constexpr std::uintptr_t kSwapBuffersIatRva = 0x762110;

// Lua-API handlers of the game, called directly. `prologue` is the first 16 bytes on disk.
struct ApiFn {
    const char* name;
    std::uintptr_t rva;
    std::array<std::uint8_t, 16> prologue;
};

inline constexpr ApiFn kGetLocalTeamId{"GetLocalTeamId", 0xCCFF0, {0x48, 0x8B, 0x05, 0x39, 0x02, 0x85, 0x00, 0x48, 0x8B, 0x88, 0xD0, 0x1C, 0x00, 0x00, 0x8B, 0x81}};
inline constexpr ApiFn kGetTeamResources{"GetTeamResources", 0xE45F0, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x05, 0x33, 0x8C, 0x83, 0x00, 0x48, 0x8B, 0xD9}};
inline constexpr ApiFn kSetUnlimitedResources{"SetUnlimitedResources", 0xE4880, {0x44, 0x88, 0x44, 0x24, 0x18, 0x53, 0x56, 0x57, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x58, 0x48, 0x8B}};
inline constexpr ApiFn kGetWeaponCount{"GetWeaponCount", 0xCFC70, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57}};
inline constexpr ApiFn kGetWeaponId{"GetWeaponId", 0xCFD30, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48}};
inline constexpr ApiFn kGetWeaponBarrelPosition{"GetWeaponBarrelPosition", 0xD6620, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0x05, 0xFF, 0x6B, 0x84}};
inline constexpr ApiFn kGetFireVel{"GetFireVel", 0xE5830, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0x05, 0xEF, 0x79, 0x83}};
inline constexpr ApiFn kWorldToScreen{"WorldToScreen", 0xE6870, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x05, 0xB3, 0x69, 0x83, 0x00, 0x48, 0x8B, 0xD9}};

inline constexpr ApiFn kProjectileCount{"ProjectileCount", 0xE0850, {0x48, 0x89, 0x5C, 0x24, 0x08, 0xB8, 0xE1, 0x7A, 0x14, 0xAE, 0x45, 0x33, 0xD2, 0xF7, 0xE9, 0xC1}};
inline constexpr ApiFn kGetProjectileId{"GetProjectileId", 0xE0900, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48}};
inline constexpr ApiFn kNodePosition{"NodePosition", 0xE0E90, {0x48, 0x8B, 0x05, 0x99, 0xC3, 0x83, 0x00, 0x45, 0x33, 0xC0, 0x4C, 0x8B, 0x98, 0xE0, 0x1C, 0x00}};
inline constexpr ApiFn kNodeVelocity{"NodeVelocity", 0xE0F40, {0x48, 0x8B, 0x05, 0xE9, 0xC2, 0x83, 0x00, 0x45, 0x33, 0xC0, 0x4C, 0x8B, 0x98, 0xE0, 0x1C, 0x00}};
inline constexpr ApiFn kGetProjectileGravity{"GetProjectileGravity", 0xD4050, {0x48, 0x83, 0xEC, 0x38, 0x48, 0x8B, 0x05, 0xD5, 0x91, 0x84, 0x00, 0x33, 0xD2, 0x4C, 0x8B, 0x90}};

// Weapon type -> projectile -> gravity chain, commander gauge and the per-team build-time override.
inline constexpr ApiFn kGetDeviceType{"GetDeviceType", 0xD0F50, {0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x05, 0xD5, 0xC2, 0x84, 0x00, 0x44, 0x8B, 0xC9, 0x4C, 0x8B}};
inline constexpr ApiFn kGetDeviceTypeIndex{"GetDeviceTypeIndex", 0xD1040, {0x48, 0x8B, 0x05, 0xE9, 0xC1, 0x84, 0x00, 0x48, 0x8B, 0xD1, 0x45, 0x33, 0xC0, 0x48, 0x8B, 0x88}};
inline constexpr ApiFn kGetWeaponTypeAmmoProjectileByIndex{"GetWeaponTypeAmmoProjectileByIndex", 0xD5180, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x49, 0x63, 0xD8, 0x41, 0xB1, 0x02, 0x44, 0x8B, 0xC2, 0x8B}};
inline constexpr ApiFn kGetProjectileTypeGravity{"GetProjectileTypeGravity", 0xD40E0, {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57}};
inline constexpr ApiFn kGetTeamCommanderPoints{"GetTeamCommanderPoints", 0xE4D50, {0x48, 0x83, 0xEC, 0x28, 0xB8, 0x1F, 0x85, 0xEB, 0x51, 0xF7, 0xE9, 0xC1, 0xFA, 0x05, 0x8B, 0xC2}};
inline constexpr ApiFn kAddTeamCommanderPoints{"AddTeamCommanderPoints", 0xE4DB0, {0xB8, 0x1F, 0x85, 0xEB, 0x51, 0xF7, 0xE9, 0xC1, 0xFA, 0x05, 0x8B, 0xC2, 0xC1, 0xE8, 0x1F, 0x03}};
// SetOverrideBuildTime/SetOverrideReloadTime(team, seconds, duration): the first float replaces the build or reload time
// while it is above 0 (readers 0x257F40, 0x265A80). The second is a countdown in seconds; the frame tick at 0x2F95EF clears
// the override when it runs out and skips it when negative.
inline constexpr float kOverrideNoExpiry = -1.0f;
inline constexpr ApiFn kSetOverrideBuildTime{"SetOverrideBuildTime", 0xD7340, {0x40, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0x05, 0xE3, 0x5E, 0x84, 0x00, 0x8B, 0xF9, 0x0F}};

inline constexpr ApiFn kIsCommanderActive{"IsCommanderActive", 0xE4E80, {0xB8, 0x1F, 0x85, 0xEB, 0x51, 0xF7, 0xE9, 0xC1, 0xFA, 0x05, 0x8B, 0xC2, 0xC1, 0xE8, 0x1F, 0x03}};
inline constexpr ApiFn kSetOverrideReloadTime{"SetOverrideReloadTime", 0xD7460, {0x40, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0x05, 0xC3, 0x5D, 0x84, 0x00, 0x8B, 0xF9, 0x0F}};

// Device enumeration / health / repair and the real aim update (signatures read from the handlers).
inline constexpr ApiFn kGetDeviceCount{"GetDeviceCount", 0xCF860, {0x48, 0x8B, 0x05, 0xC9, 0xD9, 0x84, 0x00, 0x45, 0x33, 0xC0, 0x44, 0x8B, 0xD1, 0x48, 0x8B, 0x90}};
inline constexpr ApiFn kGetDeviceId{"GetDeviceId", 0xCF930, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x8B, 0x05, 0xF4, 0xD8, 0x84, 0x00, 0x45, 0x33, 0xC0, 0x44}};
inline constexpr ApiFn kGetDevicePosition{"GetDevicePosition", 0xD5770, {0x48, 0x8B, 0x05, 0xB9, 0x7A, 0x84, 0x00, 0x44, 0x8B, 0xD2, 0x33, 0xD2, 0x4C, 0x8B, 0x98, 0xD8}};
inline constexpr ApiFn kGetDeviceHitpoints{"GetDeviceHitpoints", 0xDAED0, {0x48, 0x8B, 0x05, 0x59, 0x23, 0x84, 0x00, 0x33, 0xD2, 0x4C, 0x8B, 0x90, 0xD8, 0x1C, 0x00, 0x00}};
inline constexpr ApiFn kGetDeviceInitialHitpoints{"GetDeviceInitialHitpoints", 0xDAF30, {0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x05, 0xF5, 0x22, 0x84, 0x00, 0x33, 0xD2, 0x4C, 0x8B, 0x90}};
inline constexpr ApiFn kRepairDevice{"RepairDevice", 0xDB1F0, {0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x05, 0x35, 0x20, 0x84, 0x00, 0x33, 0xD2, 0x4C, 0x8B, 0x90}};
// (name, params, file, useContext). With "" and false the event runs on every client in each script that defines `name`, others skip it.
// Team codes are side + 100 * player: 1 and 2 offline, 101 for the first player of side 1 online
// (GetLocalTeamId read live from a network match, buildid 26181732). The limit only rejects garbage.
inline constexpr int kMaxTeamCode = 9999;
inline constexpr ApiFn kSendScriptEvent{"SendScriptEvent", 0xC6F60, {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57}};
inline constexpr ApiFn kUpdateWeaponAim{"UpdateWeaponAim", 0xE5330, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0x05, 0xF3, 0x7E, 0x83, 0x00, 0x45, 0x33, 0xC0}};

// Terrain, water and weapon-timer queries. Addresses: the game's own Lua registration table (every `lea r8,[handler]` /
// `lea rdx,[name]` pair in .text of Forts.exe on disk, buildid 26181732); signatures: read from the handlers' disassembly.
//   GetWaterLevel(float x) -> float      piecewise-constant table at world + 0x12E8..0x12F0 (12-byte entries: x threshold, level, ...);
//                                        about 2.1e9 on maps without water. Deeper is a LARGER y: GetWaterDepthAt(pos) = pos.y - level.
//   CastGroundRay(const Vec3* a, const Vec3* b, int flags) -> 1 when the segment touches terrain (water is not terrain);
//                                        GetRayHitPosition(Vec3* out) returns the point stored by the last cast.
//   IsWeaponReadyToFire(int deviceId) -> bool;  GetWeaponReloadTime(int id) -> float = reload period * a fraction kept at
//                                        weapon + 0x4B8 (whether that fraction is the remaining or the elapsed part is NOT confirmed:
//                                        the module learns it from the first reload it sees to completion);
//                                        GetWeaponReloadPeriodById(int id) -> float seconds. None of them checks the device's team.
inline constexpr ApiFn kGetWaterLevel{"GetWaterLevel", 0xE05B0, {0x48, 0x8B, 0x05, 0x79, 0xCC, 0x83, 0x00, 0x48, 0x8B, 0x80, 0xE0, 0x1C, 0x00, 0x00, 0x48, 0x8B}};
inline constexpr ApiFn kCastGroundRay{"CastGroundRay", 0xD9570, {0x40, 0x53, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x01, 0x33, 0xDB, 0xF3}};
inline constexpr ApiFn kGetRayHitPosition{"GetRayHitPosition", 0xD9E60, {0xF3, 0x0F, 0x10, 0x05, 0xE0, 0x33, 0x84, 0x00, 0x48, 0x8B, 0xC1, 0xF3, 0x0F, 0x10, 0x0D, 0xD9}};
inline constexpr ApiFn kIsWeaponReadyToFire{"IsWeaponReadyToFire", 0xD7610, {0x48, 0x83, 0xEC, 0x38, 0x48, 0x8B, 0x05, 0x15, 0x5C, 0x84, 0x00, 0x44, 0x8B, 0xC9, 0x4C, 0x8B}};
inline constexpr ApiFn kGetWeaponReloadTime{"GetWeaponReloadTime", 0xE41B0, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0x05, 0x73, 0x90, 0x83, 0x00, 0x33, 0xD2, 0x4C}};
inline constexpr ApiFn kGetWeaponReloadPeriodById{"GetWeaponReloadPeriodById", 0xE3F30, {0x48, 0x83, 0xEC, 0x38, 0x48, 0x8B, 0x05, 0xF5, 0x92, 0x83, 0x00, 0x33, 0xD2, 0x4C, 0x8B, 0x90}};
// GetLocalSelectedDeviceId (0xCF710): int(), no arguments; the device the local player has selected (-1 / 0 when none).
// Which of the player's actions "selects" a gun this way is not confirmed in game.
inline constexpr ApiFn kGetLocalSelectedDeviceId{"GetLocalSelectedDeviceId", 0xCF710, {0x4C, 0x8B, 0x0D, 0x19, 0xDB, 0x84, 0x00, 0x49, 0x8B, 0x81, 0xD0, 0x1C, 0x00, 0x00, 0x4C, 0x8B}};
// GetMousePos (0xE6700): Vec3*(Vec3* out), the mouse in the game's screen space. ScreenToWorld (0xE68B0): Vec3*(Vec3* out, const Vec3* screen),
// forwards to the camera object's virtual converter. That the mouse position and the converter use the same screen space is not confirmed
// in game; the self-test logs a WorldToScreen -> ScreenToWorld round trip for that.
inline constexpr ApiFn kGetMousePos{"GetMousePos", 0xE6700, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0xD1, 0x48, 0x8B, 0x0D, 0xAD}};
inline constexpr ApiFn kScreenToWorld{"ScreenToWorld", 0xE68B0, {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x05, 0x73, 0x69, 0x83, 0x00, 0x48, 0x8B, 0xD9}};
// Node field read by GetNodeGroundBlockIndex (0xE2690): the terrain block a node is anchored to; -1 expected for a free node
// (the -1 is the handler's value for an unknown node id; for a known free node it is an assumption).
inline constexpr std::ptrdiff_t kNodeGroundBlock = 0x18;

// Raw device list of the simulation object: vector of device pointers at kGameSimOff + kDeviceListBegin/End, each
// device with its id, team code (compared by every Lua handler) and state byte (5 = ignored by them).
inline constexpr std::ptrdiff_t kDeviceListBegin = 0x6C0, kDeviceListEnd = 0x6C8;
inline constexpr std::ptrdiff_t kDeviceId = 0x314, kDeviceTeam = 0x318, kDeviceState = 0x326;

// Structure nodes (world + kNodeListBegin/End: vector of node pointers) and their links. Every link sits in the link
// vector of BOTH its nodes (records of kLinkRecord bytes at node + kNodeLinksBegin/End). Health of a link is
// kLinkHealth; its maximum is kMaterialMaxHealth in the material record (index kLinkMaterial). Layout read from
// GetLinkHealth / RepairLink (static disassembly, unverified in game). Device health is kDeviceHealth.
inline constexpr std::ptrdiff_t kNodeListBegin = 0x30, kNodeListEnd = 0x38;
inline constexpr std::ptrdiff_t kNodeId = 0xEC, kNodeMovingFlag = 1, kNodePosMoving = 0x1B0, kNodePosStatic = 0x100;  // position set picked by byte [node+1]
inline constexpr std::ptrdiff_t kNodeTeam = 0xF0, kNodeDead = 0x1A1, kNodeLinksBegin = 0xCC, kNodeLinksEnd = 0xD4;
inline constexpr std::size_t kLinkRecord = 0x19C;
inline constexpr std::ptrdiff_t kLinkOtherNode = 0x138, kLinkMaterial = 0x150, kLinkHealth = 0x154, kLinkState = 0x175;
inline constexpr std::ptrdiff_t kMaterialMaxHealth = 0x460, kDeviceHealth = 0x340;

// Where SetOverrideBuildTime stores its two floats: teams manager + base + (team % 100) * stride + offset.
inline constexpr std::ptrdiff_t kBuildOverrideBase = 0x1078, kBuildOverrideStride = 0x148, kBuildOverrideA = 0x68, kBuildOverrideB = 0x6C;
// SetOverrideReloadTime stores its two floats right after the build pair in the same per-team record.
inline constexpr std::ptrdiff_t kReloadOverrideA = 0x70, kReloadOverrideB = 0x74;
// Reload time in seconds for "minigun" (fast reload).
inline constexpr float kFastReloadValue = 0.1f;

// Link / structure materials: records of kMaterialRecord bytes in vectors at world + kMaterialsBase + side * kMaterialsSideStride
// (begin pointer, end pointer). Each record holds its SaveName at +kMaterialName and the link BuildTime float at +kMaterialBuildTime.
// Read from the Lua handlers GetLinkMaterialSaveName and the data loader (static disassembly, unverified in game).
inline constexpr std::ptrdiff_t kGameWorldOff = 0x1CE0;
inline constexpr std::ptrdiff_t kMaterialsBase = 0xB48, kMaterialsSideStride = 0x18;
inline constexpr std::size_t kMaterialRecord = 0xE90;
inline constexpr std::ptrdiff_t kMaterialName = 0x2D0, kMaterialBuildTime = 0x4BC;
inline constexpr int kMaterialSides = 3;
inline constexpr float kFastMaterialFactor = 0.1f;  // link BuildTime is multiplied by this

// Build time in seconds for "fast build".
inline constexpr float kFastBuildValue = 0.1f;

// Player aim input kept in .data (read by GetAimWeaponAngle / GetAimWeaponSpeed): float each.
// Per-weapon fire speed (GetFireVel) and this global aim speed may differ for weapons with fire power.
inline constexpr std::uintptr_t kAimAngleRva = 0x91C3B4;
inline constexpr std::uintptr_t kAimSpeedRva = 0x91C4AC;

// Projectile physics, measured on 501 recorded shots (4998 observed positions agree with the model to the log's rounding, 0.07 rms):
// the simulation runs in frames of 0.04 s (Physics.FramesRate = 25); each frame v += (g - k v) * h, then p += v * h (see Tactics.hpp).
// The drag is LINEAR in the velocity. Two values occurred: k = 0 (cannon, 252 shots) and k = 0.0840 (machinegun, ProjectileDrag 0.021,
// 227 shots), i.e. k = kDragFactor * ProjectileDrag (= Physics.AirDrag 0.4 * 10). The factor rests on that one type; the module also
// measures k on live shots (exactly, EstimateDragFrames) and trusts the measurement over the formula. GetProjectileTypeDrag (0xD43A0) has
// the same shape as GetProjectileTypeGravity: (const char* projectileName, int team) -> float. Shells with a ProjectileDrag of at least
// kPropelledDrag (missile: 64) fly under thrust and are not predicted.
inline constexpr float kDragFactor = 4.0f;
inline constexpr float kPropelledDrag = 2.0f;
inline constexpr ApiFn kGetProjectileTypeDrag{"GetProjectileTypeDrag", 0xD43A0, {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57}};

// Physics.Gravity from data/db/constants.lua (measured from the game's data file). The direction of
// "down" in world space is an assumption (+Y, i.e. Y grows downward); FortsModule.ini can override it.
inline constexpr float kDefaultGravityY = 981.0f;

}  // namespace forts::cfg
