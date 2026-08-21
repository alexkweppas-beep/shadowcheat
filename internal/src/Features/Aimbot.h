#pragma once
#include "../SDK/Math.h"

#include <cstdint>

class CPed;

namespace Features {
    extern bool g_AimbotEnabled;   // toggled by hooks SEH guard

    struct AimTargetSnapshot final
    {
        const CPed* ped{};
        Vector3 world{};
        Vector3 velocity{}; // m/s, smoothed — ballistic lead applied in SilentAim hook
        Vector2 screen{};
        std::uint32_t rageHandle{};
        std::uint64_t updatedAt{};
        int hitbox{};
        bool visible{};
        bool hitchanceEnabled{};
        float hitchance{100.0f};
        bool valid{};
    };

    struct AimDiagnostics final
    {
        int activePeds{};
        int evaluatedPeds{};
        int boneRejects{};
        int projectionRejects{};
        int visibilityRejects{};
        int fovRejects{};
        bool vectorEnabled{};
        bool vectorKeyHeld{};
        bool vectorTargetValid{};
        bool vectorWritten{};
        bool silentKeyHeld{};
        bool silentTargetValid{};
        bool triggerEnabled{};
        bool triggerKeyHeld{};
        bool triggerTargetValid{};
        bool triggerFired{};
    };

    void RunAimbot();
    bool CopySilentTarget(AimTargetSnapshot& output) noexcept;
    bool CopyDisplayTarget(AimTargetSnapshot& output) noexcept;
    AimDiagnostics GetAimDiagnostics() noexcept;
}
