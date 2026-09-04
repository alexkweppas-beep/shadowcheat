#pragma once
struct ImDrawList;

namespace Features {
    extern bool g_EspEnabled;     // toggled by hooks SEH guard
    void RenderESP(ImDrawList* drawList);
}
