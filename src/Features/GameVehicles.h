#pragma once

#include "../SDK/Game.h"

// Reads the game's own list of vehicles, every frame.
//
// Vehicle ESP has been drawing from a script snapshot that lands six or seven
// times a second, and no amount of interpolation makes six samples a second
// look like motion: the boxes either trail the car or step. The game keeps an
// array of its vehicles with a live count, and reading that costs a pointer
// walk per vehicle, so the positions can simply be correct on every frame.
//
// The script snapshot is still needed for what only the server knows, such as
// the remote id and whether a trunk is open. This supplies the part that
// belongs to the game: where the vehicle is and which way it faces.
namespace GameVehicles
{
    struct Live final
    {
        // Identity for the caller to hold on to. Matching by position alone
        // only works while the car is slow: the snapshot it is matched against
        // can be a third of a second old, and at speed that is further than
        // any sane search radius.
        const void* object{};
        Vector3 position{};
        float heading{};
    };

    // False when the addresses did not check out. Callers fall back to the
    // snapshot rather than drawing nothing: an offset that has gone stale must
    // degrade the picture, not remove it.
    bool Available() noexcept;

    int Copy(Live* output, int capacity) noexcept;
}
