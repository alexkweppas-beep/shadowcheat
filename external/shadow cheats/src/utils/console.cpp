#include "security/xorstr.hpp"
#include "console.hpp"
#include <Windows.h>
#include <cstdio>

namespace utils
{
    void AllocConsoleAndRedirect()
    {
        AllocConsole();
        FILE* f = nullptr;
        freopen_s(&f, xorstr_("CONOUT$"), xorstr_("w"), stdout);
        freopen_s(&f, xorstr_("CONOUT$"), xorstr_("w"), stderr);
        freopen_s(&f, xorstr_("CONIN$"),  xorstr_("r"), stdin);
        SetConsoleTitleA(xorstr_("Spotify"));
    }

    void FreeConsoleSafe()
    {
        FreeConsole();
    }
}
