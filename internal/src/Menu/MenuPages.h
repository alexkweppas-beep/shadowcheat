#pragma once

// Internal header for the menu page translation units. Menu.cpp owns the
// shell; every page lives in its own file and they meet here.

#include <cstddef>
#include <cstdint>

#include "Menu.h"
#include "MenuState.h"
#include "MenuUiHelpers.h"

struct ImGuiStyle;

namespace Menu
{
    // Aim reveal + inline slider are shared: Misc and Visuals reuse the same
    // disclosure and the same value row as the Aim page.
    bool BeginAimReveal(
        const char* id,
        bool open,
        float expandedHeight) noexcept;
    void EndAimReveal(bool open) noexcept;
    void DrawAimAnimatedSlider(
        const char* id,
        bool open,
        const char* icon,
        const char* label,
        float* value,
        float minimum,
        float maximum,
        const char* format) noexcept;

    MenuUi::FeatureRowResult MiscRow(
        const char* id,
        const char* icon,
        const char* label,
        bool* enabled,
        bool overflow = false,
        bool bindable = true) noexcept;

    void FormatSubscriptionRemaining(
        char* buf,
        int capacity,
        std::int64_t expiresAt) noexcept;

    // One entry point per rail destination.
    void RenderCombatPage();
    void RenderWeaponsPage();
    void RenderVisualsPage();
    // Drawn by the shell rather than by the objects page, because a
    // double-click on an object has to open it from wherever the user happens
    // to be standing in the menu.
    void RenderObjectFilterPopup() noexcept;
    void RenderPlayersPage();
    void RenderAroundPage();
    void RenderMiscPage();
    void RenderWebRadarIsland();
    void RenderPlayerPage();
    void RenderCarsPage();
    void RenderWorldPage();
    void RenderServerPage();
    void RenderConfigPage(ImGuiStyle* style);

    // Compact customisation drawn inside the gear flyout, not on a page.
    void RenderMenuSettingsFlyout();
}
