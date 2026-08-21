#pragma once

#include "../SDK/RageBuildFlavor.h"

#include <cstddef>
#include <cstdint>

namespace FiveRpFarmTracer
{
    inline constexpr std::size_t kTimelineCapacity = 192;
    inline constexpr std::size_t kNameCapacity = 96;
    inline constexpr std::size_t kPayloadCapacity = 256;

    enum class State : std::uint8_t
    {
        Idle = 0,
        Job,
        Bucket,
        Interact,
        Minigame,
        Filled,
        Deliver,
        Count
    };

    enum class EventKind : std::uint8_t
    {
        Unknown = 0,
        FarmStart,
        FarmStop,
        WorkerStatus,
        BucketRequest,
        BucketStatus,
        CowInteract,
        CowAnimationStart,
        MilkGameEnd,
        CowAnimationEnd,
        DeliveryRequest,
        DeliveryResult,
        CowSync,
        ZoneEnter,
        ZoneLeave,
        LegacyBucketToggle,
        LegacyMilkStart,
        LegacyMilkEnd,
        LegacyMilkTake,
        V8Snapshot
    };

    enum class SnapshotSource : std::uint8_t
    {
        EventOnly = 0,
        V8
    };

    enum TimelineFlags : std::uint8_t
    {
        TimelineNone = 0,
        TimelineStateChanged = 1 << 0,
        TimelinePayloadParsed = 1 << 1,
        TimelineOutOfOrder = 1 << 2,
        TimelineLegacy = 1 << 3,
        TimelineFromV8 = 1 << 4
    };

    struct Counters final
    {
        std::uint64_t observed{};
        std::uint64_t recognized{};
        std::uint64_t outOfOrder{};
        std::uint64_t transitions{};
        std::uint64_t droppedTimeline{};
        std::uint64_t generationResets{};
        std::uint64_t jobStarts{};
        std::uint64_t jobStops{};
        std::uint64_t bucketChanges{};
        std::uint64_t cowInteractions{};
        std::uint64_t minigamesStarted{};
        std::uint64_t milkAttempts{};
        std::uint64_t milkSuccesses{};
        std::uint64_t milkFailures{};
        std::uint64_t bucketFills{};
        std::uint64_t deliveriesStarted{};
        std::uint64_t deliveriesCompleted{};
        std::uint64_t deliveriesSucceeded{};
        std::uint64_t deliveriesFailed{};
        std::uint64_t cowSyncs{};
        std::uint64_t v8Requests{};
        std::uint64_t v8Snapshots{};
        std::uint64_t v8Unavailable{};
        std::uint64_t v8Failures{};
    };

    struct Timings final
    {
        std::uint64_t stateSince{};
        std::uint64_t jobStarted{};
        std::uint64_t interactStarted{};
        std::uint64_t minigameStarted{};
        std::uint64_t deliveryStarted{};
        std::uint64_t lastEvent{};
        std::uint64_t lastJobMs{};
        std::uint64_t totalJobMs{};
        std::uint64_t lastInteractMs{};
        std::uint64_t lastMinigameMs{};
        std::uint64_t lastDeliveryMs{};
        std::uint64_t stateTotalMs[
            static_cast<std::size_t>(State::Count)]{};
    };

    struct TraceSnapshot final
    {
        std::uint64_t generation{};
        std::uint64_t sequence{};
        State state{State::Idle};
        SnapshotSource source{SnapshotSource::EventOnly};
        bool isOnJob{};
        bool hasBucket{};
        bool isBucketFilled{};
        int cowIndex{-1};
        int nearestCowIndex{-1};
        float nearestCowDistance{-1.0f};
        bool v8Accessible{};
        std::uint64_t v8CapturedAt{};
        std::uint64_t v8RuntimeGeneration{};
        std::size_t timelineCount{};
        Counters counters{};
        Timings timings{};
    };

    struct TimelineEntry final
    {
        std::uint64_t sequence{};
        std::uint64_t timestamp{};
        std::uint64_t generation{};
        EventKind kind{EventKind::Unknown};
        State before{State::Idle};
        State after{State::Idle};
        int cowIndex{-1};
        std::int8_t success{-1};
        std::uint8_t flags{};
        char direction[20]{};
        char plainName[kNameCapacity]{};
        char wireName[kNameCapacity]{};
        char payload[kPayloadCapacity]{};
    };

    constexpr const char* StateName(State state) noexcept
    {
        switch (state)
        {
        case State::Idle: return "idle";
        case State::Job: return "job";
        case State::Bucket: return "bucket";
        case State::Interact: return "interact";
        case State::Minigame: return "minigame";
        case State::Filled: return "filled";
        case State::Deliver: return "deliver";
        default: return "unknown";
        }
    }

    constexpr const char* EventKindName(EventKind kind) noexcept
    {
        switch (kind)
        {
        case EventKind::FarmStart: return "farm_start";
        case EventKind::FarmStop: return "farm_stop";
        case EventKind::WorkerStatus: return "worker_status";
        case EventKind::BucketRequest: return "bucket_request";
        case EventKind::BucketStatus: return "bucket_status";
        case EventKind::CowInteract: return "cow_interact";
        case EventKind::CowAnimationStart: return "cow_animation_start";
        case EventKind::MilkGameEnd: return "milk_game_end";
        case EventKind::CowAnimationEnd: return "cow_animation_end";
        case EventKind::DeliveryRequest: return "delivery_request";
        case EventKind::DeliveryResult: return "delivery_result";
        case EventKind::CowSync: return "cow_sync";
        case EventKind::ZoneEnter: return "zone_enter";
        case EventKind::ZoneLeave: return "zone_leave";
        case EventKind::LegacyBucketToggle: return "legacy_bucket_toggle";
        case EventKind::LegacyMilkStart: return "legacy_milk_start";
        case EventKind::LegacyMilkEnd: return "legacy_milk_end";
        case EventKind::LegacyMilkTake: return "legacy_milk_take";
        case EventKind::V8Snapshot: return "v8_snapshot";
        default: return "unknown";
        }
    }

#if RAGEMP_DEV_BUILD
    // Returns true only for a farm record consumed by the state machine.
    bool Observe(
        const char* direction,
        const char* plainName,
        const char* wireName,
        const char* payload,
        std::uint64_t timestamp,
        std::uint64_t generation) noexcept;

    // Pumps the queued, read-only owner-thread V8 snapshot.
    void Update() noexcept;

    TraceSnapshot Snapshot() noexcept;

    // Copies the newest entries in chronological order.
    std::size_t CopyTimeline(
        TimelineEntry* output,
        std::size_t capacity,
        std::uint64_t* generation = nullptr) noexcept;

    // On failure output is empty and written receives required bytes,
    // excluding the trailing null.
    bool ExportJson(
        char* output,
        std::size_t capacity,
        std::size_t* written = nullptr) noexcept;

    void Reset(std::uint64_t generation) noexcept;

    bool RunTransitionSelfTest(
        char* error,
        std::size_t errorCapacity) noexcept;
#else
    inline bool Observe(
        const char*,
        const char*,
        const char*,
        const char*,
        std::uint64_t,
        std::uint64_t) noexcept
    {
        return false;
    }

    inline void Update() noexcept {}

    inline TraceSnapshot Snapshot() noexcept
    {
        return {};
    }

    inline std::size_t CopyTimeline(
        TimelineEntry*,
        std::size_t,
        std::uint64_t* = nullptr) noexcept
    {
        return 0;
    }

    inline bool ExportJson(
        char*,
        std::size_t,
        std::size_t* = nullptr) noexcept
    {
        return false;
    }

    inline void Reset(std::uint64_t) noexcept {}

    inline bool RunTransitionSelfTest(
        char*,
        std::size_t) noexcept
    {
        return true;
    }
#endif
}
