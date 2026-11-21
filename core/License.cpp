#include "License.hpp"

#include <windows.h>
#include <winhttp.h>

#include <cctype>
#include <cstdlib>
#include <vector>

#include "Update.hpp"

namespace aero::license {
namespace {

struct InetHandle {
    HINTERNET h = nullptr;
    explicit InetHandle(HINTERNET v) : h(v) {}
    ~InetHandle() { if (h) WinHttpCloseHandle(h); }
    InetHandle(const InetHandle&) = delete;
    InetHandle& operator=(const InetHandle&) = delete;
};

// Position just after "name": (and spaces), or npos.
std::size_t ValueAt(std::string_view json, std::string_view name) {
    const std::string needle = "\"" + std::string(name) + "\"";
    std::size_t p = json.find(needle);
    if (p == std::string_view::npos) return p;
    p += needle.size();
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) ++p;
    if (p >= json.size() || json[p] != ':') return std::string_view::npos;
    ++p;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) ++p;
    return p;
}

}  // namespace

Result ParseReply(std::string_view json) {
    Result r;
    const std::size_t ok = ValueAt(json, "ok");
    if (ok == std::string_view::npos) return r;
    r.reached = true;
    r.ok = json.substr(ok, 4) == "true";
    if (const std::size_t p = ValueAt(json, "reason"); p != std::string_view::npos && p < json.size() && json[p] == '"') {
        const std::size_t end = json.find('"', p + 1);
        if (end != std::string_view::npos && end - p - 1 <= 64) r.reason = std::string(json.substr(p + 1, end - p - 1));
    }
    if (const std::size_t p = ValueAt(json, "username"); p != std::string_view::npos && p < json.size() && json[p] == '"') {
        const std::size_t end = json.find('"', p + 1);
        if (end != std::string_view::npos && end - p - 1 <= 32) r.username = std::string(json.substr(p + 1, end - p - 1));
    }
    if (const std::size_t p = ValueAt(json, "expires"); p != std::string_view::npos && p < json.size() &&
                                                       std::isdigit(static_cast<unsigned char>(json[p]))) {
        std::int64_t v = 0;
        for (std::size_t i = p; i < json.size() && i < p + 16 && std::isdigit(static_cast<unsigned char>(json[i])); ++i) v = v * 10 + (json[i] - '0');
        r.expiresMs = v;
    }
    if (const std::size_t g = ValueAt(json, "games"); g != std::string_view::npos && g < json.size() && json[g] == '[') {
        const std::size_t end = json.find(']', g);
        std::string_view arr = json.substr(g, end == std::string_view::npos ? std::string_view::npos : end - g);
        for (std::size_t p = ValueAt(arr, "id"); p != std::string_view::npos && r.games.size() < 32; ) {
            if (p >= arr.size() || arr[p] != '"') break;
            const std::size_t q = arr.find('"', p + 1);
            if (q == std::string_view::npos || q - p - 1 > 32) break;
            r.games.emplace_back(arr.substr(p + 1, q - p - 1));
            arr = arr.substr(q + 1);
            p = ValueAt(arr, "id");
        }
    }
    if (!r.ok && r.reason.empty()) r.reason = "unknown";
    return r;
}

std::wstring NormalizeKey(std::wstring_view text) {
    std::wstring k;
    for (wchar_t c : text) {
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') continue;
        k += (c >= L'a' && c <= L'z') ? static_cast<wchar_t>(c - 32) : c;
    }
    if (k.size() != 24 || k.compare(0, 5, L"AERO-") != 0) return {};
    for (std::size_t i = 5; i < k.size(); ++i) {
        const bool dash = i == 9 || i == 14 || i == 19;
        const wchar_t c = k[i];
        if (dash ? c != L'-' : !((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9'))) return {};
    }
    return k;
}

std::string MachineId() {
    wchar_t guid[128]{};
    DWORD size = sizeof(guid);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", L"MachineGuid", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
                     nullptr, guid, &size) != ERROR_SUCCESS || !guid[0])
        return {};
    const std::wstring salted = std::wstring(L"aerohub:") + guid;
    const std::string hex = aero::update::Sha256Hex(salted.data(), salted.size() * sizeof(wchar_t));
    return hex.size() >= 32 ? hex.substr(0, 32) : std::string();
}

namespace {
// POST a JSON body to a path of the site; returns the reply body, or false with `error` set.
bool Post(const wchar_t* pathOnSite, const std::string& body, std::string& reply, std::wstring& error) {
    auto bad = [&](const wchar_t* what) {
        error = std::wstring(what) + L" (error " + std::to_wstring(GetLastError()) + L")";
        return false;
    };
    wchar_t host[256]{}, path[512]{};
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;
    uc.dwHostNameLength = static_cast<DWORD>(std::size(host));
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    if (!WinHttpCrackUrl(kSiteUrl, 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) return bad(L"bad site URL");
    InetHandle session(WinHttpOpen(L"AeroHub-License", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) return bad(L"WinHttpOpen failed");
    WinHttpSetTimeouts(session.h, 8000, 8000, 10000, 10000);
    InetHandle connection(WinHttpConnect(session.h, host, uc.nPort, 0));
    if (!connection.h) return bad(L"cannot connect");
    InetHandle request(WinHttpOpenRequest(connection.h, L"POST", pathOnSite, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.h) return bad(L"cannot open request");
    const wchar_t headers[] = L"Content-Type: application/json\r\n";
    if (!WinHttpSendRequest(request.h, headers, static_cast<DWORD>(-1), const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                            static_cast<DWORD>(body.size()), 0) ||
        !WinHttpReceiveResponse(request.h, nullptr))
        return bad(L"request failed");
    reply.clear();
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request.h, &avail)) return bad(L"read failed");
        if (avail == 0) break;
        if (reply.size() + avail > 4096) {
            error = L"reply too large";
            return false;
        }
        const std::size_t at = reply.size();
        reply.resize(at + avail);
        DWORD got = 0;
        if (!WinHttpReadData(request.h, reply.data() + at, avail, &got)) return bad(L"read failed");
        reply.resize(at + got);
    }
    return true;
}

std::string Body(const std::wstring& key, const std::string& machineId) {
    return "{\"key\":\"" + std::string(key.begin(), key.end()) + "\",\"hwid\":\"" + machineId + "\"}";  // key is ASCII after NormalizeKey
}
}  // namespace

bool Release(const std::wstring& key, const std::string& machineId) {
    std::string reply;
    std::wstring error;
    return Post(kReleasePath, Body(key, machineId), reply, error) && ParseReply(reply).ok;
}

Result Check(const std::wstring& key, const std::string& machineId) {
    Result r;
    std::string reply;
    if (!Post(kCheckPath, Body(key, machineId), reply, r.error)) return r;
    r = ParseReply(reply);
    if (!r.reached) r.error = L"unexpected reply from the site";
    return r;
}

}  // namespace aero::license
