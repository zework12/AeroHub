#pragma once
// Licence check against the AeroHub site: POST /api/license/check {"key", "hwid"}. The first successful check binds the key
// to this machine id on the server; the owner can reset that binding from the account page.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace aero::license {

inline constexpr wchar_t kSiteUrl[] = L"https://aerohub.155-212-218-127.sslip.io";
inline constexpr wchar_t kCheckPath[] = L"/api/license/check";
inline constexpr wchar_t kReleasePath[] = L"/api/license/release";

struct Result {
    bool reached = false;   // the server answered with a readable reply
    bool ok = false;        // the key is active and bound to this machine
    std::string reason;     // server reason when !ok: "not redeemed", "expired", "revoked", "unknown", "other machine", "rate"
    std::int64_t expiresMs = 0;  // 0 = no end date
    std::string username;   // owner of the key on the site
    std::vector<std::string> games;  // game ids the account may use ("forts", "minecraft", ...), gifts included
    std::wstring error;     // network error text when !reached
};

// Pure: reads the server's JSON reply ({"ok":true,"expires":123|null} or {"ok":false,"reason":"..."}).
Result ParseReply(std::string_view json);

// "AERO-XXXX-XXXX-XXXX-XXXX" after trimming and upper-casing; empty if the text is not in that shape.
std::wstring NormalizeKey(std::wstring_view text);

// Stable id of this Windows installation: SHA-256 of the MachineGuid, 32 hex digits. Empty on failure.
std::string MachineId();

// Network (blocking, with timeouts).
Result Check(const std::wstring& key, const std::string& machineId);
// Network: the key forgets this machine (sign-out), so it can be used on another computer. True if the site confirmed.
bool Release(const std::wstring& key, const std::string& machineId);

}  // namespace aero::license
