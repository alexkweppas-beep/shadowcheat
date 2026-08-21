#pragma once

#include <cstddef>
#include <cstdint>

namespace FeatureBinds
{
    enum Mode : int
    {
        Hold = 0,
        Toggle = 1
    };

    struct Binding final
    {
        bool* value{};
        std::uint64_t stableId{};
        int key{};
        int mode{Toggle};
        bool previousDown{};
        char label[96]{};
    };

    Binding* Register(bool* value, const char* label) noexcept;
    void Tick(bool suppressActivation) noexcept;
    void SetCaptureActive(bool active) noexcept;
    bool IsCaptureActive() noexcept;
    void ResetEdge(Binding* binding) noexcept;

    bool Export(
        char* output,
        std::size_t capacity,
        std::size_t& used) noexcept;
    void Import(const char* content) noexcept;
}
