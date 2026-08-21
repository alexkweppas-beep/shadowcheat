#include "MinimapAnchor.h"

#include <cmath>

namespace MinimapAnchor
{
    Rect Compute(
        float displayW,
        float displayH,
        float safeZone) noexcept
    {
        Rect out{};
        if (displayW < 64.f || displayH < 64.f)
            return out;

        float aspect = displayW / displayH;
        if (aspect > 2.f)
            aspect = 16.f / 9.f;
        if (safeZone < 0.5f || safeZone > 1.f)
            safeZone = 1.f;

        // frontend.xml: minimap alignX=L alignY=B
        // posX=-0.0045 posY=0.002 sizeX=0.150 sizeY=0.188888
        // Safezone inset similar to FiveM CalculateMinimap.
        const float safeOffset =
            displayW * (1.f / 20.f) * (std::fabs(safeZone - 1.f) * 10.f);

        float width = displayW / (4.f * aspect);
        float height = displayH / 5.674f;
        float left = safeOffset;
        float bottom = displayH - safeOffset;

        if (aspect > 2.f)
        {
            left += width * 0.845f;
            width *= 0.76f;
        }
        else if (aspect > 1.8f)
        {
            left += width * 0.2225f;
            width *= 0.995f;
        }

        // Match default size ratios from frontend.xml (~0.15 x 0.188888 of screen).
        const float xmlW = displayW * 0.150f;
        const float xmlH = displayH * 0.188888f;
        width = (width + xmlW) * 0.5f;
        height = (height + xmlH) * 0.5f;

        out.width = width;
        out.height = height;
        out.left = left + displayW * (-0.0045f);
        out.top = bottom - height - displayH * 0.002f;
        if (out.left < 0.f)
            out.left = 0.f;
        if (out.top < 0.f)
            out.top = 0.f;
        return out;
    }
}
