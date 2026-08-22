#include "Visibility.h"

#include <cmath>
#include <cstdio>

#include <Windows.h>

#include "NativeInvoke.hpp"
#include "../Main/Hooks.h"
#include "../SDK/RageBuildFlavor.h"

namespace Visibility
{
    namespace
    {
        // 1 = world/map geometry, 16 = objects. Peds (4), ragdolls (8) and
        // vehicles (2) are all left out on purpose. Peds and ragdolls were the
        // whole defect: with them in the mask a single passer-by hid the target,
        // and in a crowd nobody was ever visible. Vehicles are out because the
        // vehicle bit is documented to stop reporting past roughly 30 m, which
        // would turn every distant target into a visible one at exactly the
        // range where the answer starts to matter.
        constexpr int kTraceFlags = 1 | 16;

        // Standard option set used by every working probe: no extra filtering
        // beyond the collision flags above.
        constexpr int kOptionFlags = 7;

        // Slack on the depth comparison. The ray ends at the bone, so a solid
        // hit is by construction no further than the bone; the tolerance is
        // there for the case where the geometry a target is pressed against
        // registers a hit essentially at the bone itself.
        constexpr float kDepthTolerance = 0.25f;

        // Small, because each probe blocks until the physics world answers.
        //
        // The asynchronous native was the original design: start a batch, read
        // the results on the next frame, pay nothing on this one. It does not
        // work from here. Measured in game, START_SHAPE_TEST_LOS_PROBE returned
        // a null handle on every single attempt, hundreds of them, so the whole
        // budget was spent on failures and not one answer ever came back. The
        // shape test manager will not reserve a cross-frame slot for a thread
        // that does not own the script runtime; the expensive synchronous native
        // completes inside the call and needs no such slot, which is why
        // click-warp's RaycastGroundZ has always worked from the Present thread.
        //
        // So the cost is real and paid per frame, and the budget is sized for
        // that. The crosshair term in the priority below is what makes three a
        // usable number: whoever is being aimed at is re-probed within a frame
        // or two, and distant bystanders refresh over the following second.
        constexpr int kMaxProbesPerFrame = 3;

        // How long an answer stays usable. Deliberately generous rather than a
        // couple of frames: unknown resolves to visible downstream, so dropping
        // a stale answer is never more accurate than keeping it, and in a dense
        // crowd a short window would expire zones faster than the budget could
        // refill them, flickering the whole background between the hidden and
        // normal palettes. Staleness is bounded where it matters instead, by the
        // crosshair term in the probe priority: whoever is being aimed at is
        // re-probed almost every frame, and only distant bystanders drift.
        constexpr std::uint64_t kFreshMs = 1200;

        // Slots untouched for this long are free for reuse. CPed pointers are
        // recycled by the game, so holding them indefinitely risks handing an
        // old answer to a new ped.
        constexpr std::uint64_t kSlotIdleMs = 4000;

        // After this many consecutive probes that produce no usable answer, the
        // RAGE snapshot flag is used instead. Counting attempts rather than
        // successful starts matters: the first version only noticed probes that
        // started and never answered, so when the native refused to start at
        // all the counter stayed at zero, the fallback never armed, and every
        // ped silently read as visible forever.
        constexpr std::uint32_t kFallbackAfterFailures = 45;

        constexpr int kSlotCount = 512;

        constexpr int kBoneHead = 0;
        constexpr int kBoneNeck = 1;
        constexpr int kBoneSpine = 2;
        constexpr int kBonePelvis = 3;

        struct Slot final
        {
            const CPed* ped{};
            std::uint8_t mask{};
            std::uint8_t known{};
            std::uint64_t stamp[kZoneCount]{};
            std::uint64_t touched{};
        };

        Slot s_slots[kSlotCount]{};
        LONG s_epoch = 0;
        bool s_epochValid = false;
        bool s_everResolved = false;
        std::uint32_t s_failuresSinceAnswer = 0;
        bool s_fallbackActive = false;
        Diagnostics s_diagnostics{};
        // Last raw values seen from the pair of natives, for the telemetry line
        // at the end of Update. Cheaper than guessing which half fails.
        int s_lastHandle = 0;
        int s_lastStatus = 0;

        int SlotHash(const CPed* ped) noexcept
        {
            const auto value =
                reinterpret_cast<std::uintptr_t>(ped);
            // Ped pointers are 16-byte aligned, so the low bits carry nothing.
            const auto mixed =
                static_cast<std::uint32_t>(
                    (value >> 4) * 2654435761u);
            return static_cast<int>(mixed % kSlotCount);
        }

        Slot* FindSlot(const CPed* ped) noexcept
        {
            if (!ped)
                return nullptr;
            int index = SlotHash(ped);
            for (int step = 0; step < 32; ++step)
            {
                Slot& slot = s_slots[index];
                if (slot.ped == ped)
                    return &slot;
                if (!slot.ped)
                    return nullptr;
                index = (index + 1) % kSlotCount;
            }
            return nullptr;
        }

        Slot* AcquireSlot(
            const CPed* ped,
            std::uint64_t now) noexcept
        {
            if (!ped)
                return nullptr;
            int index = SlotHash(ped);
            Slot* reusable = nullptr;
            for (int step = 0; step < 32; ++step)
            {
                Slot& slot = s_slots[index];
                if (slot.ped == ped)
                    return &slot;
                if (!slot.ped)
                {
                    slot = Slot{};
                    slot.ped = ped;
                    slot.touched = now;
                    return &slot;
                }
                // An idle occupant is only taken once the whole probe chain has
                // failed to find the ped itself, so a live entry is never
                // evicted by a colliding newcomer.
                if (!reusable &&
                    now - slot.touched > kSlotIdleMs)
                    reusable = &slot;
                index = (index + 1) % kSlotCount;
            }
            if (!reusable)
                return nullptr;
            *reusable = Slot{};
            reusable->ped = ped;
            reusable->touched = now;
            return reusable;
        }

        bool FiniteVector(const Vector3& value) noexcept
        {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        // Where to aim the ray for a zone. Bones come from the engine skeleton
        // and are preferred; the chest falls back to the same synthesised point
        // the coarse box already projects, so a ped that lost the frame's bone
        // budget still gets a usable answer instead of none.
        bool ResolveZoneTarget(
            const Game::FramePedEntry& entry,
            int zone,
            Vector3& target) noexcept
        {
            const std::uint32_t bones =
                entry.visual.boneMask;
            const auto has = [&](int bone) noexcept {
                return (bones & (1u << bone)) != 0 &&
                    FiniteVector(entry.visual.bones[bone]);
            };

            switch (zone)
            {
            case 0:
                if (has(kBoneHead))
                {
                    target = entry.visual.bones[kBoneHead];
                    return true;
                }
                if (has(kBoneNeck))
                {
                    target = entry.visual.bones[kBoneNeck];
                    return true;
                }
                return false;
            case 1:
                if (has(kBoneSpine))
                {
                    target = entry.visual.bones[kBoneSpine];
                    return true;
                }
                if (has(kBoneNeck))
                {
                    target = entry.visual.bones[kBoneNeck];
                    return true;
                }
                if (FiniteVector(entry.livePosition))
                {
                    target = entry.livePosition;
                    target.z += 0.55f;
                    return true;
                }
                return false;
            case 2:
                if (has(kBonePelvis))
                {
                    target = entry.visual.bones[kBonePelvis];
                    return true;
                }
                return false;
            default:
                return false;
            }
        }

        std::uint8_t FreshKnown(
            const Slot& slot,
            std::uint64_t now) noexcept
        {
            std::uint8_t known = slot.known;
            for (int zone = 0; zone < kZoneCount; ++zone)
            {
                const auto bit =
                    static_cast<std::uint8_t>(1u << zone);
                if (!(known & bit))
                    continue;
                if (now - slot.stamp[zone] > kFreshMs)
                    known = static_cast<std::uint8_t>(
                        known & ~bit);
            }
            return known;
        }

        // The stalest zone that can actually be traced. For a fresh slot every
        // stamp is zero, so this walks head, chest, pelvis in turn and fills the
        // mask over three frames; afterwards it keeps whichever zone has drifted
        // furthest out of date.
        int PickZone(
            const Game::FramePedEntry& entry,
            const Slot& slot,
            Vector3& target) noexcept
        {
            int best = -1;
            std::uint64_t oldest = 0;
            Vector3 bestTarget{};
            for (int zone = 0; zone < kZoneCount; ++zone)
            {
                Vector3 candidate{};
                if (!ResolveZoneTarget(entry, zone, candidate))
                    continue;
                const std::uint64_t stamp = slot.stamp[zone];
                if (best >= 0 && stamp > oldest)
                    continue;
                best = zone;
                oldest = stamp;
                bestTarget = candidate;
            }
            if (best < 0)
                return -1;
            target = bestTarget;
            return best;
        }
    }

    std::uint8_t ZoneForBone(int bone) noexcept
    {
        switch (bone)
        {
        case kBoneHead:
        case kBoneNeck:
            return ZoneHead;
        case kBoneSpine:
        // Arms and hands ride the upper body, so the chest answer is the one
        // that describes them.
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
            return ZoneChest;
        case kBonePelvis:
        // Legs and feet sit below the waist and share the pelvis answer, which
        // is what makes "only the head is over the wall" come out right.
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
        case 15:
            return ZonePelvis;
        default:
            return 0;
        }
    }

    void Reset() noexcept
    {
        for (Slot& slot : s_slots)
            slot = Slot{};
        s_diagnostics = Diagnostics{};
        s_epochValid = false;
        s_failuresSinceAnswer = 0;
        // Deliberately kept across sessions: whether Present-thread probes work
        // at all is a property of the build, not of the session, and re-earning
        // the verdict every session would flip the fallback on and off.
        s_diagnostics.everResolved = s_everResolved;
        s_diagnostics.usingSnapshotFallback = s_fallbackActive;
    }

    const Diagnostics& GetDiagnostics() noexcept
    {
        return s_diagnostics;
    }

    void Update(
        Game::FramePedContext& context,
        const Vector3& camera,
        const Vector2& center,
        bool demand) noexcept
    {
        const std::uint64_t now = GetTickCount64();

        if (!s_epochValid || s_epoch != context.sessionEpoch)
        {
            Reset();
            s_epoch = context.sessionEpoch;
            s_epochValid = true;
        }

        Diagnostics diagnostics{};
        diagnostics.everResolved = s_everResolved;

        // Asked once. On this target the native registration table is
        // encrypted, so no shape test can ever run from C++ and probing would
        // burn three failed calls a frame forever; see NativesAvailable in
        // NativeInvoke.hpp for the measurement. When that is the case the RAGE
        // snapshot flag is the only source of visibility there is, so the
        // fallback is armed immediately rather than after a failure count.
        static int s_nativesUsable = -1;
        if (s_nativesUsable < 0)
        {
            s_nativesUsable =
                NativeInvoke::NativesAvailable() ? 1 : 0;
            if (!s_nativesUsable)
            {
                s_fallbackActive = true;
                hooks::HLog(
                    "Visibility: natives unavailable on this build, "
                    "using the RAGE snapshot flag");
            }
        }
        if (!s_nativesUsable)
            demand = false;

        const bool cameraValid =
            FiniteVector(camera) &&
            (camera.x != 0.f || camera.y != 0.f ||
             camera.z != 0.f);

        // Publish, and score who deserves this frame's probes. Same shape as
        // the bone refresh budget: age dominates, and the crosshair breaks ties
        // so the ped being aimed at is refreshed before the one behind you.
        int priority[Game::kMaxFramePeds]{};
        float score[Game::kMaxFramePeds]{};
        int candidateCount = 0;

        for (int index = 0; index < context.count; ++index)
        {
            Game::FramePedEntry& entry =
                context.entries[index];
            entry.visibleZoneMask = 0;
            entry.visibleZoneKnown = 0;
            if (entry.synthetic || !entry.ped)
                continue;

            Slot* slot = FindSlot(entry.ped);
            std::uint8_t known = 0;
            if (slot)
            {
                slot->touched = now;
                known = FreshKnown(*slot, now);
                entry.visibleZoneKnown = known;
                entry.visibleZoneMask =
                    static_cast<std::uint8_t>(
                        slot->mask & known);
                ++diagnostics.tracked;
            }
            // Only where the prober has nothing of its own to say, and only
            // while it is demonstrably not working. The snapshot flag has no
            // per-zone detail, so it answers for the whole body.
            if (!known && s_fallbackActive &&
                entry.visualValid)
            {
                entry.visibleZoneKnown = ZoneAll;
                entry.visibleZoneMask =
                    entry.visual.visible ? ZoneAll : 0;
            }

            if (!demand || !cameraValid ||
                !entry.coarseVisible)
                continue;

            std::uint64_t oldest = now;
            for (int zone = 0; zone < kZoneCount; ++zone)
            {
                const std::uint64_t stamp =
                    slot ? slot->stamp[zone] : 0;
                if (stamp < oldest)
                    oldest = stamp;
            }
            std::uint64_t age = now - oldest;
            if (age > 5000)
                age = 5000;

            const float dx = entry.coarseScreen.x - center.x;
            const float dy = entry.coarseScreen.y - center.y;
            score[candidateCount] =
                static_cast<float>(age) * 100000.f -
                (dx * dx + dy * dy);
            priority[candidateCount] = index;
            ++candidateCount;
        }
        diagnostics.candidates =
            static_cast<std::uint32_t>(candidateCount);

        for (int index = 1; index < candidateCount; ++index)
        {
            const int value = priority[index];
            const float valueScore = score[index];
            int insert = index;
            while (insert > 0 &&
                score[insert - 1] < valueScore)
            {
                priority[insert] = priority[insert - 1];
                score[insert] = score[insert - 1];
                --insert;
            }
            priority[insert] = value;
            score[insert] = valueScore;
        }

        int budget = kMaxProbesPerFrame;
        for (int rank = 0;
             rank < candidateCount && budget > 0;
             ++rank)
        {
            Game::FramePedEntry& entry =
                context.entries[priority[rank]];
            Slot* slot = AcquireSlot(entry.ped, now);
            if (!slot)
                continue;
            slot->touched = now;

            Vector3 target{};
            const int zone =
                PickZone(entry, *slot, target);
            if (zone < 0)
                continue;

            const float distance =
                (target - camera).Length();
            // Nothing can occlude a target inside the camera's own near plane,
            // and a degenerate ray would waste one of three probes.
            if (!(distance > 0.35f))
            {
                slot->mask = ZoneAll;
                slot->known = ZoneAll;
                for (int index = 0; index < kZoneCount; ++index)
                    slot->stamp[index] = now;
                continue;
            }

            --budget;

            // Entity 0 rather than the local ped handle: peds are already out
            // of the collision flags, so there is nothing to exclude, and
            // PLAYER_PED_ID is one of the natives that reads back empty on the
            // Present thread.
            const int handle =
                NativeInvoke::
                    StartExpensiveSynchronousShapeTestLosProbe(
                        camera.x,
                        camera.y,
                        camera.z,
                        target.x,
                        target.y,
                        target.z,
                        kTraceFlags,
                        0,
                        kOptionFlags);
            NativeInvoke::ShapeTestResult result{};
            const bool answered = handle &&
                NativeInvoke::GetShapeTestResult(handle, result);
            ++diagnostics.started;
            s_lastHandle = handle;
            s_lastStatus = result.status;
            if (!answered)
            {
                ++diagnostics.dropped;
                ++s_failuresSinceAnswer;
                continue;
            }

            bool blocked = false;
            if (result.hit)
            {
                const Vector3 point{
                    result.x, result.y, result.z};
                // A hit the engine could not describe still means something is
                // in the way.
                blocked = !FiniteVector(point) ||
                    (point - camera).Length() <
                        distance - kDepthTolerance;
            }

            const auto bit =
                static_cast<std::uint8_t>(1u << zone);
            slot->mask = blocked
                ? static_cast<std::uint8_t>(slot->mask & ~bit)
                : static_cast<std::uint8_t>(slot->mask | bit);
            slot->known =
                static_cast<std::uint8_t>(slot->known | bit);
            slot->stamp[zone] = now;
            ++diagnostics.resolved;
            s_everResolved = true;
            diagnostics.everResolved = true;
            s_failuresSinceAnswer = 0;
            s_fallbackActive = false;
        }

        if (!s_fallbackActive &&
            s_failuresSinceAnswer > kFallbackAfterFailures)
        {
            s_fallbackActive = true;
            hooks::HLog(
                "Visibility: shape tests keep failing on the Present "
                "thread, falling back to the RAGE snapshot flag");
        }
        diagnostics.usingSnapshotFallback = s_fallbackActive;

        s_diagnostics = diagnostics;

#if defined(RAGEMP_DIAGNOSTICS)
        // resolved staying at zero while probed climbs is the failure this is
        // watching for: it is exactly how the asynchronous native was caught
        // returning a null handle on every single attempt.
        {
            static std::uint64_t s_lastReport = 0;
            if (now - s_lastReport >= 2000)
            {
                s_lastReport = now;

                static bool s_threadLogged = false;
                if (!s_threadLogged)
                {
                    s_threadLogged = true;
                    char who[96]{};
                    _snprintf_s(
                        who,
                        sizeof(who),
                        _TRUNCATE,
                        "Visibility: render thread=%lu",
                        GetCurrentThreadId());
                    hooks::HLog(who);
                }

                char line[224]{};
                _snprintf_s(
                    line,
                    sizeof(line),
                    _TRUNCATE,
                    "Visibility: natives=%d demand=%d cand=%u tracked=%u "
                    "probed=%u resolved=%u dropped=%u handle=%d status=%d "
                    "everResolved=%d fallback=%d",
                    s_nativesUsable,
                    demand ? 1 : 0,
                    diagnostics.candidates,
                    diagnostics.tracked,
                    diagnostics.started,
                    diagnostics.resolved,
                    diagnostics.dropped,
                    s_lastHandle,
                    s_lastStatus,
                    diagnostics.everResolved ? 1 : 0,
                    diagnostics.usingSnapshotFallback ? 1 : 0);
                hooks::HLog(line);
            }
        }
#endif
    }
}
