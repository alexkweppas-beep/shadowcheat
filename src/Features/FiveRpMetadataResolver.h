#pragma once

#include <cstdint>

namespace FiveRpMetadataResolver
{
    enum class State : std::uint8_t
    {
        Unknown = 0,
        Confirmed,
        LostPending,
        NotFiveRp
    };

    struct StatusSnapshot final
    {
        State state{State::Unknown};
        bool detected{};
        bool pending{};
        std::uint32_t positiveStreak{};
        std::uint32_t negativeStreak{};
        std::uint64_t generation{};
        std::uint64_t lastSnapshotAt{};
        std::uint64_t lastPositiveAt{};
        std::uint64_t lastNegativeAt{};
        std::uint64_t enqueueFailures{};
        int metadataCount{};
        int familyCacheCount{};
        int familyPendingCount{};
        bool helperVisible{};
        bool apiVisible{};
        bool familyCanary{};
        bool familyRpcDisabled{};
        char message[128]{};
    };

    struct Metadata final
    {
        std::uint16_t remoteId{};
        std::uint32_t handle{};
        int characterId{};
        int familyId{};
        int factionId{};
        int factionType{};
        bool masked{};
        bool displayAdmin{};
        bool nametagDisabled{};
        char name[64]{};
        char familyName[64]{};
        char factionKey[48]{};
        char factionTag[32]{};
    };

    void Update(bool active) noexcept;
    void Reset() noexcept;
    bool TryGet(
        std::uint16_t remoteId,
        std::uint32_t handle,
        Metadata& output) noexcept;
    bool IsDetected() noexcept;
    StatusSnapshot GetStatus() noexcept;
    const char* StateName(State state) noexcept;
    int Count() noexcept;
}
