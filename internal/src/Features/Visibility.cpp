#include "Visibility.h"

#include <cmath>
#include <cstdio>

#include <Windows.h>

#include "CombatBake.h"
#include "NativeInvoke.hpp"
#include "WorldProbe.h"
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
        // Sized for asynchronous probes, which is what these now are. Three was
        // right when a probe blocked the render thread until the physics world
        // answered; submitting to the shape test manager only appends a handle
        // to a queue under a lock, so the render thread pays almost nothing and
        // the answer arrives on a later frame.
        //
        // Three was also actively harmful at that rate: with fifty peds on
        // screen and answers good for just over a second, most of them were
        // permanently unmeasured, and unmeasured reads as visible. The palette
        // then looked arbitrary, and aiming at someone was enough to change
        // their colour, because the crosshair term below pushed them to the
        // front of a queue this short.
        //
        // Kept well under the manager's ninety slots regardless: the game
        // submits its own probes through the same pool, and starving those to
        // colour boxes would be a poor trade.
        //
        // Sized against the freshness window rather than picked: nine samples
        // per ped have to be re-measured inside it, so a scene with forty
        // candidates needs roughly nine probes a frame at a hundred frames a
        // second just to keep up. This leaves room above that.
        constexpr int kSlotCount = 512;

        constexpr int kBoneHead = 0;
        constexpr int kBoneNeck = 1;
        constexpr int kBoneSpine = 2;
        constexpr int kBonePelvis = 3;

        // How long a snapshot-derived answer is held once the ped's visual
        // snapshot stops arriving. Long enough to ride out a few missed
        // snapshot cycles, short enough that a ped who genuinely stopped being
        // reported does not keep an ageing verdict indefinitely.

        // Three points across each zone rather than one down its axis. Tracing
        // to the bone centre answers "is the middle of the chest exposed", and
        // that is not the question: a target leaning out of cover shows a
        // shoulder or an arm while every bone centre is still behind the wall,
        // so it read as hidden until it had stepped most of the way out.
        //
        // Centre is kept alongside the two edges because thin cover, a pole or a
        // sign, can hide the middle while both edges are clear.
        constexpr int kSampleCount = 3;
        constexpr int kSampleCentre = 0;

        struct Slot final
        {
            const CPed* ped{};
            // One bit per sample, per zone: set means that point had a clear
            // line. A zone counts as visible when any of its fresh samples is
            // clear, which is what makes a peeking target read as visible.
            std::uint8_t sampleMask[kZoneCount]{};
            std::uint8_t sampleKnown[kZoneCount]{};
            std::uint64_t stamp[kZoneCount][kSampleCount]{};
            std::uint64_t touched{};
            // Last answer taken from the RAGE snapshot flag, kept separately
            // from mask/known so that the prober coming back to life takes
            // precedence over it rather than being blended with it.
            std::uint8_t fallbackMask{};
            std::uint64_t fallbackStamp{};
            // What consumers actually see, and the candidate waiting to replace
            // it. See Stabilize.
            std::uint8_t publishedMask{};
            std::uint8_t publishedKnown{};
            std::uint8_t pendingMask{};
            std::uint8_t pendingKnown{};
            std::uint64_t pendingSince{};
            // The probe this ped currently has in flight, and what it asked
            // about. The manager answers a frame or more after submission, so
            // the question has to outlive the frame that asked it.
            int probeToken{-1};
            std::int8_t probeZone{-1};
            std::int8_t probeSample{-1};
            float probeDistance{};
            // Where the ray started. The camera has usually moved by the time
            // the answer arrives, and comparing the hit against the current
            // camera instead of the one that asked turns a clean hit into a
            // near miss whenever the player is moving.
            Vector3 probeOrigin{};
        };

        // A new answer has to hold for this long before it replaces the one on
        // screen, so a single odd sample cannot change a colour.
        //
        // Was 250 ms, which was sized against the snapshot flag's noise and is
        // simply latency now that the noise is gone. It stays non-zero because
        // the probe is not perfectly quiet either: a target at a shallow angle
        // to a wall can produce one disagreeing sample.
        //
        // The delay is symmetric. Making only visible-to-hidden slow would let
        // the noise through in the direction that matters most, since unknown
        // and failed samples both read as visible.

        // Returns true when the published answer changed.
        bool Stabilize(
            Slot& slot,
            std::uint8_t known,
            std::uint8_t mask,
            std::uint64_t now) noexcept
        {
            // Nothing to say this frame: whatever is on screen stays.
            if (!known)
                return false;
            // First answer for this ped goes up immediately. Waiting would mean
            // a quarter second of "unknown", which reads as visible, so the
            // delay would create the very flash it exists to prevent.
            if (!slot.publishedKnown)
            {
                slot.publishedKnown = known;
                slot.publishedMask = mask;
                slot.pendingSince = 0;
                return true;
            }
            if (known == slot.publishedKnown &&
                mask == slot.publishedMask)
            {
                slot.pendingSince = 0;
                return false;
            }
            if (!slot.pendingSince ||
                known != slot.pendingKnown ||
                mask != slot.pendingMask)
            {
                slot.pendingKnown = known;
                slot.pendingMask = mask;
                slot.pendingSince = now;
                return false;
            }
            if (now - slot.pendingSince <
                CombatBake::g_Policy.visSwitchMs)
                return false;
            slot.publishedKnown = slot.pendingKnown;
            slot.publishedMask = slot.pendingMask;
            slot.pendingSince = 0;
            return true;
        }

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
        // Flip counters summed over the telemetry window. Per-frame values are
        // useless here: the report samples one frame in a couple of hundred, so
        // the frame it happens to land on says nothing about the rate.
        std::uint32_t s_flipWindow = 0;
        std::uint32_t s_switchWindow = 0;
        // Answers after the depth comparison, which is the only place the
        // target's own body is told apart from cover in front of it. The raw
        // hit counters in WorldProbe cannot show this: the ray ends on the
        // target, so nearly every probe reports a hit either way.
        std::uint32_t s_hiddenWindow = 0;
        std::uint32_t s_visibleWindow = 0;

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
                    now - slot.touched > CombatBake::g_Policy.visIdleMs)
                    reusable = &slot;
                index = (index + 1) % kSlotCount;
            }
            if (!reusable)
                return nullptr;
            // The evicted occupant may still have a probe out. Dropping the
            // token without cancelling would hold one of the manager's ninety
            // slots until its deadline for a ped nobody is asking about.
            if (reusable->probeToken >= 0)
                WorldProbe::Cancel(reusable->probeToken);
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

        // Collects answers for probes submitted on earlier frames. Kept separate
        // from submission because the manager is asynchronous: it appends the
        // handle to a queue its own scheduler drains, so asking and answering
        // cannot happen in one frame.
        void Harvest(
            std::uint64_t now,
            Diagnostics& diagnostics) noexcept
        {
            for (Slot& slot : s_slots)
            {
                if (slot.probeToken < 0)
                    continue;

                Vector3 point{};
                const WorldProbe::Result result =
                    WorldProbe::Poll(slot.probeToken, &point);
                if (result == WorldProbe::Result::Pending)
                    continue;

                const int zone = slot.probeZone;
                const int sample = slot.probeSample;
                slot.probeToken = -1;
                slot.probeZone = -1;
                if (result == WorldProbe::Result::Lost ||
                    zone < 0 || zone >= kZoneCount ||
                    sample < 0 || sample >= kSampleCount)
                {
                    ++diagnostics.dropped;
                    ++s_failuresSinceAnswer;
                    continue;
                }

                bool blocked = false;
                if (result == WorldProbe::Result::Blocked)
                {
                    // A hit the engine could not describe still means
                    // something is in the way.
                    blocked = !FiniteVector(point) ||
                        (point - slot.probeOrigin).Length() <
                            slot.probeDistance -
                            CombatBake::g_Policy.visDepthTolerance;
                }

                if (blocked)
                    ++s_hiddenWindow;
                else
                    ++s_visibleWindow;

                const auto bit =
                    static_cast<std::uint8_t>(1u << sample);
                slot.sampleMask[zone] = blocked
                    ? static_cast<std::uint8_t>(
                          slot.sampleMask[zone] & ~bit)
                    : static_cast<std::uint8_t>(
                          slot.sampleMask[zone] | bit);
                slot.sampleKnown[zone] =
                    static_cast<std::uint8_t>(
                        slot.sampleKnown[zone] | bit);
                slot.stamp[zone][sample] = now;
                ++diagnostics.resolved;
                s_everResolved = true;
                diagnostics.everResolved = true;
                s_failuresSinceAnswer = 0;
                s_fallbackActive = false;
            }
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

        // Collapses the per-sample state into the per-zone answer consumers ask
        // for. A zone is known once any of its samples is, and visible if any
        // fresh sample had a clear line: the target only has to be exposed
        // somewhere across its width to count as seen.
        void DeriveZones(
            const Slot& slot,
            std::uint64_t now,
            std::uint8_t& known,
            std::uint8_t& mask) noexcept
        {
            known = 0;
            mask = 0;
            for (int zone = 0; zone < kZoneCount; ++zone)
            {
                std::uint8_t fresh = 0;
                for (int sample = 0; sample < kSampleCount; ++sample)
                {
                    const auto bit =
                        static_cast<std::uint8_t>(1u << sample);
                    if (!(slot.sampleKnown[zone] & bit))
                        continue;
                    if (now - slot.stamp[zone][sample] >
                        CombatBake::g_Policy.visFreshMs)
                        continue;
                    fresh = static_cast<std::uint8_t>(fresh | bit);
                }
                if (!fresh)
                    continue;
                const auto zoneBit =
                    static_cast<std::uint8_t>(1u << zone);
                known = static_cast<std::uint8_t>(known | zoneBit);
                if (slot.sampleMask[zone] & fresh)
                    mask = static_cast<std::uint8_t>(mask | zoneBit);
            }
        }

        // The stalest (zone, sample) pair that can actually be traced. For a
        // fresh slot every stamp is zero, so this walks the nine combinations in
        // turn and fills them in over a few frames; afterwards it keeps whichever
        // has drifted furthest out of date.
        bool PickSample(
            const Game::FramePedEntry& entry,
            const Slot& slot,
            const Vector3& camera,
            Vector3& target,
            int& outZone,
            int& outSample) noexcept
        {
            int bestZone = -1;
            int bestSample = 0;
            std::uint64_t oldest = 0;
            Vector3 bestCentre{};
            for (int zone = 0; zone < kZoneCount; ++zone)
            {
                Vector3 centre{};
                if (!ResolveZoneTarget(entry, zone, centre))
                    continue;
                for (int sample = 0; sample < kSampleCount; ++sample)
                {
                    const std::uint64_t stamp =
                        slot.stamp[zone][sample];
                    if (bestZone >= 0 && stamp > oldest)
                        continue;
                    bestZone = zone;
                    bestSample = sample;
                    oldest = stamp;
                    bestCentre = centre;
                }
            }
            if (bestZone < 0)
                return false;

            target = bestCentre;
            if (bestSample != kSampleCentre)
            {
                // Sideways across the view, so the two edge samples straddle the
                // silhouette as it appears from here rather than as the target
                // happens to be facing.
                const Vector3 forward = bestCentre - camera;
                Vector3 side{-forward.y, forward.x, 0.f};
                const float length = std::sqrt(
                    side.x * side.x + side.y * side.y);
                if (length > 0.001f)
                {
                    const float scale =
                        CombatBake::g_ZoneHalfWidth[bestZone] / length *
                        (bestSample == 1 ? 1.f : -1.f);
                    target.x += side.x * scale;
                    target.y += side.y * scale;
                }
            }
            outZone = bestZone;
            outSample = bestSample;
            return true;
        }
    }

    std::uint8_t ZoneForBone(int bone) noexcept
    {
        CombatBake::Ensure();
        if (bone < 0 || bone >= CombatBake::kBoneCount)
            return 0;
        return CombatBake::g_ZoneForBone[bone];
    }

    void Reset() noexcept
    {
        // Before the slots are wiped, or every probe still out there keeps a
        // manager slot booked against a ped that no longer exists.
        WorldProbe::CancelAll();
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
        CombatBake::Ensure();
        const std::uint64_t now = GetTickCount64();

        if (!s_epochValid || s_epoch != context.sessionEpoch)
        {
            Reset();
            s_epoch = context.sessionEpoch;
            s_epochValid = true;
        }

        Diagnostics diagnostics{};
        diagnostics.everResolved = s_everResolved;

        // Asked once, and asked of the world probe rather than of the native
        // invoker. This gate used to test whether natives resolved, which is
        // never on this target, and it therefore switched probing off entirely
        // and pinned the snapshot fallback on. Probes no longer go through a
        // native: they call the game's shape test manager directly, so whether
        // natives work has nothing to do with it. Leaving the old condition in
        // place meant the new path was compiled, shipped, and never once run.
        static int s_probeUsable = -1;
        if (s_probeUsable < 0)
        {
            s_probeUsable = WorldProbe::Initialize() ? 1 : 0;
            if (!s_probeUsable)
            {
                s_fallbackActive = true;
                hooks::HLog(
                    "Visibility: world probe could not be resolved, "
                    "using the RAGE snapshot flag");
            }
        }
        if (!s_probeUsable)
            demand = false;

        const bool cameraValid =
            FiniteVector(camera) &&
            (camera.x != 0.f || camera.y != 0.f ||
             camera.z != 0.f);

        // Answers first, before anything is published, so a result that landed
        // since the last frame is visible to consumers this frame rather than
        // the next one.
        Harvest(now, diagnostics);

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
            std::uint8_t rawKnown = 0;
            std::uint8_t rawMask = 0;
            if (slot)
            {
                slot->touched = now;
                DeriveZones(*slot, now, rawKnown, rawMask);
                known = rawKnown;
                ++diagnostics.tracked;
            }
            // Only where the prober has nothing of its own to say, and only
            // while it is demonstrably not working. The snapshot flag has no
            // per-zone detail, so it answers for the whole body.
            //
            // The answer is held in the slot rather than read straight out of
            // the entry, because the entry only carries one when its visual
            // snapshot is valid. Reading it directly meant every gap in the
            // snapshot published "unknown" for that ped, unknown reads as
            // visible downstream, and a target behind a wall blinked out of the
            // hidden palette and back for as long as the gap lasted.
            if (!known && s_fallbackActive)
            {
                if (!slot)
                    slot = AcquireSlot(entry.ped, now);
                if (slot)
                {
                    slot->touched = now;
                    if (entry.visualValid)
                    {
                        slot->fallbackMask =
                            entry.visual.visible ? ZoneAll : 0;
                        slot->fallbackStamp = now;
                    }
                    if (slot->fallbackStamp &&
                        now - slot->fallbackStamp <=
                            CombatBake::g_Policy.visFallbackHoldMs)
                    {
                        rawKnown = ZoneAll;
                        rawMask = slot->fallbackMask;
                    }
                }
                else if (entry.visualValid)
                {
                    rawKnown = ZoneAll;
                    rawMask =
                        entry.visual.visible ? ZoneAll : 0;
                }
            }

            if (slot)
            {
                if (rawKnown &&
                    (rawKnown != slot->publishedKnown ||
                     rawMask != slot->publishedMask))
                {
                    ++diagnostics.rawFlips;
                    ++s_flipWindow;
                }
                if (Stabilize(*slot, rawKnown, rawMask, now))
                {
                    ++diagnostics.switches;
                    ++s_switchWindow;
                }
                entry.visibleZoneKnown = slot->publishedKnown;
                entry.visibleZoneMask = slot->publishedMask;
            }
            else
            {
                entry.visibleZoneKnown = rawKnown;
                entry.visibleZoneMask = rawMask;
            }

            if (!demand || !cameraValid ||
                !entry.coarseVisible)
                continue;

            std::uint64_t oldest = now;
            for (int zone = 0; zone < kZoneCount; ++zone)
            {
                for (int sample = 0; sample < kSampleCount; ++sample)
                {
                    const std::uint64_t stamp =
                        slot ? slot->stamp[zone][sample] : 0;
                    if (stamp < oldest)
                        oldest = stamp;
                }
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

        // Resolved once for the whole batch: the probe starts at the camera,
        // which sits behind the player in third person, so without excluding
        // the player's own body every target reads as blocked.
        const CPed* const localPed = Game::GetLocalPed();

        // No local ped means no session: a loading screen, or a reconnect in
        // progress. Nothing is worth probing then, and calling into the shape
        // test manager while the world is still being assembled is asking for
        // trouble for an answer nobody is going to read. The rest of the
        // function still runs, so the telemetry keeps reporting.
        if (!localPed)
            WorldProbe::CancelAll();

        // Capped by what we may still take without crowding the game out of
        // its own shape test pool. Submitting past that comes back as a refusal
        // that reads exactly like the game refusing us, which would count as a
        // failure and eventually switch visibility to the fallback for no
        // reason other than our own back-pressure.
        int budget = localPed ? CombatBake::g_Policy.visMaxProbes : 0;
        const int capacity = WorldProbe::Free();
        if (budget > capacity)
            budget = capacity;
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
            // One question at a time per ped. Without this the budget would be
            // spent re-asking about peds whose answer is already on its way.
            if (slot->probeToken >= 0)
                continue;

            Vector3 target{};
            int zone = -1;
            int sample = -1;
            if (!PickSample(
                    entry, *slot, camera, target, zone, sample))
                continue;

            const float distance =
                (target - camera).Length();
            // Nothing can occlude a target inside the camera's own near plane,
            // and a degenerate ray would waste one of three probes.
            if (!(distance > 0.35f))
            {
                for (int index = 0; index < kZoneCount; ++index)
                {
                    slot->sampleMask[index] = 0x7;
                    slot->sampleKnown[index] = 0x7;
                    for (int which = 0;
                         which < kSampleCount;
                         ++which)
                        slot->stamp[index][which] = now;
                }
                continue;
            }

            --budget;

            // Straight into the game's world probe manager. This used to go
            // through NativeInvoke, which cannot work here: RAGE MP re-points
            // every entry in the game's native table at one obfuscated
            // dispatcher that identifies the native from script VM state, so a
            // lookup by hash returns something uncallable from this thread. The
            // subsystem the natives sit on top of has no such problem.
            //
            // One in flight per ped. Answers arrive a frame or more later, so
            // the token, the zone it asked about and the ray length are parked
            // on the slot until the harvest above picks them up.
            const int token = WorldProbe::Begin(
                camera, target, localPed);
            ++diagnostics.started;
            if (token < 0)
            {
                ++diagnostics.dropped;
                ++s_failuresSinceAnswer;
                continue;
            }
            slot->probeToken = token;
            slot->probeZone = static_cast<std::int8_t>(zone);
            slot->probeSample = static_cast<std::int8_t>(sample);
            slot->probeDistance = distance;
            slot->probeOrigin = camera;
        }

        if (!s_fallbackActive &&
            s_failuresSinceAnswer >
                CombatBake::g_Policy.visFallbackFailures)
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

                char line[288]{};
                _snprintf_s(
                    line,
                    sizeof(line),
                    _TRUNCATE,
                    "Visibility: probe=%d demand=%d cand=%u tracked=%u "
                    "probed=%u resolved=%u dropped=%u handle=%d status=%d "
                    "everResolved=%d fallback=%d flips=%u/2s switches=%u/2s "
                    "hidden=%u/2s visible=%u/2s",
                    s_probeUsable,
                    demand ? 1 : 0,
                    diagnostics.candidates,
                    diagnostics.tracked,
                    diagnostics.started,
                    diagnostics.resolved,
                    diagnostics.dropped,
                    s_lastHandle,
                    s_lastStatus,
                    diagnostics.everResolved ? 1 : 0,
                    diagnostics.usingSnapshotFallback ? 1 : 0,
                    s_flipWindow,
                    s_switchWindow,
                    s_hiddenWindow,
                    s_visibleWindow);
                hooks::HLog(line);
                s_flipWindow = 0;
                s_switchWindow = 0;
                s_hiddenWindow = 0;
                s_visibleWindow = 0;

                const auto& probe = WorldProbe::GetTelemetry();
                char probeLine[192]{};
                _snprintf_s(
                    probeLine,
                    sizeof(probeLine),
                    _TRUNCATE,
                    "WorldProbe: submitted=%u refused=%u blocked=%u "
                    "clear=%u lost=%u manager=%016llX",
                    probe.submitted,
                    probe.refused,
                    probe.blocked,
                    probe.clear,
                    probe.lost,
                    static_cast<unsigned long long>(probe.manager));
                hooks::HLog(probeLine);
            }
        }
#endif
    }
}
