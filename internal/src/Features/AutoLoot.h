#pragma once

#include <cstddef>
#include <cstdint>

// Picks up 5RP ground items without walking onto each one.
//
// Ground items are objects the server tags with an "objectItem" variable; the
// client copies it to a JS property shaped "id$#$name$#$count" and clears the
// variable, so the payload only exists in script memory. The pickup itself is
// their own local bridge event, cr:call:id, carrying
// inv:takeWorldItemToSlot(-1, slot, itemId, count) — raising it hands the whole
// server protocol back to their code, which is why nothing here reimplements it.
//
// Range is not ours to choose. The server accepted a take at 7.1 m and refused
// the same item at 11.9, 17 and 27 m, so the radius is clamped to what it will
// actually honour; asking for more would only trade silence for chatter.
namespace AutoLoot
{
    inline constexpr int kMaxFilters = 96;
    // Room for the Cyrillic names, which run two bytes per character:
    // "Малокалиберная винтовка" alone is 45 bytes.
    inline constexpr int kFilterLength = 64;

    // The server was measured accepting a take at 7 m and refusing the same
    // item at 12, so anything past roughly ten is refused. The slider is not
    // capped there anyway: the limit is worth being able to probe, and a
    // refusal is answered with a growing backoff rather than a retry storm.
    inline constexpr float kMaxRadius = 10.f;
    inline constexpr float kMinRadius = 1.f;
    inline constexpr float kServerLimit = 10.f;

    struct Settings final
    {
        bool enabled{};
        // On takes whatever is lying there; off narrows it to the names in the
        // filter list.
        bool takeAll{true};
        bool notify{true};
        // Draws the reach as a ring on the ground. The number in the slider
        // means nothing until it is seen next to the street it covers.
        bool showRadius{true};
        float radius{8.f};
        // Gap between attempts. One pickup is one round trip to the server, so
        // this is deliberately not per-frame.
        int intervalMs{350};
    };

    struct Status final
    {
        // Items inside the radius that pass the filter, and every item the
        // client knows about anywhere. The pair is what tells a quiet street
        // apart from a filter that matches nothing.
        int reachable{};
        int knownItems{};
        int taken{};
        int refused{};
        bool slotsKnown{};
        int freeSlots{};
        char detail[96]{};
    };

    Settings& Access() noexcept;
    void Update() noexcept;
    void Reset() noexcept;
    Status GetStatus() noexcept;

    int FilterCount() noexcept;
    const char* FilterAt(int index) noexcept;
    bool AddFilter(const char* name) noexcept;
    void RemoveFilter(int index) noexcept;
    void SeedDefaultFilters() noexcept;

    bool Export(
        char* output,
        std::size_t capacity,
        std::size_t& used) noexcept;
    void Import(const char* content) noexcept;
}
