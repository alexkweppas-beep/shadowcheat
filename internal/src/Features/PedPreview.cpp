#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "PedPreview.h"
#include "ModelPreview.h"
#include "RagePlayerNames.h"
#include "RuntimeProfiler.h"
#include "../Menu/EspProjectedOverlay.h"
#include "../Menu/EspDrawHelpers.h"
#include "../Menu/MenuTexture.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>

namespace PedPreview
{
namespace
{
    std::atomic<bool> g_WantActive{false};

    float g_RotY = 0.f;
    float g_RotX = 0.f;
    EspProjected::ElementId g_Dragging = EspProjected::ElementId::None;
    ImVec2 g_DragOffset{};
    EspProjected::OverlayRects g_LastRects{};
    float g_HoverAnim[EspProjected::kElementCount]{};

    enum class SettingsKind { None, Box, Skeleton, Element, Help };
    struct SettingsPopup
    {
        bool open = false;
        bool justOpened = false;
        SettingsKind kind = SettingsKind::None;
        EspProjected::ElementId element = EspProjected::ElementId::None;
        ImVec2 pos{};
    } g_Settings{};

    bool IsFinite2(const ImVec2& v) noexcept
    {
        return std::isfinite(v.x) && std::isfinite(v.y);
    }

    // Right half of a front facing body outline, from the top of the head down
    // the arm, around the hand, back up to the armpit, then down the torso and
    // leg to the sole and inwards to the crotch. Units are fractions of the
    // figure height; the left half is mirrored at runtime so the silhouette is
    // symmetric by construction.
    const ImVec2 kSilhouetteHalf[] = {
        { 0.000f, 0.000f },
        { 0.028f, 0.012f },
        { 0.045f, 0.035f },
        { 0.046f, 0.075f },
        { 0.038f, 0.105f },
        { 0.032f, 0.128f },
        { 0.033f, 0.152f },
        { 0.062f, 0.165f },
        { 0.092f, 0.182f },
        { 0.118f, 0.200f },
        { 0.128f, 0.218f },
        { 0.130f, 0.252f },
        { 0.130f, 0.292f },
        { 0.128f, 0.330f },
        { 0.138f, 0.370f },
        { 0.144f, 0.410f },
        { 0.147f, 0.450f },
        { 0.154f, 0.490f },
        { 0.174f, 0.528f },
        { 0.168f, 0.566f },
        { 0.146f, 0.600f },
        { 0.128f, 0.556f },
        { 0.121f, 0.492f },
        { 0.107f, 0.450f },
        { 0.094f, 0.410f },
        { 0.088f, 0.370f },
        { 0.085f, 0.332f },
        { 0.086f, 0.300f },
        { 0.079f, 0.332f },
        { 0.073f, 0.370f },
        { 0.077f, 0.410f },
        { 0.086f, 0.450f },
        { 0.094f, 0.490f },
        { 0.098f, 0.530f },
        { 0.101f, 0.578f },
        { 0.102f, 0.624f },
        { 0.097f, 0.672f },
        { 0.100f, 0.712f },
        { 0.104f, 0.762f },
        { 0.108f, 0.822f },
        { 0.104f, 0.888f },
        { 0.114f, 0.948f },
        { 0.144f, 0.990f },
        { 0.139f, 1.000f },
        { 0.094f, 1.000f },
        { 0.081f, 0.950f },
        { 0.075f, 0.898f },
        { 0.056f, 0.824f },
        { 0.052f, 0.752f },
        { 0.036f, 0.684f },
        { 0.021f, 0.624f },
        { 0.011f, 0.596f },
        { 0.000f, 0.572f }
    };

    // The stage shows a neutral grey body instead of the 3D ped: it reads
    // instantly at preview size and keeps the ESP elements the subject. The
    // figure is fitted to the projected ped box, so it always stands exactly
    // where the box and the anchored elements expect a player to be.
    void DrawSilhouette(
        ImDrawList* draw,
        const ImVec2& boxMin,
        const ImVec2& boxMax,
        int alpha) noexcept
    {
        const float height = boxMax.y - boxMin.y;
        if (!draw || alpha <= 2 || height <= 16.f)
            return;
        if (!IsFinite2(boxMin) || !IsFinite2(boxMax))
            return;

        const ImU32 tint = IM_COL32(0x1E, 0x1E, 0x1E, alpha);
        unsigned texW = 0;
        unsigned texH = 0;
        if (void* texture =
                MenuTexture::GetSilhouetteImGui(&texW, &texH))
        {
            if (texW > 0 && texH > 0)
            {
                const float figureW =
                    height * static_cast<float>(texW) /
                    static_cast<float>(texH);
                const float centerX =
                    (boxMin.x + boxMax.x) * 0.5f;
                draw->AddImage(
                    reinterpret_cast<ImTextureID>(texture),
                    ImVec2(centerX - figureW * 0.5f, boxMin.y),
                    ImVec2(centerX + figureW * 0.5f, boxMax.y),
                    ImVec2(0.f, 0.f),
                    ImVec2(1.f, 1.f),
                    tint);
                return;
            }
        }

        // Traced outline fallback for the rare frame where the texture is not
        // available yet.
        constexpr int kHalfCount =
            static_cast<int>(
                sizeof(kSilhouetteHalf) / sizeof(kSilhouetteHalf[0]));
        const float centerX = (boxMin.x + boxMax.x) * 0.5f;
        const auto map =
            [&](float nx, float ny) noexcept {
                return ImVec2(
                    centerX + nx * height,
                    boxMin.y + ny * height);
            };

        ImVec2 outline[kHalfCount * 2]{};
        int count = 0;
        for (int i = 0; i < kHalfCount; ++i)
        {
            outline[count++] = map(
                kSilhouetteHalf[i].x,
                kSilhouetteHalf[i].y);
        }
        for (int i = kHalfCount - 1; i >= 0; --i)
        {
            // Points on the axis are shared by both halves.
            if (kSilhouetteHalf[i].x < 0.002f)
                continue;
            outline[count++] = map(
                -kSilhouetteHalf[i].x,
                kSilhouetteHalf[i].y);
        }

        draw->AddConcavePolyFilled(outline, count, tint);
    }

    // Skeleton for the silhouette. Drawn from the same normalised figure so the
    // bones sit inside the body instead of following the old preview rig.
    void DrawSilhouetteSkeleton(
        ImDrawList* draw,
        const ImVec2& boxMin,
        const ImVec2& boxMax,
        ImU32 color,
        float thickness) noexcept
    {
        const float height = boxMax.y - boxMin.y;
        if (!draw || height <= 16.f)
            return;
        if (!IsFinite2(boxMin) || !IsFinite2(boxMax))
            return;

        const float centerX = (boxMin.x + boxMax.x) * 0.5f;
        const auto at =
            [&](float nx, float ny) noexcept {
                return ImVec2(
                    centerX + nx * height,
                    boxMin.y + ny * height);
            };
        const ImVec2 head = at(0.f, 0.062f);
        const ImVec2 neck = at(0.f, 0.158f);
        const ImVec2 chest = at(0.f, 0.300f);
        const ImVec2 pelvis = at(0.f, 0.520f);

        const auto bone =
            [&](const ImVec2& a, const ImVec2& b) noexcept {
                draw->AddLine(a, b, color, thickness);
            };
        bone(head, neck);
        bone(neck, chest);
        bone(chest, pelvis);

        // Per side rather than mirrored: the artwork's legs are not symmetric,
        // and a mirrored rig walked out of the left leg near the ankle.
        struct SideJoints final
        {
            float shoulderX;
            float elbowX;
            float handX;
            float hipX;
            float kneeX;
            float footX;
        };
        const SideJoints sides[2] = {
            { -0.104f, -0.140f, -0.146f, -0.045f, -0.053f, -0.060f },
            {  0.104f,  0.140f,  0.146f,  0.050f,  0.063f,  0.072f }
        };

        for (const SideJoints& side : sides)
        {
            const ImVec2 shoulder = at(side.shoulderX, 0.206f);
            const ImVec2 elbow = at(side.elbowX, 0.390f);
            const ImVec2 hand = at(side.handX, 0.530f);
            const ImVec2 hip = at(side.hipX, 0.526f);
            const ImVec2 knee = at(side.kneeX, 0.720f);
            const ImVec2 foot = at(side.footX, 0.950f);
            bone(neck, shoulder);
            bone(shoulder, elbow);
            bone(elbow, hand);
            bone(pelvis, hip);
            bone(hip, knee);
            bone(knee, foot);
        }
    }

    bool ComputeProjectedCanonicalBox(
        const ImVec2& modelMin,
        const ImVec2& modelMax,
        float rotationY,
        float rotationX,
        float viewScale,
        ImVec2& boxMin,
        ImVec2& boxMax) noexcept
    {
        using Bone = RagePlayerNames::BoneSlot;
        constexpr int kBoneCount =
            static_cast<int>(Bone::Count);
        constexpr Bone kRequiredBones[] = {
            Bone::Head,
            Bone::LeftHand,
            Bone::RightHand,
            Bone::LeftFoot,
            Bone::RightFoot
        };
        constexpr float kSilhouettePadding = 0.10f;
        constexpr float kMinimumWidthRatio = 0.42f;

        const float modelWidth =
            modelMax.x - modelMin.x;
        const float modelHeight =
            modelMax.y - modelMin.y;
        if (!IsFinite2(modelMin) ||
            !IsFinite2(modelMax) ||
            modelWidth <= 0.f ||
            modelHeight <= 0.f)
        {
            return false;
        }

        ImVec2 canonical[kBoneCount]{};
        bool available[kBoneCount]{};
        if (ModelPreview::ProjectCanonicalBones(
                modelMin,
                modelMax,
                rotationY,
                rotationX,
                viewScale,
                canonical,
                available,
                kBoneCount) <
            static_cast<int>(
                sizeof(kRequiredBones) /
                sizeof(kRequiredBones[0])))
        {
            return false;
        }

        const float envelopeX =
            modelWidth * 0.25f;
        const float envelopeY =
            modelHeight * 0.25f;
        const auto usable =
            [&](int index) noexcept {
                if (index < 0 ||
                    index >= kBoneCount ||
                    !available[index] ||
                    !IsFinite2(canonical[index]))
                {
                    return false;
                }
                const ImVec2& point =
                    canonical[index];
                return
                    point.x >= modelMin.x - envelopeX &&
                    point.x <= modelMax.x + envelopeX &&
                    point.y >= modelMin.y - envelopeY &&
                    point.y <= modelMax.y + envelopeY;
            };

        for (const Bone bone : kRequiredBones) {
            if (!usable(static_cast<int>(bone)))
                return false;
        }

        const ImVec2& head =
            canonical[static_cast<int>(Bone::Head)];
        const ImVec2& leftFoot =
            canonical[static_cast<int>(
                Bone::LeftFoot)];
        const ImVec2& rightFoot =
            canonical[static_cast<int>(
                Bone::RightFoot)];
        const float feetY =
            (std::max)(leftFoot.y, rightFoot.y);
        const float minimumAnchorHeight =
            (std::max)(24.f, modelHeight * 0.25f);
        if (feetY - head.y < minimumAnchorHeight)
            return false;

        ImVec2 projectedMin = head;
        ImVec2 projectedMax = head;
        int includedCount = 0;
        for (int index = 0;
             index < kBoneCount;
             ++index)
        {
            if (!usable(index))
                continue;
            const ImVec2& point =
                canonical[index];
            projectedMin.x =
                (std::min)(projectedMin.x, point.x);
            projectedMin.y =
                (std::min)(projectedMin.y, point.y);
            projectedMax.x =
                (std::max)(projectedMax.x, point.x);
            projectedMax.y =
                (std::max)(projectedMax.y, point.y);
            ++includedCount;
        }
        if (includedCount <
            static_cast<int>(
                sizeof(kRequiredBones) /
                sizeof(kRequiredBones[0])))
        {
            return false;
        }

        const float projectedWidth =
            projectedMax.x - projectedMin.x;
        const float projectedHeight =
            projectedMax.y - projectedMin.y;
        if (!std::isfinite(projectedWidth) ||
            !std::isfinite(projectedHeight) ||
            projectedWidth < 0.f ||
            projectedHeight < minimumAnchorHeight ||
            projectedWidth > modelWidth * 1.5f ||
            projectedHeight > modelHeight * 1.5f)
        {
            return false;
        }

        const float paddingX =
            (std::max)(
                projectedWidth *
                    kSilhouettePadding,
                modelWidth * 0.015f);
        const float paddingY =
            (std::max)(
                projectedHeight *
                    kSilhouettePadding,
                modelHeight * 0.015f);
        boxMin = ImVec2(
            projectedMin.x - paddingX,
            projectedMin.y - paddingY);
        boxMax = ImVec2(
            projectedMax.x + paddingX,
            projectedMax.y + paddingY);

        const float paddedHeight =
            boxMax.y - boxMin.y;
        const float minimumWidth =
            (std::max)(
                paddedHeight *
                    kMinimumWidthRatio,
                modelWidth * 0.72f);
        const float paddedWidth =
            boxMax.x - boxMin.x;
        if (paddedWidth < minimumWidth) {
            const float centerX =
                (boxMin.x + boxMax.x) * 0.5f;
            boxMin.x =
                centerX - minimumWidth * 0.5f;
            boxMax.x =
                centerX + minimumWidth * 0.5f;
        }
        return IsFinite2(boxMin) &&
            IsFinite2(boxMax) &&
            boxMax.x > boxMin.x &&
            boxMax.y > boxMin.y;
    }

    void RenderSettingsPopup(Menu::EspProfileSettings& profile) noexcept
    {
        if (!g_Settings.open)
            return;

        if (g_Settings.justOpened) {
            ImGui::SetNextWindowPos(g_Settings.pos, ImGuiCond_Always);
            g_Settings.justOpened = false;
        }
        ImGui::SetNextWindowSize(ImVec2(220.f, 0.f), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 10.f));
        const bool visible = ImGui::Begin(
            "##esp_preview_settings",
            &g_Settings.open,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_AlwaysAutoResize);
        if (visible) {
            if (g_Settings.kind == SettingsKind::Box) {
                ImGui::TextUnformatted("Box");
                ImGui::ColorEdit4("Color", profile.boxColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                ImGui::ColorEdit4("Fill", profile.boxFillColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                ImGui::Combo(
                    "Style", &profile.boxStyle,
                    "Full\0Corners\0Filled\0");
                ImGui::SliderFloat(
                    "Rounding", &profile.boxRounding, 0.f, 16.f, "%.1f");
                ImGui::SliderFloat(
                    "Thickness", &profile.boxThickness, 0.5f, 8.f, "%.1f");
            } else if (g_Settings.kind == SettingsKind::Skeleton) {
                ImGui::TextUnformatted("Skeleton");
                ImGui::ColorEdit4(
                    "Color",
                    profile.skeletonColor,
                    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                ImGui::SliderFloat(
                    "Thickness",
                    &profile.skeletonThickness,
                    0.5f,
                    8.f,
                    "%.1f");
            } else if (g_Settings.kind == SettingsKind::Element) {
                ImGui::TextUnformatted(
                    EspProjected::ElementLabel(g_Settings.element));
                const int idx = EspProjected::IndexOf(g_Settings.element);
                if (idx >= 0) {
                    float* color = nullptr;
                    switch (g_Settings.element) {
                    case EspProjected::ElementId::Name: color = profile.nameColor; break;
                    case EspProjected::ElementId::Faction: color = profile.factionColor; break;
                    case EspProjected::ElementId::Family: color = profile.familyColor; break;
                    case EspProjected::ElementId::Distance: color = profile.distanceColor; break;
                    case EspProjected::ElementId::Weapon: color = profile.weaponColor; break;
                    case EspProjected::ElementId::Health: color = profile.healthColor; break;
                    case EspProjected::ElementId::Armor: color = profile.armorColor; break;
                    case EspProjected::ElementId::DynamicId: color = profile.dynamicIdColor; break;
                    case EspProjected::ElementId::StaticId: color = profile.staticIdColor; break;
                    default: break;
                    }
                    if (color)
                        ImGui::ColorEdit4(
                            "Color",
                            color,
                            ImGuiColorEditFlags_NoInputs |
                                ImGuiColorEditFlags_AlphaBar);
                    ImGui::SliderFloat(
                        "Size", &profile.layouts[idx].size, 0.5f, 2.f, "%.2f");
                    if (g_Settings.element == EspProjected::ElementId::Health)
                        ImGui::Checkbox("Show value", &profile.healthShowValue);
                    if (g_Settings.element == EspProjected::ElementId::Armor)
                        ImGui::Checkbox("Show value", &profile.armorShowValue);
                    if (g_Settings.element == EspProjected::ElementId::Health)
                        ImGui::Combo(
                            "Style", &profile.healthStyle,
                            "Solid\0Gradient\0Bar + value\0Text\0Dynamic\0");
                    if (g_Settings.element == EspProjected::ElementId::Armor)
                        ImGui::Combo(
                            "Style", &profile.armorStyle,
                            "Solid\0Gradient\0Bar + value\0Text\0Dynamic\0");
                    if (g_Settings.element == EspProjected::ElementId::Weapon)
                        ImGui::Combo(
                            "Display", &profile.weaponDisplay,
                            "Text\0Icon\0Text + Icon\0");
                }
            } else if (g_Settings.kind == SettingsKind::Help) {
                ImGui::TextUnformatted("Preview");
                ImGui::TextDisabled("Drag tags to reposition");
                ImGui::TextDisabled("Mouse wheel to scale");
                ImGui::TextDisabled("Right-click an element for style");
            }
            if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) &&
                !ImGui::IsAnyItemActive() &&
                (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                 ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
            {
                g_Settings.open = false;
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }
}

void SetActive(bool active) noexcept
{
    g_WantActive.store(active);
}

bool IsActive() noexcept
{
    return g_WantActive.load();
}

void OpenSettings(const ImVec2& pos) noexcept
{
    g_Settings = {
        true,
        true,
        SettingsKind::Help,
        EspProjected::ElementId::None,
        pos
    };
}

void CaptureAndSanitizeBackbuffer(
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    IDXGISwapChain* swap) noexcept
{
    (void)device;
    (void)context;
    (void)swap;
}

void RenderIntoScene(
    ImDrawList* draw,
    const ImVec2& sceneMin,
    const ImVec2& sceneMax,
    Menu::EspProfileSettings& profile,
    bool interactive) noexcept
{
    if (!draw)
        return;
    if (!IsFinite2(sceneMin) || !IsFinite2(sceneMax))
        return;
    if ((sceneMax.x - sceneMin.x) < 32.f ||
        (sceneMax.y - sceneMin.y) < 32.f)
        return;

    EspProjected::EnsureLayoutsMigrated(profile);
    draw->PushClipRect(sceneMin, sceneMax, true);

    constexpr float kAspect = 1.18f / 2.38f;
    constexpr float kModelScale = 0.80f;
    const float viewW = sceneMax.x - sceneMin.x;
    const float viewH = sceneMax.y - sceneMin.y;
    const float topShift = (std::min)(48.f, viewH * 0.12f);
    float previewH = viewH - topShift - 20.f;
    float previewW = previewH * kAspect;
    if (previewW > viewW) {
        previewW = viewW;
        previewH = previewW / kAspect;
    }
    const ImVec2 previewMin(
        sceneMin.x + (viewW - previewW) * 0.5f,
        sceneMin.y + topShift);
    const ImVec2 previewMax(
        previewMin.x + previewW,
        previewMin.y + previewH);
    const float modelYAdj = previewH * 0.035f;
    const ImVec2 modelMin(previewMin.x, previewMin.y - modelYAdj);
    const ImVec2 modelMax(previewMax.x, previewMax.y - modelYAdj);

    // The box wraps the silhouette, so its width comes from the artwork rather
    // than the rig that no longer gets drawn: a rig aspect made it far wider
    // than the body.
    float figureAspect = 0.334f;
    {
        unsigned figureW = 0;
        unsigned figureH = 0;
        if (MenuTexture::GetSilhouetteImGui(&figureW, &figureH) &&
            figureW > 0 && figureH > 0)
        {
            figureAspect =
                static_cast<float>(figureW) /
                static_cast<float>(figureH);
        }
    }
    const float boxTop = previewMin.y + previewH * 0.06f;
    const float boxH = previewH * 0.80f;
    const float boxW = boxH * figureAspect;
    const float boxCenterX = previewMin.x + previewW * 0.5f;
    const ImVec2 boxMin(boxCenterX - boxW * 0.5f, boxTop);
    const ImVec2 boxMax(boxCenterX + boxW * 0.5f, boxTop + boxH);

    ImGui::SetCursorScreenPos(sceneMin);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##esp_preview_drag", ImVec2(viewW, viewH));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;

    EspProjected::ElementId hoveredEl = EspProjected::ElementId::None;
    if (hovered)
        hoveredEl = EspProjected::HitTest(g_LastRects, mouse);

    if (interactive && hovered && hoveredEl != EspProjected::ElementId::None) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (std::fabs(wheel) > 0.001f &&
            EspProjected::IsScalable(hoveredEl))
        {
            const int idx = EspProjected::IndexOf(hoveredEl);
            if (idx >= 0) {
                profile.layouts[idx].size = std::clamp(
                    profile.layouts[idx].size + wheel * EspProjected::kSizeStep,
                    0.5f, 2.f);
            }
        }
    }

    auto boxEdgeContains = [&](const ImVec2& pos) {
        const float pad = 5.f;
        const ImRect outer(boxMin - ImVec2(pad, pad), boxMax + ImVec2(pad, pad));
        const ImRect inner(boxMin + ImVec2(pad, pad), boxMax - ImVec2(pad, pad));
        return outer.Contains(pos) && !inner.Contains(pos);
    };

    // The wheel already resizes whatever it is pointing at, so it keeps that
    // meaning here: on the frame it thickens the frame, inside it thickens the
    // skeleton. Same gesture as right-click, which opens the settings for
    // whichever of the two the cursor is over.
    if (interactive && hovered &&
        hoveredEl == EspProjected::ElementId::None) {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (std::fabs(wheel) > 0.001f) {
            constexpr float kThicknessStep = 0.2f;
            if (profile.box && boxEdgeContains(mouse)) {
                profile.boxThickness = std::clamp(
                    profile.boxThickness + wheel * kThicknessStep,
                    0.5f,
                    8.f);
            } else if (profile.skeleton &&
                       ImRect(boxMin, boxMax).Contains(mouse)) {
                profile.skeletonThickness = std::clamp(
                    profile.skeletonThickness + wheel * kThicknessStep,
                    0.5f,
                    8.f);
            }
        }
    }

    if (interactive && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (hoveredEl != EspProjected::ElementId::None) {
            g_Settings = { true, true, SettingsKind::Element, hoveredEl, mouse };
        } else if (profile.box && boxEdgeContains(mouse)) {
            g_Settings = { true, true, SettingsKind::Box, EspProjected::ElementId::None, mouse };
        } else if (profile.skeleton &&
                   ImRect(boxMin, boxMax).Contains(mouse)) {
            g_Settings = { true, true, SettingsKind::Skeleton, EspProjected::ElementId::None, mouse };
        }
    }

    if (interactive && hovered &&
        g_Dragging == EspProjected::ElementId::None &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        hoveredEl != EspProjected::ElementId::None)
    {
        g_Dragging = hoveredEl;
        const int idx = EspProjected::IndexOf(hoveredEl);
        // Bars ride centred on the cursor, so they need no grab offset; tags
        // keep the point they were grabbed by.
        if (EspProjected::IsBar(hoveredEl))
            g_DragOffset = ImVec2(0.f, 0.f);
        else if (idx >= 0 && g_LastRects.elements[idx].visible)
            g_DragOffset = g_LastRects.elements[idx].rect.Min - mouse;
        else
            g_DragOffset = ImVec2(0.f, 0.f);
    }

    EspProjected::DrawOptions opts{};
    if (g_Dragging != EspProjected::ElementId::None) {
        opts.dragActive = true;
        opts.dragElement = g_Dragging;
        opts.dragPos = mouse + g_DragOffset;
        const auto side = EspProjected::ResolveSide(boxMin, boxMax, mouse);
        const int order = EspProjected::ResolveOrder(
            g_LastRects, side, mouse, g_Dragging);
        EspProjected::SetLayout(profile, g_Dragging, side, order);
    }

    const float lerp = std::clamp(ImGui::GetIO().DeltaTime * 14.f, 0.f, 1.f);
    for (int i = 0; i < EspProjected::kElementCount; ++i) {
        const auto id = static_cast<EspProjected::ElementId>(i);
        const bool active =
            id == hoveredEl || id == g_Dragging ||
            (g_Settings.open && g_Settings.kind == SettingsKind::Element &&
             id == g_Settings.element);
        g_HoverAnim[i] = g_HoverAnim[i] + ((active ? 1.f : 0.f) - g_HoverAnim[i]) * lerp;
        opts.interactionAlpha[i] = g_HoverAnim[i];
    }

    ModelPreview::TickApplyCloudAsset();
    // Load is deferred from InitImGui; kick it when the preview is shown.
    // (Render() alone used to gate on IsLoaded — never reached LoadDefaultAsset.)
    if (ModelPreview::IsReady() &&
        !ModelPreview::IsLoaded() &&
        !ModelPreview::IsLoading() &&
        !ModelPreview::IsLoadFailed())
    {
        ModelPreview::LoadDefaultAsset();
    }

    // Honor ImGuiStyleVar_Alpha (menu open/close fade). Hardcoded IM_COL32
    // ignores style alpha, so the silhouette would linger at full opacity.
    const float styleA = ImClamp(ImGui::GetStyle().Alpha, 0.f, 1.f);
    const int tintA = static_cast<int>(styleA * 255.f + 0.5f);
    if (tintA <= 2)
    {
        draw->PopClipRect();
        return;
    }

    DrawSilhouette(draw, boxMin, boxMax, tintA);

    if (profile.skeleton && tintA > 2) {
        DrawSilhouetteSkeleton(
            draw,
            boxMin,
            boxMax,
            EspRgba(profile.skeletonColor, tintA / 255.f),
            profile.skeletonThickness);
    }

    EspProjected::PlayerData player{};
    player.name = "shadowcheats";
    player.faction = "Faction";
    player.weapon = "Heavy Rifle";
    player.weaponHash = 0xC78D71B4u; // Heavy Rifle
    player.distance = "12m";
    player.dynamicId = "ID:12";
    player.staticId = "#345";
    player.health = 0.72f;
    player.armor = 0.45f;
    player.greenZone = true;

    // Avoid double box: DrawPlayerOverlay draws box when profile.box.
    g_LastRects = EspProjected::DrawPlayerOverlay(
        draw, profile, player,
        ImRect(boxMin, boxMax), styleA, &opts);

    if (g_Dragging != EspProjected::ElementId::None &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        g_Dragging = EspProjected::ElementId::None;
    }

    draw->PopClipRect();

    if (interactive)
        RenderSettingsPopup(profile);
}

ID3D11ShaderResourceView* ModelSrv() noexcept
{
    return reinterpret_cast<ID3D11ShaderResourceView*>(ModelPreview::GetTexture());
}

void Shutdown() noexcept
{
    g_WantActive.store(false);
    g_Dragging = EspProjected::ElementId::None;
    g_Settings = {};
}

} // namespace PedPreview
