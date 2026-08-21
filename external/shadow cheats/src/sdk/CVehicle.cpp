#include "CVehicle.hpp"
#include "offsets.hpp"
#include "../memory/memory_access.hpp"

CVehicle::CVehicle() : ptr(0), health(0), engine_health(0), lock_status(0), engine_running(false), trunk_status(0.f) {}

CVehicle::CVehicle(uintptr_t pointer) : ptr(pointer), health(0), engine_health(0), lock_status(0), engine_running(false), trunk_status(0.f) {
    Update();
}

bool CVehicle::Update() {
    if (!ptr) return false;

    // Use entityBase + 0x90 for position since pVehicle + 0x90 + 0x30 is standard but let's stick to what worked.
    position = read<Vector3>(ptr + offsets::vehicle::Position);
    health = read<float>(ptr + offsets::vehicle::Health);
    
    // Trunk
    trunk_status = read<float>(ptr + 0x5F4);
    
    // Engine RPM (100% reliable)
    float rpm = read<float>(ptr + 0x838);
    engine_running = (rpm > 0.01f);
    
    // Lock Status
    lock_status = read<int>(ptr + 0x1230);

    return IsValid();
}

bool CVehicle::IsValid() const {
    if (ptr == 0) return false;
    if (position.x == 0 && position.y == 0 && position.z == 0) return false;
    return true;
}
