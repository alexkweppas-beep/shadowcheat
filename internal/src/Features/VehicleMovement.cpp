#include "VehicleMovement.h"
#include "NativeInvoke.hpp"
#include "../Menu/Menu.h"

#include <Windows.h>
#include <cmath>

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
    bool g_PendingEngine = false;

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
        if (!NativeInvoke::Init())
            return false;
        const int ped = NativeInvoke::PlayerPedId();
        if (!ped)
            return false;
        // Script-thread answer first. Only fall back to the Present natives
        // when V8 has not published a state yet.
        if (g_LocalVehicle.valid)
        {
            if (!g_LocalVehicle.isDriver || !g_LocalVehicle.handle)
                return false;
            outPed = ped;
            outVehicle = g_LocalVehicle.handle;
            return true;
        }
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

    void ConsumeOneShots(int ped, int vehicle) noexcept
    {
        if (!vehicle)
        {
            g_PendingStop = false;
            g_PendingFlip = false;
            g_PendingCrash = false;
            g_PendingEngine = false;
            return;
        }
        if (g_PendingStop)
        {
            NativeInvoke::SetEntityVelocity(vehicle, 0.f, 0.f, 0.f);
            g_PendingStop = false;
        }
        if (g_PendingFlip)
        {
            NativeInvoke::SetEntityRotation(
                vehicle,
                0.f,
                0.f,
                NativeInvoke::GetEntityHeading(vehicle),
                2,
                true);
            g_PendingFlip = false;
        }
        if (g_PendingCrash)
        {
            NativeInvoke::NetworkExplodeVehicle(vehicle, true, false, 0);
            g_PendingCrash = false;
        }
        if (g_PendingEngine)
        {
            NativeInvoke::SetVehicleEngineOn(vehicle, true, true, false);
            g_PendingEngine = false;
        }
        if (g_Settings.engineForceOn)
            NativeInvoke::SetVehicleEngineOn(vehicle, true, false, false);
        (void)ped;
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

void RequestEngineOn() noexcept
{
    g_PendingEngine = true;
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
        g_Settings.engineForceOn ||
        g_PendingStop ||
        g_PendingFlip ||
        g_PendingCrash ||
        g_PendingEngine;

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
    if (!driving)
    {
        RestoreSpeed();
        if (g_AutopilotArmed)
            CancelAutopilot();
        g_PendingStop = false;
        g_PendingFlip = false;
        g_PendingCrash = false;
        g_PendingEngine = false;
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
