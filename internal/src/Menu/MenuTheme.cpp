#define IMGUI_DEFINE_MATH_OPERATORS

#include "MenuTheme.h"

#include <cmath>

#include "../imgui/imgui_internal.h"
#include "Menu.h"
#include "MenuState.h"
#include "Leyawin/leyawin_globals.h"
#include "Leyawin/leyawin_init.h"

namespace MenuTheme
{
    namespace
    {
        struct Hsl final
        {
            float h;  // degrees
            float s;
            float l;
        };

        Palette g_Palette{};
        Metrics g_Metrics{};
        Preset g_Preset = Preset::Iris;
        bool g_Light = false;
        // Shadow accent #8685EF (Iris preset)
        constexpr ImVec4 kIris{
            134.f / 255.f, 133.f / 255.f, 239.f / 255.f, 1.f};
        // Default accent #A8E63A (Lime)
        constexpr ImVec4 kLime{
            168.f / 255.f, 230.f / 255.f, 58.f / 255.f, 1.f};
        ImVec4 g_AccentBase = kIris;
        // Reference canvas #040404.
        constexpr ImVec4 kCanvas{
            4.f / 255.f, 4.f / 255.f, 4.f / 255.f, 1.f};
        ImVec4 g_CanvasBase = kCanvas;

        // Neutral surfaces are the canvas lifted by fixed steps, which keeps
        // the depth order intact whatever background the user picks.
        ImVec4 Lift(const ImVec4& base, float step, float alpha) noexcept
        {
            return ImVec4(
                ImSaturate(base.x + step),
                ImSaturate(base.y + step),
                ImSaturate(base.z + step),
                alpha);
        }

        Hsl ToHsl(const ImVec4& color) noexcept
        {
            const float r = ImSaturate(color.x);
            const float g = ImSaturate(color.y);
            const float b = ImSaturate(color.z);
            const float maximum = ImMax(r, ImMax(g, b));
            const float minimum = ImMin(r, ImMin(g, b));
            const float delta = maximum - minimum;

            Hsl out{0.f, 0.f, (maximum + minimum) * 0.5f};
            if (delta <= 0.0001f)
                return out;

            out.s = out.l > 0.5f
                ? delta / ImMax(0.0001f, 2.f - maximum - minimum)
                : delta / ImMax(0.0001f, maximum + minimum);

            if (maximum == r)
                out.h = 60.f * fmodf((g - b) / delta + 6.f, 6.f);
            else if (maximum == g)
                out.h = 60.f * ((b - r) / delta + 2.f);
            else
                out.h = 60.f * ((r - g) / delta + 4.f);
            return out;
        }

        ImVec4 FromHsl(float hue, float sat, float lum, float alpha) noexcept
        {
            sat = ImClamp(sat, 0.f, 1.f);
            lum = ImClamp(lum, 0.f, 1.f);
            const float c = (1.f - fabsf(2.f * lum - 1.f)) * sat;
            const float hp = fmodf(fmodf(hue, 360.f) + 360.f, 360.f) / 60.f;
            const float x = c * (1.f - fabsf(fmodf(hp, 2.f) - 1.f));
            const float m = lum - c * 0.5f;

            float r = 0.f;
            float g = 0.f;
            float b = 0.f;
            if (hp < 1.f)      { r = c; g = x; }
            else if (hp < 2.f) { r = x; g = c; }
            else if (hp < 3.f) { g = c; b = x; }
            else if (hp < 4.f) { g = x; b = c; }
            else if (hp < 5.f) { r = x; b = c; }
            else               { r = c; b = x; }
            return ImVec4(r + m, g + m, b + m, alpha);
        }

        ImVec4 WithAlpha(const ImVec4& color, float alpha) noexcept
        {
            return ImVec4(color.x, color.y, color.z, alpha);
        }

        ImVec4 PresetAccent(Preset preset) noexcept
        {
            switch (preset)
            {
            case Preset::Magenta:  // #FF3D8E
                return ImVec4(1.f, 61.f / 255.f, 142.f / 255.f, 1.f);
            case Preset::Teal:  // #12CBB4
                return ImVec4(
                    18.f / 255.f, 203.f / 255.f, 180.f / 255.f, 1.f);
            case Preset::Ember:  // #FF7A29
                return ImVec4(1.f, 122.f / 255.f, 41.f / 255.f, 1.f);
            case Preset::Lime:  // #A8E63A
                return ImVec4(
                    168.f / 255.f, 230.f / 255.f, 58.f / 255.f, 1.f);
            case Preset::Iris:
            default:
                return kIris;
            }
        }

        void Rebuild() noexcept
        {
            const Hsl accent = ToHsl(g_AccentBase);
            const float hue = accent.h;
            const float sat = ImClamp(accent.s, 0.45f, 0.95f);

            // Keep the chosen hex on ON / active / primary / slider fill.
            // Tint and pressed are the only derived steps. On light surfaces the
            // tint has to go darker than the accent, not lighter, or accent
            // glyphs disappear into the white.
            g_Palette.accent.tint = FromHsl(
                hue,
                ImMin(sat + 0.02f, 1.f),
                g_Light ? 0.44f : 0.88f,
                1.f);
            g_Palette.accent.brand = g_AccentBase;
            g_Palette.accent.active = g_AccentBase;
            g_Palette.accent.fill = g_AccentBase;
            g_Palette.accent.pressed =
                FromHsl(hue, ImMax(sat - 0.04f, 0.f), 0.42f, 1.f);

            if (!g_Light)
            {
                // User canvas; inner panes step up in value only.
                g_Palette.canvas = Lift(g_CanvasBase, 0.f, 0.96f);
                g_Palette.base = Lift(g_CanvasBase, 10.f / 255.f, 1.f);
                g_Palette.panel = Lift(g_CanvasBase, 16.f / 255.f, 1.f);
                g_Palette.raised = Lift(g_CanvasBase, 22.f / 255.f, 1.f);
                g_Palette.hover = Lift(g_CanvasBase, 30.f / 255.f, 1.f);
                g_Palette.pressed = Lift(g_CanvasBase, 14.f / 255.f, 1.f);

                g_Palette.borderSubtle = ImVec4(1.f, 1.f, 1.f, 0.08f);
                g_Palette.borderStrong = ImVec4(1.f, 1.f, 1.f, 0.14f);
                g_Palette.borderFocus =
                    WithAlpha(g_Palette.accent.active, 0.60f);

                g_Palette.textPrimary = ImVec4(1.f, 1.f, 1.f, 1.f);
                g_Palette.textMuted = ImVec4(0.62f, 0.63f, 0.66f, 1.f);
                g_Palette.textFaint = ImVec4(0.44f, 0.45f, 0.48f, 1.f);
            }
            else
            {
                // Light mode is not the dark ramp inverted: the canvas is a
                // soft grey and the cards sit on top of it in white. Everything
                // used to live between 0.93 and 1.0, so nothing had an edge and
                // the whole sheet glared.
                // Nothing is pure white: the peak surface stops at 0.98 so a
                // full-screen menu does not glare.
                g_Palette.canvas = FromHsl(hue, 0.08f, 0.90f, 1.f);
                g_Palette.base = FromHsl(hue, 0.04f, 0.975f, 1.f);
                g_Palette.panel = FromHsl(hue, 0.03f, 0.985f, 1.f);
                g_Palette.raised = FromHsl(hue, 0.11f, 0.915f, 1.f);
                g_Palette.hover = FromHsl(hue, 0.15f, 0.865f, 1.f);
                g_Palette.pressed = FromHsl(hue, 0.17f, 0.815f, 1.f);

                g_Palette.borderSubtle =
                    WithAlpha(FromHsl(hue, 0.12f, 0.24f, 1.f), 0.20f);
                g_Palette.borderStrong =
                    WithAlpha(FromHsl(hue, 0.12f, 0.20f, 1.f), 0.34f);
                g_Palette.borderFocus =
                    WithAlpha(g_Palette.accent.pressed, 0.70f);

                g_Palette.textPrimary = FromHsl(hue, 0.14f, 0.10f, 1.f);
                g_Palette.textMuted = FromHsl(hue, 0.10f, 0.34f, 1.f);
                g_Palette.textFaint = FromHsl(hue, 0.09f, 0.48f, 1.f);
            }

            const float accentLuma =
                0.2126f * g_Palette.accent.fill.x +
                0.7152f * g_Palette.accent.fill.y +
                0.0722f * g_Palette.accent.fill.z;
            g_Palette.onAccent = accentLuma > 0.55f
                ? ImVec4(0.05f, 0.05f, 0.07f, 1.f)
                : ImVec4(1.f, 1.f, 1.f, 1.f);

            // Status hues stay away from the brand so a 3 px dot never reads
            // as an accent. The light set is darker: the dark-theme tones turn
            // into pastels on white and stop reading as status at all.
            if (!g_Light)
            {
                g_Palette.success = ImVec4(0.25f, 0.83f, 0.55f, 1.f);
                g_Palette.warning = ImVec4(0.94f, 0.71f, 0.16f, 1.f);
                g_Palette.error = ImVec4(1.00f, 0.35f, 0.43f, 1.f);
                g_Palette.unknown = ImVec4(0.48f, 0.45f, 0.51f, 1.f);
            }
            else
            {
                g_Palette.success = ImVec4(0.06f, 0.58f, 0.36f, 1.f);
                g_Palette.warning = ImVec4(0.67f, 0.46f, 0.04f, 1.f);
                g_Palette.error = ImVec4(0.79f, 0.15f, 0.24f, 1.f);
                g_Palette.unknown = ImVec4(0.36f, 0.34f, 0.40f, 1.f);
            }
        }

        void MirrorToLeyawin() noexcept
        {
            InitLeyawinGlobals();

            clr->white_color = ImVec4(1.f, 1.f, 1.f, 1.f);
            clr->black_color = ImVec4(0.f, 0.f, 0.f, 1.f);
            clr->accent_color = g_Palette.accent.active;

            clr->window_background = g_Palette.canvas;
            clr->child_bottom_bar = g_Palette.base;
            clr->child_top_bar = g_Palette.panel;
            clr->selection_active = g_Palette.raised;
            clr->element_background = g_Palette.raised;
            clr->keybind_background = g_Palette.base;
            clr->combo_color = g_Palette.panel;
            clr->child_stroke_bar = g_Palette.borderSubtle;
            clr->separator = g_Palette.borderSubtle;
            clr->text_active = g_Palette.textPrimary;
            clr->text = g_Palette.textMuted;

            set->window_rounding = g_Metrics.radiusWindow;
            set->child_rounding = g_Metrics.radiusPanel;
            set->selection_rounding = g_Metrics.radiusControl;
            set->checkbox_rounding = g_Metrics.radiusControl;
            set->input_rounding = g_Metrics.radiusControl;
            set->color_rounding = g_Metrics.radiusControl;
            set->keybind_rounding = g_Metrics.radiusControl;
            set->watermark_rounding = g_Metrics.radiusControl;
            set->combo_rounding = g_Metrics.radiusControl;
            set->window_border_size = 0.f;
        }
    }

    const Palette& Colors() noexcept
    {
        return g_Palette;
    }

    const Metrics& Sizes() noexcept
    {
        return g_Metrics;
    }

    ImVec4 BrandColor() noexcept
    {
        return kIris;
    }

    Preset CurrentPreset() noexcept
    {
        return g_Preset;
    }

    void SetPreset(Preset preset) noexcept
    {
        if (preset < Preset::Iris || preset >= Preset::Count)
            preset = Preset::Iris;
        g_Preset = preset;
        g_AccentBase = PresetAccent(preset);
        Apply(g_Light);
    }

    ImVec4 PresetColor(Preset preset) noexcept
    {
        return PresetAccent(preset);
    }

    const char* PresetName(Preset preset) noexcept
    {
        switch (preset)
        {
        case Preset::Magenta: return "Magenta";
        case Preset::Teal: return "Teal";
        case Preset::Ember: return "Ember";
        case Preset::Lime: return "Lime";
        case Preset::Iris:
        default: return "Iris";
        }
    }

    void ResetToDefault() noexcept
    {
        g_Preset = Preset::Iris;
        g_AccentBase = kIris;
        g_CanvasBase = kCanvas;
        Apply(false);
    }

    void SetAccentBase(const ImVec4& color) noexcept
    {
        g_AccentBase = ImVec4(color.x, color.y, color.z, 1.f);
        Rebuild();
        MirrorToLeyawin();
    }

    ImVec4 AccentBase() noexcept
    {
        return g_AccentBase;
    }

    void SetCanvasBase(const ImVec4& color) noexcept
    {
        g_CanvasBase = ImVec4(color.x, color.y, color.z, 1.f);
        Rebuild();
        MirrorToLeyawin();
    }

    ImVec4 CanvasBase() noexcept
    {
        return g_CanvasBase;
    }

    bool IsLight() noexcept
    {
        return g_Light;
    }

    void Apply(bool light) noexcept
    {
        g_Light = light;
        Rebuild();
        MirrorToLeyawin();
    }

    float Scaled(float value) noexcept
    {
        return value * Menu::UIScale();
    }
}

// Legacy entry point kept for existing call sites.
void ApplyLeyawinTheme(bool light)
{
    MenuTheme::Apply(light);
}
