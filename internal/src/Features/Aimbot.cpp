#include "Aimbot.h"
#include "CombatBake.h"
#include "Damager.h"
#include "Friends.h"
#include "MiscFeatures.h"
#include "NativeInvoke.hpp"
#include "RagePlayerNames.h"
#include "Visibility.h"
#include "VisualPolicy.h"
#include "../SDK/Game.h"
#include "../SDK/Math.h"
#include "../SDK/Offsets.h"
#include "../SDK/RageBuildFlavor.h"
#include "../Menu/Menu.h"
#include "../Main/Hooks.h"
#include "../imgui/imgui.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace Features {

bool g_AimbotEnabled = true;

namespace {

enum class Hitbox : int
{
    Head = 0,
    Neck = 1,
    Chest = 2,
    Pelvis = 3,
    LeftUpperArm = 4,
    LeftForearm = 5,
    LeftHand = 6,
    RightUpperArm = 7,
    RightForearm = 8,
    RightHand = 9,
    LeftThigh = 10,
    LeftCalf = 11,
    LeftFoot = 12,
    RightThigh = 13,
    RightCalf = 14,
    RightFoot = 15,
    Count = 16
};

struct ModeSettings final
{
    bool enabled{};
    bool nearest{};
    bool boneNearestAny{};
    bool visibleCheck{};
    bool wallshot{};
    bool ignoreDead{};
    bool ignoreNpc{};
    bool ignoreAdmins{};
    bool ignoreFriends{};
    int adminThreshold{};
    bool lockOnLast{};
    float fovPixels{};
    // Test the crosshair against the ped's projected silhouette instead of
    // against a pixel radius around a bone centre. Trigger wants this; vector
    // and silent want the radius, because for them the radius is the aim FOV
    // and is supposed to reach past the target.
    bool hitboxTest{};
    bool bones[static_cast<int>(Hitbox::Count)]{};
};

struct TargetCandidate final
{
    const CPed* ped{};
    Vector3 world{};
    Vector2 screen{};
    float crosshairDistanceSq{1e18f};
    std::uint32_t rageHandle{};
    // Carried alongside the entity handle because the damage path talks to the
    // script runtime, where a player is addressed by remote id.
    int remoteId{-1};
    int hitbox{-1};
    bool visible{};
    bool valid{};
};

AimTargetSnapshot g_SilentTarget{};
volatile LONG g_SilentTargetSequence = 0;
AimTargetSnapshot g_DisplayTarget{};
AimTargetSnapshot g_LockedSilentTarget{};
AimTargetSnapshot g_LastVectorTarget{};
AimDiagnostics g_Diagnostics{};

void PublishSilentTarget(
    const AimTargetSnapshot& snapshot) noexcept
{
    InterlockedIncrement(&g_SilentTargetSequence);
    MemoryBarrier();
    g_SilentTarget = snapshot;
    MemoryBarrier();
    InterlockedIncrement(&g_SilentTargetSequence);
}

constexpr int HitboxToBoneSlot(Hitbox hitbox) noexcept
{
    const int slot = static_cast<int>(hitbox);
    if (slot < 0 || slot >= static_cast<int>(Hitbox::Count))
        return -1;
    return slot;
}

bool TryGetHitboxWorld(
    const Game::PedVisualData& visual,
    Hitbox hitbox,
    Vector3& world) noexcept
{
    const int bone = HitboxToBoneSlot(hitbox);
    if (bone < 0)
        return false;
    const std::uint32_t bit = 1u << bone;
    if (!(visual.boneMask & bit))
        return false;
    world = visual.bones[bone];
    return std::isfinite(world.x) &&
        std::isfinite(world.y) &&
        std::isfinite(world.z);
}

bool PedPassesBasicFilters(
    const Game::FramePedEntry& entry,
    const CPed* localPed,
    const ModeSettings& settings) noexcept
{
    if (!entry.valid || !entry.ped ||
        entry.ped == localPed)
        return false;
    if (settings.ignoreDead && entry.dead)
        return false;
    return std::isfinite(entry.livePosition.x) &&
        std::isfinite(entry.livePosition.y) &&
        std::isfinite(entry.livePosition.z);
}

// "Ignore invisible" means ignore peds we know are behind something, not peds
// we know nothing about, so a zone with no fresh probe answer never rejects.
//
// Coarse gate only: it drops a ped whose every measured zone is occluded. Which
// part of a half-covered target may be shot at is decided per hitbox, because a
// torso answer must not authorise a shot at an exposed head and an exposed head
// must not authorise one at a covered torso.
bool RejectedByVisibility(
    const Game::FramePedEntry& entry,
    const ModeSettings& settings) noexcept
{
    if (settings.wallshot || !settings.visibleCheck)
        return false;
    if (!entry.visibleZoneKnown)
        return false;
    return (entry.visibleZoneMask &
        entry.visibleZoneKnown) == 0;
}

// Whether the zone covering this bone has line of sight. Unknown counts as
// visible for the same reason as above.
bool HitboxVisible(
    const Game::FramePedEntry& entry,
    int bone) noexcept
{
    const std::uint8_t zone =
        Visibility::ZoneForBone(bone);
    if (!zone || !(entry.visibleZoneKnown & zone))
        return true;
    return (entry.visibleZoneMask & zone) != 0;
}

bool HitboxRejectedByVisibility(
    const Game::FramePedEntry& entry,
    const ModeSettings& settings,
    int bone) noexcept
{
    if (settings.wallshot || !settings.visibleCheck)
        return false;
    return !HitboxVisible(entry, bone);
}

bool PedRejectedByTypeFilter(
    const Game::FramePedEntry& entry,
    const ModeSettings& settings) noexcept
{
    if (!settings.ignoreNpc &&
        !settings.ignoreAdmins &&
        !settings.ignoreFriends)
        return false;

    bool isPlayer = entry.isPlayer;
    const int adminLevel =
        entry.visual.adminLevel;
    const bool isAnimal = entry.isAnimal;
    if (isAnimal)
        isPlayer = false;

    const auto profile = VisualPolicy::SelectProfile(
        isPlayer,
        adminLevel,
        settings.adminThreshold,
        false,
        false,
        isAnimal);
    if (settings.ignoreNpc &&
        (profile == VisualPolicy::Profile::Npc ||
         profile == VisualPolicy::Profile::Animal))
        return true;
    if (settings.ignoreAdmins &&
        profile == VisualPolicy::Profile::Admin)
        return true;
    if (settings.ignoreFriends) {
        const char* name = nullptr;
        if (entry.visual.name[0])
            name = entry.visual.name;
        if (name && Friends::IsFriend(name))
            return true;
    }
    return false;
}

// One capsule per selectable hitbox, laid over the rig. A capsule whose ends
// are the same bone is a disc around that bone. Radii live in CombatBake and
// are decoded once; this path only reads the baked copies.
using HitboxCapsule = CombatBake::Capsule;
static_assert(
    static_cast<int>(Hitbox::Count) == CombatBake::kHitboxCount);

float PointSegmentDistanceSq(
    const ImVec2& point,
    const ImVec2& a,
    const ImVec2& b) noexcept
{
    const float abx = b.x - a.x;
    const float aby = b.y - a.y;
    const float apx = point.x - a.x;
    const float apy = point.y - a.y;
    const float lengthSq = abx * abx + aby * aby;
    float t = 0.f;
    if (lengthSq > 1e-4f)
    {
        t = (apx * abx + apy * aby) / lengthSq;
        t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    }
    const float dx = apx - abx * t;
    const float dy = apy - aby * t;
    return dx * dx + dy * dy;
}

// Screen pixels per world metre at this ped's depth.
//
// Measured off bone pairs rather than the projection matrix: a pair whose world
// axis runs across the view reports the true scale and a foreshortened one
// reports less, so the largest ratio of the candidates is the estimate. Pairs
// shorter than 20 cm are skipped because their pixel span is mostly noise.
bool ProjectedPixelsPerMetre(
    const Game::FramePedEntry& entry,
    float& pixelsPerMetre) noexcept
{
    CombatBake::Ensure();
    float best = 0.f;
    for (int pairIndex = 0;
         pairIndex < CombatBake::kScalePairCount;
         ++pairIndex)
    {
        const std::uint8_t from =
            CombatBake::g_ScalePairs[pairIndex][0];
        const std::uint8_t to =
            CombatBake::g_ScalePairs[pairIndex][1];
        const std::uint32_t bits =
            (1u << from) | (1u << to);
        if ((entry.projectedBoneMask & bits) != bits ||
            (entry.visual.boneMask & bits) != bits)
            continue;
        const Vector3& wa = entry.visual.bones[from];
        const Vector3& wb = entry.visual.bones[to];
        const float wx = wb.x - wa.x;
        const float wy = wb.y - wa.y;
        const float wz = wb.z - wa.z;
        const float worldSpanSq =
            wx * wx + wy * wy + wz * wz;
        if (worldSpanSq < CombatBake::g_Policy.minBoneSpanMetres * CombatBake::g_Policy.minBoneSpanMetres)
            continue;
        const float sx =
            entry.projectedBones[to].x -
            entry.projectedBones[from].x;
        const float sy =
            entry.projectedBones[to].y -
            entry.projectedBones[from].y;
        const float ratioSq =
            (sx * sx + sy * sy) / worldSpanSq;
        if (ratioSq > best)
            best = ratioSq;
    }
    if (!(best > CombatBake::g_Policy.minPixelsPerMetre * CombatBake::g_Policy.minPixelsPerMetre) ||
        !std::isfinite(best))
    {
        return false;
    }
    pixelsPerMetre = std::sqrt(best);
    return true;
}

// True when the crosshair lands on the projected silhouette of one of the
// enabled hitboxes. Candidates are ranked by how far inside a capsule the
// crosshair sits relative to that capsule's own width, so the ped it is most
// solidly on wins instead of whichever bone centre happens to be nearest.
// Shortest distance between the aim line and a bone segment, in metres.
//
// The screen-space version of this test measures pixels between the crosshair
// and a projected segment, then compares that against a radius converted from
// metres at the target's distance. That conversion is a single number for the
// whole body, so a near arm and a far shoulder are judged at the same scale,
// and the answer drifts from the geometry the shot will actually meet. Done in
// world space there is nothing to convert and nothing to drift.
float RayToSegmentDistanceSq(
    const Vector3& origin,
    const Vector3& direction,
    const Vector3& from,
    const Vector3& to) noexcept
{
    const Vector3 segment(to.x - from.x, to.y - from.y, to.z - from.z);
    const Vector3 offset(
        from.x - origin.x, from.y - origin.y, from.z - origin.z);

    const float segmentLengthSq =
        segment.x * segment.x + segment.y * segment.y +
        segment.z * segment.z;
    const float rayDotSegment =
        direction.x * segment.x + direction.y * segment.y +
        direction.z * segment.z;
    const float rayDotOffset =
        direction.x * offset.x + direction.y * offset.y +
        direction.z * offset.z;
    const float segmentDotOffset =
        segment.x * offset.x + segment.y * offset.y +
        segment.z * offset.z;

    float alongRay = 0.f;
    float alongSegment = 0.f;
    const float denominator = segmentLengthSq - rayDotSegment * rayDotSegment;
    if (segmentLengthSq <= 1e-6f)
    {
        // A point rather than a segment: the head and pelvis capsules are
        // written that way on purpose.
        alongRay = rayDotOffset;
        alongSegment = 0.f;
    }
    else if (denominator > 1e-6f)
    {
        alongSegment =
            (rayDotSegment * rayDotOffset - segmentDotOffset) / denominator;
        if (alongSegment < 0.f)
            alongSegment = 0.f;
        else if (alongSegment > 1.f)
            alongSegment = 1.f;
        alongRay = rayDotOffset + alongSegment * rayDotSegment;
    }
    else
    {
        // Ray parallel to the bone.
        alongSegment = 0.f;
        alongRay = rayDotOffset;
    }
    // Never behind the camera: a bone at the player's back must not count as
    // being under the crosshair.
    if (alongRay < 0.f)
        alongRay = 0.f;

    const Vector3 onRay(
        origin.x + direction.x * alongRay,
        origin.y + direction.y * alongRay,
        origin.z + direction.z * alongRay);
    const Vector3 onSegment(
        from.x + segment.x * alongSegment,
        from.y + segment.y * alongSegment,
        from.z + segment.z * alongSegment);

    const float dx = onRay.x - onSegment.x;
    const float dy = onRay.y - onSegment.y;
    const float dz = onRay.z - onSegment.z;
    return dx * dx + dy * dy + dz * dz;
}

__declspec(noinline) bool EvaluateHitboxCandidate(
    const Game::FramePedEntry& entry,
    const ModeSettings& settings,
    const ImVec2& center,
    TargetCandidate& output) noexcept
{
    // World space when the ray is available, which it is whenever a frame has
    // been drawn. The projected fallback stays for the case where it is not.
    Vector3 rayOrigin{};
    Vector3 rayDirection{};
    const bool haveRay = Game::LastViewRay(rayOrigin, rayDirection);

    float pixelsPerMetre = 0.f;
    if (!haveRay && !ProjectedPixelsPerMetre(entry, pixelsPerMetre))
    {
        ++g_Diagnostics.projectionRejects;
        return false;
    }

    float bestRatioSq = 4.f;
    int bestHitbox = -1;
    int bestAnchor = -1;
    for (int index = 0;
         index < static_cast<int>(Hitbox::Count);
         ++index)
    {
        if (!settings.bones[index])
            continue;
        // A covered zone loses the capsule contest outright. Without this, a
        // target crouched behind a wall with only the head exposed still let
        // the crosshair land on the torso silhouette and the trigger fired into
        // the wall.
        if (HitboxRejectedByVisibility(
                entry, settings, index))
            continue;
        HitboxCapsule capsule = CombatBake::g_Capsules[index];
        // Neck and Chest both hang off Spine. When the engine hands back a rig
        // without it, aiming at the torso used to match nothing at all, so both
        // fall back to a span that only needs Neck and Pelvis.
        const std::uint32_t spineBit =
            1u << static_cast<int>(
                RagePlayerNames::BoneSlot::Spine);
        if ((capsule.from == 2 || capsule.to == 2) &&
            !(entry.projectedBoneMask & spineBit))
        {
            const auto hitbox = static_cast<Hitbox>(index);
            if (hitbox == Hitbox::Chest || hitbox == Hitbox::Neck)
            {
                capsule.from = 1;
                capsule.to = 3;
            }
        }
        const std::uint32_t bits =
            (1u << capsule.from) | (1u << capsule.to);
        if ((entry.projectedBoneMask & bits) != bits)
            continue;

        float ratioSq = 4.f;
        if (haveRay)
        {
            const Vector3& a = entry.visual.bones[capsule.from];
            const Vector3& b = entry.visual.bones[capsule.to];
            const float distSq = RayToSegmentDistanceSq(
                rayOrigin, rayDirection, a, b);
            ratioSq = distSq / (capsule.radiusMetres * capsule.radiusMetres);
        }
        else
        {
            const float radius =
                capsule.radiusMetres * pixelsPerMetre;
            if (radius < CombatBake::g_Policy.minCapsulePixels)
                continue;
            const ImVec2 a(
                entry.projectedBones[capsule.from].x,
                entry.projectedBones[capsule.from].y);
            const ImVec2 b(
                entry.projectedBones[capsule.to].x,
                entry.projectedBones[capsule.to].y);
            const float distSq = PointSegmentDistanceSq(center, a, b);
            ratioSq = distSq / (radius * radius);
        }
        if (ratioSq <= bestRatioSq)
        {
            bestRatioSq = ratioSq;
            bestHitbox = index;
            // Recorded here so a fallback span reports the bone it actually
            // used; the table's own `from` may be the missing one.
            bestAnchor = capsule.from;
        }
    }

    if (bestHitbox < 0 || bestAnchor < 0)
    {
        ++g_Diagnostics.fovRejects;
        return false;
    }

    // The aim point stays the bone itself, not the point on the capsule the
    // crosshair happened to be closest to.
    const int anchor = bestAnchor;
    Vector3 world{};
    if (!TryGetHitboxWorld(
            entry.visual,
            static_cast<Hitbox>(anchor),
            world))
    {
        ++g_Diagnostics.boneRejects;
        return false;
    }

    output.ped = entry.ped;
    output.world = world;
    output.screen = entry.projectedBones[anchor];
    output.crosshairDistanceSq = bestRatioSq;
    output.rageHandle = entry.visual.rageHandle;
    output.remoteId = static_cast<int>(entry.visual.remoteId);
    output.hitbox = bestHitbox;
    output.visible = HitboxVisible(entry, anchor);
    output.valid = true;
    return true;
}

__declspec(noinline) bool EvaluateCandidate(
    const Game::FramePedEntry& entry,
    const ModeSettings& settings,
    Hitbox hitbox,
    float fovPixelsSq,
    const ImVec2& center,
    TargetCandidate& output) noexcept
{
    Vector3 world{};
    if (!TryGetHitboxWorld(
            entry.visual, hitbox, world))
    {
        ++g_Diagnostics.boneRejects;
        return false;
    }
    const int bone = HitboxToBoneSlot(hitbox);
    if (bone < 0 ||
        !(entry.projectedBoneMask &
          (1u << bone)))
    {
        ++g_Diagnostics.projectionRejects;
        return false;
    }
    if (HitboxRejectedByVisibility(
            entry, settings, bone))
    {
        ++g_Diagnostics.visibilityRejects;
        return false;
    }
    const Vector2 screen =
        entry.projectedBones[bone];
    const float dx = screen.x - center.x;
    const float dy = screen.y - center.y;
    const float distanceSq = dx * dx + dy * dy;
    if (distanceSq > fovPixelsSq)
    {
        ++g_Diagnostics.fovRejects;
        return false;
    }

    output.ped = entry.ped;
    output.world = world;
    output.screen = screen;
    output.crosshairDistanceSq = distanceSq;
    output.rageHandle =
        entry.visual.rageHandle;
    output.remoteId =
        static_cast<int>(entry.visual.remoteId);
    output.hitbox = static_cast<int>(hitbox);
    output.visible = HitboxVisible(entry, bone);
    output.valid = true;
    return true;
}

void AppendEnabledHitboxes(
    const ModeSettings& settings,
    Hitbox* out,
    int& count) noexcept
{
    count = 0;
    if (settings.boneNearestAny)
    {
        // Every bone, limbs included. This was restricted to the torso chain
        // because limbs near the crosshair made the aim twitch, but the fix for
        // that is the sticky hitbox below, which refuses to change bone unless
        // the challenger is clearly closer. Excluding them instead threw away
        // the case the setting exists for: a target behind cover with only an
        // arm or a leg exposed had nothing left to aim at.
        for (int index = 0;
             index < static_cast<int>(Hitbox::Count);
             ++index)
            out[count++] = static_cast<Hitbox>(index);
        return;
    }
    for (int i = 0; i < static_cast<int>(Hitbox::Count); ++i)
    {
        if (settings.bones[i])
            out[count++] = static_cast<Hitbox>(i);
    }
}

// Keep previous hitbox unless another is clearly closer (reduces micro-jitter).
bool PreferStickyHitbox(
    const TargetCandidate& sticky,
    const TargetCandidate& challenger) noexcept
{
    if (!sticky.valid)
        return false;
    if (!challenger.valid)
        return true;
    return sticky.crosshairDistanceSq <=
        challenger.crosshairDistanceSq *
            CombatBake::g_Policy.hitboxStick;
}

__declspec(noinline) bool SelectTarget(
    const ModeSettings& settings,
    const Game::FramePedContext& frame,
    const ImVec2& center,
    const AimTargetSnapshot* locked,
    TargetCandidate& best) noexcept
{
    best = {};
    if (!settings.enabled)
    {
        return false;
    }
    if (!settings.hitboxTest && settings.fovPixels <= 0.0f)
    {
        return false;
    }

    Hitbox enabled[static_cast<int>(Hitbox::Count)]{};
    int enabledCount = 0;
    AppendEnabledHitboxes(settings, enabled, enabledCount);
    if (enabledCount <= 0)
    {
        return false;
    }

    const float fovPixelsSq =
        settings.fovPixels * settings.fovPixels;

    if (settings.lockOnLast &&
        locked &&
        locked->valid &&
        locked->ped)
    {
        const Game::FramePedEntry*
            lockedEntry =
                Game::FindFramePed(locked->ped);
        if (lockedEntry &&
            PedPassesBasicFilters(
                *lockedEntry,
                frame.localPed,
                settings))
        {
            if (PedRejectedByTypeFilter(
                    *lockedEntry,
                    settings))
            {
                // fall through to full scan
            }
            else if (!RejectedByVisibility(
                         *lockedEntry, settings))
            {
                const Hitbox preferred =
                    locked->hitbox >= 0 &&
                    locked->hitbox < static_cast<int>(Hitbox::Count)
                    ? static_cast<Hitbox>(locked->hitbox)
                    : Hitbox::Head;
                TargetCandidate lockedCandidate{};
                if (EvaluateCandidate(
                        *lockedEntry,
                        settings,
                        preferred,
                        fovPixelsSq,
                        center,
                        lockedCandidate))
                {
                    best = lockedCandidate;
                    return true;
                }

                for (int index = 0; index < enabledCount; ++index)
                {
                    TargetCandidate candidate{};
                    if (!EvaluateCandidate(
                            *lockedEntry,
                            settings,
                            enabled[index],
                            fovPixelsSq,
                            center,
                            candidate))
                        continue;
                    if (!best.valid ||
                        candidate.crosshairDistanceSq <
                            best.crosshairDistanceSq)
                        best = candidate;
                }
                if (best.valid)
                {
                    return true;
                }
            }
        }
    }

    for (int pedIndex = 0;
         pedIndex < frame.count;
         ++pedIndex)
    {
        const Game::FramePedEntry& entry =
            frame.entries[pedIndex];
        const CPed* ped = entry.ped;
        if (!PedPassesBasicFilters(
                entry,
                frame.localPed,
                settings))
            continue;
        ++g_Diagnostics.evaluatedPeds;

        if (entry.visualValid &&
            entry.visual.isLocal)
            continue;

        if (!entry.coarseVisible)
            continue;
        // The silhouette test has no radius to cull against, and a ped close
        // enough to fill the screen has its torso proxy far from the crosshair
        // while a limb crosses it. Its own capsule loop is the cheap reject.
        if (!settings.hitboxTest)
        {
            const float coarseDx =
                entry.coarseScreen.x - center.x;
            const float coarseDy =
                entry.coarseScreen.y - center.y;
            const float coarseRadius =
                settings.fovPixels +
                CombatBake::g_Policy.coarseFovPad;
            if (coarseDx * coarseDx +
                    coarseDy * coarseDy >
                coarseRadius * coarseRadius)
                continue;
        }

        if (PedRejectedByTypeFilter(
                entry, settings))
            continue;

        if (RejectedByVisibility(entry, settings))
        {
            ++g_Diagnostics.visibilityRejects;
            continue;
        }

        // Filled by BuildFramePedContext from the engine skeleton; empty means
        // this ped lost the frame's bone refresh budget.
        if (!entry.visual.boneMask)
            continue;

        if (settings.hitboxTest)
        {
            TargetCandidate hit{};
            if (EvaluateHitboxCandidate(
                    entry, settings, center, hit) &&
                (!best.valid ||
                 hit.crosshairDistanceSq <
                     best.crosshairDistanceSq))
                best = hit;
            continue;
        }

        TargetCandidate pedBest{};
        TargetCandidate sticky{};
        const bool trySticky =
            settings.nearest &&
            locked &&
            locked->valid &&
            locked->ped == ped &&
            locked->hitbox >= 0 &&
            locked->hitbox < static_cast<int>(Hitbox::Count);
        if (trySticky)
        {
            EvaluateCandidate(
                entry,
                settings,
                static_cast<Hitbox>(locked->hitbox),
                fovPixelsSq,
                center,
                sticky);
        }

        for (int index = 0; index < enabledCount; ++index)
        {
            TargetCandidate candidate{};
            if (!EvaluateCandidate(
                    entry,
                    settings,
                    enabled[index],
                    fovPixelsSq,
                    center,
                    candidate))
                continue;

            if (!settings.nearest)
            {
                pedBest = candidate;
                break;
            }

            if (!pedBest.valid ||
                candidate.crosshairDistanceSq <
                    pedBest.crosshairDistanceSq)
                pedBest = candidate;
        }

        if (!pedBest.valid)
            continue;
        if (PreferStickyHitbox(sticky, pedBest))
            pedBest = sticky;
        if (!best.valid ||
            pedBest.crosshairDistanceSq < best.crosshairDistanceSq)
            best = pedBest;
    }

    const bool selected = best.valid;
    return selected;
}

AimTargetSnapshot ToSnapshot(
    const TargetCandidate& candidate) noexcept
{
    AimTargetSnapshot snapshot{};
    if (!candidate.valid)
        return snapshot;
    snapshot.ped = candidate.ped;
    snapshot.world = candidate.world;
    snapshot.screen = candidate.screen;
    snapshot.rageHandle = candidate.rageHandle;
    snapshot.updatedAt = GetTickCount64();
    snapshot.hitbox = candidate.hitbox;
    snapshot.visible = candidate.visible;
    snapshot.valid = true;
    return snapshot;
}

Vector3 NormalizeLook(Vector3 v) noexcept;

// The game builds its camera basis from this vector and expects unit length.
// Writing the raw sum let the length drift away from 1, which scaled the view
// and made ESP boxes grow or shrink while the aim was engaged.
void WriteLookDirection(Vector3* angles, const Vector3& look) noexcept
{
    if (!angles)
        return;
    const Vector3 unit = NormalizeLook(look);
    if (unit.Length() < 1e-4f)
        return;
    angles->x = unit.x;
    angles->y = unit.y;
    angles->z = unit.z;
}

Vector3 NormalizeLook(Vector3 v) noexcept
{
    const float len = v.Length();
    if (len > 1e-4f)
    {
        v.x /= len;
        v.y /= len;
        v.z /= len;
    }
    return v;
}

// The camera angles we write are only consumed while the game itself is
// aiming, which is why holding the bind alone used to do nothing until the
// right button was held too. Holding the bind now asserts the aim input for as
// long as it is held, and releases only the press we made ourselves.
void UpdateSyntheticAim(bool wanted, int bindKey) noexcept
{
    static bool s_owned = false;

    auto sendRightButton = [](bool down) noexcept {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = down
            ? MOUSEEVENTF_RIGHTDOWN
            : MOUSEEVENTF_RIGHTUP;
        SendInput(1, &input, sizeof(input));
    };

    // Nothing to assert when the bind already is the aim button.
    if (bindKey == VK_RBUTTON)
    {
        if (s_owned)
        {
            sendRightButton(false);
            s_owned = false;
        }
        return;
    }

    if (wanted && !s_owned)
    {
        // Someone already holding the button owns it; do not double-press.
        if ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) == 0)
        {
            sendRightButton(true);
            s_owned = true;
        }
        return;
    }
    if (!wanted && s_owned)
    {
        sendRightButton(false);
        s_owned = false;
    }
}

float ClampFloat(float value, float lo, float hi) noexcept
{
    if (value < lo)
        return lo;
    if (value > hi)
        return hi;
    return value;
}

float RandomUnit() noexcept
{
    // xorshift32 — cheap frame-varying unit float in [0,1).
    static uint32_t s = 0xA341316Cu;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return static_cast<float>(s & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

float RandomSigned() noexcept
{
    return RandomUnit() * 2.0f - 1.0f;
}

bool ReadViewportLook(
    Vector3& outPos,
    Vector3& outLook) noexcept
{
    if (!Offsets::ViewportPtr || !IsValidPtr(Offsets::ViewportPtr))
        return false;

    uint8_t* viewport = nullptr;
    __try {
        viewport = *Offsets::ViewportPtr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!IsValidPtr(viewport))
        return false;

    __try {
        const float* V = reinterpret_cast<const float*>(
            viewport + Offsets::Viewport::ViewMatrix);
        outPos = Vector3(V[12], V[13], V[14]);
        // View forward matches FPS look as -row2 (verified vs CPlayerAngles).
        outLook = NormalizeLook(Vector3(-V[8], -V[9], -V[10]));
        return outPos.Length() > 1e-3f && outLook.Length() > 1e-3f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

__declspec(noinline) bool ApplyVectorAim(
    const TargetCandidate& target) noexcept
{
    if (!target.valid)
    {
        return false;
    }

    uint8_t* cam = Game::GetGameplayCamera();
    if (!cam)
    {
        return false;
    }

    const Menu::AimWeaponProfile& profile = Menu::GetActiveAimProfile();
    if (!profile.vecEnabled)
    {
        return false;
    }

    // Aim from the real rendered camera (viewport), not CPlayerAngles+0x60.
    // Preserve FPS/TPS encoding bias by applying the same delta to both.
    Vector3 lensPos{};
    Vector3 viewLook{};
    if (!ReadViewportLook(lensPos, viewLook))
    {
        return false;
    }

    const float distanceMeters = (target.world - lensPos).Length();
    float smoothness = Menu::EvaluateSmoothness(
        profile,
        distanceMeters,
        std::sqrt(target.crosshairDistanceSq));
    if (!(smoothness >= 1.0f))
        smoothness = 1.0f;
    if (smoothness > 100.0f)
        smoothness = 100.0f;

    // Smart: near crosshair → slower (higher smoothness), far → base speed.
    if (profile.vecSmartSmooth)
    {
        const float distance = std::sqrt(target.crosshairDistanceSq);
        const float smart =
            distance < 8.0f ? 0.35f :
            (distance < 24.0f ? 0.65f : 1.0f);
        smoothness /= smart;
    }

    // Random: sometimes slower / sometimes a bit faster around the base.
    if (profile.vecRandomSmooth)
    {
        const float amount = ClampFloat(profile.vecRandomAmt, 0.0f, 1.0f);
        const float speedFactor = ClampFloat(1.0f - amount * RandomUnit(), 0.15f, 1.0f);
        smoothness /= speedFactor;
    }

    if (profile.humanize)
    {
        static const CPed* s_LastHumanPed = nullptr;
        static ULONGLONG s_AcquiredAt = 0;
        static ULONGLONG s_SwitchAt = 0;
        const ULONGLONG now = GetTickCount64();
        if (target.ped != s_LastHumanPed)
        {
            s_LastHumanPed = target.ped;
            s_AcquiredAt = now;
            s_SwitchAt = now;
        }
        if (profile.reactionDelayMs > 0.f &&
            now - s_AcquiredAt < static_cast<ULONGLONG>(profile.reactionDelayMs))
        {
            return true;
        }
        if (profile.targetSwitchDelayMs > 0.f &&
            now - s_SwitchAt < static_cast<ULONGLONG>(profile.targetSwitchDelayMs) &&
            target.ped == s_LastHumanPed)
        {
            // already same target — switch delay only on change (handled above)
        }
        smoothness *= 1.f + profile.fatigue * 0.35f * RandomUnit();
    }

    smoothness = ClampFloat(smoothness, 1.0f, 250.0f);

    Vector3 aimWorld = target.world;
    if (profile.vecPrediction && target.ped)
    {
        const Vector3 velocity = target.ped->GetVelocity();
        if (std::isfinite(velocity.x) &&
            std::isfinite(velocity.y) &&
            std::isfinite(velocity.z))
        {
            const float predictionScale = ClampFloat(
                profile.vecPredictionScale,
                0.f,
                2.f);
            const float leadTime = ClampFloat(
                0.045f + distanceMeters / 1400.f,
                0.045f,
                0.14f) * predictionScale;
            aimWorld.x += velocity.x * leadTime;
            aimWorld.y += velocity.y * leadTime;
            aimWorld.z += velocity.z * leadTime;
        }
    }
    if (profile.vecCamShake || (profile.humanize && profile.microJitter > 0.f))
    {
        const float amount = ClampFloat(
            profile.vecCamShake ? profile.vecShakeAmt : 0.f, 0.0f, 1.0f);
        const float jitter = ClampFloat(profile.humanize ? profile.microJitter : 0.f, 0.f, 1.f);
        const float shake = amount * 0.04f + jitter * 0.03f;
        aimWorld.x += RandomSigned() * shake;
        aimWorld.y += RandomSigned() * shake;
        aimWorld.z += RandomSigned() * shake;
    }

    if (profile.humanize && profile.overshoot > 0.f)
    {
        const Vector3 toTarget = NormalizeLook(aimWorld - lensPos);
        const float over = profile.overshoot * 0.15f * RandomUnit();
        aimWorld.x += toTarget.x * over;
        aimWorld.y += toTarget.y * over;
        aimWorld.z += toTarget.z * over;
    }

    const Vector3 deltaPos = aimWorld - lensPos;
    const float dist = deltaPos.Length();
    if (dist < 1e-4f)
    {
        return true;
    }

    const Vector3 desired = NormalizeLook(Vector3(
        deltaPos.x / dist,
        deltaPos.y / dist,
        deltaPos.z / dist));

    // The step is a fraction of the remaining angle, so the divisor must never
    // drop below 1: at smoothness 1 the old /1.5 made the step 1.5x the delta,
    // which flew past the target and threw the aim off to the side.
    float deltaFormula = smoothness / 1.5f;
    if (deltaFormula < 1.0f)
        deltaFormula = 1.0f;
    Vector3 step(
        (desired.x - viewLook.x) / deltaFormula,
        (desired.y - viewLook.y) / deltaFormula,
        (desired.z - viewLook.z) / deltaFormula);

    if (profile.vecCamShake)
    {
        const float amount = ClampFloat(profile.vecShakeAmt, 0.0f, 1.0f);
        // Tiny look-vector noise so camera visibly jitters even at high smooth.
        const float lookNoise = amount * 0.0015f;
        step.x += RandomSigned() * lookNoise;
        step.y += RandomSigned() * lookNoise;
        step.z += RandomSigned() * lookNoise;
    }

    bool ok = false;
    __try {
        auto* fps = reinterpret_cast<Vector3*>(
            cam + Offsets::Camera::FpsAngles);
        auto* tps = reinterpret_cast<Vector3*>(
            cam + Offsets::Camera::TpsAngles);

#if RAGEMP_DEV_BUILD
        // The boxes shrink only while this runs, so the suspicion is that one
        // of these two fields is not a unit look vector and our write skews
        // the camera basis, which would rescale every projection. Log the
        // lengths on both sides of the write plus the matrix the projection
        // actually uses, twice a second.
        const Vector3 tpsBefore = *tps;
        const Vector3 fpsBefore = *fps;
#endif

        WriteLookDirection(
            tps,
            Vector3(tps->x + step.x, tps->y + step.y, tps->z + step.z));
        WriteLookDirection(
            fps,
            Vector3(fps->x + step.x, fps->y + step.y, fps->z + step.z));

#if RAGEMP_DEV_BUILD
        {
            static ULONGLONG s_LastBasisLog = 0;
            const ULONGLONG nowBasis = GetTickCount64();
            if (nowBasis - s_LastBasisLog > 500)
            {
                s_LastBasisLog = nowBasis;
                float rowRight = 0.f;
                float rowUp = 0.f;
                float rowBack = 0.f;
                float projX = 0.f;
                float projY = 0.f;
                Game::CameraBasisDiagnostics(
                    rowRight,
                    rowUp,
                    rowBack,
                    projX,
                    projY);
                char basis[256]{};
                sprintf_s(
                    basis,
                    "VecBasis: tps %.3f->%.3f fps %.3f->%.3f "
                    "view |r|=%.4f |u|=%.4f |b|=%.4f "
                    "proj %.4f/%.4f",
                    tpsBefore.Length(),
                    tps->Length(),
                    fpsBefore.Length(),
                    fps->Length(),
                    rowRight,
                    rowUp,
                    rowBack,
                    projX,
                    projY);
                hooks::HLog(basis);
            }
        }
#endif

        ok = true;

        static ULONGLONG s_LastMs = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - s_LastMs > 5000)
        {
            s_LastMs = now;
            char buf[220];
            sprintf_s(
                buf,
                "VectorAim: sm=%.0f desZ=%.3f viewZ=%.3f stepZ=%.4f tpsZ=%.3f",
                smoothness,
                desired.z,
                viewLook.z,
                step.z,
                tps->z);
            hooks::HLog(buf);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

} // namespace

bool CopySilentTarget(AimTargetSnapshot& output) noexcept
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const LONG before = InterlockedCompareExchange(
            &g_SilentTargetSequence, 0, 0);
        if (before & 1)
            continue;
        MemoryBarrier();
        const AimTargetSnapshot snapshot = g_SilentTarget;
        MemoryBarrier();
        const LONG after = InterlockedCompareExchange(
            &g_SilentTargetSequence, 0, 0);
        if (before != after || (after & 1))
            continue;
        output = snapshot;
        if (!output.valid)
            output = {};
        return output.valid;
    }
    output = {};
    return false;
}

bool CopyDisplayTarget(AimTargetSnapshot& output) noexcept
{
    if (!g_DisplayTarget.valid)
    {
        output = {};
        return false;
    }
    output = g_DisplayTarget;
    return true;
}

AimDiagnostics GetAimDiagnostics() noexcept
{
    return g_Diagnostics;
}

__declspec(noinline) void RunAimbot()
{
    CombatBake::Ensure();
    g_Diagnostics = {};
    // The published target is not blanked here. The bullet hook reads it from
    // the game's own thread, so clearing it up front left a window on every
    // frame, between here and the silent block below, where a shot fired in that
    // window found no target and was never redirected. Each path below publishes
    // exactly once instead.
    g_DisplayTarget = {};

    const Menu::AimWeaponProfile& profile = Menu::GetActiveAimProfile();
    g_Diagnostics.vectorEnabled = profile.vecEnabled;
    const bool vectorKeyHeld =
        profile.vecKey <= 0 ||
        (GetAsyncKeyState(profile.vecKey) & 0x8000) != 0;
    const bool silentKeyHeld =
        profile.silentKey <= 0 ||
        (GetAsyncKeyState(profile.silentKey) & 0x8000) != 0;
    g_Diagnostics.vectorKeyHeld =
        profile.vecEnabled && vectorKeyHeld;

#if RAGEMP_DEV_BUILD
    // Unconditional: a silent diagnostic told us nothing last time because it
    // was gated on the very flag under suspicion.
    {
        static ULONGLONG s_LastAimStateLog = 0;
        const ULONGLONG nowState = GetTickCount64();
        if (nowState - s_LastAimStateLog > 500)
        {
            s_LastAimStateLog = nowState;
            char state[160]{};
            sprintf_s(
                state,
                "AimState: vecEn=%d vecKey=%d held=%d aimbotEn=%d",
                profile.vecEnabled ? 1 : 0,
                profile.vecKey,
                vectorKeyHeld ? 1 : 0,
                g_AimbotEnabled ? 1 : 0);
            hooks::HLog(state);
        }
    }
#endif
    g_Diagnostics.silentKeyHeld =
        profile.silentEnabled && silentKeyHeld;
    g_Diagnostics.triggerEnabled =
        profile.triggerEnabled;

    UpdateSyntheticAim(
        g_AimbotEnabled &&
            profile.vecEnabled &&
            vectorKeyHeld,
        profile.vecKey);

    if (!g_AimbotEnabled)
    {
        return;
    }
    // The damager reads the same frame the aim modes do, so it has to keep the
    // pass alive on its own even when none of them is on.
    const bool damagerArmed = Damager::Armed();
    if (!damagerArmed)
        Damager::PublishTarget({});
    if (!profile.silentEnabled &&
        !profile.vecEnabled &&
        !profile.triggerEnabled &&
        !damagerArmed)
    {
        return;
    }

    const Game::FramePedContext& frame =
        Game::GetFramePedContext();
    auto* localPed = frame.localPed;
    if (!localPed)
    {
        return;
    }
    g_Diagnostics.activePeds = frame.count;

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 center(display.x * 0.5f, display.y * 0.5f);

    TargetCandidate vectorTarget{};
    TargetCandidate silentTarget{};
    TargetCandidate triggerTarget{};

    if (profile.vecEnabled && vectorKeyHeld)
    {
        ModeSettings vectorSettings{};
        vectorSettings.enabled = true;
        vectorSettings.nearest =
            profile.vecNearest || profile.vecBoneNearestAny;
        vectorSettings.boneNearestAny = profile.vecBoneNearestAny;
        vectorSettings.visibleCheck = profile.vecVisible;
        vectorSettings.ignoreDead = profile.ignoreDead;
        vectorSettings.ignoreNpc = profile.ignoreNpc;
        vectorSettings.ignoreAdmins = profile.ignoreAdmins;
        vectorSettings.ignoreFriends =
            profile.ignoreFriends || Menu::s_Aim_IgnoreFriends;
        vectorSettings.adminThreshold = Menu::s_AdminLevelThreshold;
        vectorSettings.lockOnLast = false;
        const float approxDist = 40.f; // refined per-candidate below via dynamic FOV base
        vectorSettings.fovPixels = Menu::EvaluateDynamicFov(
            profile, approxDist, false, 0.f);
        for (int i = 0; i < Menu::kAimHitboxCount; ++i)
            vectorSettings.bones[i] = profile.vecBones[i];
        // Previous vector aim point — stickiness only, not ped lock.
        const AimTargetSnapshot* vectorSticky =
            profile.vecSticky &&
                g_LastVectorTarget.valid &&
                g_LastVectorTarget.ped
            ? &g_LastVectorTarget
            : nullptr;
        SelectTarget(
            vectorSettings,
            frame,
            center,
            vectorSticky,
            vectorTarget);
        if (vectorTarget.valid)
        {
            g_Diagnostics.vectorWritten =
                ApplyVectorAim(vectorTarget);
            g_LastVectorTarget = ToSnapshot(vectorTarget);
        }
        else
        {
            g_LastVectorTarget = {};
        }
        g_Diagnostics.vectorTargetValid =
            vectorTarget.valid;
    }
    else
    {
        g_LastVectorTarget = {};
    }

    if (profile.silentEnabled && silentKeyHeld)
    {
        ModeSettings silentSettings{};
        silentSettings.enabled = true;
        silentSettings.nearest =
            profile.silentNearest || profile.silentBoneNearestAny;
        silentSettings.boneNearestAny = profile.silentBoneNearestAny;
        silentSettings.visibleCheck = profile.silentVisible;
        silentSettings.wallshot = profile.silentWallshot;
        silentSettings.ignoreDead = profile.ignoreDead;
        silentSettings.ignoreNpc = profile.ignoreNpc;
        silentSettings.ignoreAdmins = profile.ignoreAdmins;
        silentSettings.ignoreFriends =
            profile.ignoreFriends || Menu::s_Aim_IgnoreFriends;
        silentSettings.adminThreshold = Menu::s_AdminLevelThreshold;
        silentSettings.lockOnLast = profile.silentLock;
        silentSettings.fovPixels = profile.silentFov;
        for (int i = 0; i < Menu::kAimHitboxCount; ++i)
            silentSettings.bones[i] = profile.silentBones[i];
        SelectTarget(
            silentSettings,
            frame,
            center,
            profile.silentLock ? &g_LockedSilentTarget : nullptr,
            silentTarget);
        if (silentTarget.valid)
        {
            // Assembled locally and handed over in one go. Writing the shared
            // copy field by field left the sequence counter even while it was
            // half updated, so the hook could accept a mix of two frames.
            AimTargetSnapshot snapshot =
                ToSnapshot(silentTarget);
            snapshot.hitchanceEnabled = true; // hitchance is always on
            // Distance from the local player, for the hitchance curve. This was
            // derived from crosshairDistanceSq, which is the crosshair offset in
            // pixels and not a distance at all, so a distance-based curve was
            // being sampled with a number that had nothing to do with range.
            float distMeters =
                CombatBake::g_Policy.hitchanceFallbackMetres;
            if (frame.localPositionValid)
            {
                const float dx =
                    silentTarget.world.x - frame.localPosition.x;
                const float dy =
                    silentTarget.world.y - frame.localPosition.y;
                const float dz =
                    silentTarget.world.z - frame.localPosition.z;
                distMeters = std::sqrt(
                    dx * dx + dy * dy + dz * dz);
            }
            snapshot.hitchance =
                Menu::EvaluateSilentHitchance(profile, distMeters);

            // Silent suite: always-on velocity track + miss bias + reaction gate.
            // Ballistic lead (distance / bullet speed) is applied in SilentAim hook.
            static const CPed* s_SilentPed = nullptr;
            static int s_SilentHitbox = -1;
            static ULONGLONG s_SilentAcquired = 0;
            static Vector3 s_PrevWorld{};
            static ULONGLONG s_PrevAt = 0;
            static Vector3 s_SmoothVel{};
            const ULONGLONG now = GetTickCount64();
            if (silentTarget.ped != s_SilentPed)
            {
                s_SilentPed = silentTarget.ped;
                s_SilentHitbox = silentTarget.hitbox;
                s_SilentAcquired = now;
                s_PrevWorld = silentTarget.world;
                s_PrevAt = now;
                s_SmoothVel = silentTarget.ped
                    ? silentTarget.ped->GetVelocity()
                    : Vector3{};
            }
            else if (silentTarget.hitbox != s_SilentHitbox)
            {
                // Same ped, different bone. The gap between two bones is not
                // motion: head to pelvis inside one frame reads as tens of
                // metres per second, and the blend below carried that into the
                // lead for several frames afterwards. Rebase the history and
                // skip the sample. The reaction timer is deliberately left
                // alone, since the target itself did not change.
                s_SilentHitbox = silentTarget.hitbox;
                s_PrevWorld = silentTarget.world;
                s_PrevAt = now;
            }

            // Blend entity velocity with bone-history velocity (covers vehicles).
            if (s_PrevAt != 0 && now > s_PrevAt)
            {
                const float dt =
                    static_cast<float>(now - s_PrevAt) / 1000.f;
                if (dt > 0.001f &&
                    dt < CombatBake::g_Policy.velocitySampleMaxDt)
                {
                    Vector3 boneVel(
                        (silentTarget.world.x - s_PrevWorld.x) / dt,
                        (silentTarget.world.y - s_PrevWorld.y) / dt,
                        (silentTarget.world.z - s_PrevWorld.z) / dt);
                    Vector3 entityVel = silentTarget.ped
                        ? silentTarget.ped->GetVelocity()
                        : Vector3{};
                    const float boneSpeed = boneVel.Length();
                    const float entitySpeed = entityVel.Length();
                    // Prefer the stronger signal; bone wins in vehicles.
                    Vector3 sample =
                        boneSpeed >= entitySpeed *
                            CombatBake::g_Policy.boneVelPrefer
                        ? boneVel
                        : entityVel;
                    const float sampleSpeed = sample.Length();
                    if (sampleSpeed > CombatBake::g_Policy.maxTrackedSpeed)
                        sample = sample * (
                            CombatBake::g_Policy.maxTrackedSpeed /
                            sampleSpeed);
                    const float alpha =
                        CombatBake::g_Policy.velocityBlend;
                    s_SmoothVel = Vector3(
                        s_SmoothVel.x * (1.f - alpha) + sample.x * alpha,
                        s_SmoothVel.y * (1.f - alpha) + sample.y * alpha,
                        s_SmoothVel.z * (1.f - alpha) + sample.z * alpha);
                }
            }
            s_PrevWorld = silentTarget.world;
            s_PrevAt = now;
            snapshot.velocity = s_SmoothVel;

            if (profile.silentReactionMs > 0.f &&
                now - s_SilentAcquired <
                    static_cast<ULONGLONG>(profile.silentReactionMs))
            {
                // Hold previous / skip until reaction elapses.
                snapshot.valid = false;
            }
            else
            {
                Vector3 aim = silentTarget.world;
                if (profile.silentMissBias > 0.f)
                {
                    const float bias =
                        profile.silentMissBias *
                        CombatBake::g_Policy.missBiasScale;
                    aim.x += (RandomUnit() * 2.f - 1.f) * bias;
                    aim.y += (RandomUnit() * 2.f - 1.f) * bias;
                    aim.z += (RandomUnit() * 2.f - 1.f) * bias * 0.5f;
                }
                snapshot.world = aim;
            }

            PublishSilentTarget(snapshot);
            if (profile.silentLock && snapshot.valid)
                g_LockedSilentTarget = snapshot;
        }
        else
        {
            PublishSilentTarget({});
            if (profile.silentLock)
                g_LockedSilentTarget = {};
        }
    }
    else
    {
        PublishSilentTarget({});
        g_LockedSilentTarget = {};
    }
    g_Diagnostics.silentTargetValid =
        g_SilentTarget.valid;

    static const CPed* s_TriggerCandidate = nullptr;
    static const Menu::AimWeaponProfile* s_TriggerProfile = nullptr;
    static int s_TriggerHitbox = -1;
    static ULONGLONG s_TriggerAcquiredAt = 0;
    static ULONGLONG s_TriggerLastShotAt = 0;
    static ULONGLONG s_TriggerLastTickAt = 0;

    const bool triggerKeyHeld =
        profile.triggerKey <= 0 ||
        (GetAsyncKeyState(profile.triggerKey) & 0x8000) != 0;
    g_Diagnostics.triggerKeyHeld =
        profile.triggerEnabled && triggerKeyHeld;

    if (profile.triggerEnabled && triggerKeyHeld)
    {
        ModeSettings triggerSettings{};
        triggerSettings.enabled = true;
        // Fire when the crosshair is actually on an enabled body part. There is
        // no radius to tune: the capsules are sized in metres and scaled per
        // ped, so the test means the same thing at 3 m and at 300 m.
        triggerSettings.hitboxTest = true;
        triggerSettings.visibleCheck =
            profile.triggerVisible;
        triggerSettings.ignoreDead = profile.ignoreDead;
        triggerSettings.ignoreNpc = profile.ignoreNpc;
        triggerSettings.ignoreAdmins = profile.ignoreAdmins;
        triggerSettings.ignoreFriends =
            profile.ignoreFriends || Menu::s_Aim_IgnoreFriends;
        triggerSettings.adminThreshold =
            Menu::s_AdminLevelThreshold;
        triggerSettings.lockOnLast = false;
        for (int i = 0; i < Menu::kAimHitboxCount; ++i)
            triggerSettings.bones[i] =
                profile.triggerBones[i];

        const ULONGLONG now = GetTickCount64();
        if (s_TriggerProfile != &profile ||
            s_TriggerLastTickAt == 0 ||
            now < s_TriggerLastTickAt ||
            now - s_TriggerLastTickAt > 250)
        {
            s_TriggerProfile = &profile;
            s_TriggerCandidate = nullptr;
            s_TriggerHitbox = -1;
            s_TriggerAcquiredAt = now;
        }
        s_TriggerLastTickAt = now;

        SelectTarget(
            triggerSettings,
            frame,
            center,
            nullptr,
            triggerTarget);
        g_Diagnostics.triggerTargetValid =
            triggerTarget.valid;
        if (triggerTarget.valid)
        {
            // Only a change of ped restarts the reaction delay. Restarting it
            // when the closest bone changed meant that sweeping across a body,
            // where head and chest trade places every frame, kept resetting the
            // dwell timer and the trigger never fired at all.
            if (triggerTarget.ped != s_TriggerCandidate)
            {
                s_TriggerCandidate = triggerTarget.ped;
                s_TriggerAcquiredAt = now;
            }
            s_TriggerHitbox = triggerTarget.hitbox;
            const ULONGLONG delay = static_cast<ULONGLONG>(
                ClampFloat(profile.triggerDelayMs, 0.f, 250.f));
            const ULONGLONG cooldown = static_cast<ULONGLONG>(
                ClampFloat(profile.triggerCooldownMs, 20.f, 500.f));
            const bool dwellReady =
                now >= s_TriggerAcquiredAt &&
                now - s_TriggerAcquiredAt >= delay;
            const bool cooldownReady =
                s_TriggerLastShotAt == 0 ||
                (now >= s_TriggerLastShotAt &&
                 now - s_TriggerLastShotAt >= cooldown);
            if (dwellReady && cooldownReady)
            {
                // The shot goes through the RAGE script thread. RunAimbot runs
                // in the D3D Present hook, and a control value submitted from
                // there is never consumed by the game — which is why calling
                // SET_CONTROL_VALUE_NEXT_FRAME straight from here fired
                // nothing at all.
                bool fired =
                    MiscFeatures::RequestAttackBurst(60);
                if (!fired)
                {
                    // Only reachable before V8 comes up. Kept so the feature
                    // degrades instead of going silent.
                    fired = NativeInvoke::SetControlValueNextFrame(
                        0,
                        24,
                        1.f);
                }
                if (fired)
                {
                    s_TriggerLastShotAt = now;
                    g_Diagnostics.triggerFired = true;
                }
            }
        }
        else
        {
            s_TriggerCandidate = nullptr;
            s_TriggerHitbox = -1;
            s_TriggerAcquiredAt = now;
        }
    }
    else
    {
        s_TriggerProfile = nullptr;
        s_TriggerCandidate = nullptr;
        s_TriggerHitbox = -1;
        s_TriggerAcquiredAt = 0;
        s_TriggerLastTickAt = 0;
    }

    if (damagerArmed)
    {
        const Damager::Settings& damager = Damager::Access();
        ModeSettings damagerSettings{};
        damagerSettings.enabled = true;
        // Nearest to the crosshair, never sticky: the damage is dealt to
        // whoever is under the reticle right now, and holding a ped that has
        // walked out of the cone would keep killing it off screen.
        damagerSettings.nearest = true;
        damagerSettings.visibleCheck = damager.visibleCheck;
        damagerSettings.ignoreDead = profile.ignoreDead;
        damagerSettings.ignoreNpc = profile.ignoreNpc;
        damagerSettings.ignoreAdmins = profile.ignoreAdmins;
        damagerSettings.ignoreFriends =
            profile.ignoreFriends || Menu::s_Aim_IgnoreFriends;
        damagerSettings.adminThreshold = Menu::s_AdminLevelThreshold;
        damagerSettings.lockOnLast = false;
        damagerSettings.fovPixels = damager.fov;
        for (int index = 0;
             index < Menu::kAimHitboxCount &&
                 index < Damager::kBoneCount;
             ++index)
            damagerSettings.bones[index] = damager.bones[index];

        TargetCandidate damagerTarget{};
        SelectTarget(
            damagerSettings,
            frame,
            center,
            nullptr,
            damagerTarget);

        Damager::Target published{};
        if (damagerTarget.valid && frame.localPositionValid)
        {
            const float dx =
                damagerTarget.world.x - frame.localPosition.x;
            const float dy =
                damagerTarget.world.y - frame.localPosition.y;
            const float dz =
                damagerTarget.world.z - frame.localPosition.z;
            const float distance =
                std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance <= damager.maxDistance)
            {
                // The bullet starts half a metre in front of the bone, on our
                // side of it, so the only thing the segment can cross is the
                // victim: from further away a wall, a car door or their own
                // vehicle would take the hit instead.
                const float kOriginLead =
                    CombatBake::g_Policy.damagerOriginLead;
                float ox = damagerTarget.world.x;
                float oy = damagerTarget.world.y;
                float oz = damagerTarget.world.z + kOriginLead;
                if (distance > 0.01f)
                {
                    ox -= dx / distance * kOriginLead;
                    oy -= dy / distance * kOriginLead;
                    oz = damagerTarget.world.z -
                        dz / distance * kOriginLead;
                }
                // A remote id only means anything for a real player; on an NPC
                // it would address somebody else entirely, so the health probe
                // is left out rather than pointed at a stranger. The hits
                // themselves are aimed by coordinate and do not need it.
                const Game::FramePedEntry* entry =
                    Game::FindFramePed(damagerTarget.ped);
                const bool isPlayer =
                    entry && entry->visual.isPlayer;
                published.valid = true;
                published.handle = damagerTarget.rageHandle;
                published.remoteId =
                    isPlayer ? damagerTarget.remoteId : -1;
                published.aimX = damagerTarget.world.x;
                published.aimY = damagerTarget.world.y;
                published.aimZ = damagerTarget.world.z;
                published.originX = ox;
                published.originY = oy;
                published.originZ = oz;
                published.distance = distance;
            }
        }
        Damager::PublishTarget(published);
    }

    if (profile.silentMark && g_SilentTarget.valid)
        g_DisplayTarget = g_SilentTarget;
    else if (vectorTarget.valid)
        g_DisplayTarget = ToSnapshot(vectorTarget);
}

} // namespace Features
