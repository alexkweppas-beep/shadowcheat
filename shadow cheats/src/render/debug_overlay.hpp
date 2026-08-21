#pragma once
#include <atomic>

namespace features
{
    extern std::atomic<unsigned int> g_EntityLoopHz;

    void DrawDebugOverlay();
}
