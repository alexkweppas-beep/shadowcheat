#pragma once

#include "RagePlayerNames.h"
#include "../imgui/imgui.h"

namespace EspSkeleton
{
    struct BonePair final
    {
        RagePlayerNames::BoneSlot from{};
        RagePlayerNames::BoneSlot to{};
    };

    const BonePair* CanonicalPairs(
        int& count) noexcept;

    bool ValidateProjected(
        const ImVec2* points,
        const bool* valid,
        bool requireAll) noexcept;

    int DrawProjected(
        ImDrawList* draw,
        const ImVec2* points,
        const bool* valid,
        ImU32 color,
        float thickness) noexcept;
}
