#include "GameApi.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "FortsConfig.hpp"
#include "Log.hpp"
#include "Pe.hpp"

namespace forts {
namespace {

namespace cfg = forts::cfg;

constexpr float kMaxWorldCoord = 1.0e7f;
constexpr int kMaxWeapons = 512;

using FnGetLocalTeamId = int (*)();
using FnSetUnlimited = void (*)(int, bool, bool);
using FnGetTeamResources = TeamResources* (*)(TeamResources*, int);
using FnGetWeaponCount = int (*)(int);
using FnGetWeaponId = int (*)(int, int);
using FnVec3ById = Vec3* (*)(Vec3*, int);
using FnWorldToScreen = Vec3* (*)(Vec3*, const Vec3*);
using FnVec3Out = Vec3* (*)(Vec3*);
using FnProjectileCount = int (*)(int);
using FnGetProjectileId = int (*)(int, int);
using FnGetProjectileGravity = float (*)(int);
using FnGetDeviceType = const char* (*)(int);
using FnGetDeviceTypeIndex = int (*)(const char*);
using FnAmmoProjectile = const char* (*)(int, int, int);
using FnProjectileTypeGravity = float (*)(const char*, int);
using FnCommanderPoints = float (*)(int);
using FnAddCommanderPoints = void (*)(int, float);
using FnSetBuildOverride = void (*)(int, float, float);
using FnCommanderActive = unsigned char (*)(int);
using FnGetDeviceCount = int (*)(int);
using FnGetDeviceId = int (*)(int, int);
using FnGetDeviceHp = float (*)(int);
using FnRepairDevice = void (*)(int);
using FnUpdateWeaponAim = void (*)(int, const Vec3*);
using FnSendScriptEvent = void (*)(const char*, const char*, const char*, bool);
using FnWaterLevel = float (*)(float);
using FnCastGroundRay = int (*)(const Vec3*, const Vec3*, int);
using FnRayHitPosition = Vec3* (*)(Vec3*);
using FnWeaponReady = unsigned char (*)(int);
using FnWeaponSeconds = float (*)(int);

// Material record names: either an inline char array or an MSVC std::string at the same address.
std::string ReadRecordName(const std::uint8_t* p) {
    auto printable = [](const char* s) {
        if (!s[0]) return false;
        for (int i = 0; i < 48; ++i) {
            if (!s[i]) return true;
            if (s[i] < 0x20 || s[i] > 0x7E) return false;
        }
        return false;
    };
    const char* inlineName = reinterpret_cast<const char*>(p);
    if (printable(inlineName)) return inlineName;
    std::uint64_t size = 0, cap = 0;
    std::memcpy(&size, p + 0x10, sizeof(size));
    std::memcpy(&cap, p + 0x18, sizeof(cap));
    if (size == 0 || size > 47 || cap < size) return {};
    const char* data = cap >= 16 ? *reinterpret_cast<const char* const*>(p) : inlineName;
    return data && printable(data) ? std::string(data) : std::string();
}

std::uint8_t* Game();

const std::uint8_t* MaterialRecord(int side, int index) {
    const std::uint8_t* g = Game();
    if (!g || side < 0 || side >= cfg::kMaterialSides) return nullptr;
    const std::uint8_t* world = *reinterpret_cast<const std::uint8_t* const*>(g + cfg::kGameWorldOff);
    if (!world) return nullptr;
    const std::uint8_t* vec = world + cfg::kMaterialsBase + static_cast<std::ptrdiff_t>(side) * cfg::kMaterialsSideStride;
    const std::uint8_t* begin = *reinterpret_cast<const std::uint8_t* const*>(vec);
    const std::uint8_t* end = *reinterpret_cast<const std::uint8_t* const*>(vec + 8);
    if (!begin || end <= begin) return nullptr;
    const std::size_t bytes = static_cast<std::size_t>(end - begin);
    if (bytes % cfg::kMaterialRecord != 0) return nullptr;
    const std::size_t n = bytes / cfg::kMaterialRecord;
    if (n == 0 || n > 64 || index < 0 || static_cast<std::size_t>(index) >= n) return nullptr;
    return begin + static_cast<std::size_t>(index) * cfg::kMaterialRecord;
}

bool PrintableName(const char* s, std::size_t maxLen = 48) {
    if (!s || !*s) return false;
    for (std::size_t i = 0; i < maxLen; ++i) {
        if (!s[i]) return true;
        if (s[i] < 0x20 || s[i] > 0x7E) return false;
    }
    return false;
}

std::uintptr_t g_base = 0;

template <class T>
T Fn(const cfg::ApiFn& f) {
    return reinterpret_cast<T>(g_base + f.rva);
}

// SEH guard. The callable must not own objects that need unwinding in a way that matters if it faults.
template <class F>
bool Guard(F&& f) {
    __try {
        return f();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Plausible(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::fabs(v.x) < kMaxWorldCoord &&
           std::fabs(v.y) < kMaxWorldCoord && std::fabs(v.z) < kMaxWorldCoord;
}

std::uint8_t* Game() { return *reinterpret_cast<std::uint8_t**>(g_base + cfg::kGameGlobalRva); }

bool CheckFunction(const cfg::ApiFn& f, std::string& error) {
    const auto* code = reinterpret_cast<const std::uint8_t*>(g_base + f.rva);
    const bool ok = Guard([&] { return std::memcmp(code, f.prologue.data(), f.prologue.size()) == 0; });
    if (!ok) error = std::string("prologue mismatch at ") + f.name;
    return ok;
}

}  // namespace

bool GameApi::Init(HMODULE exe, std::string& error) {
    base_ = g_base = reinterpret_cast<std::uintptr_t>(exe);
    const auto id = aero::ReadImageIdentity(GetCurrentProcess(), g_base);
    if (!id) {
        error = "cannot read PE header of the host exe";
        return false;
    }
    if (id->timeDateStamp != cfg::kTimeDateStamp || id->sizeOfImage != cfg::kSizeOfImage) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "build mismatch: stamp 0x%X size 0x%X (module built for 0x%X / 0x%X)",
                      id->timeDateStamp, id->sizeOfImage, cfg::kTimeDateStamp, cfg::kSizeOfImage);
        error = buf;
        return false;
    }
    for (const cfg::ApiFn* f : {&cfg::kGetLocalTeamId, &cfg::kGetTeamResources, &cfg::kSetUnlimitedResources,
                                &cfg::kGetWeaponCount, &cfg::kGetWeaponId, &cfg::kGetWeaponBarrelPosition,
                                &cfg::kGetFireVel, &cfg::kWorldToScreen, &cfg::kProjectileCount,
                                &cfg::kGetProjectileId, &cfg::kNodePosition, &cfg::kNodeVelocity,
                                &cfg::kGetProjectileGravity, &cfg::kGetDeviceType, &cfg::kGetDeviceTypeIndex,
                                &cfg::kGetWeaponTypeAmmoProjectileByIndex, &cfg::kGetProjectileTypeGravity,
                                &cfg::kGetTeamCommanderPoints, &cfg::kAddTeamCommanderPoints, &cfg::kSetOverrideBuildTime,
                                &cfg::kIsCommanderActive, &cfg::kSetOverrideReloadTime, &cfg::kGetDeviceCount, &cfg::kGetDeviceId,
                                &cfg::kGetDevicePosition, &cfg::kGetDeviceHitpoints, &cfg::kGetDeviceInitialHitpoints,
                                &cfg::kRepairDevice, &cfg::kUpdateWeaponAim, &cfg::kSendScriptEvent,
                                &cfg::kGetWaterLevel, &cfg::kCastGroundRay, &cfg::kGetRayHitPosition, &cfg::kIsWeaponReadyToFire,
                                &cfg::kGetWeaponReloadTime, &cfg::kGetWeaponReloadPeriodById, &cfg::kGetProjectileTypeDrag,
                                &cfg::kGetLocalSelectedDeviceId, &cfg::kGetMousePos, &cfg::kScreenToWorld}) {
        if (!CheckFunction(*f, error)) return false;
    }
    return true;
}

bool GameApi::InWorld() const {
    return Guard([] {
        const std::uint8_t* g = Game();
        if (!g) return false;
        return *reinterpret_cast<void* const*>(g + cfg::kGameTeamsOff) != nullptr &&
               *reinterpret_cast<void* const*>(g + cfg::kGameSimOff) != nullptr &&
               *reinterpret_cast<void* const*>(g + cfg::kGameCameraOff) != nullptr;
    });
}

bool GameApi::Multiplayer(bool& online) const {
    return Guard([&] {
        const std::uint8_t* g = Game();
        if (!g) return false;
        online = g[cfg::kGameMultiplayerFlagOff] != 0;
        return true;
    });
}

bool GameApi::LocalTeam(int& team) const {
    return Guard([&] {
        team = Fn<FnGetLocalTeamId>(cfg::kGetLocalTeamId)();
        return team >= 0 && team <= cfg::kMaxTeamCode;
    });
}

bool GameApi::SetUnlimited(int team, bool on) const {
    return Guard([&] {
        Fn<FnSetUnlimited>(cfg::kSetUnlimitedResources)(team, on, on);
        return true;
    });
}

bool GameApi::Resources(int team, TeamResources& out) const {
    return Guard([&] {
        Fn<FnGetTeamResources>(cfg::kGetTeamResources)(&out, team);
        return std::isfinite(out.slotFromOff58) && std::isfinite(out.slotFromOff50);
    });
}

bool GameApi::OwnWeapons(int team, std::vector<int>& ids) const {
    ids.clear();
    int count = 0;
    if (!Guard([&] {
            count = Fn<FnGetWeaponCount>(cfg::kGetWeaponCount)(team);
            return count >= 0 && count <= kMaxWeapons;
        }))
        return false;
    for (int i = 0; i < count; ++i) {
        int id = 0;
        if (!Guard([&] {
                id = Fn<FnGetWeaponId>(cfg::kGetWeaponId)(team, i);
                return true;
            }))
            return false;
        if (id != 0) ids.push_back(id);
    }
    return true;
}

bool GameApi::BarrelPosition(int weaponId, Vec3& out) const {
    return Guard([&] {
        Fn<FnVec3ById>(cfg::kGetWeaponBarrelPosition)(&out, weaponId);
        return Plausible(out);
    });
}

bool GameApi::FireVelocity(int weaponId, Vec3& out) const {
    return Guard([&] {
        Fn<FnVec3ById>(cfg::kGetFireVel)(&out, weaponId);
        return Plausible(out);
    });
}

bool GameApi::ToScreen(const Vec3& world, Vec3& out) const {
    return Guard([&] {
        Fn<FnWorldToScreen>(cfg::kWorldToScreen)(&out, &world);
        return std::isfinite(out.x) && std::isfinite(out.y);
    });
}

bool GameApi::ProjectileIds(int team, std::vector<int>& ids) const {
    ids.clear();
    int count = 0;
    if (!Guard([&] {
            count = Fn<FnProjectileCount>(cfg::kProjectileCount)(team);
            return count >= 0 && count <= kMaxWeapons;
        }))
        return false;
    for (int i = 0; i < count; ++i) {
        int id = -1;
        if (!Guard([&] {
                id = Fn<FnGetProjectileId>(cfg::kGetProjectileId)(team, i);
                return true;
            }))
            return false;
        if (id >= 0) ids.push_back(id);
    }
    return true;
}

bool GameApi::NodePos(int id, Vec3& out) const {
    return Guard([&] {
        Fn<FnVec3ById>(cfg::kNodePosition)(&out, id);
        return Plausible(out);
    });
}

bool GameApi::NodeVel(int id, Vec3& out) const {
    return Guard([&] {
        Fn<FnVec3ById>(cfg::kNodeVelocity)(&out, id);
        return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
    });
}

bool GameApi::ProjectileGravity(int id, float& out) const {
    return Guard([&] {
        out = Fn<FnGetProjectileGravity>(cfg::kGetProjectileGravity)(id);
        return std::isfinite(out);
    });
}

bool GameApi::DeviceTypeName(int deviceId, std::string& out) const {
    const char* name = nullptr;
    if (!Guard([&] {
            name = Fn<FnGetDeviceType>(cfg::kGetDeviceType)(deviceId);
            return PrintableName(name);
        }))
        return false;
    out = name;
    return true;
}

bool GameApi::WeaponProjectile(int weaponId, int team, std::string& projectileName, float& gravity) const {
    std::string type;
    if (!DeviceTypeName(weaponId, type)) return false;
    const char* proj = nullptr;
    float g = 0;
    const bool ok = Guard([&] {
        const int typeIndex = Fn<FnGetDeviceTypeIndex>(cfg::kGetDeviceTypeIndex)(type.c_str());
        if (typeIndex < 0 || typeIndex > 10000) return false;
        proj = Fn<FnAmmoProjectile>(cfg::kGetWeaponTypeAmmoProjectileByIndex)(typeIndex, team, 0);
        if (!PrintableName(proj)) return false;
        g = Fn<FnProjectileTypeGravity>(cfg::kGetProjectileTypeGravity)(proj, team);
        return std::isfinite(g);
    });
    if (!ok) return false;
    projectileName = proj;
    gravity = g;
    return true;
}

bool GameApi::CommanderPoints(int team, float& out) const {
    return Guard([&] {
        out = Fn<FnCommanderPoints>(cfg::kGetTeamCommanderPoints)(team);
        return std::isfinite(out);
    });
}

bool GameApi::AddCommanderPoints(int team, float amount) const {
    return Guard([&] {
        Fn<FnAddCommanderPoints>(cfg::kAddTeamCommanderPoints)(team, amount);
        return true;
    });
}

bool GameApi::ReadBuildOverride(int team, float& a, float& b) const {
    return Guard([&] {
        const std::uint8_t* g = Game();
        if (!g || team < 0 || team > 2) return false;  // records exist for the first sides only
        const std::uint8_t* mgr = *reinterpret_cast<const std::uint8_t* const*>(g + cfg::kGameTeamsOff);
        if (!mgr) return false;
        const std::uint8_t* rec = mgr + cfg::kBuildOverrideBase + static_cast<std::ptrdiff_t>(team) * cfg::kBuildOverrideStride;
        std::memcpy(&a, rec + cfg::kBuildOverrideA, sizeof(a));
        std::memcpy(&b, rec + cfg::kBuildOverrideB, sizeof(b));
        return std::isfinite(a) && std::isfinite(b);
    });
}

bool GameApi::SetBuildOverride(int team, float a, float b) const {
    return Guard([&] {
        Fn<FnSetBuildOverride>(cfg::kSetOverrideBuildTime)(team, a, b);
        return true;
    });
}

bool GameApi::ReadReloadOverride(int team, float& a, float& b) const {
    return Guard([&] {
        const std::uint8_t* g = Game();
        if (!g || team < 0 || team > 2) return false;
        const std::uint8_t* mgr = *reinterpret_cast<const std::uint8_t* const*>(g + cfg::kGameTeamsOff);
        if (!mgr) return false;
        const std::uint8_t* rec = mgr + cfg::kBuildOverrideBase + static_cast<std::ptrdiff_t>(team) * cfg::kBuildOverrideStride;
        std::memcpy(&a, rec + cfg::kReloadOverrideA, sizeof(a));
        std::memcpy(&b, rec + cfg::kReloadOverrideB, sizeof(b));
        return std::isfinite(a) && std::isfinite(b);
    });
}

bool GameApi::SetReloadOverride(int team, float a, float b) const {
    return Guard([&] {
        Fn<FnSetBuildOverride>(cfg::kSetOverrideReloadTime)(team, a, b);
        return true;
    });
}

bool GameApi::CommanderActive(int team, bool& active) const {
    return Guard([&] {
        active = Fn<FnCommanderActive>(cfg::kIsCommanderActive)(team) != 0;
        return true;
    });
}

bool GameApi::MaterialBuildTimes(std::vector<MaterialBuild>& out) const {
    out.clear();
    return Guard([&] {
        for (int side = 0; side < cfg::kMaterialSides; ++side) {
            for (int i = 0; i < 64; ++i) {
                const std::uint8_t* rec = MaterialRecord(side, i);
                if (!rec) break;
                std::string name = ReadRecordName(rec + cfg::kMaterialName);
                float t = 0;
                std::memcpy(&t, rec + cfg::kMaterialBuildTime, sizeof(t));
                if (name.empty() || !std::isfinite(t) || t < 0.0f || t > 200.0f) continue;
                out.push_back({side, i, std::move(name), t});
            }
        }
        return !out.empty();
    });
}

bool GameApi::SetMaterialBuildTime(int side, int index, const std::string& expectedName, float value) const {
    return Guard([&] {
        const std::uint8_t* rec = MaterialRecord(side, index);
        if (!rec || ReadRecordName(rec + cfg::kMaterialName) != expectedName) return false;
        std::memcpy(const_cast<std::uint8_t*>(rec) + cfg::kMaterialBuildTime, &value, sizeof(value));
        return true;
    });
}

namespace {

// Link record of `node` that points to `other`, or nullptr.
std::uint8_t* FindLink(std::uint8_t* node, const std::uint8_t* other) {
    std::uint8_t* begin = *reinterpret_cast<std::uint8_t**>(node + cfg::kNodeLinksBegin);
    std::uint8_t* end = *reinterpret_cast<std::uint8_t**>(node + cfg::kNodeLinksEnd);
    if (!begin || end < begin || static_cast<std::size_t>(end - begin) % cfg::kLinkRecord != 0) return nullptr;
    const std::size_t n = static_cast<std::size_t>(end - begin) / cfg::kLinkRecord;
    if (n > 64) return nullptr;
    for (std::size_t i = 0; i < n; ++i) {
        std::uint8_t* rec = begin + i * cfg::kLinkRecord;
        if (*reinterpret_cast<const std::uint8_t* const*>(rec + cfg::kLinkOtherNode) == other) return rec;
    }
    return nullptr;
}

}  // namespace

bool GameApi::HealTeam(int teamCode, float fraction, bool devices, bool links, HealResult& result) const {
    result = {};
    if (!(fraction > 0.0f) || fraction > 1.0f) return false;
    bool ok = false;
    if (devices) {
        ok |= Guard([&] {
            const std::uint8_t* g = Game();
            if (!g) return false;
            const std::uint8_t* sim = *reinterpret_cast<const std::uint8_t* const*>(g + cfg::kGameSimOff);
            if (!sim) return false;
            std::uint8_t* const* begin = *reinterpret_cast<std::uint8_t* const* const*>(sim + cfg::kDeviceListBegin);
            std::uint8_t* const* end = *reinterpret_cast<std::uint8_t* const* const*>(sim + cfg::kDeviceListEnd);
            if (!begin || end < begin || static_cast<std::size_t>(end - begin) > 4096) return false;
            for (std::uint8_t* const* it = begin; it != end; ++it) {
                std::uint8_t* d = *it;
                if (!d) continue;
                int team = 0, id = 0;
                std::memcpy(&team, d + cfg::kDeviceTeam, sizeof(team));
                if (team != teamCode || d[cfg::kDeviceState] == 5) continue;
                std::memcpy(&id, d + cfg::kDeviceId, sizeof(id));
                float hp = 0;
                std::memcpy(&hp, d + cfg::kDeviceHealth, sizeof(hp));
                const float maxHp = Fn<FnGetDeviceHp>(cfg::kGetDeviceInitialHitpoints)(id);
                if (!std::isfinite(hp) || !std::isfinite(maxHp) || maxHp <= 0.0f || hp <= 0.0f || hp >= maxHp * 0.999f) continue;
                const float healed = std::min(maxHp, hp + fraction * maxHp);
                std::memcpy(d + cfg::kDeviceHealth, &healed, sizeof(healed));
                ++result.devices;
            }
            return true;
        });
    }
    if (links) {
        ok |= Guard([&] {
            const std::uint8_t* g = Game();
            if (!g) return false;
            const std::uint8_t* world = *reinterpret_cast<const std::uint8_t* const*>(g + cfg::kGameWorldOff);
            if (!world) return false;
            std::uint8_t* const* begin = *reinterpret_cast<std::uint8_t* const* const*>(world + cfg::kNodeListBegin);
            std::uint8_t* const* end = *reinterpret_cast<std::uint8_t* const* const*>(world + cfg::kNodeListEnd);
            if (!begin || end < begin || static_cast<std::size_t>(end - begin) > 20000) return false;
            const int side = std::clamp(((teamCode % 100) + 100) % 100 - 1, 0, cfg::kMaterialSides - 1);
            for (std::uint8_t* const* it = begin; it != end; ++it) {
                std::uint8_t* node = *it;
                if (!node || node[cfg::kNodeDead] != 0) continue;
                int team = 0;
                std::memcpy(&team, node + cfg::kNodeTeam, sizeof(team));
                if (team != teamCode) continue;
                std::uint8_t* lbegin = *reinterpret_cast<std::uint8_t**>(node + cfg::kNodeLinksBegin);
                std::uint8_t* lend = *reinterpret_cast<std::uint8_t**>(node + cfg::kNodeLinksEnd);
                if (!lbegin || lend <= lbegin || static_cast<std::size_t>(lend - lbegin) % cfg::kLinkRecord != 0) continue;
                const std::size_t count = static_cast<std::size_t>(lend - lbegin) / cfg::kLinkRecord;
                if (count > 64) continue;
                for (std::size_t i = 0; i < count; ++i) {
                    std::uint8_t* rec = lbegin + i * cfg::kLinkRecord;
                    const std::uint8_t state = rec[cfg::kLinkState];
                    if (state != 1 && state != 2) continue;  // only finished links (not under construction)
                    std::uint8_t* other = *reinterpret_cast<std::uint8_t**>(rec + cfg::kLinkOtherNode);
                    if (!other || other == node) continue;
                    int matIndex = 0;
                    float hp = 0;
                    std::memcpy(&matIndex, rec + cfg::kLinkMaterial, sizeof(matIndex));
                    std::memcpy(&hp, rec + cfg::kLinkHealth, sizeof(hp));
                    const std::uint8_t* mat = MaterialRecord(side, matIndex);
                    if (!mat) continue;
                    float maxHp = 0;
                    std::memcpy(&maxHp, mat + cfg::kMaterialMaxHealth, sizeof(maxHp));
                    if (!std::isfinite(hp) || !std::isfinite(maxHp) || maxHp <= 0.0f || hp <= 0.0f || hp > maxHp * 1.01f) continue;
                    ++result.linksSeen;
                    if (hp >= maxHp * 0.999f) continue;
                    const float healed = std::min(maxHp, hp + fraction * maxHp);
                    std::memcpy(rec + cfg::kLinkHealth, &healed, sizeof(healed));
                    if (std::uint8_t* back = FindLink(other, node)) std::memcpy(back + cfg::kLinkHealth, &healed, sizeof(healed));
                    ++result.links;
                }
            }
            return true;
        });
    }
    return ok;
}

bool GameApi::BaseSegments(int teamCode, std::vector<BaseSegment>& out) const {
    out.clear();
    return Guard([&] {
        const std::uint8_t* g = Game();
        if (!g) return false;
        const std::uint8_t* world = *reinterpret_cast<const std::uint8_t* const*>(g + cfg::kGameWorldOff);
        if (!world) return false;
        std::uint8_t* const* begin = *reinterpret_cast<std::uint8_t* const* const*>(world + cfg::kNodeListBegin);
        std::uint8_t* const* end = *reinterpret_cast<std::uint8_t* const* const*>(world + cfg::kNodeListEnd);
        if (!begin || end < begin || static_cast<std::size_t>(end - begin) > 20000) return false;
        const int side = std::clamp(((teamCode % 100) + 100) % 100 - 1, 0, cfg::kMaterialSides - 1);
        auto position = [](const std::uint8_t* n, float& x, float& y) {
            const std::ptrdiff_t off = n[cfg::kNodeMovingFlag] ? cfg::kNodePosMoving : cfg::kNodePosStatic;
            std::memcpy(&x, n + off, sizeof(x));
            std::memcpy(&y, n + off + 4, sizeof(y));
            return std::isfinite(x) && std::isfinite(y) && std::fabs(x) < 1.0e7f && std::fabs(y) < 1.0e7f;
        };
        for (std::uint8_t* const* it = begin; it != end && out.size() < 4000; ++it) {
            const std::uint8_t* node = *it;
            if (!node || node[cfg::kNodeDead] != 0) continue;
            int team = 0, id = 0;
            std::memcpy(&team, node + cfg::kNodeTeam, sizeof(team));
            if (team != teamCode) continue;
            std::memcpy(&id, node + cfg::kNodeId, sizeof(id));
            const std::uint8_t* lbegin = *reinterpret_cast<const std::uint8_t* const*>(node + cfg::kNodeLinksBegin);
            const std::uint8_t* lend = *reinterpret_cast<const std::uint8_t* const*>(node + cfg::kNodeLinksEnd);
            if (!lbegin || lend <= lbegin || static_cast<std::size_t>(lend - lbegin) % cfg::kLinkRecord != 0) continue;
            const std::size_t count = static_cast<std::size_t>(lend - lbegin) / cfg::kLinkRecord;
            if (count > 64) continue;
            float x1 = 0, y1 = 0;
            if (!position(node, x1, y1)) continue;
            for (std::size_t i = 0; i < count; ++i) {
                const std::uint8_t* rec = lbegin + i * cfg::kLinkRecord;
                const std::uint8_t* other = *reinterpret_cast<const std::uint8_t* const*>(rec + cfg::kLinkOtherNode);
                if (!other) continue;
                int otherId = 0;
                std::memcpy(&otherId, other + cfg::kNodeId, sizeof(otherId));
                if (otherId <= id) continue;  // every beam is stored in both nodes; take it once
                float x2 = 0, y2 = 0;
                if (!position(other, x2, y2)) continue;
                BaseSegment s;
                s.x1 = x1; s.y1 = y1; s.x2 = x2; s.y2 = y2;
                s.building = rec[cfg::kLinkState] != 1 && rec[cfg::kLinkState] != 2;
                int matIndex = 0;
                float hp = 0, maxHp = 0;
                std::memcpy(&matIndex, rec + cfg::kLinkMaterial, sizeof(matIndex));
                std::memcpy(&hp, rec + cfg::kLinkHealth, sizeof(hp));
                if (const std::uint8_t* mat = MaterialRecord(side, matIndex)) std::memcpy(&maxHp, mat + cfg::kMaterialMaxHealth, sizeof(maxHp));
                s.health = (std::isfinite(hp) && std::isfinite(maxHp) && maxHp > 0.0f) ? std::clamp(hp / maxHp, 0.0f, 1.0f) : 1.0f;
                out.push_back(s);
            }
        }
        return !out.empty();
    });
}

bool GameApi::RawDevices(std::vector<RawDevice>& out) const {
    out.clear();
    return Guard([&] {
        const std::uint8_t* g = Game();
        if (!g) return false;
        const std::uint8_t* sim = *reinterpret_cast<const std::uint8_t* const*>(g + cfg::kGameSimOff);
        if (!sim) return false;
        const std::uint8_t* const* begin = *reinterpret_cast<const std::uint8_t* const* const*>(sim + cfg::kDeviceListBegin);
        const std::uint8_t* const* end = *reinterpret_cast<const std::uint8_t* const* const*>(sim + cfg::kDeviceListEnd);
        if (!begin || end < begin || static_cast<std::size_t>(end - begin) > 4096) return false;
        for (const std::uint8_t* const* it = begin; it != end; ++it) {
            const std::uint8_t* d = *it;
            if (!d) continue;
            RawDevice r;
            std::memcpy(&r.id, d + cfg::kDeviceId, sizeof(r.id));
            std::memcpy(&r.teamCode, d + cfg::kDeviceTeam, sizeof(r.teamCode));
            r.state = d[cfg::kDeviceState];
            out.push_back(r);
        }
        return true;
    });
}

bool GameApi::DeviceIds(int team, std::vector<int>& ids) const {
    ids.clear();
    int count = 0;
    if (!Guard([&] {
            count = Fn<FnGetDeviceCount>(cfg::kGetDeviceCount)(team);
            return count >= 0 && count <= 2048;
        }))
        return false;
    for (int i = 0; i < count; ++i) {
        int id = 0;
        if (!Guard([&] {
                id = Fn<FnGetDeviceId>(cfg::kGetDeviceId)(team, i);
                return true;
            }))
            return false;
        if (id != 0) ids.push_back(id);
    }
    return true;
}

bool GameApi::DevicePosition(int deviceId, Vec3& out) const {
    return Guard([&] {
        Fn<FnVec3ById>(cfg::kGetDevicePosition)(&out, deviceId);
        return Plausible(out) && !(out.x == 0.0f && out.y == 0.0f);  // zero vector = device not found
    });
}

bool GameApi::DeviceHealth(int deviceId, float& hp, float& maxHp) const {
    return Guard([&] {
        hp = Fn<FnGetDeviceHp>(cfg::kGetDeviceHitpoints)(deviceId);
        maxHp = Fn<FnGetDeviceHp>(cfg::kGetDeviceInitialHitpoints)(deviceId);
        return std::isfinite(hp) && std::isfinite(maxHp) && maxHp > 0.0f;
    });
}

bool GameApi::RepairDevice(int deviceId) const {
    return Guard([&] {
        Fn<FnRepairDevice>(cfg::kRepairDevice)(deviceId);
        return true;
    });
}

bool GameApi::SendScriptEvent(const char* function, const char* params) const {
    return Guard([&] {
        Fn<FnSendScriptEvent>(cfg::kSendScriptEvent)(function, params, "", false);
        return true;
    });
}

bool GameApi::AimWeaponAt(int weaponId, const Vec3& target) const {
    return Guard([&] {
        Fn<FnUpdateWeaponAim>(cfg::kUpdateWeaponAim)(weaponId, &target);
        return true;
    });
}

bool GameApi::AimInput(float& angle, float& speed) const {
    return Guard([&] {
        angle = *reinterpret_cast<const float*>(g_base + cfg::kAimAngleRva);
        speed = *reinterpret_cast<const float*>(g_base + cfg::kAimSpeedRva);
        return std::isfinite(angle) && std::isfinite(speed);
    });
}

bool GameApi::ProjectileTypeDrag(const std::string& projectileName, int team, float& drag) const {
    return Guard([&] {
        drag = Fn<FnProjectileTypeGravity>(cfg::kGetProjectileTypeDrag)(projectileName.c_str(), team);  // same (name, team) -> float shape
        return std::isfinite(drag) && drag >= 0.0f && drag < 1.0e4f;
    });
}

bool GameApi::GroundRay(const Vec3& a, const Vec3& b, bool& hit, Vec3& point) const {
    return Guard([&] {
        hit = Fn<FnCastGroundRay>(cfg::kCastGroundRay)(&a, &b, 0) != 0;  // flags 0 = TERRAIN_NOFLAG as the game's own mods pass it
        if (!hit) return true;
        Fn<FnRayHitPosition>(cfg::kGetRayHitPosition)(&point);
        return Plausible(point);
    });
}

bool GameApi::WaterLevelAt(float x, float& level) const {
    return Guard([&] {
        level = Fn<FnWaterLevel>(cfg::kGetWaterLevel)(x);
        return std::isfinite(level);
    });
}

bool GameApi::SelectedDevice(int& deviceId) const {
    return Guard([&] {
        deviceId = Fn<FnGetLocalTeamId>(cfg::kGetLocalSelectedDeviceId)();
        return true;
    });
}

bool GameApi::MouseWorld(Vec3& screen, Vec3& world) const {
    return Guard([&] {
        Fn<FnVec3Out>(cfg::kGetMousePos)(&screen);
        Fn<FnWorldToScreen>(cfg::kScreenToWorld)(&world, &screen);
        return Plausible(screen) && Plausible(world);
    });
}

bool GameApi::ScreenToWorldPoint(const Vec3& screen, Vec3& world) const {
    return Guard([&] {
        Fn<FnWorldToScreen>(cfg::kScreenToWorld)(&world, &screen);
        return Plausible(world);
    });
}

bool GameApi::WeaponReady(int deviceId, bool& ready) const {
    return Guard([&] {
        ready = Fn<FnWeaponReady>(cfg::kIsWeaponReadyToFire)(deviceId) != 0;
        return true;
    });
}

bool GameApi::WeaponReload(int deviceId, float& value, float& period) const {
    return Guard([&] {
        value = Fn<FnWeaponSeconds>(cfg::kGetWeaponReloadTime)(deviceId);
        period = Fn<FnWeaponSeconds>(cfg::kGetWeaponReloadPeriodById)(deviceId);
        return std::isfinite(value) && std::isfinite(period) && period >= 0.0f && period < 1.0e4f && std::fabs(value) < 1.0e5f;
    });
}

bool GameApi::BaseGraphOf(int teamCode, BaseGraph& out) const {
    out.nodes.clear();
    out.edges.clear();
    return Guard([&] {
        const std::uint8_t* g = Game();
        if (!g) return false;
        const std::uint8_t* world = *reinterpret_cast<const std::uint8_t* const*>(g + cfg::kGameWorldOff);
        if (!world) return false;
        std::uint8_t* const* begin = *reinterpret_cast<std::uint8_t* const* const*>(world + cfg::kNodeListBegin);
        std::uint8_t* const* end = *reinterpret_cast<std::uint8_t* const* const*>(world + cfg::kNodeListEnd);
        if (!begin || end < begin || static_cast<std::size_t>(end - begin) > 20000) return false;
        const int side = std::clamp(((teamCode % 100) + 100) % 100 - 1, 0, cfg::kMaterialSides - 1);
        auto position = [](const std::uint8_t* n, float& x, float& y) {
            const std::ptrdiff_t off = n[cfg::kNodeMovingFlag] ? cfg::kNodePosMoving : cfg::kNodePosStatic;
            std::memcpy(&x, n + off, sizeof(x));
            std::memcpy(&y, n + off + 4, sizeof(y));
            return std::isfinite(x) && std::isfinite(y) && std::fabs(x) < 1.0e7f && std::fabs(y) < 1.0e7f;
        };
        // Pass 1: the live nodes of this team.
        std::vector<const std::uint8_t*> ptrs;
        for (std::uint8_t* const* it = begin; it != end && ptrs.size() < 6000; ++it) {
            const std::uint8_t* node = *it;
            if (!node || node[cfg::kNodeDead] != 0) continue;
            int team = 0;
            std::memcpy(&team, node + cfg::kNodeTeam, sizeof(team));
            if (team != teamCode) continue;
            GraphNode n;
            if (!position(node, n.x, n.y)) continue;
            std::memcpy(&n.id, node + cfg::kNodeId, sizeof(n.id));
            int ground = -1;
            std::memcpy(&ground, node + cfg::kNodeGroundBlock, sizeof(ground));
            n.ground = ground;
            n.anchored = ground >= 0 && ground < 100000;
            out.nodes.push_back(n);
            ptrs.push_back(node);
        }
        if (out.nodes.empty()) return false;
        std::unordered_map<int, int> index;
        for (std::size_t i = 0; i < out.nodes.size(); ++i) index[out.nodes[i].id] = static_cast<int>(i);
        // Pass 2: every link once (it sits in both of its nodes; take it from the node with the smaller id).
        for (std::size_t i = 0; i < ptrs.size() && out.edges.size() < 12000; ++i) {
            const std::uint8_t* node = ptrs[i];
            const std::uint8_t* lbegin = *reinterpret_cast<const std::uint8_t* const*>(node + cfg::kNodeLinksBegin);
            const std::uint8_t* lend = *reinterpret_cast<const std::uint8_t* const*>(node + cfg::kNodeLinksEnd);
            if (!lbegin || lend <= lbegin || static_cast<std::size_t>(lend - lbegin) % cfg::kLinkRecord != 0) continue;
            const std::size_t count = static_cast<std::size_t>(lend - lbegin) / cfg::kLinkRecord;
            if (count > 64) continue;
            for (std::size_t j = 0; j < count; ++j) {
                const std::uint8_t* rec = lbegin + j * cfg::kLinkRecord;
                const std::uint8_t* other = *reinterpret_cast<const std::uint8_t* const*>(rec + cfg::kLinkOtherNode);
                if (!other) continue;
                int otherId = 0;
                std::memcpy(&otherId, other + cfg::kNodeId, sizeof(otherId));
                if (otherId <= out.nodes[i].id) continue;
                const auto found = index.find(otherId);
                if (found == index.end()) continue;
                GraphEdge e;
                e.a = static_cast<int>(i);
                e.b = found->second;
                e.building = rec[cfg::kLinkState] != 1 && rec[cfg::kLinkState] != 2;
                int matIndex = 0;
                float hp = 0, maxHp = 0;
                std::memcpy(&matIndex, rec + cfg::kLinkMaterial, sizeof(matIndex));
                std::memcpy(&hp, rec + cfg::kLinkHealth, sizeof(hp));
                if (const std::uint8_t* mat = MaterialRecord(side, matIndex)) std::memcpy(&maxHp, mat + cfg::kMaterialMaxHealth, sizeof(maxHp));
                e.health = (std::isfinite(hp) && std::isfinite(maxHp) && maxHp > 0.0f) ? std::clamp(hp / maxHp, 0.0f, 1.0f) : 1.0f;
                out.edges.push_back(e);
            }
        }
        return !out.edges.empty();
    });
}

bool GameApi::CameraFocus(Vec3& out) const {
    return Guard([&] {
        const std::uint8_t* g = Game();
        if (!g) return false;
        const std::uint8_t* cam = *reinterpret_cast<const std::uint8_t* const*>(g + cfg::kGameCameraOff);
        if (!cam) return false;
        std::memcpy(&out, cam + cfg::kCameraFocusOff, sizeof(out));
        return Plausible(out);
    });
}

}  // namespace forts
