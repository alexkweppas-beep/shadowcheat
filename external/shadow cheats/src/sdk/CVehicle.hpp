#pragma once
#include <cstdint>
#include "math_utils.hpp"

class CVehicle {
public:
    uintptr_t ptr;
    Vector3 position;
    float health;
    float engine_health;
    int lock_status;
    bool engine_running;
    float trunk_status;

    CVehicle();
    CVehicle(uintptr_t pointer);

    bool Update();
    bool IsValid() const;
};
