#pragma once

#include "../imgui/imgui.h"
#include <cmath>

enum EspHealthBarPosition : int
{
    EspHealthBarLeft = 0,
    EspHealthBarRight,
    EspHealthBarTop,
    EspHealthBarBottom,
};

enum EspBoxStyle : int
{
    EspBoxFull = 0,
    EspBoxCorners,
    EspBoxFilled,
    EspBoxStyleCount
};

// How the aim target is marked.
enum EspTargetMarkStyle : int
{
    // A single pulsing dot on the aimed-at bone.
    EspTargetMarkDot = 0,
    // Particles orbiting the body, each dragging a fading trail. Drawn as
    // additive glow sprites positioned in the world, so the ring foreshortens
    // and shrinks with distance on its own.
    EspTargetMarkOrbits,
    EspTargetMarkStyleCount
};

// How much of a target behind cover the visible check recolours.
enum EspHiddenStyle : int
{
    // Box, skeleton and every label take the hidden colour.
    EspHiddenTintAll = 0,
    // Only the outline is recoloured. Labels keep their own colours, so a
    // faction tag stays readable as a faction tag while the shape still reads
    // as being behind something.
    EspHiddenTintOutline,
    EspHiddenStyleCount
};

enum EspBarStyle : int
{
    EspBarSolid = 0,
    EspBarGradient,
    EspBarValue,   // bar + numeric value
    EspBarText,    // text only: [HP N] / [A N]
    EspBarDynamic, // solid fill; colour from remaining %
    EspBarStyleCount
};

enum EspWeaponDisplay : int
{
    EspWeaponText = 0,
    EspWeaponIcon,
    EspWeaponBoth,
    EspWeaponDisplayCount
};

// Clones an already-built run of glyph quads: the same geometry moved by a
// delta and optionally recoloured. Four of the five outline passes are the very
// same quads, so laying the string out five times was wasted work — at a
// hundred players that is thousands of redundant glyph lookups per frame.
inline void EspCloneTextPass(
    ImDrawList* draw,
    int vtxStart,
    int vtxCount,
    int idxStart,
    int idxCount,
    unsigned int srcBase,
    float deltaX,
    float deltaY,
    bool recolor,
    ImU32 color) noexcept
{
    draw->PrimReserve(idxCount, vtxCount);
    const unsigned int dstBase = draw->_VtxCurrentIdx;
    // The reserve above can reallocate, so the source is re-read afterwards.
    const ImDrawVert* source =
        draw->VtxBuffer.Data + vtxStart;
    ImDrawVert* target = draw->_VtxWritePtr;
    for (int index = 0; index < vtxCount; ++index)
    {
        target[index].pos.x = source[index].pos.x + deltaX;
        target[index].pos.y = source[index].pos.y + deltaY;
        target[index].uv = source[index].uv;
        target[index].col = recolor
            ? color
            : source[index].col;
    }
    const ImDrawIdx* sourceIdx =
        draw->IdxBuffer.Data + idxStart;
    ImDrawIdx* targetIdx = draw->_IdxWritePtr;
    for (int index = 0; index < idxCount; ++index)
    {
        targetIdx[index] = static_cast<ImDrawIdx>(
            sourceIdx[index] - srcBase + dstBase);
    }
    draw->_VtxWritePtr += vtxCount;
    draw->_IdxWritePtr += idxCount;
    draw->_VtxCurrentIdx += static_cast<unsigned int>(vtxCount);
}

inline void EspDrawOutlinedText(
    ImDrawList* draw,
    ImFont* font,
    float fontSize,
    const ImVec2& pos,
    ImU32 color,
    const char* text,
    float outlineOffset = 1.0f) noexcept
{
    if (!draw || !text || !text[0])
        return;
    if (!font)
        font = ImGui::GetFont();
    if (fontSize <= 0.f)
        fontSize = ImGui::GetFontSize();

    // Align to integer screen pixels to prevent subpixel blur / jitter when target is far
    const ImVec2 p(floorf(pos.x + 0.5f), floorf(pos.y + 0.5f));

    const int a = (int)((color >> IM_COL32_A_SHIFT) & 0xFF);
    if (a <= 0)
        return;
    const int oa = (a * 185) / 255;
    const ImU32 outline = IM_COL32(0, 0, 0, oa);

    // First pass is laid out for real; the rest are clones of it. Glyphs that
    // straddle the clip rect edge are cut by the first pass, so a clone can
    // differ from a fresh layout within one pixel of the screen border.
    const int vtxStart = draw->VtxBuffer.Size;
    const int idxStart = draw->IdxBuffer.Size;
    const unsigned int srcBase = draw->_VtxCurrentIdx;
    const ImVec2 origin(p.x - outlineOffset, p.y);
    draw->AddText(font, fontSize, origin, outline, text);
    const int vtxCount = draw->VtxBuffer.Size - vtxStart;
    const int idxCount = draw->IdxBuffer.Size - idxStart;
    if (vtxCount <= 0 || idxCount <= 0)
        return;

    EspCloneTextPass(
        draw, vtxStart, vtxCount, idxStart, idxCount, srcBase,
        outlineOffset * 2.f, 0.f, false, outline);
    EspCloneTextPass(
        draw, vtxStart, vtxCount, idxStart, idxCount, srcBase,
        outlineOffset, -outlineOffset, false, outline);
    EspCloneTextPass(
        draw, vtxStart, vtxCount, idxStart, idxCount, srcBase,
        outlineOffset, outlineOffset, false, outline);
    EspCloneTextPass(
        draw, vtxStart, vtxCount, idxStart, idxCount, srcBase,
        outlineOffset, 0.f, true, color);
}

enum EspTagSide : int
{
    EspTagAbove = 0,
    EspTagBelow = 1,
};

inline int EspSnapHealthBarPosition(const ImVec2& boxMin, const ImVec2& boxMax, const ImVec2& point)
{
    const ImVec2 center((boxMin.x + boxMax.x) * 0.5f, (boxMin.y + boxMax.y) * 0.5f);
    const float dx = point.x - center.x;
    const float dy = point.y - center.y;
    if (fabsf(dx) >= fabsf(dy))
        return dx < 0.f ? EspHealthBarLeft : EspHealthBarRight;
    return dy < 0.f ? EspHealthBarTop : EspHealthBarBottom;
}

inline int EspSnapTagSide(const ImVec2& boxMin, const ImVec2& boxMax, const ImVec2& point)
{
    const float cy = (boxMin.y + boxMax.y) * 0.5f;
    return point.y < cy ? EspTagAbove : EspTagBelow;
}

inline float EspTagBaselineY(
    float anchorTop,
    float anchorBottom,
    int side,
    float textH,
    int stackFromEdge)
{
    if (side == EspTagBelow)
        return anchorBottom + 4.f + static_cast<float>(stackFromEdge) * (textH + 2.f);
    return anchorTop - textH - 4.f - static_cast<float>(stackFromEdge) * (textH + 2.f);
}

inline void EspComputeHealthBarRects(
    const ImVec2& boxMin,
    const ImVec2& boxMax,
    float hpPercent,
    int position,
    ImVec2* fillMin,
    ImVec2* fillMax,
    ImVec2* outlineMin,
    ImVec2* outlineMax,
    int stackIndex = 0)
{
    if (hpPercent < 0.f) hpPercent = 0.f;
    if (hpPercent > 1.f) hpPercent = 1.f;
    if (stackIndex < 0) stackIndex = 0;

    const float barThick = 4.f;
    const float barPad = 2.f;
    const float stackShift = static_cast<float>(stackIndex) * (barThick + barPad + 3.f);
    const float boxW = boxMax.x - boxMin.x;
    const float boxH = boxMax.y - boxMin.y;

    switch (position) {
    case EspHealthBarRight:
        *outlineMin = ImVec2(boxMax.x + barPad + stackShift, boxMin.y - 1.f);
        *outlineMax = ImVec2(boxMax.x + barPad + barThick + 2.f + stackShift, boxMax.y + 1.f);
        *fillMin = ImVec2(boxMax.x + barPad + 1.f + stackShift, boxMax.y - boxH * hpPercent);
        *fillMax = ImVec2(boxMax.x + barPad + barThick + 1.f + stackShift, boxMax.y);
        break;
    case EspHealthBarTop:
        *outlineMin = ImVec2(boxMin.x - 1.f, boxMin.y - barPad - barThick - 2.f - stackShift);
        *outlineMax = ImVec2(boxMax.x + 1.f, boxMin.y - barPad - stackShift);
        *fillMin = ImVec2(boxMin.x, boxMin.y - barPad - barThick - stackShift);
        *fillMax = ImVec2(boxMin.x + boxW * hpPercent, boxMin.y - barPad - 1.f - stackShift);
        break;
    case EspHealthBarBottom:
        *outlineMin = ImVec2(boxMin.x - 1.f, boxMax.y + barPad + stackShift);
        *outlineMax = ImVec2(boxMax.x + 1.f, boxMax.y + barPad + barThick + 2.f + stackShift);
        *fillMin = ImVec2(boxMin.x, boxMax.y + barPad + 1.f + stackShift);
        *fillMax = ImVec2(boxMin.x + boxW * hpPercent, boxMax.y + barPad + barThick + 1.f + stackShift);
        break;
    case EspHealthBarLeft:
    default:
        *outlineMin = ImVec2(boxMin.x - barPad - barThick - 2.f - stackShift, boxMin.y - 1.f);
        *outlineMax = ImVec2(boxMin.x - barPad - stackShift, boxMax.y + 1.f);
        *fillMin = ImVec2(boxMin.x - barPad - barThick - 1.f - stackShift, boxMax.y - boxH * hpPercent);
        *fillMax = ImVec2(boxMin.x - barPad - 1.f - stackShift, boxMax.y);
        break;
    }
}

inline ImU32 EspRgb(const float color[3], int alpha = 255)
{
    if (!color)
        return IM_COL32(255, 255, 255, alpha);
    return IM_COL32(
        (int)(color[0] * 255.f),
        (int)(color[1] * 255.f),
        (int)(color[2] * 255.f),
        alpha);
}

inline ImU32 EspRgba(const float color[4], float opacity = 1.f)
{
    auto sat = [](float v) noexcept {
        return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
    };
    if (!color)
        return IM_COL32(255, 255, 255, (int)(255.f * sat(opacity) + 0.5f));
    return IM_COL32(
        (int)(color[0] * 255.f),
        (int)(color[1] * 255.f),
        (int)(color[2] * 255.f),
        (int)(sat(color[3]) * 255.f * sat(opacity) + 0.5f));
}

inline ImU32 EspHealthColor(float hpPercent, int alpha = 255)
{
    if (hpPercent < 0.f) hpPercent = 0.f;
    if (hpPercent > 1.f) hpPercent = 1.f;
    return IM_COL32(
        (int)(255.f * (1.f - hpPercent)),
        (int)(255.f * hpPercent),
        0,
        alpha);
}

inline ImU32 EspArmorGradientColor(float armorPercent, int alpha = 255)
{
    if (armorPercent < 0.f) armorPercent = 0.f;
    if (armorPercent > 1.f) armorPercent = 1.f;
    // Light blue -> deep blue
    const int r = (int)(40.f + 80.f * (1.f - armorPercent));
    const int g = (int)(120.f + 80.f * armorPercent);
    const int b = (int)(200.f + 55.f * armorPercent);
    return IM_COL32(r, g, b > 255 ? 255 : b, alpha);
}

inline void EspDrawCornerBox(
    ImDrawList* draw,
    const ImVec2& boxMin,
    const ImVec2& boxMax,
    ImU32 color,
    float thickness = 1.5f)
{
    if (!draw)
        return;
    const float w = boxMax.x - boxMin.x;
    const float h = boxMax.y - boxMin.y;
    float len = (w < h ? w : h) * 0.28f;
    if (len < 4.f) len = 4.f;
    if (len > 18.f) len = 18.f;

    // Top-left
    draw->AddLine(boxMin, ImVec2(boxMin.x + len, boxMin.y), color, thickness);
    draw->AddLine(boxMin, ImVec2(boxMin.x, boxMin.y + len), color, thickness);
    // Top-right
    draw->AddLine(ImVec2(boxMax.x, boxMin.y), ImVec2(boxMax.x - len, boxMin.y), color, thickness);
    draw->AddLine(ImVec2(boxMax.x, boxMin.y), ImVec2(boxMax.x, boxMin.y + len), color, thickness);
    // Bottom-left
    draw->AddLine(ImVec2(boxMin.x, boxMax.y), ImVec2(boxMin.x + len, boxMax.y), color, thickness);
    draw->AddLine(ImVec2(boxMin.x, boxMax.y), ImVec2(boxMin.x, boxMax.y - len), color, thickness);
    // Bottom-right
    draw->AddLine(boxMax, ImVec2(boxMax.x - len, boxMax.y), color, thickness);
    draw->AddLine(boxMax, ImVec2(boxMax.x, boxMax.y - len), color, thickness);
}

inline void EspDrawBoxStyled(
    ImDrawList* draw,
    const ImVec2& boxMin,
    const ImVec2& boxMax,
    ImU32 outlineColor,
    int style,
    ImU32 fillColor = IM_COL32(0, 0, 0, 0),
    float thickness = 1.5f)
{
    if (!draw)
        return;
    switch (style) {
    case EspBoxCorners:
        EspDrawCornerBox(draw, boxMin, boxMax, outlineColor, thickness);
        break;
    case EspBoxFilled:
        draw->AddRectFilled(boxMin, boxMax, fillColor);
        draw->AddRect(boxMin, boxMax, outlineColor, 0.f, 0, thickness);
        break;
    case EspBoxFull:
    default:
        draw->AddRect(boxMin, boxMax, outlineColor, 0.f, 0, thickness);
        break;
    }
}

inline void EspDrawBarStyled(
    ImDrawList* draw,
    const ImVec2& boxMin,
    const ImVec2& boxMax,
    float percent,
    int position,
    int style,
    const float solidColor[3],
    bool armor,
    int stackIndex = 0,
    int alpha = 255,
    ImVec2 offset = ImVec2(0.f, 0.f))
{
    if (!draw)
        return;
    if (percent < 0.f) percent = 0.f;
    if (percent > 1.f) percent = 1.f;

    ImVec2 fillMin, fillMax, outlineMin, outlineMax;
    EspComputeHealthBarRects(
        boxMin, boxMax, percent, position,
        &fillMin, &fillMax, &outlineMin, &outlineMax, stackIndex);
    fillMin = ImVec2(fillMin.x + offset.x, fillMin.y + offset.y);
    fillMax = ImVec2(fillMax.x + offset.x, fillMax.y + offset.y);
    outlineMin = ImVec2(outlineMin.x + offset.x, outlineMin.y + offset.y);
    outlineMax = ImVec2(outlineMax.x + offset.x, outlineMax.y + offset.y);

    // Background track
    draw->AddRectFilled(
        outlineMin, outlineMax, IM_COL32(0, 0, 0, (alpha * 180) / 255), 1.5f);

    const bool vertical =
        position == EspHealthBarLeft || position == EspHealthBarRight;

    // EspBarValue uses the same fill as Solid; EspBarText is handled by overlay.
    if (style == EspBarGradient)
    {
        const ImU32 low = armor
            ? EspArmorGradientColor(0.f, alpha)
            : EspHealthColor(0.f, alpha);
        const ImU32 high = armor
            ? EspArmorGradientColor(1.f, alpha)
            : EspHealthColor(1.f, alpha);
        if (vertical)
        {
            draw->AddRectFilledMultiColor(
                fillMin, fillMax, low, low, high, high);
        }
        else
        {
            draw->AddRectFilledMultiColor(
                fillMin, fillMax, low, high, high, low);
        }
    }
    else if (style == EspBarDynamic)
    {
        const ImU32 col = armor
            ? EspArmorGradientColor(percent, alpha)
            : EspHealthColor(percent, alpha);
        draw->AddRectFilled(fillMin, fillMax, col, 1.f);
    }
    else
    {
        draw->AddRectFilled(fillMin, fillMax, EspRgb(solidColor, alpha), 1.f);
    }
    draw->AddRect(outlineMin, outlineMax, IM_COL32(0, 0, 0, alpha), 1.5f);
}

inline void EspDrawHealthBarAt(
    ImDrawList* draw,
    const ImVec2& boxMin,
    const ImVec2& boxMax,
    float hpPercent,
    int position,
    ImVec2 offset,
    int alpha = 255)
{
    static const float kGreen[3] = { 0.f, 1.f, 0.f };
    EspDrawBarStyled(
        draw, boxMin, boxMax, hpPercent, position,
        EspBarGradient, kGreen, false, 0, alpha, offset);
}

inline void EspDrawHealthBar(
    ImDrawList* draw,
    const ImVec2& boxMin,
    const ImVec2& boxMax,
    float hpPercent,
    int position)
{
    EspDrawHealthBarAt(draw, boxMin, boxMax, hpPercent, position, ImVec2(0.f, 0.f), 255);
}

inline void EspDrawSkeletonGlow(
    ImDrawList* draw,
    const ImVec2* points,
    const int* pairs,
    int pairCount,
    ImU32 color,
    float strength = 0.65f)
{
    if (!draw || !points || !pairs || pairCount <= 0)
        return;
    if (strength < 0.05f) strength = 0.05f;
    if (strength > 1.f) strength = 1.f;

    const int cr = (color >> IM_COL32_R_SHIFT) & 0xFF;
    const int cg = (color >> IM_COL32_G_SHIFT) & 0xFF;
    const int cb = (color >> IM_COL32_B_SHIFT) & 0xFF;

    // Soft bloom: wide transparent passes first, then tighter core.
    // Extra outer pass approximates native silhouette glow falloff.
    static const float kWidths[] = { 22.f, 14.f, 9.f, 5.5f, 3.f, 1.6f };
    static const float kAlphas[] = { 0.06f, 0.10f, 0.18f, 0.32f, 0.55f, 0.85f };
    for (int pass = 0; pass < 6; ++pass)
    {
        const int a = (int)(kAlphas[pass] * strength * 255.f);
        if (a <= 0)
            continue;
        const ImU32 col = IM_COL32(cr, cg, cb, a);
        const float width = kWidths[pass] * (0.75f + 0.5f * strength);
        for (int i = 0; i < pairCount; ++i)
        {
            const int aIdx = pairs[i * 2];
            const int bIdx = pairs[i * 2 + 1];
            draw->AddLine(points[aIdx], points[bIdx], col, width);
        }
    }
}

// Native-style silhouette (RE: silhouettelayer → PostFX edge), OBS-safe ImGui.
// Full body contour: thick limb capsules + densified outer hull (not sparse bone hull).
inline void EspDrawSilhouetteGlow(
    ImDrawList* draw,
    const ImVec2* points,
    const bool* ok,
    int count,
    ImU32 color,
    float strength = 0.65f)
{
    if (!draw || !points || !ok || count < 3)
        return;
    if (strength < 0.05f) strength = 0.05f;
    if (strength > 1.f) strength = 1.f;

    static const int kPairs[] = {
        0, 1, 1, 2, 2, 3,
        1, 4, 4, 5, 5, 6,
        1, 7, 7, 8, 8, 9,
        3, 10, 10, 11, 11, 12,
        3, 13, 13, 14, 14, 15
    };
    const int pairCount =
        static_cast<int>(sizeof(kPairs) / sizeof(kPairs[0]) / 2);

    const int cr = (color >> IM_COL32_R_SHIFT) & 0xFF;
    const int cg = (color >> IM_COL32_G_SHIFT) & 0xFF;
    const int cb = (color >> IM_COL32_B_SHIFT) & 0xFF;

    auto limbThickness = [](int a, int b) -> float {
        // Torso thicker than limbs; head-neck medium.
        if ((a <= 3 && b <= 3))
            return 14.f;
        if (a == 0 || b == 0)
            return 12.f;
        return 9.f;
    };

    // 1) Capsule passes along every bone segment — covers full limb contour.
    static const float kCapMul[] = { 1.55f, 1.20f, 0.95f, 0.70f, 0.45f };
    static const float kCapAlpha[] = { 0.10f, 0.18f, 0.32f, 0.52f, 0.80f };
    for (int pass = 0; pass < 5; ++pass)
    {
        const int a = (int)(kCapAlpha[pass] * strength * 255.f);
        if (a <= 0)
            continue;
        const ImU32 col = IM_COL32(cr, cg, cb, a);
        for (int i = 0; i < pairCount; ++i)
        {
            const int ia = kPairs[i * 2];
            const int ib = kPairs[i * 2 + 1];
            if (ia >= count || ib >= count || !ok[ia] || !ok[ib])
                continue;
            const float w =
                limbThickness(ia, ib) * kCapMul[pass] *
                (0.75f + 0.5f * strength);
            draw->AddLine(points[ia], points[ib], col, w);
        }
        if (count > 0 && ok[0])
            draw->AddCircleFilled(
                points[0],
                6.f * kCapMul[pass] * (0.75f + 0.4f * strength),
                col,
                16);
    }

    // 2) Densify: samples along limbs + perpendicular offsets → outer hull.
    ImVec2 cloud[192]{};
    int n = 0;
    auto pushPt = [&](float x, float y) {
        if (n >= 192)
            return;
        cloud[n++] = ImVec2(x, y);
    };
    for (int i = 0; i < count && n < 192; ++i)
    {
        if (ok[i])
            pushPt(points[i].x, points[i].y);
    }
    for (int i = 0; i < pairCount; ++i)
    {
        const int ia = kPairs[i * 2];
        const int ib = kPairs[i * 2 + 1];
        if (ia >= count || ib >= count || !ok[ia] || !ok[ib])
            continue;
        const float dx = points[ib].x - points[ia].x;
        const float dy = points[ib].y - points[ia].y;
        const float len = sqrtf(dx * dx + dy * dy);
        if (len < 0.5f)
            continue;
        const float nx = -dy / len;
        const float ny = dx / len;
        const float halfW = limbThickness(ia, ib) * 0.45f *
            (0.75f + 0.5f * strength);
        constexpr int kSamples = 4;
        for (int s = 0; s <= kSamples; ++s)
        {
            const float t = (float)s / (float)kSamples;
            const float px = points[ia].x + dx * t;
            const float py = points[ia].y + dy * t;
            pushPt(px, py);
            pushPt(px + nx * halfW, py + ny * halfW);
            pushPt(px - nx * halfW, py - ny * halfW);
        }
    }
    if (count > 0 && ok[0])
    {
        const float hr = 8.f * (0.75f + 0.5f * strength);
        for (int k = 0; k < 8; ++k)
        {
            const float ang = (float)k * 6.2831853f / 8.f;
            pushPt(
                points[0].x + cosf(ang) * hr,
                points[0].y + sinf(ang) * hr);
        }
    }
    if (n < 3)
        return;

    int leftmost = 0;
    for (int i = 1; i < n; ++i)
    {
        if (cloud[i].x < cloud[leftmost].x ||
            (cloud[i].x == cloud[leftmost].x &&
             cloud[i].y < cloud[leftmost].y))
            leftmost = i;
    }
    int hullIdx[192]{};
    int h = 0;
    int p = leftmost;
    do
    {
        if (h >= 192)
            break;
        hullIdx[h++] = p;
        int q = (p + 1) % n;
        for (int i = 0; i < n; ++i)
        {
            const float cross =
                (cloud[q].x - cloud[p].x) * (cloud[i].y - cloud[p].y) -
                (cloud[q].y - cloud[p].y) * (cloud[i].x - cloud[p].x);
            if (cross < 0.f)
                q = i;
            else if (cross == 0.f)
            {
                const float dq =
                    (cloud[q].x - cloud[p].x) * (cloud[q].x - cloud[p].x) +
                    (cloud[q].y - cloud[p].y) * (cloud[q].y - cloud[p].y);
                const float di =
                    (cloud[i].x - cloud[p].x) * (cloud[i].x - cloud[p].x) +
                    (cloud[i].y - cloud[p].y) * (cloud[i].y - cloud[p].y);
                if (di > dq)
                    q = i;
            }
        }
        p = q;
    } while (p != leftmost && h < 192);
    if (h < 3)
        return;

    ImVec2 hullPts[192]{};
    ImVec2 c(0.f, 0.f);
    for (int i = 0; i < h; ++i)
    {
        hullPts[i] = cloud[hullIdx[i]];
        c.x += hullPts[i].x;
        c.y += hullPts[i].y;
    }
    c.x /= (float)h;
    c.y /= (float)h;
    const float inflate = 2.5f + 3.5f * strength;
    for (int i = 0; i < h; ++i)
    {
        float dx = hullPts[i].x - c.x;
        float dy = hullPts[i].y - c.y;
        const float len = sqrtf(dx * dx + dy * dy);
        if (len > 0.01f)
        {
            hullPts[i].x += dx / len * inflate;
            hullPts[i].y += dy / len * inflate;
        }
    }

    draw->AddConvexPolyFilled(
        hullPts,
        h,
        IM_COL32(cr, cg, cb, (int)(22.f * strength)));

    static const float kEdgeW[] = { 8.f, 5.f, 3.f, 1.6f };
    static const float kEdgeA[] = { 0.16f, 0.30f, 0.55f, 0.90f };
    for (int pass = 0; pass < 4; ++pass)
    {
        const int a = (int)(kEdgeA[pass] * strength * 255.f);
        if (a <= 0)
            continue;
        draw->AddPolyline(
            hullPts,
            h,
            IM_COL32(cr, cg, cb, a),
            ImDrawFlags_Closed,
            kEdgeW[pass] * (0.7f + 0.5f * strength));
    }
}

inline void EspDrawBoxGlow(
    ImDrawList* draw,
    const ImVec2& boxMin,
    const ImVec2& boxMax,
    ImU32 color,
    float strength = 0.65f)
{
    if (!draw)
        return;
    if (strength < 0.05f) strength = 0.05f;
    if (strength > 1.f) strength = 1.f;

    const int cr = (color >> IM_COL32_R_SHIFT) & 0xFF;
    const int cg = (color >> IM_COL32_G_SHIFT) & 0xFF;
    const int cb = (color >> IM_COL32_B_SHIFT) & 0xFF;

    static const float kPads[] = { 10.f, 6.f, 3.f };
    static const float kAlphas[] = { 0.12f, 0.22f, 0.40f };
    for (int pass = 0; pass < 3; ++pass)
    {
        const int a = (int)(kAlphas[pass] * strength * 255.f);
        if (a <= 0)
            continue;
        const float pad = kPads[pass];
        draw->AddRect(
            ImVec2(boxMin.x - pad, boxMin.y - pad),
            ImVec2(boxMax.x + pad, boxMax.y + pad),
            IM_COL32(cr, cg, cb, a),
            2.f,
            0,
            2.5f + pass);
    }
}

inline void EspDrawHealthBarUserColor(
    ImDrawList* draw,
    const ImVec2& boxMin,
    const ImVec2& boxMax,
    float hpPercent,
    int position,
    const float userColor[3],
    int alpha = 255)
{
    EspDrawBarStyled(
        draw, boxMin, boxMax, hpPercent, position,
        EspBarSolid, userColor, false, 0, alpha);
}
