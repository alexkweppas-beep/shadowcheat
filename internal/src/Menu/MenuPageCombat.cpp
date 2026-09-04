#include "MenuPages.h"
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cfloat>

#define IMGUI_DEFINE_MATH_OPERATORS

#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"
#include "Leyawin/custom_widgets.hpp"
#include "Leyawin/leyawin_globals.h"
#include "Leyawin/leyawin_init.h"
#include "EspDrawHelpers.h"
#include "EspProjectedOverlay.h"
#include "MenuTexture.h"
#include "MenuUiHelpers.h"
#include "MenuState.h"
#include "MenuConfig.h"
#include "MenuSearch.h"
#include "MenuTheme.h"
#include "MenuType.h"
#include "MenuMotion.h"
#include "WeaponManager.h"
#include "Leyawin/leyawin_safe.h"
#include "IconsFontAwesome6.h"
#include "WeaponIconMap.h"
#include "../Features/Friends.h"
#include "../Features/MiscFeatures.h"
#include "../Features/QuantTools.h"
#include "../Features/UkraineTools.h"
#include "../Features/FiveRpFarmBot.h"
#include "../Features/RagePlayerNames.h"
#include "../Features/VehicleMovement.h"
#include "../Features/SilentAim.h"
#include "../Features/Aimbot.h"
#include "../Features/Damager.h"
#include "../Features/FiveRpMetadataResolver.h"
#include "../Features/RageV8Runtime.h"
#include "../Features/RuntimeProfiler.h"
#include "../Features/ScriptBridge.h"
#include "../Features/CloudStorage.h"
#include "../Features/FeatureBinds.h"
#include "../Features/ObsBypass.h"
#include "../Features/MeshSilhouette.h"
#include "../Features/PedPreview.h"
#include "../Main/Hooks.h"
#include "../SDK/Game.h"
#include "../SDK/RageBuildFlavor.h"
#include "../SDK/WeaponGroups.h"
#include "../SDK/WeaponCatalog.h"

#include <algorithm>
#include <cmath>
#include <ctime>


namespace Menu
{
    void RenderWeaponsPage()
    {
        WeaponManagerUi::Render();
    }

    static const char* AimPresetName() noexcept
    {
        if (s_AimPresetKind == AimPresetKind::Weapon)
        {
            if (const WeaponCatalog::Entry* weapon =
                    WeaponCatalog::FindByHash(s_AimPresetWeapon))
                return weapon->name;
            return T("Weapon", "Оружие", "Зброя");
        }
        if (s_AimPresetGroup <= 0)
            return T("All", "Все", "Усі");
        return WeaponGroups::GroupLabel(
            static_cast<WeaponGroups::Group>(s_AimPresetGroup),
            static_cast<int>(s_Language));
    }

    static const char* AimPresetIcon() noexcept
    {
        int group = s_AimPresetGroup;
        if (s_AimPresetKind == AimPresetKind::Weapon)
            group = AimGroupFromWeaponHash(s_AimPresetWeapon);
        switch (group)
        {
        case static_cast<int>(WeaponGroups::Group::Pistol):
            return ICON_FA_GUN;
        case static_cast<int>(WeaponGroups::Group::SMG):
            return ICON_FA_BURST;
        case static_cast<int>(WeaponGroups::Group::Rifle):
            return ICON_FA_PERSON_RIFLE;
        case static_cast<int>(WeaponGroups::Group::Shotgun):
            return ICON_FA_EXPLOSION;
        case static_cast<int>(WeaponGroups::Group::Sniper):
            return ICON_FA_CROSSHAIRS;
        case static_cast<int>(WeaponGroups::Group::MG):
            return ICON_FA_BOLT;
        default:
            return ICON_FA_LAYER_GROUP;
        }
    }

    static AimWeaponProfile* EditedAimProfile(
        WeaponOverrideSlot** outSlot = nullptr) noexcept
    {
        if (outSlot)
            *outSlot = nullptr;
        if (s_AimPresetKind == AimPresetKind::Weapon)
        {
            WeaponOverrideSlot* slot =
                FindWeaponOverride(s_AimPresetWeapon);
            if (!slot)
            {
                s_AimPresetKind = AimPresetKind::Group;
                s_AimPresetWeapon = 0;
                s_AimPresetGroup = 0;
                return &GetAimProfile(0);
            }
            if (outSlot)
                *outSlot = slot;
            return &slot->profile;
        }
        s_AimPresetGroup = ImClamp(
            s_AimPresetGroup,
            0,
            kAimWeaponGroupCount - 1);
        return &GetAimProfile(s_AimPresetGroup);
    }

    // Only changes which profile is being looked at.
    //
    // It used to copy General into the group and latch useCustom on, so a
    // single click on a weapon chip silently made that group's frozen copy the
    // one the aimbot reads, for good. Every later edit to General then did
    // nothing while that weapon was held: the FOV slider moved, the aim kept
    // the value from the moment of the click, and the bones did too. Turning a
    // group into its own configuration is now something asked for, on the
    // toggle inside the group.
    static void SelectAimGroup(int group) noexcept
    {
        s_AimPresetKind = AimPresetKind::Group;
        s_AimPresetGroup = ImClamp(
            group,
            0,
            kAimWeaponGroupCount - 1);
        s_AimPresetWeapon = 0;
        s_AimWeaponTab = s_AimPresetGroup;
    }

    static void DrawAimPresetSelectorV2() noexcept
    {
        struct DropdownState final
        {
            bool open = false;
            bool closing = false;
            float alpha = 0.f;
            float slide = -5.f;
            float width = 0.f;
            float height = 0.f;
            int openFrame = -1;
            std::uint32_t removingHash = 0;
            float removeProgress = 0.f;
            char headerName[96]{};
            const char* headerIcon = ICON_FA_LAYER_GROUP;
            ImVec2 popupMin{};
            ImVec2 popupMax{};
        };
        static DropdownState state{};

        const float scale = ImClamp(
            static_cast<float>(s_UIScalePercent) / 100.f,
            1.f,
            2.f);
        const ImVec2 closedSize(205.f * scale, 32.f * scale);
        const float targetWidth = 320.f * scale;
        const ImVec2 min = ImGui::GetCursorScreenPos();
        ImGuiContext& g = *GImGui;
        const float dt = ImClamp(g.IO.DeltaTime, 0.f, 0.05f);
        const char* presetIcon = AimPresetIcon();
        const float priorEase =
            0.5f - 0.5f *
            cosf(state.alpha * 3.14159265358979323846f);
        const float priorDimensionEase =
            state.open && !state.closing
            ? priorEase
            : state.alpha;
        const float priorButtonWidth = ImLerp(
            closedSize.x,
            targetWidth,
            priorDimensionEase);

        auto FitText = [](const char* source,
                          char* output,
                          int capacity,
                          float maximumWidth) noexcept
        {
            if (!source)
                source = "";
            lstrcpynA(output, source, capacity);
            if (ImGui::CalcTextSize(output).x <= maximumWidth)
                return;
            int length = static_cast<int>(strlen(output));
            while (length > 3)
            {
                output[--length] = 0;
                char candidate[96]{};
                _snprintf(
                    candidate,
                    sizeof(candidate) - 1,
                    "%s...",
                    output);
                if (ImGui::CalcTextSize(candidate).x <= maximumWidth)
                {
                    lstrcpynA(output, candidate, capacity);
                    return;
                }
            }
            lstrcpynA(output, "...", capacity);
        };

        ImGui::PushID("aim_preset_selector_v2");
        ImGui::InvisibleButton(
            "##closed_button",
            ImVec2(priorButtonWidth, closedSize.y));
        const bool buttonHovered = ImGui::IsItemHovered();
        const bool buttonClicked = ImGui::IsItemClicked();
        if (buttonClicked)
        {
            if (!state.open)
            {
                lstrcpynA(
                    state.headerName,
                    AimPresetName(),
                    static_cast<int>(sizeof(state.headerName)));
                state.headerIcon = AimPresetIcon();
                state.open = true;
                state.closing = false;
            }
            else
            {
                state.closing = !state.closing;
            }
            state.openFrame = g.FrameCount;
        }
        const bool overPopup = ImGui::IsMouseHoveringRect(
            state.popupMin,
            state.popupMax,
            false);
        if (state.open &&
            !state.closing &&
            g.IO.MouseClicked[0] &&
            g.FrameCount > state.openFrame &&
            !overPopup)
        {
            state.closing = true;
        }

        const bool targetOpen =
            state.open && !state.closing;
        state.alpha = ImClamp(
            state.alpha +
                (targetOpen ? 1.f : -1.f) *
                    dt * (targetOpen ? 6.f : 5.5f),
            0.f,
            1.f);
        if (state.closing && state.alpha <= 0.001f)
        {
            state.alpha = 0.f;
            state.open = false;
            state.closing = false;
        }
        const float popupEase =
            0.5f - 0.5f *
            cosf(state.alpha * 3.14159265358979323846f);
        const float popupDimensionEase = targetOpen
            ? popupEase
            : state.alpha;
        const float popupVisualAlpha = targetOpen
            ? sqrtf(ImMax(popupEase, 0.f))
            : state.alpha;
        const float contentSlideY = targetOpen
            ? (1.f - popupEase) * 8.f * scale
            : -(1.f - state.alpha) * 12.f * scale;

        if (state.removingHash)
        {
            state.removeProgress = ImClamp(
                state.removeProgress - dt * 4.8f,
                0.f,
                1.f);
            if (state.removeProgress <= 0.001f)
            {
                const std::uint32_t removedHash =
                    state.removingHash;
                const bool removedSelected =
                    s_AimPresetKind ==
                        AimPresetKind::Weapon &&
                    s_AimPresetWeapon == removedHash;
                const int fallbackGroup =
                    AimGroupFromWeaponHash(removedHash);
                RemoveWeaponOverride(removedHash);
                if (removedSelected)
                    SelectAimGroup(fallbackGroup);
                state.removingHash = 0;
                state.removeProgress = 0.f;
            }
        }
        const float removeEase = state.removingHash
            ? 0.5f - 0.5f *
                cosf(
                    state.removeProgress *
                    3.14159265358979323846f)
            : 0.f;

        float visibleWeaponRows =
            static_cast<float>(s_WeaponOverrideCount);
        if (state.removingHash)
            visibleWeaponRows -= 1.f - removeEase;
        float weaponSectionEase =
            s_WeaponOverrideCount > 0 ? 1.f : 0.f;
        if (s_WeaponOverrideCount == 1 &&
            state.removingHash)
        {
            weaponSectionEase = removeEase;
        }
        const float targetHeight =
            (14.f + 2.f * 20.f +
             weaponSectionEase * 20.f +
             35.f * (1 + kAimWeaponGroupCount - 1) +
             50.f * visibleWeaponRows +
             42.f) * scale;
        state.width = targetWidth;
        state.height = targetHeight;

        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float closedAlpha = 1.f;
        const float buttonVisualWidth = ImLerp(
            closedSize.x,
            targetWidth,
            popupDimensionEase);
        const ImVec2 buttonVisualSize(
            buttonVisualWidth,
            closedSize.y);
        const ImDrawFlags closedCorners =
            targetOpen
            ? ImDrawFlags_RoundCornersTop
            : ImDrawFlags_RoundCornersAll;
        const ImU32 closedBg = gui->get_clr(
            buttonHovered
                ? clr->element_background
                : clr->child_bottom_bar,
            0.94f * closedAlpha);
        draw->AddRectFilled(
            min,
            min + buttonVisualSize,
            closedBg,
            8.f * scale,
            closedCorners);
        draw->AddRect(
            min,
            min + buttonVisualSize,
            gui->get_clr(
                buttonHovered
                    ? clr->accent_color
                    : clr->child_stroke_bar,
                (buttonHovered ? 0.55f : 0.72f) *
                    closedAlpha),
            8.f * scale,
            closedCorners,
            1.f);
        ImFont* iconFont =
            set->icon ? set->icon : ImGui::GetFont();
        draw->AddText(
            iconFont,
            11.f * scale,
            min + ImVec2(12.f, 9.5f) * scale,
            gui->get_clr(
                clr->accent_color,
                closedAlpha),
            presetIcon);
        char closedName[96]{};
        FitText(
            AimPresetName(),
            closedName,
            sizeof(closedName),
            targetWidth - 66.f * scale);
        const ImVec2 closedTextSize =
            ImGui::CalcTextSize(closedName);
        draw->PushClipRect(
            min + ImVec2(31.f, 0.f) * scale,
            min + ImVec2(
                buttonVisualWidth - 28.f * scale,
                closedSize.y),
            true);
        draw->AddText(
            ImVec2(
                min.x + 35.f * scale,
                min.y +
                    (closedSize.y - closedTextSize.y) * 0.5f),
            gui->get_clr(
                clr->text_active,
                closedAlpha),
            closedName);
        draw->PopClipRect();
        draw->AddText(
            iconFont,
            9.f * scale,
            ImVec2(
                min.x + buttonVisualWidth - 20.f * scale,
                min.y + 10.5f * scale),
            gui->get_clr(
                clr->text,
                0.72f * closedAlpha),
            ICON_FA_CHEVRON_DOWN);

        if (state.open || state.alpha > 0.01f)
        {
            ImGui::SetNextWindowPos(
                min + ImVec2(
                    0.f,
                    closedSize.y - 2.f * scale),
                ImGuiCond_Always);
            ImGui::SetNextWindowSize(
                ImVec2(state.width, state.height),
                ImGuiCond_Always);
            // Pushing an absolute alpha would ignore the shell fade, so the
            // popup stayed lit while the rest of the menu was closing.
            ImGui::PushStyleVar(
                ImGuiStyleVar_Alpha,
                ImGui::GetStyle().Alpha * popupVisualAlpha);
            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowPadding,
                ImVec2(8.f, 7.f) * scale);
            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowRounding,
                0.f);
            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowBorderSize,
                0.f);
            ImGui::PushStyleVar(
                ImGuiStyleVar_ItemSpacing,
                ImVec2(4.f, 4.f) * scale);
            ImGui::PushStyleColor(
                ImGuiCol_WindowBg,
                ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGui::PushStyleColor(
                ImGuiCol_Border,
                ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoNavFocus |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse;
            if (!targetOpen)
                flags |= ImGuiWindowFlags_NoInputs;
            ImGui::Begin(
                "##aim_preset_dropdown_v2",
                nullptr,
                flags);
            if (state.open)
                ImGui::BringWindowToDisplayFront(
                    ImGui::GetCurrentWindow());
            state.popupMin = ImGui::GetWindowPos();
            const float visibleWidth = ImLerp(
                closedSize.x,
                state.width,
                popupDimensionEase);
            const float visibleHeight = targetOpen
                ? state.height * popupDimensionEase
                : state.height;
            state.popupMax = state.popupMin +
                ImVec2(
                    visibleWidth,
                    visibleHeight);
            ImDrawList* popupDraw =
                ImGui::GetWindowDrawList();
            popupDraw->AddRectFilled(
                state.popupMin,
                state.popupMax,
                gui->get_clr(clr->child_bottom_bar, 0.995f),
                10.f * scale,
                ImDrawFlags_RoundCornersBottom);
            popupDraw->AddRect(
                state.popupMin,
                state.popupMax,
                gui->get_clr(clr->child_stroke_bar, 0.78f),
                10.f * scale,
                ImDrawFlags_RoundCornersBottom,
                1.f);
            ImGui::PushClipRect(
                state.popupMin,
                ImVec2(
                    state.popupMax.x,
                    state.popupMin.y +
                        ImMax(visibleHeight, 1.f)),
                true);

            ImGui::SetCursorPos(
                ImVec2(
                    8.f * scale,
                    7.f * scale + contentSlideY));
            ImGui::TextDisabled(
                "%s",
                T("PRESETS", "ПРЕСЕТЫ", "ПРЕСЕТИ"));
            if (gui->selectable_ex(
                    T("All###aim_v2_all",
                      "Все###aim_v2_all",
                      "Усі###aim_v2_all"),
                    s_AimPresetKind == AimPresetKind::Group &&
                        s_AimPresetGroup == 0))
            {
                SelectAimGroup(0);
                state.closing = true;
            }
            ImGui::TextDisabled(
                "%s",
                T("CATEGORIES", "КАТЕГОРИИ", "КАТЕГОРІЇ"));
            for (int group = 1;
                 group < kAimWeaponGroupCount;
                 ++group)
            {
                char label[96]{};
                _snprintf(
                    label,
                    sizeof(label) - 1,
                    "%s###aim_v2_group_%d",
                    WeaponGroups::GroupLabel(
                        static_cast<WeaponGroups::Group>(group),
                        static_cast<int>(s_Language)),
                    group);
                if (gui->selectable_ex(
                        label,
                        s_AimPresetKind == AimPresetKind::Group &&
                            s_AimPresetGroup == group))
                {
                    SelectAimGroup(group);
                    state.closing = true;
                }
            }

            if (s_WeaponOverrideCount > 0)
            {
                ImGuiWindow* sectionWindow =
                    ImGui::GetCurrentWindow();
                const float sectionStartY =
                    ImGui::GetCursorPosY();
                const float sectionStartScreenY =
                    ImGui::GetCursorScreenPos().y;
                const float sectionPreviousMaxY =
                    sectionWindow->DC.CursorMaxPos.y;
                ImGui::PushStyleVar(
                    ImGuiStyleVar_Alpha,
                    ImGui::GetStyle().Alpha *
                        ImMax(weaponSectionEase, 0.02f));
                ImGui::TextDisabled(
                    "%s",
                    T("WEAPONS", "ОРУЖИЕ", "ЗБРОЯ"));
                ImGui::PopStyleVar();
                const float sectionFullAdvance =
                    ImGui::GetCursorPosY() - sectionStartY;
                ImGui::SetCursorPosY(
                    sectionStartY +
                    sectionFullAdvance * weaponSectionEase);
                sectionWindow->DC.CursorMaxPos.y = ImMax(
                    sectionPreviousMaxY,
                    sectionStartScreenY +
                        sectionFullAdvance * weaponSectionEase);
                for (int i = 0;
                     i < s_WeaponOverrideCount;
                     ++i)
                {
                    const WeaponOverrideSlot& slot =
                        s_WeaponOverrides[i];
                    const WeaponCatalog::Entry* weapon =
                        WeaponCatalog::FindByHash(slot.hash);
                    ImGui::PushID(
                        static_cast<int>(slot.hash));
                    const bool removing =
                        state.removingHash == slot.hash;
                    const float rowEase =
                        removing ? removeEase : 1.f;
                    ImGuiWindow* rowParent =
                        ImGui::GetCurrentWindow();
                    const float rowStartY =
                        ImGui::GetCursorPosY();
                    const float rowStartScreenY =
                        ImGui::GetCursorScreenPos().y;
                    const float rowPreviousMaxY =
                        rowParent->DC.CursorMaxPos.y;
                    const float rowFullHeight =
                        50.f * scale;
                    const float rowAnimatedHeight =
                        rowFullHeight * rowEase;
                    ImGui::PushStyleVar(
                        ImGuiStyleVar_Alpha,
                        ImGui::GetStyle().Alpha * ImMax(rowEase, 0.02f));
                    ImGui::PushStyleVar(
                        ImGuiStyleVar_WindowPadding,
                        ImVec2(0.f, 0.f));
                    ImGui::PushStyleColor(
                        ImGuiCol_ChildBg,
                        ImVec4(0.f, 0.f, 0.f, 0.f));
                    ImGuiWindowFlags rowFlags =
                        ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse |
                        ImGuiWindowFlags_NoSavedSettings;
                    if (removing)
                        rowFlags |= ImGuiWindowFlags_NoInputs;
                    ImGui::BeginChild(
                        "##animated_weapon_row",
                        ImVec2(0.f, rowFullHeight),
                        ImGuiChildFlags_None,
                        rowFlags);
                    const ImVec2 rowClipMin =
                        ImGui::GetWindowPos();
                    const ImVec2 rowClipMax(
                        rowClipMin.x +
                            ImGui::GetWindowSize().x,
                        rowClipMin.y +
                            ImMax(rowAnimatedHeight, 1.f));
                    ImGui::PushClipRect(
                        rowClipMin,
                        rowClipMax,
                        true);
                    ImGui::SetCursorPosY(
                        ImGui::GetCursorPosY() -
                        (1.f - rowEase) * 6.f * scale);
                    char meta[96]{};
                    _snprintf(
                        meta,
                        sizeof(meta) - 1,
                        "%s  ·  %s",
                        weapon ? weapon->spawn : "weapon",
                        T("right-click to remove",
                          "ПКМ — удалить",
                          "ПКМ — видалити"));
                    MenuUi::ListRowDesc row{};
                    row.title =
                        weapon ? weapon->name : "Weapon";
                    row.meta = meta;
                    row.selected =
                        s_AimPresetKind ==
                            AimPresetKind::Weapon &&
                        s_AimPresetWeapon == slot.hash;
                    row.height = 46.f;
                    row.iconSlotW = 52.f * scale;
                    float iconW = 44.f * scale;
                    float iconH = 24.f * scale;
                    if (weapon)
                    {
                        MenuTexture::FitWeaponIconSize(
                            weapon->iconFile,
                            48.f * scale,
                            26.f * scale,
                            &iconW,
                            &iconH);
                        if (void* texture =
                                MenuTexture::GetWeaponIconImGui(
                                    weapon->iconFile))
                        {
                            row.icon =
                                reinterpret_cast<ImTextureID>(
                                    texture);
                        }
                    }
                    row.iconW = iconW;
                    row.iconH = iconH;
                    const bool pressed =
                        !removing &&
                        MenuUi::DrawListRow(
                            "weapon_row",
                            row);
                    const bool rowHovered =
                        ImGui::IsItemHovered();
                    if (rowHovered &&
                        state.removingHash == 0 &&
                        ImGui::IsMouseClicked(
                            ImGuiMouseButton_Right))
                    {
                        state.removingHash = slot.hash;
                        state.removeProgress = 1.f;
                    }
                    if (pressed)
                    {
                        s_AimPresetKind =
                            AimPresetKind::Weapon;
                        s_AimPresetWeapon = slot.hash;
                        s_WeaponOverrides[i].active = true;
                        state.closing = true;
                    }
                    ImGui::PopClipRect();
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                    ImGui::PopStyleVar(2);
                    ImGui::SetCursorPosY(
                        rowStartY + rowAnimatedHeight);
                    rowParent->DC.CursorMaxPos.y = ImMax(
                        rowPreviousMaxY,
                        rowStartScreenY +
                            rowAnimatedHeight);
                    ImGui::PopID();
                }
            }

            ImGui::Dummy(ImVec2(0.f, 2.f * scale));
            if (MenuUi::ActionButton(
                    "aim_v2_add_weapon",
                    T("Add weapon",
                      "Добавить оружие",
                      "Додати зброю"),
                    ImVec2(
                        ImGui::GetContentRegionAvail().x,
                        31.f * scale),
                    true,
                    ICON_FA_CIRCLE_PLUS))
            {
                WeaponManagerUi::OpenAddWeapon();
                state.closing = true;
            }

            ImGui::PopClipRect();
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(5);
        }
        ImGui::PopID();
    }

    static void DrawAimBooleanRow(
        const char* id,
        const char* icon,
        const char* label,
        bool* value,
        bool overflow = false) noexcept
    {
        MenuUi::FeatureRowDesc row{};
        row.icon = icon;
        row.label = label;
        row.enabled = value;
        row.overflow = overflow;
        row.iconScale = 0.78f;
        // No rule under the row: spacing separates them well enough, and the
        // stack of hairlines read as a table.
        MenuUi::FeatureRow(id, row);
    }

    static void DrawAimColorToggleRow(
        const char* id,
        const char* icon,
        const char* label,
        bool* enabled,
        float color[3],
        bool overflow = false) noexcept
    {
        float rgba[4] = {
            color[0],
            color[1],
            color[2],
            1.f
        };
        MenuUi::FeatureRowDesc row{};
        row.icon = icon;
        row.label = label;
        row.enabled = enabled;
        row.color = rgba;
        row.overflow = overflow;
        row.iconScale = 0.78f;
        MenuUi::FeatureRow(id, row);
        color[0] = rgba[0];
        color[1] = rgba[1];
        color[2] = rgba[2];
    }

    // Main puts silent and vector next to each other, so each side drops the
    // master/detail split and stacks its cards in the one scroll column it got.
    static bool s_AimStackColumns = false;

    static void BeginAimColumn(
        const char* id,
        const ImVec2& size,
        const char* title,
        bool divider) noexcept
    {
        if (s_AimStackColumns)
            return;
        const float scale = ImClamp(
            static_cast<float>(s_UIScalePercent) / 100.f,
            1.f,
            2.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.f);
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(14.f, 11.f) * scale);
        ImGui::PushStyleVar(
            ImGuiStyleVar_ItemSpacing,
            ImVec2(8.f, 8.f) * scale);
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            gui->get_clr(clr->child_bottom_bar, 0.14f));
        ImGuiWindowFlags columnFlags =
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus;
        // General (left) is a fixed workspace; Settings (right) owns overflow.
        if (!divider)
        {
            columnFlags |=
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse;
        }
        ImGui::BeginChild(
            id,
            size,
            ImGuiChildFlags_AlwaysUseWindowPadding,
            columnFlags);
        if (title && title[0])
        {
            ImGui::PushStyleColor(ImGuiCol_Text, clr->text);
            ImGui::TextUnformatted(title);
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0.f, 4.f * scale));
        }
    }

    static void EndAimColumn() noexcept
    {
        if (s_AimStackColumns)
            return;
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(4);
    }

    // Dependent settings used to slide in and out with the toggle above them,
    // which moved everything below on every click. They are always present
    // now; the group is only a visual indent.
    bool BeginAimReveal(
        const char* id,
        bool open,
        float expandedHeight) noexcept
    {
        (void)open;
        (void)expandedHeight;
        // No box either: a framed sub-panel made one dependent slider look
        // selected next to the plain rows around it.
        ImGui::PushID(id ? id : "aim_reveal");
        return true;
    }

    void EndAimReveal(bool open) noexcept
    {
        (void)open;
        ImGui::PopID();
    }

    void DrawAimAnimatedSlider(
        const char* id,
        bool open,
        const char* icon,
        const char* label,
        float* value,
        float minimum,
        float maximum,
        const char* format) noexcept
    {
        (void)open;
        ImGui::PushID(id ? id : "aim_inline_reveal");
        gui->slider_float_icon(
            icon,
            label,
            value,
            minimum,
            maximum,
            format);
        ImGui::PopID();
    }

    static void DrawAimFilters(
        AimWeaponProfile& profile,
        const char* idPrefix,
        bool* visibleCheck,
        bool showTitle = true) noexcept
    {
        ImGui::PushID(idPrefix);
        if (showTitle)
            MenuUi::SectionTitle(T("Filters", "Фильтры", "Фільтри"));
        if (visibleCheck)
        {
            DrawAimBooleanRow(
                "ignore_invisible",
                ICON_FA_EYE_SLASH,
                T("Ignore Invisible",
                  "Игнорировать невидимых",
                  "Ігнорувати невидимих"),
                visibleCheck);
        }
        DrawAimBooleanRow(
            "ignore_dead",
            ICON_FA_SKULL,
            T("Ignore Dead", "Игнорировать мёртвых", "Ігнорувати мертвих"),
            &profile.ignoreDead);
        DrawAimBooleanRow(
            "ignore_npc",
            ICON_FA_ROBOT,
            T("Ignore NPC", "Игнорировать NPC", "Ігнорувати NPC"),
            &profile.ignoreNpc);
        DrawAimBooleanRow(
            "ignore_admin",
            ICON_FA_USER_SHIELD,
            T("Ignore Admins", "Игнорировать админов", "Ігнорувати адмінів"),
            &profile.ignoreAdmins);
        DrawAimBooleanRow(
            "ignore_friends",
            ICON_FA_USER_GROUP,
            T("Ignore Friends", "Игнорировать друзей", "Ігнорувати друзів"),
            &profile.ignoreFriends);
        ImGui::PopID();
    }

    static void AimPlainCheck(
        const char* icon,
        const char* label,
        bool* value) noexcept
    {
        char visible[128]{};
        int length = 0;
        if (label)
        {
            while (label[length] &&
                   !(label[length] == '#' &&
                     label[length + 1] == '#' &&
                     label[length + 2] == '#') &&
                   length + 1 < static_cast<int>(sizeof(visible)))
            {
                visible[length] = label[length];
                ++length;
            }
        }
        visible[length] = 0;
        ImGui::PushID(label ? label : "aim_toggle");
        DrawAimBooleanRow(
            "advanced_toggle",
            icon,
            visible,
            value);
        ImGui::PopID();
    }

    static void RenderAimVectorAdvanced(
        AimWeaponProfile& profile) noexcept
    {
        // No sub-captions inside the card: each row says what it does, and a
        // toggle is immediately followed by the settings it controls.
        AimPlainCheck(
            ICON_FA_EXPAND,
            T("Dynamic FOV###aim_adv_dynamic_fov",
              "Динамический FOV###aim_adv_dynamic_fov",
              "Динамічний FOV###aim_adv_dynamic_fov"),
            &profile.dynamicFov);
        if (BeginAimReveal(
                "vector_dynamic_fov_details",
                profile.dynamicFov,
                165.f))
        {
            gui->slider_float_icon(
                ICON_FA_CROSSHAIRS,
                "ADS###aim_adv_vec_ads",
                &profile.dynamicFovAdsMul,
                0.3f,
                1.2f,
                "%.2fx");
            gui->slider_float_icon(
                ICON_FA_PERSON_RUNNING,
                T("Target speed###aim_adv_vec_speed",
                  "Скорость цели###aim_adv_vec_speed",
                  "Швидкість цілі###aim_adv_vec_speed"),
                &profile.dynamicFovSpeedExpand,
                0.f,
                1.5f,
                "%.2fx");
            gui->slider_float_icon(
                ICON_FA_RULER,
                T("Distance scale###aim_adv_vec_fov_scale",
                  "Масштаб дистанции###aim_adv_vec_fov_scale",
                  "Масштаб дистанції###aim_adv_vec_fov_scale"),
                &profile.fovDistanceMul,
                0.25f,
                2.f,
                "%.2fx");
            EndAimReveal(profile.dynamicFov);
        }
    }

    static void RenderAimVector(
        AimWeaponProfile& profile,
        bool layerEnabled) noexcept
    {
        const float availableW = ImGui::GetContentRegionAvail().x;
        const float availableH = ImGui::GetContentRegionAvail().y;
        const MenuUi::MasterDetailMetrics layout =
            MenuUi::CalculateMasterDetail(
                availableW,
                availableH,
                440.f,
                390.f,
                290.f);

        BeginAimColumn(
            "##aim_vector_general",
            ImVec2(layout.masterWidth, layout.masterHeight),
            nullptr,
            false);
        if (!layerEnabled)
            ImGui::BeginDisabled();
        MenuUi::BeginInspectorIsland(
            "aim_vector_general_island",
            T("VECTOR", "ВЕКТОР", "ВЕКТОР"),
            0.f,
            nullptr);
        DrawAimBooleanRow(
            "vector_enabled",
            ICON_FA_POWER_OFF,
            T("Enabled", "Включено", "Увімкнено"),
            &profile.vecEnabled);
        DrawAimColorToggleRow(
            "vector_draw_fov",
            ICON_FA_BULLSEYE,
            T("Draw FOV",
              "Показывать FOV",
              "Показувати FOV"),
            &profile.vecDrawFov,
            profile.vecFovColor);
        gui->slider_float_icon(
            ICON_FA_BULLSEYE,
            "FOV###aim_vector_fov",
            &profile.vecFov,
            1.f,
            300.f,
            "%.0f px");
        gui->slider_float_icon(
            ICON_FA_SLIDERS,
            T("Smoothness###aim_vector_smooth",
              "Плавность###aim_vector_smooth",
              "Плавність###aim_vector_smooth"),
            &profile.vecSmoothness,
            1.f,
            100.f,
            "%.0f");
        AimPlainCheck(
            ICON_FA_DICE,
            T("Random smooth###aim_vec_random",
              "Случайная плавность###aim_vec_random",
              "Випадкова плавність###aim_vec_random"),
            &profile.vecRandomSmooth);
        if (BeginAimReveal(
                "vector_random_details",
                profile.vecRandomSmooth,
                40.f))
        {
            gui->slider_float_icon(
                ICON_FA_DICE,
                T("Random amount###aim_vec_random_amount",
                  "Сила случайности###aim_vec_random_amount",
                  "Сила випадковості###aim_vec_random_amount"),
                &profile.vecRandomAmt,
                0.f,
                1.f,
                "%.2f");
            EndAimReveal(profile.vecRandomSmooth);
        }
        AimPlainCheck(
            ICON_FA_GAUGE_HIGH,
            T("Smart smooth###aim_vec_smart",
              "Умная плавность###aim_vec_smart",
              "Розумна плавність###aim_vec_smart"),
            &profile.vecSmartSmooth);
        gui->slider_float_icon(
            ICON_FA_SLIDERS,
            T("Smooth multiplier###aim_vector_smooth_mul",
              "Множитель плавности###aim_vector_smooth_mul",
              "Множник плавності###aim_vector_smooth_mul"),
            &profile.smoothDistanceMul,
            0.25f,
            2.f,
            "%.2fx");
        DrawAimBooleanRow(
            "vector_sticky",
            ICON_FA_LINK,
            T("Lock on last", "Держать цель", "Тримати ціль"),
            &profile.vecSticky);
        MenuUi::DrawBoneDropdown(
            profile.vecBones,
            "aim_vector",
            &profile.vecBoneNearestAny);
        RenderAimVectorAdvanced(profile);
        MenuUi::EndInspectorIsland();

        if (!layerEnabled)
            ImGui::EndDisabled();
        EndAimColumn();
    }

    static void RenderAimSilent(
        AimWeaponProfile& profile,
        bool layerEnabled) noexcept
    {
        const MenuUi::MasterDetailMetrics layout =
            MenuUi::CalculateMasterDetail(
                ImGui::GetContentRegionAvail().x,
                ImGui::GetContentRegionAvail().y,
                440.f,
                390.f,
                290.f);
        BeginAimColumn(
            "##aim_silent_general",
            ImVec2(layout.masterWidth, layout.masterHeight),
            nullptr,
            false);
        if (!layerEnabled)
            ImGui::BeginDisabled();
        // The card names the mode, so the column needs no heading above it.
        MenuUi::BeginInspectorIsland(
            "aim_silent_general_island",
            T("SILENT", "САЙЛЕНТ", "САЙЛЕНТ"),
            0.f,
            nullptr);
        // 1. Включить
        DrawAimBooleanRow(
            "silent_enabled",
            ICON_FA_POWER_OFF,
            T("Enabled", "Включено", "Увімкнено"),
            &profile.silentEnabled);
        // 2. Кости
        MenuUi::DrawBoneDropdown(
            profile.silentBones,
            "aim_silent",
            &profile.silentBoneNearestAny);
        // 3. Показывать FOV
        DrawAimColorToggleRow(
            "silent_draw_fov",
            ICON_FA_BULLSEYE,
            T("Draw FOV",
              "Показывать FOV",
              "Показувати FOV"),
            &profile.silentDrawFov,
            profile.silentFovColor);
        // 4. Ползунок FOV
        gui->slider_float_icon(
            ICON_FA_BULLSEYE,
            "FOV###aim_silent_fov",
            &profile.silentFov,
            1.f,
            300.f,
            "%.0f px");
        // 5. Ползунок шанса попадания (всегда виден)
        gui->slider_float_icon(
            ICON_FA_CHART_PIE,
            T("Hitchance###aim_silent_hitchance_pct",
              "Шанс попадания###aim_silent_hitchance_pct",
              "Шанс влучання###aim_silent_hitchance_pct"),
            &profile.silentHitchancePct,
            1.f,
            100.f,
            "%.0f%%");
        // 7. Wallshot
        DrawAimBooleanRow(
            "silent_wallshot",
            ICON_FA_MOUNTAIN,
            T("Wallshot", "Wallshot", "Wallshot"),
            &profile.silentWallshot);
        // 8. Держать цель
        DrawAimBooleanRow(
            "silent_lock",
            ICON_FA_LINK,
            T("Lock on last", "Держать цель", "Тримати ціль"),
            &profile.silentLock);
        // 9. Метка цели
        DrawAimColorToggleRow(
            "silent_mark_target",
            ICON_FA_CIRCLE_DOT,
            T("Mark target", "Метка цели", "Мітка цілі"),
            &profile.silentMark,
            s_Aim_MarkColor);
        if (profile.silentMark)
        {
            static const char* const kEn[] = { "Dot", "Orbits" };
            static const char* const kRu[] = { "Точка", "Орбиты" };
            static const char* const kUk[] = { "Точка", "Орбіти" };
            gui->dropdown(
                T("Mark style###aim_mark_style",
                  "Стиль метки###aim_mark_style",
                  "Стиль мітки###aim_mark_style"),
                &profile.silentMarkStyle,
                s_Language == Language::Russian ? kRu
                    : (s_Language == Language::Ukrainian ? kUk : kEn),
                EspTargetMarkStyleCount);
            if (profile.silentMarkStyle < 0 ||
                profile.silentMarkStyle >= EspTargetMarkStyleCount)
                profile.silentMarkStyle = EspTargetMarkDot;
        }
        MenuUi::EndInspectorIsland();

        MenuUi::BeginInspectorIsland(
            "aim_silent_filters_island",
            T("IGNORE", "ИГНОР", "ІГНОР"),
            10.f,
            nullptr);
        // Игнорировать невидимых управляет vecVisible (видимость для векторного айма)
        DrawAimFilters(
            profile,
            "aim_silent_filters",
            &profile.vecVisible,
            false);
        MenuUi::EndInspectorIsland();
        if (!layerEnabled)
            ImGui::EndDisabled();
        EndAimColumn();
    }

    static void RenderAimTrigger(
        AimWeaponProfile& profile,
        bool layerEnabled) noexcept
    {
        if (!layerEnabled)
            ImGui::BeginDisabled();
        MenuUi::BeginInspectorIsland(
            "aim_trigger_general_island",
            T("TRIGGER", "ТРИГГЕР", "ТРИГЕР"),
            0.f,
            nullptr);
        DrawAimBooleanRow(
            "trigger_enabled",
            ICON_FA_POWER_OFF,
            T("Enabled", "Включено", "Увімкнено"),
            &profile.triggerEnabled);
        // Unbound means always armed while enabled.
        gui->keybind(
            T("Hold key###aim_trigger_key",
              "Клавиша удержания###aim_trigger_key",
              "Клавіша утримання###aim_trigger_key"),
            &profile.triggerKey,
            ICON_FA_KEY);
        gui->slider_float_icon(
            ICON_FA_CLOCK,
            T("Reaction delay###aim_trigger_delay",
              "Задержка реакции###aim_trigger_delay",
              "Затримка реакції###aim_trigger_delay"),
            &profile.triggerDelayMs,
            0.f,
            50.f,
            "%.0f ms");
        gui->slider_float_icon(
            ICON_FA_HOURGLASS_HALF,
            T("Shot interval###aim_trigger_cooldown",
              "Интервал выстрелов###aim_trigger_cooldown",
              "Інтервал пострілів###aim_trigger_cooldown"),
            &profile.triggerCooldownMs,
            20.f,
            500.f,
            "%.0f ms");
        MenuUi::DrawBoneDropdown(
            profile.triggerBones,
            "aim_trigger");
        MenuUi::EndInspectorIsland();
        if (!layerEnabled)
            ImGui::EndDisabled();
    }

    // A weapon group starts out following the shared profile and takes over the
    // moment something in it is actually changed.
    //
    // Nothing is asked of the user for this: an explicit "use own settings"
    // switch is one more thing to forget, and merely opening a group used to be
    // enough to freeze a copy of the shared profile into it, which is how the
    // aim ended up reading values the page was no longer showing. A group that
    // has never been touched is not a configuration, so the held weapon falls
    // back to the shared one.
    static void MarkEditedScopeConfigured(
        const AimWeaponProfile& before,
        AimWeaponProfile* edited) noexcept
    {
        if (!edited ||
            s_AimPresetKind != AimPresetKind::Group ||
            s_AimPresetGroup <= 0 ||
            edited->useCustom)
            return;
        // Both sides are copies of the same trivially copyable object, so the
        // padding matches and only a real field change shows up here.
        if (std::memcmp(
                &before,
                edited,
                sizeof(AimWeaponProfile)) == 0)
            return;
        edited->useCustom = true;
    }

    static void RenderAimDamager(bool layerEnabled) noexcept
    {
        Damager::Settings& damager = Damager::Access();
        if (!layerEnabled)
            ImGui::BeginDisabled();
        MenuUi::BeginInspectorIsland(
            "aim_damager_general_island",
            T("DAMAGER", "ДАМАГЕР", "ДАМАГЕР"),
            0.f,
            nullptr);
        DrawAimBooleanRow(
            "damager_enabled",
            ICON_FA_POWER_OFF,
            T("Enabled", "Включено", "Увімкнено"),
            &damager.enabled);
        // Unbound means armed for as long as the feature is on, which is how
        // the trigger reads its own key too.
        gui->keybind(
            T("Hold key###aim_damager_key",
              "Клавиша удержания###aim_damager_key",
              "Клавіша утримання###aim_damager_key"),
            &damager.holdKey,
            ICON_FA_KEY);
        gui->slider_float_icon(
            ICON_FA_BULLSEYE,
            T("FOV###aim_damager_fov",
              "FOV###aim_damager_fov",
              "FOV###aim_damager_fov"),
            &damager.fov,
            Damager::kMinFov,
            Damager::kMaxFov,
            "%.0f px");
        gui->slider_float_icon(
            ICON_FA_RULER_HORIZONTAL,
            T("Range###aim_damager_range",
              "Дистанция###aim_damager_range",
              "Дистанція###aim_damager_range"),
            &damager.maxDistance,
            Damager::kMinDistance,
            Damager::kMaxDistance,
            "%.0f m");
        float damage = static_cast<float>(damager.damage);
        if (gui->slider_float_icon(
                ICON_FA_HAND_FIST,
                T("Damage per hit###aim_damager_damage",
                  "Урон за удар###aim_damager_damage",
                  "Шкода за удар###aim_damager_damage"),
                &damage,
                1.f,
                200.f,
                "%.0f"))
            damager.damage = static_cast<int>(damage);
        // Hits land inside one pass, so this is what decides whether the target
        // drops now or bleeds slowly: the rate below only spaces the passes out.
        float hits = static_cast<float>(damager.hits);
        if (gui->slider_float_icon(
                ICON_FA_LAYER_GROUP,
                T("Hits per pass###aim_damager_hits",
                  "Ударов за проход###aim_damager_hits",
                  "Ударів за прохід###aim_damager_hits"),
                &hits,
                1.f,
                20.f,
                "%.0f"))
            damager.hits = static_cast<int>(hits);
        float interval = static_cast<float>(damager.intervalMs);
        if (gui->slider_float_icon(
                ICON_FA_STOPWATCH,
                T("Interval###aim_damager_interval",
                  "Интервал###aim_damager_interval",
                  "Інтервал###aim_damager_interval"),
                &interval,
                40.f,
                1000.f,
                "%.0f ms"))
            damager.intervalMs = static_cast<int>(interval);
        MenuUi::SectionTitle(
            T("Reported as", "Считается как", "Рахується як"));
        const MenuUi::IconTabDesc weaponTabs[3] = {
            {"damager_wp_fist",
             ICON_FA_HAND_FIST,
             T("Fist", "Кулак", "Кулак")},
            {"damager_wp_knife",
             ICON_FA_KHANDA,
             T("Knife", "Нож", "Ніж")},
            {"damager_wp_current",
             ICON_FA_GUN,
             T("Held", "В руках", "В руках")}
        };
        MenuUi::ChipTabs(
            "aim_damager_weapon",
            weaponTabs,
            3,
            &damager.weapon);
        MenuUi::LabelWithHelp(
            T("Fists need nothing in hand",
              "Кулаку ничего не нужно в руках",
              "Кулаку нічого не потрібно в руках"),
            "The hit is reported to the server as this weapon. A roleplay "
            "server checks it against what you are holding, and fists are the "
            "one weapon everybody always has.",
            "Удар уходит на сервер как это оружие. RP-сервер сверяет его с тем, "
            "что у вас в руках, а кулаки есть у всех и всегда.",
            "Удар іде на сервер як ця зброя. RP-сервер зіставляє його з тим, "
            "що у вас в руках, а кулаки є в усіх і завжди.");
        MenuUi::DrawBoneDropdown(
            damager.bones,
            "aim_damager");
        MenuUi::EndInspectorIsland();
        if (!layerEnabled)
            ImGui::EndDisabled();
    }

    static void RenderAimDamagerFilters(
        AimWeaponProfile& profile,
        bool layerEnabled) noexcept
    {
        Damager::Settings& damager = Damager::Access();
        const Damager::Status status = Damager::GetStatus();
        if (!layerEnabled)
            ImGui::BeginDisabled();
        MenuUi::BeginInspectorIsland(
            "aim_damager_filters_island",
            T("IGNORE", "ИГНОР", "ІГНОР"),
            0.f,
            nullptr);
        DrawAimFilters(
            profile,
            "aim_damager_filters",
            &damager.visibleCheck,
            false);
        DrawAimBooleanRow(
            "damager_stop_when_down",
            ICON_FA_SKULL,
            T("Stop when down", "Стоп когда упал", "Стоп коли впав"),
            &damager.stopWhenDown);
        DrawAimBooleanRow(
            "damager_notify",
            ICON_FA_BELL,
            T("Notify", "Уведомления", "Повідомлення"),
            &damager.notify);
        MenuUi::EndInspectorIsland();

        MenuUi::BeginInspectorIsland(
            "aim_damager_status_island",
            T("Status", "Состояние", "Стан"),
            0.f,
            T("What the target reports",
              "Что сообщает цель",
              "Що повідомляє ціль"));
        char summary[192]{};
        if (!status.armed)
            _snprintf_s(
                summary,
                sizeof(summary),
                _TRUNCATE,
                "%s",
                T("Disarmed", "Не активен", "Не активний"));
        else if (!status.hasTarget)
            _snprintf_s(
                summary,
                sizeof(summary),
                _TRUNCATE,
                "%s",
                T("Nobody in the FOV",
                  "В FOV никого",
                  "У FOV нікого"));
        else
            _snprintf_s(
                summary,
                sizeof(summary),
                _TRUNCATE,
                T("Target %d at %.1f m\nHealth %d, hits sent %d",
                  "Цель %d на %.1f м\nЗдоровье %d, ударов отправлено %d",
                  "Ціль %d на %.1f м\nЗдоров'я %d, ударів надіслано %d"),
                status.remoteId,
                static_cast<double>(status.distance),
                status.health,
                status.hits);
        ImGui::TextUnformatted(summary);
        // The health line is the only honest answer to "does this work here":
        // it is the number the victim's own client publishes, so a value that
        // never moves means the server dropped every hit.
        if (status.detail[0])
            ImGui::TextDisabled("%s", status.detail);
        MenuUi::EndInspectorIsland();
        if (!layerEnabled)
            ImGui::EndDisabled();
    }

    static void RenderAimTriggerFilters(
        AimWeaponProfile& profile,
        bool layerEnabled) noexcept
    {
        if (!layerEnabled)
            ImGui::BeginDisabled();
        MenuUi::BeginInspectorIsland(
            "aim_trigger_filters_island",
            T("IGNORE", "ИГНОР", "ІГНОР"),
            0.f,
            nullptr);
        DrawAimFilters(
            profile,
            "aim_trigger_filters",
            &profile.triggerVisible,
            false);
        MenuUi::EndInspectorIsland();
        if (!layerEnabled)
            ImGui::EndDisabled();
    }

    // Scope bar for the aim profile: All, the weapon groups, the per-weapon
    // overrides behind Other, and Add weapon. Floats over the bottom of the
    // page; the caller reserves its height so no setting hides underneath.
    static bool s_AimWeaponBarOtherOpen = false;
    static float s_AimWeaponBarOtherPhase = 0.f;
    static float s_AimWeaponBarPresencePhase = 0.f;
    static float s_AimWeaponBarOtherAnim = 0.f;
    static float s_AimWeaponBarOtherPresence = 0.f;
    // Right-clicked chip collapses before the override is actually dropped, so
    // the strip closes the gap instead of snapping.
    static std::uint32_t s_AimWeaponBarRemoving = 0;
    static float s_AimWeaponBarRemovePhase = 0.f;

    static void AimWeaponBarTick() noexcept
    {
        if (s_AimWeaponBarRemoving)
        {
            s_AimWeaponBarRemovePhase = MenuMotion::Advance(
                s_AimWeaponBarRemovePhase,
                false,
                0.16f);
            if (s_AimWeaponBarRemovePhase <= 0.002f)
            {
                const std::uint32_t hash = s_AimWeaponBarRemoving;
                const bool wasSelected =
                    s_AimPresetKind == AimPresetKind::Weapon &&
                    s_AimPresetWeapon == hash;
                const int fallbackGroup = AimGroupFromWeaponHash(hash);
                RemoveWeaponOverride(hash);
                if (wasSelected)
                    SelectAimGroup(fallbackGroup);
                s_AimWeaponBarRemoving = 0;
                s_AimWeaponBarRemovePhase = 0.f;
            }
        }

        // Timer driven instead of exponential convergence: the strip has to hit
        // zero exactly, otherwise the last pixels crawl and the close reads as
        // a stall.
        const bool hasOverrides = s_WeaponOverrideCount > 0;
        if (!hasOverrides)
            s_AimWeaponBarOtherOpen = false;
        s_AimWeaponBarPresencePhase = MenuMotion::Advance(
            s_AimWeaponBarPresencePhase,
            hasOverrides,
            MenuMotion::kSelection);
        s_AimWeaponBarOtherPhase = MenuMotion::Advance(
            s_AimWeaponBarOtherPhase,
            s_AimWeaponBarOtherOpen,
            0.15f);
        // EaseOut keeps a soft landing when opening while the close accelerates
        // into zero instead of trailing off.
        s_AimWeaponBarOtherPresence =
            MenuMotion::EaseOut(s_AimWeaponBarPresencePhase);
        s_AimWeaponBarOtherAnim =
            MenuMotion::EaseOut(s_AimWeaponBarOtherPhase);
    }

    // The override strip is always a single row: it scrolls sideways instead of
    // stacking upwards, so the bar keeps a predictable height.
    static float AimWeaponBarRevealHeight(float scale) noexcept
    {
        const float rowH = 32.f * scale;
        return s_AimWeaponBarOtherAnim * (rowH + 6.f * scale);
    }

    static float AimWeaponBarHeight(float scale) noexcept
    {
        const float rowH = 32.f * scale;
        const float pad = 6.f * scale;
        return pad * 2.f + rowH + AimWeaponBarRevealHeight(scale);
    }

    static void DrawAimWeaponBar(
        const ImVec2& barMin,
        float barWidth,
        float scale) noexcept
    {
        const MenuTheme::Palette& tokens = MenuTheme::Colors();
        const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
        const float rowH = 32.f * scale;
        const float pad = 6.f * scale;
        const float barH = AimWeaponBarHeight(scale);
        ImDrawList* draw = ImGui::GetWindowDrawList();

        draw->AddRectFilled(
            barMin,
            barMin + ImVec2(barWidth, barH),
            gui->get_clr(tokens.panel, 0.82f),
            metrics.radiusPanel * scale);
        draw->AddRect(
            barMin,
            barMin + ImVec2(barWidth, barH),
            gui->get_clr(tokens.borderSubtle, 1.f),
            metrics.radiusPanel * scale,
            0,
            metrics.stroke);

        ImGui::PushID("aim_weapon_bar");
        const float revealH = AimWeaponBarRevealHeight(scale);
        float cursorY = barMin.y + pad;

        if (revealH > 1.f)
        {
            // Clipping the child as it grows gives the strip its slide-out
            // reveal; content stays a single scrollable line.
            ImGui::SetCursorScreenPos(ImVec2(barMin.x + pad, cursorY));
            ImGui::PushStyleVar(
                ImGuiStyleVar_ItemSpacing,
                ImVec2(4.f * scale, 0.f));
            if (ImGui::BeginChild(
                    "##aim_other_strip",
                    ImVec2(barWidth - pad * 2.f, revealH),
                    ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse))
            {
                bool firstChip = true;
                for (int i = 0; i < s_WeaponOverrideCount; ++i)
                {
                    const WeaponOverrideSlot& slot = s_WeaponOverrides[i];
                    const float collapse =
                        slot.hash == s_AimWeaponBarRemoving
                            ? MenuMotion::EaseStandard(
                                s_AimWeaponBarRemovePhase)
                            : 1.f;
                    if (collapse <= 0.004f)
                        continue;
                    const WeaponCatalog::Entry* weapon =
                        WeaponCatalog::FindByHash(slot.hash);
                    const char* name = weapon && weapon->name[0]
                        ? weapon->name
                        : T("Weapon", "Оружие", "Зброя");
                    ImFont* font = set->poppins_widget
                        ? set->poppins_widget
                        : ImGui::GetFont();
                    const float chipW =
                        (font->CalcTextSizeA(
                             MenuType::BodySize(), FLT_MAX, 0.f, name).x +
                         40.f * scale) *
                        collapse;
                    char chipId[32]{};
                    _snprintf(chipId, sizeof(chipId) - 1, "wpn_%u", slot.hash);
                    if (!firstChip)
                        ImGui::SameLine();
                    firstChip = false;
                    ImGui::PushStyleVar(
                        ImGuiStyleVar_Alpha,
                        ImGui::GetStyle().Alpha * collapse);
                    const bool pressed = MenuUi::NavTab(
                        chipId,
                        ICON_FA_GUN,
                        name,
                        s_AimPresetKind == AimPresetKind::Weapon &&
                            s_AimPresetWeapon == slot.hash,
                        true,
                        ImVec2(chipW, rowH));
                    const bool hovered = ImGui::IsItemHovered();
                    ImGui::PopStyleVar();
                    if (pressed && !s_AimWeaponBarRemoving)
                    {
                        s_AimPresetKind = AimPresetKind::Weapon;
                        s_AimPresetWeapon = slot.hash;
                    }
                    // Right click drops the preset; no confirm dialog here
                    // because the chip can be re-added from Add weapon.
                    if (hovered &&
                        !s_AimWeaponBarRemoving &&
                        ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                    {
                        s_AimWeaponBarRemoving = slot.hash;
                        s_AimWeaponBarRemovePhase = 1.f;
                    }
                    if (hovered)
                    {
                        ImGui::SetTooltip(
                            "%s",
                            T("Right click to remove",
                              "Правый клик — удалить",
                              "Правий клік — видалити"));
                    }
                }
                const float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0.f && ImGui::IsWindowHovered())
                {
                    ImGui::SetScrollX(
                        ImGui::GetScrollX() - wheel * 70.f * scale);
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleVar();
            cursorY += revealH;
        }

        // Main row: All, the groups, then Other and Add weapon on the right.
        const char* addLabel =
            T("Add weapon", "Добавить оружие", "Додати зброю");
        const char* otherLabel = T("Other", "Другое", "Інше");
        ImFont* chipFont = set->poppins_widget
            ? set->poppins_widget
            : ImGui::GetFont();
        const float chipFontSize = MenuType::BodySize();
        auto chipWidth = [&](const char* text) noexcept
        {
            return chipFont->CalcTextSizeA(
                       chipFontSize, FLT_MAX, 0.f, text).x +
                34.f * scale;
        };

        const float addW = chipWidth(addLabel);
        const float otherW = s_AimWeaponBarOtherPresence > 0.01f
            ? chipWidth(otherLabel) * s_AimWeaponBarOtherPresence
            : 0.f;
        float x = barMin.x + pad;
        const float groupsRight =
            barMin.x + barWidth - pad - addW - otherW -
            (otherW > 0.f ? 4.f * scale : 0.f);

        for (int group = 0; group < kAimWeaponGroupCount; ++group)
        {
            const char* label = group == 0
                ? T("All", "Все", "Усі")
                : WeaponGroups::GroupLabel(
                    static_cast<WeaponGroups::Group>(group),
                    static_cast<int>(s_Language));
            const float w = chipWidth(label);
            if (x + w > groupsRight)
                break;
            char chipId[32]{};
            _snprintf(chipId, sizeof(chipId) - 1, "grp_%d", group);
            ImGui::SetCursorScreenPos(ImVec2(x, cursorY));
            if (MenuUi::NavTab(
                    chipId,
                    nullptr,
                    label,
                    s_AimPresetKind == AimPresetKind::Group &&
                        s_AimPresetGroup == group,
                    true,
                    ImVec2(w, rowH)))
            {
                s_AimWeaponBarOtherOpen = false;
                SelectAimGroup(group);
            }
            x += w + 2.f * scale;
        }

        float right = barMin.x + barWidth - pad;
        ImGui::SetCursorScreenPos(ImVec2(right - addW, cursorY));
        if (MenuUi::NavTab(
                "add_weapon",
                ICON_FA_CIRCLE_PLUS,
                addLabel,
                false,
                false,
                ImVec2(addW, rowH)))
        {
            WeaponManagerUi::OpenAddWeapon();
        }
        right -= addW + 4.f * scale;

        if (otherW > 0.f)
        {
            ImGui::SetCursorScreenPos(ImVec2(right - otherW, cursorY));
            if (MenuUi::NavTab(
                    "other_weapons",
                    ICON_FA_LAYER_GROUP,
                    otherLabel,
                    s_AimWeaponBarOtherOpen ||
                        s_AimPresetKind == AimPresetKind::Weapon,
                    false,
                    ImVec2(otherW, rowH)))
            {
                s_AimWeaponBarOtherOpen = !s_AimWeaponBarOtherOpen;
            }
        }
        ImGui::PopID();
    }

    static bool RenderAimRemoveConfirm(float scale) noexcept
    {
        const char* popup = T(
            "Remove weapon preset###aim_remove_confirm",
            "Удалить пресет оружия###aim_remove_confirm",
            "Видалити пресет зброї###aim_remove_confirm");
        if (s_AimRemoveRequest)
        {
            ImGui::OpenPopup(popup);
            s_AimRemoveRequest = false;
        }
        ImGui::SetNextWindowSize(
            ImVec2(330.f, 0.f) * scale,
            ImGuiCond_Appearing);
        bool removed = false;
        if (ImGui::BeginPopupModal(
                popup,
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiWindowFlags_NoSavedSettings))
        {
            ImGui::TextUnformatted(
                T("Remove this weapon preset?",
                  "Удалить этот пресет оружия?",
                  "Видалити цей пресет зброї?"));
            ImGui::TextDisabled(
                "%s",
                T("Aim settings and attachments for it will be removed.",
                  "Настройки аима и обвесы этого оружия будут удалены.",
                  "Налаштування аіму й обвіси цієї зброї буде видалено."));
            ImGui::Dummy(ImVec2(0.f, 8.f * scale));
            const float buttonW =
                (ImGui::GetContentRegionAvail().x - 8.f * scale) *
                0.5f;
            if (MenuUi::ActionButton(
                    "aim_remove_cancel",
                    T("Cancel", "Отмена", "Скасувати"),
                    ImVec2(buttonW, 30.f * scale)))
            {
                s_AimRemovePending = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(0.f, 8.f * scale);
            if (MenuUi::ActionButton(
                    "aim_remove_confirm_action",
                    T("Remove", "Удалить", "Видалити"),
                    ImVec2(buttonW, 30.f * scale),
                    true,
                    ICON_FA_TRASH))
            {
                const int fallbackGroup =
                    AimGroupFromWeaponHash(s_AimRemovePending);
                removed = RemoveWeaponOverride(
                    s_AimRemovePending);
                s_AimRemovePending = 0;
                SelectAimGroup(fallbackGroup);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        return removed;
    }

    static void RenderAimOtherLayer(
        float alpha,
        float offsetX,
        bool interactive) noexcept
    {
        if (alpha <= 0.001f)
            return;

        auto& s = MiscFeatures::GetSettings();
        const float scale = ImClamp(
            static_cast<float>(s_UIScalePercent) / 100.f,
            1.f,
            2.f);

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * alpha);
        ImGui::SetCursorPos(
            ImGui::GetCursorPos() +
            ImVec2(offsetX, 0.f));

        MenuUi::BeginPageColumns("aim_other_columns");
        MenuUi::BeginInspectorIsland(
            "player_combat_globals",
            T("WEAPON GLOBALS", "ОРУЖИЕ (ГЛОБАЛЬНО)", "ЗБРОЯ (ГЛОБАЛЬНО)"),
            0.f);
        
        // This is a direct port from MenuPagePlayer.cpp, utilizing MiscFeatures::GetSettings().
        // We will need to redefine or move DrawAimAnimatedSlider if it's strictly local, but 
        // wait, DrawAimAnimatedSlider is actually in MenuPageCombat.cpp anyway!
        // No, DrawAimAnimatedSlider is in MenuPageCombat.cpp, so we can just use it!
        // Wait, MiscRow is not in MenuPageCombat.cpp. Let's see if we can use DrawAimBooleanRow instead.

        DrawAimBooleanRow(
            "player_nospread",
            ICON_FA_BULLSEYE,
            T("No spread", "Без разброса", "Без розкиду"),
            &s.noSpread);
        if (s.noSpread)
        {
            DrawAimAnimatedSlider(
                "player_nospread_pct",
                s.noSpread,
                ICON_FA_BULLSEYE,
                T("Spread cut###player_nospread_pct",
                  "Срез разброса###player_nospread_pct",
                  "Зріз розкиду###player_nospread_pct"),
                &s.noSpreadPct,
                0.f,
                100.f,
                "%.0f%%");
        }
        DrawAimBooleanRow(
            "player_norecoil",
            ICON_FA_CROSSHAIRS,
            T("No recoil", "Без отдачи", "Без віддачі"),
            &s.noRecoil);
        if (s.noRecoil)
        {
            DrawAimAnimatedSlider(
                "player_norecoil_pct",
                s.noRecoil,
                ICON_FA_CROSSHAIRS,
                T("Recoil cut###player_norecoil_pct",
                  "Срез отдачи###player_norecoil_pct",
                  "Зріз віддачі###player_norecoil_pct"),
                &s.noRecoilPct,
                0.f,
                100.f,
                "%.0f%%");
        }
        DrawAimBooleanRow(
            "player_reload",
            ICON_FA_ROTATE,
            T("Reload speed", "Скорость перезарядки", "Швидкість перезарядки"),
            &s.reloadSpeed);
        if (s.reloadSpeed)
        {
            DrawAimAnimatedSlider(
                "player_reload_pct",
                s.reloadSpeed,
                ICON_FA_ROTATE,
                T("Reload boost###player_reload_pct",
                  "Ускорение перезарядки###player_reload_pct",
                  "Прискорення перезарядки###player_reload_pct"),
                &s.reloadSpeedPct,
                0.f,
                100.f,
                "%.0f%%");
        }
        MenuUi::EndInspectorIsland();
        MenuUi::NextPageColumn();

        MenuUi::BeginInspectorIsland(
            "player_combat_ammo",
            T("AMMO", "ПАТРОНЫ", "НАБОЇ"),
            0.f);
        DrawAimBooleanRow(
            "player_noreload",
            ICON_FA_BAN,
            T("No reload", "Без перезарядки", "Без перезарядки"),
            &s.noReload);
        DrawAimBooleanRow(
            "player_infammo",
            ICON_FA_INFINITY,
            T("Infinite ammo", "Бесконечные патроны", "Нескінченні набої"),
            &s.infiniteAmmo);
        DrawAimBooleanRow(
            "player_infclip",
            ICON_FA_LAYER_GROUP,
            T("Infinite clip", "Бесконечный магазин", "Нескінченний магазин"),
            &s.infiniteClip);
        MenuUi::EndInspectorIsland();
        MenuUi::EndPageColumns();

        ImGui::PopStyleVar();
    }

    static void RenderAimbotPage()
    {
        struct ModeTransition final
        {
            int shown = 0;
            int from = 0;
            int to = 0;
            float progress = 1.f;
        };
        static ModeTransition transition{};
        EnsureAimProfilesInitialized();
        const float scale = ImClamp(
            static_cast<float>(s_UIScalePercent) / 100.f,
            1.f,
            2.f);
        // Silent no longer owns a destination of its own: Main carries both
        // silent and vector, so legacy routes that asked for 1 land there.
        // Arsenal is gone too; the weapon bar at the bottom replaced it.
        if (s_AimMode == 1 || s_AimMode == 3)
            s_AimMode = 0;
        const int requestedMode = s_AimMode == 4
            ? 4
            : (s_AimMode == 5 ? 5 : ImClamp(s_AimMode, 0, 2));
        if (requestedMode != transition.to)
        {
            transition.from =
                transition.progress < 0.999f
                ? transition.to
                : transition.shown;
            transition.to = requestedMode;
            transition.progress = 0.f;
        }

        if (RenderAimRemoveConfirm(scale))
            return;
        AimWeaponProfile* profile = EditedAimProfile();
        if (!profile)
            return;

        const float dt = ImClamp(
            ImGui::GetIO().DeltaTime,
            0.f,
            0.05f);
        if (transition.progress < 1.f)
        {
            transition.progress = ImClamp(
                transition.progress + dt * 5.f,
                0.f,
                1.f);
            if (transition.progress >= 0.999f)
            {
                transition.progress = 1.f;
                transition.shown = transition.to;
            }
        }
        else
        {
            transition.shown = s_AimMode;
            transition.from = transition.shown;
            transition.to = transition.shown;
        }

        const float transitionEase =
            0.5f - 0.5f *
            cosf(
                transition.progress *
                3.14159265358979323846f);
        // Measured against the Visuals scope row, which leaves 14 px between
        // its strip and the first card; the content child already carries 12 of
        // them in its padding.
        const float topInset = 2.f * scale;
        const ImVec2 contentStart =
            ImGui::GetCursorScreenPos() + ImVec2(0.f, topInset);
        const ImVec2 fullAvail = ImGui::GetContentRegionAvail();
        // The weapon bar owns the bottom strip, so the page never renders a
        // setting behind it.
        const float weaponBarWidth = fullAvail.x;
        AimWeaponBarTick();
        const float weaponBarHeight = AimWeaponBarHeight(scale);
        const float weaponBarGap = 8.f * scale;
        const ImVec2 contentSize(
            fullAvail.x,
            ImMax(
                80.f * scale,
                fullAvail.y - topInset - weaponBarHeight - weaponBarGap));
        const float direction =
            transition.to >= transition.from
            ? 1.f
            : -1.f;

        auto RenderModeLayer =
            [&](int mode,
                float alpha,
                float offsetX,
                bool interactive) noexcept
        {
            if (alpha <= 0.002f)
                return;
            ImGui::SetCursorScreenPos(
                contentStart +
                ImVec2(offsetX * scale, 0.f));
            ImGui::PushID(mode);
            ImGui::PushStyleVar(
                ImGuiStyleVar_Alpha,
                ImGui::GetStyle().Alpha * alpha);
            ImGui::PushStyleColor(
                ImGuiCol_ChildBg,
                ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_NoSavedSettings;
            if (!interactive)
                flags |= ImGuiWindowFlags_NoInputs;
            ImGui::BeginChild(
                "##aim_mode_layer",
                contentSize,
                ImGuiChildFlags_None,
                flags);
            if (mode == 0 || mode == 1 || mode == 2 || mode == 4)
            {
                // One scroll for both halves: two independent scrollbars meant
                // the two configs drifted out of step while reading them, and
                // Trigger had no scroll at all so its filters were unreachable.
                const MenuTheme::Metrics& mainMetrics = MenuTheme::Sizes();
                const float columnGap = mainMetrics.space3 * scale;
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                ImGui::PushStyleVar(
                    ImGuiStyleVar_WindowPadding,
                    ImVec2(0.f, 0.f));
                ImGui::BeginChild(
                    "##aim_main_scroll",
                    avail,
                    ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoSavedSettings);
                const float columnW = ImMax(
                    1.f,
                    (ImGui::GetContentRegionAvail().x - columnGap) * 0.5f);

                s_AimStackColumns = true;
                // Cards inside a column are spaced by their own leading gap
                // only; the inherited item spacing stacked on top of it and
                // pushed the filters card far away from the settings card.
                ImGui::PushStyleVar(
                    ImGuiStyleVar_ItemSpacing,
                    ImVec2(0.f, 0.f));
                ImGui::BeginChild(
                    "##aim_column_left",
                    ImVec2(columnW, 0.f),
                    ImGuiChildFlags_AutoResizeY,
                        ImGuiWindowFlags_NoSavedSettings |
                        ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse);
                if (mode == 4)
                    RenderAimDamager(true);
                else if (mode == 2)
                    RenderAimTrigger(*profile, true);
                else
                    RenderAimSilent(*profile, true);
                ImGui::EndChild();
                ImGui::SameLine(0.f, columnGap);
                ImGui::BeginChild(
                    "##aim_column_right",
                    ImVec2(columnW, 0.f),
                    ImGuiChildFlags_AutoResizeY,
                        ImGuiWindowFlags_NoSavedSettings |
                        ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse);
                if (mode == 4)
                    RenderAimDamagerFilters(*profile, true);
                else if (mode == 2)
                    RenderAimTriggerFilters(*profile, true);
                else
                    RenderAimVector(*profile, true);
                ImGui::EndChild();
                ImGui::PopStyleVar();
                s_AimStackColumns = false;
                ImGui::EndChild();
                ImGui::PopStyleVar();
            }
            else if (mode == 5)
            {
                RenderAimOtherLayer(alpha, offsetX, interactive);
            }
            else
                WeaponManagerUi::Render();
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            ImGui::PopID();
        };

        // Taken before the controls are drawn and compared after: a change to
        // any of them is what turns a weapon group into a configuration of its
        // own, without asking for a switch to be flipped first.
        const AimWeaponProfile beforeEdit = *profile;
        if (transition.progress < 1.f)
        {
            RenderModeLayer(
                transition.from,
                1.f - transitionEase,
                -direction * 18.f * transitionEase,
                false);
            RenderModeLayer(
                transition.to,
                transitionEase,
                direction * 18.f *
                    (1.f - transitionEase),
                false);
        }
        else
        {
            RenderModeLayer(
                transition.shown,
                1.f,
                0.f,
                true);
        }
        MarkEditedScopeConfigured(beforeEdit, profile);
        DrawAimWeaponBar(
            ImVec2(
                contentStart.x,
                contentStart.y + contentSize.y + weaponBarGap),
            weaponBarWidth,
            scale);

        ImGui::SetCursorScreenPos(
            contentStart +
            ImVec2(0.f, contentSize.y + weaponBarGap + weaponBarHeight));

        // The weapon picker is drawn by the shell: it is a root level modal and
        // cannot be begun from inside the content child.
        s_Aim_IgnoreFriends =
            s_AimWeaponProfiles[0].ignoreFriends;
        PushGeneralToLegacy();
    }

    void RenderCombatPage()
    {
        RenderAimbotPage();
    }
}
