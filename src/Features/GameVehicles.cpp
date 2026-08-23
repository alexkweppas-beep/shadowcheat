#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cmath>
#include <cstdint>

#include "GameVehicles.h"

#include "../SDK/Offsets.h"
#include "../SDK/RageBuildFlavor.h"

#if defined(RAGEMP_DIAGNOSTICS)
#include <cstdio>

#include "../Main/Hooks.h"
#endif

namespace
{
    // Measured on a live client, not carried over from another build.
    constexpr std::uintptr_t kTypeAt = 0x28;      // 3 for a vehicle
    constexpr std::uintptr_t kTransformAt = 0x30; // -> matrix block
    constexpr std::uintptr_t kMatrixPositionAt = 0x50;
    constexpr std::uintptr_t kMatrixForwardAt = 0x30;
    constexpr std::uint8_t kVehicleType = 3;

    // The list has room for 150 on this build. The bound is generous rather
    // than exact so a patch that resizes it does not silently disable the
    // feature, while a wild count still cannot walk us off into nothing.
    constexpr std::uint32_t kMaxPlausibleCount = 1024;

    // Not IsValidPtr: that one demands sixteen byte alignment because it is
    // written for heap objects, and the count here is a dword global sitting
    // eight bytes into a pair. It rejected a perfectly readable address and
    // the whole list silently came back empty.
    bool Addressable(const void* at) noexcept
    {
        const auto value = reinterpret_cast<std::uintptr_t>(at);
        return value >= 0x10000 && value < 0x00007FFFFFFEFFFFull;
    }

    // Objects sit one after another at this spacing, measured off their own
    // addresses: 56 of 69 neighbouring pairs were exactly this far apart.
    constexpr std::uintptr_t kVehicleStride = 0x1AA0;

    // Walking the pool costs one type byte per slot, so the whole thing is
    // affordable per frame, but not without a bound.
    constexpr int kMaxPoolSlots = 4096;

    std::uintptr_t g_poolStart = 0;
    std::uintptr_t g_poolEnd = 0;

    // Vehicles seen before, kept between frames.
    //
    // The global list holds a rotating subset and the surrounding memory turned
    // out to be a general arena rather than a tidy pool, so neither source sees
    // everything at once. What they do see is different vehicles at different
    // moments, and a vehicle stays at its address for as long as it exists, so
    // remembering them accumulates the full set. Each is re-checked every frame
    // and dropped the moment it stops answering as a vehicle.
    constexpr int kKnownCapacity = 256;
    std::uintptr_t g_known[kKnownCapacity]{};
    int g_knownCount = 0;

    void Remember(std::uintptr_t vehicle) noexcept
    {
        for (int index = 0; index < g_knownCount; ++index)
        {
            if (g_known[index] == vehicle)
                return;
        }
        if (g_knownCount < kKnownCapacity)
            g_known[g_knownCount++] = vehicle;
    }

    bool ReadPointer(std::uintptr_t at, std::uintptr_t& out) noexcept
    {
        out = 0;
        if (!Addressable(reinterpret_cast<const void*>(at)))
            return false;
        __try
        {
            out = *reinterpret_cast<const std::uintptr_t*>(at);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return out != 0;
    }
}

namespace
{
    bool IsVehicleObject(std::uintptr_t candidate) noexcept
    {
        if (!IsValidPtr(reinterpret_cast<const void*>(candidate)))
            return false;
        std::uint8_t type = 0;
        __try
        {
            type = *reinterpret_cast<const std::uint8_t*>(
                candidate + kTypeAt);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return type == kVehicleType;
    }

    // The pool is one allocation, and the allocation's extent is something the
    // operating system will simply state. That is worth more than any offset:
    // the global list nearby holds only part of the vehicles, but every one of
    // them lives in this block, so knowing where the block begins and ends is
    // enough to enumerate all of them.
    void LearnPoolFrom(std::uintptr_t vehicle) noexcept
    {
        if (vehicle >= g_poolStart && vehicle < g_poolEnd)
            return;

        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(
                reinterpret_cast<const void*>(vehicle),
                &info,
                sizeof(info)) != sizeof(info))
            return;
        if (info.State != MEM_COMMIT)
            return;

        const auto regionStart =
            reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        const std::uintptr_t regionEnd = regionStart + info.RegionSize;
        // Aligned to this vehicle's own phase within the block rather than to
        // the block itself: the pool need not start at the page boundary.
        const std::uintptr_t offset = vehicle - regionStart;
        g_poolStart = vehicle - (offset / kVehicleStride) * kVehicleStride;
        g_poolEnd = regionEnd;
    }
}

namespace GameVehicles
{
    bool Available() noexcept
    {
        if (!Offsets::VehicleListPtr || !Offsets::VehicleListCapacityPtr)
            return false;
        if (!Addressable(Offsets::VehicleListPtr) ||
            !Addressable(Offsets::VehicleListCapacityPtr))
            return false;

        std::uintptr_t list = 0;
        if (!ReadPointer(
                reinterpret_cast<std::uintptr_t>(Offsets::VehicleListPtr),
                list))
            return false;
        if (!Addressable(reinterpret_cast<const void*>(list)))
            return false;

        std::uint32_t capacityInList = 0;
        __try
        {
            capacityInList = *Offsets::VehicleListCapacityPtr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return capacityInList > 0 && capacityInList <= kMaxPlausibleCount;
    }

    int Copy(Live* output, int capacity) noexcept
    {
        if (!output || capacity <= 0)
            return 0;

        std::uintptr_t list = 0;
        if (!Offsets::VehicleListPtr ||
            !Addressable(Offsets::VehicleListPtr) ||
            !ReadPointer(
                reinterpret_cast<std::uintptr_t>(Offsets::VehicleListPtr),
                list))
            return 0;
        if (!Addressable(reinterpret_cast<const void*>(list)))
            return 0;

        std::uint32_t capacityInList = 0;
        if (!Offsets::VehicleListCapacityPtr ||
            !Addressable(Offsets::VehicleListCapacityPtr))
            return 0;
        __try
        {
            capacityInList = *Offsets::VehicleListCapacityPtr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
        if (capacityInList == 0 || capacityInList > kMaxPlausibleCount)
            return 0;

#if defined(RAGEMP_DIAGNOSTICS)
        int rejectedNull = 0;
        int rejectedPointer = 0;
        int rejectedType = 0;
        int rejectedMatrix = 0;
#endif
        int written = 0;
        for (std::uint32_t index = 0;
             index < capacityInList && written < capacity;
             ++index)
        {
            std::uintptr_t vehicle = 0;
            if (!ReadPointer(list + index * sizeof(std::uintptr_t), vehicle))
            {
#if defined(RAGEMP_DIAGNOSTICS)
                ++rejectedNull;
#endif
                continue;
            }
            if (!IsValidPtr(reinterpret_cast<const void*>(vehicle)))
            {
#if defined(RAGEMP_DIAGNOSTICS)
                ++rejectedPointer;
#endif
                continue;
            }

            // The type byte is the cheap guard against reading a slot that has
            // been recycled into something else between the count and here.
            std::uint8_t type = 0;
            __try
            {
                type = *reinterpret_cast<const std::uint8_t*>(
                    vehicle + kTypeAt);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                continue;
            }
            if (type != kVehicleType)
            {
#if defined(RAGEMP_DIAGNOSTICS)
                ++rejectedType;
#endif
                continue;
            }

            std::uintptr_t matrix = 0;
            if (!ReadPointer(vehicle + kTransformAt, matrix))
                continue;
            if (!IsValidPtr(reinterpret_cast<const void*>(matrix)))
            {
#if defined(RAGEMP_DIAGNOSTICS)
                ++rejectedMatrix;
#endif
                continue;
            }

            Vector3 position{};
            Vector3 forward{};
            __try
            {
                position = *reinterpret_cast<const Vector3*>(
                    matrix + kMatrixPositionAt);
                forward = *reinterpret_cast<const Vector3*>(
                    matrix + kMatrixForwardAt);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                continue;
            }
            if (!std::isfinite(position.x) ||
                !std::isfinite(position.y) ||
                !std::isfinite(position.z))
                continue;

            Live& live = output[written++];
            live.object = reinterpret_cast<const void*>(vehicle);
            live.position = position;
            // Heading the way the game states it: degrees clockwise from north,
            // taken from the forward row of the matrix.
            float heading = std::atan2(-forward.x, forward.y) *
                (180.f / 3.14159265f);
            if (!std::isfinite(heading))
                heading = 0.f;
            if (heading < 0.f)
                heading += 360.f;
            live.heading = heading;
            Remember(vehicle);
            LearnPoolFrom(vehicle);
        }

        // Everything else in the pool. The global list turned out to hold only
        // a working subset, twenty-one of the sixty-odd vehicles around, and
        // the rest were falling back to the script snapshot and its stepping.
        // They are all neighbours in one block, so once any of them is known
        // the others are a walk away.
        int fromPool = 0;
        if (g_poolStart != 0 && g_poolEnd > g_poolStart)
        {
            int slots = 0;
            for (std::uintptr_t at = g_poolStart;
                 at + kVehicleStride <= g_poolEnd &&
                 written < capacity &&
                 slots < kMaxPoolSlots;
                 at += kVehicleStride, ++slots)
            {
                if (!IsVehicleObject(at))
                    continue;

                bool already = false;
                for (int index = 0; index < written; ++index)
                {
                    if (output[index].object ==
                        reinterpret_cast<const void*>(at))
                    {
                        already = true;
                        break;
                    }
                }
                if (already)
                    continue;

                std::uintptr_t matrix = 0;
                if (!ReadPointer(at + kTransformAt, matrix))
                    continue;
                if (!IsValidPtr(reinterpret_cast<const void*>(matrix)))
                    continue;

                Vector3 position{};
                Vector3 forward{};
                __try
                {
                    position = *reinterpret_cast<const Vector3*>(
                        matrix + kMatrixPositionAt);
                    forward = *reinterpret_cast<const Vector3*>(
                        matrix + kMatrixForwardAt);
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    continue;
                }
                if (!std::isfinite(position.x) ||
                    !std::isfinite(position.y) ||
                    !std::isfinite(position.z))
                    continue;

                Live& live = output[written++];
                live.object = reinterpret_cast<const void*>(at);
                live.position = position;
                float heading = std::atan2(-forward.x, forward.y) *
                    (180.f / 3.14159265f);
                if (!std::isfinite(heading))
                    heading = 0.f;
                if (heading < 0.f)
                    heading += 360.f;
                live.heading = heading;
                Remember(at);
                ++fromPool;
            }
        }

        // Everything remembered from earlier frames that is still a vehicle.
        int fromMemory = 0;
        int survivors = 0;
        for (int index = 0; index < g_knownCount; ++index)
        {
            const std::uintptr_t vehicle = g_known[index];
            if (!IsVehicleObject(vehicle))
                continue;
            g_known[survivors++] = vehicle;

            bool already = false;
            for (int other = 0; other < written; ++other)
            {
                if (output[other].object ==
                    reinterpret_cast<const void*>(vehicle))
                {
                    already = true;
                    break;
                }
            }
            if (already || written >= capacity)
                continue;

            std::uintptr_t matrix = 0;
            if (!ReadPointer(vehicle + kTransformAt, matrix))
                continue;
            if (!IsValidPtr(reinterpret_cast<const void*>(matrix)))
                continue;

            Vector3 position{};
            Vector3 forward{};
            __try
            {
                position = *reinterpret_cast<const Vector3*>(
                    matrix + kMatrixPositionAt);
                forward = *reinterpret_cast<const Vector3*>(
                    matrix + kMatrixForwardAt);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                continue;
            }
            if (!std::isfinite(position.x) ||
                !std::isfinite(position.y) ||
                !std::isfinite(position.z))
                continue;

            Live& live = output[written++];
            live.object = reinterpret_cast<const void*>(vehicle);
            live.position = position;
            float heading = std::atan2(-forward.x, forward.y) *
                (180.f / 3.14159265f);
            if (!std::isfinite(heading))
                heading = 0.f;
            if (heading < 0.f)
                heading += 360.f;
            live.heading = heading;
            ++fromMemory;
        }
        g_knownCount = survivors;
#if defined(RAGEMP_DIAGNOSTICS)
        {
            static ULONGLONG lastReport = 0;
            const ULONGLONG now = GetTickCount64();
            if (now - lastReport >= 3000)
            {
                lastReport = now;
                char line[176]{};
                _snprintf_s(
                    line,
                    sizeof(line),
                    _TRUNCATE,
                    "GameVehicles: total=%d (pool +%d, remembered +%d), "
                    "known=%d",
                    written,
                    fromPool,
                    fromMemory,
                    g_knownCount);
                hooks::HLog(line);
            }
        }
#endif
        return written;
    }
}
