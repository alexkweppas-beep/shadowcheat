#pragma once
#include <Windows.h>
#include <cstdint>

namespace Memory
{
    // Find a pattern in a specific module (default to main module)
    uint8_t* FindPattern(const char* moduleName, const char* pattern);

    // Resolves a RIP-relative address (e.g., 48 8B 05 ? ? ? ?)
    template <typename T>
    T GetAbsoluteAddress(uint8_t* instruction, int offset = 3, int size = 7)
    {
        if (!instruction) return nullptr;
        return (T)(instruction + size + *(int32_t*)(instruction + offset));
    }
}
