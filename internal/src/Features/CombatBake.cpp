#include "CombatBake.h"
#include "../vmp_markers.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>

namespace
{
    constexpr std::uint32_t MixKey() noexcept
    {
        return 0x6B2D41C3u ^ 0xCCEE5F9Au;
    }

    constexpr std::uint32_t PackCapsule(
        std::uint8_t from,
        std::uint8_t to,
        std::uint16_t millimetres) noexcept
    {
        return (static_cast<std::uint32_t>(from)
            | (static_cast<std::uint32_t>(to) << 8)
            | (static_cast<std::uint32_t>(millimetres) << 16)) ^ MixKey();
    }

    constexpr std::uint32_t PackPair(
        std::uint8_t from,
        std::uint8_t to,
        int slot) noexcept
    {
        return (static_cast<std::uint32_t>(from)
            | (static_cast<std::uint32_t>(to) << 8)
            | (static_cast<std::uint32_t>(slot) << 16)) ^ MixKey();
    }

    constexpr std::uint32_t PackBytes(
        std::uint8_t a,
        std::uint8_t b,
        std::uint8_t c,
        std::uint8_t d) noexcept
    {
        return (static_cast<std::uint32_t>(a)
            | (static_cast<std::uint32_t>(b) << 8)
            | (static_cast<std::uint32_t>(c) << 16)
            | (static_cast<std::uint32_t>(d) << 24)) ^ MixKey();
    }

    constexpr std::uint32_t kPackedCapsules[CombatBake::kHitboxCount] = {
        PackCapsule(0, 0, 120),
        PackCapsule(1, 2, 100),
        PackCapsule(2, 3, 190),
        PackCapsule(3, 3, 170),
        PackCapsule(4, 5, 70),
        PackCapsule(5, 6, 60),
        PackCapsule(6, 6, 60),
        PackCapsule(7, 8, 70),
        PackCapsule(8, 9, 60),
        PackCapsule(9, 9, 60),
        PackCapsule(10, 11, 90),
        PackCapsule(11, 12, 70),
        PackCapsule(12, 12, 70),
        PackCapsule(13, 14, 90),
        PackCapsule(14, 15, 70),
        PackCapsule(15, 15, 70),
    };

    constexpr std::uint32_t kPackedScalePairs[CombatBake::kScalePairCount] = {
        PackPair(0, 3, 0),
        PackPair(10, 12, 1),
        PackPair(13, 15, 2),
        PackPair(4, 7, 3),
    };

    constexpr std::uint32_t kPackedZones[4] = {
        PackBytes(1, 1, 2, 4),
        PackBytes(2, 2, 2, 2),
        PackBytes(2, 2, 4, 4),
        PackBytes(4, 4, 4, 4),
    };

    constexpr std::uint32_t kPackedWidths[CombatBake::kZoneCount] = {
        130u ^ MixKey(),
        280u ^ MixKey(),
        220u ^ MixKey(),
    };

    constexpr std::uint32_t kPackedSkeleton[CombatBake::kSkeletonPairCount] = {
        PackPair(0, 1, 0),
        PackPair(1, 2, 1),
        PackPair(2, 3, 2),
        PackPair(1, 4, 3),
        PackPair(4, 5, 4),
        PackPair(5, 6, 5),
        PackPair(1, 7, 6),
        PackPair(7, 8, 7),
        PackPair(8, 9, 8),
        PackPair(3, 10, 9),
        PackPair(10, 11, 10),
        PackPair(11, 12, 11),
        PackPair(3, 13, 12),
        PackPair(13, 14, 13),
        PackPair(14, 15, 14),
    };

    bool ValidPolicy(const CombatBake::Policy& policy) noexcept
    {
        static_assert(
            sizeof(CombatBake::Policy) == 116,
            "server policy wire layout changed");
        std::array<float, 22> values{};
        std::memcpy(
            values.data(),
            &policy,
            sizeof(values));
        for (const float value : values)
        {
            if (!std::isfinite(value) ||
                value < 0.f ||
                value > 10000.f)
                return false;
        }
        return policy.silentTargetAgeMs <= 5000 &&
            policy.visFreshMs <= 5000 &&
            policy.visIdleMs <= 60000 &&
            policy.visFallbackFailures <= 1000 &&
            policy.visFallbackHoldMs <= 60000 &&
            policy.visSwitchMs <= 5000 &&
            policy.visMaxProbes > 0 &&
            policy.visMaxProbes <= 64;
    }

}

namespace CombatBake
{
    Capsule g_Capsules[kHitboxCount]{};
    std::uint8_t g_ScalePairs[kScalePairCount][2]{};
    std::uint8_t g_ZoneForBone[kBoneCount]{};
    float g_ZoneHalfWidth[kZoneCount]{};
    std::uint8_t g_SkeletonFrom[kSkeletonPairCount]{};
    std::uint8_t g_SkeletonTo[kSkeletonPairCount]{};
    Policy g_Policy{};
    volatile int g_GeometryReady = 0;
    volatile int g_Ready = 0;

    __declspec(noinline) void Bake() noexcept
    {
        if (g_GeometryReady)
            return;

        SHADOW_PROTECT_MUT("Combat.Bake");
        const std::uint32_t key = MixKey();

        for (int i = 0; i < kHitboxCount; ++i)
        {
            const std::uint32_t word = kPackedCapsules[i] ^ key;
            g_Capsules[i].from =
                static_cast<std::uint8_t>(word);
            g_Capsules[i].to =
                static_cast<std::uint8_t>(word >> 8);
            g_Capsules[i].radiusMetres =
                static_cast<float>((word >> 16) & 0xFFFFu) * 0.001f;
        }

        for (int i = 0; i < kScalePairCount; ++i)
        {
            const std::uint32_t word = kPackedScalePairs[i] ^ key;
            g_ScalePairs[i][0] = static_cast<std::uint8_t>(word);
            g_ScalePairs[i][1] = static_cast<std::uint8_t>(word >> 8);
        }

        for (int i = 0; i < 4; ++i)
        {
            const std::uint32_t word = kPackedZones[i] ^ key;
            g_ZoneForBone[i * 4 + 0] =
                static_cast<std::uint8_t>(word);
            g_ZoneForBone[i * 4 + 1] =
                static_cast<std::uint8_t>(word >> 8);
            g_ZoneForBone[i * 4 + 2] =
                static_cast<std::uint8_t>(word >> 16);
            g_ZoneForBone[i * 4 + 3] =
                static_cast<std::uint8_t>(word >> 24);
        }

        for (int i = 0; i < kZoneCount; ++i)
        {
            const std::uint32_t mm = (kPackedWidths[i] ^ key) & 0xFFFFu;
            g_ZoneHalfWidth[i] = static_cast<float>(mm) * 0.001f;
        }

        for (int i = 0; i < kSkeletonPairCount; ++i)
        {
            const std::uint32_t word = kPackedSkeleton[i] ^ key;
            g_SkeletonFrom[i] = static_cast<std::uint8_t>(word);
            g_SkeletonTo[i] = static_cast<std::uint8_t>(word >> 8);
        }

        g_GeometryReady = 1;
        SHADOW_PROTECT_END();
    }

    __declspec(noinline) bool ApplyPolicy(
        const void* data,
        std::size_t size) noexcept
    {
        if (!data || size < sizeof(Policy))
            return false;
        Bake();
        bool accepted = false;
        SHADOW_PROTECT_MUT("Combat.Policy");
        Policy candidate{};
        std::memcpy(&candidate, data, sizeof(candidate));
        if (ValidPolicy(candidate))
        {
            g_Policy = candidate;
            g_Ready = 1;
            accepted = true;
        }
        SecureZeroMemory(&candidate, sizeof(candidate));
        SHADOW_PROTECT_END();
        return accepted;
    }
}
