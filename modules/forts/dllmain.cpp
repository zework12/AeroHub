#include <windows.h>

#include <string>

#include "Features.hpp"
#include "Log.hpp"

namespace {

DWORD WINAPI InitThread(LPVOID) {
    forts::Logf("FortsModule loaded (Insert = menu, F6 = unlimited resources, End = all off)");
    std::string error;
    if (forts::InstallFeatures(error)) {
        forts::Logf("hook installed; features active");
    } else {
        forts::Logf("NOT activated: %s", error.c_str());
    }
    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        forts::g_self = module;
        DisableThreadLibraryCalls(module);
        if (HANDLE t = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
