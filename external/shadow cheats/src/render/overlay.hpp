#pragma once
#include <Windows.h>

namespace overlay
{
    inline int  width        = 0;
    inline int  height       = 0;
    inline bool menu_open    = true;

    void Render();
    void Shutdown();

    HWND FindGameWindow();
    bool IsGameWindowActive();
}
