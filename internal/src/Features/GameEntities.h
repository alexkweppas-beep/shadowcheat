#pragma once

#include <cstdint>

#include "../SDK/Game.h"

// Reads the game's own lists of vehicles and objects, every frame.
//
// The alternative is a script snapshot that lands a few times a second, and no
// amount of interpolation makes a few samples a second look like motion: boxes
// either trail the thing they belong to or step after it. The game keeps these
// entities in per-kind lists hanging off CReplayInterface, so their positions
// can simply be correct on every frame.
//
// The script snapshot is still needed for what only the server knows, such as
// the remote id and whether a trunk is open. This supplies the part that
// belongs to the game: where a thing is and which way it faces.
//
// This replaces a reader that used one flat global array of vehicles. That
// array holds only a working subset - roughly half the vehicles around - so it
// had to be paired with a walk of the surrounding heap block and a memory of
// everything seen before, and even then it missed some. The interface here
// states the whole set outright, so none of that is needed.
namespace GameEntities
{
    enum class Kind : std::uint8_t
    {
        Vehicle,
        Object,
    };

    inline constexpr unsigned kVehicles = 1u;
    inline constexpr unsigned kObjects = 2u;

    struct Live final
    {
        // Identity for the caller to hold on to. Matching by position alone
        // only works while the thing is slow: the snapshot it is matched
        // against can be a third of a second old, and at speed that is further
        // than any sane search radius.
        const void* object{};
        Kind kind{Kind::Vehicle};
        Vector3 position{};
        float heading{};
    };

    // False when the addresses did not check out. Callers fall back to the
    // snapshot rather than drawing nothing: an offset that has gone stale must
    // degrade the picture, not remove it.
    bool Available() noexcept;

    // kinds is a mask of kVehicles / kObjects.
    int Copy(Live* output, int capacity, unsigned kinds) noexcept;
}
