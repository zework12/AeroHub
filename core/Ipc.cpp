#include "Ipc.hpp"

#include <algorithm>
#include <cstring>

namespace aero::ipc {
namespace {

void CopyText(char* dst, std::size_t cap, const char* src) {
    const std::size_t n = src ? std::min(std::strlen(src), cap - 1) : 0;
    if (n) std::memcpy(dst, src, n);
    dst[n] = '\0';
}

}  // namespace

std::wstring MappingName(DWORD pid) { return L"Local\\AeroHub_Control_" + std::to_wstring(pid); }

bool Server::Create() {
    if (block_) return true;
    map_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(Block),
                              MappingName(GetCurrentProcessId()).c_str());
    if (!map_) return false;
    void* view = MapViewOfFile(map_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Block));
    if (!view) {
        CloseHandle(map_);
        map_ = nullptr;
        return false;
    }
    auto* block = static_cast<Block*>(view);
    std::memset(block, 0, sizeof(Block));
    block->version = kVersion;
    MemoryBarrier();
    block->magic = kMagic;  // the hub ignores the block until this is set
    MemoryBarrier();
    block_ = block;  // published to the module's own threads last
    return true;
}

int Server::AddFeature(const char* name, Kind kind) {
    if (!block_ || block_->featureCount >= kMaxFeatures) return -1;
    const int i = block_->featureCount;
    CopyText(block_->features[i].name, sizeof(block_->features[i].name), name);
    block_->features[i].kind = kind;
    MemoryBarrier();
    block_->featureCount = i + 1;
    return i;
}

void Server::SetState(int feature, bool on) {
    if (block_ && feature >= 0 && feature < block_->featureCount) InterlockedExchange(&block_->features[feature].state, on ? 1 : 0);
}

LONG Server::TakeRequest(int feature) {
    if (!block_ || feature < 0 || feature >= block_->featureCount) return kNone;
    return InterlockedExchange(&block_->features[feature].request, kNone);
}

void Server::SetTelemetry(int slot, const char* name, const char* value) {
    if (!block_ || slot < 0 || slot >= kMaxTelemetry) return;
    CopyText(block_->telemetry[slot].name, sizeof(block_->telemetry[slot].name), name);
    CopyText(block_->telemetry[slot].value, sizeof(block_->telemetry[slot].value), value);
    if (slot >= block_->telemetryCount) block_->telemetryCount = slot + 1;
}

void Server::Beat() {
    if (block_) InterlockedIncrement(&block_->heartbeat);
}

bool Client::Open(DWORD pid) {
    Close();
    map_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MappingName(pid).c_str());
    if (!map_) return false;
    void* view = MapViewOfFile(map_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Block));
    if (!view) {
        Close();
        return false;
    }
    auto* b = static_cast<Block*>(view);
    if (b->magic != kMagic || b->version != kVersion) {
        UnmapViewOfFile(view);
        Close();
        return false;
    }
    block_ = b;
    return true;
}

void Client::Close() {
    if (block_) UnmapViewOfFile(block_);
    block_ = nullptr;
    if (map_) CloseHandle(map_);
    map_ = nullptr;
}

void Client::SendRequest(int feature, LONG request) {
    if (block_ && feature >= 0 && feature < block_->featureCount) InterlockedExchange(&block_->features[feature].request, request);
}

}  // namespace aero::ipc
