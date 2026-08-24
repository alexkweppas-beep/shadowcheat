#include "MiscFeatures.h"

#include "NativeInvoke.hpp"
#include "FiveRpAdminScan.h"
#include "QuantTools.h"
#include "RagePlayerNames.h"
#include "RageV8Runtime.h"
#include "RageWorldEntities.h"
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
    bool g_PendingOpenTrunk = false;
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
    std::uint64_t g_CwAimQueryId = 0;
    ULONGLONG g_CwAimAt = 0;
    ULONGLONG g_CwHintAt = 0;
    bool g_CwHaveHint = false;
    // Where the outstanding probe was fired from, so the distance it comes back
    // with is measured against the same origin.
    Vector3 g_CwAimOrigin{};
    float g_CwHintDist = 0.f;
    // Corner order is bit-coded: bit 0 is X, bit 1 is Y, bit 2 is Z, so two
    // corners share an edge exactly when their indices differ in one bit.
    Vector3 g_CwBox[8]{};
    bool g_CwHaveBox = false;
    float g_CwHintX = 0.f;
    float g_CwHintY = 0.f;
    float g_CwHintZ = 0.f;
    // Player / ammo / vehicle frame natives live on the RAGE script thread.
    // Present returns empty for this whole class of natives, which is why the
    // features below never applied while they were called from the render hook.
    bool g_PlJsHooked = false;
    bool g_PlJsPushed = false;
    std::uint64_t g_PlJsGeneration = 0;
    char g_PlJsLastState[512]{};
    ULONGLONG g_PlVehQueryAt = 0;
    std::uint64_t g_PlVehQueryId = 0;
    ULONGLONG g_AntiAfkNextPokeAt = 0;
    // Waypoint teleport handshake with the script hook: C++ bumps the request
    // counter, JS runs the probe over several frames and reports back.
    int g_TpSeq = 0;
    int g_TpReportedSeq = 0;
    std::uint64_t g_TpQueryId = 0;
    ULONGLONG g_TpQueryAt = 0;
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

    // Every field is captured and validated on its own. The old all-or-nothing
    // capture rejected the whole patch when a single float looked wrong — a
    // melee weapon with a zero reload time silently disabled spread and recoil
    // too, with no log to say why.
    struct WeaponField final
    {
        float original{};
        bool captured{};
        bool applied{};
    };

    struct WeaponInfoBackup final
    {
        uint8_t* info{};
        std::uint32_t hash{};
        WeaponField accuracySpread{};
        WeaponField batchSpread{};
        WeaponField recoilAccuracyMax{};
        WeaponField recoilErrorTime{};
        WeaponField recoilRecoveryRate{};
        WeaponField recoilShakeAmplitude{};
        WeaponField reloadTimeMp{};
        WeaponField reloadTimeSp{};
        WeaponField vehicleReloadTime{};
        WeaponField animReloadRate{};
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

    bool ReadWeaponHash(uint8_t* info, std::uint32_t& out) noexcept
    {
        __try {
            out = *reinterpret_cast<std::uint32_t*>(
                info + Offsets::WeaponInfo::Hash);
            return out != 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // The weapon manager holds the info pointer at a known slot, but a shifted
    // layout would hand back an unrelated object, and writing floats into one
    // is what produced the old renderer AVs. A candidate therefore has to carry
    // a non-zero joaat at +0x10 before it is written to at all.
    //
    // Matching the equipped hash is preferred but not required: the hash source
    // (weapon manager +0x18) can itself be wrong on a shifted build, and
    // refusing to patch in that case would disable combat outright.
    uint8_t* GetLocalWeaponInfo(
        std::uint32_t expectedHash,
        std::uint32_t& resolvedHash) noexcept
    {
        resolvedHash = 0;
        CPed* ped = Game::GetLocalPed();
        if (!ped)
            return nullptr;

        uint8_t* wm = nullptr;
        __try {
            wm = *reinterpret_cast<uint8_t**>(
                reinterpret_cast<uint8_t*>(ped) +
                Offsets::Ped::WeaponManager);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
        if (!IsValidPtr(wm))
            return nullptr;

        static constexpr uint32_t kInfoSlots[] = {
            Offsets::WeaponManager::WeaponInfo, 0x10, 0x28, 0x30
        };
        uint8_t* fallback = nullptr;
        std::uint32_t fallbackHash = 0;
        for (const uint32_t slot : kInfoSlots)
        {
            uint8_t* info = nullptr;
            __try {
                info = *reinterpret_cast<uint8_t**>(wm + slot);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                info = nullptr;
            }
            if (!IsValidPtr(info))
                continue;
            std::uint32_t hash = 0;
            if (!ReadWeaponHash(info, hash))
                continue;
            if (expectedHash != 0 && hash == expectedHash)
            {
                resolvedHash = hash;
                return info;
            }
            if (!fallback)
            {
                fallback = info;
                fallbackHash = hash;
            }
        }
        resolvedHash = fallbackHash;
        return fallback;
    }

    void RestoreWeaponField(
        uint8_t* info,
        uint32_t offset,
        WeaponField& field) noexcept
    {
        if (field.applied && field.captured && IsValidPtr(info))
            WriteWeaponFloat(info, offset, field.original);
        field = {};
    }

    void RestoreWeaponInfo() noexcept
    {
        if (!g_WeaponBackup.valid) {
            g_WeaponBackup = {};
            return;
        }
        uint8_t* info = g_WeaponBackup.info;
        RestoreWeaponField(
            info,
            Offsets::WeaponInfo::AccuracySpread,
            g_WeaponBackup.accuracySpread);
        RestoreWeaponField(
            info,
            Offsets::WeaponInfo::BatchSpread,
            g_WeaponBackup.batchSpread);
        RestoreWeaponField(
            info,
            Offsets::WeaponInfo::RecoilAccuracyMax,
            g_WeaponBackup.recoilAccuracyMax);
        RestoreWeaponField(
            info,
            Offsets::WeaponInfo::RecoilErrorTime,
            g_WeaponBackup.recoilErrorTime);
        RestoreWeaponField(
            info,
            Offsets::WeaponInfo::RecoilRecoveryRate,
            g_WeaponBackup.recoilRecoveryRate);
        RestoreWeaponField(
            info,
            Offsets::WeaponInfo::RecoilShakeAmplitude,
            g_WeaponBackup.recoilShakeAmplitude);
        RestoreWeaponField(
            info,
            Offsets::WeaponInfo::ReloadTimeMP,
            g_WeaponBackup.reloadTimeMp);
        RestoreWeaponField(
            info,
            Offsets::WeaponInfo::ReloadTimeSP,
            g_WeaponBackup.reloadTimeSp);
        RestoreWeaponField(
            info,
            Offsets::WeaponInfo::VehicleReloadTime,
            g_WeaponBackup.vehicleReloadTime);
        RestoreWeaponField(
            info,
            Offsets::WeaponInfo::AnimReloadRate,
            g_WeaponBackup.animReloadRate);
        g_WeaponBackup = {};
    }

    bool InRange(float v, float lo, float hi) noexcept
    {
        return std::isfinite(v) && v >= lo && v <= hi;
    }

    // Capture-on-demand: a field is only read the first time it is about to be
    // written, and a field that fails its own range check just opts out.
    bool EnsureWeaponField(
        uint8_t* info,
        uint32_t offset,
        WeaponField& field,
        float lo,
        float hi) noexcept
    {
        if (field.captured)
            return true;
        float value = 0.f;
        if (!ReadWeaponFloat(info, offset, value))
            return false;
        if (!InRange(value, lo, hi))
            return false;
        field.original = value;
        field.captured = true;
        return true;
    }

    void ScaleWeaponField(
        uint8_t* info,
        uint32_t offset,
        WeaponField& field,
        float lo,
        float hi,
        float scale) noexcept
    {
        if (!EnsureWeaponField(info, offset, field, lo, hi))
            return;
        if (WriteWeaponFloat(info, offset, field.original * scale))
            field.applied = true;
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
        // Player globals are the base. A per-weapon override can only add to
        // them: it used to replace them outright, so adding a gun to the
        // arsenal (which force-sets `active` with combat flags defaulted off)
        // silently disabled the global toggles for that weapon.
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
            if (ov->profile.noSpread)
            {
                noSpread = true;
                noSpreadPct = ov->profile.noSpreadPct;
            }
            if (ov->profile.noRecoil)
            {
                noRecoil = true;
                noRecoilPct = ov->profile.noRecoilPct;
            }
            if (ov->profile.reloadSpeed)
            {
                reloadSpeed = true;
                reloadSpeedPct = ov->profile.reloadSpeedPct;
            }
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

        std::uint32_t resolvedHash = 0;
        uint8_t* info = GetLocalWeaponInfo(hash, resolvedHash);
        if (!info) {
            RestoreWeaponInfo();
#if RAGEMP_DEV_BUILD
            static ULONGLONG s_lastLog = 0;
            const ULONGLONG now = GetTickCount64();
            if (now - s_lastLog > 5000) {
                s_lastLog = now;
                hooks::HLog(
                    "[Misc] combat: no CWeaponInfo behind the weapon manager");
            }
#endif
            return;
        }

        // Re-capture whenever the weapon (or its info block) changes, so the
        // previous gun is handed back its own values. Keyed on the info's own
        // hash, which is the value that actually identifies the block.
        if (!g_WeaponBackup.valid ||
            g_WeaponBackup.info != info ||
            g_WeaponBackup.hash != resolvedHash)
        {
            RestoreWeaponInfo();
            g_WeaponBackup.info = info;
            g_WeaponBackup.hash = resolvedHash;
            g_WeaponBackup.valid = true;
        }

        const float spreadT =
            noSpread ? Clamp01(noSpreadPct * 0.01f) : 0.f;
        const float recoilT =
            noRecoil ? Clamp01(noRecoilPct * 0.01f) : 0.f;
        const float reloadT =
            reloadSpeed ? Clamp01(reloadSpeedPct * 0.01f) : 0.f;

        if (noSpread)
        {
            // At 100% these really go to zero. Leaving 5% behind was the
            // difference between "no spread" and "slightly less spread".
            const float scale = 1.f - spreadT;
            ScaleWeaponField(
                info,
                Offsets::WeaponInfo::AccuracySpread,
                g_WeaponBackup.accuracySpread,
                0.f, 25.f, scale);
            ScaleWeaponField(
                info,
                Offsets::WeaponInfo::BatchSpread,
                g_WeaponBackup.batchSpread,
                0.f, 25.f, scale);
            // Sustained-fire bloom. It was only ever touched by no-recoil, so
            // enabling no-spread alone did nothing on automatic weapons.
            ScaleWeaponField(
                info,
                Offsets::WeaponInfo::RecoilAccuracyMax,
                g_WeaponBackup.recoilAccuracyMax,
                0.f, 25.f, scale);
        }

        if (noRecoil)
        {
            const float scale = 1.f - recoilT;
            ScaleWeaponField(
                info,
                Offsets::WeaponInfo::RecoilShakeAmplitude,
                g_WeaponBackup.recoilShakeAmplitude,
                0.f, 10.f, scale);
            ScaleWeaponField(
                info,
                Offsets::WeaponInfo::RecoilErrorTime,
                g_WeaponBackup.recoilErrorTime,
                0.f, 10.f, scale);
            ScaleWeaponField(
                info,
                Offsets::WeaponInfo::RecoilAccuracyMax,
                g_WeaponBackup.recoilAccuracyMax,
                0.f, 25.f, scale);
            // Faster recovery, not a smaller value: this one is a rate.
            ScaleWeaponField(
                info,
                Offsets::WeaponInfo::RecoilRecoveryRate,
                g_WeaponBackup.recoilRecoveryRate,
                0.01f, 50.f, 1.f + recoilT * 4.f);
        }

        if (reloadSpeed)
        {
            // Times shrink, the animation rate grows. Each field validates on
            // its own, so a weapon with an odd vehicle reload time no longer
            // cancels the whole patch.
            const float timeScale = 1.f - reloadT * 0.9f;
            ScaleWeaponField(
                info,
                Offsets::WeaponInfo::ReloadTimeMP,
                g_WeaponBackup.reloadTimeMp,
                0.05f, 15.f, timeScale);
            ScaleWeaponField(
                info,
                Offsets::WeaponInfo::ReloadTimeSP,
                g_WeaponBackup.reloadTimeSp,
                0.05f, 15.f, timeScale);
            ScaleWeaponField(
                info,
                Offsets::WeaponInfo::VehicleReloadTime,
                g_WeaponBackup.vehicleReloadTime,
                0.05f, 15.f, timeScale);
            ScaleWeaponField(
                info,
                Offsets::WeaponInfo::AnimReloadRate,
                g_WeaponBackup.animReloadRate,
                0.05f, 10.f, 1.f + reloadT * 3.f);
        }
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

    bool PlayerV8Ready() noexcept;
    bool LocalIsInVehicle() noexcept;

    // The poke itself lives in the V8 render hook, which is the only place the
    // server's own activity function can be reached. This only re-rolls the
    // interval so the pacing stays irregular.
    //
    // The old body poked INPUT_LOOK_LR/UD from Present with a ±0.002 amplitude
    // and rotated the nav forward vector. Neither is an activity the server
    // reads, and Present does not deliver control values anyway.
    void ApplyAntiAfk() noexcept
    {
        if (!g_Settings.antiAfk)
        {
            g_LastAntiAfkTick = 0;
            return;
        }

        const ULONGLONG now = GetTickCount64();
        if (g_LastAntiAfkTick != 0 &&
            now - g_LastAntiAfkTick < g_NextAntiAfkIntervalMs)
            return;
        g_LastAntiAfkTick = now;

        // 22 s - 34 s window.
        constexpr float kBaseSec = 28.f;
        const float jitterFactor =
            0.8f + static_cast<float>(rand() % 41) * 0.01f;
        g_NextAntiAfkIntervalMs = static_cast<ULONGLONG>(
            kBaseSec * jitterFactor * 1000.f);
        g_AntiAfkNextPokeAt = g_NextAntiAfkIntervalMs;
    }

    // The script thread owns this while V8 is up: it drives the run/sprint
    // multiplier and the move-rate override, both of which survive the task
    // system. This memory path is only the fallback for when V8 is down.
    //
    // It writes CPed+Velocity (the field the aimbot reads), not nav+0x70 —
    // that offset is not the velocity and multiplying it did nothing useful.
    void ApplySpeedhack() noexcept
    {
        if (!g_Settings.speedhack)
            return;
        if (PlayerV8Ready() && g_PlJsHooked)
            return;
        if (LocalIsInVehicle())
            return;

        CPed* ped = Game::GetLocalPed();
        if (!ped)
            return;
        auto* base = reinterpret_cast<uint8_t*>(ped);

        Vector3 velocity{};
        __try {
            velocity = *reinterpret_cast<Vector3*>(
                base + Offsets::Ped::Velocity);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return;
        }
        if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y))
            return;

        const float speed2 =
            velocity.x * velocity.x + velocity.y * velocity.y;
        // Boost whenever the player is actually moving. The old Shift-only gate
        // meant walking, and any server that eats Shift, produced nothing.
        if (speed2 < 0.04f)
            return;

        float mul = g_Settings.speedhackValue;
        if (mul < 1.f)
            mul = 1.f;
        if (mul > 10.f)
            mul = 10.f;

        __try {
            *reinterpret_cast<Vector3*>(
                base + Offsets::Ped::Velocity) = Vector3{
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

    // A raw "is this pointer non-null and aligned" test accepted weapon,
    // inventory and nav objects as vehicles, which is what made the on-foot
    // speedhack think the player was seated. A candidate now has to carry a
    // plausible handling blob before it counts as a vehicle.
    struct VehicleHandlingLayout final
    {
        uint32_t driveForce;
        uint32_t tractionMax;
        uint32_t lowSpeedLoss;
    };

    constexpr VehicleHandlingLayout kHandlingLayouts[] = {
        {0x4C, 0x78, 0x90},
        {0x60, 0x88, 0xA8},
        {0x54, 0x80, 0x98},
        {0x48, 0x74, 0x8C},
    };

    constexpr uint32_t kPedVehicleCandidates[] = {
        0xD10, 0xD28, 0xD30, 0xC30, 0xD00, 0xD08, 0xCE0, 0xCF0, 0xD18
    };

    constexpr uint32_t kVehicleHandlingCandidates[] = {
        0x918, 0x938, 0x960, 0x908, 0x888, 0x900, 0x920, 0x980, 0x870
    };

    bool ReadFloatSafe(const uint8_t* base, uint32_t off, float& out) noexcept
    {
        __try {
            out = *reinterpret_cast<const float*>(base + off);
            return std::isfinite(out);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool LooksLikeHandling(const uint8_t* handling) noexcept
    {
        if (!IsValidPtr(handling))
            return false;
        float mass = 0.f;
        if (!ReadFloatSafe(handling, 0x0C, mass) ||
            !(mass > 20.f && mass < 200000.f))
            return false;
        for (const VehicleHandlingLayout& layout : kHandlingLayouts)
        {
            float drive = 0.f;
            float traction = 0.f;
            float loss = 0.f;
            if (!ReadFloatSafe(handling, layout.driveForce, drive) ||
                !ReadFloatSafe(handling, layout.tractionMax, traction) ||
                !ReadFloatSafe(handling, layout.lowSpeedLoss, loss))
                continue;
            if (drive > 0.01f && drive < 80.f &&
                traction > 0.05f && traction < 80.f &&
                loss >= 0.f && loss < 40.f)
                return true;
        }
        return false;
    }

    bool LooksLikeVehicle(uint8_t* candidate, uint8_t* pedBase) noexcept
    {
        if (!IsValidPtr(candidate) || candidate == pedBase)
            return false;
        uint8_t* nav = nullptr;
        __try {
            nav = *reinterpret_cast<uint8_t**>(candidate + 0x30);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        if (!IsValidPtr(nav))
            return false;

        for (const uint32_t off : kVehicleHandlingCandidates)
        {
            uint8_t* handling = nullptr;
            __try {
                handling = *reinterpret_cast<uint8_t**>(candidate + off);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                handling = nullptr;
            }
            if (LooksLikeHandling(handling))
                return true;
        }
        return false;
    }

    uint8_t* LocalVehiclePtr() noexcept
    {
        CPed* ped = Game::GetLocalPed();
        if (!ped || !IsValidPtr(ped))
            return nullptr;
        auto* base = reinterpret_cast<uint8_t*>(ped);
        for (const uint32_t off : kPedVehicleCandidates)
        {
            uint8_t* vehicle = nullptr;
            __try {
                vehicle = *reinterpret_cast<uint8_t**>(base + off);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                vehicle = nullptr;
            }
            if (LooksLikeVehicle(vehicle, base))
                return vehicle;
        }
        return nullptr;
    }

    // True only when we are actually seated. Prefers the script-thread answer
    // and falls back to the validated memory scan.
    bool LocalIsInVehicle() noexcept
    {
        const auto state = VehicleMovement::GetLocalVehicleState();
        if (state.valid)
            return state.inVehicle;
        return LocalVehiclePtr() != nullptr;
    }

    uint8_t* TpEntityPtr() noexcept
    {
        CPed* ped = Game::GetLocalPed();
        if (!ped || !IsValidPtr(ped))
            return nullptr;
        if (uint8_t* vehicle = LocalVehiclePtr())
            return vehicle;
        return reinterpret_cast<uint8_t*>(ped);
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
/* Whatever the previous handler left applied to the game, undone once, before
anything else.
A reconnect replaces the V8 context, and the handler registered in the old one
dies with it, mid-flight. Everything it had asserted stays asserted: the ped
frozen, its collision off, every control disabled, and the render switch still
pointing at a script camera that no longer exists. None of it can be undone from
the dead context, and the fresh state object here starts with _on false, so the
teardown branch below never fires and never cleans it up.
The camera one is what made this look like the game refusing to reconnect: the
frame keeps being rendered from a camera that is gone. */
if(!g.__bfMv){
try{mp.game.cam.renderScriptCams(false,false,0,true,false,0);}catch(e){}
try{var lp0=mp.players&&mp.players.local;
if(lp0){
try{lp0.freezePosition(false);}catch(e){}
try{lp0.setCollision(true,true);}catch(e){}
try{var v0=lp0.vehicle;
if(v0){v0.freezePosition(false);v0.setCollision(true,true);}}catch(e){}
}}catch(e){}
try{
if(mp.game.controls&&typeof mp.game.controls.enableAllControlActions==='function')
mp.game.controls.enableAllControlActions(0);
else mp.game.invoke('0xA5FFE9B05F199DE7',0);
}catch(e){}
}
g.__bfMv=g.__bfMv||{freecam:false,noclip:false,paused:false,fcSpeed:1.5,ncSpeed:1.5};
/* Re-registering used to be blocked by a one-shot guard flag. The V8 context
outlives a payload unload, so the handler from the previous injection stayed
registered and a rebuilt hook could never replace it. Keeping the function
around and removing it first makes re-injection idempotent. */
if(g.__bfMvFn){try{mp.events.remove('render',g.__bfMvFn);}catch(e){}}
g.__bfKd=function(vk,c){
try{if(mp.keys&&mp.keys.isDown(vk))return true;}catch(e){}
try{return mp.game.controls.getDisabledControlNormal(0,c)>0.4;}catch(e){}
return false;
};
g.__bfMvFn=function(){
var s=g.__bfMv;if(!s)return;
var p=mp.players&&mp.players.local;
/* Gated on _rsc as well as on the camera. Setup can leave script cams
rendering without a camera object, and gating the teardown on the object alone
meant that state was never undone: the view stayed where freecam left it. */
if(!s.freecam&&(s._cam||s._rsc)){
/* Rendering goes back to the gameplay camera before the script camera is
destroyed. The other order leaves the game rendering script cams with nothing
to render, which is exactly the camera that never came back. */
try{mp.game.cam.renderScriptCams(false,false,0,true,true,0);}catch(e){}
s._rsc=false;
if(s._cam){
try{s._cam.setActive(false);}catch(e){}
try{s._cam.destroy(true);}catch(e){try{s._cam.destroy();}catch(e2){}}
s._cam=null;
}
}
if(!s.freecam&&!s.noclip){
if(s._on&&p){
try{p.freezePosition(false);p.setCollision(true,true);p.setVelocity(0,0,0);}catch(e){}
/* The car has to be handed back too, or it stays frozen and ghosted after
noclip is switched off. */
if(s._nv){
try{
var ov=mp.vehicles&&mp.vehicles.atHandle?mp.vehicles.atHandle(s._nv):null;
if(ov){ov.freezePosition(false);ov.setCollision(true,true);ov.setVelocity(0,0,0);}
}catch(e){}
}
}
s._nv=0;
s._on=false;
return;
}
if(!p)return;
s._on=true;
/* Freezing blocks weapons outright, so noclip does not freeze. It does not need
to: the position is written every frame below, and zeroing the velocity with it
holds the entity just as still without taking the gun away. Freecam still
freezes, because there the player is not where the camera is and should not be
acting at all. */
try{p.freezePosition(!!s.freecam);p.setVelocity(0,0,0);}catch(e){}
/* Whatever actually has to be moved. Seated, the player cannot be: the seat
puts them back every frame, so noclip did nothing at all inside a vehicle. */
var nv=null;
try{if(p.vehicle&&p.vehicle.handle)nv=p.vehicle;}catch(e){nv=null;}
s._nv=nv?nv.handle:0;
if(nv){try{nv.freezePosition(!!s.freecam);nv.setVelocity(0,0,0);}catch(e){}}
/* Freecam takes every control except the two look axes, which is right when the
player is somewhere else entirely. Noclip leaves them alone: that blanket is
what killed shooting, since it covers attack and aim along with everything else,
and movement never needed disabling because the position is overwritten every
frame regardless of what the ped tries to do. */
if(s.freecam){
for(var i=0;i<350;i++){
if(i===1||i===2)continue;
try{mp.game.controls.disableControlAction(0,i,true);}catch(e){}
}
}
if(s.freecam){
if(!s._cam){
var pos=p.position,rot={x:0,z:0};
try{rot=mp.game.cam.getGameplayCamRot(2);}catch(e){}
s.pitch=rot.x||0;s.yaw=rot.z||0;
var nc=null;
try{nc=mp.cameras.new('default',
new mp.Vector3(pos.x,pos.y,pos.z+0.6),
new mp.Vector3(s.pitch,0,s.yaw),50);}catch(e){nc=null;}
if(!nc)return;
/* Published the moment it exists, and the render switch is tracked on its
own. The old code assigned inside a try whose catch nulled it again, so a
setup that threw half way through orphaned the camera and left the render
switch on with no way to reach it. */
s._cam=nc;
try{nc.setActive(true);}catch(e){}
try{mp.game.cam.renderScriptCams(true,false,0,true,false,0);
s._rsc=true;}catch(e){}
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
var ne=nv||p;
try{ne.setCollision(false,false);}catch(e){}
var nrot={x:0,y:0,z:0};
try{nrot=mp.game.cam.getGameplayCamRot(2);}catch(e){}
var nyr=(nrot.z||0)*0.01745329252,npr=(nrot.x||0)*0.01745329252;
var nfx=-Math.sin(nyr)*Math.cos(npr),nfy=Math.cos(nyr)*Math.cos(npr),nfz=Math.sin(npr);
var nrx=Math.cos(nyr),nry=Math.sin(nyr);
var nfast=g.__bfKd(0x10,21)?2.5:1;
var nspd=(s.ncSpeed||1.5)*nfast*0.35;
var np=ne.position;
if(g.__bfKd(0x57,32))np=new mp.Vector3(np.x+nfx*nspd,np.y+nfy*nspd,np.z+nfz*nspd);
if(g.__bfKd(0x53,33))np=new mp.Vector3(np.x-nfx*nspd,np.y-nfy*nspd,np.z-nfz*nspd);
if(g.__bfKd(0x41,34))np=new mp.Vector3(np.x-nrx*nspd,np.y-nry*nspd,np.z);
if(g.__bfKd(0x44,35))np=new mp.Vector3(np.x+nrx*nspd,np.y+nry*nspd,np.z);
if(g.__bfKd(0x20,22))np=new mp.Vector3(np.x,np.y,np.z+nspd);
if(g.__bfKd(0x11,36))np=new mp.Vector3(np.x,np.y,np.z-nspd);
/* Noclip moves the player every frame, so the window is renewed on a timer
instead of per move: the call is the server's own "a jump is coming" API and
spamming it every frame would itself stand out. */
try{var nw=Date.now();if(!s._acAt||nw-s._acAt>2000){s._acAt=nw;
mp.events.call('c:ac:check',3000);}}catch(e){}
try{ne.setCoordsNoOffset(np.x,np.y,np.z,false,false,false);}catch(e){}
};
mp.events.add('render',g.__bfMvFn);
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
g.__bfCw=g.__bfCw||{on:false,paused:false,key:2};
/* Same reason as the movement hook: the V8 context outlives a payload unload,
so a one-shot guard flag left the previous injection's handler in place and no
rebuilt hook could ever replace it. */
if(g.__bfCwFn){try{mp.events.remove('render',g.__bfCwFn);}catch(e){}}
function fin(v){
return !!v&&isFinite(v.x)&&isFinite(v.y)&&isFinite(v.z)&&
(v.x!==0||v.y!==0||v.z!==0);
}
/* The ray comes from C++, which reads it out of the viewport matrix the frame
is actually drawn with. Asking the game for it here does not work: measured in
game, getGameplayCamCoord yields nothing usable on foot, and the values it was
defaulted to were zeroes, so the probe was cast out of the world origin and the
destination landed somewhere random. Only the freecam path worked, and only
because that pose came off a camera object this script created itself. */
function ray(s){
if(!s.rayAt||Date.now()-s.rayAt>1200)return null;
var o={x:s.ox,y:s.oy,z:s.oz},d={x:s.dx,y:s.dy,z:s.dz};
if(!fin(o)||!fin(d))return null;
return {o:o,d:d};
}
function hitAt(ox,oy,oz,fx,fy,fz){
/* Started a little ahead of the lens. In third person the camera sits close
to, and sometimes inside, world geometry, and a ray that begins there reports
a hit at the camera itself, which parks the destination at your feet. */
ox+=fx*0.5;oy+=fy*0.5;oz+=fz*0.5;
var to=new mp.Vector3(ox+fx*250,oy+fy*250,oz+fz*250);
var from=new mp.Vector3(ox,oy,oz);
/* World, vehicles and objects. Foliage used to be in this mask, and on foot the
ray runs at eye level straight through the grass around you, so it stopped a
couple of metres away instead of where you were pointing and flickered as the
vegetation came and went. Vehicles are in so that aiming at one can be
recognised as aiming at that vehicle rather than at the ground behind it. */
try{
var h=mp.raycasting.testPointToPoint(from,to,mp.players.local,1|2|16);
if(h&&h.position)return {p:h.position,e:h.entity||null};
}catch(e){}
try{
var p=mp.players&&mp.players.local;
var ray=mp.game.shapetest.startExpensiveSynchronousShapeTestLosProbe(ox,oy,oz,to.x,to.y,to.z,1|2|16,p?p.handle:0,7);
var r=mp.game.shapetest.getShapeTestResult(ray);
if(r){
var ok=r[1]||r.hit;
var pos=r[2]||r.endCoords;
if(ok&&pos)return {p:pos,e:null};
}
}catch(e){}
return null;
}
function isVeh(e){
try{return !!e&&e.type==='vehicle'&&!!e.handle;}catch(x){}
return false;
}
/* The eight corners in world space, taken through the vehicle's own transform
so the box follows pitch and roll on a slope instead of only heading. C++ just
projects them. */
function vehBox(v){
try{
var d=mp.game.gameplay.getModelDimensions(v.model);
var lo=d&&(d[0]||d.min),hi=d&&(d[1]||d.max);
if(!lo||!hi)return null;
var out=[];
for(var i=0;i<8;i++){
var w=v.getOffsetFromInWorldCoords(
(i&1)?hi.x:lo.x,(i&2)?hi.y:lo.y,(i&4)?hi.z:lo.z);
if(!w||!isFinite(w.x))return null;
out.push(w.x,w.y,w.z);
}
return out;
}catch(e){}
return null;
}
/* Tri-state on purpose: true occupied, false free, null unknown. Collapsing
unknown into occupied is what stopped this working, because then every seat
looked taken and no attempt was ever made. */
function taken(v,h,i){
try{if(v&&typeof v.isSeatFree==='function')return !v.isSeatFree(i);}catch(e){}
try{var q=mp.game.vehicle.getPedInVehicleSeat(h,i);
if(typeof q==='number')return q!==0;}catch(e){}
/* Same native by hash. The wrapper above returns nothing usable here, which is
why every seat read as unknown. */
try{
var q2=mp.game.invoke('0xBB40DD2270B65366',h,i,0);
if(typeof q2==='number')return q2!==0;
}catch(e){}
return null;
}
/* Three surfaces, and it records which ones even exist. Two of them silently
did nothing here, and without that record the failure looks the same as "the
seat was taken". */
function put(h,i){
var p=mp.players&&mp.players.local;
if(!p){g.__bfCw.dbgPut='no local';return null;}
try{mp.events.call('c:ac:check',3000);}catch(e){}
var why='';
try{
if(typeof p.putIntoVehicle==='function'){p.putIntoVehicle(h,i);return 'put';}
why+='noPut ';
}catch(e){why+='putThrew ';}
try{
if(mp.game&&mp.game.ped&&
typeof mp.game.ped.setPedIntoVehicle==='function'){
mp.game.ped.setPedIntoVehicle(p.handle,h,i);return 'ped';}
why+='noPed ';
}catch(e){why+='pedThrew ';}
try{
if(mp.game&&typeof mp.game.invoke==='function'){
mp.game.invoke('0xF75B0D629E1C063D',p.handle,h,i);return 'inv';}
why+='noInv ';
}catch(e){why+='invThrew ';}
g.__bfCw.dbgPut=why;
return null;
}
/* Driver first, then passengers. Seats known to be free are tried before ones
that could not be read at all. Takes the handle, because that is a number and
survives being carried between ticks, unlike the entity wrapper. */
function seat(h,v){
if(!h){g.__bfCw.dbgGo='no handle';return false;}
var order=[-1,0,1,2,3];
var unknown=[];
for(var k=0;k<order.length;k++){
var st=taken(v,h,order[k]);
if(st===false){
var how=put(h,order[k]);
if(how){g.__bfCw.dbgGo='seat '+order[k]+' via '+how;return true;}
}else if(st===null)unknown.push(order[k]);
}
for(var u=0;u<unknown.length;u++){
var how2=put(h,unknown[u]);
if(how2){g.__bfCw.dbgGo='seat? '+unknown[u]+' via '+how2;return true;}
}
g.__bfCw.dbgGo='no seat';
return false;
}
function warp(x,y,z){
var p=mp.players&&mp.players.local;if(!p)return;
var ent=p;
try{
if(p.vehicle&&p.vehicle.handle){
if(mp.game.vehicle.getPedInVehicleSeat(p.vehicle.handle,-1)===p.handle)ent=p.vehicle;
}
}catch(e){}
/* The server's own spawn and yacht scripts announce a coming position jump
with this call before they move the player, and its checks expect the jump
inside that window. A warp that skips it looks exactly like what they hunt. */
try{mp.events.call('c:ac:check',3000);}catch(e){}
try{ent.setCoordsNoOffset(x,y,z,false,false,false);}catch(e){}
}
/* Called by C++ on the click, with the ray of the frame the click happened on.
The probe is redone here and now rather than reusing the last one: that one is a
round trip old, and in freecam the view travels far enough in that time to land
the jump somewhere other than the marker. */
/* Built here rather than inside the command C++ sends every cycle: inlined
there the command text ran past its buffer, and sprintf_s answers an overrun by
killing the process outright. */
g.__bfCw.rep=function(){
var s=g.__bfCw;
if(!s||!s._hAt||Date.now()-s._hAt>600)return '0 0 0 0 0';
var o='1 '+s._hx.toFixed(3)+' '+s._hy.toFixed(3)+' '+s._hz.toFixed(3);
if(s._box&&s._box.length===24){
o+=' 1';
for(var i=0;i<24;i++)o+=' '+s._box[i].toFixed(2);
}else o+=' 0';
return o;
};
g.__bfCw.go=function(ox,oy,oz,dx,dy,dz){
try{
var s=g.__bfCw;
s.dbgGo='';
/* Whatever the render pass last had under the ray, which is at most a frame
old and, unlike a probe fired from here, carries the entity. */
var fresh=s._hAt&&Date.now()-s._hAt<600;
if(fresh&&s._vh){
/* Re-resolved from the handle. The wrapper the probe handed back does not
survive into a later tick: reading .type off it here threw, the check read as
"not a vehicle", and the click silently warped instead of seating. */
var v=null;
try{v=mp.vehicles&&mp.vehicles.atHandle?mp.vehicles.atHandle(s._vh):null;}catch(e){}
if(seat(s._vh,v))return;
}
var r=hitAt(ox,oy,oz,dx,dy,dz);
if(r&&fin(r.p)){
var rh=0;
try{rh=r.e&&r.e.handle?r.e.handle:0;}catch(e){}
if(rh&&seat(rh,r.e))return;
warp(r.p.x,r.p.y,r.p.z+0.6);
if(!s.dbgGo)s.dbgGo='warp';
return;
}
/* No probe of our own: fall back on the highlighted point. */
if(fresh){warp(s._hx,s._hy,s._hz+0.6);s.dbgGo='warp cached';return;}
s.dbgGo='no hit';
}catch(e){g.__bfCw.dbgGo='go threw';}
};
g.__bfCwFn=function(){
var s=g.__bfCw;if(!s||!s.on){s&&(s._was=false);return;}
var t=Date.now();
var cam=ray(s);
var hit=null;
if(cam){
var r=hitAt(cam.o.x,cam.o.y,cam.o.z,cam.d.x,cam.d.y,cam.d.z);
hit=r&&fin(r.p)?r.p:null;
if(hit){
/* Stored raw. Smoothing lived here to fight jitter from a ray derived off
camera Euler angles; the ray now arrives from the viewport matrix and is
steady, so filtering here only added latency on top of the round trip. What
little easing is wanted is done on the depth, on the C++ side. */
s._hx=hit.x;s._hy=hit.y;s._hz=hit.z;s._hAt=t;
/* The vehicle is kept, not just its box. The click used to re-probe and
mp.raycasting only reports an entity from inside this event, so outside it the
probe fell through to the shape test path and came back with a position and no
entity, which read as "not a vehicle" every time. Acting on what is highlighted
is also the behaviour that matches what you see. */
if(isVeh(r.e)){
var vh=0;
try{vh=r.e.handle||0;}catch(e){}
s._vh=vh;s._box=vehBox(r.e);
}else{s._vh=0;s._box=null;}
}
}
/* A missed probe keeps the last destination for a moment instead of dropping
it: grazing ground angles miss constantly, and dropping the point on every miss
is what made the marker blink. Nothing is drawn from here any more, the overlay
reprojects this point itself every frame. */
var live=s._hAt!==undefined&&t-s._hAt<600;
if(s.dbg){
try{
mp.game.graphics.drawText(
'cw ray='+(cam?'ok':'NULL')+' hit='+(hit?'ok':'MISS')+' live='+(live?1:0)+
' vh='+(s._vh||0)+' go='+(s.dbgGo||'-')+
' put='+(s.dbgPut||'-'),
[0.5,0.74],
{font:4,centre:true,color:[120,220,255,255],scale:[0.36,0.36],outline:true});
}catch(e){}
}
/* The click is watched by C++, which then calls go() above. Reading the key
here as well would give two independent triggers for one press. */
};
mp.events.add('render',g.__bfCwFn);
}catch(e){}})();)JS";
        if (!EnqueueClickWarpScript(kHook))
            return false;
        g_CwJsHooked = true;
        return true;
    }

    // Defined further down, past the script plumbing this needs.
    bool EnqueuePlayerScript(const char* script) noexcept;

    // Ray against a vehicle's own box: the snapshot carries heading and half
    // extents, so the test is done in the vehicle's frame where the box is
    // axis-aligned and the whole thing is a slab intersection.
    bool RayHitsVehicle(
        const Vector3& origin,
        const Vector3& direction,
        const RageWorldEntities::Entry& entry,
        float& outDistance) noexcept
    {
        const float radians = entry.heading * 3.14159265f / 180.f;
        const float sine = std::sin(radians);
        const float cosine = std::cos(radians);

        const float dx = origin.x - entry.x;
        const float dy = origin.y - entry.y;
        const Vector3 localOrigin(
            dx * cosine + dy * sine,
            -dx * sine + dy * cosine,
            origin.z - entry.z);
        const Vector3 localDirection(
            direction.x * cosine + direction.y * sine,
            -direction.x * sine + direction.y * cosine,
            direction.z);

        const float half[3] = {
            entry.halfExtentX, entry.halfExtentY, entry.halfExtentZ};
        const float start[3] = {
            localOrigin.x, localOrigin.y, localOrigin.z};
        const float step[3] = {
            localDirection.x, localDirection.y, localDirection.z};

        // Not "near" and "far": those are Windows macros and turn this into a
        // syntax error the moment windows.h is in scope.
        float entry_ = 0.f;
        float exit_ = 1.0e9f;
        for (int axis = 0; axis < 3; ++axis)
        {
            if (std::fabs(step[axis]) < 1.0e-6f)
            {
                if (std::fabs(start[axis]) > half[axis])
                    return false;
                continue;
            }
            const float inverse = 1.f / step[axis];
            float first = (-half[axis] - start[axis]) * inverse;
            float second = (half[axis] - start[axis]) * inverse;
            if (first > second)
            {
                const float swap = first;
                first = second;
                second = swap;
            }
            if (first > entry_)
                entry_ = first;
            if (second < exit_)
                exit_ = second;
            if (entry_ > exit_)
                return false;
        }
        if (exit_ < 0.f)
            return false;
        outDistance = entry_;
        return true;
    }

    // Sends the server's own "open this vehicle's inventory" event for whatever
    // vehicle the camera is pointing at. The target is chosen here rather than
    // in script because the snapshot already knows every vehicle's box, id and
    // trunk state, and because mp.raycasting only names an entity from inside a
    // render event, which a keypress is not.
    void OpenAimedTrunkInventory() noexcept
    {
        Vector3 origin{};
        Vector3 look{};
        if (!ReadViewportLook(origin, look))
            return;

        static RageWorldEntities::Entry entries[
            RageWorldEntities::kMaxEntries]{};
        const int count = RageWorldEntities::CopySnapshot(
            entries, RageWorldEntities::kMaxEntries);

        std::uint32_t bestId = RageWorldEntities::kInvalidRemoteId;
        float bestDistance = 0.f;
        bool bestTrunk = false;
        for (int index = 0; index < count; ++index)
        {
            const RageWorldEntities::Entry& entry = entries[index];
            if (!entry.valid ||
                entry.kind != RageWorldEntities::EntityKind::Vehicle ||
                entry.remoteId == RageWorldEntities::kInvalidRemoteId)
                continue;
            float distance = 0.f;
            if (!RayHitsVehicle(origin, look, entry, distance))
                continue;
            // An open trunk wins over a nearer closed one: the whole point is
            // to reach the car that can actually be looted.
            const bool better =
                bestId == RageWorldEntities::kInvalidRemoteId ||
                (entry.trunkOpen && !bestTrunk) ||
                (entry.trunkOpen == bestTrunk && distance < bestDistance);
            if (!better)
                continue;
            bestId = entry.remoteId;
            bestDistance = distance;
            bestTrunk = entry.trunkOpen;
        }

        if (bestId == RageWorldEntities::kInvalidRemoteId)
            return;

        // The server takes this over its own RPC bridge rather than as a plain
        // remote event, and the vehicle is passed by remote id in the wrapper
        // shape the bridge expects.
        char script[420]{};
        _snprintf_s(
            script,
            sizeof(script),
            _TRUNCATE,
            "try{mp.events.callRemote('__rpc:process',JSON.stringify({"
            "req:1,id:'bf'+Date.now().toString(36),"
            "name:'__rpc:triggerEvent',env:'client',"
            "args:['server_vehicle_openInventory',{'__t':'v',i:%u}],"
            "fenv:'cef',noRet:1}));}catch(e){}",
            static_cast<unsigned>(bestId));
        EnqueuePlayerScript(script);
    }

    bool PlayerV8Ready() noexcept
    {
        const auto status = RageV8Runtime::GetStatus();
        if (status.state != RageV8Runtime::State::Ready ||
            !status.rootCaptured)
            return false;
        if (status.generation != g_PlJsGeneration)
        {
            // A new V8 generation means our hook is gone with the old context.
            g_PlJsHooked = false;
            g_PlJsPushed = false;
            g_PlJsLastState[0] = '\0';
            g_PlVehQueryId = 0;
            g_PlJsGeneration = status.generation;
        }
        return true;
    }

    bool EnqueuePlayerScript(const char* script) noexcept
    {
        if (!script || !PlayerV8Ready())
            return false;
        return RageV8Runtime::EnqueueFireAndForget(script);
    }

    // One persistent render hook owns everything that has to run on the script
    // thread every frame: on-foot speed, super jump, stamina, ammo and the
    // vehicle frame natives. It also publishes the single vehicle state that
    // C++ reads back, so speedhack, teleport and the Cars page stop disagreeing
    // about whether the player is seated.
    bool EnsurePlayerHook() noexcept
    {
        if (g_PlJsHooked)
            return true;
        static constexpr char kHook[] = R"JS((function(){try{
var g=typeof globalThis!=='undefined'?globalThis:this;
g.__bfPl=g.__bfPl||{};
/* Removed before being re-added, like the movement and click-warp hooks. The
guard flag this replaces made re-injection a no-op: the handler from the
previous payload stayed registered and kept answering with its own logic, so a
rebuilt hook changed nothing until the whole V8 context was recreated. */
if(g.__bfPlFn){try{mp.events.remove('render',g.__bfPlFn);}catch(e){}}
/* Left null until the first render tick, so C++ can tell "the hook is up and
you are on foot" from "the hook never ran" and keep its own fallback. */
g.__bfVeh=null;
function nz(v,d){return (typeof v==='number'&&isFinite(v))?v:d;}
function cl(v,lo,hi){v=nz(v,lo);return v<lo?lo:(v>hi?hi:v);}
function inv(){try{return mp.game.invoke.apply(mp.game,arguments);}catch(e){return 0;}}
/* Which wrappers this build actually exposes, reported once. Upper case means
present. Guessing native hashes has already cost a "Script Server Error" popup,
so what exists is measured instead. */
g.__bfPl.caps=(function(){
var o='';
try{o+=(mp.game.ped&&typeof mp.game.ped.setPedCanBeKnockedOffVehicle==='function')?'K':'k';}catch(e){o+='?';}
try{o+=(mp.game.ped&&typeof mp.game.ped.setPedConfigFlag==='function')?'C':'c';}catch(e){o+='?';}
try{o+=(mp.game.vehicle&&typeof mp.game.vehicle.getPedInVehicleSeat==='function')?'S':'s';}catch(e){o+='?';}
try{o+=(mp.game.vehicle&&typeof mp.game.vehicle.setVehicleGravity==='function')?'G':'g';}catch(e){o+='?';}
try{o+=(mp.game.ui&&typeof mp.game.ui.getFirstBlipInfoId==='function')?'B':'b';}catch(e){o+='?';}
try{o+=(mp.game.gameplay&&typeof mp.game.gameplay.getGroundZFor3dCoord==='function')?'Z':'z';}catch(e){o+='?';}
return o;
})();
g.__bfAfkPoke=function(){
try{if(typeof g.antiAFK_sendAction==='function'){g.antiAFK_sendAction();return true;}}catch(e){}
try{
var api=g.__butterflyFiveRpApi||g.api;
if(api&&typeof api.antiAFK_sendAction==='function'){api.antiAFK_sendAction();return true;}
}catch(e){}
return false;
};
g.__bfPlFn=function(){
var s=g.__bfPl;if(!s)return;
var p=mp.players&&mp.players.local;if(!p)return;
var h=0;try{h=p.handle;}catch(e){return;}
if(!h)return;

/* ---- vehicle state: the one answer everything else reads ---- */
var v=null;try{v=p.vehicle||null;}catch(e){v=null;}
var vh=0;try{vh=(v&&v.handle)?v.handle:0;}catch(e){vh=0;}
var inVeh=!!vh;
if(!inVeh){try{inVeh=!!p.isInAnyVehicle(false);}catch(e){}}
var driver=false;
if(vh){
try{driver=(mp.game.vehicle.getPedInVehicleSeat(vh,-1)===h);}catch(e){}
/* GET_PED_IN_VEHICLE_SEAT through invoke as well: the wrapper above is one of
the surfaces that does nothing here, and with only that check the driver seat
always read as taken by someone else, so vehicle features stayed disabled. */
if(!driver){try{driver=(inv('0xBB40DD2270B65366',vh,-1,0)===h);}catch(e){}}
if(!driver){try{driver=(p.seat===-1);}catch(e){}}
}
var entering=false;try{entering=!!p.isGettingIntoVehicle();}catch(e){}
g.__bfVeh={in:inVeh,driver:driver,entering:entering,handle:vh};

/* ---- on-foot speed ---- */
if(s.speed&&!inVeh){
var mul=cl(s.speedMul,1,10);
/* The sprint multiplier saturates around 1.49; the move-rate override is
what actually carries values past that. */
try{mp.game.player.setRunSprintMultiplierFor(mul>1.49?1.49:mul);}catch(e){}
inv('0x085BF80FA50A39D1',h,mul);
s._spd=true;
}else if(s._spd){
s._spd=false;
try{mp.game.player.setRunSprintMultiplierFor(1);}catch(e){}
inv('0x085BF80FA50A39D1',h,1);
}

/* ---- trigger shot ----
INPUT_ATTACK is a this-frame control value, and Present never delivers it.
C++ arms a short window and the assertion happens here, on the thread the
game actually reads controls from. */
if(g.__bfShoot&&Date.now()<g.__bfShoot){
inv('0xE8A25867FBA3B05E',0,24,1.0);
/* RAGE's wrapper for that native is setControlNormal; there is no
setControlValueNextFrame on mp.game.controls. */
try{mp.game.controls.setControlNormal(0,24,1.0);}catch(e){}
}

/* ---- super jump: a this-frame flag, so it needs every render ---- */
if(s.jump){
/* The native takes a player index, and so does the wrapper. Called with no
argument it did nothing and threw nothing, which is the worst of both: the
feature was dead and the fallback below never ran because there was no
exception to trigger it. */
var pid=-1;try{pid=mp.game.player.playerId();}catch(e){pid=-1;}
if(!(pid>=0))pid=-1;
if(pid>=0){
var jok=false;
try{
if(mp.game.gameplay&&typeof mp.game.gameplay.setSuperJumpThisFrame==='function'){
mp.game.gameplay.setSuperJumpThisFrame(pid);jok=true;}
}catch(e){}
if(!jok)inv('0x57FFF03E423A4C0B',pid);
}
}
if(s.stamina){try{mp.game.player.restoreStamina(1);}catch(e){}}

/* ---- ammo / clip / instant reload ---- */
if(s.infAmmo||s.infClip||s.noReload){
var w=0;try{w=p.weapon;}catch(e){w=0;}
if(!w)w=inv('0x0A6DB4965674D243',h);
if(w){
if(s.infAmmo)inv('0x3EDCB0505123623B',h,true,w);
if(s.infClip)inv('0x183DADC6AA953186',h,true);
if(s.noReload){
/* Refill the clip only while a reload is actually running, so the reload
ends immediately instead of the magazine never draining. */
var busy=false;
try{busy=!!p.isReloading();}catch(e){}
if(!busy)busy=!!inv('0x8B9F1FC6AE8166C0',h);
if(busy){
var mx=inv('0xA38DCFFCEA8962FA',h,w,true);
if(mx>0){inv('0xDCD2A934D65CB497',h,w,mx);
try{p.clearTasks();}catch(e){}}
}
}
s._ammoW=w;
}
s._ammo=true;
}else if(s._ammo){
s._ammo=false;
inv('0x183DADC6AA953186',h,false);
if(s._ammoW)inv('0x3EDCB0505123623B',h,false,s._ammoW);
s._ammoW=0;
}

/* ---- anti-AFK: server-side activity, not camera jitter ---- */
if(s.afk){
var now=Date.now();
if(!s._afkAt||now-s._afkAt>s.afkMs){
s._afkAt=now;
try{g.__bfAfkPoke();}catch(e){}
try{if(typeof g.antiAFK_sendAFK==='function')g.antiAFK_sendAFK=function(){};}catch(e){}
}
}else{s._afkAt=0;}

/* ---- waypoint teleport: multi-frame, on the script thread ---- */
var tp=s._tp;
if(!tp)tp=s._tp={seq:0,phase:0,x:0,y:0,at:0};
if((s.tpSeq|0)!==tp.seq){
tp.seq=s.tpSeq|0;tp.phase=0;
var wx=0,wy=0,got=false;
try{
var b=mp.game.ui.getFirstBlipInfoId(8);
while(b&&mp.game.ui.doesBlipExist(b)){
var c=null;
try{c=mp.game.ui.getBlipInfoIdCoord(b);}catch(e){}
if(c&&(c.x||c.y)){
wx=c.x;wy=c.y;got=true;
var ty=0;try{ty=mp.game.ui.getBlipInfoIdType(b);}catch(e){}
if(ty===4)break;
}
b=mp.game.ui.getNextBlipInfoId(8);
}
}catch(e){}
if(got){tp.x=wx;tp.y=wy;tp.at=Date.now();tp.phase=1;
g.__bfTp={seq:tp.seq,state:'probing'};}
else g.__bfTp={seq:tp.seq,state:'nomark'};
}
if(tp.phase===1){
try{mp.game.streaming.requestCollisionAtCoord(tp.x,tp.y,0);}catch(e){}
try{mp.game.streaming.requestCollisionAtCoord(tp.x,tp.y,300);}catch(e){}
var gz=null;
try{
var hit=mp.raycasting.testPointToPoint(
new mp.Vector3(tp.x,tp.y,1200),
new mp.Vector3(tp.x,tp.y,-250),p,1|16);
if(hit&&hit.position&&isFinite(hit.position.z))gz=hit.position.z;
}catch(e){}
if(gz===null){
try{var r=mp.game.gameplay.getGroundZFor3dCoord(tp.x,tp.y,1200,false);
if(typeof r==='number'&&isFinite(r)&&r>-200&&r<1000)gz=r;}catch(e){}
}
if(gz!==null&&gz>-200&&gz<1000){
var ent=(vh&&driver&&v)?v:p;
/* The server's own scripts announce a coming position jump before they
move the player; a warp that skips this looks like what they hunt. */
try{mp.events.call('c:ac:check',3000);}catch(e){}
try{ent.setCoordsNoOffset(tp.x,tp.y,gz+1,false,false,false);}catch(e){}
try{ent.setVelocity(0,0,0);}catch(e){}
tp.phase=0;
var moved=false;
try{var np=ent.position;
moved=(Math.abs(np.x-tp.x)<10&&Math.abs(np.y-tp.y)<10);}catch(e){}
g.__bfTp={seq:tp.seq,state:moved?'ok':'blocked'};
}else if(Date.now()-tp.at>6000){
tp.phase=0;
g.__bfTp={seq:tp.seq,state:'noground'};
}
}

/* ---- vehicle frame natives (driver only) ---- */
if(s.vSeatbelt&&inVeh){
/* Two halves, and both are needed: the config flag stops you leaving through
the windscreen in a car, the knock-off state stops you being thrown off a bike.
The hash here used to read 0x7A653569DA385A54, which shares only its first half
with the real one and is what RAGE answered with "Invalid native has been
passed"; the canonical value is 0x7A6535691B477C48, state 1 meaning never. */
inv('0x7A6535691B477C48',h,1);
inv('0x1913FE4CBF41C463',h,32,false);
}
if(s.vSkipAnim){
/* Not gated on the `entering` flag any more. That came from
p.isGettingIntoVehicle(), one of the wrappers that answers nothing here, so it
read false forever and this never ran at all. The native below returns zero when
nobody is getting in, which gates it just as well and does not depend on a
surface that may not exist.
p.vehicle is still null while the enter animation plays, so the target has to
come from the "trying to enter" native rather than from vh. */
if(!inVeh){
/* Through the wrapper, never through invoke. The hash for
GET_VEHICLE_PED_IS_TRYING_TO_ENTER is documented and correct, but this RAGE
build does not carry it in its table, and an unknown hash there is not a quiet
failure: the client puts up "Invalid native has been passed" every time. It
fired on every exit, because leaving a vehicle is exactly when this branch
starts running. Absent wrapper means the enter half simply does nothing. */
var tv=0;
try{
var pedApi=mp.game.ped;
if(pedApi&&typeof pedApi.getVehiclePedIsTryingToEnter==='function')
tv=pedApi.getVehiclePedIsTryingToEnter(h)|0;
}catch(e){tv=0;}
if(tv){
/* The seat the game was actually taking. Forcing -1 put you behind the wheel of
a car you were boarding as a passenger, and did nothing at all when the driver
seat was occupied. */
var ts=-1;
try{
var pedApi2=mp.game.ped;
if(pedApi2&&typeof pedApi2.getSeatPedIsTryingToEnter==='function')
ts=pedApi2.getSeatPedIsTryingToEnter(h)|0;
}catch(e){ts=-1;}
if(!(ts>=-1&&ts<=15))ts=-1;
/* This one is left on invoke: the same hash is used elsewhere in this file and
has been seen to work, so it is known to be in the table. */
inv('0xF75B0D629E1C063D',h,tv,ts);
}
}else{
/* The exit half, which the label promised and the code never had. Clearing the
ped's tasks while seated drops you out on the spot, so all this needs is to
notice the exit being requested. Both control surfaces are consulted because
servers disable the stock one. */
var wantOut=false;
try{
var c=mp.game.controls;
if(c){
if(typeof c.isControlJustPressed==='function')
wantOut=wantOut||!!c.isControlJustPressed(0,75);
if(typeof c.isDisabledControlJustPressed==='function')
wantOut=wantOut||!!c.isDisabledControlJustPressed(0,75);
}
}catch(e){}
if(!wantOut){
wantOut=!!inv('0x580417101DDB492F',0,75)||
!!inv('0x91AEF906BCA88877',0,75);
}
if(wantOut){
var cok=false;
try{
if(mp.game.ped&&typeof mp.game.ped.clearPedTasksImmediately==='function'){
mp.game.ped.clearPedTasksImmediately(h);cok=true;}
}catch(e){}
if(!cok)inv('0xAAA34F8A7CB32098',h);
}
}
}
if(vh&&driver){
if(s.vSpeed){
var vm=cl(s.vSpeedMul,1,10);
inv('0x93A3996368C94158',vh,vm);
inv('0xB59E4BD37AE292DB',vh,vm);
s._vs=vh;
}else if(s._vs){
inv('0x93A3996368C94158',s._vs,1);
inv('0xB59E4BD37AE292DB',s._vs,1);
s._vs=0;
}
if(s.vEngine)inv('0x2497C4717C8B881E',vh,true,false,false);
/* ---- autopilot to the map waypoint ----
Lives here because the C++ path is built on native calls and none resolve on
this target. The blip walk is the same one the waypoint teleport uses: type 4 is
the waypoint proper, and the loop keeps the last candidate in case only the
route blips exist. */
if(s.vAuto&&driver){
var anow=Date.now();
if(!s._apAt||anow-s._apAt>1000){
s._apAt=anow;
var ax=0,ay=0,agot=false,ablips=0;
try{
var ab=mp.game.ui.getFirstBlipInfoId(8);
while(ab&&mp.game.ui.doesBlipExist(ab)&&ablips<64){
++ablips;
var ac=null;try{ac=mp.game.ui.getBlipInfoIdCoord(ab);}catch(e){}
if(ac&&(ac.x||ac.y)){
ax=ac.x;ay=ac.y;agot=true;
var aty=0;try{aty=mp.game.ui.getBlipInfoIdType(ab);}catch(e){}
if(aty===4)break;
}
ab=mp.game.ui.getNextBlipInfoId(8);
}
}catch(e){if(s.dbg)g.__bfPl.apdbg='walk threw';}
/* Only assembled when someone is going to read it. This formats two floats and
concatenates on every autopilot cycle, and the reply it rides along in is polled
four times a second, so in a build with no log to write to it was pure waste. */
if(s.dbg)g.__bfPl.apdbg='blips='+ablips+(agot?(' at='+ax.toFixed(0)+','+ay.toFixed(0)):' nomark');
if(agot){
/* The destination needs a usable height. A map waypoint carries none, and the
ground probe cannot answer for terrain that is not streamed in yet, so this used
to hand the task z=0, which is sea level: the target sat below the road, no
route could be found, and the driver simply held the handbrake. That is exactly
what the log showed, z=0 on every cycle.
Preference order is the nearest road node, then the ground probe, then our own
height, which is wrong by metres rather than by the whole map. */
var az=0,azsrc='none';
try{
var an=mp.game.pathfind&&mp.game.pathfind.getClosestVehicleNode
?mp.game.pathfind.getClosestVehicleNode(ax,ay,0,1,3.0,0):null;
var anp=an&&(an.position||an);
if(anp&&isFinite(anp.z)&&(anp.x||anp.y)){
ax=anp.x;ay=anp.y;az=anp.z;azsrc='node';
}
}catch(e){}
if(azsrc==='none'){
try{
var ag=mp.game.gameplay.getGroundZFor3dCoord(ax,ay,1200,false);
if(typeof ag==='number'&&isFinite(ag)&&ag>-200&&ag<1000&&ag!==0){
az=ag;azsrc='ground';
}
}catch(e){}
}
if(azsrc==='none'){
try{var mp2=p.position;if(mp2&&isFinite(mp2.z)){az=mp2.z;azsrc='self';}}catch(e){}
}
/* Re-issued only when it means something: when the destination has actually
moved, when nothing is running yet, or when the car has been stationary long
enough to count as stuck. It used to go out every four seconds regardless, and
each call restarts the task, which is why the car never built up speed.

The driving style is assembled from the documented flags rather than copied as a
magic number. Steering around obstacles is in; stopping for cars, for peds and
at lights is deliberately out, since those are what made it crawl. */
var amoved=(!s._apx)||Math.abs(ax-s._apx)>5||Math.abs(ay-s._apy)>5;
/* GET_ENTITY_SPEED, not the velocity property. That property reads back zero
here, so the stuck detector believed the car was standing still and re-issued
the task, and every re-issue restarts it: the car crawled because it was never
allowed to finish accelerating. */
var aspd=0;
try{var asv=inv('0xD5037BA82E12416F',vh);
if(typeof asv==='number'&&isFinite(asv))aspd=asv;}catch(e){}
if(aspd>1.5)s._apMove=anow;
var astuck=(anow-(s._apMove||anow))>3000;
if(amoved||!s._ap||astuck){
s._apx=ax;s._apy=ay;s._apMove=anow;
inv('0x158BB33F920D360C',h,vh,ax,ay,az,60.0,
4|8|16|32|262144|524288,20.0);
s._ap=1;
if(s.dbg)g.__bfPl.apdbg+=' z='+az.toFixed(0)+'/'+azsrc+' sent';
}else if(s.dbg){
g.__bfPl.apdbg+=' running v='+aspd.toFixed(1);
}
/* Re-asserted every cycle: the task otherwise settles back to a cruise speed
of its own and the requested one is forgotten. */
inv('0x5C9B84BD7D31D908',h,60.0);
}
}
}else if(s._ap){
s._ap=0;s._apAt=0;s._apx=0;s._apy=0;s._apMove=0;
try{p.clearTasks();}catch(e){}
}
if(s.vGrav){
/* Extra gravity as a velocity delta, not a force.
APPLY_FORCE_TO_ENTITY was applied at the entity origin, which is not the centre
of mass, so every frame added torque and the car tumbled. It also passed
isForceRel true, meaning the vector was read in the car's own axes, so "down"
became "sideways" the moment it tilted and the car was thrown off the road. And
the magnitude was a fixed multiple per frame, so the whole effect scaled with
frame rate.
A velocity delta has no torque, no orientation ambiguity, and scales with real
time, so the multiplier means the same thing at any frame rate. */
var mul=cl(s.vGravMul,1,30);
if(mul>1.01){
var now2=Date.now();
var dt=(now2-(s._gt||now2))/1000;
s._gt=now2;
if(dt>0.005&&dt<0.05){
/* A world-space impulse at the centre of mass, which needs no velocity read.
Reading the velocity property and writing it back was worse than useless here:
it reports zero on this build, so the write pinned the car to a standstill,
which is the handbrake feel. Centre of mass means no torque, so no tumbling,
and world space means "down" stays down whatever the car is doing. */
inv('0x18FF00FC7EFF559E',vh,1,0,0,-(mul-1)*9.8*dt,
false,false,false,true);
}
}else{s._gt=0;}
}else{s._gt=0;}
}else if(s._vs){
inv('0x93A3996368C94158',s._vs,1);inv('0xB59E4BD37AE292DB',s._vs,1);s._vs=0;
}
};
mp.events.add('render',g.__bfPlFn);
}catch(e){}})();)JS";
        if (!EnqueuePlayerScript(kHook))
            return false;
        g_PlJsHooked = true;
        return true;
    }

    bool PushPlayerState() noexcept
    {
        const auto& vm = VehicleMovement::GetSettings();

        // Installed unconditionally: the hook is also what publishes vehicle
        // state, and that answer is needed even when no toggle is on.
        if (!EnsurePlayerHook())
            return false;

        if (g_AntiAfkNextPokeAt == 0)
            g_AntiAfkNextPokeAt = 22000 +
                static_cast<ULONGLONG>(rand() % 13000);

        char state[512]{};
        sprintf_s(
            state,
            "try{var g=typeof globalThis!=='undefined'?globalThis:this;"
            "g.__bfPl=g.__bfPl||{};var s=g.__bfPl;"
            "s.speed=%s;s.speedMul=%.2f;s.jump=%s;s.stamina=%s;"
            "s.infAmmo=%s;s.infClip=%s;s.noReload=%s;"
            "s.afk=%s;s.afkMs=%llu;s.tpSeq=%d;"
            "s.vSeatbelt=%s;s.vSkipAnim=%s;s.vSpeed=%s;s.vSpeedMul=%.2f;"
            "s.vGrav=%s;s.vGravMul=%.2f;s.vEngine=%s;"
            "s.vAuto=%s;s.dbg=%s;}catch(e){}",
            g_Settings.speedhack ? "true" : "false",
            g_Settings.speedhackValue,
            g_Settings.superJump ? "true" : "false",
            g_Settings.unlimitedStamina ? "true" : "false",
            g_Settings.infiniteAmmo ? "true" : "false",
            g_Settings.infiniteClip ? "true" : "false",
            g_Settings.noReload ? "true" : "false",
            g_Settings.antiAfk ? "true" : "false",
            g_AntiAfkNextPokeAt,
            g_TpSeq,
            vm.seatbelt ? "true" : "false",
            vm.skipEnterAnim ? "true" : "false",
            vm.speedEnable ? "true" : "false",
            vm.speedMult,
            vm.gravityEnable ? "true" : "false",
            vm.gravityMult,
            vm.engineForceOn ? "true" : "false",
            vm.autopilot ? "true" : "false",
            // Off in a shipping build, so the bookkeeping strings below are not
            // assembled at all rather than merely not printed.
#if defined(RAGEMP_DIAGNOSTICS)
            "true"
#else
            "false"
#endif
        );

        if (g_PlJsPushed &&
            std::strcmp(state, g_PlJsLastState) == 0)
            return true;
        if (!EnqueuePlayerScript(state))
            return false;
        strcpy_s(g_PlJsLastState, state);
        g_PlJsPushed = true;
        return true;
    }

    // Vehicle state is read back instead of being probed from Present, so the
    // Cars page, the on-foot speed skip and teleport all answer from one place.
    void PollVehicleState() noexcept
    {
        const bool ready = PlayerV8Ready();
#if defined(RAGEMP_DIAGNOSTICS)
        {
            static ULONGLONG s_lastLog = 0;
            const ULONGLONG stamp = GetTickCount64();
            if (stamp - s_lastLog > 2000)
            {
                s_lastLog = stamp;
                char line[128]{};
                _snprintf_s(
                    line,
                    sizeof(line),
                    _TRUNCATE,
                    "[Misc] vehq v8=%d hooked=%d id=%llu",
                    ready ? 1 : 0,
                    g_PlJsHooked ? 1 : 0,
                    static_cast<unsigned long long>(g_PlVehQueryId));
                hooks::HLog(line);
            }
        }
#endif
        if (!ready || !g_PlJsHooked)
        {
            VehicleMovement::ClearLocalVehicleState();
            return;
        }

        if (g_PlVehQueryId != 0)
        {
            // Abandoned after a while. A result that never arrives used to park
            // the id forever, and every later cycle returned right here, so one
            // lost reply stopped vehicle detection for the rest of the session.
            const ULONGLONG pendingSince = g_PlVehQueryAt;
            if (pendingSince != 0 &&
                GetTickCount64() - pendingSince > 2000)
            {
                g_PlVehQueryId = 0;
                hooks::HLog(
                    "[Misc] vehicle poll timed out; retrying");
                return;
            }
            RageV8Runtime::Result result{};
            if (RageV8Runtime::PollResultFor(g_PlVehQueryId, result))
            {
                g_PlVehQueryId = 0;
                int published = 0;
                int inVehicle = 0;
                int driver = 0;
                int entering = 0;
                int handle = 0;
                const int parsed = result.success
                    ? sscanf_s(
                          result.text,
                          "%d %d %d %d %d",
                          &published,
                          &inVehicle,
                          &driver,
                          &entering,
                          &handle)
                    : 0;
#if defined(RAGEMP_DIAGNOSTICS)
                {
                    char line[160]{};
                    _snprintf_s(
                        line,
                        sizeof(line),
                        _TRUNCATE,
                        "[Misc] vehq reply ok=%d n=%d text=%.140s",
                        result.success ? 1 : 0,
                        parsed,
                        result.text ? result.text : "(null)");
                    hooks::HLog(line);
                }
#endif
                if (parsed == 5)
                {
                    if (published != 0)
                    {
                        VehicleMovement::SetLocalVehicleState(
                            inVehicle != 0,
                            driver != 0,
                            entering != 0,
                            handle);
                    }
                    else
                    {
                        // Hook is queued but has not ticked yet: keep using the
                        // memory/native fallback instead of claiming "on foot".
                        VehicleMovement::ClearLocalVehicleState();
                    }
                }
            }
            return;
        }

        const ULONGLONG now = GetTickCount64();
        if (g_PlVehQueryAt != 0 && now - g_PlVehQueryAt < 150)
            return;
        g_PlVehQueryAt = now;

        // The leading return is load bearing: the runtime wraps a command in a
        // function and takes that function's return value, so an expression
        // statement evaluates and is thrown away. Without it this query answered
        // the string "undefined" on every cycle, the parse found no fields, and
        // vehicle state was never published at all.
        static constexpr char kQuery[] =
            "return (function(){try{"
            "var g=typeof globalThis!=='undefined'?globalThis:this;"
            "var v=g.__bfVeh;if(!v)return '0 0 0 0 0';"
            // The trailing capability word is ignored by the numeric parse and
            // exists only for the diagnostics log.
            "return '1 '+(v.in?1:0)+' '+(v.driver?1:0)+' '+"
            "(v.entering?1:0)+' '+(v.handle|0)+' '+"
            "((g.__bfPl&&g.__bfPl.caps)||'-')+' ap['+"
            "((g.__bfPl&&g.__bfPl.apdbg)||'-')+']';"
            "}catch(e){return '0 0 0 0 0';}})()";
        std::uint64_t id = 0;
        if (RageV8Runtime::Enqueue(kQuery, &id))
            g_PlVehQueryId = id;
    }

    void PollWaypointTeleport() noexcept
    {
        if (g_TpReportedSeq == g_TpSeq)
            return;

        if (g_TpQueryId != 0)
        {
            RageV8Runtime::Result result{};
            if (!RageV8Runtime::PollResultFor(g_TpQueryId, result))
                return;
            g_TpQueryId = 0;
            if (!result.success)
                return;
            int seq = 0;
            char state[32]{};
            if (sscanf_s(
                    result.text,
                    "%d %31s",
                    &seq,
                    state,
                    static_cast<unsigned>(sizeof(state))) != 2)
                return;
            if (seq != g_TpSeq || std::strcmp(state, "probing") == 0)
                return;

            g_TpReportedSeq = seq;
            if (std::strcmp(state, "ok") == 0)
            {
                Menu::PushNotification(
                    "TP Waypoint",
                    "Teleported to ground",
                    true,
                    2000);
            }
            else if (std::strcmp(state, "nomark") == 0)
            {
                Menu::PushNotification(
                    "TP Waypoint",
                    "No GPS mark (open map, set waypoint)",
                    false,
                    3000);
            }
            else if (std::strcmp(state, "blocked") == 0)
            {
                Menu::PushNotification(
                    "TP Waypoint",
                    "Move was rejected at the destination",
                    false,
                    3000);
            }
            else
            {
                Menu::PushNotification(
                    "TP Waypoint",
                    "Ground not found; teleport cancelled",
                    false,
                    3000);
            }
            return;
        }

        const ULONGLONG now = GetTickCount64();
        if (g_TpQueryAt != 0 && now - g_TpQueryAt < 200)
            return;
        g_TpQueryAt = now;

        // Same missing return as the vehicle query above, with the same effect:
        // the waypoint teleport never learned its own progress.
        static constexpr char kQuery[] =
            "return (function(){try{"
            "var g=typeof globalThis!=='undefined'?globalThis:this;"
            "var t=g.__bfTp;if(!t)return '0 none';"
            "return (t.seq|0)+' '+t.state;"
            "}catch(e){return '0 none';}})()";
        std::uint64_t id = 0;
        if (RageV8Runtime::Enqueue(kQuery, &id))
            g_TpQueryId = id;
    }

    void SyncPlayerScript() noexcept
    {
        if (!PlayerV8Ready())
        {
            g_PlJsPushed = false;
            g_PlJsLastState[0] = '\0';
            g_PlVehQueryId = 0;
            g_TpQueryId = 0;
            VehicleMovement::ClearLocalVehicleState();
#if RAGEMP_DEV_BUILD
            {
                static ULONGLONG s_lastLog = 0;
                const ULONGLONG now = GetTickCount64();
                if (now - s_lastLog > 10000)
                {
                    s_lastLog = now;
                    hooks::HLog(
                        "[Misc] player bridge idle: V8 not ready "
                        "(speed / jump / ammo / vehicle on fallback)");
                }
            }
#endif
            return;
        }
        PushPlayerState();
        PollVehicleState();
        PollWaypointTeleport();
    }

    bool PushClickWarpState(bool on, bool paused, int key) noexcept
    {
        if (on && !EnsureClickWarpHook())
            return false;
        if (!g_CwJsHooked && !on)
            return true;
#if defined(RAGEMP_DIAGNOSTICS)
        constexpr const char* kDebug = "true";
#else
        constexpr const char* kDebug = "false";
#endif
        char cmd[288]{};
        sprintf_s(
            cmd,
            "try{var g=typeof globalThis!=='undefined'?globalThis:this;"
            "g.__bfCw=g.__bfCw||{};"
            "g.__bfCw.on=%s;g.__bfCw.paused=%s;g.__bfCw.key=%d;"
            "g.__bfCw.dbg=%s;}catch(e){}",
            on ? "true" : "false",
            paused ? "true" : "false",
            key,
            kDebug);
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

    // Hands the aim ray to the script hook and takes back the point it probed,
    // in one command per cycle.
    //
    // The ray is read out of the viewport matrix, which is the camera the frame
    // is drawn with, so it needs no native and cannot disagree with the screen.
    // The script side used to derive it from GET_GAMEPLAY_CAM_COORD and that
    // yields nothing usable here: on foot the destination never resolved at all.
    //
    // A cycle costs a V8 round trip, so the point arrives tens of milliseconds
    // old. That does not show, because what is published is a world position and
    // the overlay reprojects it every frame: it stays welded to the ground while
    // the camera moves, which is also what stopped it flickering.
    void PollClickWarpAim() noexcept
    {
        if (!g_Settings.clickWarp || !g_CwJsOn)
        {
            g_CwHaveHint = false;
            g_CwHaveBox = false;
            g_CwAimQueryId = 0;
            return;
        }

        const ULONGLONG now = GetTickCount64();

        if (g_CwAimQueryId != 0)
        {
            // Same guard as the vehicle poll: a reply that never lands must not
            // park the id, or the marker stops updating for good.
            if (g_CwAimAt != 0 && now - g_CwAimAt > 2000)
            {
                g_CwAimQueryId = 0;
                return;
            }
            RageV8Runtime::Result result{};
            if (RageV8Runtime::PollResultFor(g_CwAimQueryId, result))
            {
                g_CwAimQueryId = 0;
                // Scanned as a plain sequence of numbers rather than through a
                // format string: the reply carries an optional 24-float box and
                // a fixed format for that is only a way to get it wrong.
                float values[32]{};
                int count = 0;
                if (result.success)
                {
                    const char* cursor = result.text;
                    while (count < 32)
                    {
                        char* end = nullptr;
                        const float parsed =
                            std::strtof(cursor, &end);
                        if (end == cursor)
                            break;
                        values[count++] = parsed;
                        cursor = end;
                    }
                }
                const int ok = count >= 4
                    ? static_cast<int>(values[0])
                    : 0;
                const float x = count >= 4 ? values[1] : 0.f;
                const float y = count >= 4 ? values[2] : 0.f;
                const float z = count >= 4 ? values[3] : 0.f;

                g_CwHaveBox = false;
                if (count >= 29 && values[4] != 0.f)
                {
                    bool boxOk = true;
                    for (int corner = 0; corner < 8; ++corner)
                    {
                        const float* v = &values[5 + corner * 3];
                        if (!std::isfinite(v[0]) ||
                            !std::isfinite(v[1]) ||
                            !std::isfinite(v[2]))
                        {
                            boxOk = false;
                            break;
                        }
                        g_CwBox[corner] = Vector3(v[0], v[1], v[2]);
                    }
                    g_CwHaveBox = boxOk;
                }

                if (ok != 0 &&
                    std::isfinite(x) && std::isfinite(y) && std::isfinite(z))
                {
                    // Only the distance is taken from the probe. The direction
                    // is known every frame, so keeping the point itself would
                    // pin the marker to where the aim was a round trip ago,
                    // which is the lag this avoids.
                    const Vector3 hit(x, y, z);
                    const float distance =
                        (hit - g_CwAimOrigin).Length();
                    if (std::isfinite(distance) &&
                        distance > 0.5f && distance < 400.f)
                    {
                        // Eased while the depth creeps, taken outright when it
                        // jumps: sliding across a 50 m step would look like the
                        // marker flying off.
                        g_CwHintDist =
                            (g_CwHaveHint &&
                             std::fabs(distance - g_CwHintDist) < 8.f)
                                ? g_CwHintDist +
                                      (distance - g_CwHintDist) * 0.5f
                                : distance;
                        g_CwHaveHint = true;
                        g_CwHintAt = now;
                    }
                }
            }
        }

        // The destination survives a few missed cycles. Dropping it the moment
        // one probe comes back empty is the flicker this replaced.
        if (g_CwHaveHint && now - g_CwHintAt > 1500)
        {
            g_CwHaveHint = false;
            g_CwHaveBox = false;
        }

        if (g_CwAimQueryId != 0 || now - g_CwAimAt < 60)
            return;

        Vector3 cam{};
        Vector3 look{};
        if (!ReadViewportLook(cam, look))
            return;
        g_CwAimAt = now;
        g_CwAimOrigin = cam;

        // Truncating rather than sprintf_s: generated script text grows as this
        // evolves, and an overrun here is not a bad string, it is a __fastfail
        // that takes the game with it. A truncated command is caught by the
        // reply parse instead.
        char cmd[320]{};
        if (_snprintf_s(
                cmd,
                sizeof(cmd),
                _TRUNCATE,
                "return (function(){try{"
                "var g=typeof globalThis!=='undefined'?globalThis:this;"
                "var s=g.__bfCw;if(!s)return '0 0 0 0 0';"
                "s.ox=%.3f;s.oy=%.3f;s.oz=%.3f;"
                "s.dx=%.5f;s.dy=%.5f;s.dz=%.5f;s.rayAt=Date.now();"
                "return s.rep?s.rep():'0 0 0 0 0';"
                "}catch(e){return '0 0 0 0 0';}})()",
                cam.x, cam.y, cam.z, look.x, look.y, look.z) < 0)
            return;
        if (!RageV8Runtime::Enqueue(cmd, &g_CwAimQueryId))
            g_CwAimQueryId = 0;
    }

    // The aimed vehicle, outlined so it is obvious that the click will seat you
    // rather than drop you on the roof. The corners arrive in world space from
    // the script side and are reprojected here every frame, so the outline
    // tracks the car even between probe cycles.
    void RenderClickWarpVehicleBox(ImDrawList* draw) noexcept
    {
        if (!draw || !g_CwHaveBox)
            return;

        ImVec2 corner[8]{};
        for (int index = 0; index < 8; ++index)
        {
            const auto projected = Game::WorldToScreen(g_CwBox[index]);
            if (!projected)
                return;
            corner[index] = ImVec2(projected->x, projected->y);
        }

        // Indices differing in exactly one bit share an edge.
        static constexpr int kEdges[12][2] = {
            {0, 1}, {2, 3}, {4, 5}, {6, 7},
            {0, 2}, {1, 3}, {4, 6}, {5, 7},
            {0, 4}, {1, 5}, {2, 6}, {3, 7},
        };
        constexpr ImU32 kLine = IM_COL32(120, 205, 255, 200);
        constexpr ImU32 kNode = IM_COL32(226, 246, 255, 240);
        for (const auto& edge : kEdges)
        {
            draw->AddLine(
                corner[edge[0]], corner[edge[1]], kLine, 1.6f);
        }
        for (const ImVec2& point : corner)
            draw->AddCircleFilled(point, 2.f, kNode, 8);
    }

    // Drawn by the overlay rather than by a world marker native. Reprojecting
    // one world position every frame is what makes it steady: the script-side
    // marker was redrawn from a fresh probe each frame and blinked whenever one
    // missed, and it could not be styled beyond the shapes the game ships.
    void RenderClickWarpMarker() noexcept
    {
        if (!g_Settings.clickWarp || !g_CwHaveHint)
            return;
        // Placed on this frame's ray at the probed depth, not at the point the
        // probe returned. The probe is a V8 round trip behind, so pinning the
        // returned point made the marker trail the crosshair while turning;
        // only the depth actually needs the probe, and depth changes slowly.
        Vector3 cam{};
        Vector3 look{};
        if (!ReadViewportLook(cam, look))
            return;
        const auto screen = Game::WorldToScreen(
            cam + look * g_CwHintDist);
        if (!screen)
            return;
        ImDrawList* draw = Overlay::GetDrawList();
        if (!draw)
            return;

        const ImVec2 p(screen->x, screen->y);
        const float time =
            static_cast<float>(GetTickCount64() % 6283) * 0.001f;

        constexpr ImU32 kEdge = IM_COL32(120, 205, 255, 235);
        constexpr ImU32 kCore = IM_COL32(226, 246, 255, 245);
        constexpr ImU32 kGlow = IM_COL32(80, 170, 255, 60);

        draw->AddCircleFilled(p, 14.f, kGlow, 24);
        // Three arcs on a slow spin. Continuous motion reads as a live target;
        // a static ring next to a moving world is what looks broken.
        for (int arc = 0; arc < 3; ++arc)
        {
            const float from =
                time * 1.6f + static_cast<float>(arc) * 2.0944f;
            draw->PathArcTo(p, 10.5f, from, from + 1.15f, 12);
            draw->PathStroke(kEdge, 0, 2.2f);
        }
        draw->AddCircleFilled(p, 3.2f, kCore, 12);

        const float bob = std::sin(time * 2.4f) * 2.f;
        const ImVec2 tip(p.x, p.y - 20.f + bob);
        draw->AddTriangleFilled(
            ImVec2(tip.x, tip.y + 7.f),
            ImVec2(tip.x - 6.f, tip.y - 4.f),
            ImVec2(tip.x + 6.f, tip.y - 4.f),
            kEdge);

        RenderClickWarpVehicleBox(draw);
    }

    void ApplyClickWarp() noexcept
    {
        SyncClickWarpScript();
        PollClickWarpAim();

        const auto v8 = RageV8Runtime::GetStatus();
        if (v8.state == RageV8Runtime::State::Ready &&
            v8.rootCaptured)
        {
            // The click is owned here rather than by the script hook, which is
            // what keeps the jump and the marker agreeing: both are built from
            // this frame's ray, and the hook re-probes it on arrival instead of
            // reusing a destination that is a round trip old. Reading it in
            // both places would also fire two jumps for one press.
            const int key = g_Settings.clickWarpKey > 0
                ? g_Settings.clickWarpKey
                : 0x02;
            const bool down =
                g_Settings.clickWarp &&
                g_CwJsOn &&
                !Menu::IsVisible() &&
                (GetAsyncKeyState(key) & 0x8000) != 0;
            if (down && !g_ClickWarpKeyWasDown)
            {
                Vector3 cam{};
                Vector3 look{};
                if (ReadViewportLook(cam, look))
                {
                    char cmd[288]{};
                    if (_snprintf_s(
                            cmd,
                            sizeof(cmd),
                            _TRUNCATE,
                            "try{var g=typeof globalThis!=='undefined'?"
                            "globalThis:this;var s=g.__bfCw;"
                            "if(s&&s.go)s.go(%.3f,%.3f,%.3f,%.5f,%.5f,%.5f);"
                            "}catch(e){}",
                            cam.x, cam.y, cam.z, look.x, look.y, look.z) >= 0)
                        EnqueueClickWarpScript(cmd);
                }
            }
            g_ClickWarpKeyWasDown = down;
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

        // Stamina, super jump and the ammo natives are this-frame calls that
        // only land on the script thread; the V8 hook owns them. What is left
        // here is a fallback for a session where V8 never came up.
        const bool scriptOwnsPlayer = PlayerV8Ready() && g_PlJsHooked;
        if (!scriptOwnsPlayer)
        {
            // PLAYER_ID() is 0 for the local player, so the old `if (player)`
            // guard threw away every valid id and neither of these ever ran.
            if (g_Settings.unlimitedStamina)
                NativeInvoke::RestorePlayerStamina(player, 1.f);
            if (g_Settings.superJump)
                NativeInvoke::SetSuperJumpThisFrame(player);

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
            // Queue() only reaches Quant servers, so repair did nothing
            // everywhere else. Go straight to the script thread instead.
            static constexpr char kRepairJs[] =
                "try{"
                "var p=mp.players&&mp.players.local;"
                "var v=p&&p.vehicle;"
                "if(v&&v.handle){"
                "try{v.repair();}catch(e){"
                "try{mp.game.invoke('0x115722B1B9C14C1C',v.handle);}catch(e2){}"
                "}"
                "}"
                "}catch(e){}";
            if (!EnqueuePlayerScript(kRepairJs))
                Queue("misc_repair");
        }
        if (g_PendingOpenTrunk) {
            g_PendingOpenTrunk = false;
            OpenAimedTrunkInventory();
        }
        if (g_PendingTpWaypoint) {
            g_PendingTpWaypoint = false;
            // Preferred path: the script thread, where the blip natives, the
            // ground probe and setCoords all actually run. Present is only the
            // fallback for a session where V8 never came up.
            if (PlayerV8Ready() && g_PlJsHooked)
            {
                ++g_TpSeq;
                g_TpQueryId = 0;
                g_TpQueryAt = 0;
                if (PushPlayerState())
                {
                    Menu::PushNotification(
                        "TP Waypoint",
                        "Resolving destination ground…",
                        true,
                        1500);
                }
                else
                {
                    --g_TpSeq;
                    Menu::PushNotification(
                        "TP Waypoint",
                        "Script bridge busy, try again",
                        false,
                        2500);
                }
            }
            else
            {
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
    g_PlJsHooked = false;
    g_PlJsPushed = false;
    g_PlJsLastState[0] = '\0';
    g_PlVehQueryId = 0;
    g_PlVehQueryAt = 0;
    g_AntiAfkNextPokeAt = 0;
    g_TpSeq = 0;
    g_TpReportedSeq = 0;
    g_TpQueryId = 0;
    g_TpQueryAt = 0;
    VehicleMovement::ClearLocalVehicleState();
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
    // Clear every toggle the script hook holds so nothing is left applied
    // (speed multiplier, infinite ammo, vehicle power) after unload.
    if (g_PlJsHooked)
    {
        g_Settings.speedhack = false;
        g_Settings.superJump = false;
        g_Settings.unlimitedStamina = false;
        g_Settings.infiniteAmmo = false;
        g_Settings.infiniteClip = false;
        g_Settings.noReload = false;
        g_Settings.antiAfk = false;
        auto& vm = VehicleMovement::GetSettings();
        vm.speedEnable = false;
        vm.gravityEnable = false;
        vm.seatbelt = false;
        vm.skipEnterAnim = false;
        vm.engineForceOn = false;
        g_PlJsPushed = false;
        PushPlayerState();
    }
    g_PlJsHooked = false;
    g_PlJsPushed = false;
    g_PendingTpWaypoint = false;
    g_PendingRepairVehicle = false;
    // Hand the weapon its own spread/recoil/reload values back before unload.
    RestoreWeaponInfo();
    VehicleMovement::ClearLocalVehicleState();
    VehicleMovement::Shutdown();
}

void Update() noexcept
{
    HandleKeyToggles();
    ApplyNoclip();
    ApplyFreecam();
    SyncMoveScript();
    // Publishes vehicle state and owns the script-thread player natives, so it
    // has to run before anything that asks whether the player is seated.
    SyncPlayerScript();
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

void RequestOpenTrunkInventory() noexcept
{
    g_PendingOpenTrunk = true;
}

bool RequestTeleportWaypoint() noexcept
{
    g_PendingTpWaypoint = true;
    return true;
}

bool RequestAttackBurst(int milliseconds) noexcept
{
    if (!PlayerV8Ready() || !g_PlJsHooked)
        return false;
    if (milliseconds < 10)
        milliseconds = 10;
    if (milliseconds > 200)
        milliseconds = 200;
    // Arms a window rather than firing once: the render hook then asserts the
    // control on every frame inside it, so a single missed frame cannot eat
    // the shot.
    char script[160]{};
    sprintf_s(
        script,
        "try{var g=typeof globalThis!=='undefined'?globalThis:this;"
        "g.__bfShoot=Date.now()+%d;}catch(e){}",
        milliseconds);
    return EnqueuePlayerScript(script);
}

void RenderPlayersOverlay() noexcept
{
    RenderClickWarpMarker();
    if (!g_Settings.showPlayers && !g_Settings.showAdmins &&
        !g_Settings.showAdminList)
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

    // Label and number on one line, the number brighter than the label. Right
    // aligning the count put it outside the auto-resized window and it was
    // simply not there.
    const auto countRow =
        [](const char* label, int value) noexcept {
            ImGui::TextColored(
                ImVec4(0.78f, 0.81f, 0.86f, 1.f),
                "%s:",
                label);
            ImGui::SameLine(0.f, 6.f);
            ImGui::TextColored(
                ImVec4(1.f, 1.f, 1.f, 1.f),
                "%d",
                value);
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
    if (g_Settings.showAdminList) {
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
    MF_WRITE("mf_show_admin_list", g_Settings.showAdminList ? 1 : 0);
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
    MF_READ_BOOL("mf_show_admin_list", g_Settings.showAdminList);
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
        MF_READ_BOOL("mf_veh_skip_anim", vehicle.skipEnterAnim);
        MF_READ_BOOL("mf_veh_autopilot", vehicle.autopilot);
        MF_READ_BOOL("mf_veh_engine", vehicle.engineForceOn);

        if (!(vehicle.speedMult >= 1.f))
            vehicle.speedMult = 1.f;
        if (vehicle.speedMult > 10.f)
            vehicle.speedMult = 10.f;
        if (!(vehicle.gravityMult >= 1.f))
            vehicle.gravityMult = 1.f;
        if (vehicle.gravityMult >
            VehicleMovement::Settings::kMaxGravity)
            vehicle.gravityMult =
                VehicleMovement::Settings::kMaxGravity;
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
