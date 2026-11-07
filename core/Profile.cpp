#include "Profile.hpp"

#include <windows.h>

#include <cwchar>

namespace aero {
namespace {

std::wstring ReadKey(const std::wstring& file, const wchar_t* key) {
    wchar_t buf[512]{};
    GetPrivateProfileStringW(L"Profile", key, L"", buf, static_cast<DWORD>(std::size(buf)), file.c_str());
    return buf;
}

bool ParseU32(const std::wstring& text, std::uint32_t& out) {
    if (text.empty()) return false;
    wchar_t* end = nullptr;
    const unsigned long long v = std::wcstoull(text.c_str(), &end, 0);  // base 0: accepts 0x...
    if (end == text.c_str() || *end != L'\0' || v > 0xFFFFFFFFull) return false;
    out = static_cast<std::uint32_t>(v);
    return true;
}

}  // namespace

std::vector<GameProfile> LoadProfiles(const std::filesystem::path& dir, std::vector<std::wstring>& errors) {
    std::vector<GameProfile> result;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() != L".ini") continue;
        const std::wstring file = std::filesystem::absolute(it->path()).wstring();

        GameProfile p;
        p.file = it->path();
        p.name = ReadKey(file, L"Name");
        p.processName = ReadKey(file, L"Process");
        p.moduleDll = ReadKey(file, L"ModuleDll");
        p.note = ReadKey(file, L"BuildNote");
        p.hotkeys = ReadKey(file, L"Hotkeys");
        p.steamAppId = ReadKey(file, L"SteamAppId");
        if (ReadKey(file, L"Kind") == L"FabricMod") {
            p.kind = GameKind::FabricMod;
            p.modFile = ReadKey(file, L"ModFile");
            p.gameVersion = ReadKey(file, L"GameVersion");
            if (p.processName.empty()) p.processName = L"javaw.exe";
            if (!p.name.empty() && !p.modFile.empty() && !p.gameVersion.empty()) {
                result.push_back(std::move(p));
            } else {
                errors.push_back(it->path().filename().wstring() + L": a FabricMod profile needs Name, ModFile, GameVersion");
            }
            continue;
        }
        const bool ok = !p.name.empty() && !p.processName.empty() && !p.moduleDll.empty() &&
                        ParseU32(ReadKey(file, L"ExpectedTimeDateStamp"), p.timeDateStamp) &&
                        ParseU32(ReadKey(file, L"ExpectedSizeOfImage"), p.sizeOfImage);
        if (ok) {
            result.push_back(std::move(p));
        } else {
            errors.push_back(it->path().filename().wstring() +
                             L": needs Name, Process, ModuleDll, ExpectedTimeDateStamp, ExpectedSizeOfImage");
        }
    }
    return result;
}

}  // namespace aero
