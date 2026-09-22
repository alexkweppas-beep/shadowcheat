#pragma once

#include <cstddef>
#include <cstdint>

// Combat and ESP tables that would otherwise sit as plaintext floats/indices
// in .rdata. Bake() decodes them once under VMProtect Ultra; every Present
// path then reads the POD copies below.
namespace CombatBake
{
    struct Capsule final
    {
        std::uint8_t from;
        std::uint8_t to;
        float radiusMetres;
    };

    struct Policy final
    {
        float hitboxStick;
        float coarseFovPad;
        float minBoneSpanMetres;
        float minPixelsPerMetre;
        float minCapsulePixels;
        float velocityBlend;
        float damagerOriginLead;
        float hitchanceFallbackMetres;
        float velocitySampleMaxDt;
        float silentMaxLead;
        float silentOvershoot;
        float silentBackoff;
        float silentMinRay;
        float silentWallshotMinLength;
        float missBiasScale;
        float visDepthTolerance;
        float espHeadToCrown;
        float espFootToSole;
        float espTypicalHead;
        float espTypicalFeet;
        float boneVelPrefer;
        float maxTrackedSpeed;
        std::uint32_t silentTargetAgeMs;
        std::uint32_t visFreshMs;
        std::uint32_t visIdleMs;
        std::uint32_t visFallbackFailures;
        std::uint32_t visFallbackHoldMs;
        std::uint32_t visSwitchMs;
        int visMaxProbes;
    };

    inline constexpr int kHitboxCount = 16;
    inline constexpr int kScalePairCount = 4;
    inline constexpr int kSkeletonPairCount = 15;
    inline constexpr int kZoneCount = 3;
    inline constexpr int kBoneCount = 16;

    extern Capsule g_Capsules[kHitboxCount];
    extern std::uint8_t g_ScalePairs[kScalePairCount][2];
    extern std::uint8_t g_ZoneForBone[kBoneCount];
    extern float g_ZoneHalfWidth[kZoneCount];
    extern std::uint8_t g_SkeletonFrom[kSkeletonPairCount];
    extern std::uint8_t g_SkeletonTo[kSkeletonPairCount];
    extern Policy g_Policy;
    extern volatile int g_GeometryReady;
    extern volatile int g_Ready;

    __declspec(noinline) void Bake() noexcept;
    __declspec(noinline) bool ApplyPolicy(
        const void* data,
        std::size_t size) noexcept;

    inline void Ensure() noexcept
    {
        if (!g_GeometryReady)
            Bake();
    }
}
