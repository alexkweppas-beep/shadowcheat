#pragma once

#include <cstdint>

namespace RageWorldEntities
{
    inline constexpr int kMaxEntries = 512;
    inline constexpr std::uint32_t kInvalidRemoteId = 0xFFFFFFFFu;

    enum class EntityKind : std::uint8_t
    {
        Vehicle = 0,
        Object,
        Pickup
    };

    enum class VehicleClass : std::uint8_t
    {
        Car = 0,
        Bike,
        Boat,
        Aircraft,
        Other
    };

    struct Entry final
    {
        bool valid{};
        EntityKind kind{EntityKind::Vehicle};
        std::uint32_t handle{};
        std::uint32_t remoteId{kInvalidRemoteId};
        std::uint32_t modelHash{};
        float x{};
        float y{};
        float z{};
        float heading{};
        float health{};
        float bodyHealth{};
        bool occupied{};
        VehicleClass vehicleClass{VehicleClass::Other};
        float halfExtentX{};
        float halfExtentY{};
        float halfExtentZ{};
    };

    struct Snapshot final
    {
        std::uint64_t generation{};
        std::uint64_t capturedAtMs{};
        int count{};
        Entry entries[kMaxEntries]{};
    };

    // Call from the regular feature tick. Runtime work is throttled internally.
    void Update() noexcept;
    void Clear() noexcept;

    // Copies into caller-owned fixed storage; no allocation is performed.
    int CopySnapshot(
        Entry* output,
        int capacity,
        std::uint64_t* generation = nullptr,
        std::uint64_t* capturedAtMs = nullptr) noexcept;

    // Copies a coherent snapshot, including metadata, under the module lock.
    int GetSnapshot(Snapshot& output) noexcept;
    int Count() noexcept;
}
