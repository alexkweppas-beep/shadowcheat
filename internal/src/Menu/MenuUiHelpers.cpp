#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "MenuUiHelpers.h"
#include "Menu.h"
#include "MenuTheme.h"
#include "MenuType.h"
#include "MenuMotion.h"
#include "IconsFontAwesome6.h"
#include "Leyawin/custom_widgets.hpp"
#include "Leyawin/leyawin_globals.h"
#include "Leyawin/leyawin_safe.h"
#include "../Features/FeatureBinds.h"
#include "../imgui/imgui_internal.h"

#include <cstdio>
#include <cfloat>
#include <cmath>
#include <cstring>

namespace MenuUi
{
namespace
{
    const char* T(
        const char* en,
        const char* ru,
        const char* ua) noexcept
    {
        return Menu::Translate(en, ru, ua);
    }

    float UiScale() noexcept
    {
        const float scale =
            static_cast<float>(Menu::s_UIScalePercent) / 100.f;
        return ImClamp(scale, 1.f, 2.f);
    }

    void ClampTooltipToMenu(float tipW, float tipH) noexcept
    {
        const float scale = UiScale();
        const float cursorGap = 14.f * scale;
        const float edge = 8.f * scale;
        ImGuiWindow* host = ImGui::FindWindowByName("window");
        ImVec2 tipPos =
            ImGui::GetMousePos() + ImVec2(cursorGap, cursorGap);
        if (host)
        {
            const ImVec2 mn = host->Pos;
            const ImVec2 mx = host->Pos + host->Size;
            if (tipPos.x + tipW > mx.x - edge)
                tipPos.x = mx.x - tipW - edge;
            if (tipPos.x < mn.x + edge)
                tipPos.x = mn.x + edge;
            if (tipPos.y + tipH > mx.y - edge)
                tipPos.y = mx.y - tipH - edge;
            if (tipPos.y < mn.y + edge)
                tipPos.y = mn.y + edge;
        }
        ImGui::SetNextWindowPos(tipPos, ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(0.f, 0.f),
            ImVec2(tipW + 16.f * scale, tipH + 40.f * scale));
    }

    ImVec2 IconDrawPos(
        ImFont* font,
        float fontSize,
        const char* icon,
        const ImVec2& center) noexcept
    {
        if (!font || !icon || !icon[0] || font->FontSize <= 0.01f)
            return ImVec2(IM_ROUND(center.x), IM_ROUND(center.y));
        unsigned int code = 0;
        ImTextCharFromUtf8(&code, icon, nullptr);
        const ImFontGlyph* glyph =
            font->FindGlyphNoFallback(static_cast<ImWchar>(code));
        const float s = fontSize / font->FontSize;
        if (glyph)
        {
            return ImVec2(
                IM_ROUND(center.x - (glyph->X0 + glyph->X1) * 0.5f * s),
                IM_ROUND(center.y - (glyph->Y0 + glyph->Y1) * 0.5f * s));
        }
        const ImVec2 sz = font->CalcTextSizeA(
            fontSize, FLT_MAX, 0.f, icon);
        return ImVec2(
            IM_ROUND(center.x - sz.x * 0.5f),
            IM_ROUND(center.y - sz.y * 0.5f));
    }

    void DrawNavIcon(
        ImDrawList* draw,
        const ImVec2& center,
        const char* icon,
        ImU32 color,
        float fontSize) noexcept
    {
        if (!draw || !icon || !icon[0])
            return;
        ImFont* font = set->icon ? set->icon : ImGui::GetFont();
        if (!font)
            return;
        draw->AddText(
            font,
            fontSize,
            IconDrawPos(font, fontSize, icon, center),
            color,
            icon);
    }

    // Returns the advanced width so callers can butt a hairline against it.
    float DrawTrackedText(
        ImDrawList* draw,
        ImFont* font,
        float fontSize,
        ImVec2 pos,
        ImU32 color,
        const char* text,
        float tracking) noexcept
    {
        if (!draw || !font || !text || !text[0])
            return 0.f;
        const float startX = pos.x;
        const char* s = text;
        while (*s)
        {
            unsigned int c = 0;
            const int n = ImTextCharFromUtf8(&c, s, nullptr);
            (void)c;
            if (n < 1)
                break;
            char buf[8]{};
            memcpy(buf, s, static_cast<size_t>(n));
            draw->AddText(
                font,
                fontSize,
                pos,
                color,
                buf,
                buf + n);
            pos.x += font->CalcTextSizeA(
                fontSize, FLT_MAX, 0.f, buf, buf + n).x + tracking;
            s += n;
        }
        return ImMax(pos.x - tracking - startX, 0.f);
    }

    void RenderBindPopup(FeatureBinds::Binding* binding) noexcept
    {
        if (!binding)
            return;
        if (!gui->begin_popup_w("##bind", 220, -1))
            return;
        FeatureBinds::SetCaptureActive(true);
        ImGui::TextDisabled("%s", binding->label);
        if (gui->keybind("Key##feature_bind", &binding->key))
            FeatureBinds::ResetEdge(binding);
        gui->separator();
        // An action runs once per press, so offering Hold would only promise
        // something the binding cannot do.
        if (!binding->action)
        {
            const char* modes[] = {"Hold", "Toggle"};
            gui->dropdown(
                "Mode##feature_bind",
                &binding->mode,
                modes,
                IM_ARRAYSIZE(modes),
                2);
            gui->separator();
        }
        if (gui->button(
                "Clear bind##feature_bind",
                ImVec2(
                    200.f * UiScale(),
                    MenuTheme::Sizes().controlHeight * UiScale())))
        {
            binding->key = 0;
            binding->previousDown = false;
        }
        gui->end_popup_w();
    }
}

void HelpIcon(
    const char* tipEn,
    const char* tipRu,
    const char* tipUa,
    bool sameLine) noexcept
{
    if (sameLine)
        ImGui::SameLine(0.f, MenuTheme::Sizes().space2 * UiScale());

    ImGui::PushID(tipEn);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float scale = UiScale();
    const float r = 7.f * scale;
    const float h = ImMax(
        ImGui::GetTextLineHeight(),
        r * 2.f);
    const ImVec2 center(p.x + r, p.y + h * 0.5f);
    ImGui::InvisibleButton("##help", ImVec2(r * 2.f + 2.f * scale, h));
    const bool hovered = ImGui::IsItemHovered();
    const ImU32 ring = hovered
        ? gui->get_clr(clr->accent_color)
        : gui->get_clr(clr->text, 0.7f);
    const ImU32 fill = gui->get_clr(clr->element_background);
    dl->AddCircleFilled(center, r, fill, 16);
    dl->AddCircle(center, r, ring, 16, MenuTheme::Sizes().stroke);
    const char* letter = "i";
    const ImVec2 ts = ImGui::CalcTextSize(letter);
    dl->AddText(
        ImVec2(
            center.x - ts.x * 0.5f,
            center.y - ts.y * 0.5f - 1.f * scale),
        ring,
        letter);
    if (hovered)
    {
        const float kTipW = 240.f * scale;
        ClampTooltipToMenu(kTipW, 120.f * scale);
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(kTipW);
        ImGui::TextUnformatted(T(tipEn, tipRu, tipUa));
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::PopID();
}

void LabelWithHelp(
    const char* label,
    const char* tipEn,
    const char* tipRu,
    const char* tipUa) noexcept
{
    const char* end = ImGui::FindRenderedTextEnd(
        label ? label : "");
    ImGui::TextUnformatted(label ? label : "", end);
    HelpIcon(tipEn, tipRu, tipUa, true);
}

bool CheckWithHelp(
    const char* label,
    bool* value,
    const char* tipEn,
    const char* tipRu,
    const char* tipUa,
    bool bindable) noexcept
{
    const ImVec2 rowPos = ImGui::GetCursorScreenPos();
    const float rowW = ImGui::GetContentRegionAvail().x;
    const bool changed = gui->checkbox(
        label,
        value,
        false,
        nullptr,
        nullptr,
        nullptr,
        bindable);
    const ImVec2 after = ImGui::GetCursorScreenPos();

    // Place (i) immediately after the visible label text, not past the row.
    const ImVec2 labelSz = ImGui::CalcTextSize(label, nullptr, true);
    const float iconX = rowPos.x + labelSz.x + 8.f;
    const float iconY = rowPos.y + 4.f;
    const float maxX = rowPos.x + rowW - 28.f;
    ImGui::SetCursorScreenPos(
        ImVec2(iconX < maxX ? iconX : maxX, iconY));
    HelpIcon(tipEn, tipRu, tipUa, false);
    ImGui::SetCursorScreenPos(after);
    return changed;
}

void SliderWithHelp(
    const char* label,
    float* value,
    float mn,
    float mx,
    const char* fmt,
    const char* tipEn,
    const char* tipRu,
    const char* tipUa) noexcept
{
    LabelWithHelp(label, tipEn, tipRu, tipUa);
    char id[96]{};
    _snprintf(id, sizeof(id) - 1, "##%s", label);
    gui->slider_float(id, value, mn, mx, fmt);
}

void SectionTitle(const char* label) noexcept
{
    // Subgroup captions share the section caption style so a page never has
    // two competing ways of naming a group.
    InspectorSection(label);
}

bool NavRailItem(
    const char* id,
    const char* icon,
    const char* label,
    bool selected,
    const ImVec2& size,
    float railExpand,
    float iconCenterX) noexcept
{
    ImGui::PushID(id ? id : "nav");
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool clicked =
        ImGui::InvisibleButton("##item", size);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max = min + size;
    const float scale = UiScale();
    const float open = ImClamp(railExpand, 0.f, 1.f);

    struct NavAnim final
    {
        float selected = 0.f;
        float hover = 0.f;
    };
    NavAnim& anim =
        LeyawinAnim<NavAnim, 7201>().get(ImGui::GetItemID());
    anim.selected = MenuMotion::Approach(
        anim.selected, selected ? 1.f : 0.f, MenuMotion::kSelection);
    anim.hover = MenuMotion::Approach(
        anim.hover, hovered ? 1.f : 0.f, MenuMotion::kHover);

    // Row chrome only exists once the whole drawer is open.
    // Collapsed rail is icon-only — selected = accent glyph, no tile.
    const float chrome = ImClamp((open - 0.18f) / 0.45f, 0.f, 1.f);
    const float fill = ImMax(anim.selected, anim.hover * 0.72f);
    if (chrome > 0.01f && fill > 0.01f)
    {
        draw->AddRectFilled(
            min,
            max,
            gui->get_clr(
                selected
                    ? clr->selection_active
                    : clr->element_background,
                (selected ? 0.92f : 0.55f) * fill * chrome),
            8.f * scale);
    }

    const float iconBox = size.y;
    const float centerX =
        iconCenterX > 1.f ? iconCenterX : min.x + iconBox * 0.5f;
    const ImVec2 iconCenter(
        IM_ROUND(centerX),
        IM_ROUND((min.y + max.y) * 0.5f));
    const ImU32 iconColor = selected
        ? gui->get_clr(clr->accent_color)
        : gui->get_clr(clr->text_active, hovered ? 1.f : 0.78f);
    DrawNavIcon(
        draw,
        iconCenter,
        icon && icon[0] ? icon : "?",
        iconColor,
        iconBox * 0.48f);

    if (open > 0.02f && label && label[0])
    {
        ImFont* labelFont =
            set->poppins_selection
                ? set->poppins_selection
                : ImGui::GetFont();
        const float labelSize = ImGui::GetFontSize() * 1.14f;
        const ImVec2 textSize = labelFont
            ? labelFont->CalcTextSizeA(
                  labelSize, FLT_MAX, 0.f, label)
            : ImVec2(0.f, 0.f);
        const ImVec2 labelPos(
            min.x + iconBox + 10.f * scale,
            iconCenter.y - textSize.y * 0.5f);
        draw->PushClipRect(min, max, true);
        const ImU32 labelCol = selected || hovered
            ? gui->get_clr(clr->text_active, open)
            : gui->get_clr(clr->text_active, 0.82f * open);
        draw->AddText(
            labelFont,
            labelSize,
            labelPos,
            labelCol,
            label);
        draw->PopClipRect();
    }

    ImGui::PopID();
    return clicked;
}

void NavRailGroupLabel(
    const char* label,
    const ImVec2& size,
    float railExpand) noexcept
{
    const float open = ImClamp(railExpand, 0.f, 1.f);
    if (open < 0.05f || !label || !label[0] || size.y < 1.f)
        return;

    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max = min + size;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* font = set->poppins_selection
        ? set->poppins_selection
        : ImGui::GetFont();
    const float scale = UiScale();
    const float fontSize = ImGui::GetFontSize() * 0.84f;
    const float tracking = 1.15f * scale;
    const ImU32 col = gui->get_clr(clr->text_active, 0.70f * open);

    ImVec2 pos(
        min.x + 2.f * scale,
        max.y - fontSize - 2.f * scale);
    draw->PushClipRect(min, max, true);
    const char* s = label;
    while (*s)
    {
        unsigned int c = 0;
        const int n = ImTextCharFromUtf8(&c, s, nullptr);
        (void)c;
        if (n < 1)
            break;
        char buf[8]{};
        memcpy(buf, s, static_cast<size_t>(n));
        draw->AddText(
            font,
            fontSize,
            pos,
            col,
            buf,
            buf + n);
        pos.x += font->CalcTextSizeA(
            fontSize, FLT_MAX, 0.f, buf, buf + n).x + tracking;
        s += n;
    }
    draw->PopClipRect();
}

void NavRailAccount(
    const ImVec2& avatarMin,
    float avatarSize,
    const char* name,
    const char* subscription,
    float railExpand) noexcept
{
    if (avatarSize < 8.f)
        return;

    const float open = ImClamp(railExpand, 0.f, 1.f);
    const float scale = UiScale();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 center(
        avatarMin.x + avatarSize * 0.5f,
        avatarMin.y + avatarSize * 0.5f);
    const float radius = avatarSize * 0.5f;

    draw->AddCircleFilled(
        center,
        radius,
        gui->get_clr(clr->selection_active, 0.92f));
    draw->AddCircle(
        center,
        radius,
        gui->get_clr(clr->accent_color, 0.38f),
        0,
        1.2f * scale);

    char initials[12]{};
    int taken = 0;
    bool take = true;
    const char* src = name && name[0] ? name : "U";
    while (*src && taken < 2)
    {
        unsigned int c = 0;
        const int n = ImTextCharFromUtf8(&c, src, nullptr);
        (void)c;
        if (n < 1)
            break;
        if (*src == ' ' || *src == '\t')
        {
            take = true;
            src += n;
            continue;
        }
        if (take)
        {
            const int used = static_cast<int>(strlen(initials));
            if (used + n < static_cast<int>(sizeof(initials)) - 1)
            {
                memcpy(initials + used, src, static_cast<size_t>(n));
                initials[used + n] = 0;
                ++taken;
            }
            take = false;
        }
        src += n;
    }
    if (!initials[0])
    {
        initials[0] = 'U';
        initials[1] = 0;
    }

    ImFont* initFont = set->poppins_selection
        ? set->poppins_selection
        : ImGui::GetFont();
    const float initSize = avatarSize * 0.38f;
    const ImVec2 initSz = initFont->CalcTextSizeA(
        initSize, FLT_MAX, 0.f, initials);
    draw->AddText(
        initFont,
        initSize,
        ImVec2(
            center.x - initSz.x * 0.5f,
            center.y - initSz.y * 0.5f),
        gui->get_clr(clr->accent_color),
        initials);

    if (open < 0.04f)
        return;

    const char* nick = name && name[0] ? name : "User";
    const char* plan =
        subscription && subscription[0] ? subscription : "";
    ImFont* nameFont = initFont;
    ImFont* planFont = set->poppins_widget
        ? set->poppins_widget
        : nameFont;
    const float nameSize = ImGui::GetFontSize() * 1.28f;
    const float planSize = ImGui::GetFontSize() * 0.84f;
    const float textX = avatarMin.x + avatarSize + 10.f * scale;
    const float lineGap = 2.f * scale;
    const float blockH = plan[0]
        ? nameSize + lineGap + planSize
        : nameSize;
    const float textY =
        avatarMin.y + (avatarSize - blockH) * 0.5f;
    const ImVec2 win = ImGui::GetWindowPos();
    const ImVec2 winSize = ImGui::GetWindowSize();
    draw->PushClipRect(
        ImVec2(avatarMin.x, ImMin(avatarMin.y, textY)),
        ImVec2(
            win.x + winSize.x - 8.f * scale,
            ImMax(avatarMin.y + avatarSize, textY + blockH)),
        true);
    draw->AddText(
        nameFont,
        nameSize,
        ImVec2(textX, textY),
        gui->get_clr(clr->text_active, open),
        nick);
    if (plan[0])
    {
        draw->AddText(
            planFont,
            planSize,
            ImVec2(textX, textY + nameSize + lineGap),
            gui->get_clr(clr->text_active, 0.38f * open),
            plan);
    }
    draw->PopClipRect();
}

bool NavTab(
    const char* id,
    const char* icon,
    const char* label,
    bool selected,
    bool primary,
    const ImVec2& size) noexcept
{
    ImGui::PushID(id ? id : "nav_tab");
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##segment", size);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max = min + size;
    const float scale = UiScale();
    const MenuTheme::Palette& tokens = MenuTheme::Colors();
    const MenuTheme::Metrics& metrics = MenuTheme::Sizes();

    struct TabAnim final
    {
        float on = 0.f;
        float hot = 0.f;
    };
    TabAnim& anim =
        LeyawinAnim<TabAnim, 7221>().get(ImGui::GetItemID());
    anim.on = MenuMotion::Approach(
        anim.on, selected ? 1.f : 0.f, MenuMotion::kSelection);
    anim.hot = MenuMotion::Approach(
        anim.hot, hovered ? 1.f : 0.f, MenuMotion::kHover);

    const float radius = metrics.radiusControl * scale;
    if (anim.hot > 0.01f && anim.on < 0.99f)
    {
        draw->AddRectFilled(
            min,
            max,
            gui->get_clr(tokens.hover, anim.hot * (1.f - anim.on)),
            radius);
    }
    if (anim.on > 0.01f)
    {
        draw->AddRectFilled(
            min,
            max,
            gui->get_clr(
                primary ? tokens.accent.fill : tokens.raised,
                anim.on),
            radius);
    }

    ImFont* font = set->poppins_widget
        ? set->poppins_widget
        : ImGui::GetFont();
    const float fontSize = MenuType::BodySize();
    const float iconSz = (primary ? 14.f : 13.f) * scale;
    const float gap = 7.f * scale;
    ImFont* iconFont = set->icon ? set->icon : font;
    const bool hasIcon = icon && icon[0];
    const ImVec2 labelSz =
        font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, label ? label : "");
    const float iconW = hasIcon
        ? iconFont->CalcTextSizeA(iconSz, FLT_MAX, 0.f, icon).x
        : 0.f;
    const float blockW = labelSz.x + (hasIcon ? iconW + gap : 0.f);
    const float centerY = (min.y + max.y) * 0.5f;
    float x = min.x + ImMax(6.f * scale, (size.x - blockW) * 0.5f);

    const ImU32 textCol = gui->get_clr(
        ImLerp(
            ImLerp(tokens.textMuted, tokens.textPrimary, anim.hot),
            primary ? tokens.onAccent : tokens.textPrimary,
            anim.on),
        1.f);
    // A child segment marks itself with an accent glyph; the category row
    // already carries the accent as a filled pill.
    const ImU32 iconCol = (!primary && anim.on > 0.5f)
        ? gui->get_clr(tokens.accent.active, 1.f)
        : textCol;

    draw->PushClipRect(min, max, true);
    if (hasIcon)
    {
        DrawNavIcon(
            draw,
            ImVec2(IM_ROUND(x + iconW * 0.5f), IM_ROUND(centerY)),
            icon,
            iconCol,
            iconSz);
        x += iconW + gap;
    }
    draw->AddText(
        font,
        fontSize,
        ImVec2(x, centerY - labelSz.y * 0.5f),
        textCol,
        label ? label : "");
    draw->PopClipRect();

    ImGui::PopID();
    return clicked;
}

void DrawWorkspaceFrost(
    const ImVec2& min,
    const ImVec2& max,
    float amount) noexcept
{
    const float a = ImClamp(amount, 0.f, 1.f);
    if (a <= 0.01f || max.x <= min.x || max.y <= min.y)
        return;

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    if (!draw)
        return;

    const float scale = UiScale();
    ImGuiWindow* host = ImGui::FindWindowByName("window");
    if (host)
        draw->PushClipRect(min, max, true);

    draw->AddRectFilled(
        min,
        max,
        gui->get_clr(clr->window_background, 0.46f * a));
    draw->AddRectFilled(
        min,
        max,
        gui->get_clr(clr->child_top_bar, 0.30f * a));

    const float inset = 14.f * scale;
    for (int i = 0; i < 4; ++i)
    {
        const float t = static_cast<float>(i + 1) / 5.f;
        const ImVec2 p0 = min + ImVec2(inset * t, inset * t);
        const ImVec2 p1 = max - ImVec2(inset * t, inset * t);
        if (p1.x <= p0.x || p1.y <= p0.y)
            break;
        draw->AddRectFilled(
            p0,
            p1,
            gui->get_clr(clr->window_background, 0.07f * a),
            14.f * scale);
    }

    gui->add_rect_filled_multi_color(
        draw,
        min,
        ImVec2(min.x + 28.f * scale, max.y),
        gui->get_clr(clr->window_background, 0.22f * a),
        gui->get_clr(clr->window_background, 0.f),
        gui->get_clr(clr->window_background, 0.f),
        gui->get_clr(clr->window_background, 0.22f * a));
    draw->AddRectFilled(
        min,
        max,
        gui->get_clr(clr->accent_color, 0.035f * a));

    if (host)
        draw->PopClipRect();
}

bool PageChip(
    const char* id,
    const char* label,
    bool selected,
    const ImVec2& size) noexcept
{
    ImGui::PushID(id ? id : "chip");
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool clicked =
        ImGui::InvisibleButton("##chip", size);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    struct ChipAnim final
    {
        float selected = 0.f;
        float hovered = 0.f;
    };
    ChipAnim& anim =
        LeyawinAnim<ChipAnim, 7202>().get(ImGui::GetItemID());
    anim.selected = MenuMotion::Approach(
        anim.selected, selected ? 1.f : 0.f, MenuMotion::kSelection);
    anim.hovered = MenuMotion::Approach(
        anim.hovered, hovered ? 1.f : 0.f, MenuMotion::kHover);

    const ImU32 left = gui->get_clr(
        selected ? clr->selection_active : clr->child_bottom_bar,
        0.82f + anim.hovered * 0.12f);
    const ImU32 right = gui->get_clr(
        selected ? clr->element_background : clr->combo_color,
        0.68f + anim.hovered * 0.18f);
    draw->AddRectFilled(
        min,
        min + size,
        right,
        8.f * UiScale());
    draw->AddRectFilledMultiColor(
        min + ImVec2(1.f, 1.f),
        min + size - ImVec2(1.f, 1.f),
        left,
        right,
        right,
        left);
    draw->AddRect(
        min,
        min + size,
        gui->get_clr(
            selected
                ? clr->accent_color
                : clr->child_stroke_bar,
            selected ? 0.62f : (0.72f + anim.hovered * 0.28f)),
        8.f * UiScale(),
        0,
        1.f);
    if (anim.selected > 0.01f)
    {
        const float lineW = ImMin(size.x * 0.42f, 42.f * UiScale());
        draw->AddRectFilled(
            ImVec2(
                min.x + (size.x - lineW) * 0.5f,
                min.y + size.y - 2.f * UiScale()),
            ImVec2(
                min.x + (size.x + lineW) * 0.5f,
                min.y + size.y),
            gui->get_clr(clr->accent_color, anim.selected),
            2.f);
    }
    const ImVec2 textSize =
        ImGui::CalcTextSize(label ? label : "");
    draw->AddText(
        ImVec2(
            min.x + (size.x - textSize.x) * 0.5f,
            min.y + (size.y - textSize.y) * 0.5f),
        gui->get_clr(
            selected || hovered
                ? clr->text_active
                : clr->text),
        label ? label : "");
    ImGui::PopID();
    return clicked;
}

bool ChipStrip(
    const char* id,
    const ChipDesc* chips,
    int count,
    int* selected,
    float minimumWidth,
    float height) noexcept
{
    if (!chips || count <= 0 || !selected)
        return false;

    ImGui::PushID(id ? id : "chip_strip");
    ImGui::BeginGroup();
    const float scale = UiScale();
    const float gap = 7.f * scale;
    const float available = ImMax(ImGui::GetContentRegionAvail().x, 1.f);
    float desired = minimumWidth * scale;
    for (int i = 0; i < count; ++i)
    {
        const float textW =
            ImGui::CalcTextSize(chips[i].label ? chips[i].label : "").x;
        desired = ImMax(desired, textW + 26.f * scale);
    }

    int columns = static_cast<int>(
        floorf((available + gap) / (desired + gap)));
    columns = ImClamp(columns, 1, count);
    const float width =
        ImMax(
            64.f * scale,
            (available - gap * static_cast<float>(columns - 1)) /
                static_cast<float>(columns));
    const float chipH = height * scale;

    bool changed = false;
    for (int i = 0; i < count; ++i)
    {
        if (i > 0 && (i % columns) != 0)
            ImGui::SameLine(0.f, gap);
        if (PageChip(
                chips[i].id,
                chips[i].label,
                *selected == i,
                ImVec2(width, chipH)))
        {
            *selected = i;
            changed = true;
        }
    }
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

bool IconTabStrip(
    const char* id,
    const IconTabDesc* tabs,
    int count,
    int* selected) noexcept
{
    if (!tabs || count <= 0 || !selected)
        return false;

    ImGui::PushID(id ? id : "segmented_tabs");
    const MenuTheme::Palette& tokens = MenuTheme::Colors();
    const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
    const float scale = UiScale();
    const float tabH = metrics.tabHeight * scale;
    const float padX = 12.f * scale;
    const float iconSz = metrics.iconRow * scale;
    const float iconGap = 7.f * scale;
    const float available = ImMax(ImGui::GetContentRegionAvail().x, 1.f);

    float widths[16]{};
    const int n = ImMin(count, 16);
    float total = 0.f;
    for (int i = 0; i < n; ++i)
    {
        const float textW = ImGui::CalcTextSize(
            tabs[i].label ? tabs[i].label : "").x;
        widths[i] = padX * 2.f + iconSz + iconGap + textW;
        total += widths[i];
    }

    bool iconOnly = total > available;
    if (iconOnly)
    {
        total = 0.f;
        const float iconTab = padX * 2.f + iconSz;
        for (int i = 0; i < n; ++i)
        {
            widths[i] = iconTab;
            total += widths[i];
        }
    }

    // Centre the group inside the available width, otherwise the rule below
    // it runs far past the last tab on one side only and the whole strip
    // reads as if it were nudged sideways.
    const ImVec2 contentMin = ImGui::GetCursorScreenPos();
    const float railLeft = contentMin.x;
    const float railRight = contentMin.x + available;
    const ImVec2 trackMin(
        railLeft + ImMax(0.f, (available - total) * 0.5f),
        contentMin.y);
    ImGui::SetCursorScreenPos(trackMin);
    const ImVec2 trackMax = trackMin + ImVec2(total, tabH);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Underline rail instead of a filled pill: the selection reads as a
    // tab strip, not as another card stacked on the page.
    draw->AddLine(
        ImVec2(railLeft, trackMax.y),
        ImVec2(railRight, trackMax.y),
        gui->get_clr(tokens.borderSubtle, 1.f),
        metrics.stroke);

    ImVec2 tabMin[16]{};
    bool hovered[16]{};
    bool changed = false;
    const int sel = ImClamp(*selected, 0, n - 1);
    for (int i = 0; i < n; ++i)
    {
        if (i > 0)
            ImGui::SameLine(0.f, 0.f);
        ImGui::PushID(tabs[i].id ? tabs[i].id : "tab");
        const bool clicked =
            ImGui::InvisibleButton("##tab", ImVec2(widths[i], tabH));
        tabMin[i] = ImGui::GetItemRectMin();
        hovered[i] = ImGui::IsItemHovered();
        if (clicked)
        {
            *selected = i;
            changed = true;
            ImGui::MarkItemEdited(ImGui::GetItemID());
        }
        ImGui::PopID();
    }

    struct SlideAnim final
    {
        float off = 0.f;
        float w = 0.f;
        float inited = 0.f;
    };
    SlideAnim& slide =
        LeyawinAnim<SlideAnim, 7210>().get(ImGui::GetID("##slide"));
    const float targetOff = tabMin[sel].x - trackMin.x;
    const float targetW = widths[sel];
    if (slide.inited < 0.5f)
    {
        slide.off = targetOff;
        slide.w = targetW;
        slide.inited = 1.f;
    }
    else
    {
        slide.off = MenuMotion::Approach(
            slide.off, targetOff, MenuMotion::kSelection);
        slide.w = MenuMotion::Approach(
            slide.w, targetW, MenuMotion::kSelection);
    }

    const float underlineH = 2.f * scale;
    draw->AddRectFilled(
        ImVec2(
            trackMin.x + slide.off + padX * 0.5f,
            trackMax.y - underlineH),
        ImVec2(
            trackMin.x + slide.off + slide.w - padX * 0.5f,
            trackMax.y),
        gui->get_clr(tokens.accent.fill, 1.f),
        underlineH * 0.5f);

    for (int i = 0; i < n; ++i)
    {
        const bool isSel = (sel == i);
        struct TabAnim final
        {
            float selected = 0.f;
            float hovered = 0.f;
        };
        ImGui::PushID(tabs[i].id ? tabs[i].id : "tab");
        TabAnim& anim =
            LeyawinAnim<TabAnim, 7212>().get(ImGui::GetID("##tab"));
        ImGui::PopID();
        anim.selected = MenuMotion::Approach(
            anim.selected, isSel ? 1.f : 0.f, MenuMotion::kSelection);
        anim.hovered = MenuMotion::Approach(
            anim.hovered, hovered[i] ? 1.f : 0.f, MenuMotion::kHover);

        const ImVec2 textSize = ImGui::CalcTextSize(
            tabs[i].label ? tabs[i].label : "");
        float contentW = iconSz;
        if (!iconOnly)
            contentW += iconGap + textSize.x;
        const float cx = tabMin[i].x + (widths[i] - contentW) * 0.5f;
        const float cy = tabMin[i].y + tabH * 0.5f;
        const float mix = ImMax(anim.selected, anim.hovered * 0.6f);
        DrawNavIcon(
            draw,
            ImVec2(cx + iconSz * 0.5f, cy),
            tabs[i].icon,
            gui->get_clr(
                ImLerp(tokens.textFaint, tokens.accent.active, mix),
                1.f),
            iconSz);
        if (!iconOnly && tabs[i].label && tabs[i].label[0])
        {
            draw->AddText(
                ImVec2(
                    cx + iconSz + iconGap,
                    cy - textSize.y * 0.5f),
                gui->get_clr(
                    ImLerp(tokens.textMuted, tokens.textPrimary, mix),
                    1.f),
                tabs[i].label);
        }
        else if (iconOnly && hovered[i] && tabs[i].label && tabs[i].label[0])
        {
            ImGui::SetTooltip("%s", tabs[i].label);
        }
    }

    ImGui::PopID();
    ImGui::NewLine();
    ImGui::Dummy(ImVec2(0.f, metrics.space2 * scale));
    return changed;
}

bool SubNavTabs(
    const char* id,
    const IconTabDesc* tabs,
    int count,
    int* selected,
    float height) noexcept
{
    if (!tabs || count <= 0 || !selected)
        return false;

    const MenuTheme::Palette& tokens = MenuTheme::Colors();
    const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
    const float scale = UiScale();
    const float stripH = height * scale;
    const float inner = 4.f * scale;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImMax(ImGui::GetContentRegionAvail().x, 1.f);
    const ImVec2 max = min + ImVec2(width, stripH);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(
        min,
        max,
        gui->get_clr(tokens.base, 0.92f),
        metrics.radiusPanel * scale);
    draw->AddRect(
        min,
        max,
        gui->get_clr(tokens.borderSubtle, 1.f),
        metrics.radiusPanel * scale,
        0,
        metrics.stroke);

    const int n = ImMin(count, 24);
    const int sel = ImClamp(*selected, 0, n - 1);
    const float segW = (width - inner * 2.f) / static_cast<float>(n);
    const float segH = stripH - inner * 2.f;
    bool changed = false;

    ImGui::PushID(id ? id : "sub_nav_tabs");
    for (int i = 0; i < n; ++i)
    {
        ImGui::SetCursorScreenPos(
            ImVec2(
                min.x + inner + segW * static_cast<float>(i),
                min.y + inner));
        if (NavTab(
                tabs[i].id,
                tabs[i].icon,
                tabs[i].label,
                sel == i,
                false,
                ImVec2(segW, segH)))
        {
            *selected = i;
            changed = true;
        }
    }
    ImGui::PopID();

    ImGui::SetCursorScreenPos(min);
    ImGui::Dummy(ImVec2(width, stripH));
    return changed;
}

bool ChipTabs(
    const char* id,
    const IconTabDesc* tabs,
    int count,
    int* selected) noexcept
{
    if (!tabs || count <= 0 || !selected)
        return false;

    ImGui::PushID(id ? id : "chip_tabs");
    const MenuTheme::Palette& tokens = MenuTheme::Colors();
    const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
    const float scale = UiScale();
    const float chipH = 24.f * scale;
    const float padX = metrics.space3 * scale;
    const float iconSz = 12.f * scale;
    const float iconGap = 6.f * scale;
    const float gap = metrics.space1 * scale;
    const float avail = ImMax(ImGui::GetContentRegionAvail().x, 1.f);
    const float startX = ImGui::GetCursorPosX();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const int n = ImMin(count, 24);
    const int sel = ImClamp(*selected, 0, n - 1);
    bool changed = false;
    float lineX = 0.f;

    for (int i = 0; i < n; ++i)
    {
        const char* label = tabs[i].label ? tabs[i].label : "";
        const bool hasIcon = tabs[i].icon && tabs[i].icon[0];
        const float textW = ImGui::CalcTextSize(label).x;
        float chipW = padX * 2.f + textW;
        if (hasIcon)
            chipW += iconSz + iconGap;

        if (i > 0)
        {
            if (lineX + gap + chipW > avail)
            {
                ImGui::SetCursorPosX(startX);
                lineX = 0.f;
            }
            else
            {
                ImGui::SameLine(0.f, gap);
                lineX += gap;
            }
        }
        lineX += chipW;

        ImGui::PushID(tabs[i].id ? tabs[i].id : "chip");
        const bool clicked = ImGui::InvisibleButton("##chip", ImVec2(chipW, chipH));
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const bool hovered = ImGui::IsItemHovered();
        if (clicked)
        {
            *selected = i;
            changed = true;
            ImGui::MarkItemEdited(ImGui::GetItemID());
        }

        struct ChipAnim final
        {
            float on = 0.f;
            float hot = 0.f;
        };
        ChipAnim& anim = LeyawinAnim<ChipAnim, 7214>().get(ImGui::GetItemID());
        anim.on = MenuMotion::Approach(
            anim.on, (sel == i) ? 1.f : 0.f, MenuMotion::kSelection);
        anim.hot = MenuMotion::Approach(
            anim.hot, hovered ? 1.f : 0.f, MenuMotion::kHover);
        ImGui::PopID();

        const float radius = chipH * 0.5f;
        if (anim.hot > 0.01f && anim.on < 0.99f)
        {
            draw->AddRectFilled(
                min, max, gui->get_clr(tokens.hover, anim.hot), radius);
        }
        if (anim.on > 0.01f)
        {
            draw->AddRectFilled(
                min,
                max,
                gui->get_clr(tokens.accent.fill, anim.on * 0.16f),
                radius);
            draw->AddRect(
                min,
                max,
                gui->get_clr(tokens.accent.fill, anim.on * 0.75f),
                radius,
                0,
                metrics.stroke);
        }

        const float mix = ImMax(anim.on, anim.hot * 0.6f);
        float cx = min.x + padX;
        const float cy = (min.y + max.y) * 0.5f;
        if (hasIcon)
        {
            DrawNavIcon(
                draw,
                ImVec2(cx + iconSz * 0.5f, cy),
                tabs[i].icon,
                gui->get_clr(
                    ImLerp(tokens.textFaint, tokens.accent.active, mix), 1.f),
                iconSz);
            cx += iconSz + iconGap;
        }
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        draw->AddText(
            ImVec2(cx, cy - textSize.y * 0.5f),
            gui->get_clr(ImLerp(tokens.textMuted, tokens.textPrimary, mix), 1.f),
            label);
    }

    ImGui::PopID();
    ImGui::Dummy(ImVec2(0.f, metrics.space1 * scale));
    return changed;
}

namespace
{
    struct PageColumns final
    {
        int count = 1;
        int index = 0;
        float width = 0.f;
        float height = 0.f;
        float gap = 0.f;
    };

    PageColumns g_PageColumns{};

    void BeginColumnChild(int index) noexcept
    {
        char name[24]{};
        ImFormatString(name, IM_ARRAYSIZE(name), "##page_col_%d", index);
        ImGui::BeginChild(
            name,
            ImVec2(g_PageColumns.width, g_PageColumns.height),
            ImGuiChildFlags_None,
            ImGuiWindowFlags_NoSavedSettings);
    }
}

int BeginPageColumns(
    const char* id,
    int desired,
    float minColumnW) noexcept
{
    const float scale = UiScale();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float gap = MenuTheme::Sizes().space6 * scale;
    const float width = ImMax(avail.x, 1.f);

    int count = ImClamp(desired, 1, 3);
    while (count > 1 &&
           (width - gap * static_cast<float>(count - 1)) /
               static_cast<float>(count) < minColumnW * scale)
    {
        --count;
    }

    g_PageColumns.count = count;
    g_PageColumns.index = 0;
    g_PageColumns.gap = gap;
    g_PageColumns.height = ImMax(avail.y, 1.f);
    g_PageColumns.width =
        (width - gap * static_cast<float>(count - 1)) /
        static_cast<float>(count);

    ImGui::PushID(id ? id : "page_columns");
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
    BeginColumnChild(0);
    return count;
}

void NextPageColumn() noexcept
{
    if (g_PageColumns.count <= 1)
    {
        // Narrow pane: keep stacking in the one column we have.
        ImGui::Dummy(
            ImVec2(0.f, MenuTheme::Sizes().space4 * UiScale()));
        return;
    }
    if (g_PageColumns.index + 1 >= g_PageColumns.count)
        return;

    ImGui::EndChild();
    ++g_PageColumns.index;
    ImGui::SameLine(0.f, g_PageColumns.gap);
    BeginColumnChild(g_PageColumns.index);
}

void EndPageColumns() noexcept
{
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    g_PageColumns.count = 1;
    g_PageColumns.index = 0;
}

void InspectorSection(const char* label) noexcept
{
    const MenuTheme::Palette& tokens = MenuTheme::Colors();
    const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
    const float scale = UiScale();
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImGui::Dummy(ImVec2(0.f, metrics.space4 * scale));
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float width = ImMax(ImGui::GetContentRegionAvail().x, 1.f);
    ImFont* font = MenuType::Label();
    const float fontSize = MenuType::LabelSize();
    // Caption only: the rule that used to run from the caption to the right
    // edge cut every card into visible strips.
    (void)width;
    DrawTrackedText(
        draw,
        font,
        fontSize,
        min,
        gui->get_clr(tokens.textFaint, 1.f),
        label ? label : "",
        1.1f * scale);
    ImGui::Dummy(ImVec2(0.f, fontSize + metrics.space2 * scale));
}

void BeginInspectorIsland(
    const char* id,
    const char* label,
    float topGap,
    const char* subtitle,
    bool titleInside) noexcept
{
    const MenuTheme::Palette& tokens = MenuTheme::Colors();
    const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
    const float scale = UiScale();
    const bool firstBlock =
        ImGui::GetCursorPosY() <= ImGui::GetCursorStartPos().y + 0.5f;

    // A negative gap means the caller already placed the island, which is how
    // the pages that sit right under a sub-tab strip keep their own spacing.
    if (topGap < 0.f)
    {
    }
    else if (!firstBlock)
        ImGui::Dummy(ImVec2(0.f, ImMax(topGap, metrics.space3) * scale));
    else if (topGap > 0.f)
        ImGui::Dummy(ImVec2(0.f, topGap * scale));

    // Label above card: small muted text (default style for most pages).
    if (!titleInside && label && label[0])
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImFont* labelFont = MenuType::Micro();
        const float labelSize = MenuType::MicroSize();
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        // Indent to match the card's left window padding so it looks symmetric.
        const ImVec2 labelPos(
            cursor.x + metrics.space4 * scale,
            cursor.y);
        draw->AddText(
            labelFont,
            labelSize,
            labelPos,
            gui->get_clr(tokens.textFaint, 1.f),
            label);
        const ImVec2 labelSz = labelFont->CalcTextSizeA(
            labelSize, FLT_MAX, 0.f, label);
        ImGui::Dummy(ImVec2(0.f, labelSz.y + 3.f * scale));
    }

    ImGui::PushID(id ? id : "##section");
    ImGui::PushStyleVar(
        ImGuiStyleVar_ChildRounding, metrics.radiusPanel * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, metrics.stroke);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(metrics.space4, metrics.space4) * scale);
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(0.f, metrics.space2 * scale));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, tokens.base);
    ImGui::PushStyleColor(ImGuiCol_Border, tokens.borderSubtle);
    ImGui::BeginChild(
        id ? id : "##island",
        ImVec2(0.f, 0.f),
        ImGuiChildFlags_Borders |
            ImGuiChildFlags_AutoResizeY |
            ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar);

    // Title inside card: white bold (Settings style, titleInside=true).
    if (titleInside && label && label[0])
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImFont* titleFont = MenuType::Title();
        const float titleSize = MenuType::TitleSize();
        const ImVec2 titlePos = ImGui::GetCursorScreenPos();
        const ImVec2 titleSz = titleFont->CalcTextSizeA(
            titleSize, FLT_MAX, 0.f, label);
        draw->AddText(
            titleFont,
            titleSize,
            titlePos,
            gui->get_clr(tokens.textPrimary, 1.f),
            label);
        float blockH = titleSz.y;
        if (subtitle && subtitle[0])
        {
            ImFont* subFont = MenuType::Micro();
            const float subSize = MenuType::MicroSize();
            const ImVec2 subPos(
                titlePos.x,
                titlePos.y + titleSz.y + 2.f * scale);
            draw->AddText(
                subFont,
                subSize,
                subPos,
                gui->get_clr(tokens.textMuted, 1.f),
                subtitle);
            const ImVec2 subSz = subFont->CalcTextSizeA(
                subSize, FLT_MAX, 0.f, subtitle);
            blockH += 2.f * scale + subSz.y;
        }
        ImGui::Dummy(ImVec2(0.f, blockH + metrics.space2 * scale));
    }
}

void EndInspectorIsland() noexcept
{
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
    ImGui::PopID();
}

void DrawLanguageFlag(
    ImDrawList* draw,
    const ImVec2& min,
    const ImVec2& max,
    int language,
    float alpha) noexcept
{
    if (!draw)
        return;
    const float a = ImClamp(alpha, 0.f, 1.f);
    if (a <= 0.01f)
        return;
    const auto col = [a](int r, int g, int b, int baseA = 255) noexcept {
        return IM_COL32(r, g, b, (int)(baseA * a + 0.5f));
    };
    const float r = 2.f;
    const float w = max.x - min.x;
    const float h = max.y - min.y;
    if (w < 4.f || h < 4.f)
        return;
    draw->PushClipRect(min, max, true);
    switch (language)
    {
    case 1:
    {
        const float bh = h / 3.f;
        draw->AddRectFilled(
            min,
            ImVec2(max.x, min.y + bh),
            col(255, 255, 255),
            r,
            ImDrawFlags_RoundCornersTop);
        draw->AddRectFilled(
            ImVec2(min.x, min.y + bh),
            ImVec2(max.x, min.y + bh * 2.f),
            col(0, 57, 166));
        draw->AddRectFilled(
            ImVec2(min.x, min.y + bh * 2.f),
            max,
            col(213, 43, 30),
            r,
            ImDrawFlags_RoundCornersBottom);
        break;
    }
    case 2:
    {
        const float bh = h * 0.5f;
        draw->AddRectFilled(
            min,
            ImVec2(max.x, min.y + bh),
            col(0, 91, 187),
            r,
            ImDrawFlags_RoundCornersTop);
        draw->AddRectFilled(
            ImVec2(min.x, min.y + bh),
            max,
            col(255, 213, 0),
            r,
            ImDrawFlags_RoundCornersBottom);
        break;
    }
    default:
    {
        const int stripes = 7;
        const float sh = h / static_cast<float>(stripes);
        for (int i = 0; i < stripes; ++i)
        {
            const ImU32 stripe = (i % 2 == 0)
                ? col(179, 25, 66)
                : col(255, 255, 255);
            ImDrawFlags corners = 0;
            if (i == 0)
                corners = ImDrawFlags_RoundCornersTop;
            if (i == stripes - 1)
                corners = ImDrawFlags_RoundCornersBottom;
            draw->AddRectFilled(
                ImVec2(min.x, min.y + sh * static_cast<float>(i)),
                ImVec2(max.x, min.y + sh * static_cast<float>(i + 1)),
                stripe,
                r,
                corners);
        }
        draw->AddRectFilled(
            min,
            ImVec2(min.x + w * 0.40f, min.y + sh * 4.f),
            col(10, 49, 97),
            r,
            ImDrawFlags_RoundCornersTopLeft);
        break;
    }
    }
    draw->PopClipRect();
    draw->AddRect(min, max, col(255, 255, 255, 55), r, 0, 1.f);
}

FeatureRowResult FeatureRow(
    const char* id,
    const FeatureRowDesc& desc) noexcept
{
    FeatureRowResult result{};
    if (!id || !id[0])
        return result;

    const MenuTheme::Palette& tokens = MenuTheme::Colors();
    const MenuTheme::Metrics& metrics = MenuTheme::Sizes();

    ImGui::PushID(id);
    const float scale = UiScale();
    const float rowH = metrics.rowHeight * scale;
    const float width = ImMax(ImGui::GetContentRegionAvail().x, 1.f);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max = min + ImVec2(width, rowH);

    const float pad = metrics.space2 * scale;
    const float toggleW = 36.f * scale;
    const float toggleH = 20.f * scale;
    const float overflowS = 20.f * scale;
    const float swatchD = 16.f * scale;
    const float gap = metrics.space2 * scale;
    const float iconSz = metrics.iconRow * scale;

    float right = max.x - pad;
    const ImRect toggleRect(
        ImVec2(right - toggleW, min.y + (rowH - toggleH) * 0.5f),
        ImVec2(right, min.y + (rowH + toggleH) * 0.5f));
    right = toggleRect.Min.x - gap;

    ImRect swatchRect{};
    if (desc.color)
    {
        swatchRect = ImRect(
            ImVec2(right - swatchD, min.y + (rowH - swatchD) * 0.5f),
            ImVec2(right, min.y + (rowH + swatchD) * 0.5f));
        right = swatchRect.Min.x - gap;
    }

    ImRect overflowRect{};
    if (desc.overflow)
    {
        overflowRect = ImRect(
            ImVec2(right - overflowS, min.y + (rowH - overflowS) * 0.5f),
            ImVec2(right, min.y + (rowH + overflowS) * 0.5f));
        right = overflowRect.Min.x - gap;
    }

    const float labelMaxX = right;

    const bool overSwatch = desc.color &&
        ImGui::IsMouseHoveringRect(
            swatchRect.Min - ImVec2(4.f, 4.f),
            swatchRect.Max + ImVec2(4.f, 4.f),
            false);
    const bool overOverflow = desc.overflow &&
        ImGui::IsMouseHoveringRect(
            overflowRect.Min - ImVec2(4.f, 4.f),
            overflowRect.Max + ImVec2(4.f, 4.f),
            false);

    const bool rowClicked =
        ImGui::InvisibleButton("##row", ImVec2(width, rowH));
    const bool rowHot = ImGui::IsItemHovered();
    const bool rowHeld = ImGui::IsItemActive();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        (overSwatch || overOverflow))
    {
        const char* popupName = overSwatch ? "##color" : "##more";
        const ImRect& hit = overSwatch ? swatchRect : overflowRect;
        if (gui->is_popup_w_open(popupName))
            gui->close_popup_w(popupName);
        else
            gui->open_popup_w(popupName, hit.Min, hit.Max);
        if (overOverflow)
            result.overflow = true;
    }
    else if (rowClicked && desc.enabled && !overSwatch && !overOverflow)
    {
        // The whole row toggles: clicking the label used to do nothing here
        // while the stock checkbox toggled, which taught two different rules.
        *desc.enabled = !*desc.enabled;
        result.toggled = true;
        // A click is a decision, so the row stops showing a bind demonstration
        // and goes back to showing what it is set to.
        FeatureBinds::ClearPreview(desc.enabled);
        ImGui::MarkItemEdited(ImGui::GetItemID());
    }

    if (desc.enabled &&
        desc.bindable &&
        rowHot &&
        !overSwatch &&
        !overOverflow &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        // Anchored to the toggle, not to the whole row: the row spans the
        // column, so the panel used to open far to the left of the switch
        // that was actually clicked.
        gui->open_popup_w(
            "##bind",
            toggleRect.Min,
            toggleRect.Max);
    }

    struct RowAnim final
    {
        float on = 0.f;
        float hot = 0.f;
        float press = 0.f;
    };
    RowAnim& anim =
        LeyawinAnim<RowAnim, 7211>().get(ImGui::GetID("##row"));
    // While a bind for this row is pressed with the menu open, the switch shows
    // the press rather than the setting: that is the whole point of the
    // demonstration, and the setting behind it is deliberately untouched.
    bool on = desc.enabled && *desc.enabled;
    if (desc.enabled)
    {
        bool preview = false;
        if (FeatureBinds::PreviewState(desc.enabled, preview))
            on = preview;
    }
    anim.on = MenuMotion::Approach(
        anim.on, on ? 1.f : 0.f, MenuMotion::kSelection);
    anim.hot = MenuMotion::Approach(
        anim.hot, rowHot ? 1.f : 0.f, MenuMotion::kHover);
    anim.press = MenuMotion::Approach(
        anim.press, rowHeld ? 1.f : 0.f, MenuMotion::kPress);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    // Nothing draws behind the row: no hover or pressed plate, and no focus
    // outline either, which used to leave the last clicked row ringed in blue.
    const ImVec2 iconC(
        min.x + pad + iconSz * 0.5f,
        min.y + rowH * 0.5f);
    if (desc.icon && desc.icon[0])
    {
        DrawNavIcon(
            draw,
            iconC,
            desc.icon,
            gui->get_clr(
                ImLerp(tokens.textFaint, tokens.accent.active, anim.on),
                1.f),
            iconSz * ImClamp(desc.iconScale, 0.55f, 1.25f));
    }

    const char* label = desc.label ? desc.label : "";
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    draw->PushClipRect(
        min,
        ImVec2(labelMaxX - 4.f * scale, max.y),
        true);
    draw->AddText(
        ImVec2(
            min.x + pad + iconSz + metrics.space2 * scale,
            min.y + (rowH - textSize.y) * 0.5f),
        gui->get_clr(
            ImLerp(tokens.textMuted, tokens.textPrimary, anim.on),
            1.f),
        label);
    draw->PopClipRect();

    if (desc.tipEn)
    {
        ImGui::SetCursorScreenPos(ImVec2(
            min.x + pad + iconSz + metrics.space2 * scale + textSize.x + 6.f * scale,
            min.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f));
        HelpIcon(desc.tipEn, desc.tipRu ? desc.tipRu : desc.tipEn, desc.tipUa ? desc.tipUa : desc.tipEn, false);
    }

    if (desc.color)
    {
        const ImVec2 center = swatchRect.GetCenter();
        const float r = swatchRect.GetWidth() * 0.5f;
        draw->AddCircleFilled(
            center,
            r,
            gui->get_clr(
                ImColor(
                    desc.color[0],
                    desc.color[1],
                    desc.color[2],
                    desc.color[3])),
            20);
        draw->AddCircle(
            center,
            r,
            gui->get_clr(
                tokens.borderStrong,
                overSwatch ? 1.f : 0.65f),
            20,
            metrics.stroke);
    }

    if (desc.overflow)
    {
        DrawNavIcon(
            draw,
            overflowRect.GetCenter(),
            ICON_FA_ELLIPSIS,
            gui->get_clr(
                overOverflow ? tokens.textPrimary : tokens.textFaint,
                1.f),
            metrics.iconRow * scale);
    }

    const float knobR = toggleH * 0.5f - 2.f * scale;
    const float travel = toggleW - toggleH;
    const ImVec2 trackMin = toggleRect.Min;
    const ImVec2 trackMax = toggleRect.Max;
    draw->AddRectFilled(
        trackMin,
        trackMax,
        gui->get_clr(
            ImLerp(tokens.raised, tokens.accent.fill, anim.on),
            1.f),
        toggleH * 0.5f);
    if (anim.on < 0.99f)
    {
        draw->AddRect(
            trackMin,
            trackMax,
            gui->get_clr(tokens.borderStrong, 1.f - anim.on),
            toggleH * 0.5f,
            0,
            metrics.stroke);
    }
    const float knobX =
        trackMin.x + toggleH * 0.5f + travel * anim.on;
    const float knobY = (trackMin.y + trackMax.y) * 0.5f;
    // The knob is a surface, not text, but when the accent color is very light (e.g. white),
    // a white knob will disappear into the active track. Interpolate to onAccent when active.
    draw->AddCircleFilled(
        ImVec2(knobX, knobY),
        knobR,
        gui->get_clr(ImLerp(ImVec4(1.f, 1.f, 1.f, 1.f), tokens.onAccent, anim.on), 1.f),
        16);
    if (MenuTheme::IsLight())
    {
        draw->AddCircle(
            ImVec2(knobX, knobY),
            knobR,
            gui->get_clr(tokens.borderStrong, 1.f - anim.on * 0.6f),
            16,
            metrics.stroke);
    }

    if (desc.color && gui->begin_popup_w("##color", 220 * scale, -1))
    {
        float col4[4] = {
            desc.color[0],
            desc.color[1],
            desc.color[2],
            desc.color[3] };
        gui->color_picker(
            "picker",
            col4,
            ImGuiColorEditFlags_AlphaBar |
                ImGuiColorEditFlags_AlphaPreviewHalf |
                ImGuiColorEditFlags_DisplayRGB |
                ImGuiColorEditFlags_DisplayHex,
            nullptr);
        desc.color[0] = col4[0];
        desc.color[1] = col4[1];
        desc.color[2] = col4[2];
        desc.color[3] = col4[3];
        gui->end_popup_w();
    }

    if (desc.enabled && desc.bindable)
    {
        FeatureBinds::Binding* binding =
            FeatureBinds::Register(desc.enabled, desc.label);
        RenderBindPopup(binding);
    }

    ImGui::PopID();
    return result;
}

ScriptCardResult ScriptCard(
    const char* id,
    const ScriptCardDesc& desc) noexcept
{
    ScriptCardResult result{};
    const MenuTheme::Palette& tokens = MenuTheme::Colors();
    const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
    const float scale = UiScale();
    const float width = ImMax(ImGui::GetContentRegionAvail().x, 160.f);
    const float height = 58.f * scale;
    const float pad = 12.f * scale;
    const float gap = 8.f * scale;

    ImGui::PushID(id ? id : "script_card");
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max = min + ImVec2(width, height);

    ImFont* labelFont = set->poppins_widget
        ? set->poppins_widget
        : ImGui::GetFont();
    ImFont* iconFont = set->icon ? set->icon : ImGui::GetFont();

    // The pill is as wide as the glyph and the label need. At a fixed
    // seventy-eight the English "Load" sat in it comfortably and the Russian
    // one filled it edge to edge, with the play glyph on top of the border.
    const char* runLabel = desc.runLabel && desc.runLabel[0]
        ? desc.runLabel
        : "Load";
    const float runIconSize = 11.f * scale;
    const float runIconGap = 6.f * scale;
    const ImVec2 runIconExtent = iconFont->CalcTextSizeA(
        runIconSize, FLT_MAX, 0.f, ICON_FA_PLAY);
    const ImVec2 runTextExtent = labelFont->CalcTextSizeA(
        MenuType::BodySize(), FLT_MAX, 0.f, runLabel);
    const float runContentW =
        runIconExtent.x + runIconGap + runTextExtent.x;
    const float runW = ImMax(
        78.f * scale,
        runContentW + 24.f * scale);
    const float runH = 30.f * scale;
    const float moreS = 26.f * scale;

    float right = max.x - pad;
    const ImRect runRect(
        ImVec2(right - runW, min.y + (height - runH) * 0.5f),
        ImVec2(right, min.y + (height + runH) * 0.5f));
    right = runRect.Min.x - gap;
    const ImRect moreRect(
        ImVec2(right - moreS, min.y + (height - moreS) * 0.5f),
        ImVec2(right, min.y + (height + moreS) * 0.5f));
    right = moreRect.Min.x - gap;
    const float textMaxX = right - gap;

    // Which card is being renamed, if any. Editing suspends the card's own
    // hit-testing so the click lands in the text field instead.
    static ImGuiID s_RenamingCard = 0;
    static bool s_RenameFocus = false;
    const ImGuiID cardId = ImGui::GetID("##card");
    const bool canRename =
        desc.editableTitle != nullptr && desc.editableCapacity > 1;
    const bool renaming = canRename && s_RenamingCard == cardId;

    const auto over =
        [&](const ImRect& rect) noexcept {
            if (renaming)
                return false;
            return ImGui::IsMouseHoveringRect(
                rect.Min - ImVec2(3.f, 3.f),
                rect.Max + ImVec2(3.f, 3.f),
                false);
        };
    const bool overRun = desc.canRun && over(runRect);
    const bool overMore = over(moreRect);

    bool hovered = false;
    if (!renaming)
    {
        ImGui::InvisibleButton("##card", ImVec2(width, height));
        hovered = ImGui::IsItemHovered();
    }
    else
    {
        ImGui::Dummy(ImVec2(width, height));
    }
    // The rename field is placed back inside the card, so the row cursor has to
    // be restored before returning or the next card would stack into it.
    const ImVec2 afterCursor = ImGui::GetCursorScreenPos();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (overRun)
        {
            result.run = true;
        }
        else if (overMore)
        {
            if (gui->is_popup_w_open("##more"))
                gui->close_popup_w("##more");
            else
                gui->open_popup_w("##more", moreRect.Min, moreRect.Max);
            result.overflow = true;
        }
    }

    struct CardAnim final
    {
        float hot = 0.f;
        float fav = 0.f;
    };
    CardAnim& anim =
        LeyawinAnim<CardAnim, 7233>().get(cardId);
    anim.hot = MenuMotion::Approach(
        anim.hot, hovered ? 1.f : 0.f, MenuMotion::kHover);
    anim.fav = MenuMotion::Approach(
        anim.fav, desc.favorite ? 1.f : 0.f, MenuMotion::kSelection);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(
        min,
        max,
        gui->get_clr(
            ImLerp(tokens.panel, tokens.raised, anim.hot),
            0.92f),
        metrics.radiusPanel * scale);
    draw->AddRect(
        min,
        max,
        gui->get_clr(tokens.borderSubtle, 1.f),
        metrics.radiusPanel * scale,
        0,
        metrics.stroke);

    const float titleSize = MenuType::BodySize();
    const float microSize = MenuType::MicroSize();
    const char* title = desc.title && desc.title[0]
        ? desc.title
        : "Untitled";

    ImVec2 titleExtent = labelFont->CalcTextSizeA(
        titleSize, FLT_MAX, 0.f, title);
    float titleLimit = textMaxX - (min.x + pad);
    if (desc.stamp && desc.stamp[0])
        titleLimit -= 92.f * scale;
    const float titleW = ImMin(titleExtent.x, ImMax(24.f, titleLimit));
    const ImVec2 titleMin(min.x + pad, min.y + 9.f * scale);
    if (!renaming)
    {
        gui->render_text_clipped(
            labelFont,
            titleMin,
            ImVec2(titleMin.x + titleW, titleMin.y + 18.f * scale),
            gui->get_clr(tokens.textPrimary),
            title,
            {0.f, 0.5f});
        if (canRename &&
            hovered &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
            !overRun && !overMore &&
            ImGui::GetIO().MousePos.y <= min.y + 30.f * scale)
        {
            s_RenamingCard = cardId;
            s_RenameFocus = true;
        }
    }
    else
    {
        ImGui::SetCursorScreenPos(
            ImVec2(titleMin.x - 4.f * scale, titleMin.y - 3.f * scale));
        ImGui::SetNextItemWidth(
            ImMax(90.f * scale, textMaxX - titleMin.x));
        if (s_RenameFocus)
        {
            ImGui::SetKeyboardFocusHere();
            s_RenameFocus = false;
        }
        ImGui::PushStyleVar(
            ImGuiStyleVar_FramePadding,
            ImVec2(5.f * scale, 3.f * scale));
        ImGui::PushStyleColor(
            ImGuiCol_FrameBg,
            gui->get_clr(tokens.raised, 1.f));
        const bool submitted = ImGui::InputText(
            "##rename",
            desc.editableTitle,
            static_cast<size_t>(desc.editableCapacity),
            ImGuiInputTextFlags_EnterReturnsTrue |
                ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        // Enter, Escape and clicking away all end the edit; Escape restores the
        // original text on ImGui's side.
        if (submitted || ImGui::IsItemDeactivated())
        {
            s_RenamingCard = 0;
            result.renamed = true;
        }
    }

    if (!renaming && desc.stamp && desc.stamp[0])
    {
        const ImVec2 stampExtent = labelFont->CalcTextSizeA(
            microSize, FLT_MAX, 0.f, desc.stamp);
        const ImVec2 chipMin(
            titleMin.x + titleW + gap,
            titleMin.y + 1.f * scale);
        const ImVec2 chipMax(
            chipMin.x + stampExtent.x + 12.f * scale,
            chipMin.y + 16.f * scale);
        if (chipMax.x <= textMaxX)
        {
            draw->AddRectFilled(
                chipMin,
                chipMax,
                gui->get_clr(tokens.raised, 0.9f),
                metrics.radiusSmall * scale * 0.6f);
            draw->AddText(
                labelFont,
                microSize,
                ImVec2(
                    chipMin.x + 6.f * scale,
                    chipMin.y + (16.f * scale - stampExtent.y) * 0.5f),
                gui->get_clr(tokens.textMuted, 1.f),
                desc.stamp);
        }
    }

    if (desc.subtitle && desc.subtitle[0])
    {
        draw->AddText(
            labelFont,
            microSize,
            ImVec2(min.x + pad, min.y + 33.f * scale),
            gui->get_clr(tokens.textFaint, 1.f),
            desc.subtitle);
    }

    const ImVec2 moreExtent = iconFont->CalcTextSizeA(
        13.f * scale, FLT_MAX, 0.f, ICON_FA_ELLIPSIS);
    draw->AddText(
        iconFont,
        13.f * scale,
        moreRect.GetCenter() - moreExtent * 0.5f,
        gui->get_clr(
            overMore ? tokens.textPrimary : tokens.textFaint,
            1.f),
        ICON_FA_ELLIPSIS);

    draw->AddRectFilled(
        runRect.Min,
        runRect.Max,
        gui->get_clr(
            desc.canRun ? tokens.raised : tokens.panel,
            desc.canRun ? (overRun ? 1.f : 0.85f) : 0.6f),
        metrics.radiusControl * scale);
    draw->AddRect(
        runRect.Min,
        runRect.Max,
        gui->get_clr(tokens.borderSubtle, 1.f),
        metrics.radiusControl * scale,
        0,
        metrics.stroke);
    const float runX =
        runRect.GetCenter().x - runContentW * 0.5f;
    draw->AddText(
        iconFont,
        runIconSize,
        ImVec2(runX, runRect.GetCenter().y - runIconExtent.y * 0.5f),
        gui->get_clr(
            desc.canRun ? tokens.textPrimary : tokens.textFaint,
            1.f),
        ICON_FA_PLAY);
    draw->AddText(
        labelFont,
        MenuType::BodySize(),
        ImVec2(
            runX + runIconExtent.x + runIconGap,
            runRect.GetCenter().y - runTextExtent.y * 0.5f),
        gui->get_clr(
            desc.canRun ? tokens.textPrimary : tokens.textFaint,
            1.f),
        runLabel);

    ImGui::SetCursorScreenPos(afterCursor);
    ImGui::PopID();
    return result;
}

bool BeginOverflowPopup(const char* rowId, float width) noexcept
{
    if (!rowId || !rowId[0])
        return false;
    ImGui::PushID(rowId);
    if (!gui->begin_popup_w("##more", width, -1))
    {
        ImGui::PopID();
        return false;
    }
    return true;
}

void EndOverflowPopup() noexcept
{
    gui->end_popup_w();
    ImGui::PopID();
}

void CloseOverflowPopup() noexcept
{
    gui->close_popup_w("##more");
}

void PageHeader(
    const char* title,
    const char* subtitle) noexcept
{
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        clr->text_active);
    ImGui::TextUnformatted(title ? title : "");
    ImGui::PopStyleColor();
    if (subtitle && subtitle[0])
    {
        ImGui::SameLine(0.f, 10.f * UiScale());
        ImGui::TextDisabled("%s", subtitle);
    }
    ImGui::Spacing();
}

void StatusChip(
    const char* id,
    const char* text,
    bool positive) noexcept
{
    const float scale = UiScale();
    const ImVec2 textSize =
        ImGui::CalcTextSize(text ? text : "");
    const ImVec2 size(
        textSize.x + 18.f * scale,
        ImGui::GetTextLineHeight() + 8.f * scale);
    ImGui::PushID(id ? id : "status");
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##status", size);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec4 color = positive
        ? MenuTheme::Colors().success
        : MenuTheme::Colors().warning;
    draw->AddRectFilled(
        min,
        min + size,
        gui->get_clr(clr->element_background, 0.82f),
        6.f * scale);
    draw->AddCircleFilled(
        ImVec2(
            min.x + 8.f * scale,
            min.y + size.y * 0.5f),
        2.5f * scale,
        ImGui::GetColorU32(color));
    draw->AddText(
        ImVec2(
            min.x + 13.f * scale,
            min.y + (size.y - textSize.y) * 0.5f),
        gui->get_clr(clr->text_active),
        text ? text : "");
    ImGui::PopID();
}

void BeginGlassPanel(
    const char* id,
    const ImVec2& size,
    const char* title,
    const char* subtitle,
    ImGuiWindowFlags flags,
    bool showAccentTick) noexcept
{
    // The accent tick above every panel was pure decoration and made each
    // pane shout for attention; the accent now only marks state.
    (void)showAccentTick;

    const MenuTheme::Palette& tokens = MenuTheme::Colors();
    const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
    const float scale = UiScale();
    ImGui::PushStyleVar(
        ImGuiStyleVar_ChildRounding, metrics.radiusPanel * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, metrics.stroke);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(metrics.space3, metrics.space3) * scale);
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(metrics.space2, metrics.space2) * scale);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, tokens.base);
    ImGui::PushStyleColor(ImGuiCol_Border, tokens.borderSubtle);
    ImGui::BeginChild(
        id ? id : "##glass_panel",
        size,
        ImGuiChildFlags_Border |
            ImGuiChildFlags_AlwaysUseWindowPadding,
        flags |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (title && title[0])
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 min = ImGui::GetCursorScreenPos();
        ImFont* font = MenuType::Title();
        const float fontSize = MenuType::TitleSize();
        const ImVec2 titleSz = font->CalcTextSizeA(
            fontSize, FLT_MAX, 0.f, title);
        draw->AddText(
            font,
            fontSize,
            min,
            gui->get_clr(tokens.textPrimary, 1.f),
            title);
        float blockH = titleSz.y;
        if (subtitle && subtitle[0])
        {
            ImFont* subFont = MenuType::Micro();
            const float subSize = MenuType::MicroSize();
            draw->AddText(
                subFont,
                subSize,
                ImVec2(min.x, min.y + titleSz.y + 2.f * scale),
                gui->get_clr(tokens.textMuted, 1.f),
                subtitle);
            blockH += 2.f * scale + subSize;
        }
        ImGui::Dummy(ImVec2(0.f, blockH + metrics.space2 * scale));
    }
}

void EndGlassPanel() noexcept
{
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
}

void BeginToolbar(
    const char* id,
    const ImVec2& size) noexcept
{
    const float scale = UiScale();
    ImVec2 actual = size;
    if (actual.x <= 0.f)
        actual.x = ImGui::GetContentRegionAvail().x;
    if (actual.y <= 0.f)
        actual.y = 44.f * scale;

    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 9.f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(7.f, 6.f) * scale);
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(7.f, 5.f) * scale);
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        gui->get_clr(clr->element_background, 0.46f));
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        gui->get_clr(clr->child_stroke_bar, 0.78f));
    ImGui::BeginChild(
        id ? id : "##glass_toolbar",
        actual,
        ImGuiChildFlags_Border |
            ImGuiChildFlags_AlwaysUseWindowPadding,
        ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings);
}

void EndToolbar() noexcept
{
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
}

bool ActionButton(
    const char* id,
    const char* label,
    const ImVec2& size,
    bool primary,
    const char* icon,
    void (*bindAction)()) noexcept
{
    ImGui::PushID(id ? id : "action");
    const MenuTheme::Palette& tokens = MenuTheme::Colors();
    const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
    const float scale = UiScale();

    // Follow the ImGui convention for a non positive width: fill the region and
    // subtract the value. Callers passing -1 used to get a one pixel sliver.
    ImVec2 buttonSize = size;
    if (buttonSize.x <= 0.f)
    {
        buttonSize.x = ImMax(
            ImGui::GetContentRegionAvail().x + buttonSize.x,
            1.f);
    }
    if (buttonSize.y <= 0.f)
        buttonSize.y = metrics.rowHeight * scale;

    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##action", buttonSize);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    FeatureBinds::Binding* binding = nullptr;
    if (bindAction)
    {
        binding = FeatureBinds::RegisterAction(id, label, bindAction);
        if (binding)
        {
            if (hovered &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                gui->open_popup_w(
                    "##bind",
                    min,
                    min + buttonSize);
            }
            RenderBindPopup(binding);
        }
    }

    struct ActionAnim final
    {
        float hot = 0.f;
        float down = 0.f;
    };
    ActionAnim& anim =
        LeyawinAnim<ActionAnim, 7203>().get(ImGui::GetItemID());
    anim.hot = MenuMotion::Approach(
        anim.hot, hovered ? 1.f : 0.f, MenuMotion::kHover);
    anim.down = MenuMotion::Approach(
        anim.down, held ? 1.f : 0.f, MenuMotion::kPress);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max = min + buttonSize;
    const float radius = metrics.radiusControl * scale;

    // Primary is the only place the accent is used as a surface; everything
    // else keeps the accent for state and borders.
    const ImVec4 fill = primary
        ? ImLerp(tokens.accent.fill, tokens.accent.pressed, ImMax(anim.hot, anim.down))
        : ImLerp(
            ImLerp(tokens.raised, tokens.hover, anim.hot),
            tokens.pressed,
            anim.down);
    draw->AddRectFilled(min, max, gui->get_clr(fill, 1.f), radius);
    if (!primary)
    {
        draw->AddRect(
            min,
            max,
            gui->get_clr(
                ImLerp(tokens.borderSubtle, tokens.accent.fill, anim.hot),
                1.f),
            radius,
            0,
            metrics.stroke);
    }

    const ImVec4 contentCol = primary
        ? tokens.onAccent
        : ImLerp(tokens.textMuted, tokens.textPrimary, anim.hot);

    const char* text = label ? label : "";
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    float totalW = textSize.x;
    const float iconGap = metrics.space2 * scale;
    const float iconSz = metrics.iconRow * scale;
    ImVec2 iconSize{};
    if (icon && icon[0] && set->icon)
    {
        iconSize = set->icon->CalcTextSizeA(iconSz, FLT_MAX, 0.f, icon);
        totalW += iconSize.x + iconGap;
    }
    // A bound key is shown on the button itself, and the label is centred in
    // what is left rather than under the badge.
    const char* keyText = binding && binding->key
        ? gui->key_name(binding->key)
        : nullptr;
    ImFont* keyFont = keyText ? MenuType::Mono() : nullptr;
    float keyWidth = 0.f;
    if (keyFont)
    {
        const float keySize = MenuType::MonoSize();
        const ImVec2 keyExtent = keyFont->CalcTextSizeA(
            keySize, FLT_MAX, 0.f, keyText);
        keyWidth = keyExtent.x + metrics.space2 * scale;
        draw->AddText(
            keyFont,
            keySize,
            ImVec2(
                max.x - metrics.space2 * scale - keyExtent.x,
                min.y + (buttonSize.y - keyExtent.y) * 0.5f),
            gui->get_clr(
                primary ? tokens.onAccent : tokens.textFaint,
                primary ? 0.75f : 1.f),
            keyText);
    }

    float x = min.x + (buttonSize.x - keyWidth - totalW) * 0.5f;
    const float y = min.y + (buttonSize.y - textSize.y) * 0.5f;
    if (icon && icon[0] && set->icon)
    {
        draw->AddText(
            set->icon,
            iconSz,
            ImVec2(x, min.y + (buttonSize.y - iconSize.y) * 0.5f),
            gui->get_clr(contentCol, 1.f),
            icon);
        x += iconSize.x + iconGap;
    }
    draw->AddText(ImVec2(x, y), gui->get_clr(contentCol, 1.f), text);
    ImGui::PopID();
    return pressed;
}

bool GhostIconButton(
    const char* id,
    const char* icon,
    const ImVec2& size,
    bool danger) noexcept
{
    ImGui::PushID(id ? id : "ghost_icon");
    const MenuTheme::Palette& tokens = MenuTheme::Colors();
    const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
    const float scale = UiScale();

    ImVec2 buttonSize = size;
    if (buttonSize.x <= 0.f)
    {
        buttonSize.x = ImMax(
            ImGui::GetContentRegionAvail().x + buttonSize.x,
            1.f);
    }
    if (buttonSize.y <= 0.f)
        buttonSize.y = metrics.rowHeight * scale;

    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##ghost", buttonSize);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();

    struct GhostAnim final
    {
        float hot = 0.f;
        float down = 0.f;
    };
    GhostAnim& anim =
        LeyawinAnim<GhostAnim, 7311>().get(ImGui::GetItemID());
    anim.hot = MenuMotion::Approach(
        anim.hot, hovered ? 1.f : 0.f, MenuMotion::kHover);
    anim.down = MenuMotion::Approach(
        anim.down, held ? 1.f : 0.f, MenuMotion::kPress);

    // Colour carries the whole state: nothing is drawn behind the glyph, so
    // there is no surface to tint.
    const ImVec4 target = danger ? tokens.error : tokens.textPrimary;
    const ImVec4 content =
        ImLerp(tokens.textMuted, target, ImMax(anim.hot, anim.down));

    if (icon && icon[0] && set->icon)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        // Presses nudge the glyph down a pixel, which is the whole feedback
        // budget a background-free control has.
        const float iconSz = metrics.iconRow * scale;
        const ImVec2 iconSize =
            set->icon->CalcTextSizeA(iconSz, FLT_MAX, 0.f, icon);
        draw->AddText(
            set->icon,
            iconSz,
            ImVec2(
                min.x + (buttonSize.x - iconSize.x) * 0.5f,
                min.y + (buttonSize.y - iconSize.y) * 0.5f +
                    anim.down),
            gui->get_clr(content, 1.f),
            icon);
    }
    ImGui::PopID();
    return pressed;
}

bool SearchRow(
    const char* id,
    const char* hint,
    char* buffer,
    std::size_t capacity,
    const ImVec2& size,
    ImGuiInputTextFlags flags) noexcept
{
    if (!buffer || capacity == 0)
        return false;

    const float scale = UiScale();
    const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
    const float width =
        size.x > 0.f ? size.x : ImGui::GetContentRegionAvail().x;
    const float height = size.y > 0.f ? size.y : 36.f * scale;
    const float vertical =
        ImMax(0.f, (height - ImGui::GetTextLineHeight()) * 0.5f);

    ImGui::PushID(id ? id : "search");
    ImGui::PushStyleColor(
        ImGuiCol_FrameBg,
        gui->get_clr(clr->element_background, 0.72f));
    ImGui::PushStyleColor(
        ImGuiCol_FrameBgHovered,
        gui->get_clr(clr->element_background, 0.94f));
    ImGui::PushStyleColor(
        ImGuiCol_FrameBgActive,
        gui->get_clr(clr->combo_color, 0.96f));
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        gui->get_clr(clr->child_stroke_bar, 0.95f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_FrameRounding,
        metrics.radiusControl * scale);
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(30.f * scale, vertical));
    ImGui::SetNextItemWidth(width);
    const bool changed = ImGui::InputTextWithHint(
        "##field",
        hint ? hint : "",
        buffer,
        capacity,
        flags);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);

    if (set->icon)
    {
        const ImVec2 iconSize = set->icon->CalcTextSizeA(
            ImGui::GetFontSize(),
            FLT_MAX,
            0.f,
            ICON_FA_MAGNIFYING_GLASS);
        ImGui::GetWindowDrawList()->AddText(
            set->icon,
            ImGui::GetFontSize(),
            ImVec2(
                min.x + 10.f * scale,
                min.y + (max.y - min.y - iconSize.y) * 0.5f -
                    0.5f * scale),
            gui->get_clr(clr->text, 0.78f),
            ICON_FA_MAGNIFYING_GLASS);
    }
    ImGui::PopID();
    return changed;
}

void EmptyState(
    const char* id,
    const char* icon,
    const char* title,
    const char* detail,
    float height) noexcept
{
    const float scale = UiScale();
    const float h = ImMax(height * scale, 88.f * scale);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 size(ImGui::GetContentRegionAvail().x, h);
    ImGui::PushID(id ? id : "empty");
    ImGui::InvisibleButton("##empty_state", size);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    const ImVec2 center(
        min.x + size.x * 0.5f,
        min.y + size.y * 0.5f);
    const float iconFontSize = 22.f * scale;
    const char* glyph = icon && icon[0] ? icon : ICON_FA_INBOX;
    ImFont* iconFont = set->icon ? set->icon : ImGui::GetFont();
    const ImVec2 iconSize = iconFont->CalcTextSizeA(
        iconFontSize,
        FLT_MAX,
        0.f,
        glyph);
    const ImVec2 titleSize =
        ImGui::CalcTextSize(title ? title : "");
    const ImVec2 detailSize =
        ImGui::CalcTextSize(detail ? detail : "");
    const float contentH =
        iconSize.y + 9.f * scale + titleSize.y +
        (detail && detail[0]
            ? (5.f * scale + detailSize.y)
            : 0.f);
    float y = center.y - contentH * 0.5f;
    draw->AddText(
        iconFont,
        iconFontSize,
        ImVec2(center.x - iconSize.x * 0.5f, y),
        gui->get_clr(clr->accent_color, 0.88f),
        glyph);
    y += iconSize.y + 9.f * scale;
    draw->AddText(
        ImVec2(center.x - titleSize.x * 0.5f, y),
        gui->get_clr(clr->text_active),
        title ? title : "");
    if (detail && detail[0])
    {
        y += titleSize.y + 5.f * scale;
        draw->AddText(
            ImVec2(center.x - detailSize.x * 0.5f, y),
            gui->get_clr(clr->text, 0.82f),
            detail);
    }
    ImGui::PopID();
}

MasterDetailMetrics CalculateMasterDetail(
    float availableWidth,
    float availableHeight,
    float preferredMasterWidth,
    float minimumDetailWidth,
    float compactMasterHeight) noexcept
{
    const float scale = UiScale();
    MasterDetailMetrics result{};
    result.gap = 12.f * scale;
    const float master = preferredMasterWidth * scale;
    const float detailMinimum = minimumDetailWidth * scale;
    result.sideBySide =
        availableWidth >= master + detailMinimum + result.gap;

    if (result.sideBySide)
    {
        result.masterWidth = ImMin(
            master,
            availableWidth * 0.50f);
        result.detailWidth = ImMax(
            1.f,
            availableWidth - result.masterWidth - result.gap);
        result.masterHeight = ImMax(availableHeight, 1.f);
        result.detailHeight = result.masterHeight;
    }
    else
    {
        result.masterWidth = ImMax(availableWidth, 1.f);
        result.detailWidth = result.masterWidth;
        result.masterHeight = ImMin(
            compactMasterHeight * scale,
            ImMax(150.f * scale, availableHeight * 0.42f));
        result.detailHeight = ImMax(
            180.f * scale,
            availableHeight - result.masterHeight - result.gap);
    }
    return result;
}

const char* BoneLabel(int index) noexcept
{
    static const char* kEn[Menu::kAimHitboxCount] = {
        "Head", "Neck", "Chest", "Pelvis",
        "Left Upper Arm", "Left Forearm", "Left Hand",
        "Right Upper Arm", "Right Forearm", "Right Hand",
        "Left Thigh", "Left Calf", "Left Foot",
        "Right Thigh", "Right Calf", "Right Foot"
    };
    static const char* kRu[Menu::kAimHitboxCount] = {
        "Голова", "Шея", "Грудь", "Таз",
        "Л. плечо", "Л. предплечье", "Л. кисть",
        "П. плечо", "П. предплечье", "П. кисть",
        "Л. бедро", "Л. голень", "Л. стопа",
        "П. бедро", "П. голень", "П. стопа"
    };
    static const char* kUa[Menu::kAimHitboxCount] = {
        "Голова", "Шия", "Груди", "Таз",
        "Л. плече", "Л. передпліччя", "Л. кисть",
        "П. плече", "П. передпліччя", "П. кисть",
        "Л. стегно", "Л. гомілка", "Л. стопа",
        "П. стегно", "П. гомілка", "П. стопа"
    };
    if (index < 0 || index >= Menu::kAimHitboxCount)
        return "?";
    return T(kEn[index], kRu[index], kUa[index]);
}

void DrawBoneDropdown(
    bool* bones,
    const char* idSuffix,
    bool* nearestAny) noexcept
{
    if (!bones)
        return;

    char id[72]{};
    _snprintf(
        id,
        sizeof(id) - 1,
        "%s###bones_%s",
        T("Bones", "Кости", "Кістки"),
        idSuffix ? idSuffix : "b");

    if (!nearestAny)
    {
        const char* plain[Menu::kAimHitboxCount]{};
        for (int i = 0; i < Menu::kAimHitboxCount; ++i)
            plain[i] = BoneLabel(i);
        gui->multi_dropdown_icon(
            ICON_FA_BONE,
            id,
            bones,
            plain,
            Menu::kAimHitboxCount,
            8);
        return;
    }

    // Nearest is an option in the same list instead of a switch beside it:
    // choosing it hands the pick to the aimbot, so the explicit bones clear.
    constexpr int kTotal = Menu::kAimHitboxCount + 1;
    const char* labels[kTotal]{};
    bool values[kTotal]{};
    labels[0] = T("Nearest", "Ближайшая", "Найближча");
    values[0] = *nearestAny;
    for (int i = 0; i < Menu::kAimHitboxCount; ++i)
    {
        labels[i + 1] = BoneLabel(i);
        values[i + 1] = bones[i];
    }
    bool before[kTotal]{};
    for (int i = 0; i < kTotal; ++i)
        before[i] = values[i];

    gui->multi_dropdown_icon(
        ICON_FA_BONE,
        id,
        values,
        labels,
        kTotal,
        8);

    if (values[0] && !before[0])
    {
        for (int i = 1; i < kTotal; ++i)
            values[i] = false;
    }
    else
    {
        for (int i = 1; i < kTotal; ++i)
        {
            if (values[i] && !before[i])
            {
                values[0] = false;
                break;
            }
        }
    }

    bool anyBone = false;
    for (int i = 1; i < kTotal; ++i)
        anyBone = anyBone || values[i];
    // Clearing the last bone would leave the mode without a target, so it falls
    // back to nearest rather than to nothing.
    if (!anyBone)
        values[0] = true;

    *nearestAny = values[0];
    for (int i = 0; i < Menu::kAimHitboxCount; ++i)
        bones[i] = values[i + 1];
}

void DrawBoneOptions(
    bool* nearest,
    bool* anyBone,
    bool* visible,
    const char* idSuffix) noexcept
{
    char n[48]{};
    char a[48]{};
    char v[48]{};
    _snprintf(n, sizeof(n) - 1, "Nearest##%s", idSuffix);
    _snprintf(a, sizeof(a) - 1, "Any Bone##%s", idSuffix);
    _snprintf(v, sizeof(v) - 1, "Visible##%s", idSuffix);
    CheckWithHelp(
        n, nearest,
        "Prefer the bone/ped closest to crosshair among allowed bones.",
        "Брать ближайшую к прицелу кость/педа среди разрешённых.",
        "Брати найближчу до прицілу кістку/педа серед дозволених.",
        false);
    CheckWithHelp(
        a, anyBone,
        "Allow any skeleton bone, not only the checked ones.",
        "Разрешить любую кость скелета, не только отмеченные.",
        "Дозволити будь-яку кістку скелета, не лише позначені.",
        false);
    CheckWithHelp(
        v, visible,
        "Require line-of-sight to the target bone.",
        "Требовать видимость кости цели.",
        "Вимагати видимість кістки цілі.",
        false);
}

bool DrawListRow(const char* id, const ListRowDesc& desc) noexcept
{
    ImGui::PushID(id);
    const float scale = UiScale();
    const float h =
        (desc.height > 0.f ? desc.height : 48.f) * scale;
    const ImVec2 rowSize(ImGui::GetContentRegionAvail().x, h);
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##row", rowSize);
    const bool hovered = ImGui::IsItemHovered();

    struct RowAnim final { float hot = 0.f; };
    RowAnim& anim = LeyawinAnim<RowAnim, 6201>().get(ImGui::GetItemID());
    const float target = (desc.selected || hovered) ? 1.f : 0.f;
    anim.hot = MenuMotion::Approach(
        anim.hot, target, MenuMotion::kHover);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 rowMax(cursor.x + rowSize.x, cursor.y + rowSize.y);
    dl->AddRectFilled(
        cursor,
        rowMax,
        gui->get_clr(
            clr->element_background,
            0.18f + 0.20f * anim.hot),
        8.f * scale);
    if (anim.hot > 0.01f)
    {
        dl->AddRectFilled(
            cursor,
            rowMax,
            gui->get_clr(
                desc.selected ? clr->selection_active : clr->element_background,
                (desc.selected ? 1.f : 0.75f) * anim.hot),
            8.f * scale);
    }
    dl->AddRect(
        cursor,
        rowMax,
        gui->get_clr(
            desc.selected ? clr->accent_color : clr->child_stroke_bar,
            desc.selected ? 0.34f : 0.42f + anim.hot * 0.24f),
        8.f * scale);
    if (desc.selected)
    {
        dl->AddRectFilled(
            ImVec2(cursor.x, cursor.y + 8.f * scale),
            ImVec2(
                cursor.x + 3.f * scale,
                rowMax.y - 8.f * scale),
            gui->get_clr(clr->accent_color),
            2.f * scale);
    }

    float textX = cursor.x + 12.f * scale;
    if (desc.iconSlotW > 0.f)
    {
        const float slotW = desc.iconSlotW * scale;
        if (desc.icon && desc.iconW > 0.f && desc.iconH > 0.f)
        {
            const float iconW = desc.iconW * scale;
            const float iconH = desc.iconH * scale;
            const ImVec2 iconPos(
                cursor.x + 10.f * scale +
                    (slotW - iconW) * 0.5f,
                cursor.y + (h - iconH) * 0.5f);
            // Tint with style Alpha so icons fade with menu close animation.
            dl->AddImage(
                desc.icon,
                iconPos,
                ImVec2(iconPos.x + iconW, iconPos.y + iconH),
                ImVec2(0.f, 0.f),
                ImVec2(1.f, 1.f),
                ImGui::GetColorU32(ImVec4(1.f, 1.f, 1.f, 1.f)));
        }
        else
        {
            dl->AddRectFilled(
                ImVec2(
                    cursor.x + 10.f * scale,
                    cursor.y + (h - 22.f * scale) * 0.5f),
                ImVec2(
                    cursor.x + 10.f * scale + slotW,
                    cursor.y + (h + 22.f * scale) * 0.5f),
                gui->get_clr(clr->child_top_bar),
                4.f * scale);
        }
        textX =
            cursor.x + 10.f * scale + slotW + 10.f * scale;
    }

    dl->AddText(
        ImVec2(
            textX,
            cursor.y +
                (desc.meta
                    ? 7.f * scale
                    : (h - ImGui::GetTextLineHeight()) * 0.5f)),
        gui->get_clr(clr->text_active),
        desc.title ? desc.title : "");
    if (desc.meta && desc.meta[0])
    {
        dl->AddText(
            ImVec2(textX, cursor.y + 25.f * scale),
            gui->get_clr(clr->text, 0.75f),
            desc.meta);
    }

    if (desc.accentDot)
    {
        dl->AddCircleFilled(
            ImVec2(
                rowMax.x - 14.f * scale,
                cursor.y + h * 0.5f),
            4.f * scale,
            gui->get_clr(clr->accent_color));
    }

    ImGui::PopID();
    return clicked;
}
}
