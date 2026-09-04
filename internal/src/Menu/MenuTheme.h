#pragma once

#include "../imgui/imgui.h"

// Single source of truth for menu colour and metric tokens.
//
// Default chrome matches the locked reference: near-black canvas #0A0A0A,
// cobalt accent #4D5DFA, and 8/10/14/16 radii. Appearance can still retint
// the accent; c_colors stays as a compatibility projection of these tokens.
namespace MenuTheme
{
    struct AccentRamp final
    {
        ImVec4 tint;     // lightest: hover glyphs, thin highlights
        ImVec4 brand;    // logo and accent text, the reference tone
        ImVec4 active;   // active icons and outlines
        ImVec4 fill;     // selected tab, primary button, enabled toggle
        ImVec4 pressed;  // pressed state and light-theme accent
    };

    struct Palette final
    {
        AccentRamp accent{};

        ImVec4 canvas{};   // window backdrop
        ImVec4 base{};     // panels
        ImVec4 panel{};    // panel headers, popups
        ImVec4 raised{};   // controls
        ImVec4 hover{};    // control hover
        ImVec4 pressed{};  // control pressed

        ImVec4 borderSubtle{};
        ImVec4 borderStrong{};
        ImVec4 borderFocus{};

        ImVec4 textPrimary{};
        ImVec4 textMuted{};
        ImVec4 textFaint{};
        // Ink for labels sitting on an accent fill. Follows the accent's own
        // luminance, so a lime or teal pill gets dark text instead of white.
        ImVec4 onAccent{};

        ImVec4 success{};
        ImVec4 warning{};
        ImVec4 error{};
        ImVec4 unknown{};
    };

    // 4 px rhythm scaled from one canonical baseline.
    struct Metrics final
    {
        float space1 = 4.f;
        float space2 = 8.f;
        float space3 = 12.f;
        float space4 = 16.f;
        float space5 = 20.f;
        float space6 = 24.f;

        float rowHeight = 36.f;
        float controlHeight = 32.f;
        float tabHeight = 38.f;
        float headerHeight = 52.f;
        float instrumentHeight = 26.f;
        float railWidth = 220.f;
        float railExpandedWidth = 220.f;

        float radiusSmall = 8.f;
        float radiusControl = 10.f;
        float radiusPanel = 14.f;
        float radiusWindow = 16.f;
        float stroke = 1.f;

        float iconRail = 24.f;
        float iconRow = 16.f;
    };

    // Accent-only presets. Deliberately off the reference cobalt so the menu
    // does not read as a copy of it; nothing else in the palette changes.
    enum class Preset : int
    {
        Iris = 0,
        Magenta,
        Teal,
        Ember,
        Lime,
        Count
    };

    const Palette& Colors() noexcept;
    const Metrics& Sizes() noexcept;

    // Canonical brand tone of the logo. Never follows the user accent.
    ImVec4 BrandColor() noexcept;

    Preset CurrentPreset() noexcept;
    void SetPreset(Preset preset) noexcept;
    const char* PresetName(Preset preset) noexcept;
    ImVec4 PresetColor(Preset preset) noexcept;

    // Back to the shipped look: cobalt accent, near-black canvas, dark theme.
    void ResetToDefault() noexcept;

    // Accent chosen by the user; the whole ramp and the neutrals follow it.
    void SetAccentBase(const ImVec4& color) noexcept;
    ImVec4 AccentBase() noexcept;

    // Darkest surface. Panels, controls and hover states step up from it, so
    // one pick retints the whole dark palette.
    void SetCanvasBase(const ImVec4& color) noexcept;
    ImVec4 CanvasBase() noexcept;

    bool IsLight() noexcept;

    // Rebuilds the palette and mirrors it into c_colors / c_settings.
    void Apply(bool light) noexcept;

    // Scaled metric helper: MenuTheme::Scaled(Sizes().rowHeight).
    float Scaled(float value) noexcept;
}
