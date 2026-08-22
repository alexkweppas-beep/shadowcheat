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
        // Shared by the two menu pages and the clamp, so the ceiling is stated
        // once rather than repeated as a literal in four places.
        static constexpr float kMaxGravity = 30.f;
        // Deprecated compatibility field. The max-speed override had no
        // reliable restore value and is intentionally forced off at runtime.
        bool unlockSpeedLimit{};

        bool seatbelt{};
        bool skipEnterAnim{};
        bool autopilot{};
        bool engineForceOn{};
    };

    // Resolved on the RAGE script thread and published here. Present cannot
    // answer "am I seated": GET_VEHICLE_PED_IS_IN has to hand out a script
    // handle for the vehicle, and without a script thread it returns 0 even
    // while the ped is in the driver seat.
    struct LocalVehicleState final
    {
        bool valid{};
        bool inVehicle{};
        bool isDriver{};
        bool isEntering{};
        int handle{};
    };

    Settings& GetSettings() noexcept;
    Status GetStatus() noexcept;
    void Update() noexcept;
    void Shutdown() noexcept;

    void SetLocalVehicleState(
        bool inVehicle,
        bool isDriver,
        bool isEntering,
        int handle) noexcept;
    void ClearLocalVehicleState() noexcept;
    LocalVehicleState GetLocalVehicleState() noexcept;

    void RequestStop() noexcept;
    void RequestFlip() noexcept;
    void RequestCrash() noexcept;
    void RequestEngineOn() noexcept;
}
