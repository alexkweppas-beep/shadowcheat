#pragma once

#include <cstdint>

namespace RagePlayerNames
{
    enum class EntityKind : std::uint8_t
    {
        Player = 0,
        Npc = 1,
        Animal = 2
    };

    enum class BoneSlot : std::uint8_t
    {
        Head = 0,
        Neck,
        Spine,
        Pelvis,
        LeftUpperArm,
        LeftForearm,
        LeftHand,
        RightUpperArm,
        RightForearm,
        RightHand,
        LeftThigh,
        LeftCalf,
        LeftFoot,
        RightThigh,
        RightCalf,
        RightFoot,
        Count
    };

    struct BonePosition final
    {
        float x{};
        float y{};
        float z{};
    };

    struct SnapshotEntry final
    {
        EntityKind kind{EntityKind::Player};
        bool local{};
        std::uint16_t remoteId{};
        std::uint32_t handle{};
        float x{};
        float y{};
        float z{};
        float heading{};
        int adminLevel{};
        bool visible{};
        int factionId{};
        int factionType{};
        int familyId{};
        int sqlId{};
        bool masked{};
        bool displayAdmin{};
        bool nametagDisabled{};
        bool dead{};
        bool knock{};
        bool inGreenZone{};
        // Roleplay hit points, 0 to 100, or -1 when unreadable. hpSync is the
        // value RAGE syncs and hpRaw the untouched native read, kept apart
        // because the native baseline differs between local and remote peds.
        // dyingFlag mirrors IS_PED_DEAD_OR_DYING, -1 when the native is absent.
        float hp{-1.f};
        float hpSync{-1.f};
        float hpRaw{-1.f};
        int dyingFlag{-1};
        std::uint32_t boneMask{};
        BonePosition bones[
            static_cast<int>(BoneSlot::Count)]{};
        char name[48]{};
        char factionKey[48]{};
        char factionTag[24]{};
        char familyName[48]{};
        char nickname[48]{};
    };

    struct IdentityStatus final
    {
        bool helperReady{};
        bool familyCanary{};
        bool familyRpcDisabled{};
        int canonicalNames{};
        int fallbackNames{};
        int stringFactions{};
        int familyCacheCount{};
        int familyPendingCount{};
        // Replies to family lookups, and how many of those carried no name.
        // Separated because a server that never answers and one that refuses
        // look identical from the cache count alone.
        int familyRepliesSeen{};
        int familyRepliesEmpty{};
    };

    void Update() noexcept;
    void ObserveIdentity(
        std::uint16_t remoteId,
        const char* name,
        const char* factionTag) noexcept;
    void ObserveFamily(
        int familyId,
        const char* name) noexcept;
    bool Get(
        std::uint16_t remoteId,
        char* output,
        int outputSize) noexcept;
    bool FindNearest(
        float x,
        float y,
        float z,
        float maxDistance,
        char* output,
        int outputSize) noexcept;
    int CopySnapshot(
        SnapshotEntry* output,
        int capacity,
        std::uint64_t* generation = nullptr) noexcept;

    // Copying 256 fat entries just to learn whether anything changed is the
    // single most wasteful thing the frame used to do. Callers that only need
    // the change marker, or the local player's affiliation, use these instead.
    std::uint64_t SnapshotGeneration() noexcept;

    struct LocalIdentity final
    {
        bool valid{};
        int factionId{};
        int familyId{};
        char factionKey[48]{};
        char familyName[48]{};
    };

    LocalIdentity GetLocalIdentity() noexcept;

    bool GetByHandle(
        std::uint32_t handle,
        SnapshotEntry& entry) noexcept;
    IdentityStatus GetIdentityStatus() noexcept;
    int Count() noexcept;
}
