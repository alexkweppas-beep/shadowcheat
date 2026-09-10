#include "CombatBake.h"
#include "../vmp_markers.hpp"

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

    constexpr std::uint32_t PackSlot(
        std::uint32_t value,
        int slot) noexcept
    {
        return value ^ MixKey() ^
            (static_cast<std::uint32_t>(slot) * 0x9E3779B9u);
    }

    // Thousandths of the runtime float (1.35 → 1350).
    constexpr std::uint32_t kPackedMilli[] = {
        PackSlot(1350u, 0),     // hitboxStick
        PackSlot(260000u, 1),   // coarseFovPad
        PackSlot(200u, 2),      // minBoneSpanMetres
        PackSlot(500u, 3),      // minPixelsPerMetre
        PackSlot(500u, 4),      // minCapsulePixels
        PackSlot(420u, 5),      // velocityBlend
        PackSlot(500u, 6),      // damagerOriginLead
        PackSlot(40000u, 7),    // hitchanceFallbackMetres
        PackSlot(350u, 8),      // velocitySampleMaxDt
        PackSlot(7500u, 9),     // silentMaxLead
        PackSlot(600u, 10),     // silentOvershoot
        PackSlot(750u, 11),     // silentBackoff
        PackSlot(50u, 12),      // silentMinRay
        PackSlot(1200u, 13),    // silentWallshotMinLength
        PackSlot(80u, 14),      // missBiasScale
        PackSlot(250u, 15),     // visDepthTolerance
        PackSlot(130u, 16),     // espHeadToCrown
        PackSlot(90u, 17),      // espFootToSole
        PackSlot(760u, 18),     // espTypicalHead
        PackSlot(1000u, 19),    // espTypicalFeet
        PackSlot(850u, 20),     // boneVelPrefer
        PackSlot(120000u, 21),  // maxTrackedSpeed
    };

    constexpr std::uint32_t kPackedRaw[] = {
        PackSlot(200u, 0),      // silentTargetAgeMs
        PackSlot(400u, 1),      // visFreshMs
        PackSlot(4000u, 2),     // visIdleMs
        PackSlot(45u, 3),       // visFallbackFailures
        PackSlot(1500u, 4),     // visFallbackHoldMs
        PackSlot(120u, 5),      // visSwitchMs
        PackSlot(32u, 6),       // visMaxProbes
    };

    constexpr float DecodeMilli(
        std::uint32_t packed,
        int slot,
        std::uint32_t key) noexcept
    {
        const std::uint32_t milles =
            packed ^ key ^
            (static_cast<std::uint32_t>(slot) * 0x9E3779B9u);
        return static_cast<float>(milles) * 0.001f;
    }

    constexpr std::uint32_t DecodeRaw(
        std::uint32_t packed,
        int slot,
        std::uint32_t key) noexcept
    {
        return packed ^ key ^
            (static_cast<std::uint32_t>(slot) * 0x9E3779B9u);
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
    volatile int g_Ready = 0;

    __declspec(noinline) void Bake() noexcept
    {
        if (g_Ready)
            return;

        SHADOW_PROTECT_ULTRA("Combat.Bake");
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

        g_Policy.hitboxStick = DecodeMilli(kPackedMilli[0], 0, key);
        g_Policy.coarseFovPad = DecodeMilli(kPackedMilli[1], 1, key);
        g_Policy.minBoneSpanMetres = DecodeMilli(kPackedMilli[2], 2, key);
        g_Policy.minPixelsPerMetre = DecodeMilli(kPackedMilli[3], 3, key);
        g_Policy.minCapsulePixels = DecodeMilli(kPackedMilli[4], 4, key);
        g_Policy.velocityBlend = DecodeMilli(kPackedMilli[5], 5, key);
        g_Policy.damagerOriginLead = DecodeMilli(kPackedMilli[6], 6, key);
        g_Policy.hitchanceFallbackMetres =
            DecodeMilli(kPackedMilli[7], 7, key);
        g_Policy.velocitySampleMaxDt =
            DecodeMilli(kPackedMilli[8], 8, key);
        g_Policy.silentMaxLead = DecodeMilli(kPackedMilli[9], 9, key);
        g_Policy.silentOvershoot = DecodeMilli(kPackedMilli[10], 10, key);
        g_Policy.silentBackoff = DecodeMilli(kPackedMilli[11], 11, key);
        g_Policy.silentMinRay = DecodeMilli(kPackedMilli[12], 12, key);
        g_Policy.silentWallshotMinLength =
            DecodeMilli(kPackedMilli[13], 13, key);
        g_Policy.missBiasScale = DecodeMilli(kPackedMilli[14], 14, key);
        g_Policy.visDepthTolerance =
            DecodeMilli(kPackedMilli[15], 15, key);
        g_Policy.espHeadToCrown = DecodeMilli(kPackedMilli[16], 16, key);
        g_Policy.espFootToSole = DecodeMilli(kPackedMilli[17], 17, key);
        g_Policy.espTypicalHead = DecodeMilli(kPackedMilli[18], 18, key);
        g_Policy.espTypicalFeet = DecodeMilli(kPackedMilli[19], 19, key);
        g_Policy.boneVelPrefer = DecodeMilli(kPackedMilli[20], 20, key);
        g_Policy.maxTrackedSpeed = DecodeMilli(kPackedMilli[21], 21, key);

        g_Policy.silentTargetAgeMs = DecodeRaw(kPackedRaw[0], 0, key);
        g_Policy.visFreshMs = DecodeRaw(kPackedRaw[1], 1, key);
        g_Policy.visIdleMs = DecodeRaw(kPackedRaw[2], 2, key);
        g_Policy.visFallbackFailures = DecodeRaw(kPackedRaw[3], 3, key);
        g_Policy.visFallbackHoldMs = DecodeRaw(kPackedRaw[4], 4, key);
        g_Policy.visSwitchMs = DecodeRaw(kPackedRaw[5], 5, key);
        g_Policy.visMaxProbes =
            static_cast<int>(DecodeRaw(kPackedRaw[6], 6, key));

        g_Ready = 1;
        SHADOW_PROTECT_END();
    }
}
