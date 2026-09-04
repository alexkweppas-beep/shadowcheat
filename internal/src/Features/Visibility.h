#pragma once

#include <cstdint>

#include "../SDK/Game.h"
#include "../SDK/Math.h"

// Line-of-sight from the rendered camera to a target's bones, evaluated on the
// Present thread with asynchronous shape tests.
//
// This replaces the RAGE snapshot's chest-to-chest flag, which traced from the
// local ped with peds included in the collision mask: any bystander between the
// two bodies reported the target as hidden, the trace started metres away from
// where the camera actually was in third person, and the answer only reached C++
// once per snapshot. Here the trace starts at the camera, excludes peds so a
// crowd cannot occlude anything, and each answer is keyed on the CPed pointer,
// so a snapshot mismatch can no longer attach someone else's visibility.
namespace Visibility
{
    // Sixteen bones collapse onto three zones. Probing every bone would cost
    // five times the shape tests for an answer nobody consumes at that
    // granularity: what callers need is whether the head is exposed above cover
    // while the body is not.
    enum Zone : std::uint8_t
    {
        ZoneHead = 1u << 0,
        ZoneChest = 1u << 1,
        ZonePelvis = 1u << 2,
        ZoneAll = ZoneHead | ZoneChest | ZonePelvis
    };

    inline constexpr int kZoneCount = 3;

    struct Diagnostics final
    {
        std::uint32_t tracked{};
        std::uint32_t started{};
        std::uint32_t resolved{};
        std::uint32_t dropped{};
        std::uint32_t candidates{};
        // Per frame: how many peds arrived with an answer differing from the
        // one on screen, and how many of those actually got through the switch
        // delay. rawFlips staying high while switches stays near zero is the
        // signature of a noisy source being damped rather than a world that is
        // genuinely changing.
        std::uint32_t rawFlips{};
        std::uint32_t switches{};
        // False until a probe has actually come back Ready. Present-thread
        // shape tests are expected to work, but if this build of the game
        // disagrees the failure has to be visible rather than silently
        // reporting everyone as unknown forever.
        bool everResolved{};
        // True while the RAGE snapshot flag is standing in because probes keep
        // being started and never answered.
        bool usingSnapshotFallback{};
    };

    // Which zone covers a bone slot, or 0 for slots no zone represents.
    std::uint8_t ZoneForBone(int bone) noexcept;

    // Called from hkPresent once bones are filled, before anything reads the
    // frame. Publishes the masks known so far onto the entries, then spends a
    // small per-frame budget of synchronous probes on whoever needs one most.
    // The screen centre only breaks ties in that budget. With demand false, the
    // existing answers keep ageing out but no probe runs: nothing consumes the
    // masks, and every probe blocks on the physics world.
    void Update(
        Game::FramePedContext& context,
        const Vector3& camera,
        const Vector2& center,
        bool demand) noexcept;

    void Reset() noexcept;
    const Diagnostics& GetDiagnostics() noexcept;
}
