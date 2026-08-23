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
        // Set instead of value for buttons. Hold/Toggle has no meaning for a
        // one-shot, so an action fires on the key down edge and nothing else.
        void (*action)(){};
        std::uint64_t stableId{};
        int key{};
        int mode{Toggle};
        bool previousDown{};
        char label[96]{};
    };

    Binding* Register(bool* value, const char* label) noexcept;

    // Buttons have no state to flip, so they bind to a call. Identity comes from
    // the caller's stable id rather than the label, which moves with language.
    Binding* RegisterAction(
        const char* id,
        const char* label,
        void (*action)()) noexcept;
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
