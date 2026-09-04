#include "WorldEntityMotion.h"

#include "RageWorldEntities.h"

#include <Windows.h>

#include <cmath>
#include <cstdio>

#include "../Main/Hooks.h"
#include "../SDK/RageBuildFlavor.h"

namespace
{
    // Power of two: the slot index is masked, not divided.
    constexpr int kSlots = 1024;
    constexpr int kProbeWindow = 8;

    // How far past the last sample the pose may be carried, as a multiple of
    // the interval actually being observed.
    //
    // This used to be a fixed 0.22 s, chosen against the 150 ms request
    // throttle. That was the wrong number: 150 ms is how often a snapshot is
    // asked for, not how often one lands, and the reply has a script-thread
    // round trip on top. Whenever the real gap ran past the cap the pose froze,
    // waited, then jumped when the next sample arrived, which is the stepping
    // this was supposed to remove. The interval is measured instead.
    constexpr float kPredictIntervals = 1.6f;
    constexpr float kMinPredictSeconds = 0.10f;
    constexpr float kMaxPredictSeconds = 0.80f;

    // Smoothed interval between accepted samples. One number for the whole
    // snapshot: every entity in it arrives on the same reply.
    float g_sampleInterval = 0.15f;

    // Time constant of the ease toward the predicted pose. Small enough that the
    // box stays on the car, large enough to swallow the correction that lands
    // with every new snapshot.
    constexpr float kSmoothSeconds = 0.055f;

    // Beyond this a sample is a teleport, a respawn or a recycled handle rather
    // than motion, and easing across it would drag the box over the map.
    constexpr float kTeleportMeters = 12.f;

    constexpr float kMaxSpeed = 150.f;          // m/s
    constexpr float kMaxHeadingRate = 720.f;    // deg/s
    constexpr ULONGLONG kStaleMs = 2000;

    struct Track final
    {
        bool used{};
        std::uint32_t handle{};
        std::uint64_t generation{};
        std::uint64_t stamp{};
        float sampleX{}, sampleY{}, sampleZ{}, sampleHeading{};
        float velocityX{}, velocityY{}, velocityZ{}, headingRate{};
        float renderX{}, renderY{}, renderZ{}, renderHeading{};
        ULONGLONG touched{};
    };

    Track g_tracks[kSlots]{};

    float WrapDegrees(float degrees) noexcept
    {
        degrees = std::fmod(degrees, 360.f);
        if (degrees < 0.f)
            degrees += 360.f;
        return degrees;
    }

    // Signed shortest way round, so 359 -> 1 is +2 and never -358.
    float ShortestArc(float degrees) noexcept
    {
        degrees = std::fmod(degrees + 180.f, 360.f);
        if (degrees < 0.f)
            degrees += 360.f;
        return degrees - 180.f;
    }

    float Clamp(float value, float limit) noexcept
    {
        if (!std::isfinite(value))
            return 0.f;
        if (value > limit)
            return limit;
        if (value < -limit)
            return -limit;
        return value;
    }

    Track* Acquire(std::uint32_t handle, ULONGLONG now) noexcept
    {
        const std::uint32_t hash = handle * 2654435761u;
        const int start = static_cast<int>(hash & (kSlots - 1));

        for (int step = 0; step < kProbeWindow; ++step)
        {
            Track& track = g_tracks[(start + step) & (kSlots - 1)];
            if (track.used && track.handle == handle)
                return &track;
        }

        Track* fallback = nullptr;
        ULONGLONG oldest = ~0ull;
        for (int step = 0; step < kProbeWindow; ++step)
        {
            Track& track = g_tracks[(start + step) & (kSlots - 1)];
            if (!track.used ||
                now - track.touched > kStaleMs)
            {
                track = {};
                track.used = true;
                track.handle = handle;
                return &track;
            }
            if (track.touched < oldest)
            {
                oldest = track.touched;
                fallback = &track;
            }
        }

        // Window full of live entries: the least recently drawn one loses. At
        // 512 snapshot entries against 1024 slots this is effectively dead code.
        if (fallback)
        {
            *fallback = {};
            fallback->used = true;
            fallback->handle = handle;
        }
        return fallback;
    }
}

namespace WorldEntityMotion
{
    void Smooth(
        RageWorldEntities::Entry* entries,
        int count,
        std::uint64_t generation,
        std::uint64_t capturedAtMs,
        float deltaSeconds,
        bool vehiclesAreLive) noexcept
    {
        if (!entries || count <= 0)
            return;

        const ULONGLONG now = GetTickCount64();

        // A stalled or absurd frame delta would either freeze the ease or make
        // it overshoot, so keep it inside what a real frame can be.
        if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.f)
            deltaSeconds = 1.f / 60.f;
        else if (deltaSeconds > 0.25f)
            deltaSeconds = 0.25f;

        const float alpha =
            1.f - std::exp(-deltaSeconds / kSmoothSeconds);

        // Measured once per snapshot rather than per entity, and eased so one
        // late reply does not swing the horizon.
        static std::uint64_t s_lastGeneration = 0;
        static ULONGLONG s_lastArrival = 0;
        if (generation != s_lastGeneration)
        {
            if (s_lastArrival != 0 && now > s_lastArrival)
            {
                const float observed =
                    static_cast<float>(now - s_lastArrival) / 1000.f;
                if (observed > 0.01f && observed < 2.f)
                    g_sampleInterval +=
                        (observed - g_sampleInterval) * 0.25f;
            }
            s_lastGeneration = generation;
            s_lastArrival = now;

#if defined(RAGEMP_DIAGNOSTICS)
            static ULONGLONG s_lastReport = 0;
            if (now - s_lastReport >= 3000)
            {
                s_lastReport = now;
                char line[128]{};
                _snprintf_s(
                    line,
                    sizeof(line),
                    _TRUNCATE,
                    "WorldEntityMotion: snapshot every %.0f ms, "
                    "predicting up to %.0f ms",
                    g_sampleInterval * 1000.f,
                    g_sampleInterval * kPredictIntervals * 1000.f);
                hooks::HLog(line);
            }
#endif
        }

        float horizon = g_sampleInterval * kPredictIntervals;
        if (horizon < kMinPredictSeconds)
            horizon = kMinPredictSeconds;
        else if (horizon > kMaxPredictSeconds)
            horizon = kMaxPredictSeconds;

        for (int index = 0; index < count; ++index)
        {
            RageWorldEntities::Entry& entry = entries[index];
            if (!entry.valid || entry.handle == 0)
                continue;
            if (vehiclesAreLive &&
                entry.kind == RageWorldEntities::EntityKind::Vehicle)
                continue;
            if (!std::isfinite(entry.x) ||
                !std::isfinite(entry.y) ||
                !std::isfinite(entry.z) ||
                !std::isfinite(entry.heading))
                continue;

            Track* track = Acquire(entry.handle, now);
            if (!track)
                continue;

            if (track->stamp == 0)
            {
                // First sighting: draw where the game says it is, with nothing
                // to interpolate from yet.
                track->generation = generation;
                track->stamp = capturedAtMs ? capturedAtMs : now;
                track->sampleX = entry.x;
                track->sampleY = entry.y;
                track->sampleZ = entry.z;
                track->sampleHeading = entry.heading;
                track->velocityX = 0.f;
                track->velocityY = 0.f;
                track->velocityZ = 0.f;
                track->headingRate = 0.f;
                track->renderX = entry.x;
                track->renderY = entry.y;
                track->renderZ = entry.z;
                track->renderHeading = entry.heading;
                track->touched = now;
                continue;
            }

            if (track->generation != generation)
            {
                const std::uint64_t stamp =
                    capturedAtMs ? capturedAtMs : now;
                const float span = stamp > track->stamp
                    ? static_cast<float>(stamp - track->stamp) / 1000.f
                    : 0.f;

                const float deltaX = entry.x - track->sampleX;
                const float deltaY = entry.y - track->sampleY;
                const float deltaZ = entry.z - track->sampleZ;
                const float moved = std::sqrt(
                    deltaX * deltaX +
                    deltaY * deltaY +
                    deltaZ * deltaZ);

                if (moved > kTeleportMeters)
                {
                    track->renderX = entry.x;
                    track->renderY = entry.y;
                    track->renderZ = entry.z;
                    track->renderHeading = entry.heading;
                    track->velocityX = 0.f;
                    track->velocityY = 0.f;
                    track->velocityZ = 0.f;
                    track->headingRate = 0.f;
                }
                else if (span > 0.001f && span < 1.f)
                {
                    track->velocityX =
                        Clamp(deltaX / span, kMaxSpeed);
                    track->velocityY =
                        Clamp(deltaY / span, kMaxSpeed);
                    track->velocityZ =
                        Clamp(deltaZ / span, kMaxSpeed);
                    track->headingRate = Clamp(
                        ShortestArc(
                            entry.heading -
                            track->sampleHeading) / span,
                        kMaxHeadingRate);
                }
                else
                {
                    // Sample gap too long to trust as motion, most likely the
                    // entity streamed out and back.
                    track->velocityX = 0.f;
                    track->velocityY = 0.f;
                    track->velocityZ = 0.f;
                    track->headingRate = 0.f;
                }

                track->generation = generation;
                track->stamp = stamp;
                track->sampleX = entry.x;
                track->sampleY = entry.y;
                track->sampleZ = entry.z;
                track->sampleHeading = entry.heading;
            }

            float age = now > track->stamp
                ? static_cast<float>(now - track->stamp) / 1000.f
                : 0.f;
            if (age > horizon)
                age = horizon;

            const float targetX =
                track->sampleX + track->velocityX * age;
            const float targetY =
                track->sampleY + track->velocityY * age;
            const float targetZ =
                track->sampleZ + track->velocityZ * age;
            const float targetHeading =
                track->sampleHeading + track->headingRate * age;

            track->renderX += (targetX - track->renderX) * alpha;
            track->renderY += (targetY - track->renderY) * alpha;
            track->renderZ += (targetZ - track->renderZ) * alpha;
            track->renderHeading = WrapDegrees(
                track->renderHeading +
                ShortestArc(
                    targetHeading - track->renderHeading) * alpha);
            track->touched = now;

            entry.x = track->renderX;
            entry.y = track->renderY;
            entry.z = track->renderZ;
            entry.heading = track->renderHeading;
        }
    }

    void Reset() noexcept
    {
        for (Track& track : g_tracks)
            track = {};
    }
}
