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
#include "../Features/FiveRpMetadataResolver.h"
#include "../Features/RageV8Runtime.h"
#include "../Features/RuntimeProfiler.h"
#include "../Features/ScriptBridge.h"
#include "../Features/CloudStorage.h"
#include "../Features/FeatureBinds.h"
#include "../Features/ObsBypass.h"
#include "../Features/MeshSilhouette.h"
#include "../Features/PedPreview.h"
#include "../Helpers/LocalTime.h"
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
    namespace
    {
        // The gauge has no plan length to divide by, so it reads remaining time
        // against a month: a full bar means "plenty", and the last days visibly
        // drain it.
        constexpr std::int64_t kSubscriptionGaugeSpan = 30 * 86400;

        void DrawSubscriptionCard(float scale)
        {
            const MenuTheme::Palette& tokens = MenuTheme::Colors();
            const MenuTheme::Metrics& metrics = MenuTheme::Sizes();

            std::int64_t expiresAt = s_AccountExpiresAt;
            if (expiresAt <= 0)
                expiresAt = CloudStorage::SubscriptionEndsAt();
            const std::int64_t now =
                static_cast<std::int64_t>(time(nullptr));
            const std::int64_t left =
                expiresAt > 0 ? expiresAt - now : 0;
            const bool expired = expiresAt <= 0 || left <= 0;
            // Under three days the card switches to amber: this panel is the
            // only place the expiry shows, so it has to raise the alarm itself.
            const bool ending = !expired && left < 3 * 86400;
            const ImVec4 statusColor = expired
                ? tokens.error
                : (ending ? tokens.warning : tokens.success);

            MenuUi::BeginInspectorIsland("flyout_account_island", nullptr, 0.f);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const float width = ImMax(ImGui::GetContentRegionAvail().x, 1.f);

            // --- Header: avatar, nickname, status pill -------------------
            // railExpand 0 keeps NavRailAccount to the avatar disc so the
            // nickname can be clipped against the status pill below.
            const float avatar = 34.f * scale;
            const ImVec2 headerMin = ImGui::GetCursorScreenPos();
            MenuUi::NavRailAccount(
                headerMin,
                avatar,
                s_AccountName,
                nullptr,
                0.f);

            const char* statusText = expired
                ? T("Expired", "Истекла", "Закінчилась")
                : (ending
                    ? T("Ending", "Заканчивается", "Завершується")
                    : T("Active", "Активна", "Активна"));
            ImFont* microFont = MenuType::Micro();
            const float microSize = MenuType::MicroSize();
            const ImVec2 statusSz = microFont->CalcTextSizeA(
                microSize, FLT_MAX, 0.f, statusText);
            const float dot = 4.f * scale;
            const float pillH = microSize + 8.f * scale;
            const float pillW =
                statusSz.x + dot * 2.f + 16.f * scale;
            const ImVec2 pillMin(
                headerMin.x + width - pillW,
                headerMin.y + (avatar - pillH) * 0.5f);
            draw->AddRectFilled(
                pillMin,
                pillMin + ImVec2(pillW, pillH),
                gui->get_clr(statusColor, 0.14f),
                pillH * 0.5f);
            draw->AddCircleFilled(
                ImVec2(
                    pillMin.x + 8.f * scale,
                    pillMin.y + pillH * 0.5f),
                dot,
                gui->get_clr(statusColor, 1.f),
                12);
            draw->AddText(
                microFont,
                microSize,
                ImVec2(
                    pillMin.x + 8.f * scale + dot + 5.f * scale,
                    pillMin.y + (pillH - microSize) * 0.5f),
                gui->get_clr(statusColor, 1.f),
                statusText);

            const char* nick =
                s_AccountName[0] ? s_AccountName : "User";
            ImFont* nickFont = MenuType::Title();
            const float nickSize = MenuType::TitleSize();
            const float nickX = headerMin.x + avatar + 10.f * scale;
            draw->PushClipRect(
                ImVec2(nickX, headerMin.y),
                ImVec2(pillMin.x - 8.f * scale, headerMin.y + avatar),
                true);
            draw->AddText(
                nickFont,
                nickSize,
                ImVec2(nickX, headerMin.y + (avatar - nickSize) * 0.5f),
                gui->get_clr(tokens.textPrimary, 1.f),
                nick);
            draw->PopClipRect();
            ImGui::Dummy(ImVec2(0.f, avatar + metrics.space3 * scale));

            // --- Remaining time -----------------------------------------
            char value[32]{};
            const char* caption = nullptr;
            if (expired)
            {
                lstrcpynA(
                    value,
                    T("No subscription",
                      "Нет подписки",
                      "Немає підписки"),
                    static_cast<int>(sizeof(value)));
            }
            else
            {
                FormatSubscriptionRemaining(
                    value,
                    static_cast<int>(sizeof(value)),
                    expiresAt);
                caption = T("left", "осталось", "залишилось");
            }

            ImFont* valueFont = expired ? MenuType::Title() : MenuType::Mono();
            const float valueSize = expired
                ? MenuType::TitleSize()
                : MenuType::DisplaySize();
            const ImVec2 valuePos = ImGui::GetCursorScreenPos();
            const ImVec2 valueSz = valueFont->CalcTextSizeA(
                valueSize, FLT_MAX, 0.f, value);
            draw->AddText(
                valueFont,
                valueSize,
                valuePos,
                gui->get_clr(
                    expired ? tokens.textMuted : tokens.textPrimary, 1.f),
                value);
            if (caption)
            {
                draw->AddText(
                    microFont,
                    microSize,
                    ImVec2(
                        valuePos.x + valueSz.x + 6.f * scale,
                        valuePos.y + valueSz.y - microSize - 1.f * scale),
                    gui->get_clr(tokens.textFaint, 1.f),
                    caption);
            }
            ImGui::Dummy(
                ImVec2(0.f, valueSz.y + metrics.space2 * scale));

            // --- Gauge ---------------------------------------------------
            const float trackH = 5.f * scale;
            const ImVec2 trackMin = ImGui::GetCursorScreenPos();
            const ImVec2 trackMax = trackMin + ImVec2(width, trackH);
            draw->AddRectFilled(
                trackMin,
                trackMax,
                gui->get_clr(tokens.raised, 1.f),
                trackH * 0.5f);
            if (!expired)
            {
                const float fraction = ImClamp(
                    static_cast<float>(left) /
                        static_cast<float>(kSubscriptionGaugeSpan),
                    0.03f,
                    1.f);
                draw->AddRectFilled(
                    trackMin,
                    ImVec2(
                        trackMin.x + width * fraction,
                        trackMax.y),
                    gui->get_clr(statusColor, 0.92f),
                    trackH * 0.5f);
            }
            ImGui::Dummy(
                ImVec2(0.f, trackH + metrics.space2 * scale));

            // --- Expiry date --------------------------------------------
            char expiry[64]{};
            SYSTEMTIME parts{};
            if (TimeUtil::LocalTimeFromUnix(expiresAt, parts))
            {
                _snprintf(
                    expiry,
                    sizeof(expiry) - 1,
                    "%s %02d.%02d.%04d",
                    T("Until", "До", "До"),
                    parts.wDay,
                    parts.wMonth,
                    parts.wYear);
            }
            else
            {
                lstrcpynA(
                    expiry,
                    T("Not activated", "Не активирована", "Не активована"),
                    static_cast<int>(sizeof(expiry)));
            }
            const ImVec2 expiryPos = ImGui::GetCursorScreenPos();
            ImFont* iconFont = set->icon ? set->icon : microFont;
            const float iconSize = microSize + 1.f * scale;
            draw->AddText(
                iconFont,
                iconSize,
                expiryPos,
                gui->get_clr(tokens.textFaint, 1.f),
                ICON_FA_CALENDAR_DAY);
            draw->AddText(
                microFont,
                microSize,
                ImVec2(
                    expiryPos.x + iconSize + 6.f * scale,
                    expiryPos.y),
                gui->get_clr(tokens.textMuted, 1.f),
                expiry);
            ImGui::Dummy(ImVec2(0.f, microSize + 1.f * scale));
            MenuUi::EndInspectorIsland();
        }

        void DrawGlobalKeysCard()
        {
            MenuUi::BeginInspectorIsland(
                "flyout_keys_island",
                T("Keys", "Клавиши", "Клавіші"),
                0.f);

            gui->keybind(
                T("Open menu", "Открыть меню", "Відкрити меню"),
                &s_MenuToggleKey,
                ICON_FA_KEYBOARD);
            // Capturing must not let the pressed key also reach the toggle or
            // the unload handler in the same frame.
            if (ImGui::IsItemActive())
                FeatureBinds::SetCaptureActive(true);
            // Clearing this bind would lock the user out of their own menu.
            if (s_MenuToggleKey <= 0 && !ImGui::IsItemActive())
                s_MenuToggleKey = VK_F4;

            gui->keybind(
                T("Unload", "Выгрузить", "Вивантажити"),
                &s_UnloadKey,
                ICON_FA_POWER_OFF);
            if (ImGui::IsItemActive())
                FeatureBinds::SetCaptureActive(true);
            MenuUi::EndInspectorIsland();

            // Capture bypass belongs with the other global switches rather
            // than on a feature page: it changes how the overlay is drawn,
            // not what it draws.
            MenuUi::BeginInspectorIsland(
                "flyout_capture_island",
                T("Capture", "Захват", "Захоплення"),
                10.f,
                ObsBypass::StatusText());
            MenuUi::SettingRowDesc obs{};
            obs.icon = ICON_FA_VIDEO;
            obs.label = T(
                "OBS bypass",
                "Обход OBS",
                "Обхід OBS");
            obs.enabled = &set->obs_bypass;
            // Not a feature bind: this changes how the overlay is presented,
            // so a hotkey for it would only cause confusion.
            obs.bindable = false;
            if (MenuUi::SettingRow("flyout_obs_bypass", obs).toggled)
                ObsBypass::SetEnabled(set->obs_bypass);
            MenuUi::EndInspectorIsland();
        }
    }

    void RenderMenuSettingsFlyout()
    {
        const float scale = ImClamp(
            static_cast<float>(s_UIScalePercent) / 100.f,
            1.f,
            2.f);
        SetRenderPhase("flyout:account");
        DrawSubscriptionCard(scale);

        // Two columns are what keep every setting above the fold. The shared
        // BeginPageColumns() cannot be used here: it sizes its children from
        // the available height, and this panel's height comes from its content.
        const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
        const float columnGap = metrics.space3 * scale;
        const float columnWidth =
            (ImMax(ImGui::GetContentRegionAvail().x, 1.f) - columnGap) * 0.5f;

        ImGui::BeginChild(
            "##flyout_column_look",
            ImVec2(columnWidth, 0.f),
            ImGuiChildFlags_AutoResizeY,
            ImGuiWindowFlags_NoSavedSettings);

        SetRenderPhase("flyout:appearance");
        MenuUi::BeginInspectorIsland(
            "flyout_look_island",
            T("Appearance", "Внешний вид", "Зовнішній вигляд"),
            0.f);

        // Only the five rasterised font variants are valid, so the value snaps
        // to the nearest 25%.
        int scalePercent = s_UIScalePercent;
        if (gui->slider_int(
                T("Size###flyout_scale",
                  "Размер###flyout_scale",
                  "Розмір###flyout_scale"),
                &scalePercent,
                100,
                200,
                "%d%%"))
        {
            s_UIScalePercent =
                100 + ImClamp((scalePercent - 100 + 12) / 25, 0, 4) * 25;
        }
        gui->color_edit(
            T("Accent###flyout_accent",
              "Акцент###flyout_accent",
              "Акцент###flyout_accent"),
            func->accent_color);
        MenuTheme::SetAccentBase(
            ImVec4(
                func->accent_color[0],
                func->accent_color[1],
                func->accent_color[2],
                1.f));

        // House accents. These only move the accent ramp; surfaces and text
        // keep their own tokens.
        {
            const float swatch = 20.f * scale;
            const float swatchGap = 7.f * scale;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec4 current = MenuTheme::AccentBase();
            ImGui::PushID("flyout_accent_presets");
            for (int i = 0;
                 i < static_cast<int>(MenuTheme::Preset::Count);
                 ++i)
            {
                const auto preset = static_cast<MenuTheme::Preset>(i);
                const ImVec4 color = MenuTheme::PresetColor(preset);
                if (i > 0)
                    ImGui::SameLine(0.f, swatchGap);
                ImGui::PushID(i);
                ImGui::InvisibleButton("##sw", ImVec2(swatch, swatch));
                const bool hovered = ImGui::IsItemHovered();
                const bool active =
                    fabsf(color.x - current.x) < 0.01f &&
                    fabsf(color.y - current.y) < 0.01f &&
                    fabsf(color.z - current.z) < 0.01f;
                const ImVec2 center = ImGui::GetItemRectMin() +
                    ImVec2(swatch * 0.5f, swatch * 0.5f);
                draw->AddCircleFilled(
                    center,
                    swatch * 0.42f,
                    gui->get_clr(color, hovered || active ? 1.f : 0.82f),
                    20);
                if (active)
                {
                    draw->AddCircle(
                        center,
                        swatch * 0.5f,
                        gui->get_clr(
                            MenuTheme::Colors().textPrimary, 0.85f),
                        20,
                        1.6f * scale);
                }
                if (hovered)
                {
                    ImGui::SetTooltip(
                        "%s",
                        MenuTheme::PresetName(preset));
                }
                if (ImGui::IsItemClicked())
                {
                    MenuTheme::SetPreset(preset);
                    const ImVec4 picked = MenuTheme::AccentBase();
                    func->accent_color[0] = picked.x;
                    func->accent_color[1] = picked.y;
                    func->accent_color[2] = picked.z;
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }

        // The canvas drives every neutral surface, so editing it retints the
        // panels and controls with it.
        static float s_CanvasEdit[4]{};
        static bool s_CanvasEditReady = false;
        if (!s_CanvasEditReady)
        {
            const ImVec4 canvas = MenuTheme::CanvasBase();
            s_CanvasEdit[0] = canvas.x;
            s_CanvasEdit[1] = canvas.y;
            s_CanvasEdit[2] = canvas.z;
            s_CanvasEdit[3] = 1.f;
            s_CanvasEditReady = true;
        }
        if (gui->color_edit(
                T("Background###flyout_canvas",
                  "Фон###flyout_canvas",
                  "Фон###flyout_canvas"),
                s_CanvasEdit))
        {
            MenuTheme::SetCanvasBase(
                ImVec4(
                    s_CanvasEdit[0],
                    s_CanvasEdit[1],
                    s_CanvasEdit[2],
                    1.f));
        }

        int theme = s_LightTheme ? 1 : 0;
        const MenuUi::IconTabDesc themes[] = {
            {
                "dark",
                ICON_FA_MOON,
                T("Dark", "Тёмная", "Темна")
            },
            {
                "light",
                ICON_FA_SUN,
                T("Light", "Светлая", "Світла")
            }
        };
        if (MenuUi::ChipTabs(
                "flyout_theme",
                themes,
                IM_ARRAYSIZE(themes),
                &theme))
        {
            s_LightTheme = theme == 1;
            ApplyLeyawinTheme(s_LightTheme);
        }
        if (MenuUi::ActionButton(
                "flyout_theme_reset",
                T("Reset to default",
                  "Сбросить к стандартной",
                  "Скинути до стандартної"),
                ImVec2(-1.f, 30.f * scale),
                false,
                ICON_FA_ARROW_ROTATE_LEFT))
        {
            s_LightTheme = false;
            ApplyLeyawinTheme(s_LightTheme);
            MenuTheme::ResetToDefault();
            // The accent picker is pushed into the theme every frame, so the
            // edit buffers have to follow the reset or they would undo it.
            const ImVec4 accent = MenuTheme::AccentBase();
            func->accent_color[0] = accent.x;
            func->accent_color[1] = accent.y;
            func->accent_color[2] = accent.z;
            const ImVec4 canvas = MenuTheme::CanvasBase();
            s_CanvasEdit[0] = canvas.x;
            s_CanvasEdit[1] = canvas.y;
            s_CanvasEdit[2] = canvas.z;
            s_CanvasEdit[3] = 1.f;
        }
        MenuUi::EndInspectorIsland();
        ImGui::EndChild();

        ImGui::SameLine(0.f, columnGap);

        ImGui::BeginChild(
            "##flyout_column_input",
            ImVec2(columnWidth, 0.f),
            ImGuiChildFlags_AutoResizeY,
            ImGuiWindowFlags_NoSavedSettings);

        SetRenderPhase("flyout:keys");
        DrawGlobalKeysCard();
        ImGui::EndChild();

        // Full width: the three chips fit on one row here, and it leaves the
        // two columns above ending at the same height.
        SetRenderPhase("flyout:language");
        MenuUi::BeginInspectorIsland(
            "flyout_language_island",
            T("Language", "Язык", "Мова"),
            10.f);
        const MenuUi::ChipDesc langChips[] = {
            {"en", "English"},
            {"ru", "Русский"},
            {"ua", "Українська"}
        };
        int selectedLang = static_cast<int>(s_Language);
        if (MenuUi::ChipStrip(
                "flyout_language",
                langChips,
                IM_ARRAYSIZE(langChips),
                &selectedLang,
                92.f,
                30.f))
        {
            s_Language = static_cast<Language>(
                ImClamp(selectedLang, 0, 2));
        }
        MenuUi::EndInspectorIsland();

        ApplyRawImGuiTheme(ImGui::GetStyle());
    }

    enum class ConfigTileAction : int
    {
        None = 0,
        Load,
        Save,
        SetDefault,
        Rename,
        Delete
    };

    static bool ConfigNameContains(
        const char* text,
        const char* query) noexcept
    {
        if (!query || !query[0])
            return true;
        if (!text)
            return false;
        for (int start = 0; text[start]; ++start)
        {
            int offset = 0;
            while (query[offset] && text[start + offset])
            {
                char a = text[start + offset];
                char b = query[offset];
                if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
                if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
                if (a != b)
                    break;
                ++offset;
            }
            if (!query[offset])
                return true;
        }
        return false;
    }

    static ConfigTileAction DrawConfigTile(
        const char* id,
        const char* title,
        const char* meta,
        float width,
        bool loaded,
        bool isDefault,
        char* editableTitle = nullptr,
        int editableCapacity = 0) noexcept
    {
        struct TileAnim final
        {
            float hover = 0.f;
        };
        const float scale = ImClamp(
            static_cast<float>(s_UIScalePercent) / 100.f,
            1.f,
            2.f);
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 size(width, 88.f * scale);
        ImGui::BeginGroup();
        ImGui::Dummy(size);
        const ImVec2 after = ImGui::GetCursorScreenPos();
        const ImVec2 max = min + size;
        const bool hovered =
            ImGui::IsMouseHoveringRect(min, max, false);
        ImGui::PushID(id ? id : "config_tile");
        TileAnim& anim =
            LeyawinAnim<TileAnim, 7601>().get(
                ImGui::GetID("##state"));
        anim.hover = ImLerp(
            anim.hover,
            hovered ? 1.f : 0.f,
            gui->fixed_speed(16.f));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(
            min,
            max,
            gui->get_clr(
                ImLerp(
                    clr->child_bottom_bar,
                    clr->element_background,
                    anim.hover * 0.55f),
                0.76f),
            14.f * scale);
        draw->AddRect(
            min,
            max,
            gui->get_clr(clr->child_stroke_bar, 0.38f),
            14.f * scale);
        ImFont* iconFont =
            set->icon ? set->icon : ImGui::GetFont();
        draw->AddText(
            iconFont,
            11.f * scale,
            min + ImVec2(12.f, 14.f) * scale,
            gui->get_clr(clr->accent_color),
            isDefault
                ? ICON_FA_STAR
                : ICON_FA_FILE_LINES);
        // Double-clicking the name turns it into a text field; only local slots
        // hand over a buffer, cloud entries stay read-only.
        static ImGuiID s_RenamingTile = 0;
        static bool s_RenameFocus = false;
        const ImGuiID tileId = ImGui::GetID("##name");
        const bool canRename =
            editableTitle != nullptr && editableCapacity > 1;
        const bool renaming = canRename && s_RenamingTile == tileId;
        ConfigTileAction action = ConfigTileAction::None;
        const ImVec2 titleMin = min + ImVec2(35.f, 10.f) * scale;
        const ImVec2 titleMax(
            max.x - 168.f * scale,
            min.y + 36.f * scale);
        if (!renaming)
        {
            gui->render_text_clipped(
                set->poppins_widget,
                titleMin,
                titleMax,
                gui->get_clr(clr->text_active),
                title ? title : "Config",
                {0.f, 0.5f});
            if (canRename &&
                ImGui::IsMouseHoveringRect(titleMin, titleMax, false) &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                s_RenamingTile = tileId;
                s_RenameFocus = true;
            }
        }
        else
        {
            ImGui::SetCursorScreenPos(
                ImVec2(titleMin.x - 4.f * scale, titleMin.y));
            ImGui::SetNextItemWidth(
                ImMax(80.f * scale, titleMax.x - titleMin.x));
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
                gui->get_clr(clr->element_background, 1.f));
            const bool submitted = ImGui::InputText(
                "##rename",
                editableTitle,
                static_cast<size_t>(editableCapacity),
                ImGuiInputTextFlags_EnterReturnsTrue |
                    ImGuiInputTextFlags_AutoSelectAll);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            if (submitted || ImGui::IsItemDeactivated())
            {
                s_RenamingTile = 0;
                action = ConfigTileAction::Rename;
            }
        }
        gui->render_text_clipped(
            set->poppins_widget,
            min + ImVec2(12.f, 40.f) * scale,
            ImVec2(max.x - 168.f * scale, max.y - 10.f * scale),
            gui->get_clr(clr->text, 0.72f),
            meta ? meta : "Config",
            {0.f, 0.5f});

        const ImVec2 moreMin(
            max.x - 36.f * scale,
            min.y + 10.f * scale);
        ImGui::SetCursorScreenPos(moreMin);
        ImGui::InvisibleButton(
            "##more",
            ImVec2(28.f, 28.f) * scale);
        const bool moreHovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked())
        {
            gui->open_popup_w(
                "##config_actions",
                ImGui::GetItemRectMin(),
                ImGui::GetItemRectMax());
        }
        gui->render_text_clipped(
            iconFont,
            moreMin,
            moreMin + ImVec2(28.f, 28.f) * scale,
            gui->get_clr(
                moreHovered
                    ? clr->text_active
                    : clr->text,
                moreHovered ? 1.f : 0.65f),
            ICON_FA_ELLIPSIS,
            {0.5f, 0.5f});

        if (gui->begin_popup_w(
                "##config_actions",
                206.f * scale,
                -1))
        {
            ImGui::TextDisabled(
                "%s",
                T("CONFIG ACTIONS",
                  "ДЕЙСТВИЯ",
                  "ДІЇ"));
            ImGui::Dummy(ImVec2(0.f, 2.f * scale));
            if (MenuUi::ActionButton(
                    "config_set_default",
                    T("Set as default",
                      "Сделать по умолчанию",
                      "Зробити типовим"),
                    ImVec2(
                        ImGui::GetContentRegionAvail().x,
                        31.f * scale),
                    false,
                    ICON_FA_STAR))
            {
                action = ConfigTileAction::SetDefault;
                gui->dismiss_popup_w("##config_actions");
            }
            if (MenuUi::ActionButton(
                    "config_delete",
                    T("Delete", "Удалить", "Видалити"),
                    ImVec2(
                        ImGui::GetContentRegionAvail().x,
                        31.f * scale),
                    false,
                    ICON_FA_TRASH_CAN))
            {
                action = ConfigTileAction::Delete;
                gui->dismiss_popup_w("##config_actions");
            }
            gui->end_popup_w();
        }

        const ImVec2 saveMin(
            max.x - 154.f * scale,
            min.y + 46.f * scale);
        ImGui::SetCursorScreenPos(saveMin);
        if (MenuUi::ActionButton(
                "config_save",
                T("Save", "Сохранить", "Зберегти"),
                ImVec2(70.f, 28.f) * scale,
                true,
                ICON_FA_FLOPPY_DISK))
        {
            action = ConfigTileAction::Save;
        }
        ImGui::SameLine(0.f, 6.f * scale);
        if (MenuUi::ActionButton(
                "config_load",
                T("Load", "Загрузить", "Завантажити"),
                ImVec2(70.f, 28.f) * scale,
                false,
                ICON_FA_DOWNLOAD))
        {
            action = ConfigTileAction::Load;
        }
        (void)loaded;
        ImGui::SetCursorScreenPos(after);
        ImGui::EndGroup();
        ImGui::PopID();
        return action;
    }

    static void RenderConfigSlotsPageRedesign(ImGuiStyle* style)
    {
        (void)style;
        static char search[64]{};
        static int loadedLocal = -1;
        const float scale = ImClamp(
            static_cast<float>(s_UIScalePercent) / 100.f,
            1.f,
            2.f);
        EnsureLocalConfigsLoaded();

        MenuUi::BeginInspectorIsland(
            "config_library_header",
            T("My configs",
              "Мои конфиги",
              "Мої конфіги"),
            0.f,
            T("Stored on this PC",
              "Хранятся на этом ПК",
              "Зберігаються на цьому ПК"));
        const float gap = 8.f * scale;
        const float newWidth = 132.f * scale;
        const float searchWidth = ImMax(
            140.f * scale,
            ImGui::GetContentRegionAvail().x - newWidth - gap);
        MenuUi::SearchRow(
            "config_library_search",
            T("Search configs...",
              "Поиск конфигов...",
              "Пошук конфігів..."),
            search,
            sizeof(search),
            ImVec2(searchWidth, 34.f * scale));
        ImGui::SameLine(0.f, gap);
        if (MenuUi::ActionButton(
                "config_library_new",
                T("New config",
                  "Новый конфиг",
                  "Новий конфіг"),
                ImVec2(newWidth, 34.f * scale),
                true,
                ICON_FA_FILE_CIRCLE_PLUS) &&
            ConfigSlotCount() < set->kConfigSlots)
        {
            char generated[32]{};
            for (int number = 1; number <= 99; ++number)
            {
                _snprintf(
                    generated,
                    sizeof(generated) - 1,
                    "New Config %d",
                    number);
                bool exists = false;
                for (int i = 0; i < ConfigSlotCount(); ++i)
                    if (_stricmp(
                            set->selection_config[i],
                            generated) == 0)
                        exists = true;
                if (!exists)
                    break;
            }
            const int slot = ConfigSlotCount()++;
            lstrcpynA(
                set->selection_config[slot],
                generated,
                sizeof(set->selection_config[slot]));
            ExportCurrentConfig(
                LocalConfigSlot(slot),
                LocalConfigSlotCapacity());
            StampLocalConfig(slot, true);
            loadedLocal = slot;
            SaveLocalConfigs();
        }
        MenuUi::EndInspectorIsland();

        const float gridHeight =
            ImGui::GetContentRegionAvail().y;
        ImGui::BeginChild(
            "##config_tile_grid",
            ImVec2(0.f, gridHeight),
            ImGuiChildFlags_None,
            ImGuiWindowFlags_NoSavedSettings);
        const float tileGap = 9.f * scale;
        // Scrollbar space is reserved up front. Deriving it from the animated
        // page height made the tiles resize on every frame of the category
        // transition, which is what read as a stutter.
        const float tileWidth =
            (ImGui::GetContentRegionAvail().x -
             ImGui::GetStyle().ScrollbarSize - tileGap) *
            0.5f;
        int shown = 0;
        bool stop = false;
        for (int i = 0; i < ConfigSlotCount() && !stop; ++i)
        {
            if (!ConfigNameContains(
                    set->selection_config[i],
                    search))
                continue;
            if ((shown % 2) == 1)
                ImGui::SameLine(0.f, tileGap);
            char meta[96]{};
            const std::int64_t stamp =
                LocalConfigModifiedAt(i) > 0
                ? LocalConfigModifiedAt(i)
                : LocalConfigCreatedAt(i);
            SYSTEMTIME local{};
            if (TimeUtil::LocalTimeFromUnix(stamp, local))
                _snprintf(
                    meta,
                    sizeof(meta) - 1,
                    "%s · %02d.%02d.%02d %02d:%02d",
                    i == DefaultConfigSlot()
                        ? T("Default", "По умолчанию", "Типовий")
                        : T("Saved", "Сохранён", "Збережено"),
                    local.wDay,
                    local.wMonth,
                    local.wYear % 100,
                    local.wHour,
                    local.wMinute);
            else
                lstrcpynA(
                    meta,
                    T("Config", "Конфиг", "Конфіг"),
                    sizeof(meta));
            char id[32]{};
            _snprintf(id, sizeof(id) - 1, "local_cfg_%d", i);
            const ConfigTileAction action =
                DrawConfigTile(
                    id,
                    set->selection_config[i],
                    meta,
                    tileWidth,
                    loadedLocal == i,
                    DefaultConfigSlot() == i,
                    set->selection_config[i],
                    static_cast<int>(sizeof(set->selection_config[i])));
            if (action == ConfigTileAction::Save)
            {
                ExportCurrentConfig(
                    LocalConfigSlot(i),
                    LocalConfigSlotCapacity());
                StampLocalConfig(i, false);
                loadedLocal = i;
                SaveLocalConfigs();
            }
            else if (action == ConfigTileAction::Load)
            {
                ImportCurrentConfig(LocalConfigSlot(i));
                loadedLocal = i;
            }
            else if (action == ConfigTileAction::SetDefault)
            {
                DefaultConfigSlot() = i;
                SaveLocalConfigs();
            }
            else if (action == ConfigTileAction::Rename)
            {
                SaveLocalConfigs();
            }
            else if (action == ConfigTileAction::Delete)
            {
                for (int j = i; j + 1 < ConfigSlotCount(); ++j)
                {
                    memcpy(
                        set->selection_config[j],
                        set->selection_config[j + 1],
                        sizeof(set->selection_config[j]));
                    MoveLocalConfigSlot(j, j + 1);
                }
                --ConfigSlotCount();
                if (loadedLocal == i)
                    loadedLocal = -1;
                else if (loadedLocal > i)
                    --loadedLocal;
                int& fallback = DefaultConfigSlot();
                if (fallback == i)
                    fallback = -1;
                else if (fallback > i)
                    --fallback;
                SaveLocalConfigs();
                stop = true;
            }
            ++shown;
        }
        if (shown == 0)
        {
            MenuUi::EmptyState(
                "config_library_empty",
                ICON_FA_FILE_LINES,
                T("No configs",
                  "Конфигов нет",
                  "Конфігів немає"),
                T("Create a config or change the filter",
                  "Создай конфиг или измени фильтр",
                  "Створи конфіг або зміни фільтр"),
                150.f);
        }
        ImGui::EndChild();
    }

    // Session shows only authoritative account state; no invented status.
    void RenderConfigPage(ImGuiStyle* style)
    {
        s_ConfigSubTab = 0;
        RenderConfigSlotsPageRedesign(style);
    }
}
