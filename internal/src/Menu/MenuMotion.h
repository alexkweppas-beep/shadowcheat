#pragma once

// One motion system for the whole menu.
//
// The old code mixed gui->fixed_speed(), which divides by the framerate, with
// hand-rolled delta-time easings, so the same animation ran at different
// speeds on 60 and 144 fps. Everything here is delta-time driven, clamped
// against long frames (alt-tab), and reduced motion snaps decorative moves.
namespace MenuMotion
{
    // Seconds. Press is the shortest so clicks feel immediate.
    inline constexpr float kPress = 0.09f;
    inline constexpr float kHover = 0.14f;
    inline constexpr float kSelection = 0.18f;
    inline constexpr float kPage = 0.20f;
    inline constexpr float kReveal = 0.22f;
    inline constexpr float kRailPin = 0.24f;

    bool ReducedMotion() noexcept;
    void SetReducedMotion(bool enabled) noexcept;

    // Frame delta clamped so a stalled frame cannot teleport animations.
    float Delta() noexcept;

    // Exponential convergence that settles within `duration`.
    float Approach(
        float current,
        float target,
        float duration) noexcept;

    // 0..1 progress advanced by one frame.
    float Advance(
        float progress,
        bool forward,
        float duration) noexcept;

    // Standard ease for movement, decelerate for entrances.
    float EaseStandard(float t) noexcept;
    float EaseOut(float t) noexcept;
}
