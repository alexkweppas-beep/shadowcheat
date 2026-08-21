#pragma once

#include "../imgui/imgui.h"

namespace MinimapAnchor
{
    struct Rect final
    {
        float left{};
        float top{};
        float width{};
        float height{};

        ImVec2 Center() const noexcept
        {
            return ImVec2(left + width * 0.5f, top + height * 0.5f);
        }

        float Radius() const noexcept
        {
            const float r = (width < height ? width : height) * 0.5f;
            return r > 4.f ? r - 2.f : r;
        }
    };

    // Approximate GTA V minimap from frontend.xml (L/B) + safezone/aspect.
    // safeZone: 0.9..1.0 typical (1.0 = default). Pass 0 to use 1.0.
    Rect Compute(
        float displayW,
        float displayH,
        float safeZone = 1.f) noexcept;
}
