#include "SilentAim.h"
#include "Aimbot.h"
#include "../Menu/Menu.h"
#include "../SDK/Game.h"
#include "../SDK/Math.h"
#include "../SDK/Offsets.h"
#include "../Memory/Pattern.h"
#include "../Main/Hooks.h"

#include <Windows.h>
#include <MinHook.h>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace SilentAim {
namespace {

constexpr std::uint64_t kTargetMaxAgeMs = 350;

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
        return aim;

    // Two-pass ballistic lead: t ≈ |aim - muzzle| / v_bullet.
    Vector3 predicted = aim;
    for (int i = 0; i < 2; ++i)
    {
        const float dx = predicted.x - muzzle.x;
        const float dy = predicted.y - muzzle.y;
        const float dz = predicted.z - muzzle.z;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (!(dist > 0.05f))
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
    constexpr float kMaxLead = 7.5f;
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
        return aim;
    return predicted;
}

std::int64_t __fastcall hkBulletInstance(
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
                (now - target.updatedAt) <= kTargetMaxAgeMs;
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
                    Vector3 predicted = target.world;
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
                            target.world,
                            scaledVelocity,
                            velocity);
                    }
                    end->x = predicted.x;
                    end->y = predicted.y;
                    end->z = predicted.z;
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

bool Initialize() noexcept
{
    InterlockedExchange(
        &g_RngState,
        static_cast<LONG>(GetTickCount64() ^ 0x51Bu));

    if (g_Installed.load())
        return true;

    HMODULE module = GetModuleHandleW(L"GTA5.exe");
    if (!module) {
        g_Message = "GTA5.exe not loaded";
        return false;
    }

    uint8_t* match = Memory::FindPattern(
        "GTA5.exe",
        Offsets::Patterns::BulletInstance);
    if (!match) {
        g_Message = "bullet_instance pattern not found";
        hooks::HLog("SilentAim: bullet_instance pattern miss");
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
        return false;
    }

    if (MH_CreateHook(
            g_Target,
            reinterpret_cast<void*>(&hkBulletInstance),
            reinterpret_cast<void**>(&g_Original)) != MH_OK)
    {
        g_Message = "MinHook create failed";
        hooks::HLog("SilentAim: MH_CreateHook failed");
        return false;
    }

    if (MH_EnableHook(g_Target) != MH_OK) {
        MH_RemoveHook(g_Target);
        g_Original = nullptr;
        g_Message = "MinHook enable failed";
        hooks::HLog("SilentAim: MH_EnableHook failed");
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
