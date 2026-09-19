#pragma once

#include "Menu.h"
#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"

#include <array>
#include <cstdint>

namespace EspProjected
{
    enum class Side : int
    {
        Top = 0,
        Bottom = 1,
        Left = 2,
        Right = 3,
        Count = 4
    };

    enum class ElementId : int
    {
        None = -1,
        Name = 0,
        Faction,
        Family,
        Distance,
        Weapon,
        Health,
        Armor,
        DynamicId,
        StaticId,
        GreenZone,
        Count
    };

    static constexpr int kElementCount = static_cast<int>(ElementId::Count);
    static constexpr float kSizeStep = 0.05f;

    struct Layout
    {
        Side side = Side::Top;
        int order = 0;
    };

    struct ElementStyle
    {
        float size = 1.f; // 0.5 .. 2.0 multiplier
        bool showValue = true;
    };

    struct PlayerData
    {
        const char* name = "shadowcheats";
        const char* faction = "Faction";
        const char* family = "Family";
        const char* weapon = "Heavy Rifle";
        const char* distance = "12m";
        const char* dynamicId = "ID:1";
        const char* staticId = "#100";
        std::uint32_t weaponHash = 0;
        float health = 0.72f;
        float armor = 0.45f;
        float rawDistance = 0.f;
        bool dead = false;
        bool knock = false;
        bool greenZone = false;
    };

    struct ElementRect
    {
        bool visible = false;
        ImRect rect{};
        Side side = Side::Top;
        int order = 0;
    };

    struct OverlayRects
    {
        std::array<ElementRect, kElementCount> elements{};
    };

    struct DrawOptions
    {
        ElementId dragElement = ElementId::None;
        ImVec2 dragPos{};
        bool dragActive = false;
        ElementId hoverElement = ElementId::None;
        float interactionAlpha[kElementCount]{};
        std::uint32_t disabledMask{};
        const float* colorOverride = nullptr;
        // Recolours the box alone. Narrower than colorOverride, which takes
        // every element at once, and needed because the box is not one of the
        // ElementId entries and so cannot be addressed through them.
        const float* boxColorOverride = nullptr;
        // Per-faction text colour for this one player. Narrower than
        // colorOverride, which recolours every element at once.
        const float* factionColorOverride = nullptr;
    };

    inline int IndexOf(ElementId id) noexcept
    {
        const int i = static_cast<int>(id);
        return (i >= 0 && i < kElementCount) ? i : -1;
    }

    inline bool IsBar(ElementId id) noexcept
    {
        return id == ElementId::Health || id == ElementId::Armor;
    }

    inline bool IsScalable(ElementId id) noexcept
    {
        return id != ElementId::None;
    }

    Layout GetLayout(const Menu::EspProfileSettings& p, ElementId id) noexcept;
    void SetLayout(Menu::EspProfileSettings& p, ElementId id, Side side, int order) noexcept;
    ElementStyle GetStyle(const Menu::EspProfileSettings& p, ElementId id) noexcept;
    void SetStyleSize(Menu::EspProfileSettings& p, ElementId id, float size) noexcept;

    // Migrate legacy nameTagPos/healthBarPos into side+order layouts once.
    void EnsureLayoutsMigrated(Menu::EspProfileSettings& p) noexcept;

    OverlayRects DrawPlayerOverlay(
        ImDrawList* draw,
        const Menu::EspProfileSettings& profile,
        const PlayerData& player,
        const ImVec2& top2d,
        const ImVec2& bottom2d,
        float opacity = 1.f,
        const DrawOptions* options = nullptr);

    // Exact-box path. The supplied rectangle is used for the box and every
    // element anchor; no height-derived width is reconstructed.
    OverlayRects DrawPlayerOverlay(
        ImDrawList* draw,
        const Menu::EspProfileSettings& profile,
        const PlayerData& player,
        const ImRect& box,
        float opacity = 1.f,
        const DrawOptions* options = nullptr);

    ElementId HitTest(
        const OverlayRects& rects,
        const ImVec2& mouse,
        const ImVec2& pad = ImVec2(4.f, 3.f)) noexcept;

    Side ResolveSide(
        const ImVec2& boxMin,
        const ImVec2& boxMax,
        const ImVec2& point) noexcept;

    int ResolveOrder(
        const OverlayRects& rects,
        Side side,
        const ImVec2& point,
        ElementId moving) noexcept;

    const char* ElementLabel(ElementId id) noexcept;

    // Label / glyph-pass tally for the profiler. Reading resets it, so the
    // caller reports one frame worth of draw work.
    void ConsumeDrawCounters(
        unsigned int& labels,
        unsigned int& glyphPasses) noexcept;
}
