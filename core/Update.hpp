#pragma once
// Self-update through the GitHub releases of the project. Nothing is installed without the user's click: the hub only asks
// GitHub for the latest release, and on request downloads AeroHub-Setup.exe, checks its SHA-256 against the digest GitHub
// reports for that asset, and hands it to the caller to run.
#include <filesystem>
#include <string>
#include <string_view>

namespace aero::update {

struct Version {
    int major = 0, minor = 0, patch = 0;
    bool ok = false;
};

// "1.3.1" or "v1.3.1" (exactly three numeric parts; anything else, e.g. a pre-release suffix, is not ok).
Version ParseVersion(std::wstring_view text);
// Both versions must be ok.
bool IsNewer(const Version& latest, const Version& current);

struct Release {
    bool ok = false;
    std::wstring error;
    std::wstring tag;     // "v1.3.1"
    std::wstring url;     // https download URL of AeroHub-Setup.exe (checked against the project's release path)
    std::wstring sha256;  // 64 lowercase hex digits, from the asset's "digest" field
};

// Pure: reads the JSON of GET /repos/<owner>/<repo>/releases/latest. A release without an AeroHub-Setup.exe asset that carries a
// sha256 digest is rejected, so an unverifiable file is never offered.
Release ParseRelease(const std::string& json);

// Network (blocking, with timeouts): ask GitHub for the latest release.
Release FetchLatest();
// Network (blocking): download the release's installer into `dest`, verify size, "MZ" header and SHA-256. On failure nothing is left at dest.
bool Download(const Release& release, const std::filesystem::path& dest, std::wstring& error);

// Lowercase hex SHA-256 of a buffer; empty on failure.
std::string Sha256Hex(const void* data, std::size_t size);

}  // namespace aero::update
