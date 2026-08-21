#pragma once

#include <cstdint>

namespace SilentAim
{
    struct Status final
    {
        bool installed{};
        bool available{};
        std::uintptr_t resolvedRva{};
        const char* message{"Uninitialized"};
    };

    bool Initialize() noexcept;
    void Shutdown() noexcept;
    Status GetStatus() noexcept;
}
