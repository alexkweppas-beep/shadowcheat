#include "VehicleMovement.h"
#include "NativeInvoke.hpp"
#include "RageV8Runtime.h"
#include "../Menu/Menu.h"
#include "../SDK/RageBuildFlavor.h"

#include <Windows.h>
#include <cmath>

#if defined(RAGEMP_DIAGNOSTICS)
#include <cstdio>

#include "../Main/Hooks.h"
#endif

namespace VehicleMovement
{
namespace
{
    Settings g_Settings{};
    Status g_Status = Status::Off;
    int g_SpeedVehicle = 0;
    bool g_SpeedApplied = false;
    int g_AutopilotVehicle = 0;
    bool g_AutopilotArmed = false;
    bool g_PendingStop = false;
    bool g_PendingFlip = false;
    bool g_PendingCrash = false;

    float ClampMultiplier(float value) noexcept
    {
        if (!(value >= 1.f))
            return 1.f;
        if (value > 10.f)
            return 10.f;
        return value;
    }

    // Downforce has its own ceiling. It is expressed in g now that it is
    // applied as a velocity delta, so large values are meaningful and stable,
    // where the speed multiplier above genuinely stops being useful past ten.
    float ClampGravity(float value) noexcept
    {
        if (!(value >= 1.f))
            return 1.f;
        if (value > Settings::kMaxGravity)
            return Settings::kMaxGravity;
        return value;
    }

    LocalVehicleState g_LocalVehicle{};

    bool ResolveDriverVehicle(int& outPed, int& outVehicle) noexcept
    {
        outPed = 0;
        outVehicle = 0;
        // The script thread's answer is asked for first and on its own terms.
        // It used to be reached only after PLAYER_PED_ID returned something,
        // and that native returns zero here: the table it lives in is
        // encrypted on this build, so every C++ native is a no-op. The result
        // was that being plainly seated and driving still resolved to "not
        // driving", and every action on the vehicle page was dropped in
        // silence.
        if (g_LocalVehicle.valid)
        {
            if (!g_LocalVehicle.isDriver || !g_LocalVehicle.handle)
                return false;
            outPed = NativeInvoke::PlayerPedId();
            outVehicle = g_LocalVehicle.handle;
            return true;
        }
        if (!NativeInvoke::Init())
            return false;
        const int ped = NativeInvoke::PlayerPedId();
        if (!ped)
            return false;
        if (!NativeInvoke::DoesEntityExist(ped))
            return false;
        const int vehicle = NativeInvoke::GetVehiclePedIsIn(ped, false);
        if (!vehicle || !NativeInvoke::DoesEntityExist(vehicle))
            return false;
        if (NativeInvoke::GetPedInVehicleSeat(vehicle, -1) != ped)
            return false;
        outPed = ped;
        outVehicle = vehicle;
        return true;
    }

    void RestoreSpeed() noexcept
    {
        if (g_SpeedApplied &&
            g_SpeedVehicle != 0 &&
            NativeInvoke::DoesEntityExist(g_SpeedVehicle))
        {
            NativeInvoke::SetVehicleEnginePowerMultiplier(
                g_SpeedVehicle,
                1.f);
            NativeInvoke::SetVehicleEngineTorqueMultiplier(
                g_SpeedVehicle,
                1.f);
        }
        g_SpeedApplied = false;
        g_SpeedVehicle = 0;
    }

    void CancelAutopilot() noexcept
    {
        if (g_AutopilotArmed)
        {
            const int ped = NativeInvoke::PlayerPedId();
            if (ped && NativeInvoke::DoesEntityExist(ped))
                NativeInvoke::ClearPedTasks(ped);
        }
        g_AutopilotArmed = false;
        g_AutopilotVehicle = 0;
    }

    void ApplySeatbelt(int ped) noexcept
    {
        if (!g_Settings.seatbelt || !ped)
            return;
        // 1 = never knocked off. Flag 32 = can fly through windscreen.
        NativeInvoke::SetPedCanBeKnockedOffVehicle(ped, 1);
        NativeInvoke::SetPedConfigFlag(ped, 32, false);
    }

    void ApplySkipAnim(int ped) noexcept
    {
        if (!g_Settings.skipEnterAnim || !ped)
            return;
        if (NativeInvoke::IsPedGettingIntoAVehicle(ped))
        {
            const int vehicle = NativeInvoke::GetVehiclePedIsEntering(ped);
            if (vehicle && NativeInvoke::DoesEntityExist(vehicle))
                NativeInvoke::SetPedIntoVehicle(ped, vehicle, -1);
        }
        if (NativeInvoke::IsPedJumpingOutOfVehicle(ped))
        {
            const int vehicle = NativeInvoke::GetVehiclePedIsIn(ped, true);
            if (vehicle && NativeInvoke::DoesEntityExist(vehicle))
                NativeInvoke::TaskLeaveVehicle(ped, vehicle, 16);
        }
    }

    void ApplyAutopilot(int ped, int vehicle) noexcept
    {
        if (!g_Settings.autopilot)
        {
            if (g_AutopilotArmed)
                CancelAutopilot();
            return;
        }
        if (!ped || !vehicle)
            return;

        if (g_AutopilotArmed && g_AutopilotVehicle == vehicle)
            return;
        if (g_AutopilotArmed)
            CancelAutopilot();

        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
        if (!NativeInvoke::FindWaypointCoords(x, y, z))
            return;
        NativeInvoke::TaskVehicleDriveToCoordLongrange(
            ped,
            vehicle,
            x,
            y,
            z,
            28.f,
            787004,
            12.f);
        g_AutopilotVehicle = vehicle;
        g_AutopilotArmed = true;
    }

    // These have to run on the script thread. The C++ native path cannot do
    // them at all: the game's native table is encrypted on this build, so a
    // handler lookup returns nothing and the call is a silent no-op.
    //
    // The body is handed the driver's vehicle as `v` and is only reached when
    // there is one.
    // The outcome is recorded where the vehicle poll can read it back. Without
    // that, a wrapper this build happens not to expose looks exactly like a
    // wrapper that ran and did nothing, and telling those two apart by
    // guesswork has already cost several rounds.
    bool RunOnVehicle(const char* label, const char* body) noexcept
    {
        if (!label || !body)
            return false;
        char script[768]{};
        _snprintf(
            script,
            sizeof(script) - 1,
            // One slot per action. Sharing a single one meant the next press
            // erased the answer to the previous, so pressing two buttons in a
            // row reported only the second.
            "try{var g=typeof globalThis!=='undefined'?globalThis:this;"
            "g.__bfPl=g.__bfPl||{};g.__bfPl.va=g.__bfPl.va||{};"
            "var how='none';"
            "var p=mp.players&&mp.players.local;var v=p&&p.vehicle;"
            "if(!v||!v.handle){g.__bfPl.va['%s']='noveh';}"
            "else{try{%s g.__bfPl.va['%s']=how;}"
            "catch(e){g.__bfPl.va['%s']='err '+"
            "String((e&&e.message)||e).slice(0,40).replace(/\\s+/g,'_');}}"
            "}catch(e){}",
            label,
            body,
            label,
            label);
        return RageV8Runtime::EnqueueFireAndForget(script);
    }

    void ConsumeOneShots(int ped, int vehicle) noexcept
    {
        if (!vehicle)
        {
            g_PendingStop = false;
            g_PendingFlip = false;
            g_PendingCrash = false;
            return;
        }
        if (g_PendingStop)
        {
            g_PendingStop = false;
            RunOnVehicle(
                "stop",
                "v.setVelocity(0,0,0);how='vel';"
                "if(typeof v.setBrake==='function'){v.setBrake(1);"
                "how='vel+brake';}");
        }
        if (g_PendingFlip)
        {
            g_PendingFlip = false;
            // Upright, keeping the way it faces. Roll and pitch are what a
            // flipped car has, and heading is the part worth preserving.
            RunOnVehicle(
                "flip",
                "var hd=0;"
                "try{hd=Number(v.getHeading());}catch(e){}"
                "if(!isFinite(hd)){try{hd=Number(v.heading);}catch(e){hd=0;}}"
                "if(!isFinite(hd))hd=0;"
                "if(typeof v.setRotation==='function'){"
                "v.setRotation(0,0,hd,2,true);how='wrapper';}"
                "else if(mp.game.entity&&"
                "typeof mp.game.entity.setEntityRotation==='function'){"
                "mp.game.entity.setEntityRotation("
                "v.handle,0,0,hd,2,true);how='ns';}");
        }
        if (g_PendingCrash)
        {
            g_PendingCrash = false;
            // The vehicle may simply not be ours to blow up: on a roleplay
            // server the entity belongs to the server and the explosion has to
            // be its decision. Which of the surfaces exists is measured rather
            // than assumed.
            RunOnVehicle(
                "crash",
                // Both arguments are required: called bare the wrapper refuses
                // with "isAudible is not a boolean".
                "if(typeof v.explode==='function'){v.explode(true,false);"
                "how='explode';}"
                "else if(mp.game.vehicle&&typeof "
                "mp.game.vehicle.networkExplodeVehicle==='function'){"
                "mp.game.vehicle.networkExplodeVehicle("
                "v.handle,true,false,0);how='network';}"
                "else if(mp.game.fire&&typeof "
                "mp.game.fire.addExplosion==='function'){"
                "var q=v.position;"
                "mp.game.fire.addExplosion(q.x,q.y,q.z,7,1,true,false,1);"
                "how='blast';}");
        }
        // Engine control lived here and was removed. The engine is the
        // server's: setting it from the client resynced the vehicle and threw
        // the occupant out, and asking over the server's own event does not
        // bypass the ownership check, it invokes it. Neither is a feature.
        (void)ped;
        (void)vehicle;
    }
} // namespace

Settings& GetSettings() noexcept
{
    return g_Settings;
}

Status GetStatus() noexcept
{
    return g_Status;
}

void RequestStop() noexcept
{
    g_PendingStop = true;
}

void RequestFlip() noexcept
{
    g_PendingFlip = true;
}

void RequestCrash() noexcept
{
    g_PendingCrash = true;
}

void Update() noexcept
{
    g_Settings.unlockSpeedLimit = false;
    g_Settings.speedMult = ClampMultiplier(g_Settings.speedMult);
    g_Settings.gravityMult = ClampGravity(g_Settings.gravityMult);

    const bool any =
        g_Settings.speedEnable ||
        g_Settings.gravityEnable ||
        g_Settings.seatbelt ||
        g_Settings.skipEnterAnim ||
        g_Settings.autopilot ||
        g_PendingStop ||
        g_PendingFlip ||
        g_PendingCrash;

    if (!NativeInvoke::Init())
    {
        RestoreSpeed();
        CancelAutopilot();
        g_Status = Status::NativeUnavailable;
        return;
    }

    // While the script thread owns the per-frame natives, applying them from
    // Present as well would only fight that path.
    const bool scriptOwnsFrame = g_LocalVehicle.valid;

    // Hand back anything the Present path had applied before V8 came up, so
    // the two never both think they own the multipliers.
    if (scriptOwnsFrame)
    {
        if (g_SpeedApplied)
            RestoreSpeed();
    }

    const int localPed = NativeInvoke::PlayerPedId();
    if (!scriptOwnsFrame &&
        localPed &&
        NativeInvoke::DoesEntityExist(localPed))
    {
        ApplySeatbelt(localPed);
        ApplySkipAnim(localPed);
    }

    int ped = 0;
    int vehicle = 0;
    const bool driving = ResolveDriverVehicle(ped, vehicle);

#if defined(RAGEMP_DIAGNOSTICS)
    // Every one of these actions is dropped on the floor unless the player is
    // driving, and it is dropped without a word, which is indistinguishable
    // from the action being broken. The ids are here as well because the
    // native table resolving is not the same as calls through it working.
    {
        static ULONGLONG lastReport = 0;
        const ULONGLONG nowMs = GetTickCount64();
        const bool pending =
            g_PendingStop || g_PendingFlip ||
            g_PendingCrash;
        if (pending || nowMs - lastReport >= 2000)
        {
            lastReport = nowMs;
            char line[224]{};
            _snprintf(
                line,
                sizeof(line) - 1,
                "[VehAct] driving=%d ped=%d veh=%d scriptOwns=%d "
                "pubValid=%d pubIn=%d pubDriver=%d pubHandle=%d "
                "pending(stop=%d flip=%d crash=%d engine=%d)",
                driving ? 1 : 0,
                ped,
                vehicle,
                scriptOwnsFrame ? 1 : 0,
                g_LocalVehicle.valid ? 1 : 0,
                g_LocalVehicle.inVehicle ? 1 : 0,
                g_LocalVehicle.isDriver ? 1 : 0,
                g_LocalVehicle.handle,
                g_PendingStop ? 1 : 0,
                g_PendingFlip ? 1 : 0,
                g_PendingCrash ? 1 : 0,
                0);
            hooks::HLog(line);
        }
    }
#endif
    if (!driving)
    {
        RestoreSpeed();
        if (g_AutopilotArmed)
            CancelAutopilot();
        g_PendingStop = false;
        g_PendingFlip = false;
        g_PendingCrash = false;
        g_Status = any ? Status::NoVehicle : Status::Off;
        const bool seated = g_LocalVehicle.valid
            ? g_LocalVehicle.inVehicle
            : (localPed != 0 &&
               NativeInvoke::GetVehiclePedIsIn(localPed, false) != 0);
        if (seated)
            g_Status = Status::Passenger;
        if (!any)
            g_Status = Status::Off;
        return;
    }

    ConsumeOneShots(ped, vehicle);

    if (!scriptOwnsFrame)
    {
        if (g_Settings.speedEnable)
        {
            if (g_SpeedApplied && g_SpeedVehicle != vehicle)
                RestoreSpeed();
            NativeInvoke::SetVehicleEnginePowerMultiplier(
                vehicle,
                g_Settings.speedMult);
            NativeInvoke::SetVehicleEngineTorqueMultiplier(
                vehicle,
                g_Settings.speedMult);
            g_SpeedVehicle = vehicle;
            g_SpeedApplied = true;
        }
        else
        {
            RestoreSpeed();
        }

        if (g_Settings.gravityEnable)
        {
            const float downforce =
                (g_Settings.gravityMult - 1.f) * 3.f;
            if (downforce > 0.f)
            {
                NativeInvoke::ApplyForceToEntity(
                    vehicle,
                    1,
                    0.f,
                    0.f,
                    -downforce,
                    0.f,
                    0.f,
                    0.f,
                    0,
                    false,
                    false,
                    true,
                    false,
                    true);
            }
        }
    }

    // Gated like the rest: the script hook owns the drive task when V8 is up,
    // and this path cannot work anyway while native resolution is unavailable.
    if (!scriptOwnsFrame)
        ApplyAutopilot(ped, vehicle);

    g_Status = Status::Active;
}

void SetLocalVehicleState(
    bool inVehicle,
    bool isDriver,
    bool isEntering,
    int handle) noexcept
{
    g_LocalVehicle.valid = true;
    g_LocalVehicle.inVehicle = inVehicle;
    g_LocalVehicle.isDriver = isDriver;
    g_LocalVehicle.isEntering = isEntering;
    g_LocalVehicle.handle = handle;
}

void ClearLocalVehicleState() noexcept
{
    g_LocalVehicle = {};
}

LocalVehicleState GetLocalVehicleState() noexcept
{
    return g_LocalVehicle;
}

void Shutdown() noexcept
{
    g_LocalVehicle = {};
    RestoreSpeed();
    CancelAutopilot();
    g_Status = Status::Off;
}
} // namespace VehicleMovement
