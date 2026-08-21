#include "Aimbot.h"
#include "Friends.h"
#include "NativeInvoke.hpp"
#include "RagePlayerNames.h"
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
    bool bones[static_cast<int>(Hitbox::Count)]{};
};

struct TargetCandidate final
{
    const CPed* ped{};
    Vector3 world{};
    Vector2 screen{};
    float crosshairDistanceSq{1e18f};
    std::uint32_t rageHandle{};
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

bool EvaluateCandidate(
    const Game::FramePedEntry& entry,
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
    output.hitbox = static_cast<int>(hitbox);
    output.visible =
        entry.visualValid &&
        entry.visual.visible;
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
        // Core torso chain only — limbs flicker near the crosshair and
        // look like camera shake even with shake disabled.
        static constexpr Hitbox kCore[] = {
            Hitbox::Head,
            Hitbox::Neck,
            Hitbox::Chest,
            Hitbox::Pelvis,
        };
        for (const Hitbox hitbox : kCore)
            out[count++] = hitbox;
        return;
    }
    for (int i = 0; i < static_cast<int>(Hitbox::Count); ++i)
    {
        if (settings.bones[i])
            out[count++] = static_cast<Hitbox>(i);
    }
}

// Keep previous hitbox unless another is clearly closer (reduces micro-jitter).
constexpr float kHitboxStickFactor = 1.35f;

bool PreferStickyHitbox(
    const TargetCandidate& sticky,
    const TargetCandidate& challenger) noexcept
{
    if (!sticky.valid)
        return false;
    if (!challenger.valid)
        return true;
    return sticky.crosshairDistanceSq <=
        challenger.crosshairDistanceSq * kHitboxStickFactor;
}

bool SelectTarget(
    const ModeSettings& settings,
    const Game::FramePedContext& frame,
    const ImVec2& center,
    const AimTargetSnapshot* locked,
    TargetCandidate& best) noexcept
{
    best = {};
    if (!settings.enabled || settings.fovPixels <= 0.0f)
        return false;

    Hitbox enabled[static_cast<int>(Hitbox::Count)]{};
    int enabledCount = 0;
    AppendEnabledHitboxes(settings, enabled, enabledCount);
    if (enabledCount <= 0)
        return false;

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
            else if (settings.wallshot ||
                !(settings.visibleCheck &&
                  VisualPolicy::UseHiddenPalette(
                      true,
                      lockedEntry->visualValid,
                      lockedEntry->visualValid &&
                          lockedEntry->visual.visible)))
            {
                const Hitbox preferred =
                    locked->hitbox >= 0 &&
                    locked->hitbox < static_cast<int>(Hitbox::Count)
                    ? static_cast<Hitbox>(locked->hitbox)
                    : Hitbox::Head;
                TargetCandidate lockedCandidate{};
                if (EvaluateCandidate(
                        *lockedEntry,
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
                    return true;
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

        if (entry.coarseVisible)
        {
            const float coarseDx =
                entry.coarseScreen.x - center.x;
            const float coarseDy =
                entry.coarseScreen.y - center.y;
            const float coarseRadius =
                settings.fovPixels + 260.f;
            if (coarseDx * coarseDx +
                    coarseDy * coarseDy >
                coarseRadius * coarseRadius)
                continue;
        }
        else
            continue;

        if (PedRejectedByTypeFilter(
                entry, settings))
            continue;

        if (!settings.wallshot &&
            settings.visibleCheck &&
            VisualPolicy::UseHiddenPalette(
                true,
                entry.visualValid,
                entry.visualValid &&
                    entry.visual.visible))
        {
            ++g_Diagnostics.visibilityRejects;
            continue;
        }

        // Bones from TryGetPedVisual (memory). Engine GetBone is quarantined.
        if (!entry.visual.boneMask)
            continue;

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

    return best.valid;
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

bool ApplyVectorAim(const TargetCandidate& target) noexcept
{
    if (!target.valid)
        return false;

    uint8_t* cam = Game::GetGameplayCamera();
    if (!cam)
        return false;

    const Menu::AimWeaponProfile& profile = Menu::GetActiveAimProfile();
    if (!profile.vecEnabled)
        return false;

    // Aim from the real rendered camera (viewport), not CPlayerAngles+0x60.
    // Preserve FPS/TPS encoding bias by applying the same delta to both.
    Vector3 lensPos{};
    Vector3 viewLook{};
    if (!ReadViewportLook(lensPos, viewLook))
        return false;

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
            return true;
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
        return true;

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

void RunAimbot()
{
    g_Diagnostics = {};
    PublishSilentTarget({});
    g_DisplayTarget = {};

    const Menu::AimWeaponProfile& profile = Menu::GetActiveAimProfile();
    g_Diagnostics.vectorEnabled = profile.vecEnabled;
    const bool vectorKeyHeld =
        profile.vecKey <= 0 ||
        (GetAsyncKeyState(profile.vecKey) & 0x8000) != 0;
    const bool silentKeyHeld =
        profile.silentKey <= 0 ||
        (GetAsyncKeyState(profile.silentKey) & 0x8000) != 0;
    const bool triggerKeyHeld =
        profile.triggerKey <= 0 ||
        (GetAsyncKeyState(profile.triggerKey) & 0x8000) != 0;
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
    g_Diagnostics.triggerKeyHeld =
        profile.triggerEnabled && triggerKeyHeld;

    UpdateSyntheticAim(
        g_AimbotEnabled &&
            profile.vecEnabled &&
            vectorKeyHeld,
        profile.vecKey);

    if (!g_AimbotEnabled)
        return;
    if (!profile.silentEnabled &&
        !profile.vecEnabled &&
        !profile.triggerEnabled)
        return;

    const Game::FramePedContext& frame =
        Game::GetFramePedContext();
    auto* localPed = frame.localPed;
    if (!localPed)
        return;
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
            g_SilentTarget = ToSnapshot(silentTarget);
            g_SilentTarget.hitchanceEnabled =
                profile.silentHitchance;
            // Approximate distance from local player for hitchance curve.
            float distMeters = 40.f;
            if (localPed)
            {
                // world is already bone pos; use crosshair proxy via FOV radius scale
                distMeters = std::sqrt(
                    (std::max)(0.f, silentTarget.crosshairDistanceSq)) * 0.35f + 5.f;
            }
            g_SilentTarget.hitchance =
                Menu::EvaluateSilentHitchance(profile, distMeters);

            // Silent suite: always-on velocity track + miss bias + reaction gate.
            // Ballistic lead (distance / bullet speed) is applied in SilentAim hook.
            static const CPed* s_SilentPed = nullptr;
            static ULONGLONG s_SilentAcquired = 0;
            static Vector3 s_PrevWorld{};
            static ULONGLONG s_PrevAt = 0;
            static Vector3 s_SmoothVel{};
            const ULONGLONG now = GetTickCount64();
            if (silentTarget.ped != s_SilentPed)
            {
                s_SilentPed = silentTarget.ped;
                s_SilentAcquired = now;
                s_PrevWorld = silentTarget.world;
                s_PrevAt = now;
                s_SmoothVel = silentTarget.ped
                    ? silentTarget.ped->GetVelocity()
                    : Vector3{};
            }

            // Blend entity velocity with bone-history velocity (covers vehicles).
            if (s_PrevAt != 0 && now > s_PrevAt)
            {
                const float dt =
                    static_cast<float>(now - s_PrevAt) / 1000.f;
                if (dt > 0.001f && dt < 0.35f)
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
                    Vector3 sample = boneSpeed >= entitySpeed * 0.85f
                        ? boneVel
                        : entityVel;
                    const float sampleSpeed = sample.Length();
                    if (sampleSpeed > 120.f)
                        sample = sample * (120.f / sampleSpeed);
                    constexpr float kAlpha = 0.42f;
                    s_SmoothVel = Vector3(
                        s_SmoothVel.x * (1.f - kAlpha) + sample.x * kAlpha,
                        s_SmoothVel.y * (1.f - kAlpha) + sample.y * kAlpha,
                        s_SmoothVel.z * (1.f - kAlpha) + sample.z * kAlpha);
                }
            }
            s_PrevWorld = silentTarget.world;
            s_PrevAt = now;
            g_SilentTarget.velocity = s_SmoothVel;

            if (profile.silentReactionMs > 0.f &&
                now - s_SilentAcquired <
                    static_cast<ULONGLONG>(profile.silentReactionMs))
            {
                // Hold previous / skip until reaction elapses.
                g_SilentTarget.valid = false;
            }
            else
            {
                Vector3 aim = silentTarget.world;
                if (profile.silentMissBias > 0.f)
                {
                    const float bias = profile.silentMissBias * 0.08f;
                    aim.x += (RandomUnit() * 2.f - 1.f) * bias;
                    aim.y += (RandomUnit() * 2.f - 1.f) * bias;
                    aim.z += (RandomUnit() * 2.f - 1.f) * bias * 0.5f;
                }
                g_SilentTarget.world = aim;
            }

            if (g_SilentTarget.valid)
                PublishSilentTarget(g_SilentTarget);
            if (profile.silentLock && g_SilentTarget.valid)
                g_LockedSilentTarget = g_SilentTarget;
        }
        else if (profile.silentLock)
        {
            g_LockedSilentTarget = {};
        }
    }
    else
    {
        g_LockedSilentTarget = {};
    }
    g_Diagnostics.silentTargetValid =
        g_SilentTarget.valid;

    static AimTargetSnapshot s_TriggerLocked{};
    static const CPed* s_TriggerCandidate = nullptr;
    static const Menu::AimWeaponProfile* s_TriggerProfile = nullptr;
    static int s_TriggerHitbox = -1;
    static ULONGLONG s_TriggerAcquiredAt = 0;
    static ULONGLONG s_TriggerLastShotAt = 0;
    static ULONGLONG s_TriggerLastTickAt = 0;

    if (profile.triggerEnabled && triggerKeyHeld)
    {
        ModeSettings triggerSettings{};
        triggerSettings.enabled = true;
        triggerSettings.nearest =
            profile.triggerNearest ||
            profile.triggerBoneNearestAny;
        triggerSettings.boneNearestAny =
            profile.triggerBoneNearestAny;
        triggerSettings.visibleCheck =
            profile.triggerVisible;
        triggerSettings.ignoreDead = profile.ignoreDead;
        triggerSettings.ignoreNpc = profile.ignoreNpc;
        triggerSettings.ignoreAdmins = profile.ignoreAdmins;
        triggerSettings.ignoreFriends =
            profile.ignoreFriends || Menu::s_Aim_IgnoreFriends;
        triggerSettings.adminThreshold =
            Menu::s_AdminLevelThreshold;
        triggerSettings.lockOnLast =
            profile.triggerSticky;
        triggerSettings.fovPixels =
            ClampFloat(profile.triggerFov, 1.f, 40.f);
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
            s_TriggerLocked = {};
        }
        s_TriggerLastTickAt = now;

        SelectTarget(
            triggerSettings,
            frame,
            center,
            profile.triggerSticky
                ? &s_TriggerLocked
                : nullptr,
            triggerTarget);
        g_Diagnostics.triggerTargetValid =
            triggerTarget.valid;
        if (triggerTarget.valid)
        {
            if (triggerTarget.ped != s_TriggerCandidate ||
                triggerTarget.hitbox != s_TriggerHitbox)
            {
                s_TriggerCandidate = triggerTarget.ped;
                s_TriggerHitbox = triggerTarget.hitbox;
                s_TriggerAcquiredAt = now;
            }
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
                // INPUT_ATTACK = 24. Native input is frame-scoped.
                if (NativeInvoke::SetControlValueNextFrame(
                        0,
                        24,
                        1.f))
                {
                    s_TriggerLastShotAt = now;
                    g_Diagnostics.triggerFired = true;
                }
            }
            if (profile.triggerSticky)
                s_TriggerLocked = ToSnapshot(triggerTarget);
        }
        else
        {
            s_TriggerCandidate = nullptr;
            s_TriggerHitbox = -1;
            s_TriggerAcquiredAt = now;
            s_TriggerLocked = {};
        }
    }
    else
    {
        s_TriggerProfile = nullptr;
        s_TriggerCandidate = nullptr;
        s_TriggerHitbox = -1;
        s_TriggerAcquiredAt = 0;
        s_TriggerLastTickAt = 0;
        s_TriggerLocked = {};
    }

    if (profile.silentMark && g_SilentTarget.valid)
        g_DisplayTarget = g_SilentTarget;
    else if (vectorTarget.valid)
        g_DisplayTarget = ToSnapshot(vectorTarget);
}

} // namespace Features
