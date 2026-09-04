#pragma once

#include <cstddef>
#include <cstdint>

// Hurts whoever is inside the aim FOV without drawing a weapon or playing an
// animation.
//
// The damage is delivered as an invisible, inaudible bullet fired between two
// points that both sit inside the victim, with our own ped named as its owner
// and WEAPON_UNARMED as the weapon: the server sees a fist landing on them from
// us. That indirection is the whole feature. Writing the victim's health, or
// calling APPLY_DAMAGE_TO_PED on their ped, only touches our own copy of them —
// remote health is owned by their client — so nothing would reach the server.
// A damage event does, because the runtime relays it to the victim, whose own
// client then reports the loss.
//
// Unarmed is the default for a reason beyond the missing animation: a roleplay
// server validates the weapon against what the attacker is holding, and fists
// are the one weapon everybody always has. Knife and the currently held weapon
// are offered because which of the three a given server honours is not
// something that can be known from here.
namespace Damager
{
    // Mirrors Menu::kAimHitboxCount. Kept as its own constant so the header
    // stays free of the menu.
    inline constexpr int kBoneCount = 16;

    enum class WeaponKind : int
    {
        Fist = 0,
        Knife,
        Current,
        Count
    };

    inline constexpr float kMinFov = 20.f;
    inline constexpr float kMaxFov = 600.f;
    inline constexpr float kMinDistance = 2.f;
    inline constexpr float kMaxDistance = 150.f;

    struct Settings final
    {
        bool enabled{};
        // 0 means armed for as long as the feature is on. Anything else has to
        // be held, which is what keeps a stray target from dying while the
        // player is walking past it.
        int holdKey{};
        float fov{140.f};
        float maxDistance{20.f};
        // Per hit, in the game's own scale. The server may well clamp it, which
        // is what the hit count below is for.
        int damage{30};
        // Hits per pass. One pass is one script round trip, so raising this is
        // how the kill gets fast; raising the rate instead only queues work.
        int hits{5};
        int intervalMs{120};
        bool visibleCheck{};
        // A body on the floor still answers the FOV test, so without this the
        // feature keeps hammering a corpse and keeps announcing itself.
        bool stopWhenDown{false};
        bool notify{};
        int weapon{static_cast<int>(WeaponKind::Fist)};
        // Head and chest, matching the aim defaults.
        bool bones[kBoneCount]{
            true, false, true, false};
    };

    struct Target final
    {
        bool valid{};
        std::uint32_t handle{};
        int remoteId{-1};
        // The bone being hit, and the point the bullet starts from: the bone
        // pushed a little towards us, so the segment it travels is entirely
        // inside the victim and no wall can be in the way.
        float aimX{};
        float aimY{};
        float aimZ{};
        float originX{};
        float originY{};
        float originZ{};
        float distance{};
    };

    struct Status final
    {
        bool armed{};
        bool hasTarget{};
        int remoteId{-1};
        float distance{};
        // The victim's health as their own client reports it, which is how a
        // hit that the server accepted is told from one it dropped. GTA5RP maps
        // its roleplay bar onto 100..200, so 100 is already down.
        int health{-1};
        int hits{};
        int passes{};
        char detail[96]{};
    };

    Settings& Access() noexcept;
    // Called from the aim pass, which is the one place that already has the
    // frame's peds, their bones and their screen positions.
    void PublishTarget(const Target& target) noexcept;
    void Update() noexcept;
    void Reset() noexcept;
    Status GetStatus() noexcept;
    bool Armed() noexcept;

    bool Export(
        char* output,
        std::size_t capacity,
        std::size_t& used) noexcept;
    void Import(const char* content) noexcept;
}
