#include "SilentAim.h"
#include "Aimbot.h"
#include "CombatBake.h"
#include "../Menu/Menu.h"
#include "../SDK/Game.h"
#include "../SDK/Math.h"
#include "../SDK/Offsets.h"
#include "../Memory/Pattern.h"
#include "../Main/Hooks.h"
#include "../vmp_markers.hpp"

#include <Windows.h>
#include <MinHook.h>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace SilentAim {
namespace {

// How stale a published target may be and still be shot at. The aim point is
// carried forward over this gap along the tracked velocity, so the two have to
// agree: anything older than the compensation clamp would be aimed at a
// position nothing corrects. 350 ms used to be accepted with no compensation at
// all, which is over two metres of error on a sprinting target.

// sp¬ sBulletInstance signature.
using BulletInstanceFn = std::int64_t(__fastcall*)(
    std::int64_t* bulletInstance,
    const std::int64_t* weaponInfo,
    Vector3* start,
    Vector3* end,
    float velocity,
    unsigned int weaponHash,
    bool createsTrace,
    bool isAccurate);

BulletInstanceFn g_Original = nullptr;
void* g_Target = nullptr;

std::atomic<bool> g_Installed{false};
std::atomic<std::uintptr_t> g_ResolvedRva{0};
const char* g_Message = "Uninitialized";

volatile LONG g_RngState = 1;
volatile LONG g_ActiveCallbacks = 0;

float NextUnit() noexcept
{
    LONG state = InterlockedCompareExchange(&g_RngState, 0, 0);
    if (state == 0)
        state = 1;
    state = state * 1103515245 + 12345;
    InterlockedExchange(&g_RngState, state);
    return static_cast<float>(
        (static_cast<std::uint32_t>(state) >> 8) & 0xFFFF) /
        65535.0f;
}

bool PassesHitchance(const Features::AimTargetSnapshot& target) noexcept
{
    if (!target.hitchanceEnabled)
        return true;
    float chance = target.hitchance;
    if (!(chance > 0.0f))
        return false;
    if (chance >= 100.0f)
        return true;
    return NextUnit() * 100.0f <= chance;
}

bool IsReadablePtr(const void* p) noexcept
{
    if (!p)
        return false;
    const auto addr = reinterpret_cast<uintptr_t>(p);
    return addr > 0x10000 && addr < 0x00007FFFFFFEFFFF;
}

Vector3 PredictAimPoint(
    const Vector3& muzzle,
    const Vector3& aim,
    const Vector3& targetVel,
    float bulletSpeed) noexcept
{
    float speed = bulletSpeed;
    if (!std::isfinite(speed) || speed < 80.f)
        speed = 900.f;
    if (speed > 5000.f)
        speed = 5000.f;

    const float velLen = targetVel.Length();
    if (!(velLen > 0.15f) ||
        !std::isfinite(targetVel.x) ||
        !std::isfinite(targetVel.y) ||
        !std::isfinite(targetVel.z))
    {
        return aim;
    }

    // Two-pass ballistic lead: t ≈ |aim - muzzle| / v_bullet.
    Vector3 predicted = aim;
    for (int i = 0; i < 2; ++i)
    {
        const float dx = predicted.x - muzzle.x;
        const float dy = predicted.y - muzzle.y;
        const float dz = predicted.z - muzzle.z;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!(dist > CombatBake::g_Policy.silentMinRay))
            break;
        const float t = dist / speed;
        predicted.x = aim.x + targetVel.x * t;
        predicted.y = aim.y + targetVel.y * t;
        predicted.z = aim.z + targetVel.z * t;
    }

    // Cap extreme lead (desync / bad vel) so we never aim meters past the ped.
    const float leadX = predicted.x - aim.x;
    const float leadY = predicted.y - aim.y;
    const float leadZ = predicted.z - aim.z;
    const float leadLen =
        std::sqrt(leadX * leadX + leadY * leadY + leadZ * leadZ);
    const float kMaxLead = CombatBake::g_Policy.silentMaxLead;
    if (leadLen > kMaxLead)
    {
        const float s = kMaxLead / leadLen;
        predicted.x = aim.x + leadX * s;
        predicted.y = aim.y + leadY * s;
        predicted.z = aim.z + leadZ * s;
    }

    if (!std::isfinite(predicted.x) ||
        !std::isfinite(predicted.y) ||
        !std::isfinite(predicted.z))
    {
        return aim;
    }
    return predicted;
}

__declspec(noinline) std::int64_t __fastcall hkBulletInstance(
    std::int64_t* bulletInstance,
    const std::int64_t* weaponInfo,
    Vector3* start,
    Vector3* end,
    float velocity,
    unsigned int weaponHash,
    bool createsTrace,
    bool isAccurate)
{
    InterlockedIncrement(&g_ActiveCallbacks);
    CombatBake::Ensure();

    const Menu::AimWeaponProfile& profile =
        Menu::GetActiveAimProfile();
    const bool activationHeld =
        profile.silentKey <= 0 ||
        (GetAsyncKeyState(profile.silentKey) & 0x8000) != 0;
    if (profile.silentEnabled &&
        activationHeld &&
        IsReadablePtr(start) &&
        IsReadablePtr(end) &&
        g_Original)
    {
        Features::AimTargetSnapshot target{};
        if (Features::CopySilentTarget(target) &&
            target.valid &&
            std::isfinite(target.world.x) &&
            std::isfinite(target.world.y) &&
            std::isfinite(target.world.z))
        {
            const std::uint64_t now = GetTickCount64();
            const bool fresh =
                target.updatedAt != 0 &&
                now >= target.updatedAt &&
                (now - target.updatedAt) <=
                    CombatBake::g_Policy.silentTargetAgeMs;
            const bool visibleOk =
                profile.silentWallshot ||
                !profile.silentVisible ||
                target.visible;

            if (fresh &&
                visibleOk &&
                PassesHitchance(target))
            {
                // Keep real muzzle; lead end by target velocity / bullet speed.
                __try {
                    const Vector3 muzzle(start->x, start->y, start->z);

                    // The snapshot was taken on the render thread and this shot
                    // leaves the barrel later, on the game thread. Carry the aim
                    // point across that gap along the tracked velocity, or a
                    // target that is merely walking is missed by however far it
                    // moved since the frame the bone was read on. This is not
                    // the ballistic lead below: it corrects our own latency,
                    // which is always present, rather than bullet flight time.
                    Vector3 aim = target.world;
                    {
                        float age = static_cast<float>(
                            now - target.updatedAt) / 1000.f;
                        const float maxAge =
                            static_cast<float>(
                                CombatBake::g_Policy.silentTargetAgeMs) /
                            1000.f;
                        if (age > maxAge)
                            age = maxAge;
                        aim.x += target.velocity.x * age;
                        aim.y += target.velocity.y * age;
                        aim.z += target.velocity.z * age;
                    }

                    Vector3 predicted = aim;
                    if (profile.silentPrediction)
                    {
                        const float leadScale =
                            profile.silentLeadScale < 0.f
                            ? 0.f
                            : (profile.silentLeadScale > 2.f
                                ? 2.f
                                : profile.silentLeadScale);
                        const Vector3 scaledVelocity(
                            target.velocity.x * leadScale,
                            target.velocity.y * leadScale,
                            target.velocity.z * leadScale);
                        predicted = PredictAimPoint(
                            muzzle,
                            aim,
                            scaledVelocity,
                            velocity);
                    }

                    // The trace is pushed a little past the aim point instead of
                    // terminating on it. A bone centre can sit centimetres from
                    // the edge of the ped's collision, and a ray that ends right
                    // there can fail to register at all. Overshooting costs
                    // nothing: the game still reports the first thing the ray
                    // meets, so this cannot change which entity is hit.
                    Vector3 traceEnd = predicted;
                    {
                        const float dx = predicted.x - muzzle.x;
                        const float dy = predicted.y - muzzle.y;
                        const float dz = predicted.z - muzzle.z;
                        const float length = std::sqrt(
                            dx * dx + dy * dy + dz * dz);
                        if (length > CombatBake::g_Policy.silentMinRay &&
                            std::isfinite(length))
                        {
                            const float overshoot =
                                CombatBake::g_Policy.silentOvershoot;
                            const float scale =
                                (length + overshoot) / length;
                            traceEnd.x = muzzle.x + dx * scale;
                            traceEnd.y = muzzle.y + dy * scale;
                            traceEnd.z = muzzle.z + dz * scale;
                        }
                    }
                    end->x = traceEnd.x;
                    end->y = traceEnd.y;
                    end->z = traceEnd.z;

                    // Moving the endpoint does not get a bullet through
                    // geometry: the game still traces muzzle -> end and stops at
                    // the first thing it meets, so the shot lands in the wall.
                    // Wallshot was only bypassing this cheat's own visibility
                    // filter, which let a covered ped be locked but never hit.
                    //
                    // Pull the origin forward to just short of the target along
                    // the same ray. Direction and impact point are unchanged and
                    // only the traced length shrinks, so everything between the
                    // muzzle and the backoff point is skipped regardless of how
                    // thick it is.
                    //
                    // Not gated on target.visible. That flag is a RAGE ray
                    // refreshed a few times a second and aimed head to head, so
                    // it reads "visible" for a ped whose head clears a low wall
                    // while the chest being aimed at does not, and it lags a
                    // quarter second behind someone stepping into cover. Gating
                    // on it made the feature work only sometimes, which is worse
                    // than a switch that always does what it says.
                    if (profile.silentWallshot)
                    {
                        const float dx = predicted.x - muzzle.x;
                        const float dy = predicted.y - muzzle.y;
                        const float dz = predicted.z - muzzle.z;
                        const float length = std::sqrt(
                            dx * dx + dy * dy + dz * dz);
                        // Point blank has no cover worth clearing, and backing
                        // off past the muzzle would flip the ray.
                        if (length >
                                CombatBake::g_Policy.silentWallshotMinLength &&
                            std::isfinite(length))
                        {
                            // Only cover within this distance of the target is
                            // still in the way, so it does not need to be large.
                            const float kBackoffMetres =
                                CombatBake::g_Policy.silentBackoff;
                            float backoff = length * 0.5f;
                            if (backoff > kBackoffMetres)
                                backoff = kBackoffMetres;
                            const float scale = backoff / length;
                            start->x = predicted.x - dx * scale;
                            start->y = predicted.y - dy * scale;
                            start->z = predicted.z - dz * scale;
                        }
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                }
            }
        }
    }

    std::int64_t result = 0;
    if (g_Original) {
        __try {
            result = g_Original(
                bulletInstance,
                weaponInfo,
                start,
                end,
                velocity,
                weaponHash,
                createsTrace,
                isAccurate);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            result = 0;
        }
    }

    InterlockedDecrement(&g_ActiveCallbacks);
    return result;
}

} // namespace

__declspec(noinline) bool Initialize() noexcept
{
    SHADOW_PROTECT_ULTRA("Silent.Initialize");
    InterlockedExchange(
        &g_RngState,
        static_cast<LONG>(GetTickCount64() ^ 0x51Bu));

    if (g_Installed.load())
    {
        SHADOW_PROTECT_END();
        return true;
    }

    HMODULE module = GetModuleHandleW(L"GTA5.exe");
    if (!module) {
        g_Message = "GTA5.exe not loaded";
        SHADOW_PROTECT_END();
        return false;
    }

    uint8_t* match = Memory::FindPattern(
        "GTA5.exe",
        Offsets::Patterns::BulletInstance);
    if (!match) {
        g_Message = "bullet_instance pattern not found";
        hooks::HLog("SilentAim: bullet_instance pattern miss");
        SHADOW_PROTECT_END();
        return false;
    }

    g_Target = match;
    g_ResolvedRva.store(
        reinterpret_cast<std::uintptr_t>(match) -
        reinterpret_cast<std::uintptr_t>(module));

    if (MH_Initialize() != MH_OK &&
        MH_Initialize() != MH_ERROR_ALREADY_INITIALIZED)
    {
        g_Message = "MinHook initialization failed";
        SHADOW_PROTECT_END();
        return false;
    }

    if (MH_CreateHook(
            g_Target,
            reinterpret_cast<void*>(&hkBulletInstance),
            reinterpret_cast<void**>(&g_Original)) != MH_OK)
    {
        g_Message = "MinHook create failed";
        hooks::HLog("SilentAim: MH_CreateHook failed");
        SHADOW_PROTECT_END();
        return false;
    }

    if (MH_EnableHook(g_Target) != MH_OK) {
        MH_RemoveHook(g_Target);
        g_Original = nullptr;
        g_Message = "MinHook enable failed";
        hooks::HLog("SilentAim: MH_EnableHook failed");
        SHADOW_PROTECT_END();
        return false;
    }

    g_Installed.store(true);
    g_Message = "sp¬ bullet_instance hook ready";
    char buf[96]{};
    wsprintfA(
        buf,
        "SilentAim: bullet_instance hooked RVA=0x%llX",
        static_cast<unsigned long long>(g_ResolvedRva.load()));
    hooks::HLog(buf);
    SHADOW_PROTECT_END();
    return true;
}

void Shutdown() noexcept
{
    if (!g_Installed.exchange(false))
        return;

    if (g_Target)
        MH_DisableHook(g_Target);

    // Wait for in-flight callbacks before remove.
    for (int i = 0; i < 200; ++i) {
        if (InterlockedCompareExchange(&g_ActiveCallbacks, 0, 0) == 0)
            break;
        Sleep(1);
    }

    if (g_Target) {
        MH_RemoveHook(g_Target);
        g_Target = nullptr;
    }
    g_Original = nullptr;
    g_Message = "Shutdown";
}

Status GetStatus() noexcept
{
    Status status{};
    status.installed = g_Installed.load();
    status.available = status.installed && g_Original != nullptr;
    status.resolvedRva = g_ResolvedRva.load();
    status.message = g_Message;
    return status;
}

} // namespace SilentAim
