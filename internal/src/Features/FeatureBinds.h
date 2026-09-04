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
        // A press made with the menu open is shown here and nowhere else: the
        // setting is left exactly as it was, so the key can be checked against
        // the row it belongs to without the feature ever running. Dropped as
        // the menu closes.
        bool previewActive{};
        bool previewShown{};
        char label[96]{};
    };

    Binding* Register(bool* value, const char* label) noexcept;

    // Buttons have no state to flip, so they bind to a call. Identity comes from
    // the caller's stable id rather than the label, which moves with language.
    Binding* RegisterAction(
        const char* id,
        const char* label,
        void (*action)()) noexcept;
    // previewOnly: the menu is up, so a press is a demonstration. The switch
    // moves and nothing else happens, and the setting is never written.
    void Tick(bool suppressActivation, bool previewOnly = false) noexcept;

    // What a row should draw instead of its setting, while a bind for it is
    // being pressed with the menu open. False means "draw the setting".
    bool PreviewState(const bool* value, bool& shown) noexcept;
    // Clicked by hand: the demonstration is over and the row goes back to
    // showing what it is actually set to.
    void ClearPreview(const bool* value) noexcept;
    void SetCaptureActive(bool active) noexcept;
    bool IsCaptureActive() noexcept;
    void ResetEdge(Binding* binding) noexcept;

    bool Export(
        char* output,
        std::size_t capacity,
        std::size_t& used) noexcept;
    void Import(const char* content) noexcept;
}
