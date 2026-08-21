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
    int g_FlyVehicle = 0;
    bool g_FlyHeld = false;
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

    float ClampFlySpeed(float value) noexcept
    {
        if (!(value >= 0.2f))
            return 0.2f;
        if (value > 20.f)
            return 20.f;
        return value;
    }

    bool ResolveDriverVehicle(int& outPed, int& outVehicle) noexcept
    {
        outPed = 0;
        outVehicle = 0;
        if (!NativeInvoke::Init())
            return false;
        const int ped = NativeInvoke::PlayerPedId();
        if (!ped || !NativeInvoke::DoesEntityExist(ped))
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

    void RestoreFly(int vehicle) noexcept
    {
        if (vehicle != 0 && NativeInvoke::DoesEntityExist(vehicle))
        {
            NativeInvoke::SetEntityHasGravity(vehicle, true);
            NativeInvoke::SetVehicleGravity(vehicle, true);
            NativeInvoke::FreezeEntityPosition(vehicle, false);
        }
        g_FlyHeld = false;
        g_FlyVehicle = 0;
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

    void ApplyFly(int vehicle) noexcept
    {
        if (!g_Settings.fly)
        {
            if (g_FlyHeld)
                RestoreFly(g_FlyVehicle);
            return;
        }
        if (!vehicle)
            return;

        if (g_FlyHeld && g_FlyVehicle != vehicle)
            RestoreFly(g_FlyVehicle);

        NativeInvoke::SetEntityHasGravity(vehicle, false);
        NativeInvoke::SetVehicleGravity(vehicle, false);
        g_FlyVehicle = vehicle;
        g_FlyHeld = true;

        float vx = 0.f;
        float vy = 0.f;
        float vz = 0.f;
        if (!Menu::IsVisible())
        {
            const float forwardInput =
                ((GetAsyncKeyState('W') & 0x8000) ? 1.f : 0.f) -
                ((GetAsyncKeyState('S') & 0x8000) ? 1.f : 0.f);
            const float rightInput =
                ((GetAsyncKeyState('D') & 0x8000) ? 1.f : 0.f) -
                ((GetAsyncKeyState('A') & 0x8000) ? 1.f : 0.f);
            const float verticalInput =
                ((GetAsyncKeyState(VK_SPACE) & 0x8000) ? 1.f : 0.f) -
                ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? 1.f : 0.f);
            float pitch = 0.f;
            float roll = 0.f;
            float yaw = 0.f;
            if ((forwardInput != 0.f ||
                 rightInput != 0.f ||
                 verticalInput != 0.f) &&
                NativeInvoke::GetGameplayCamRot(pitch, roll, yaw))
            {
                (void)roll;
                constexpr float kRadians =
                    3.14159265358979323846f / 180.f;
                const float pitchRadians = pitch * kRadians;
                const float yawRadians = yaw * kRadians;
                const float fx =
                    -std::sin(yawRadians) * std::cos(pitchRadians);
                const float fy =
                    std::cos(yawRadians) * std::cos(pitchRadians);
                const float fz = std::sin(pitchRadians);
                const float rx = std::cos(yawRadians);
                const float ry = std::sin(yawRadians);
                float dx = fx * forwardInput + rx * rightInput;
                float dy = fy * forwardInput + ry * rightInput;
                float dz = fz * forwardInput + verticalInput;
                const float length =
                    std::sqrt(dx * dx + dy * dy + dz * dz);
                if (length > 0.0001f)
                {
                    const float speed =
                        ClampFlySpeed(g_Settings.flySpeed);
                    g_Settings.flySpeed = speed;
                    const float boost =
                        (GetAsyncKeyState(VK_SHIFT) & 0x8000)
                            ? 3.f
                            : 1.f;
                    const float mag = speed * 20.f * boost;
                    vx = (dx / length) * mag;
                    vy = (dy / length) * mag;
                    vz = (dz / length) * mag;
                }
            }
        }
        NativeInvoke::SetEntityVelocity(vehicle, vx, vy, vz);
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
    g_Settings.gravityMult = ClampMultiplier(g_Settings.gravityMult);
    g_Settings.flySpeed = ClampFlySpeed(g_Settings.flySpeed);

    const bool any =
        g_Settings.speedEnable ||
        g_Settings.gravityEnable ||
        g_Settings.seatbelt ||
        g_Settings.fly ||
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
        if (g_FlyHeld)
            RestoreFly(g_FlyVehicle);
        CancelAutopilot();
        g_Status = Status::NativeUnavailable;
        return;
    }

    const int localPed = NativeInvoke::PlayerPedId();
    if (localPed && NativeInvoke::DoesEntityExist(localPed))
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
        if (g_FlyHeld)
            RestoreFly(g_FlyVehicle);
        if (g_AutopilotArmed)
            CancelAutopilot();
        g_PendingStop = false;
        g_PendingFlip = false;
        g_PendingCrash = false;
        g_PendingEngine = false;
        g_Status = any ? Status::NoVehicle : Status::Off;
        if (localPed && NativeInvoke::GetVehiclePedIsIn(localPed, false))
            g_Status = Status::Passenger;
        if (!any)
            g_Status = Status::Off;
        return;
    }

    ConsumeOneShots(ped, vehicle);
    ApplyFly(vehicle);
    ApplyAutopilot(ped, vehicle);

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

    if (g_Settings.gravityEnable && !g_Settings.fly)
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

    g_Status = Status::Active;
}

void Shutdown() noexcept
{
    RestoreSpeed();
    if (g_FlyHeld)
        RestoreFly(g_FlyVehicle);
    CancelAutopilot();
    g_Status = Status::Off;
}
} // namespace VehicleMovement
