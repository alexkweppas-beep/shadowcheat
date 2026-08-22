#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "EspProjectedOverlay.h"
#include "EspDrawHelpers.h"
#include "MenuTexture.h"
#include "Menu.h"
#include "WeaponIconMap.h"
#include "../Features/RuntimeProfiler.h"
#include "../SDK/WeaponCatalog.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace EspProjected
{
namespace
{
    // Draw counters for the profiler: how many labels and how many glyph
    // passes the crowd cost this frame. Present-thread only, no atomics.
    std::uint32_t g_FrameLabels = 0;
    std::uint32_t g_FrameGlyphPasses = 0;

    // Scratch for the one-time layout migration, built at load instead of per
    // ped. Overlay drawing is single-threaded (Present or the preview).
    Menu::EspProfileSettings g_MigrationScratch{};

    float ClampSize(float s) noexcept
    {
        if (s < 0.5f) return 0.5f;
        if (s > 2.0f) return 2.0f;
        return s;
    }

    ImU32 WithAlpha(ImU32 c, float opacity) noexcept
    {
        const int a = (int)(((c >> 24) & 0xFF) * opacity);
        return (c & 0x00FFFFFF) | ((ImU32)std::clamp(a, 0, 255) << 24);
    }

    int BarStyleOf(const Menu::EspProfileSettings& p, ElementId id) noexcept
    {
        return id == ElementId::Armor ? p.armorStyle : p.healthStyle;
    }

    bool DrawsAsBar(const Menu::EspProfileSettings& p, ElementId id) noexcept
    {
        if (!IsBar(id))
            return false;
        return BarStyleOf(p, id) != EspBarText;
    }

    bool Enabled(const Menu::EspProfileSettings& p, ElementId id) noexcept
    {
        switch (id) {
        case ElementId::Name: return p.nameTag;
        case ElementId::Faction: return p.factionTag;
        case ElementId::Family: return p.familyTag;
        case ElementId::Distance: return p.distance;
        case ElementId::Weapon: return p.weaponTag;
        case ElementId::Health: return p.healthBar;
        case ElementId::Armor: return p.armorBar;
        case ElementId::DynamicId: return p.dynamicIdTag;
        case ElementId::StaticId: return p.staticIdTag;
        default: return false;
        }
    }

    const float* ColorOf(const Menu::EspProfileSettings& p, ElementId id) noexcept
    {
        switch (id) {
        case ElementId::Name: return p.nameColor;
        case ElementId::Faction: return p.factionColor;
        case ElementId::Family: return p.familyColor;
        case ElementId::Distance: return p.distanceColor;
        case ElementId::Weapon: return p.weaponColor;
        case ElementId::Health: return p.healthColor;
        case ElementId::Armor: return p.armorColor;
        case ElementId::DynamicId: return p.dynamicIdColor;
        case ElementId::StaticId: return p.staticIdColor;
        default: return p.nameColor;
        }
    }

    const char* TextOf(const PlayerData& player, ElementId id) noexcept
    {
        switch (id) {
        case ElementId::Name: return player.name ? player.name : "";
        case ElementId::Faction: return player.faction ? player.faction : "";
        case ElementId::Family: return player.family ? player.family : "";
        case ElementId::Distance: return player.distance ? player.distance : "";
        case ElementId::Weapon: return player.weapon ? player.weapon : "";
        case ElementId::DynamicId: return player.dynamicId ? player.dynamicId : "";
        case ElementId::StaticId: return player.staticId ? player.staticId : "";
        default: return "";
        }
    }

    const char* IconFileForHash(std::uint32_t hash) noexcept
    {
        if (const auto* e = WeaponCatalog::FindByHash(hash))
            return e->iconFile;
        return nullptr;
    }
}

Layout GetLayout(const Menu::EspProfileSettings& p, ElementId id) noexcept
{
    const int i = IndexOf(id);
    Layout out{};
    if (i < 0) return out;
    out.side = static_cast<Side>(std::clamp(p.layouts[i].side, 0, 3));
    out.order = (std::max)(0, p.layouts[i].order);
    return out;
}

void SetLayout(Menu::EspProfileSettings& p, ElementId id, Side side, int order) noexcept
{
    const int i = IndexOf(id);
    if (i < 0) return;
    p.layouts[i].side = static_cast<int>(side);
    p.layouts[i].order = (std::max)(0, order);

    const int s = static_cast<int>(side);
    switch (id) {
    case ElementId::Health:
        p.healthBarPos = (s == 2) ? EspHealthBarLeft
            : (s == 3) ? EspHealthBarRight
            : (s == 0) ? EspHealthBarTop
                       : EspHealthBarBottom;
        break;
    case ElementId::Armor:
        p.armorBarPos = (s == 2) ? EspHealthBarLeft
            : (s == 3) ? EspHealthBarRight
            : (s == 0) ? EspHealthBarTop
                       : EspHealthBarBottom;
        break;
    case ElementId::Name:
        p.nameTagPos = (side == Side::Bottom) ? EspTagBelow : EspTagAbove;
        break;
    case ElementId::Faction:
        p.factionTagPos = (side == Side::Bottom) ? EspTagBelow : EspTagAbove;
        break;
    case ElementId::Distance:
        p.distancePos = (side == Side::Bottom) ? EspTagBelow : EspTagAbove;
        break;
    case ElementId::Weapon:
        p.weaponTagPos = (side == Side::Bottom) ? EspTagBelow : EspTagAbove;
        break;
    default:
        break;
    }
}

ElementStyle GetStyle(const Menu::EspProfileSettings& p, ElementId id) noexcept
{
    ElementStyle s{};
    const int i = IndexOf(id);
    if (i >= 0)
        s.size = ClampSize(p.layouts[i].size);
    if (id == ElementId::Health)
        s.showValue = p.healthShowValue || p.healthStyle == EspBarValue;
    if (id == ElementId::Armor)
        s.showValue = p.armorShowValue || p.armorStyle == EspBarValue;
    return s;
}

void SetStyleSize(Menu::EspProfileSettings& p, ElementId id, float size) noexcept
{
    const int i = IndexOf(id);
    if (i < 0) return;
    p.layouts[i].size = ClampSize(size);
}

void EnsureLayoutsMigrated(Menu::EspProfileSettings& p) noexcept
{
    if (p.layoutsMigrated)
        return;

    auto fromTag = [](int tagPos) -> Side {
        return tagPos == EspTagBelow ? Side::Bottom : Side::Top;
    };
    auto fromBar = [](int barPos) -> Side {
        switch (barPos) {
        case EspHealthBarRight: return Side::Right;
        case EspHealthBarTop: return Side::Top;
        case EspHealthBarBottom: return Side::Bottom;
        default: return Side::Left;
        }
    };

    p.layouts[0] = { static_cast<int>(fromTag(p.nameTagPos)), 0, 1.f };
    p.layouts[1] = { static_cast<int>(fromTag(p.factionTagPos)), 1, 1.f };
    p.layouts[2] = { static_cast<int>(Side::Top), 2, 1.f }; // Family
    p.layouts[3] = { static_cast<int>(fromTag(p.distancePos)), 0, 1.f };
    p.layouts[4] = { static_cast<int>(fromTag(p.weaponTagPos)), 1, 1.f };
    p.layouts[5] = { static_cast<int>(fromBar(p.healthBarPos)), 0, 1.f };
    p.layouts[6] = { static_cast<int>(fromBar(p.armorBarPos)), 1, 1.f };
    p.layouts[7] = { static_cast<int>(Side::Top), 3, 1.f }; // DynamicId
    p.layouts[8] = { static_cast<int>(Side::Top), 4, 1.f }; // StaticId
    p.layoutsMigrated = true;
}

const char* ElementLabel(ElementId id) noexcept
{
    switch (id) {
    case ElementId::Name: return "Name";
    case ElementId::Faction: return "Faction";
    case ElementId::Family: return "Family";
    case ElementId::Distance: return "Distance";
    case ElementId::Weapon: return "Weapon";
    case ElementId::Health: return "Health";
    case ElementId::Armor: return "Armor";
    case ElementId::DynamicId: return "Dynamic ID";
    case ElementId::StaticId: return "Static ID";
    default: return "Element";
    }
}

Side ResolveSide(
    const ImVec2& boxMin,
    const ImVec2& boxMax,
    const ImVec2& point) noexcept
{
    if (point.x < boxMin.x) return Side::Left;
    if (point.x > boxMax.x) return Side::Right;
    const float cy = (boxMin.y + boxMax.y) * 0.5f;
    return point.y < cy ? Side::Top : Side::Bottom;
}

int ResolveOrder(
    const OverlayRects& rects,
    Side side,
    const ImVec2& point,
    ElementId moving) noexcept
{
    float centers[kElementCount]{};
    int centerCount = 0;
    for (int i = 0; i < kElementCount; ++i) {
        const auto& e = rects.elements[i];
        if (!e.visible || e.side != side)
            continue;
        if (static_cast<ElementId>(i) == moving)
            continue;
        if ((side == Side::Left || side == Side::Right) &&
            IsBar(static_cast<ElementId>(i)))
            continue;
        centers[centerCount++] = e.rect.GetCenter().y;
    }
    std::sort(centers, centers + centerCount);
    int order = 0;
    for (int index = 0; index < centerCount; ++index) {
        if (point.y > centers[index])
            ++order;
    }
    return order;
}

ElementId HitTest(
    const OverlayRects& rects,
    const ImVec2& mouse,
    const ImVec2& pad) noexcept
{
    ElementId best = ElementId::None;
    float bestArea = 1e18f;
    for (int i = 0; i < kElementCount; ++i) {
        const auto& e = rects.elements[i];
        if (!e.visible)
            continue;
        const ImRect hit(
            e.rect.Min - pad,
            e.rect.Max + pad);
        if (!hit.Contains(mouse))
            continue;
        const float area =
            (hit.Max.x - hit.Min.x) * (hit.Max.y - hit.Min.y);
        if (area < bestArea) {
            bestArea = area;
            best = static_cast<ElementId>(i);
        }
    }
    return best;
}

static OverlayRects DrawPlayerOverlayImpl(
    ImDrawList* draw,
    const Menu::EspProfileSettings& profile,
    const PlayerData& player,
    const ImVec2& boxMin,
    const ImVec2& boxMax,
    float opacity,
    const DrawOptions* options,
    bool exactBox)
{
    OverlayRects out{};
    if (!draw ||
        !std::isfinite(boxMin.x) ||
        !std::isfinite(boxMin.y) ||
        !std::isfinite(boxMax.x) ||
        !std::isfinite(boxMax.y) ||
        boxMax.x <= boxMin.x ||
        boxMax.y <= boxMin.y)
        return out;

    // The migration copy is half a kilobyte; declaring it here meant every ped
    // in the crowd paid a zero-init for a branch that runs once per profile.
    const Menu::EspProfileSettings* localPtr =
        &profile;
    if (!profile.layoutsMigrated)
    {
        g_MigrationScratch = profile;
        EnsureLayoutsMigrated(g_MigrationScratch);
        localPtr = &g_MigrationScratch;
    }
    const Menu::EspProfileSettings& local =
        *localPtr;
    const auto resolveColor =
        [&](ElementId id) noexcept -> const float* {
            // A drag preview recolours everything, so it wins: while dragging,
            // the element being moved has to read as the thing under the
            // cursor, not as its faction.
            if (options && options->colorOverride)
                return options->colorOverride;
            if (id == ElementId::Faction &&
                options &&
                options->factionColorOverride)
                return options->factionColorOverride;
            return ColorOf(local, id);
        };
    float drawOpacity = opacity;
    if (player.dead || player.knock)
        drawOpacity *= 0.7f;
    const bool reducedOutline =
        player.rawDistance > 120.f &&
        !(options && options->dragActive);
    const auto drawLabel =
        [&](ImFont* font,
            float fontSize,
            const ImVec2& position,
            ImU32 color,
            const char* text) {
            ++g_FrameLabels;
            g_FrameGlyphPasses += reducedOutline ? 2u : 5u;
            if (!reducedOutline)
            {
                EspDrawOutlinedText(
                    draw,
                    font,
                    fontSize,
                    position,
                    color,
                    text);
                return;
            }
            draw->AddText(
                font,
                fontSize,
                position + ImVec2(1.f, 1.f),
                WithAlpha(
                    IM_COL32(0, 0, 0, 210),
                    drawOpacity),
                text);
            draw->AddText(
                font,
                fontSize,
                position,
                color,
                text);
        };

    const float cx = (boxMin.x + boxMax.x) * 0.5f;
    const float topGap = 6.f;
    const float bottomGap = exactBox ? 6.f : 4.f;
    const float sideGap = exactBox ? 6.f : 8.f;
    const float barOutset = exactBox ? 4.f : 0.f;
    const float barStackStride = 9.f;

    if (local.box) {
        RUNTIME_PROFILE_BEGIN(boxTimer);
        // The box-only override wins over the all-element one, so a caller can
        // recolour the outline while leaving the labels alone.
        const float* outlineOverride =
            options
                ? (options->boxColorOverride
                    ? options->boxColorOverride
                    : options->colorOverride)
                : nullptr;
        const float* boxColSrc =
            (player.dead || player.knock)
                ? local.deadColor
                : (outlineOverride ? outlineOverride : local.boxColor);
        const ImU32 boxCol = EspRgba(boxColSrc, drawOpacity);
        const float* fillSrc =
            outlineOverride ? outlineOverride : local.boxFillColor;
        const ImU32 fillCol = EspRgba(fillSrc, drawOpacity * 0.22f);
        EspDrawBoxStyled(
            draw, boxMin, boxMax, boxCol, local.boxStyle, fillCol, 1.8f);
        RUNTIME_PROFILE_ACCUM(EspBoxes, boxTimer);
    }

    struct Item
    {
        ElementId id{};
        Layout layout{};
        ElementStyle style{};
        bool isBar{};
    };
    Item items[kElementCount]{};
    int itemCount = 0;
    for (int i = 0; i < kElementCount; ++i) {
        const auto id = static_cast<ElementId>(i);
        if (!Enabled(local, id))
            continue;
        if (options &&
            (options->disabledMask &
             (1u << static_cast<unsigned>(i))))
            continue;
        Item item{};
        item.id = id;
        item.layout = GetLayout(local, id);
        item.style = GetStyle(local, id);
        item.isBar = DrawsAsBar(local, id);
        if (options && options->dragActive && options->dragElement == id) {
            item.layout.side = ResolveSide(boxMin, boxMax, options->dragPos);
        }
        items[itemCount++] = item;
    }

    char hpTextBuf[24]{};
    char arTextBuf[24]{};
    const auto countBarsOnSide =
        [&](Side side) noexcept {
            int count = 0;
            for (int index = 0;
                 index < itemCount;
                 ++index)
            {
                if (items[index].isBar &&
                    items[index].layout.side == side)
                {
                    ++count;
                }
            }
            return count;
        };

    RUNTIME_PROFILE_BEGIN(textTimer);
    for (int s = 0; s < 4; ++s) {
        const Side side = static_cast<Side>(s);
        Item* list[kElementCount]{};
        int listCount = 0;
        for (int index = 0;
             index < itemCount;
             ++index)
        {
            if (items[index].layout.side == side &&
                !items[index].isBar)
                list[listCount++] = &items[index];
        }
        for (int index = 1;
             index < listCount;
             ++index)
        {
            Item* value = list[index];
            int insert = index;
            while (insert > 0 &&
                list[insert - 1]->layout.order >
                    value->layout.order)
            {
                list[insert] = list[insert - 1];
                --insert;
            }
            list[insert] = value;
        }
        float cursor = 0.f;
        const int adjacentBarCount =
            countBarsOnSide(side);
        if ((side == Side::Top ||
             side == Side::Bottom) &&
            adjacentBarCount > 0)
        {
            cursor = exactBox
                ? static_cast<float>(
                    adjacentBarCount) *
                    barStackStride
                : static_cast<float>(
                    adjacentBarCount) * 8.f + 4.f;
        }
        const float sideBarClearance =
            exactBox &&
            (side == Side::Left ||
             side == Side::Right)
                ? static_cast<float>(
                    adjacentBarCount) *
                    barStackStride
                : 0.f;
        const float sideTextGap =
            sideGap + sideBarClearance;

        float distScale = 1.f;
        if (player.rawDistance > 35.f) {
            const float t = (std::min)(player.rawDistance - 35.f, 215.f) / 215.f;
            distScale = 1.f - (0.40f * t); // smooth scale down to 0.60x at 250m+
        }

        for (int listIndex = 0;
             listIndex < listCount;
             ++listIndex)
        {
            Item* it = list[listIndex];
            const float fontSize = ImGui::GetFontSize() * it->style.size * distScale;
            ImFont* font = ImGui::GetFont();

            // Health/Armor text style
            if (IsBar(it->id) && BarStyleOf(local, it->id) == EspBarText) {
                const float pct =
                    it->id == ElementId::Health ? player.health : player.armor;
                char* buf = it->id == ElementId::Health ? hpTextBuf : arTextBuf;
                sprintf_s(
                    buf,
                    24,
                    it->id == ElementId::Health ? "[HP %.0f]" : "[A %.0f]",
                    pct * 100.f);
                const ImVec2 textSize =
                    font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, buf);
                ImVec2 pos{};
                if (side == Side::Top) {
                    pos = ImVec2(cx - textSize.x * 0.5f, boxMin.y - topGap - cursor - textSize.y);
                    cursor += textSize.y + 2.f;
                } else if (side == Side::Bottom) {
                    pos = ImVec2(cx - textSize.x * 0.5f, boxMax.y + bottomGap + cursor);
                    cursor += textSize.y + 2.f;
                } else if (side == Side::Left) {
                    pos = ImVec2(boxMin.x - sideTextGap - textSize.x, boxMin.y + cursor);
                    cursor += textSize.y + 2.f;
                } else {
                    pos = ImVec2(boxMax.x + sideTextGap, boxMin.y + cursor);
                    cursor += textSize.y + 2.f;
                }
                if (options && options->dragActive && options->dragElement == it->id)
                    pos = options->dragPos;
                drawLabel(
                    font, fontSize, pos,
                    EspRgba(resolveColor(it->id), drawOpacity),
                    buf);
                const int idx = IndexOf(it->id);
                out.elements[idx].visible = true;
                out.elements[idx].rect = ImRect(pos, pos + textSize);
                out.elements[idx].side = side;
                out.elements[idx].order = it->layout.order;
                continue;
            }

            // Weapon: text / icon / both
            if (it->id == ElementId::Weapon) {
                const int mode = std::clamp(
                    local.weaponDisplay, 0, EspWeaponDisplayCount - 1);
                const bool wantText =
                    mode == EspWeaponText || mode == EspWeaponBoth;
                const bool wantIcon =
                    mode == EspWeaponIcon || mode == EspWeaponBoth;
                const char* text = wantText ? TextOf(player, ElementId::Weapon) : "";
                
                ImFont* iconFont = Menu::GetWeaponIconFont();
                char iconUtf8[8]{};
                bool hasFontIcon = wantIcon && BuildWeaponIconUtf8(player.weaponHash, iconFont, iconUtf8);

                const char* iconFile =
                    (wantIcon && !hasFontIcon) ? IconFileForHash(player.weaponHash) : nullptr;
                void* iconTex = iconFile
                    ? MenuTexture::GetWeaponIconImGui(iconFile)
                    : nullptr;

                float iconW = 0.f, iconH = 0.f;
                if (hasFontIcon && iconFont) {
                    ImVec2 iconSz = iconFont->CalcTextSizeA(fontSize * 1.15f, FLT_MAX, 0.f, iconUtf8);
                    iconW = iconSz.x;
                    iconH = iconSz.y;
                } else if (iconTex && iconFile) {
                    const float maxH = fontSize * 1.35f;
                    const float maxW = maxH * 2.2f;
                    MenuTexture::FitWeaponIconSize(
                        iconFile, maxW, maxH, &iconW, &iconH);
                }

                ImVec2 textSize(0.f, 0.f);
                if (text && text[0])
                    textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, text);

                const float gap = (iconW > 0.f && textSize.x > 0.f) ? 4.f : 0.f;
                const float totalW = iconW + gap + textSize.x;
                const float totalH = (std::max)(iconH, textSize.y);
                if (totalW <= 0.f || totalH <= 0.f)
                    continue;

                ImVec2 pos{};
                if (side == Side::Top) {
                    pos = ImVec2(cx - totalW * 0.5f, boxMin.y - topGap - cursor - totalH);
                    cursor += totalH + 2.f;
                } else if (side == Side::Bottom) {
                    pos = ImVec2(cx - totalW * 0.5f, boxMax.y + bottomGap + cursor);
                    cursor += totalH + 2.f;
                } else if (side == Side::Left) {
                    pos = ImVec2(boxMin.x - sideTextGap - totalW, boxMin.y + cursor);
                    cursor += totalH + 2.f;
                } else {
                    pos = ImVec2(boxMax.x + sideTextGap, boxMin.y + cursor);
                    cursor += totalH + 2.f;
                }
                if (options && options->dragActive && options->dragElement == it->id)
                    pos = options->dragPos;

                // Same hover plate the plain text elements get; the weapon row
                // used to be the only element that stayed unlit.
                const float weaponHi = options
                    ? options->interactionAlpha[IndexOf(it->id)]
                    : 0.f;
                if (weaponHi > 0.01f) {
                    draw->AddRectFilled(
                        ImVec2(pos.x - 3.f, pos.y - 1.f),
                        ImVec2(pos.x + totalW + 3.f, pos.y + totalH + 1.f),
                        IM_COL32(
                            255, 255, 255,
                            (int)(28.f * weaponHi * drawOpacity)),
                        3.f);
                }

                float x = pos.x;
                if (hasFontIcon && iconFont && iconW > 0.f) {
                    const float iy = pos.y + (totalH - iconH) * 0.5f;
                    drawLabel(
                        iconFont, fontSize * 1.15f, ImVec2(x, iy),
                        EspRgba(resolveColor(
                            ElementId::Weapon), drawOpacity),
                        iconUtf8);
                    x += iconW + gap;
                } else if (iconTex && iconW > 0.f) {
                    const float iy = pos.y + (totalH - iconH) * 0.5f;
                    draw->AddImage(
                        (ImTextureID)iconTex,
                        ImVec2(x, iy),
                        ImVec2(x + iconW, iy + iconH),
                        ImVec2(0.f, 0.f),
                        ImVec2(1.f, 1.f),
                        WithAlpha(IM_COL32(255, 255, 255, 255), drawOpacity));
                    x += iconW + gap;
                }
                if (text && text[0]) {
                    const float ty = pos.y + (totalH - textSize.y) * 0.5f;
                    drawLabel(
                        font, fontSize, ImVec2(x, ty),
                        EspRgba(resolveColor(
                            ElementId::Weapon), drawOpacity),
                        text);
                }

                const int idx = IndexOf(it->id);
                out.elements[idx].visible = true;
                out.elements[idx].rect =
                    ImRect(pos, ImVec2(pos.x + totalW, pos.y + totalH));
                out.elements[idx].side = side;
                out.elements[idx].order = it->layout.order;
                continue;
            }

            const char* text = TextOf(player, it->id);
            if (!text || !text[0])
                continue;

            const ImVec2 textSize =
                font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, text);

            ImVec2 pos{};
            if (side == Side::Top) {
                pos = ImVec2(cx - textSize.x * 0.5f, boxMin.y - topGap - cursor - textSize.y);
                cursor += textSize.y + 2.f;
            } else if (side == Side::Bottom) {
                pos = ImVec2(cx - textSize.x * 0.5f, boxMax.y + bottomGap + cursor);
                cursor += textSize.y + 2.f;
            } else if (side == Side::Left) {
                pos = ImVec2(boxMin.x - sideTextGap - textSize.x, boxMin.y + cursor);
                cursor += textSize.y + 2.f;
            } else {
                pos = ImVec2(boxMax.x + sideTextGap, boxMin.y + cursor);
                cursor += textSize.y + 2.f;
            }

            if (options && options->dragActive && options->dragElement == it->id)
                pos = options->dragPos;

            const int idx = IndexOf(it->id);
            const float hi =
                options ? options->interactionAlpha[idx] : 0.f;
            if (hi > 0.01f) {
                draw->AddRectFilled(
                    ImVec2(pos.x - 3.f, pos.y - 1.f),
                    ImVec2(pos.x + textSize.x + 3.f, pos.y + textSize.y + 1.f),
                    IM_COL32(255, 255, 255, (int)(28.f * hi * drawOpacity)),
                    3.f);
            }

            drawLabel(
                font, fontSize, pos,
                EspRgba(resolveColor(it->id), drawOpacity),
                text);

            out.elements[idx].visible = true;
            out.elements[idx].rect = ImRect(pos, pos + textSize);
            out.elements[idx].side = side;
            out.elements[idx].order = it->layout.order;
        }
    }
    RUNTIME_PROFILE_ACCUM(EspText, textTimer);

    RUNTIME_PROFILE_BEGIN(barTimer);
    for (int itemIndex = 0;
         itemIndex < itemCount;
         ++itemIndex)
    {
        Item& it = items[itemIndex];
        if (!it.isBar)
            continue;
        const float pct =
            it.id == ElementId::Health ? player.health : player.armor;
        const int barPos =
            it.layout.side == Side::Right ? EspHealthBarRight
            : it.layout.side == Side::Top ? EspHealthBarTop
            : it.layout.side == Side::Bottom ? EspHealthBarBottom
                                            : EspHealthBarLeft;
        const int stack =
            (it.id == ElementId::Armor && local.healthBar &&
             DrawsAsBar(local, ElementId::Health) &&
             GetLayout(local, ElementId::Health).side == it.layout.side)
                ? 1
                : 0;
        const float thickScale = it.style.size;
        ImVec2 fillMin, fillMax, outlineMin, outlineMax;
        EspComputeHealthBarRects(
            boxMin, boxMax, std::clamp(pct, 0.f, 1.f), barPos,
            &fillMin, &fillMax, &outlineMin, &outlineMax, stack);
        ImVec2 barOffset{};
        if (exactBox) {
            if (barPos == EspHealthBarLeft)
                barOffset.x = -barOutset;
            else if (barPos == EspHealthBarRight)
                barOffset.x = barOutset;
            else if (barPos == EspHealthBarTop)
                barOffset.y = -barOutset;
            else
                barOffset.y = barOutset;
            fillMin = fillMin + barOffset;
            fillMax = fillMax + barOffset;
            outlineMin = outlineMin + barOffset;
            outlineMax = outlineMax + barOffset;
        }

        if (thickScale != 1.f) {
            const float grow = (thickScale - 1.f) * 2.f;
            if (barPos == EspHealthBarLeft || barPos == EspHealthBarRight) {
                outlineMin.x -= grow;
                outlineMax.x += grow;
                fillMin.x -= grow * 0.5f;
                fillMax.x += grow * 0.5f;
            } else {
                outlineMin.y -= grow;
                outlineMax.y += grow;
                fillMin.y -= grow * 0.5f;
                fillMax.y += grow * 0.5f;
            }
        }

        const bool draggingBar =
            options && options->dragActive && options->dragElement == it.id;
        if (draggingBar) {
            // Centred on the cursor: anchoring a tall bar by its top left
            // corner threw it far above the pointer and it read as snapping to
            // the top edge.
            const ImVec2 size = outlineMax - outlineMin;
            outlineMin = options->dragPos - size * 0.5f;
            outlineMax = outlineMin + size;
            fillMin = outlineMin + ImVec2(1.f, 1.f);
            fillMax = outlineMax - ImVec2(1.f, 1.f);
        }

        int style = BarStyleOf(local, it.id);
        if (style == EspBarValue)
            style = EspBarSolid;
        if (!draggingBar) {
            EspDrawBarStyled(
                draw, boxMin, boxMax, std::clamp(pct, 0.f, 1.f), barPos, style,
                resolveColor(it.id), it.id == ElementId::Armor, stack,
                (int)(255.f * drawOpacity), barOffset);
        }

        if (draggingBar || thickScale > 1.05f || thickScale < 0.95f) {
            const bool armorBar = it.id == ElementId::Armor;
            const int fillAlpha = (int)(255.f * drawOpacity);
            const ImU32 fillCol = (style == EspBarDynamic)
                ? (armorBar
                    ? EspArmorGradientColor(std::clamp(pct, 0.f, 1.f), fillAlpha)
                    : EspHealthColor(std::clamp(pct, 0.f, 1.f), fillAlpha))
                : EspRgba(resolveColor(it.id), drawOpacity);
            draw->AddRect(
                outlineMin, outlineMax,
                WithAlpha(IM_COL32(0, 0, 0, 180), drawOpacity), 0.f, 0, 1.f);
            draw->AddRectFilled(fillMin, fillMax, fillCol);
        }

        if (it.style.showValue) {
            char buf[16]{};
            sprintf_s(buf, "%.0f", pct * 100.f);
            const float fs = ImGui::GetFontSize() * 0.85f;
            const ImVec2 ts =
                ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.f, buf);
            ImVec2 tp(
                (outlineMin.x + outlineMax.x - ts.x) * 0.5f,
                (outlineMin.y + outlineMax.y - ts.y) * 0.5f);
            if (barPos == EspHealthBarLeft) {
                const float leftmostX = (stack > 0) ? outlineMin.x : (outlineMin.x - (local.armorBar && DrawsAsBar(local, ElementId::Armor) && GetLayout(local, ElementId::Armor).side == Side::Left ? 9.f : 0.f));
                const float yPos = (it.id == ElementId::Health) ? (outlineMin.y + 2.f) : (outlineMax.y - ts.y - 2.f);
                tp = ImVec2(leftmostX - ts.x - 3.f, yPos);
            } else if (barPos == EspHealthBarRight) {
                const float rightmostX = (stack > 0) ? outlineMax.x : (outlineMax.x + (local.armorBar && DrawsAsBar(local, ElementId::Armor) && GetLayout(local, ElementId::Armor).side == Side::Right ? 9.f : 0.f));
                const float yPos = (it.id == ElementId::Health) ? (outlineMin.y + 2.f) : (outlineMax.y - ts.y - 2.f);
                tp = ImVec2(rightmostX + 3.f, yPos);
            }
            drawLabel(
                ImGui::GetFont(), fs, tp,
                WithAlpha(IM_COL32(255, 255, 255, 220), drawOpacity),
                buf);
        }

        const int idx = IndexOf(it.id);
        out.elements[idx].visible = true;
        out.elements[idx].rect = ImRect(outlineMin, outlineMax);
        out.elements[idx].side = it.layout.side;
        out.elements[idx].order = it.layout.order;
    }
    RUNTIME_PROFILE_ACCUM(EspBoxes, barTimer);

    return out;
}

OverlayRects DrawPlayerOverlay(
    ImDrawList* draw,
    const Menu::EspProfileSettings& profile,
    const PlayerData& player,
    const ImVec2& top2d,
    const ImVec2& bottom2d,
    float opacity,
    const DrawOptions* options)
{
    const float boxHeight =
        bottom2d.y - top2d.y;
    const ImVec2 boxMin(
        top2d.x - boxHeight * 0.18f,
        top2d.y);
    const ImVec2 boxMax(
        top2d.x + boxHeight * 0.18f,
        bottom2d.y);
    return DrawPlayerOverlayImpl(
        draw,
        profile,
        player,
        boxMin,
        boxMax,
        opacity,
        options,
        false);
}

OverlayRects DrawPlayerOverlay(
    ImDrawList* draw,
    const Menu::EspProfileSettings& profile,
    const PlayerData& player,
    const ImRect& box,
    float opacity,
    const DrawOptions* options)
{
    return DrawPlayerOverlayImpl(
        draw,
        profile,
        player,
        box.Min,
        box.Max,
        opacity,
        options,
        true);
}

void ConsumeDrawCounters(
    unsigned int& labels,
    unsigned int& glyphPasses) noexcept
{
    labels = g_FrameLabels;
    glyphPasses = g_FrameGlyphPasses;
    g_FrameLabels = 0;
    g_FrameGlyphPasses = 0;
}

} // namespace EspProjected
