#include "Update.hpp"

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <cctype>
#include <cstdint>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace aero::update {

namespace {

constexpr wchar_t kLatestUrl[] = L"https://api.github.com/repos/zework12/AeroHub/releases/latest";
constexpr wchar_t kDownloadPrefix[] = L"https://github.com/zework12/AeroHub/releases/download/";
constexpr char kAssetName[] = "AeroHub-Setup.exe";
constexpr std::size_t kMaxJsonBytes = 2u << 20;
constexpr std::size_t kMaxInstallerBytes = 64u << 20;
constexpr std::size_t kMinInstallerBytes = 100u << 10;

// ---------------------------------------------------------------- minimal JSON reader (enough for the release document)
struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    std::string text;  // string value, or the literal of a number / bool
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    const Json* Find(const char* key) const {
        for (const auto& m : members)
            if (m.first == key) return &m.second;
        return nullptr;
    }
    const std::string* Str(const char* key) const {
        const Json* j = Find(key);
        return j && j->type == Type::String ? &j->text : nullptr;
    }
};

class JsonReader {
public:
    explicit JsonReader(const std::string& s) : s_(s) {}
    bool Parse(Json& out) {
        if (!Value(out, 0)) return false;
        Space();
        return p_ == s_.size();
    }

private:
    void Space() {
        while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\t' || s_[p_] == '\n' || s_[p_] == '\r')) ++p_;
    }
    bool Literal(const char* word, Json& out, Json::Type type) {
        const std::size_t n = std::char_traits<char>::length(word);
        if (s_.compare(p_, n, word) != 0) return false;
        p_ += n;
        out.type = type;
        out.text = word;
        return true;
    }
    bool String(std::string& out) {
        if (p_ >= s_.size() || s_[p_] != '"') return false;
        ++p_;
        while (p_ < s_.size()) {
            const char c = s_[p_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (p_ >= s_.size()) return false;
            const char e = s_[p_++];
            switch (e) {
                case '"': case '\\': case '/': out.push_back(e); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u':
                    if (p_ + 4 > s_.size()) return false;
                    p_ += 4;           // the fields we read are ASCII; any other character becomes '?'
                    out.push_back('?');
                    break;
                default: return false;
            }
        }
        return false;
    }
    bool Value(Json& out, int depth) {
        if (depth > 32) return false;
        Space();
        if (p_ >= s_.size()) return false;
        const char c = s_[p_];
        if (c == '{') {
            ++p_;
            out.type = Json::Type::Object;
            Space();
            if (p_ < s_.size() && s_[p_] == '}') { ++p_; return true; }
            for (;;) {
                Space();
                std::string key;
                if (!String(key)) return false;
                Space();
                if (p_ >= s_.size() || s_[p_++] != ':') return false;
                Json v;
                if (!Value(v, depth + 1)) return false;
                out.members.emplace_back(std::move(key), std::move(v));
                Space();
                if (p_ >= s_.size()) return false;
                if (s_[p_] == ',') { ++p_; continue; }
                if (s_[p_] == '}') { ++p_; return true; }
                return false;
            }
        }
        if (c == '[') {
            ++p_;
            out.type = Json::Type::Array;
            Space();
            if (p_ < s_.size() && s_[p_] == ']') { ++p_; return true; }
            for (;;) {
                Json v;
                if (!Value(v, depth + 1)) return false;
                out.items.push_back(std::move(v));
                Space();
                if (p_ >= s_.size()) return false;
                if (s_[p_] == ',') { ++p_; continue; }
                if (s_[p_] == ']') { ++p_; return true; }
                return false;
            }
        }
        if (c == '"') {
            out.type = Json::Type::String;
            return String(out.text);
        }
        if (c == 't') return Literal("true", out, Json::Type::Bool);
        if (c == 'f') return Literal("false", out, Json::Type::Bool);
        if (c == 'n') return Literal("null", out, Json::Type::Null);
        const std::size_t start = p_;
        while (p_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[p_])) || s_[p_] == '-' || s_[p_] == '+' || s_[p_] == '.' || s_[p_] == 'e' || s_[p_] == 'E')) ++p_;
        if (p_ == start) return false;
        out.type = Json::Type::Number;
        out.text = s_.substr(start, p_ - start);
        return true;
    }
    const std::string& s_;
    std::size_t p_ = 0;
};

std::wstring Widen(const std::string& s) {  // the release fields we use are ASCII
    std::wstring w;
    w.reserve(s.size());
    for (char c : s) w.push_back(static_cast<unsigned char>(c));
    return w;
}

bool IsLowerHex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

// ---------------------------------------------------------------- WinHTTP
struct InetHandle {
    HINTERNET h = nullptr;
    InetHandle() = default;
    explicit InetHandle(HINTERNET handle) : h(handle) {}
    InetHandle(const InetHandle&) = delete;
    InetHandle& operator=(const InetHandle&) = delete;
    ~InetHandle() {
        if (h) WinHttpCloseHandle(h);
    }
};

std::wstring LastErrorText(const wchar_t* what) {
    return std::wstring(what) + L" (error " + std::to_wstring(GetLastError()) + L")";
}

bool HttpGet(const std::wstring& url, std::size_t maxBytes, std::vector<unsigned char>& out, std::wstring& error) {
    wchar_t host[256]{}, path[2048]{}, extra[2048]{};
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = host;
    uc.dwHostNameLength = static_cast<DWORD>(std::size(host));
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = static_cast<DWORD>(std::size(path));
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = static_cast<DWORD>(std::size(extra));
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS) {
        error = L"bad update URL";
        return false;
    }
    InetHandle session(WinHttpOpen(L"AeroHub-Updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) {
        error = LastErrorText(L"WinHttpOpen failed");
        return false;
    }
    WinHttpSetTimeouts(session.h, 10000, 10000, 20000, 30000);
    InetHandle connection(WinHttpConnect(session.h, host, uc.nPort, 0));
    if (!connection.h) {
        error = LastErrorText(L"cannot connect");
        return false;
    }
    const std::wstring target = std::wstring(path) + extra;
    InetHandle request(WinHttpOpenRequest(connection.h, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request.h) {
        error = LastErrorText(L"cannot open request");
        return false;
    }
    const wchar_t headers[] = L"Accept: application/vnd.github+json, application/octet-stream\r\n";
    if (!WinHttpSendRequest(request.h, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.h, nullptr)) {
        error = LastErrorText(L"request failed");
        return false;
    }
    DWORD status = 0, size = sizeof(status);
    if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) || status != 200) {
        error = L"server answered HTTP " + std::to_wstring(status);
        return false;
    }
    out.clear();
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request.h, &avail)) {
            error = LastErrorText(L"read failed");
            return false;
        }
        if (avail == 0) break;
        if (out.size() + avail > maxBytes) {
            error = L"response is too large";
            return false;
        }
        const std::size_t at = out.size();
        out.resize(at + avail);
        DWORD got = 0;
        if (!WinHttpReadData(request.h, out.data() + at, avail, &got)) {
            error = LastErrorText(L"read failed");
            return false;
        }
        out.resize(at + got);
    }
    return true;
}

}  // namespace

Version ParseVersion(std::wstring_view text) {
    Version v;
    std::size_t i = 0;
    if (i < text.size() && (text[i] == L'v' || text[i] == L'V')) ++i;
    int parts[3] = {0, 0, 0};
    for (int n = 0; n < 3; ++n) {
        if (n > 0) {
            if (i >= text.size() || text[i] != L'.') return {};
            ++i;
        }
        std::size_t digits = 0;
        long long value = 0;
        while (i < text.size() && text[i] >= L'0' && text[i] <= L'9') {
            value = value * 10 + (text[i] - L'0');
            if (++digits > 6) return {};
            ++i;
        }
        if (digits == 0) return {};
        parts[n] = static_cast<int>(value);
    }
    if (i != text.size()) return {};
    v.major = parts[0];
    v.minor = parts[1];
    v.patch = parts[2];
    v.ok = true;
    return v;
}

bool IsNewer(const Version& latest, const Version& current) {
    if (!latest.ok || !current.ok) return false;
    if (latest.major != current.major) return latest.major > current.major;
    if (latest.minor != current.minor) return latest.minor > current.minor;
    return latest.patch > current.patch;
}

Release ParseRelease(const std::string& json) {
    Release r;
    Json root;
    if (!JsonReader(json).Parse(root) || root.type != Json::Type::Object) {
        r.error = L"unreadable release data";
        return r;
    }
    const std::string* tag = root.Str("tag_name");
    if (!tag || !ParseVersion(Widen(*tag)).ok) {
        r.error = L"the latest release has no usable version tag";
        return r;
    }
    r.tag = Widen(*tag);
    const Json* assets = root.Find("assets");
    if (assets && assets->type == Json::Type::Array) {
        for (const Json& a : assets->items) {
            const std::string* name = a.Str("name");
            if (!name || *name != kAssetName) continue;
            const std::string* url = a.Str("browser_download_url");
            const std::string* digest = a.Str("digest");
            if (!url || !digest) break;
            const std::wstring wurl = Widen(*url);
            if (wurl.compare(0, std::size(kDownloadPrefix) - 1, kDownloadPrefix) != 0) {
                r.error = L"the installer URL is not on the project's GitHub release path";
                return r;
            }
            static constexpr char kPrefix[] = "sha256:";
            if (digest->compare(0, sizeof(kPrefix) - 1, kPrefix) != 0 || !IsLowerHex64(digest->substr(sizeof(kPrefix) - 1))) {
                r.error = L"the installer has no SHA-256 digest to verify against";
                return r;
            }
            r.url = wurl;
            r.sha256 = Widen(digest->substr(sizeof(kPrefix) - 1));
            r.ok = true;
            return r;
        }
    }
    r.error = L"the release has no verifiable AeroHub-Setup.exe";
    return r;
}

std::string Sha256Hex(const void* data, std::size_t size) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    UCHAR hash[32]{};
    const NTSTATUS st = BCryptHash(alg, nullptr, 0, static_cast<PUCHAR>(const_cast<void*>(data)), static_cast<ULONG>(size), hash, sizeof(hash));
    BCryptCloseAlgorithmProvider(alg, 0);
    if (st < 0) return {};
    static constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    for (UCHAR b : hash) {
        hex.push_back(digits[b >> 4]);
        hex.push_back(digits[b & 15]);
    }
    return hex;
}

Release FetchLatest() {
    Release r;
    std::vector<unsigned char> body;
    if (!HttpGet(kLatestUrl, kMaxJsonBytes, body, r.error)) return r;
    return ParseRelease(std::string(body.begin(), body.end()));
}

bool Download(const Release& release, const std::filesystem::path& dest, std::wstring& error) {
    if (!release.ok) {
        error = L"no release to download";
        return false;
    }
    std::vector<unsigned char> data;
    if (!HttpGet(release.url, kMaxInstallerBytes, data, error)) return false;
    if (data.size() < kMinInstallerBytes || data[0] != 'M' || data[1] != 'Z') {
        error = L"the downloaded file is not an installer";
        return false;
    }
    const std::string got = Sha256Hex(data.data(), data.size());
    if (got.empty() || Widen(got) != release.sha256) {
        error = L"SHA-256 of the download does not match the release; it was discarded";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(dest.parent_path(), ec);
    std::filesystem::remove(dest, ec);
    {
        std::ofstream f(dest, std::ios::binary | std::ios::trunc);
        if (!f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
            error = L"cannot write " + dest.wstring();
            f.close();
            std::filesystem::remove(dest, ec);
            return false;
        }
    }
    return true;
}

}  // namespace aero::update
