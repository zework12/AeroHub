#pragma once
// Control channel between the hub and a game module: one shared-memory block per game process.
// The module publishes a list of features (toggles / buttons) and live telemetry; the hub shows them
// and sends requests. Game-agnostic: a module registers whatever features it has.
#include <windows.h>

#include <cstdint>
#include <string>

namespace aero::ipc {

constexpr std::uint32_t kMagic = 0x4F524541;  // "AERO"
constexpr std::uint32_t kVersion = 1;
constexpr int kMaxFeatures = 24;
constexpr int kMaxTelemetry = 16;

enum Kind : LONG { kToggle = 0, kButton = 1 };
enum Request : LONG { kNone = 0, kSetOn = 1, kSetOff = 2, kPress = 3 };

struct Feature {
    char name[32];
    LONG kind;
    LONG state;    // module -> hub (toggles)
    LONG request;  // hub -> module, cleared by the module when taken
    LONG reserved;
};

struct Telemetry {
    char name[32];
    char value[64];
};

struct Block {
    std::uint32_t magic;
    std::uint32_t version;
    LONG heartbeat;  // incremented by the module every rendered frame
    LONG featureCount;
    LONG telemetryCount;
    LONG reserved;
    Feature features[kMaxFeatures];
    Telemetry telemetry[kMaxTelemetry];
};

std::wstring MappingName(DWORD pid);

// Module side.
class Server {
public:
    Server() = default;
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool Create();
    int AddFeature(const char* name, Kind kind);  // -1 when full / not created
    void SetState(int feature, bool on);
    LONG TakeRequest(int feature);  // returns the pending Request and clears it
    void SetTelemetry(int slot, const char* name, const char* value);
    void Beat();
    bool Ready() const { return block_ != nullptr; }

private:
    HANDLE map_ = nullptr;
    Block* block_ = nullptr;
};

// Hub side.
class Client {
public:
    Client() = default;
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    ~Client() { Close(); }

    bool Open(DWORD pid);
    void Close();
    bool Connected() const { return block_ != nullptr; }
    const Block* View() const { return block_; }
    void SendRequest(int feature, LONG request);

private:
    HANDLE map_ = nullptr;
    Block* block_ = nullptr;
};

}  // namespace aero::ipc
