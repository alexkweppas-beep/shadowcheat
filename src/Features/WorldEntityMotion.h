#pragma once

#include <cstdint>

namespace RageWorldEntities
{
    struct Entry;
}

// The vehicle/object snapshot is a V8 roundtrip, so it lands a handful of times
// a second while the ESP draws every frame. Left alone that reads as the box
// stepping along behind a moving car. This carries each entity's pose forward
// between snapshots from its own measured velocity and eases the render pose
// toward it, which removes both the stepping and most of the trailing.
namespace WorldEntityMotion
{
    // Rewrites position and heading in place. Safe to call every frame with the
    // same snapshot: a repeated generation extrapolates rather than resampling.
    void Smooth(
        RageWorldEntities::Entry* entries,
        int count,
        std::uint64_t generation,
        std::uint64_t capturedAtMs,
        float deltaSeconds) noexcept;

    // Drop all tracks. Handles are only unique within a session.
    void Reset() noexcept;
}
