#include "MiscFeatures.h"

#include "NativeInvoke.hpp"
#include "FiveRpAdminScan.h"
#include "QuantTools.h"
#include "RagePlayerNames.h"
#include "RageV8Runtime.h"
#include "VehicleMovement.h"
#include "../Menu/Menu.h"
#include "../SDK/Game.h"
#include "../SDK/Offsets.h"
#include "../SDK/RageBuildFlavor.h"
#include "Overlay.h"
#include "../imgui/imgui.h"

#include <Windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace hooks
{
    void HLog(const char* message);
}

namespace MiscFeatures
{
namespace
{
    // Recent renderer AVs (GTA5+0x79eef6 / +0x13dd6d6) show corrupted
    // vtables and hash-bucket pointers with float bit-patterns. Quarantine
    // all GTA5.exe float writers that were added for combat/FOV until a
    // verified pre-render path exists. Config/UI may still toggle flags.
    constexpr bool kAllowWeaponMemoryWrites = true;
    constexpr bool kAllowCameraMemoryWrites = false;

    Settings g_Settings{};
    Settings g_LastSynced{};
    bool g_HaveSynced = false;
    bool g_PendingSuicide = false;
    bool g_PendingResetAnim = false;
    bool g_PendingHeal = false;
    bool g_PendingArmor = false;
    bool g_PendingRespawn = false;
    bool g_PendingRepairVehicle = false;
    bool g_PendingTpWaypoint = false;
    bool g_NoclipKeyWasDown = false;
    bool g_FreecamKeyWasDown = false;
    bool g_TpWaypointKeyWasDown = false;
    bool g_ClickWarpKeyWasDown = false;
    bool g_CwJsHooked = false;
    bool g_CwJsOn = false;
    bool g_CwJsPaused = false;
    int g_CwJsKey = 0;
    std::uint64_t g_CwJsGeneration = 0;
    bool g_CwHaveHint = false;
    float g_CwHintX = 0.f;
    float g_CwHintY = 0.f;
    float g_CwHintZ = 0.f;
    int g_NoclipHandle = 0;
    uint8_t* g_NoclipEntity = nullptr;
    ULONGLONG g_NoclipLastTick = 0;
    int g_FreecamFrozenPed = 0;
    float g_FcX = 0.f;
    float g_FcY = 0.f;
    float g_FcZ = 0.f;
    float g_FcPitch = 0.f;
    float g_FcYaw = 0.f;
    bool g_FcHavePose = false;
    bool g_FcJsHooked = false;
    bool g_FcJsActive = false;
    bool g_NcJsActive = false;
    bool g_FcJsPaused = false;
    float g_FcJsSpeed = 0.f;
    float g_NcJsSpeed = 0.f;
    std::uint64_t g_FcJsGeneration = 0;
    bool g_WeatherWasOn = false;
    bool g_TimeWasOn = false;
    bool g_FogWasOn = false;
    bool g_WtJsHooked = false;
    bool g_WtJsWeather = false;
    bool g_WtJsTime = false;
    bool g_WtJsFog = false;
    bool g_WtJsFreeze = false;
    int g_WtJsWeatherIdx = -1;
    int g_WtJsHour = -1;
    int g_WtJsMinute = -1;
    std::uint64_t g_WtJsGeneration = 0;
    ULONGLONG g_LastGreenzoneTick = 0;

    // Multi-frame waypoint TP: stream/probe while the entity stays put, then
    // perform one final warp only after destination ground is resolved.
    enum class TpWpPhase : int
    {
        Idle = 0,
        Probing = 1,
    };
    TpWpPhase g_TpWpPhase = TpWpPhase::Idle;
    float g_TpWpX = 0.f;
    float g_TpWpY = 0.f;
    int g_TpWpPed = 0;
    int g_TpWpHandle = 0;
    ULONGLONG g_TpWpStartedAt = 0;
    static constexpr ULONGLONG kTpWpTimeoutMs = 5000;
    static constexpr float kTpWpProbeZ = 1000.f;

    bool AppendNum(
        char* output,
        std::size_t capacity,
        std::size_t& used,
        const char* key,
        double value) noexcept
    {
        if (!output || used >= capacity || !key)
            return false;
        const int written = sprintf_s(
            output + used,
            capacity - used,
            "%s=%.9g\n",
            key,
            value);
        if (written <= 0)
            return false;
        used += static_cast<std::size_t>(written);
        return true;
    }

    const char* FindValue(const char* content, const char* key) noexcept
    {
        if (!content || !key)
            return nullptr;
        const std::size_t keyLength = std::strlen(key);
        const char* line = content;
        while (*line) {
            if (!std::strncmp(line, key, keyLength) &&
                line[keyLength] == '=')
                return line + keyLength + 1;
            const char* next = std::strchr(line, '\n');
            if (!next)
                break;
            line = next + 1;
        }
        return nullptr;
    }

    void Queue(const char* command) noexcept
    {
        // Silent: never toast / never spam Quant aquanaut status via UI notify.
        QuantTools::QueueCommand(command, command, true);
    }

    void SyncBoolCmd(
        bool enabled,
        bool& last,
        const char* onCmd,
        const char* offCmd) noexcept
    {
        if (g_HaveSynced && enabled == last)
            return;
        Queue(enabled ? onCmd : offCmd);
        last = enabled;
    }

    void SyncFloatCmd(
        bool enabled,
        float value,
        bool& lastEnabled,
        float& lastValue,
        const char* prefix) noexcept
    {
        if (g_HaveSynced &&
            enabled == lastEnabled &&
            std::fabs(value - lastValue) < 0.25f)
            return;
        char cmd[96]{};
        sprintf_s(
            cmd,
            "%s:%d:%.2f",
            prefix,
            enabled ? 1 : 0,
            value);
        Queue(cmd);
        lastEnabled = enabled;
        lastValue = value;
    }

    struct WeaponInfoBackup final
    {
        uint8_t* info{};
        float accuracySpread{};
        float batchSpread{};
        float recoilAccuracyMax{};
        float recoilErrorTime{};
        float recoilShakeAmplitude{};
        float reloadTimeMp{};
        float reloadTimeSp{};
        float vehicleReloadTime{};
        float animReloadRate{};
        bool valid{};
    };

    WeaponInfoBackup g_WeaponBackup{};

    bool ReadWeaponFloat(uint8_t* info, uint32_t off, float& out) noexcept
    {
        __try {
            out = *reinterpret_cast<float*>(info + off);
            return std::isfinite(out);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool WriteWeaponFloat(uint8_t* info, uint32_t off, float value) noexcept
    {
        __try {
            *reinterpret_cast<float*>(info + off) = value;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    uint8_t* GetLocalWeaponInfo() noexcept
    {
        CPed* ped = Game::GetLocalPed();
        if (!ped)
            return nullptr;
        uint8_t* info = nullptr;
        __try {
            uint8_t* wm = *reinterpret_cast<uint8_t**>(
                reinterpret_cast<uint8_t*>(ped) +
                Offsets::Ped::WeaponManager);
            if (!IsValidPtr(wm))
                return nullptr;
            info = *reinterpret_cast<uint8_t**>(
                wm + Offsets::WeaponManager::WeaponInfo);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
        return IsValidPtr(info) ? info : nullptr;
    }

    void RestoreWeaponInfo() noexcept
    {
        if (!g_WeaponBackup.valid || !IsValidPtr(g_WeaponBackup.info)) {
            g_WeaponBackup = {};
            return;
        }
        WriteWeaponFloat(
            g_WeaponBackup.info,
            Offsets::WeaponInfo::AccuracySpread,
            g_WeaponBackup.accuracySpread);
        WriteWeaponFloat(
            g_WeaponBackup.info,
            Offsets::WeaponInfo::BatchSpread,
            g_WeaponBackup.batchSpread);
        WriteWeaponFloat(
            g_WeaponBackup.info,
            Offsets::WeaponInfo::RecoilAccuracyMax,
            g_WeaponBackup.recoilAccuracyMax);
        WriteWeaponFloat(
            g_WeaponBackup.info,
            Offsets::WeaponInfo::RecoilErrorTime,
            g_WeaponBackup.recoilErrorTime);
        WriteWeaponFloat(
            g_WeaponBackup.info,
            Offsets::WeaponInfo::RecoilShakeAmplitude,
            g_WeaponBackup.recoilShakeAmplitude);
        WriteWeaponFloat(
            g_WeaponBackup.info,
            Offsets::WeaponInfo::ReloadTimeMP,
            g_WeaponBackup.reloadTimeMp);
        WriteWeaponFloat(
            g_WeaponBackup.info,
            Offsets::WeaponInfo::ReloadTimeSP,
            g_WeaponBackup.reloadTimeSp);
        WriteWeaponFloat(
            g_WeaponBackup.info,
            Offsets::WeaponInfo::VehicleReloadTime,
            g_WeaponBackup.vehicleReloadTime);
        WriteWeaponFloat(
            g_WeaponBackup.info,
            Offsets::WeaponInfo::AnimReloadRate,
            g_WeaponBackup.animReloadRate);
        g_WeaponBackup = {};
    }

    bool InRange(float v, float lo, float hi) noexcept
    {
        return std::isfinite(v) && v >= lo && v <= hi;
    }

    bool CaptureWeaponInfo(uint8_t* info) noexcept
    {
        WeaponInfoBackup backup{};
        backup.info = info;
        if (!ReadWeaponFloat(
                info,
                Offsets::WeaponInfo::AccuracySpread,
                backup.accuracySpread) ||
            !ReadWeaponFloat(
                info,
                Offsets::WeaponInfo::BatchSpread,
                backup.batchSpread) ||
            !ReadWeaponFloat(
                info,
                Offsets::WeaponInfo::RecoilAccuracyMax,
                backup.recoilAccuracyMax) ||
            !ReadWeaponFloat(
                info,
                Offsets::WeaponInfo::RecoilErrorTime,
                backup.recoilErrorTime) ||
            !ReadWeaponFloat(
                info,
                Offsets::WeaponInfo::RecoilShakeAmplitude,
                backup.recoilShakeAmplitude) ||
            !ReadWeaponFloat(
                info,
                Offsets::WeaponInfo::ReloadTimeMP,
                backup.reloadTimeMp) ||
            !ReadWeaponFloat(
                info,
                Offsets::WeaponInfo::ReloadTimeSP,
                backup.reloadTimeSp) ||
            !ReadWeaponFloat(
                info,
                Offsets::WeaponInfo::VehicleReloadTime,
                backup.vehicleReloadTime) ||
            !ReadWeaponFloat(
                info,
                Offsets::WeaponInfo::AnimReloadRate,
                backup.animReloadRate))
            return false;

        // Reject layouts that don't look like CWeaponInfo — wrong offsets
        // previously corrupted memory and crashed the renderer.
        if (!InRange(backup.accuracySpread, 0.f, 25.f) ||
            !InRange(backup.batchSpread, 0.f, 25.f) ||
            !InRange(backup.recoilAccuracyMax, 0.f, 25.f) ||
            !InRange(backup.recoilErrorTime, 0.f, 10.f) ||
            !InRange(backup.recoilShakeAmplitude, 0.f, 10.f) ||
            !InRange(backup.reloadTimeMp, 0.05f, 15.f) ||
            !InRange(backup.reloadTimeSp, 0.05f, 15.f) ||
            !InRange(backup.vehicleReloadTime, 0.05f, 15.f) ||
            !InRange(backup.animReloadRate, 0.05f, 10.f))
            return false;

        backup.valid = true;
        g_WeaponBackup = backup;
        return true;
    }

    float Clamp01(float v) noexcept
    {
        if (v < 0.f)
            return 0.f;
        if (v > 1.f)
            return 1.f;
        return v;
    }

    void ApplyWeaponCombatPatches() noexcept
    {
        // Globals from Player; equipped weapon override wins when active.
        bool noSpread = g_Settings.noSpread;
        bool noRecoil = g_Settings.noRecoil;
        bool reloadSpeed = g_Settings.reloadSpeed;
        float noSpreadPct = g_Settings.noSpreadPct;
        float noRecoilPct = g_Settings.noRecoilPct;
        float reloadSpeedPct = g_Settings.reloadSpeedPct;

        const std::uint32_t hash = Game::GetLocalWeaponHash();
        if (const Menu::WeaponOverrideSlot* ov = Menu::FindWeaponOverride(hash);
            ov && ov->active)
        {
            noSpread = ov->profile.noSpread;
            noRecoil = ov->profile.noRecoil;
            reloadSpeed = ov->profile.reloadSpeed;
            noSpreadPct = ov->profile.noSpreadPct;
            noRecoilPct = ov->profile.noRecoilPct;
            reloadSpeedPct = ov->profile.reloadSpeedPct;
        }

        const bool any = noSpread || noRecoil || reloadSpeed;
        if (!any) {
            RestoreWeaponInfo();
            return;
        }

        if (!kAllowWeaponMemoryWrites) {
            static bool logged = false;
            if (!logged) {
                logged = true;
                hooks::HLog(
                    "[Misc] combat memory writer quarantined "
                    "(renderer AV GTA5+0x79eef6 / +0x13dd6d6)");
            }
            RestoreWeaponInfo();
            return;
        }

        uint8_t* info = GetLocalWeaponInfo();
        if (!info) {
            RestoreWeaponInfo();
            return;
        }

        if (!g_WeaponBackup.valid || g_WeaponBackup.info != info) {
            if (g_WeaponBackup.valid)
                RestoreWeaponInfo();
            if (!CaptureWeaponInfo(info))
                return;
        }

        const float spreadT =
            noSpread ? Clamp01(noSpreadPct * 0.01f) : 0.f;
        const float recoilT =
            noRecoil ? Clamp01(noRecoilPct * 0.01f) : 0.f;
        const float reloadT =
            reloadSpeed ? Clamp01(reloadSpeedPct * 0.01f) : 0.f;

        // Soft scales only — never zero critical timing fields hard.
        WriteWeaponFloat(
            info,
            Offsets::WeaponInfo::AccuracySpread,
            g_WeaponBackup.accuracySpread * (1.f - spreadT * 0.95f));
        WriteWeaponFloat(
            info,
            Offsets::WeaponInfo::BatchSpread,
            g_WeaponBackup.batchSpread * (1.f - spreadT * 0.95f));
        WriteWeaponFloat(
            info,
            Offsets::WeaponInfo::RecoilShakeAmplitude,
            g_WeaponBackup.recoilShakeAmplitude * (1.f - recoilT * 0.95f));
        WriteWeaponFloat(
            info,
            Offsets::WeaponInfo::RecoilAccuracyMax,
            g_WeaponBackup.recoilAccuracyMax * (1.f - recoilT * 0.9f));
        WriteWeaponFloat(
            info,
            Offsets::WeaponInfo::RecoilErrorTime,
            g_WeaponBackup.recoilErrorTime * (1.f - recoilT * 0.85f));
        const float reloadTimeScale = 1.f - reloadT * 0.75f;
        WriteWeaponFloat(
            info,
            Offsets::WeaponInfo::ReloadTimeMP,
            g_WeaponBackup.reloadTimeMp * reloadTimeScale);
        WriteWeaponFloat(
            info,
            Offsets::WeaponInfo::ReloadTimeSP,
            g_WeaponBackup.reloadTimeSp * reloadTimeScale);
        WriteWeaponFloat(
            info,
            Offsets::WeaponInfo::VehicleReloadTime,
            g_WeaponBackup.vehicleReloadTime * reloadTimeScale);
        WriteWeaponFloat(
            info,
            Offsets::WeaponInfo::AnimReloadRate,
            g_WeaponBackup.animReloadRate * (1.f + reloadT * 1.5f));
    }

    void ApplyFixedStats() noexcept
    {
        CPed* ped = Game::GetLocalPed();
        if (!ped)
            return;
        auto* base = reinterpret_cast<uint8_t*>(ped);
        __try {
            if (g_Settings.fixedHp) {
                float* hp = reinterpret_cast<float*>(
                    base + Offsets::Ped::Health);
                // GTA stores health as 100 + value (dead at 100).
                *hp = 100.f + g_Settings.fixedHpValue;
            }
            if (g_Settings.fixedAp) {
                float* ap = reinterpret_cast<float*>(
                    base + Offsets::Ped::Armor);
                *ap = g_Settings.fixedApValue;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    ULONGLONG g_LastAntiAfkTick = 0;
    ULONGLONG g_NextAntiAfkIntervalMs = 28000;

    void ApplyAntiAfk() noexcept
    {
        if (!g_Settings.antiAfk)
            return;

        const ULONGLONG now = GetTickCount64();

        if (g_LastAntiAfkTick != 0 && (now - g_LastAntiAfkTick) < g_NextAntiAfkIntervalMs)
            return;

        g_LastAntiAfkTick = now;

        // Tuned 5RP safe pacing: 22s - 34s randomized human-like window
        constexpr float kBaseSec = 28.f;
        const float jitterFactor = 0.8f + static_cast<float>(rand() % 41) * 0.01f;
        g_NextAntiAfkIntervalMs = static_cast<ULONGLONG>(kBaseSec * jitterFactor * 1000.f);

        // 1. Reset GTA V native idle timer / camera state via subtle control impulse
        if (NativeInvoke::Init())
        {
            // INPUT_LOOK_LR (1) & INPUT_LOOK_UD (2) — micro impulse to keep camera and pad active
            const float jitterLR = static_cast<float>((rand() % 41) - 20) * 0.0001f;
            const float jitterUD = static_cast<float>((rand() % 21) - 10) * 0.0001f;
            NativeInvoke::SetControlValueNextFrame(0, 1, jitterLR);
            NativeInvoke::SetControlValueNextFrame(0, 2, jitterUD);
        }

        // 2. Micro-nudge ped heading if local ped exists and is on foot
        CPed* ped = Game::GetLocalPed();
        if (ped)
        {
            auto* base = reinterpret_cast<uint8_t*>(ped);
            uint8_t* nav = nullptr;
            __try {
                nav = *reinterpret_cast<uint8_t**>(base + 0x30);
                if (IsValidPtr(nav))
                {
                    Vector3* forward = reinterpret_cast<Vector3*>(nav + 0x20);
                    if (forward && std::isfinite(forward->x) && std::isfinite(forward->y))
                    {
                        const float angleDelta = static_cast<float>((rand() % 21) - 10) * 0.005f;
                        const float cosA = std::cos(angleDelta);
                        const float sinA = std::sin(angleDelta);
                        const float nx = forward->x * cosA - forward->y * sinA;
                        const float ny = forward->x * sinA + forward->y * cosA;
                        forward->x = nx;
                        forward->y = ny;
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        // 3. V8 script engine: trigger server action resets for GTA5RP / RageMP
        if (RageV8Runtime::GetStatus().state == RageV8Runtime::State::Ready)
        {
            static constexpr char kAntiAfkJs[] =
                "try{"
                "if(typeof global.antiAFK_sendAction==='function')global.antiAFK_sendAction();"
                "if(typeof global.antiAFK_sendAFK==='function')global.antiAFK_sendAFK=function(){};"
                "}catch(e){}";

            RageV8Runtime::EnqueueFireAndForget(kAntiAfkJs);
        }
    }

    void ApplySpeedhack() noexcept
    {
        if (!g_Settings.speedhack)
            return;
        CPed* ped = Game::GetLocalPed();
        if (!ped)
            return;

        // Skip while in a vehicle — Gravity handles cars.
        auto* base = reinterpret_cast<uint8_t*>(ped);
        for (const uint32_t off : {0xD10u, 0xD28u, 0xD30u, 0xC30u}) {
            uint8_t* vehicle = nullptr;
            __try {
                vehicle = *reinterpret_cast<uint8_t**>(base + off);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                vehicle = nullptr;
            }
            if (IsValidPtr(vehicle) && vehicle != base)
                return;
        }

        uint8_t* nav = nullptr;
        __try {
            nav = *reinterpret_cast<uint8_t**>(base + 0x30);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return;
        }
        if (!IsValidPtr(nav))
            return;

        Vector3 velocity{};
        __try {
            velocity = *reinterpret_cast<Vector3*>(nav + 0x70);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return;
        }

        const float speed2 =
            velocity.x * velocity.x + velocity.y * velocity.y;
        if (speed2 < 0.04f)
            return;

        // Only boost when sprinting (Shift) or always small boost when moving.
        const bool sprinting = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        if (!sprinting)
            return;

        float mul = g_Settings.speedhackValue;
        if (mul < 1.f)
            mul = 1.f;
        if (mul > 10.f)
            mul = 10.f;

        __try {
            *reinterpret_cast<Vector3*>(nav + 0x70) = Vector3{
                velocity.x * mul,
                velocity.y * mul,
                velocity.z};
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    void HandleKeyToggles() noexcept
    {
        if (Menu::IsVisible())
            return;

        const bool noclipDown =
            g_Settings.noclipKey > 0 &&
            (GetAsyncKeyState(g_Settings.noclipKey) & 0x8000) != 0;
        if (noclipDown && !g_NoclipKeyWasDown) {
            g_Settings.noclip = !g_Settings.noclip;
        }
        g_NoclipKeyWasDown = noclipDown;

        const bool freecamDown =
            g_Settings.freecamKey > 0 &&
            (GetAsyncKeyState(g_Settings.freecamKey) & 0x8000) != 0;
        if (freecamDown && !g_FreecamKeyWasDown) {
            g_Settings.freecam = !g_Settings.freecam;
        }
        g_FreecamKeyWasDown = freecamDown;

        const bool tpWpDown =
            g_Settings.tpWaypointKey > 0 &&
            (GetAsyncKeyState(g_Settings.tpWaypointKey) & 0x8000) != 0;
        if (tpWpDown && !g_TpWaypointKeyWasDown)
            g_PendingTpWaypoint = true;
        g_TpWaypointKeyWasDown = tpWpDown;
    }

    uint8_t* TpEntityPtr() noexcept
    {
        CPed* ped = Game::GetLocalPed();
        if (!ped || !IsValidPtr(ped))
            return nullptr;

        auto* base = reinterpret_cast<uint8_t*>(ped);
        for (const uint32_t off : {0xD10u, 0xD28u, 0xD30u, 0xC30u})
        {
            uint8_t* vehicle = nullptr;
            __try
            {
                vehicle = *reinterpret_cast<uint8_t**>(base + off);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                vehicle = nullptr;
            }
            if (IsValidPtr(vehicle) && vehicle != base)
                return vehicle;
        }
        return base;
    }

    bool SetEntityCoordsMemory(
        uint8_t* entity,
        float x,
        float y,
        float z) noexcept
    {
        if (!IsValidPtr(entity))
            return false;
        __try
        {
            uint8_t* nav = *reinterpret_cast<uint8_t**>(entity + 0x30);
            if (!IsValidPtr(nav))
                return false;

            auto* navPos = reinterpret_cast<Vector3*>(nav + 0x50);
            navPos->x = x;
            navPos->y = y;
            navPos->z = z;

            auto* entPos = reinterpret_cast<Vector3*>(
                entity + Offsets::Entity::Position);
            entPos->x = x;
            entPos->y = y;
            entPos->z = z;

            auto* navVel = reinterpret_cast<Vector3*>(nav + 0x70);
            navVel->x = 0.f;
            navVel->y = 0.f;
            navVel->z = 0.f;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadEntityPositionMemory(
        uint8_t* entity,
        Vector3& position) noexcept
    {
        if (!IsValidPtr(entity))
            return false;
        __try
        {
            uint8_t* navigation =
                *reinterpret_cast<uint8_t**>(entity + 0x30);
            if (!IsValidPtr(navigation))
                return false;
            position =
                *reinterpret_cast<Vector3*>(navigation + 0x50);
            return
                std::isfinite(position.x) &&
                std::isfinite(position.y) &&
                std::isfinite(position.z);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void RestoreNoclipEntity() noexcept
    {
        if (g_NoclipHandle != 0 &&
            NativeInvoke::Init() &&
            NativeInvoke::DoesEntityExist(g_NoclipHandle))
        {
            NativeInvoke::SetEntityCollision(
                g_NoclipHandle,
                true,
                true);
            NativeInvoke::SetEntityHasGravity(
                g_NoclipHandle,
                true);
            NativeInvoke::FreezeEntityPosition(
                g_NoclipHandle,
                false);
        }
        g_NoclipHandle = 0;
        g_NoclipEntity = nullptr;
        g_NoclipLastTick = 0;
    }

    bool SetNoclipPosition(
        int handle,
        uint8_t* entity,
        const Vector3& position) noexcept
    {
        if (!handle ||
            !IsValidPtr(entity) ||
            !std::isfinite(position.x) ||
            !std::isfinite(position.y) ||
            !std::isfinite(position.z))
        {
            return false;
        }

        NativeInvoke::SetEntityCoordsNoOffset(
            handle,
            position.x,
            position.y,
            position.z,
            true,
            true,
            true);
        NativeInvoke::SetEntityVelocity(
            handle,
            0.f,
            0.f,
            0.f);
        return SetEntityCoordsMemory(
            entity,
            position.x,
            position.y,
            position.z);
    }

    bool ReadViewportLook(Vector3& outPos, Vector3& outLook) noexcept
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
            const float* view = reinterpret_cast<const float*>(
                viewport + Offsets::Viewport::ViewMatrix);
            outPos = Vector3(view[12], view[13], view[14]);
            outLook = Vector3(-view[8], -view[9], -view[10]);
            const float length = std::sqrt(
                outLook.x * outLook.x +
                outLook.y * outLook.y +
                outLook.z * outLook.z);
            if (!(length > 0.001f))
                return false;
            outLook.x /= length;
            outLook.y /= length;
            outLook.z /= length;
            return std::fabs(outPos.x) + std::fabs(outPos.y) > 0.001f;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool ResolveLookRotation(float& pitch, float& yaw) noexcept
    {
        float roll = 0.f;
        if (NativeInvoke::GetGameplayCamRot(pitch, roll, yaw))
        {
            (void)roll;
            return true;
        }
        if (g_FcHavePose)
        {
            pitch = g_FcPitch;
            yaw = g_FcYaw;
            return true;
        }
        Vector3 pos{};
        Vector3 look{};
        if (!ReadViewportLook(pos, look))
        {
            pitch = 0.f;
            yaw = 0.f;
            return false;
        }
        constexpr float kDeg = 180.f / 3.14159265358979323846f;
        yaw = std::atan2(-look.x, look.y) * kDeg;
        const float flat = std::sqrt(look.x * look.x + look.y * look.y);
        pitch = std::atan2(look.z, flat) * kDeg;
        return true;
    }

    void LookVectors(
        float pitch,
        float yaw,
        Vector3& forward,
        Vector3& right) noexcept
    {
        constexpr float kRadians =
            3.14159265358979323846f / 180.f;
        const float pitchRadians = pitch * kRadians;
        const float yawRadians = yaw * kRadians;
        forward = Vector3{
            -std::sin(yawRadians) * std::cos(pitchRadians),
            std::cos(yawRadians) * std::cos(pitchRadians),
            std::sin(pitchRadians)};
        right = Vector3{
            std::cos(yawRadians),
            std::sin(yawRadians),
            0.f};
    }

    void ApplyNoclip() noexcept
    {
        if (!g_Settings.noclip)
        {
            RestoreNoclipEntity();
            return;
        }

        if (!NativeInvoke::Init())
        {
            RestoreNoclipEntity();
            return;
        }

        const int ped = NativeInvoke::PlayerPedId();
        int handle = ped;
        if (!ped || !NativeInvoke::DoesEntityExist(ped))
        {
            RestoreNoclipEntity();
            return;
        }

        const int vehicle =
            NativeInvoke::GetVehiclePedIsIn(ped, false);
        if (vehicle)
        {
            if (NativeInvoke::GetPedInVehicleSeat(
                    vehicle,
                    -1) != ped)
            {
                RestoreNoclipEntity();
                return;
            }
            handle = vehicle;
        }

        uint8_t* entity = TpEntityPtr();
        if (!IsValidPtr(entity) ||
            !NativeInvoke::DoesEntityExist(handle))
        {
            RestoreNoclipEntity();
            return;
        }

        if (handle != g_NoclipHandle ||
            entity != g_NoclipEntity)
        {
            RestoreNoclipEntity();
            g_NoclipHandle = handle;
            g_NoclipEntity = entity;
        }

        // Script-thread owns freeze + setCoords. Present natives fight
        // that path and GetGameplayCamRot from Present is usually empty.
        const auto v8 = RageV8Runtime::GetStatus();
        if (v8.state == RageV8Runtime::State::Ready &&
            v8.rootCaptured)
        {
            return;
        }

        NativeInvoke::SetEntityHasGravity(handle, false);
        NativeInvoke::SetEntityCollision(handle, false, false);
        NativeInvoke::FreezeEntityPosition(handle, true);
        NativeInvoke::SetEntityVelocity(handle, 0.f, 0.f, 0.f);

        const ULONGLONG now = GetTickCount64();
        float deltaSeconds = 1.f / 60.f;
        if (g_NoclipLastTick != 0 && now >= g_NoclipLastTick)
        {
            deltaSeconds =
                static_cast<float>(now - g_NoclipLastTick) * 0.001f;
            if (deltaSeconds < 0.001f)
                deltaSeconds = 0.001f;
            if (deltaSeconds > 0.05f)
                deltaSeconds = 0.05f;
        }
        g_NoclipLastTick = now;

        if (g_Settings.freecam || Menu::IsVisible())
            return;

        const float forwardInput =
            ((GetAsyncKeyState('W') & 0x8000) ? 1.f : 0.f) -
            ((GetAsyncKeyState('S') & 0x8000) ? 1.f : 0.f);
        const float rightInput =
            ((GetAsyncKeyState('D') & 0x8000) ? 1.f : 0.f) -
            ((GetAsyncKeyState('A') & 0x8000) ? 1.f : 0.f);
        const float verticalInput =
            ((GetAsyncKeyState(VK_SPACE) & 0x8000) ? 1.f : 0.f) -
            ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? 1.f : 0.f);
        if (forwardInput == 0.f &&
            rightInput == 0.f &&
            verticalInput == 0.f)
            return;

        float pitch = 0.f;
        float yaw = 0.f;
        if (!ResolveLookRotation(pitch, yaw))
            return;

        Vector3 forward{};
        Vector3 right{};
        LookVectors(pitch, yaw, forward, right);
        Vector3 direction{
            forward.x * forwardInput + right.x * rightInput,
            forward.y * forwardInput + right.y * rightInput,
            forward.z * forwardInput + verticalInput};
        const float directionLength =
            std::sqrt(
                direction.x * direction.x +
                direction.y * direction.y +
                direction.z * direction.z);
        if (!(directionLength > 0.0001f))
            return;
        direction.x /= directionLength;
        direction.y /= directionLength;
        direction.z /= directionLength;

        float speed = g_Settings.noclipSpeed;
        if (!(speed >= 0.2f))
            speed = 0.2f;
        if (speed > 20.f)
            speed = 20.f;
        g_Settings.noclipSpeed = speed;
        const float boost =
            (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 3.f : 1.f;
        const float distance = speed * 20.f * boost * deltaSeconds;

        Vector3 position{};
        if (!ReadEntityPositionMemory(entity, position))
            return;
        position.x += direction.x * distance;
        position.y += direction.y * distance;
        position.z += direction.z * distance;
        SetNoclipPosition(handle, entity, position);
    }

    bool FreecamV8Ready() noexcept
    {
        const auto status = RageV8Runtime::GetStatus();
        if (status.state != RageV8Runtime::State::Ready ||
            !status.rootCaptured)
            return false;
        if (status.generation != g_FcJsGeneration)
        {
            g_FcJsHooked = false;
            g_FcJsActive = false;
            g_NcJsActive = false;
            g_FcJsGeneration = status.generation;
        }
        return true;
    }

    bool EnqueueFreecamScript(const char* script) noexcept
    {
        if (!script || !FreecamV8Ready())
            return false;
        return RageV8Runtime::EnqueueFireAndForget(script);
    }

    bool EnqueueClearTasksScript() noexcept
    {
        static constexpr char kClearTasksJs[] =
            "try{"
            "var p=mp.players&&mp.players.local;"
            "if(!p)throw 0;"
            "try{p.clearTasksImmediately();}catch(e){}"
            "try{p.clearTasks();}catch(e){}"
            "var h=p.handle;"
            "if(h){"
            "try{mp.game.invoke('0xE1EF3C1216AFF2CD',h);}catch(e){}"
            "try{mp.game.invoke('0xAAA34F8A7CB32098',h);}catch(e){}"
            "}"
            "}catch(e){}";
        const auto status = RageV8Runtime::GetStatus();
        if (status.state != RageV8Runtime::State::Ready ||
            !status.rootCaptured)
            return false;
        return RageV8Runtime::EnqueueFireAndForget(kClearTasksJs);
    }

    bool EnsureFreecamHook() noexcept
    {
        if (g_FcJsHooked)
            return true;
        static constexpr char kHook[] = R"JS((function(){try{
var g=typeof globalThis!=='undefined'?globalThis:this;
if(g.__bfMvH3)return;
g.__bfMvH3=true;
g.__bfMv=g.__bfMv||{freecam:false,noclip:false,paused:false,fcSpeed:1.5,ncSpeed:1.5};
g.__bfKd=function(vk,c){
try{if(mp.keys&&mp.keys.isDown(vk))return true;}catch(e){}
try{return mp.game.controls.getDisabledControlNormal(0,c)>0.4;}catch(e){}
return false;
};
mp.events.add('render',function(){
var s=g.__bfMv;if(!s)return;
var p=mp.players&&mp.players.local;
if(!s.freecam&&s._cam){
try{s._cam.setActive(false);}catch(e){}
try{s._cam.destroy(true);}catch(e){try{s._cam.destroy();}catch(e2){}}
s._cam=null;
try{mp.game.cam.renderScriptCams(false,false,0,true,false,0);}catch(e){}
}
if(!s.freecam&&!s.noclip){
if(s._on&&p){
try{p.freezePosition(false);p.setCollision(true,true);p.setVelocity(0,0,0);}catch(e){}
}
s._on=false;
return;
}
if(!p)return;
s._on=true;
try{p.freezePosition(true);p.setVelocity(0,0,0);}catch(e){}
for(var i=0;i<350;i++){
if(i===1||i===2)continue;
try{mp.game.controls.disableControlAction(0,i,true);}catch(e){}
}
if(s.freecam){
if(!s._cam){
var pos=p.position,rot={x:0,z:0};
try{rot=mp.game.cam.getGameplayCamRot(2);}catch(e){}
s.pitch=rot.x||0;s.yaw=rot.z||0;
try{
s._cam=mp.cameras.new('default',
new mp.Vector3(pos.x,pos.y,pos.z+0.6),
new mp.Vector3(s.pitch,0,s.yaw),50);
s._cam.setActive(true);
mp.game.cam.renderScriptCams(true,false,0,true,false,0);
}catch(e){s._cam=null;return;}
}
if(s.paused||!s._cam)return;
var lx=0,ly=0;
try{lx=mp.game.controls.getDisabledControlNormal(0,1);}catch(e){}
try{ly=mp.game.controls.getDisabledControlNormal(0,2);}catch(e){}
s.yaw-=lx*8;s.pitch-=ly*8;
if(s.pitch>89)s.pitch=89;if(s.pitch<-89)s.pitch=-89;
var yr=s.yaw*0.01745329252,pr=s.pitch*0.01745329252;
var fx=-Math.sin(yr)*Math.cos(pr),fy=Math.cos(yr)*Math.cos(pr),fz=Math.sin(pr);
var rx=Math.cos(yr),ry=Math.sin(yr);
var fast=g.__bfKd(0x10,21)?2.5:1;
var spd=(s.fcSpeed||1.5)*fast*0.45;
var c=s._cam.getCoord();
if(g.__bfKd(0x57,32))c=new mp.Vector3(c.x+fx*spd,c.y+fy*spd,c.z+fz*spd);
if(g.__bfKd(0x53,33))c=new mp.Vector3(c.x-fx*spd,c.y-fy*spd,c.z-fz*spd);
if(g.__bfKd(0x41,34))c=new mp.Vector3(c.x-rx*spd,c.y-ry*spd,c.z);
if(g.__bfKd(0x44,35))c=new mp.Vector3(c.x+rx*spd,c.y+ry*spd,c.z);
if(g.__bfKd(0x20,22))c=new mp.Vector3(c.x,c.y,c.z+spd);
if(g.__bfKd(0x11,36))c=new mp.Vector3(c.x,c.y,c.z-spd);
s._cam.setCoord(c.x,c.y,c.z);
s._cam.setRot(s.pitch,0,s.yaw,2);
return;
}
if(s.paused)return;
try{p.setCollision(false,false);}catch(e){}
var nrot={x:0,y:0,z:0};
try{nrot=mp.game.cam.getGameplayCamRot(2);}catch(e){}
var nyr=(nrot.z||0)*0.01745329252,npr=(nrot.x||0)*0.01745329252;
var nfx=-Math.sin(nyr)*Math.cos(npr),nfy=Math.cos(nyr)*Math.cos(npr),nfz=Math.sin(npr);
var nrx=Math.cos(nyr),nry=Math.sin(nyr);
var nfast=g.__bfKd(0x10,21)?2.5:1;
var nspd=(s.ncSpeed||1.5)*nfast*0.35;
var np=p.position;
if(g.__bfKd(0x57,32))np=new mp.Vector3(np.x+nfx*nspd,np.y+nfy*nspd,np.z+nfz*nspd);
if(g.__bfKd(0x53,33))np=new mp.Vector3(np.x-nfx*nspd,np.y-nfy*nspd,np.z-nfz*nspd);
if(g.__bfKd(0x41,34))np=new mp.Vector3(np.x-nrx*nspd,np.y-nry*nspd,np.z);
if(g.__bfKd(0x44,35))np=new mp.Vector3(np.x+nrx*nspd,np.y+nry*nspd,np.z);
if(g.__bfKd(0x20,22))np=new mp.Vector3(np.x,np.y,np.z+nspd);
if(g.__bfKd(0x11,36))np=new mp.Vector3(np.x,np.y,np.z-nspd);
try{p.setCoordsNoOffset(np.x,np.y,np.z,false,false,false);}catch(e){}
});
}catch(e){}})();)JS";
        if (!EnqueueFreecamScript(kHook))
            return false;
        g_FcJsHooked = true;
        return true;
    }

    bool PushMoveState(
        bool freecam,
        bool noclip,
        bool paused,
        float freecamSpeed,
        float noclipSpeed) noexcept
    {
        if ((freecam || noclip) && !EnsureFreecamHook())
            return false;
        char cmd[320]{};
        sprintf_s(
            cmd,
            "try{var g=typeof globalThis!=='undefined'?globalThis:this;"
            "g.__bfMv=g.__bfMv||{};"
            "g.__bfMv.freecam=%s;g.__bfMv.noclip=%s;g.__bfMv.paused=%s;"
            "g.__bfMv.fcSpeed=%.2f;g.__bfMv.ncSpeed=%.2f;}catch(e){}",
            freecam ? "true" : "false",
            noclip ? "true" : "false",
            paused ? "true" : "false",
            freecamSpeed,
            noclipSpeed);
        return EnqueueFreecamScript(cmd);
    }

    float ClampMoveSpeed(float speed) noexcept
    {
        if (!(speed >= 0.2f))
            return 0.2f;
        if (speed > 20.f)
            return 20.f;
        return speed;
    }

    void SyncMoveScript() noexcept
    {
        const bool wantFc = g_Settings.freecam;
        const bool wantNc = g_Settings.noclip;
        const bool paused = Menu::IsVisible();
        const float fcSpeed = ClampMoveSpeed(g_Settings.freecamSpeed);
        const float ncSpeed = ClampMoveSpeed(g_Settings.noclipSpeed);
        g_Settings.freecamSpeed = fcSpeed;
        g_Settings.noclipSpeed = ncSpeed;

        if (!wantFc && !wantNc)
        {
            if (g_FcJsActive || g_NcJsActive)
            {
                if (PushMoveState(false, false, false, fcSpeed, ncSpeed))
                {
                    g_FcJsActive = false;
                    g_NcJsActive = false;
                }
            }
            return;
        }

        if (!FreecamV8Ready())
            return;

        if (!g_FcJsHooked ||
            g_FcJsActive != wantFc ||
            g_NcJsActive != wantNc ||
            g_FcJsPaused != paused ||
            std::fabs(g_FcJsSpeed - fcSpeed) > 0.05f ||
            std::fabs(g_NcJsSpeed - ncSpeed) > 0.05f)
        {
            if (PushMoveState(wantFc, wantNc, paused, fcSpeed, ncSpeed))
            {
                g_FcJsActive = wantFc;
                g_NcJsActive = wantNc;
                g_FcJsPaused = paused;
                g_FcJsSpeed = fcSpeed;
                g_NcJsSpeed = ncSpeed;
                if (wantNc)
                {
                    static bool loggedNc = false;
                    if (!loggedNc)
                    {
                        loggedNc = true;
                        hooks::HLog(
                            "[Misc] noclip on V8 render "
                            "(close menu, then WASD / Space / Ctrl)");
                    }
                }
            }
        }
    }

    void UnfreezeFreecamPed() noexcept
    {
        if (g_Settings.noclip)
        {
            g_FreecamFrozenPed = 0;
            return;
        }
        if (g_FreecamFrozenPed != 0 &&
            NativeInvoke::Init() &&
            NativeInvoke::DoesEntityExist(g_FreecamFrozenPed))
        {
            NativeInvoke::FreezeEntityPosition(g_FreecamFrozenPed, false);
            NativeInvoke::SetEntityHasGravity(g_FreecamFrozenPed, true);
            NativeInvoke::SetEntityCollision(g_FreecamFrozenPed, true, true);
        }
        g_FreecamFrozenPed = 0;
    }

    void RestoreFreecam() noexcept
    {
        UnfreezeFreecamPed();
        g_FcHavePose = false;
    }

    void ApplyFreecam() noexcept
    {
        if (!g_Settings.freecam)
        {
            RestoreFreecam();
            return;
        }

        if (!FreecamV8Ready())
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                hooks::HLog(
                    "[Misc] freecam/noclip wait for V8 script thread "
                    "(Present cam/coord natives are empty)");
            }
            return;
        }

        if (!NativeInvoke::Init())
            return;

        const int ped = NativeInvoke::PlayerPedId();
        if (ped && NativeInvoke::DoesEntityExist(ped))
        {
            NativeInvoke::FreezeEntityPosition(ped, true);
            NativeInvoke::SetEntityHasGravity(ped, false);
            NativeInvoke::SetEntityCollision(ped, false, false);
            g_FreecamFrozenPed = ped;
        }

        if (!g_FcHavePose)
        {
            float x = 0.f;
            float y = 0.f;
            float z = 0.f;
            float pitch = 0.f;
            float yaw = 0.f;
            if (!NativeInvoke::GetGameplayCamCoord(x, y, z))
            {
                Vector3 pos{};
                uint8_t* entity = TpEntityPtr();
                if (ReadEntityPositionMemory(entity, pos))
                {
                    x = pos.x;
                    y = pos.y;
                    z = pos.z + 0.6f;
                }
            }
            ResolveLookRotation(pitch, yaw);
            g_FcX = x;
            g_FcY = y;
            g_FcZ = z;
            g_FcPitch = pitch;
            g_FcYaw = yaw;
            g_FcHavePose = true;
        }
    }

    bool ResolveTeleportEntity(int& outPed, int& outHandle) noexcept;

    void WarpLocalTo(float x, float y, float z) noexcept
    {
        int ped = 0;
        int handle = 0;
        if (!ResolveTeleportEntity(ped, handle))
            return;
        NativeInvoke::SetEntityCoordsNoOffset(
            handle,
            x,
            y,
            z,
            false,
            false,
            true);
        NativeInvoke::SetEntityVelocity(handle, 0.f, 0.f, 0.f);
        uint8_t* entity = TpEntityPtr();
        if (IsValidPtr(entity))
            SetEntityCoordsMemory(entity, x, y, z);
    }

    bool ClickWarpV8Ready() noexcept
    {
        const auto status = RageV8Runtime::GetStatus();
        if (status.state != RageV8Runtime::State::Ready ||
            !status.rootCaptured)
            return false;
        if (status.generation != g_CwJsGeneration)
        {
            g_CwJsHooked = false;
            g_CwJsOn = false;
            g_CwJsGeneration = status.generation;
        }
        return true;
    }

    bool EnqueueClickWarpScript(const char* script) noexcept
    {
        if (!script || !ClickWarpV8Ready())
            return false;
        return RageV8Runtime::EnqueueFireAndForget(script);
    }

    bool EnsureClickWarpHook() noexcept
    {
        if (g_CwJsHooked)
            return true;
        static constexpr char kHook[] = R"JS((function(){try{
var g=typeof globalThis!=='undefined'?globalThis:this;
if(g.__bfCwH1)return;
g.__bfCwH1=true;
g.__bfCw=g.__bfCw||{on:false,paused:false,key:2};
function pose(){
var mv=g.__bfMv;
if(mv&&mv.freecam&&mv._cam){
try{var c=mv._cam.getCoord();
return {x:c.x,y:c.y,z:c.z,p:mv.pitch||0,yw:mv.yaw||0};}catch(e){}
}
var pos={x:0,y:0,z:0},rot={x:0,z:0};
try{pos=mp.game.cam.getGameplayCamCoord();}catch(e){}
try{rot=mp.game.cam.getGameplayCamRot(2);}catch(e){}
return {x:pos.x||0,y:pos.y||0,z:pos.z||0,p:rot.x||0,yw:rot.z||0};
}
function mark(x,y,z){
try{mp.game.graphics.drawMarker(1,x,y,z,0,0,0,0,0,0,0.75,0.75,0.55,80,180,255,210,false,false,2,false,null,null,false);}catch(e){
try{mp.game.graphics.drawMarker(1,new mp.Vector3(x,y,z),new mp.Vector3(0,0,0),new mp.Vector3(0,0,0),new mp.Vector3(0.75,0.75,0.55),80,180,255,210,false,false,2,false,'','',false);}catch(e2){}}
try{mp.game.graphics.drawMarker(0,x,y,z+1.2,0,0,0,0,0,0,0.42,0.42,0.42,80,180,255,230,false,false,2,false,null,null,false);}catch(e){}
}
function hitAt(ox,oy,oz,fx,fy,fz){
var to=new mp.Vector3(ox+fx*250,oy+fy*250,oz+fz*250);
var from=new mp.Vector3(ox,oy,oz);
try{
var h=mp.raycasting.testPointToPoint(from,to,mp.players.local,1|16|256);
if(h&&h.position)return h.position;
}catch(e){}
try{
var p=mp.players&&mp.players.local;
var ray=mp.game.shapetest.startExpensiveSynchronousShapeTestLosProbe(ox,oy,oz,to.x,to.y,to.z,1|16|256,p?p.handle:0,7);
var r=mp.game.shapetest.getShapeTestResult(ray);
if(r){
var ok=r[1]||r.hit;
var pos=r[2]||r.endCoords;
if(ok&&pos)return pos;
}
}catch(e){}
return null;
}
function warp(x,y,z){
var p=mp.players&&mp.players.local;if(!p)return;
var ent=p;
try{
if(p.vehicle&&p.vehicle.handle){
if(mp.game.vehicle.getPedInVehicleSeat(p.vehicle.handle,-1)===p.handle)ent=p.vehicle;
}
}catch(e){}
try{ent.setCoordsNoOffset(x,y,z,false,false,false);}catch(e){}
}
function keyDown(vk){
try{if(mp.keys&&mp.keys.isDown(vk))return true;}catch(e){}
if(vk===2||vk===1){
try{if(mp.game.controls.isDisabledControlPressed(0,vk===2?25:24))return true;}catch(e){}
try{if(mp.game.controls.isControlPressed(0,vk===2?25:24))return true;}catch(e){}
}
return false;
}
mp.events.add('render',function(){
var s=g.__bfCw;if(!s||!s.on){s&&(s._was=false);return;}
var cam=pose();
var pr=cam.p*0.01745329252,yr=cam.yw*0.01745329252;
var fx=-Math.sin(yr)*Math.cos(pr),fy=Math.cos(yr)*Math.cos(pr),fz=Math.sin(pr);
var hit=hitAt(cam.x,cam.y,cam.z,fx,fy,fz);
if(hit){
mark(hit.x,hit.y,hit.z);
s._hx=hit.x;s._hy=hit.y;s._hz=hit.z;
}
var down=keyDown(s.key|0);
if(down&&!s._was&&!s.paused&&hit)warp(hit.x,hit.y,hit.z+0.6);
s._was=down;
});
}catch(e){}})();)JS";
        if (!EnqueueClickWarpScript(kHook))
            return false;
        g_CwJsHooked = true;
        return true;
    }

    bool PushClickWarpState(bool on, bool paused, int key) noexcept
    {
        if (on && !EnsureClickWarpHook())
            return false;
        if (!g_CwJsHooked && !on)
            return true;
        char cmd[256]{};
        sprintf_s(
            cmd,
            "try{var g=typeof globalThis!=='undefined'?globalThis:this;"
            "g.__bfCw=g.__bfCw||{};"
            "g.__bfCw.on=%s;g.__bfCw.paused=%s;g.__bfCw.key=%d;}catch(e){}",
            on ? "true" : "false",
            paused ? "true" : "false",
            key);
        return EnqueueClickWarpScript(cmd);
    }

    void SyncClickWarpScript() noexcept
    {
        const bool want = g_Settings.clickWarp;
        const bool paused = Menu::IsVisible();
        const int key =
            g_Settings.clickWarpKey > 0 ? g_Settings.clickWarpKey : 0x02;
        if (!want)
        {
            if (g_CwJsOn)
            {
                if (PushClickWarpState(false, false, key))
                    g_CwJsOn = false;
            }
            return;
        }
        if (!ClickWarpV8Ready())
            return;
        if (!g_CwJsHooked ||
            !g_CwJsOn ||
            g_CwJsPaused != paused ||
            g_CwJsKey != key)
        {
            if (PushClickWarpState(true, paused, key))
            {
                g_CwJsOn = true;
                g_CwJsPaused = paused;
                g_CwJsKey = key;
            }
        }
    }

    void UpdateClickWarpHint() noexcept
    {
        g_CwHaveHint = false;
        if (!g_Settings.clickWarp)
            return;

        Vector3 cam{};
        Vector3 look{};
        if (g_Settings.freecam && g_FcHavePose)
        {
            cam = Vector3(g_FcX, g_FcY, g_FcZ);
            Vector3 right{};
            LookVectors(g_FcPitch, g_FcYaw, look, right);
            (void)right;
        }
        else if (!ReadViewportLook(cam, look))
        {
            return;
        }

        Vector3 ped{};
        const bool havePed =
            ReadEntityPositionMemory(TpEntityPtr(), ped);
        float t = 25.f;
        if (havePed && std::fabs(look.z) > 0.045f)
        {
            t = (ped.z - cam.z) / look.z;
            if (!(t >= 3.f && t <= 220.f))
                t = 25.f;
        }
        g_CwHintX = cam.x + look.x * t;
        g_CwHintY = cam.y + look.y * t;
        g_CwHintZ = havePed ? ped.z : (cam.z + look.z * t);
        g_CwHaveHint = true;
    }

    void RenderClickWarpMarker() noexcept
    {
        if (!g_Settings.clickWarp || !g_CwHaveHint)
            return;
        const auto screen = Game::WorldToScreen(
            Vector3(g_CwHintX, g_CwHintY, g_CwHintZ + 0.15f));
        if (!screen)
            return;
        ImDrawList* draw = Overlay::GetDrawList();
        if (!draw)
            return;
        const ImVec2 p(screen->x, screen->y);
        draw->AddCircleFilled(p, 6.5f, IM_COL32(80, 180, 255, 200), 18);
        draw->AddCircle(p, 12.f, IM_COL32(255, 255, 255, 230), 18, 2.2f);
        draw->AddTriangleFilled(
            ImVec2(p.x, p.y - 20.f),
            ImVec2(p.x - 8.f, p.y - 7.f),
            ImVec2(p.x + 8.f, p.y - 7.f),
            IM_COL32(80, 180, 255, 235));
    }

    void ApplyClickWarp() noexcept
    {
        SyncClickWarpScript();
        UpdateClickWarpHint();

        const auto v8 = RageV8Runtime::GetStatus();
        if (v8.state == RageV8Runtime::State::Ready &&
            v8.rootCaptured)
        {
            g_ClickWarpKeyWasDown = false;
            return;
        }

        if (!g_Settings.clickWarp ||
            Menu::IsVisible() ||
            !NativeInvoke::Init())
        {
            g_ClickWarpKeyWasDown = false;
            return;
        }

        Vector3 cam{};
        Vector3 look{};
        if (g_Settings.freecam && g_FcHavePose)
        {
            cam = Vector3(g_FcX, g_FcY, g_FcZ);
            Vector3 right{};
            LookVectors(g_FcPitch, g_FcYaw, look, right);
            (void)right;
        }
        else if (!ReadViewportLook(cam, look))
        {
            float pitch = 0.f;
            float yaw = 0.f;
            if (!NativeInvoke::GetGameplayCamCoord(cam.x, cam.y, cam.z) ||
                !ResolveLookRotation(pitch, yaw))
                return;
            Vector3 right{};
            LookVectors(pitch, yaw, look, right);
            (void)right;
        }

        const float destX = cam.x + look.x * 250.f;
        const float destY = cam.y + look.y * 250.f;
        const float destZ = cam.z + look.z * 250.f;
        const int ignore = NativeInvoke::PlayerPedId();
        const int ray =
            NativeInvoke::StartExpensiveSynchronousShapeTestLosProbe(
                cam.x,
                cam.y,
                cam.z,
                destX,
                destY,
                destZ,
                1 | 16 | 256,
                ignore,
                7);
        float hx = 0.f;
        float hy = 0.f;
        float hz = 0.f;
        if (NativeInvoke::GetShapeTestHitCoords(ray, hx, hy, hz))
        {
            g_CwHintX = hx;
            g_CwHintY = hy;
            g_CwHintZ = hz;
            g_CwHaveHint = true;
        }
        else if (!g_CwHaveHint)
        {
            return;
        }
        else
        {
            hx = g_CwHintX;
            hy = g_CwHintY;
            hz = g_CwHintZ;
        }

        const int key =
            g_Settings.clickWarpKey > 0 ? g_Settings.clickWarpKey : 0x02;
        const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
        if (down && !g_ClickWarpKeyWasDown)
            WarpLocalTo(hx, hy, hz + 0.6f);
        g_ClickWarpKeyWasDown = down;
    }

    void ApplyPlayerNatives() noexcept
    {
        if (!NativeInvoke::Init())
            return;

        const int ped = NativeInvoke::PlayerPedId();
        const int player = NativeInvoke::PlayerId();
        if (g_PendingResetAnim)
        {
            // CLEAR_PED_TASKS* from Present is empty. 5RP uses
            // mp.players.local.clearTasksImmediately() on the script thread.
            if (EnqueueClearTasksScript())
            {
                g_PendingResetAnim = false;
            }
            else
            {
                static bool logged = false;
                if (!logged)
                {
                    logged = true;
                    hooks::HLog(
                        "[Misc] clear tasks waits for V8 script thread");
                }
            }
        }

        if (player)
        {
            if (g_Settings.unlimitedStamina)
                NativeInvoke::RestorePlayerStamina(player, 1.f);
            if (g_Settings.superJump)
                NativeInvoke::SetSuperJumpThisFrame(player);
        }

        if (ped && NativeInvoke::DoesEntityExist(ped))
        {
            const int weapon = NativeInvoke::GetSelectedPedWeapon(ped);
            if (g_Settings.infiniteAmmo)
                NativeInvoke::SetPedInfiniteAmmo(ped, true, weapon);
            if (g_Settings.infiniteClip)
                NativeInvoke::SetPedInfiniteAmmoClip(ped, true);
            if (g_Settings.noReload && weapon)
            {
                const int clip =
                    NativeInvoke::GetMaxAmmoInClip(ped, weapon, true);
                if (clip > 0)
                    NativeInvoke::SetAmmoInClip(ped, weapon, clip);
            }
        }

        if (g_Settings.greenzoneBypass)
        {
            const ULONGLONG now = GetTickCount64();
            if (g_LastGreenzoneTick == 0 ||
                now - g_LastGreenzoneTick >= 2000)
            {
                g_LastGreenzoneTick = now;
                static constexpr char kGreenzoneJs[] =
                    "try{"
                    "var g=typeof globalThis!=='undefined'?globalThis:"
                    "(typeof global!=='undefined'?global:this);"
                    "var api=g.__butterflyFiveRpApi||g.api;"
                    "var data=api&&api.data;"
                    "if(data){"
                    "var list=g.sGreenZoneList;"
                    "if(typeof list==='string'){"
                    "try{list=(api.server&&api.server.strDecompress)?"
                    "JSON.parse(api.server.strDecompress(list)):"
                    "JSON.parse(list);}catch(e){list=null;}"
                    "}"
                    "var ids=[];"
                    "if(Array.isArray(list)){"
                    "for(var i=0;i<list.length;i++){"
                    "var z=list[i];"
                    "var id=z&&(z.uId||z.uid||z.id);"
                    "if(id!=null)ids.push(id);"
                    "}"
                    "}"
                    "if(!ids.length){for(var k=0;k<48;k++)ids.push(k);}"
                    "for(var j=0;j<ids.length;j++){"
                    "var key='greenZoneDisabled'+ids[j];"
                    "if(data.set)data.set(key,true);else data[key]=true;"
                    "}"
                    "}"
                    "}catch(e){}";
                if (RageV8Runtime::GetStatus().state ==
                    RageV8Runtime::State::Ready)
                    RageV8Runtime::EnqueueFireAndForget(kGreenzoneJs);
            }
        }
        else
        {
            g_LastGreenzoneTick = 0;
        }
    }

    static const char* WeatherName(int index) noexcept
    {
        static const char* kNames[] = {
            "EXTRASUNNY", "CLEAR", "CLOUDS", "SMOG", "FOGGY",
            "OVERCAST", "RAIN", "THUNDER", "CLEARING", "NEUTRAL",
            "SNOW", "BLIZZARD", "SNOWLIGHT", "XMAS", "HALLOWEEN"
        };
        if (index < 0 || index >= static_cast<int>(sizeof(kNames) / sizeof(kNames[0])))
            return kNames[0];
        return kNames[index];
    }

    bool WorldV8Ready() noexcept
    {
        const auto status = RageV8Runtime::GetStatus();
        if (status.state != RageV8Runtime::State::Ready ||
            !status.rootCaptured)
            return false;
        if (status.generation != g_WtJsGeneration)
        {
            g_WtJsHooked = false;
            g_WtJsWeather = false;
            g_WtJsTime = false;
            g_WtJsFog = false;
            g_WtJsGeneration = status.generation;
        }
        return true;
    }

    bool EnqueueWorldScript(const char* script) noexcept
    {
        if (!script || !WorldV8Ready())
            return false;
        return RageV8Runtime::EnqueueFireAndForget(script);
    }

    bool EnsureWorldHook() noexcept
    {
        if (g_WtJsHooked)
            return true;
        static constexpr char kHook[] = R"JS((function(){try{
var g=typeof globalThis!=='undefined'?globalThis:this;
if(g.__bfWtH2)return;
g.__bfWtH2=true;
g.__bfWt=g.__bfWt||{wOn:false,w:'CLEAR',tOn:false,h:12,m:0,freeze:false,fog:false};
function put(obj,key,fn){
try{obj[key]=fn;if(obj[key]===fn)return true;}catch(e){}
try{Object.defineProperty(obj,key,{value:fn,writable:true,configurable:true});return true;}catch(e){}
return false;
}
function hashStr(hash){
return (''+hash).toLowerCase();
}
function patch(){
if(g.__bfWtP)return;
var t=mp.game&&mp.game.time,gp=mp.game&&mp.game.gameplay;
if(!t||typeof t.setClockTime!=='function')return;
if(t&&typeof t.setClockTime==='function'&&!t.setClockTime.__bf){
g.__bfOt=t.setClockTime.bind(t);
var wt=function(h,m,s){
var st=g.__bfWt;
if(st&&st.tOn){h=st.h|0;m=st.m|0;s=0;}
if(st&&st._lh===h&&st._lm===m&&st._ls===s)return;
if(st){st._lh=h;st._lm=m;st._ls=s;}
return g.__bfOt(h,m,s);
};
wt.__bf=1;
put(t,'setClockTime',wt);
}
if(t&&typeof t.pauseClock==='function'&&!t.pauseClock.__bf){
g.__bfOp=t.pauseClock.bind(t);
var wp=function(p){
var st=g.__bfWt;
if(st&&st.tOn)p=true;
return g.__bfOp(!!p);
};
wp.__bf=1;
put(t,'pauseClock',wp);
}
if(gp&&typeof gp.setWeatherTypeNowPersist==='function'&&!gp.setWeatherTypeNowPersist.__bf){
g.__bfOw=gp.setWeatherTypeNowPersist.bind(gp);
var ww=function(w){
var st=g.__bfWt;
if(st&&st.wOn&&st.w)w=st.w;
if(st&&st._lw===w)return;
if(st)st._lw=w;
return g.__bfOw(w);
};
ww.__bf=1;
put(gp,'setWeatherTypeNowPersist',ww);
}
if(mp.game.invoke&&!mp.game.invoke.__bf){
g.__bfOi=mp.game.invoke.bind(mp.game);
var wi=function(hash){
var st=g.__bfWt;
var hs=hashStr(hash);
    if(st&&st.tOn&&(hs==='0x47c3b5848c3e45d8'||hs==='0xe679e3e06e363892')){
      var h=st.h|0,m=st.m|0;
      if(st._lh===h&&st._lm===m&&st._ls===0)return;
      st._lh=h;st._lm=m;st._ls=0;
      return g.__bfOi(hash,h,m,0);
    }
    if(st&&st.tOn&&hs==='0x4055e40bd2dbec1d')
      return g.__bfOi(hash,true);
    if(st&&st.wOn&&st.w&&hs==='0xed712ca327900c8a'){
if(st._lw===st.w)return;
st._lw=st.w;
return g.__bfOi(hash,st.w);
}
return g.__bfOi.apply(mp.game,arguments);
};
wi.__bf=1;
put(mp.game,'invoke',wi);
}
g.__bfWtP=1;
}
function applyTime(){
var s=g.__bfWt;if(!s||!s.tOn)return;
var h=s.h|0,m=s.m|0;
if(g.__bfOt&&(s._lh!==h||s._lm!==m||s._ls!==0)){
s._lh=h;s._lm=m;s._ls=0;
try{g.__bfOt(h,m,0);}catch(e){}
}
try{g.serverGameHour=h;g.serverGameMin=m;}catch(e){}
try{if(g.__bfOp)g.__bfOp(true);}catch(e){}
}
function applyWeather(){
var s=g.__bfWt;if(!s||!s.wOn||!s.w)return;
if(s._lw!==s.w){
s._lw=s.w;
try{if(g.__bfOw)g.__bfOw(s.w);}catch(e){}
try{mp.game.gameplay.setWeatherTypeNow(s.w);}catch(e){}
}
try{
var api=g.__butterflyFiveRpApi||g.api;
if(api&&api.data){
if(api.data.set)api.data.set('serverWeather',s.w);
else api.data.serverWeather=s.w;
}
}catch(e){}
}
patch();
mp.events.add('render',function(){
var s=g.__bfWt;if(!s)return;
if(!g.__bfOt)patch();
if(s.wOn&&s.w){s._wWas=true;applyWeather();}
else if(s._wWas){
s._wWas=false;s._lw=null;
try{mp.game.gameplay.clearWeatherTypePersist();}catch(e){}
try{mp.game.gameplay.clearOverrideWeather();}catch(e){}
}
if(s.tOn){s._tWas=true;applyTime();}
else if(s._tWas){
s._tWas=false;s._lh=s._lm=s._ls=undefined;
try{if(g.__bfOp)g.__bfOp(false);}catch(e){}
}
if(s.fog){
if(!s._fWas){
s._fWas=true;
try{mp.game.graphics.clearTimecycleModifier();}catch(e){}
try{mp.game.graphics.setTimecycleModifierStrength(0);}catch(e){}
}
}else if(s._fWas){
s._fWas=false;
try{mp.game.invoke('0x0AE73D8DF3A762B2',false);}catch(e){}
}
});
}catch(e){}})();)JS";
        if (!EnqueueWorldScript(kHook))
            return false;
        g_WtJsHooked = true;
        return true;
    }

    bool PushWorldState(
        bool weatherOn,
        const char* weather,
        bool timeOn,
        int hour,
        int minute,
        bool freeze,
        bool fog) noexcept
    {
        if ((weatherOn || timeOn || fog) && !EnsureWorldHook())
            return false;
        if (!g_WtJsHooked && !weatherOn && !timeOn && !fog)
            return true;
        if (!weather || !weather[0])
            weather = "CLEAR";
        char cmd[448]{};
        sprintf_s(
            cmd,
            "try{var g=typeof globalThis!=='undefined'?globalThis:this;"
            "g.__bfWt=g.__bfWt||{};"
            "g.__bfWt.wOn=%s;g.__bfWt.w='%s';"
            "g.__bfWt.tOn=%s;g.__bfWt.h=%d;g.__bfWt.m=%d;g.__bfWt.freeze=%s;"
            "g.__bfWt.fog=%s;g.__bfWt._lh=undefined;g.__bfWt._lw=null;}catch(e){}",
            weatherOn ? "true" : "false",
            weather,
            timeOn ? "true" : "false",
            hour,
            minute,
            freeze ? "true" : "false",
            fog ? "true" : "false");
        return EnqueueWorldScript(cmd);
    }

    void SyncWorldScript() noexcept
    {
        if (g_Settings.weatherIndex < 0)
            g_Settings.weatherIndex = 0;
        if (g_Settings.weatherIndex > 14)
            g_Settings.weatherIndex = 14;
        if (g_Settings.timeHour < 0)
            g_Settings.timeHour = 0;
        if (g_Settings.timeHour > 23)
            g_Settings.timeHour = 23;
        if (g_Settings.timeMinute < 0)
            g_Settings.timeMinute = 0;
        if (g_Settings.timeMinute > 59)
            g_Settings.timeMinute = 59;

        const bool wantW = g_Settings.weatherOverride;
        const bool wantT = g_Settings.timeOverride;
        const bool wantF = g_Settings.noFog;
        const bool freeze = wantT && g_Settings.freezeTime;
        const int wIdx = g_Settings.weatherIndex;
        const int hour = g_Settings.timeHour;
        const int minute = g_Settings.timeMinute;

        if (!wantW && !wantT && !wantF)
        {
            if (g_WtJsWeather || g_WtJsTime || g_WtJsFog)
            {
                if (PushWorldState(false, "CLEAR", false, hour, minute, false, false))
                {
                    g_WtJsWeather = false;
                    g_WtJsTime = false;
                    g_WtJsFog = false;
                }
            }
            return;
        }

        if (!WorldV8Ready())
        {
            static bool logged = false;
            if (!logged)
            {
                logged = true;
                hooks::HLog(
                    "[Misc] weather/time wait for V8 script thread");
            }
            return;
        }

        if (!g_WtJsHooked ||
            g_WtJsWeather != wantW ||
            g_WtJsTime != wantT ||
            g_WtJsFog != wantF ||
            g_WtJsFreeze != freeze ||
            g_WtJsWeatherIdx != wIdx ||
            g_WtJsHour != hour ||
            g_WtJsMinute != minute)
        {
            if (PushWorldState(
                    wantW,
                    WeatherName(wIdx),
                    wantT,
                    hour,
                    minute,
                    freeze,
                    wantF))
            {
                g_WtJsWeather = wantW;
                g_WtJsTime = wantT;
                g_WtJsFog = wantF;
                g_WtJsFreeze = freeze;
                g_WtJsWeatherIdx = wIdx;
                g_WtJsHour = hour;
                g_WtJsMinute = minute;
            }
        }
    }

    void ApplyWorldNatives() noexcept
    {
        SyncWorldScript();

        const auto v8 = RageV8Runtime::GetStatus();
        if (v8.state == RageV8Runtime::State::Ready &&
            v8.rootCaptured)
            return;

        if (!NativeInvoke::Init())
            return;

        if (g_Settings.weatherOverride)
        {
            NativeInvoke::SetWeatherTypeNowPersist(
                WeatherName(g_Settings.weatherIndex));
            g_WeatherWasOn = true;
        }
        else if (g_WeatherWasOn)
        {
            NativeInvoke::ClearWeatherTypePersist();
            NativeInvoke::ClearOverrideWeather();
            g_WeatherWasOn = false;
        }

        if (g_Settings.timeOverride)
        {
            NativeInvoke::NetworkOverrideClockTime(
                g_Settings.timeHour,
                g_Settings.timeMinute,
                0);
            NativeInvoke::PauseClock(g_Settings.freezeTime);
            g_TimeWasOn = true;
        }
        else if (g_TimeWasOn)
        {
            NativeInvoke::PauseClock(false);
            NativeInvoke::NetworkClearClockTimeOverride();
            g_TimeWasOn = false;
        }

        if (g_Settings.noFog)
        {
            NativeInvoke::ClearTimecycleModifier();
            NativeInvoke::SetTimecycleModifierStrength(0.f);
            NativeInvoke::SetFarShadowsSuppressed(true);
            g_FogWasOn = true;
        }
        else if (g_FogWasOn)
        {
            NativeInvoke::SetFarShadowsSuppressed(false);
            g_FogWasOn = false;
        }
    }

    bool IsSaneMapXY(float x, float y) noexcept
    {
        // Rough GTA V map bounds — filters garbage Vector3 reads.
        // Reject exact (0,0): native Vector3 failure / empty waypoint.
        if (x == 0.f && y == 0.f)
            return false;
        return x > -6000.f && x < 6000.f && y > -6000.f && y < 10000.f;
    }

    bool IsSaneGroundZ(float z) noexcept
    {
        // GTA terrain spans below sea level through the Chiliad summit. Keep
        // enough range for real terrain while rejecting probe/sky results.
        return std::isfinite(z) && z > -200.f && z < 900.f;
    }

    bool ResolveWaypointGroundZ(float x, float y, float& outZ) noexcept
    {
        float groundZ = 0.f;
        if (NativeInvoke::GetGroundZFor3dCoord(
                x,
                y,
                kTpWpProbeZ,
                groundZ,
                false) &&
            IsSaneGroundZ(groundZ))
        {
            outZ = groundZ + 1.f;
            return IsSaneGroundZ(outZ);
        }

        // Loaded map/object collision can still resolve when GET_GROUND_Z is
        // unavailable for the current runtime build.
        if (NativeInvoke::RaycastGroundZ(
                x,
                y,
                groundZ,
                kTpWpProbeZ,
                -200.f) &&
            IsSaneGroundZ(groundZ))
        {
            outZ = groundZ + 1.f;
            return IsSaneGroundZ(outZ);
        }
        return false;
    }

    bool ResolveTeleportEntity(int& outPed, int& outHandle) noexcept
    {
        outPed = 0;
        outHandle = 0;
        if (!NativeInvoke::Init())
            return false;

        const int ped = NativeInvoke::PlayerPedId();
        if (!ped || !NativeInvoke::DoesEntityExist(ped))
            return false;

        int handle = ped;
        const int vehicle =
            NativeInvoke::GetVehiclePedIsIn(ped, false);
        if (vehicle)
        {
            if (!NativeInvoke::DoesEntityExist(vehicle) ||
                NativeInvoke::GetPedInVehicleSeat(vehicle, -1) != ped)
            {
                return false;
            }
            handle = vehicle;
        }

        if (!NativeInvoke::DoesEntityExist(handle))
            return false;
        outPed = ped;
        outHandle = handle;
        return true;
    }

    bool IsPreparedTeleportEntityValid() noexcept
    {
        int ped = 0;
        int handle = 0;
        return ResolveTeleportEntity(ped, handle) &&
            ped == g_TpWpPed &&
            handle == g_TpWpHandle;
    }

    void RequestWaypointCollision() noexcept
    {
        NativeInvoke::SetFocusPosAndVel(
            g_TpWpX,
            g_TpWpY,
            kTpWpProbeZ);
        NativeInvoke::RequestCollisionAtCoord(
            g_TpWpX,
            g_TpWpY,
            0.f);
        NativeInvoke::RequestAdditionalCollisionAtCoord(
            g_TpWpX,
            g_TpWpY,
            kTpWpProbeZ);
    }

    void FinishTeleportWaypoint() noexcept
    {
        NativeInvoke::ClearFocus();
        g_TpWpPhase = TpWpPhase::Idle;
        g_TpWpX = 0.f;
        g_TpWpY = 0.f;
        g_TpWpPed = 0;
        g_TpWpHandle = 0;
        g_TpWpStartedAt = 0;
    }

    bool WarpPreparedTeleport(float z) noexcept
    {
        if (!IsSaneGroundZ(z) ||
            !IsPreparedTeleportEntityValid())
        {
            return false;
        }

        NativeInvoke::SetEntityCoordsNoOffset(
            g_TpWpHandle,
            g_TpWpX,
            g_TpWpY,
            z,
            false,
            false,
            true);
        NativeInvoke::SetEntityVelocity(
            g_TpWpHandle,
            0.f,
            0.f,
            0.f);
        return true;
    }

    // Returns: 1=probing, 0=no mark, -1=no controllable entity,
    // -2=native runtime unavailable.
    int BeginTeleportToWaypoint() noexcept
    {
        if (g_TpWpPhase != TpWpPhase::Idle)
            FinishTeleportWaypoint();

        float x = 0.f;
        float y = 0.f;
        float blipZ = 0.f;
        if (!NativeInvoke::FindWaypointCoords(x, y, blipZ))
        {
            // Native fallback only if invoker is alive.
            if (NativeInvoke::Init())
            {
                const int blip = NativeInvoke::GetFirstBlipInfoId(8);
                if (blip && NativeInvoke::DoesBlipExist(blip))
                {
                    if (!NativeInvoke::GetBlipInfoIdCoord(blip, x, y, blipZ))
                        NativeInvoke::GetBlipCoords(blip, x, y, blipZ);
                }
            }
        }
        if (!IsSaneMapXY(x, y))
            return 0;
        (void)blipZ;

        if (!NativeInvoke::Init())
            return -2;
        if (!ResolveTeleportEntity(g_TpWpPed, g_TpWpHandle))
            return -1;

        g_TpWpX = x;
        g_TpWpY = y;
        g_TpWpStartedAt = GetTickCount64();
        g_TpWpPhase = TpWpPhase::Probing;
        RequestWaypointCollision();
        return 1;
    }

    bool TickTeleportWaypointSettle(bool& outOk) noexcept
    {
        outOk = false;
        if (g_TpWpPhase != TpWpPhase::Probing)
            return true;

        if (!IsPreparedTeleportEntityValid())
        {
            FinishTeleportWaypoint();
            return true;
        }

        RequestWaypointCollision();

        float groundZ = 0.f;
        if (ResolveWaypointGroundZ(
                g_TpWpX,
                g_TpWpY,
                groundZ))
        {
            outOk = WarpPreparedTeleport(groundZ);
            FinishTeleportWaypoint();
            return true;
        }

        if (GetTickCount64() - g_TpWpStartedAt >=
            kTpWpTimeoutMs)
        {
            // No arbitrary altitude fallback: the entity has never moved.
            FinishTeleportWaypoint();
            return true;
        }
        return false;
    }

    // ---- Camera FOV / aspect (GTA5.exe only; never touch RAGE/JS) ----
    // Crash history: writing FPS/Mounted metadata (+0x22C OOB) and rewriting
    // full projection FOV from Present raced the renderer. Keep this path
    // extremely narrow: follow-meta FOV with backup/restore, and aspect as
    // P[0]-only scale against the game's current P[5].

    bool LooksHeapPtr(const void* p) noexcept
    {
        const auto a = reinterpret_cast<uintptr_t>(p);
        if (a < 0x10000 || a >= 0x00007FFFFFFEFFFFULL)
            return false;
        if ((a & 0xF) != 0)
            return false;
        if (a == 0xFFFFFFFFULL || (a >> 32) == 0xFFFFFFFFULL)
            return false;
        return true;
    }

    bool ReadF32(uint8_t* base, uint32_t off, float& out) noexcept
    {
        if (!base)
            return false;
        __try {
            out = *reinterpret_cast<float*>(base + off);
            return std::isfinite(out);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool WriteF32(uint8_t* base, uint32_t off, float value) noexcept
    {
        if (!base || !std::isfinite(value))
            return false;
        __try {
            *reinterpret_cast<float*>(base + off) = value;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool LooksLikeFov(float v) noexcept
    {
        return std::isfinite(v) && v >= 10.f && v <= 120.f;
    }

    bool LooksLikeNearClip(float v) noexcept
    {
        return std::isfinite(v) && v > 0.01f && v < 2.5f;
    }

    struct FovBackupEntry final
    {
        uint8_t* meta{};
        float originalFov{};
        bool valid{};
    };

    constexpr int kMaxFovBackups = 4;
    FovBackupEntry g_FovBackups[kMaxFovBackups]{};
    int g_FovBackupCount = 0;
    bool g_FovApplied = false;

    // Common gameplay-director cam slots (third-person / vehicle follow).
    static constexpr uint32_t kFollowCamSlots[] = {
        Offsets::GameplayDirector::FollowPedCamera, // 0x2C0
        0x2D0,
        0x2E0,
    };

    bool ReadFollowMeta(
        uint8_t* meta,
        float& fov,
        float& nearClip) noexcept
    {
        if (!LooksHeapPtr(meta))
            return false;
        if (!ReadF32(meta, Offsets::GameplayDirector::FollowFov, fov) ||
            !ReadF32(meta, Offsets::GameplayDirector::FollowNearClip, nearClip))
            return false;
        return LooksLikeFov(fov) && LooksLikeNearClip(nearClip);
    }

    uint8_t* GetGameplayDirector() noexcept
    {
        if (!Offsets::GameplayDirectorPtr ||
            !LooksHeapPtr(Offsets::GameplayDirectorPtr))
            return nullptr;
        uint8_t* director = nullptr;
        __try {
            director = *Offsets::GameplayDirectorPtr;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
        if (!LooksHeapPtr(director))
            return nullptr;

        // Reject wrong pattern hits: primary follow slot must expose
        // follow-camera metadata (Fov@0x30 + NearClip@0x34).
        uint8_t* cam = nullptr;
        __try {
            cam = *reinterpret_cast<uint8_t**>(
                director + Offsets::GameplayDirector::FollowPedCamera);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
        if (!LooksHeapPtr(cam))
            return nullptr;

        uint8_t* meta = nullptr;
        __try {
            meta = *reinterpret_cast<uint8_t**>(
                cam + Offsets::GameplayDirector::CameraMetadata);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
        float fov = 0.f;
        float nearClip = 0.f;
        if (!ReadFollowMeta(meta, fov, nearClip))
            return nullptr;
        return director;
    }

    void ClearFovBackups() noexcept
    {
        for (int i = 0; i < kMaxFovBackups; ++i)
            g_FovBackups[i] = {};
        g_FovBackupCount = 0;
        g_FovApplied = false;
    }

    void RestoreFovBackups() noexcept
    {
        for (int i = 0; i < g_FovBackupCount; ++i) {
            auto& b = g_FovBackups[i];
            if (!b.valid || !LooksHeapPtr(b.meta) || !LooksLikeFov(b.originalFov))
                continue;
            float cur = 0.f;
            float nearClip = 0.f;
            // Only restore if the slot still looks like follow metadata.
            if (!ReadFollowMeta(b.meta, cur, nearClip))
                continue;
            WriteF32(
                b.meta,
                Offsets::GameplayDirector::FollowFov,
                b.originalFov);
        }
        ClearFovBackups();
    }

    int FindFovBackup(uint8_t* meta) noexcept
    {
        for (int i = 0; i < g_FovBackupCount; ++i) {
            if (g_FovBackups[i].valid && g_FovBackups[i].meta == meta)
                return i;
        }
        return -1;
    }

    void ApplyFollowMetadataFov(float fov) noexcept
    {
        if (!LooksLikeFov(fov))
            return;
        uint8_t* director = GetGameplayDirector();
        if (!director)
            return;

        for (uint32_t slot : kFollowCamSlots) {
            uint8_t* cam = nullptr;
            __try {
                cam = *reinterpret_cast<uint8_t**>(director + slot);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }
            if (!LooksHeapPtr(cam))
                continue;

            uint8_t* meta = nullptr;
            __try {
                meta = *reinterpret_cast<uint8_t**>(
                    cam + Offsets::GameplayDirector::CameraMetadata);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }

            float curFov = 0.f;
            float nearClip = 0.f;
            if (!ReadFollowMeta(meta, curFov, nearClip))
                continue;

            // Backup the game's original once per metadata pointer.
            if (FindFovBackup(meta) < 0 && g_FovBackupCount < kMaxFovBackups) {
                auto& b = g_FovBackups[g_FovBackupCount++];
                b.meta = meta;
                b.originalFov = curFov;
                b.valid = true;
            }

            if (std::fabs(curFov - fov) > 0.01f)
                WriteF32(meta, Offsets::GameplayDirector::FollowFov, fov);
        }

        g_FovApplied = g_FovBackupCount > 0;
    }

    bool ReadViewportProjection(float*& outP, float& p0, float& p5) noexcept
    {
        outP = nullptr;
        p0 = 0.f;
        p5 = 0.f;
        if (!Offsets::ViewportPtr || !LooksHeapPtr(Offsets::ViewportPtr))
            return false;

        uint8_t* viewport = nullptr;
        __try {
            viewport = *Offsets::ViewportPtr;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        if (!LooksHeapPtr(viewport))
            return false;

        float* p = nullptr;
        __try {
            p = reinterpret_cast<float*>(
                viewport + Offsets::Viewport::ProjectionMatrix);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        if (!p)
            return false;

        float a = 0.f;
        float b = 0.f;
        float c = 0.f;
        float d = 0.f;
        if (!ReadF32(reinterpret_cast<uint8_t*>(p), 0, a) ||
            !ReadF32(reinterpret_cast<uint8_t*>(p), 4, c) ||
            !ReadF32(reinterpret_cast<uint8_t*>(p), 16, d) ||
            !ReadF32(reinterpret_cast<uint8_t*>(p), 20, b))
            return false;

        // Must look like a perspective scale matrix: diagonal scales, zeros off.
        if (!(a > 0.15f && a < 12.f && b > 0.15f && b < 12.f))
            return false;
        if (std::fabs(c) > 0.02f || std::fabs(d) > 0.02f)
            return false;

        outP = p;
        p0 = a;
        p5 = b;
        return true;
    }

    // Aspect memory writes were crashing the renderer on inject when the
    // saved config re-enabled them. Keep the UI, but do not touch GPU/camera
    // memory for aspect until a safe pre-render hook exists.
    void ApplyAspectRatioOnly(float /*aspect*/) noexcept {}

    int g_CameraWarmupFrames = 0;

    bool CameraWorldReady() noexcept
    {
        // Never poke camera memory during early inject / loading screens.
        if (g_CameraWarmupFrames < 180) {
            ++g_CameraWarmupFrames;
            return false;
        }
        if (!Game::GetLocalPed())
            return false;
        float* p = nullptr;
        float p0 = 0.f;
        float p5 = 0.f;
        return ReadViewportProjection(p, p0, p5);
    }

    void ApplyCameraTweaks() noexcept
    {
        // Always keep aspect inert; FOV writes are quarantined after the
        // recurring Render-thread INVALID_POINTER_READ crashes.
        g_Settings.aspectEnabled = false;

        if (!kAllowCameraMemoryWrites) {
            if (g_FovApplied)
                RestoreFovBackups();
            if (g_Settings.cameraFovEnabled) {
                static bool logged = false;
                if (!logged) {
                    logged = true;
                    hooks::HLog(
                        "[Misc] FOV memory writer quarantined "
                        "(renderer AV GTA5+0x79eef6 / +0x13dd6d6)");
                }
                g_Settings.cameraFovEnabled = false;
            }
            return;
        }

        const bool fovOn = g_Settings.cameraFovEnabled;

        if (!fovOn) {
            if (g_FovApplied)
                RestoreFovBackups();
            return;
        }

        // Saved configs may re-enable FOV on inject — wait until the world
        // camera is actually alive before touching metadata.
        if (!CameraWorldReady())
            return;

        float fov = g_Settings.cameraFov;
        if (fov < 10.f)
            fov = 10.f;
        if (fov > 120.f)
            fov = 120.f;
        ApplyFollowMetadataFov(fov);
    }

    void SyncToScript() noexcept
    {
        bool startedWaypointProbe = false;

        SyncBoolCmd(
            g_Settings.godMode,
            g_LastSynced.godMode,
            "misc_god:on",
            "misc_god:off");
        // Combat (no spread / recoil / reload) is CWeaponInfo only — no JS.
        // Noclip / freecam run on our V8 render hook. Do not Quant-sync them.
        g_Settings.disableCarCollision = false;
        g_Settings.disablePlayerCollision = false;
        SyncFloatCmd(
            g_Settings.fixedHp,
            g_Settings.fixedHpValue,
            g_LastSynced.fixedHp,
            g_LastSynced.fixedHpValue,
            "misc_fixedhp");
        SyncFloatCmd(
            g_Settings.fixedAp,
            g_Settings.fixedApValue,
            g_LastSynced.fixedAp,
            g_LastSynced.fixedApValue,
            "misc_fixedap");

        if (g_PendingSuicide) {
            g_PendingSuicide = false;
            Queue("misc_suicide");
        }
        // Reset animations is V8 clearTasksImmediately in ApplyPlayerNatives.
        if (g_PendingHeal) {
            g_PendingHeal = false;
            Queue("misc_heal");
        }
        if (g_PendingArmor) {
            g_PendingArmor = false;
            Queue("misc_armor");
        }
        if (g_PendingRespawn) {
            g_PendingRespawn = false;
            Queue("misc_respawn");
        }
        if (g_PendingRepairVehicle) {
            g_PendingRepairVehicle = false;
            Queue("misc_repair");
        }
        if (g_PendingTpWaypoint) {
            g_PendingTpWaypoint = false;
            const int start = BeginTeleportToWaypoint();
            if (start == 1)
            {
                startedWaypointProbe = true;
                Menu::PushNotification(
                    "TP Waypoint",
                    "Resolving destination ground…",
                    true,
                    1500);
            }
            else if (start == 0)
            {
                const bool listOk = NativeInvoke::InitBlipList();
                Menu::PushNotification(
                    "TP Waypoint",
                    listOk ? "No GPS mark (open map, set waypoint)"
                           : "BlipList pattern missing",
                    false,
                    3000);
            }
            else if (start == -1)
            {
                Menu::PushNotification(
                    "TP Waypoint",
                    "No controllable ped or driver vehicle",
                    false,
                    2500);
            }
            else
            {
                Menu::PushNotification(
                    "TP Waypoint",
                    "Native waypoint runtime unavailable",
                    false,
                    3000);
            }
        }

        if (g_TpWpPhase == TpWpPhase::Probing &&
            !startedWaypointProbe)
        {
            bool ok = false;
            if (TickTeleportWaypointSettle(ok))
            {
                if (ok)
                {
                    Menu::PushNotification(
                        "TP Waypoint",
                        "Teleported to ground",
                        true,
                        2000);
                }
                else
                {
                    Menu::PushNotification(
                        "TP Waypoint",
                        "Ground not found; teleport cancelled",
                        false,
                        3000);
                }
            }
        }

        g_HaveSynced = true;
        g_LastSynced = g_Settings;
    }
} // namespace

Settings& GetSettings() noexcept
{
    return g_Settings;
}

void Initialize() noexcept
{
    if (g_TpWpPhase != TpWpPhase::Idle)
        FinishTeleportWaypoint();
    g_HaveSynced = false;
    g_LastSynced = {};
    g_PendingRepairVehicle = false;
    g_LastAntiAfkTick = 0;
    ClearFovBackups();
    g_CameraWarmupFrames = 0;
    // Do not auto-arm camera writers from a previous session.
    g_Settings.aspectEnabled = false;
    g_Settings.cameraFovEnabled = false;
    g_Settings.disableCarCollision = false;
    g_Settings.disablePlayerCollision = false;

    // VehicleMovement is the sole owner of vehicle speed/gravity.
    VehicleMovement::GetSettings().unlockSpeedLimit = false;
}

void Shutdown() noexcept
{
    if (g_TpWpPhase != TpWpPhase::Idle)
        FinishTeleportWaypoint();
    RestoreNoclipEntity();
    RestoreFreecam();
    if (g_FcJsHooked)
        PushMoveState(false, false, false, 5.f, 5.f);
    g_FcJsActive = false;
    g_NcJsActive = false;
    if (g_WtJsHooked)
        PushWorldState(false, "CLEAR", false, 12, 0, false, false);
    g_WtJsWeather = false;
    g_WtJsTime = false;
    g_WtJsFog = false;
    if (g_CwJsHooked)
        PushClickWarpState(false, false, 2);
    g_CwJsOn = false;
    g_PendingTpWaypoint = false;
    g_PendingRepairVehicle = false;
    VehicleMovement::Shutdown();
}

void Update() noexcept
{
    HandleKeyToggles();
    ApplyNoclip();
    ApplyFreecam();
    SyncMoveScript();
    ApplyPlayerNatives();
    ApplyClickWarp();
    ApplyWorldNatives();
    ApplyFixedStats();
    ApplySpeedhack();
    ApplyAntiAfk();
    ApplyWeaponCombatPatches();
    ApplyCameraTweaks();
    VehicleMovement::Update();
    SyncToScript();
}

void RequestSuicide() noexcept
{
    g_PendingSuicide = true;
}

void RequestResetAnimations() noexcept
{
    g_PendingResetAnim = true;
}

void RequestHeal() noexcept
{
    g_PendingHeal = true;
    CPed* ped = Game::GetLocalPed();
    if (!ped)
        return;
    __try {
        *reinterpret_cast<float*>(
            reinterpret_cast<uint8_t*>(ped) + Offsets::Ped::Health) =
            200.f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void RequestArmor() noexcept
{
    g_PendingArmor = true;
    CPed* ped = Game::GetLocalPed();
    if (!ped)
        return;
    __try {
        *reinterpret_cast<float*>(
            reinterpret_cast<uint8_t*>(ped) + Offsets::Ped::Armor) =
            100.f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

void RequestRespawn() noexcept
{
    g_PendingRespawn = true;
}

void RequestRepairVehicle() noexcept
{
    g_PendingRepairVehicle = true;
}

bool RequestTeleportWaypoint() noexcept
{
    g_PendingTpWaypoint = true;
    return true;
}

void RenderPlayersOverlay() noexcept
{
    RenderClickWarpMarker();
    if (!g_Settings.showPlayers && !g_Settings.showAdmins)
        return;

    RagePlayerNames::SnapshotEntry entries[128]{};
    const int count = RagePlayerNames::CopySnapshot(
        entries,
        static_cast<int>(sizeof(entries) / sizeof(entries[0])));
    if (count <= 0)
        return;

    CPed* localPed = Game::GetLocalPed();
    Vector3 localPos{};
    bool haveLocal = false;
    if (localPed) {
        __try {
            localPos = localPed->GetPosition();
            haveLocal = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            haveLocal = false;
        }
    }

    // Position lives in our config, applied once and then whenever it changes
    // underneath us (config import), otherwise re-applying every frame would
    // fight the drag.
    static float s_AppliedX = -2.f;
    static float s_AppliedY = -2.f;
    if (g_Settings.aroundPosX != s_AppliedX ||
        g_Settings.aroundPosY != s_AppliedY)
    {
        s_AppliedX = g_Settings.aroundPosX;
        s_AppliedY = g_Settings.aroundPosY;
        ImGui::SetNextWindowPos(
            ImVec2(
                g_Settings.aroundPosX >= 0.f
                    ? g_Settings.aroundPosX
                    : 20.f,
                g_Settings.aroundPosY >= 0.f
                    ? g_Settings.aroundPosY
                    : 120.f),
            ImGuiCond_Always);
    }

    // Only grabbable while the menu is up: with the menu closed the mouse
    // belongs to the game, and a panel that eats clicks there is a liability.
    const bool menuOpen = Menu::IsVisible();
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_AlwaysAutoResize;
    if (!menuOpen)
    {
        flags |= ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoInputs;
    }

    ImGui::SetNextWindowBgAlpha(0.78f);
    ImGui::SetNextWindowSize(
        ImVec2(226.f, 0.f), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 9.f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding, ImVec2(13.f, 11.f));
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing, ImVec2(8.f, 6.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    if (!ImGui::Begin("Around", nullptr, flags))
    {
        ImGui::End();
        ImGui::PopStyleVar(4);
        return;
    }

    if (menuOpen)
    {
        const ImVec2 moved = ImGui::GetWindowPos();
        if (moved.x != s_AppliedX || moved.y != s_AppliedY)
        {
            s_AppliedX = moved.x;
            s_AppliedY = moved.y;
            g_Settings.aroundPosX = moved.x;
            g_Settings.aroundPosY = moved.y;
        }
    }

    // Header, plus a hint that the panel can be moved while the menu is open.
    {
        ImDrawList* header = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::TextColored(
            ImVec4(0.62f, 0.66f, 0.74f, 1.f),
            "AROUND");
        if (menuOpen)
        {
            const char* hint = "drag";
            const ImVec2 size = ImGui::CalcTextSize(hint);
            header->AddText(
                ImVec2(
                    origin.x + width - size.x,
                    origin.y + 1.f),
                IM_COL32(150, 158, 172, 130),
                hint);
        }
        const float lineY =
            ImGui::GetCursorScreenPos().y + 2.f;
        header->AddLine(
            ImVec2(origin.x, lineY),
            ImVec2(origin.x + width, lineY),
            IM_COL32(255, 255, 255, 26),
            1.f);
        ImGui::Dummy(ImVec2(0.f, 4.f));
    }

    // label on the left, count hard right: the eye finds the number without
    // reading the line.
    const auto countRow =
        [](const char* label, int value) noexcept {
            ImGui::TextColored(
                ImVec4(0.78f, 0.81f, 0.86f, 1.f),
                "%s",
                label);
            char number[16]{};
            sprintf_s(number, "%d", value);
            const float width =
                ImGui::GetContentRegionAvail().x;
            const ImVec2 size = ImGui::CalcTextSize(number);
            ImGui::SameLine(0.f, 0.f);
            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() + width - size.x);
            ImGui::TextColored(
                ImVec4(1.f, 1.f, 1.f, 1.f),
                "%s",
                number);
        };

    const float maxDist = g_Settings.playersDistance > 1.f
        ? g_Settings.playersDistance
        : 300.f;

    // Counters, not a roster: the per-player dump was noise for everyone who
    // just wants to know whether somebody is nearby.
    int playersAround = 0;
    int adminsAround = 0;
    for (int i = 0; i < count; ++i) {
        const auto& e = entries[i];
        if (e.local ||
            e.kind != RagePlayerNames::EntityKind::Player)
            continue;
        float dist = 0.f;
        if (haveLocal) {
            const float dx = e.x - localPos.x;
            const float dy = e.y - localPos.y;
            const float dz = e.z - localPos.z;
            dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        if (dist > maxDist)
            continue;
        ++playersAround;
        if (e.adminLevel > Menu::s_AdminLevelThreshold)
            ++adminsAround;
    }

    if (g_Settings.showPlayers)
        countRow("Players around", playersAround);
    if (g_Settings.showAdmins)
        countRow("Admins around", adminsAround);

    // Server-wide admins by name. This list is not limited to the stream, so
    // it is the only place an admin outside render range shows up at all.
    if (g_Settings.showAdmins) {
        FiveRpAdminScan::Entry admins[FiveRpAdminScan::kCapacity]{};
        const int adminCount = FiveRpAdminScan::CopyList(
            admins,
            static_cast<int>(sizeof(admins) / sizeof(admins[0])));
        if (adminCount > 0) {
            ImGui::Dummy(ImVec2(0.f, 2.f));
            countRow("Admins on server", adminCount);
            ImGui::Dummy(ImVec2(0.f, 1.f));

            // A busy server can have dozens on duty and the panel would grow
            // past the screen, so the tail is summarised.
            constexpr int kVisibleRows = 14;
            const int shown =
                adminCount < kVisibleRows ? adminCount : kVisibleRows;
            ImDrawList* rows = ImGui::GetWindowDrawList();
            for (int i = 0; i < shown; ++i) {
                const auto& a = admins[i];
                const ImVec2 origin =
                    ImGui::GetCursorScreenPos();
                // Filled dot for admins in render range, hollow for the rest:
                // one glyph instead of a column of asterisks.
                const ImVec2 dot(
                    origin.x + 3.f,
                    origin.y + ImGui::GetTextLineHeight() * 0.5f);
                if (a.streamed) {
                    rows->AddCircleFilled(
                        dot, 2.6f, IM_COL32(120, 224, 160, 235), 8);
                } else {
                    rows->AddCircle(
                        dot, 2.6f, IM_COL32(150, 158, 172, 170), 8, 1.1f);
                }
                ImGui::Dummy(ImVec2(11.f, 0.f));
                ImGui::SameLine(0.f, 0.f);
                ImGui::TextColored(
                    a.streamed
                        ? ImVec4(0.92f, 0.94f, 0.97f, 1.f)
                        : ImVec4(0.66f, 0.70f, 0.77f, 1.f),
                    "%s",
                    a.name[0] ? a.name : "?");
            }
            if (adminCount > shown) {
                ImGui::TextColored(
                    ImVec4(0.55f, 0.59f, 0.66f, 1.f),
                    "   +%d more",
                    adminCount - shown);
            }
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(4);
}

bool Export(
    char* output,
    std::size_t capacity,
    std::size_t& used) noexcept
{
#define MF_WRITE(name, value) \
    if (!AppendNum(output, capacity, used, name, value)) return false
    MF_WRITE("mf_nospread", g_Settings.noSpread ? 1 : 0);
    MF_WRITE("mf_nospread_pct", g_Settings.noSpreadPct);
    MF_WRITE("mf_norecoil", g_Settings.noRecoil ? 1 : 0);
    MF_WRITE("mf_norecoil_pct", g_Settings.noRecoilPct);
    MF_WRITE("mf_reload", g_Settings.reloadSpeed ? 1 : 0);
    MF_WRITE("mf_reload_pct", g_Settings.reloadSpeedPct);
    MF_WRITE("mf_god", g_Settings.godMode ? 1 : 0);
    MF_WRITE("mf_fixed_hp", g_Settings.fixedHp ? 1 : 0);
    MF_WRITE("mf_fixed_hp_val", g_Settings.fixedHpValue);
    MF_WRITE("mf_fixed_ap", g_Settings.fixedAp ? 1 : 0);
    MF_WRITE("mf_fixed_ap_val", g_Settings.fixedApValue);
    MF_WRITE("mf_anti_afk", g_Settings.antiAfk ? 1 : 0);
    MF_WRITE("mf_speedhack", g_Settings.speedhack ? 1 : 0);
    MF_WRITE("mf_speedhack_val", g_Settings.speedhackValue);
    MF_WRITE("mf_noclip", g_Settings.noclip ? 1 : 0);
    MF_WRITE("mf_noclip_speed", g_Settings.noclipSpeed);
    MF_WRITE("mf_noclip_key", g_Settings.noclipKey);
    MF_WRITE("mf_freecam", g_Settings.freecam ? 1 : 0);
    MF_WRITE("mf_freecam_speed", g_Settings.freecamSpeed);
    MF_WRITE("mf_freecam_key", g_Settings.freecamKey);
    MF_WRITE("mf_tp_wp_key", g_Settings.tpWaypointKey);
    MF_WRITE("mf_fov_on", g_Settings.cameraFovEnabled ? 1 : 0);
    MF_WRITE("mf_fov", g_Settings.cameraFov);
    MF_WRITE("mf_aspect_on", g_Settings.aspectEnabled ? 1 : 0);
    MF_WRITE("mf_aspect", g_Settings.aspectRatio);
    MF_WRITE("mf_show_players", g_Settings.showPlayers ? 1 : 0);
    MF_WRITE("mf_show_admins", g_Settings.showAdmins ? 1 : 0);
    MF_WRITE("mf_players_dist", g_Settings.playersDistance);
    MF_WRITE("mf_around_x", g_Settings.aroundPosX);
    MF_WRITE("mf_around_y", g_Settings.aroundPosY);
    MF_WRITE("mf_stamina", g_Settings.unlimitedStamina ? 1 : 0);
    MF_WRITE("mf_superjump", g_Settings.superJump ? 1 : 0);
    MF_WRITE("mf_clickwarp", g_Settings.clickWarp ? 1 : 0);
    MF_WRITE("mf_clickwarp_key", g_Settings.clickWarpKey);
    MF_WRITE("mf_greenzone", g_Settings.greenzoneBypass ? 1 : 0);
    MF_WRITE("mf_noreload", g_Settings.noReload ? 1 : 0);
    MF_WRITE("mf_infammo", g_Settings.infiniteAmmo ? 1 : 0);
    MF_WRITE("mf_infclip", g_Settings.infiniteClip ? 1 : 0);
    MF_WRITE("mf_weather_on", g_Settings.weatherOverride ? 1 : 0);
    MF_WRITE("mf_weather", g_Settings.weatherIndex);
    MF_WRITE("mf_time_on", g_Settings.timeOverride ? 1 : 0);
    MF_WRITE("mf_time_hour", g_Settings.timeHour);
    MF_WRITE("mf_time_min", g_Settings.timeMinute);
    MF_WRITE("mf_time_freeze", g_Settings.freezeTime ? 1 : 0);
    MF_WRITE("mf_nofog", g_Settings.noFog ? 1 : 0);
    {
        const auto& vehicle =
            VehicleMovement::GetSettings();
        MF_WRITE(
            "mf_vehicle_speed",
            vehicle.speedEnable ? 1 : 0);
        MF_WRITE(
            "mf_vehicle_speed_mult",
            vehicle.speedMult);
        MF_WRITE(
            "mf_vehicle_gravity",
            vehicle.gravityEnable ? 1 : 0);
        MF_WRITE(
            "mf_vehicle_gravity_mult",
            vehicle.gravityMult);
        MF_WRITE("mf_seatbelt", vehicle.seatbelt ? 1 : 0);
        MF_WRITE("mf_veh_fly", vehicle.fly ? 1 : 0);
        MF_WRITE("mf_veh_fly_speed", vehicle.flySpeed);
        MF_WRITE("mf_veh_skip_anim", vehicle.skipEnterAnim ? 1 : 0);
        MF_WRITE("mf_veh_autopilot", vehicle.autopilot ? 1 : 0);
        MF_WRITE("mf_veh_engine", vehicle.engineForceOn ? 1 : 0);
    }
#undef MF_WRITE
    return true;
}

void Import(const char* content) noexcept
{
    if (!content)
        return;
#define MF_READ_BOOL(name, target) \
    if (const char* v = FindValue(content, name)) \
        target = std::strtol(v, nullptr, 10) != 0
#define MF_READ_FLOAT(name, target) \
    if (const char* v = FindValue(content, name)) \
        target = std::strtof(v, nullptr)
#define MF_READ_INT(name, target) \
    if (const char* v = FindValue(content, name)) \
        target = static_cast<int>(std::strtol(v, nullptr, 10))
    MF_READ_BOOL("mf_nospread", g_Settings.noSpread);
    MF_READ_FLOAT("mf_nospread_pct", g_Settings.noSpreadPct);
    MF_READ_BOOL("mf_norecoil", g_Settings.noRecoil);
    MF_READ_FLOAT("mf_norecoil_pct", g_Settings.noRecoilPct);
    MF_READ_BOOL("mf_reload", g_Settings.reloadSpeed);
    MF_READ_FLOAT("mf_reload_pct", g_Settings.reloadSpeedPct);
    MF_READ_BOOL("mf_god", g_Settings.godMode);
    MF_READ_BOOL("mf_fixed_hp", g_Settings.fixedHp);
    MF_READ_FLOAT("mf_fixed_hp_val", g_Settings.fixedHpValue);
    MF_READ_BOOL("mf_fixed_ap", g_Settings.fixedAp);
    MF_READ_FLOAT("mf_fixed_ap_val", g_Settings.fixedApValue);
    MF_READ_BOOL("mf_anti_afk", g_Settings.antiAfk);
    MF_READ_BOOL("mf_speedhack", g_Settings.speedhack);
    MF_READ_FLOAT("mf_speedhack_val", g_Settings.speedhackValue);
    MF_READ_BOOL("mf_noclip", g_Settings.noclip);
    MF_READ_FLOAT("mf_noclip_speed", g_Settings.noclipSpeed);
    MF_READ_INT("mf_noclip_key", g_Settings.noclipKey);
    MF_READ_BOOL("mf_freecam", g_Settings.freecam);
    MF_READ_FLOAT("mf_freecam_speed", g_Settings.freecamSpeed);
    MF_READ_INT("mf_freecam_key", g_Settings.freecamKey);
    MF_READ_INT("mf_tp_wp_key", g_Settings.tpWaypointKey);
    MF_READ_BOOL("mf_fov_on", g_Settings.cameraFovEnabled);
    MF_READ_FLOAT("mf_fov", g_Settings.cameraFov);
    MF_READ_BOOL("mf_aspect_on", g_Settings.aspectEnabled);
    MF_READ_FLOAT("mf_aspect", g_Settings.aspectRatio);
    MF_READ_BOOL("mf_show_players", g_Settings.showPlayers);
    MF_READ_BOOL("mf_show_admins", g_Settings.showAdmins);
    MF_READ_FLOAT("mf_players_dist", g_Settings.playersDistance);
    MF_READ_FLOAT("mf_around_x", g_Settings.aroundPosX);
    MF_READ_FLOAT("mf_around_y", g_Settings.aroundPosY);
    MF_READ_BOOL("mf_stamina", g_Settings.unlimitedStamina);
    MF_READ_BOOL("mf_superjump", g_Settings.superJump);
    MF_READ_BOOL("mf_clickwarp", g_Settings.clickWarp);
    MF_READ_INT("mf_clickwarp_key", g_Settings.clickWarpKey);
    MF_READ_BOOL("mf_greenzone", g_Settings.greenzoneBypass);
    MF_READ_BOOL("mf_noreload", g_Settings.noReload);
    MF_READ_BOOL("mf_infammo", g_Settings.infiniteAmmo);
    MF_READ_BOOL("mf_infclip", g_Settings.infiniteClip);
    MF_READ_BOOL("mf_weather_on", g_Settings.weatherOverride);
    MF_READ_INT("mf_weather", g_Settings.weatherIndex);
    MF_READ_BOOL("mf_time_on", g_Settings.timeOverride);
    MF_READ_INT("mf_time_hour", g_Settings.timeHour);
    MF_READ_INT("mf_time_min", g_Settings.timeMinute);
    MF_READ_BOOL("mf_time_freeze", g_Settings.freezeTime);
    MF_READ_BOOL("mf_nofog", g_Settings.noFog);
    {
        auto& vehicle =
            VehicleMovement::GetSettings();
        MF_READ_BOOL(
            "mf_vehicle_speed",
            vehicle.speedEnable);
        MF_READ_FLOAT(
            "mf_vehicle_speed_mult",
            vehicle.speedMult);
        MF_READ_BOOL(
            "mf_vehicle_gravity",
            vehicle.gravityEnable);
        MF_READ_FLOAT(
            "mf_vehicle_gravity_mult",
            vehicle.gravityMult);
        MF_READ_BOOL("mf_seatbelt", vehicle.seatbelt);
        MF_READ_BOOL("mf_veh_fly", vehicle.fly);
        MF_READ_FLOAT("mf_veh_fly_speed", vehicle.flySpeed);
        MF_READ_BOOL("mf_veh_skip_anim", vehicle.skipEnterAnim);
        MF_READ_BOOL("mf_veh_autopilot", vehicle.autopilot);
        MF_READ_BOOL("mf_veh_engine", vehicle.engineForceOn);

        if (!(vehicle.speedMult >= 1.f))
            vehicle.speedMult = 1.f;
        if (vehicle.speedMult > 10.f)
            vehicle.speedMult = 10.f;
        if (!(vehicle.gravityMult >= 1.f))
            vehicle.gravityMult = 1.f;
        if (vehicle.gravityMult > 10.f)
            vehicle.gravityMult = 10.f;
        if (!(vehicle.flySpeed >= 0.2f))
            vehicle.flySpeed = 0.2f;
        if (vehicle.flySpeed > 20.f)
            vehicle.flySpeed = 20.f;
        vehicle.unlockSpeedLimit = false;
    }
#undef MF_READ_BOOL
#undef MF_READ_FLOAT
#undef MF_READ_INT
    // Quarantine: never re-arm camera writers from config.
    g_Settings.aspectEnabled = false;
    g_Settings.cameraFovEnabled = false;
    g_Settings.disableCarCollision = false;
    g_Settings.disablePlayerCollision = false;
    g_HaveSynced = false;
}
} // namespace MiscFeatures
