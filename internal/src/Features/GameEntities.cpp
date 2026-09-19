#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cmath>
#include <cstdint>

#include "GameEntities.h"

#include "../SDK/Offsets.h"
#include "../SDK/RageBuildFlavor.h"

#if defined(RAGEMP_DIAGNOSTICS)
#include <cstdio>

#include "../Main/Hooks.h"
#endif

namespace
{
    // A wild count must not walk us off into nothing, but the bound is
    // generous rather than exact so a patch that resizes a list does not
    // silently disable the feature.
    constexpr std::uint32_t kMaxPlausibleCount = 8192;

    struct Source final
    {
        std::uint32_t interfaceAt;
        std::uint32_t listAt;
        std::uint32_t countAt;
        std::uint8_t type;
        GameEntities::Kind kind;
        unsigned mask;
    };

    constexpr Source kSources[] = {
        {
            Offsets::Replay::VehicleInterface,
            Offsets::Replay::VehicleList,
            Offsets::Replay::VehicleCount,
            Offsets::Entity::TypeVehicle,
            GameEntities::Kind::Vehicle,
            GameEntities::kVehicles,
        },
        {
            Offsets::Replay::ObjectInterface,
            Offsets::Replay::ObjectList,
            Offsets::Replay::ObjectCount,
            Offsets::Entity::TypeObject,
            GameEntities::Kind::Object,
            GameEntities::kObjects,
        },
    };

    // Not IsValidPtr: that one demands sixteen byte alignment because it is
    // written for heap objects, and a count here is a dword inside a structure.
    bool Addressable(std::uintptr_t value) noexcept
    {
        return value >= 0x10000 && value < 0x00007FFFFFFEFFFFull;
    }

    bool ReadPointer(std::uintptr_t at, std::uintptr_t& out) noexcept
    {
        out = 0;
        if (!Addressable(at))
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

    bool ReadCount(std::uintptr_t at, std::uint32_t& out) noexcept
    {
        out = 0;
        if (!Addressable(at))
            return false;
        __try
        {
            out = *reinterpret_cast<const std::uint32_t*>(at);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return out > 0 && out <= kMaxPlausibleCount;
    }

    std::uintptr_t ReplayInterface() noexcept
    {
        if (!Offsets::ReplayInterfacePtr ||
            !Addressable(reinterpret_cast<std::uintptr_t>(
                Offsets::ReplayInterfacePtr)))
            return 0;
        std::uintptr_t replay = 0;
        if (!ReadPointer(
                reinterpret_cast<std::uintptr_t>(
                    Offsets::ReplayInterfacePtr),
                replay))
            return 0;
        return IsValidPtr(reinterpret_cast<const void*>(replay))
            ? replay : 0;
    }

    // The type byte is the cheap guard against reading a slot that has been
    // recycled into something else since the count was taken.
    bool HasType(std::uintptr_t entity, std::uint8_t wanted) noexcept
    {
        std::uint8_t type = 0;
        __try
        {
            type = *reinterpret_cast<const std::uint8_t*>(
                entity + Offsets::Entity::Type);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return type == wanted;
    }
}

namespace GameEntities
{
    bool ReadPose(
        const void* entityPtr,
        Vector3& position,
        float& heading) noexcept
    {
        std::uintptr_t entity = reinterpret_cast<std::uintptr_t>(entityPtr);
        std::uintptr_t matrix = 0;
        if (!ReadPointer(entity + Offsets::Entity::Transform, matrix))
            return false;
        if (!IsValidPtr(reinterpret_cast<const void*>(matrix)))
            return false;

        Vector3 forward{};
        __try
        {
            position = *reinterpret_cast<const Vector3*>(
                matrix + Offsets::Entity::MatrixPosition);
            forward = *reinterpret_cast<const Vector3*>(
                matrix + Offsets::Entity::MatrixForward);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        if (!std::isfinite(position.x) ||
            !std::isfinite(position.y) ||
            !std::isfinite(position.z))
            return false;

        heading = std::atan2(-forward.x, forward.y) *
            (180.f / 3.14159265f);
        if (!std::isfinite(heading))
            heading = 0.f;
        if (heading < 0.f)
            heading += 360.f;
        return true;
    }

    bool Available() noexcept
    {
        const std::uintptr_t replay = ReplayInterface();
        if (!replay)
            return false;
        for (const Source& source : kSources)
        {
            std::uintptr_t interface_ = 0;
            if (!ReadPointer(replay + source.interfaceAt, interface_))
                continue;
            if (!IsValidPtr(reinterpret_cast<const void*>(interface_)))
                continue;
            std::uintptr_t list = 0;
            std::uint32_t count = 0;
            if (ReadPointer(interface_ + source.listAt, list) &&
                IsValidPtr(reinterpret_cast<const void*>(list)) &&
                ReadCount(interface_ + source.countAt, count))
                return true;
        }
        return false;
    }

    int Copy(Live* output, int capacity, unsigned kinds) noexcept
    {
        if (!output || capacity <= 0 || !kinds)
            return 0;
        const std::uintptr_t replay = ReplayInterface();
        if (!replay)
            return 0;

        int written = 0;
#if defined(RAGEMP_DIAGNOSTICS)
        int perKind[2]{};
#endif
        for (const Source& source : kSources)
        {
            if (!(kinds & source.mask))
                continue;

            std::uintptr_t interface_ = 0;
            if (!ReadPointer(replay + source.interfaceAt, interface_))
                continue;
            if (!IsValidPtr(reinterpret_cast<const void*>(interface_)))
                continue;

            std::uintptr_t list = 0;
            std::uint32_t count = 0;
            if (!ReadPointer(interface_ + source.listAt, list) ||
                !IsValidPtr(reinterpret_cast<const void*>(list)) ||
                !ReadCount(interface_ + source.countAt, count))
                continue;

            for (std::uint32_t index = 0;
                 index < count && written < capacity;
                 ++index)
            {
                std::uintptr_t entity = 0;
                if (!ReadPointer(
                        list + index * Offsets::Replay::Stride,
                        entity))
                    continue;
                if (!IsValidPtr(reinterpret_cast<const void*>(entity)))
                    continue;
                if (!HasType(entity, source.type))
                    continue;

                Vector3 position{};
                float heading = 0.f;
                if (!GameEntities::ReadPose(reinterpret_cast<const void*>(entity), position, heading))
                    continue;

                Live& live = output[written++];
                live.object = reinterpret_cast<const void*>(entity);
                live.kind = source.kind;
                live.position = position;
                live.heading = heading;
#if defined(RAGEMP_DIAGNOSTICS)
                ++perKind[static_cast<int>(source.kind)];
#endif
            }
        }

#if defined(RAGEMP_DIAGNOSTICS)
        {
            static ULONGLONG lastReport = 0;
            const ULONGLONG now = GetTickCount64();
            if (now - lastReport >= 3000)
            {
                lastReport = now;
                char line[128]{};
                _snprintf(
                    line,
                    sizeof(line) - 1,
                    "GameEntities: vehicles=%d objects=%d",
                    perKind[0],
                    perKind[1]);
                hooks::HLog(line);
            }
        }
#endif
        return written;
    }
}
