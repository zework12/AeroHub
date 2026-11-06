#pragma once
#include <windows.h>

#include <string>
#include <vector>

namespace forts {

struct Vec3 {
    float x{}, y{}, z{};
};

// A device as it sits in the game's own list (read directly, no Lua handler in between).
struct RawDevice {
    int id = 0;
    int teamCode = 0;
    unsigned char state = 0;
};

// One beam of a base, for drawing it.
struct BaseSegment {
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    float health = 1;       // 0..1 of the material maximum
    bool building = false;  // not finished yet
};

// What one HealTeam pass changed.
struct HealResult {
    int devices = 0, links = 0;
    int linksSeen = 0;  // finished links of the team that were examined (shows whether the link layout is right)
};

// One link material (strut type) and its current build time.
struct MaterialBuild {
    int side = 0, index = 0;
    std::string name;
    float buildTime = 0;
};

// Raw result of GetTeamResources. Which slot is metal and which is energy is inferred from the
// argument order of SetUnlimitedResources and is not yet confirmed in game.
struct TeamResources {
    float slotFromOff58{};  // team object +0x58
    float slotFromOff50{};  // team object +0x50
    float third{};
};

// Typed calls into the game's own Lua-API handlers. Every call is wrapped in SEH and returns false
// when the game is not in a state where the call is safe or the call faulted.
class GameApi {
public:
    // Verifies the exe identity and the first bytes of every function before accepting it.
    bool Init(HMODULE exe, std::string& error);

    bool InWorld() const;
    bool Multiplayer(bool& online) const;
    bool LocalTeam(int& team) const;
    bool SetUnlimited(int team, bool on) const;
    bool Resources(int team, TeamResources& out) const;
    bool OwnWeapons(int team, std::vector<int>& ids) const;
    bool BarrelPosition(int weaponId, Vec3& out) const;
    bool FireVelocity(int weaponId, Vec3& out) const;
    bool ToScreen(const Vec3& world, Vec3& out) const;
    bool CameraFocus(Vec3& out) const;
    // Live projectiles of one team (ids), their node position/velocity and per-projectile gravity.
    bool ProjectileIds(int team, std::vector<int>& ids) const;
    bool NodePos(int id, Vec3& out) const;
    bool NodeVel(int id, Vec3& out) const;
    bool ProjectileGravity(int id, float& out) const;
    // Weapon type SaveName (e.g. "cannon") of a device id.
    bool DeviceTypeName(int deviceId, std::string& out) const;
    // Gravity of the projectile a weapon fires: type -> first ammo -> projectile name -> type gravity.
    bool WeaponProjectile(int weaponId, int team, std::string& projectileName, float& gravity) const;
    // Commander gauge (ready at 1.0) and the per-team build-time override.
    bool CommanderPoints(int team, float& out) const;
    bool AddCommanderPoints(int team, float amount) const;
    bool ReadBuildOverride(int team, float& a, float& b) const;
    bool SetBuildOverride(int team, float a, float b) const;
    bool ReadReloadOverride(int team, float& a, float& b) const;
    bool SetReloadOverride(int team, float a, float b) const;
    // True while the commander ability is running (the gauge drains; do not top it up then).
    bool CommanderActive(int team, bool& active) const;
    // Link materials: enumerate, and write one BuildTime (re-validated by name right before the write).
    bool MaterialBuildTimes(std::vector<MaterialBuild>& out) const;
    bool SetMaterialBuildTime(int side, int index, const std::string& expectedName, float value) const;
    // Adds `fraction` of the maximum health to every damaged device and/or link of a team (clamped to the maximum).
    // Links are written in both of their nodes. Returns false if the game structures could not be read.
    bool HealTeam(int teamCode, float fraction, bool devices, bool links, HealResult& result) const;
    // All beams whose first node belongs to the team (each beam once), with positions and health.
    bool BaseSegments(int teamCode, std::vector<BaseSegment>& out) const;
    // Every device in the simulation with its real team code and state.
    bool RawDevices(std::vector<RawDevice>& out) const;
    // Any team's devices: count, ids, position, health, repair; real aim update towards a world point.
    bool DeviceIds(int team, std::vector<int>& ids) const;
    bool DevicePosition(int deviceId, Vec3& out) const;
    bool DeviceHealth(int deviceId, float& hp, float& maxHp) const;
    bool RepairDevice(int deviceId) const;
    bool AimWeaponAt(int weaponId, const Vec3& target) const;
    bool SendScriptEvent(const char* function, const char* params) const;
    // Global player aim input (angle, speed).
    bool AimInput(float& angle, float& speed) const;

private:
    std::uintptr_t base_ = 0;
};

}  // namespace forts
