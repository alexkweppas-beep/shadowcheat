#pragma once

namespace VehicleMovement
{
    enum class Status : int
    {
        Off = 0,
        NoVehicle,
        Passenger,
        NativeUnavailable,
        Active
    };

    struct Settings final
    {
        bool speedEnable{};
        float speedMult{1.5f};
        bool gravityEnable{};
        float gravityMult{1.5f};
        // Deprecated compatibility field. The max-speed override had no
        // reliable restore value and is intentionally forced off at runtime.
        bool unlockSpeedLimit{};

        bool seatbelt{};
        bool fly{};
        float flySpeed{1.5f};
        bool skipEnterAnim{};
        bool autopilot{};
        bool engineForceOn{};
    };

    Settings& GetSettings() noexcept;
    Status GetStatus() noexcept;
    void Update() noexcept;
    void Shutdown() noexcept;

    void RequestStop() noexcept;
    void RequestFlip() noexcept;
    void RequestCrash() noexcept;
    void RequestEngineOn() noexcept;
}
