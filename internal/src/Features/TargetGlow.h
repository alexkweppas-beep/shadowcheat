#pragma once

#include "../SDK/Math.h"

struct ImDrawList;

// Additive glow sprites, anchored in the world.
//
// ImGui draws everything with one blend state, straight alpha over the frame,
// and that is why stacking translucent circles to fake a glow never looks like
// one: the overlaps darken instead of brightening. This switches the device to
// additive for the span of the sprites and switches back afterwards, which is
// the same thing the effect this imitates does, and then overlapping sprites
// add up into light.
//
// Positions are world space and projected per sprite, so a ring around a target
// is a ring in the world: it foreshortens when seen edge-on, the far side is
// smaller than the near side, and the whole thing shrinks with distance without
// anything having to scale it by hand.
namespace TargetGlow
{
    // Opens an additive span on this draw list. Every Sprite call until End
    // lands in it. Returns false when the sprite texture is not available, in
    // which case nothing should be drawn.
    bool Begin(ImDrawList* draw) noexcept;
    void End(ImDrawList* draw) noexcept;

    // One sprite at a world position. radius is in metres and projected, so the
    // sprite keeps its physical size at any distance. Colour is straight RGB;
    // intensity scales it, and above one it blows out towards white, which is
    // what additive light does.
    void Sprite(
        ImDrawList* draw,
        const Vector3& world,
        float radius,
        const float color[3],
        float intensity) noexcept;
}
