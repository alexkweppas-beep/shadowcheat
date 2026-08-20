#include <windows.h>

#include <shadow/dev.hpp>

namespace {

void open_dev_console() {
    if constexpr (shadow::kDevBuild) {
        AllocConsole();
        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
    }
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        open_dev_console();
        shadow::log("internal attached");
    }

    return TRUE;
}
