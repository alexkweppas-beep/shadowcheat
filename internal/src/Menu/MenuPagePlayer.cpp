#include "MenuPages.h"

#define IMGUI_DEFINE_MATH_OPERATORS

#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"
#include "Leyawin/custom_widgets.hpp"
#include "Leyawin/leyawin_globals.h"
#include "Leyawin/leyawin_init.h"
#include "IconsFontAwesome6.h"
#include "MenuUiHelpers.h"
#include "MenuState.h"
#include "MenuTheme.h"
#include "../Features/AntiCheatShield.h"
#include "../Features/AutoLoot.h"
#include "../Features/MiscFeatures.h"
#include "../Features/ServerProfile.h"

#include <cfloat>
#include <cstdio>

namespace Menu
{
    static void RenderPlayerSelfPage()
    {
        auto& s = MiscFeatures::GetSettings();
        const float scale = UIScale();

        MenuUi::BeginInspectorIsland(
            "player_protection",
            T("PROTECTION", "ЗАЩИТА", "ЗАХИСТ"),
            0.f);
        MiscRow(
            "misc_god",
            ICON_FA_SHIELD_HEART,
            T("God Mode", "Режим бога", "Режим бога"),
            &s.godMode);
        MiscRow(
            "misc_fixed_hp",
            ICON_FA_HEART,
            T("Fixed HP", "Фикс HP", "Фікс HP"),
            &s.fixedHp);
        DrawAimAnimatedSlider(
            "misc_fixed_hp_value",
            s.fixedHp,
            ICON_FA_HEART_PULSE,
            T("Health###misc_fixed_hp_value",
              "Здоровье###misc_fixed_hp_value",
              "Здоров'я###misc_fixed_hp_value"),
            &s.fixedHpValue,
            1.f,
            100.f,
            "%.0f HP");
        MiscRow(
            "misc_fixed_ap",
            ICON_FA_SHIELD,
            T("Fixed Armor", "Фикс брони", "Фікс броні"),
            &s.fixedAp);
        DrawAimAnimatedSlider(
            "misc_fixed_ap_value",
            s.fixedAp,
            ICON_FA_SHIELD,
            T("Armor###misc_fixed_ap_value",
              "Броня###misc_fixed_ap_value",
              "Броня###misc_fixed_ap_value"),
            &s.fixedApValue,
            0.f,
            100.f,
            "%.0f AP");
        MiscRow(
            "misc_anti_afk",
            ICON_FA_USER_CLOCK,
            T("Anti-AFK", "Анти-АФК", "Анти-АФК"),
            &s.antiAfk);
        MiscRow(
            "player_stamina",
            ICON_FA_PERSON_RUNNING,
            T("Unlimited stamina", "Бесконечная выносливость", "Нескінченна витривалість"),
            &s.unlimitedStamina);

        MiscRow(
            "player_ac_shield",
            ICON_FA_SHIELD_HALVED,
            T("Disable anti-cheat", "Отключение античита", "Відключення античиту"),
            &AntiCheatShield::Enabled());
        MenuUi::EndInspectorIsland();

        MenuUi::NextPageColumn();
        MenuUi::BeginInspectorIsland(
            "player_actions",
            T("ACTIONS", "ДЕЙСТВИЯ", "ДІЇ"),
            0.f);
        const float gap = 8.f * scale;
        const float half = ImMax(
            120.f * scale,
            (ImGui::GetContentRegionAvail().x - gap) * 0.5f);
        if (MenuUi::ActionButton(
                "misc_heal",
                T("Heal", "Вылечить", "Вилікувати"),
                ImVec2(half, 34.f * scale),
                false,
                ICON_FA_KIT_MEDICAL,
                &MiscFeatures::RequestHeal))
            MiscFeatures::RequestHeal();
        ImGui::SameLine(0.f, gap);
        if (MenuUi::ActionButton(
                "misc_armor",
                T("Give Armor", "Дать броню", "Дати броню"),
                ImVec2(half, 34.f * scale),
                false,
                ICON_FA_SHIELD,
                &MiscFeatures::RequestArmor))
            MiscFeatures::RequestArmor();
        if (MenuUi::ActionButton(
                "misc_respawn",
                T("Respawn", "Респавн", "Респавн"),
                ImVec2(half, 34.f * scale),
                false,
                ICON_FA_ROTATE,
                &MiscFeatures::RequestRespawn))
            MiscFeatures::RequestRespawn();
        ImGui::SameLine(0.f, gap);
        if (MenuUi::ActionButton(
                "misc_reset_anim",
                T("Clear tasks", "Сброс задач", "Скинути задачі"),
                ImVec2(half, 34.f * scale),
                false,
                ICON_FA_PERSON,
                &MiscFeatures::RequestResetAnimations))
            MiscFeatures::RequestResetAnimations();
        if (MenuUi::ActionButton(
                "misc_suicide",
                T("Suicide", "Самоубийство", "Самогубство"),
                ImVec2(ImGui::GetContentRegionAvail().x, 34.f * scale),
                false,
                ICON_FA_SKULL,
                &MiscFeatures::RequestSuicide))
            MiscFeatures::RequestSuicide();
        MiscRow(
            "misc_respawn_inplace",
            ICON_FA_MAP_PIN,
            T("Respawn where you fell",
              "Респавн на месте смерти",
              "Респавн на місці смерті"),
            &s.respawnInPlace);
        MenuUi::EndInspectorIsland();

        MenuUi::BeginInspectorIsland(
            "player_collision",
            nullptr,
            0.f);


        // Applies to whatever the player is riding in: seated it is the car
        // that has to pass through, on foot the ped.
        MiscRow(
            "misc_nocol_ped",
            ICON_FA_PERSON,
            T("No collision: players",
              "Без коллизии: игроки",
              "Без колізії: гравці"),
            &s.disablePlayerCollision);
        MiscRow(
            "misc_nocol_veh",
            ICON_FA_CAR,
            T("No collision: vehicles",
              "Без коллизии: транспорт",
              "Без колізії: транспорт"),
            &s.disableCarCollision);
        MiscRow(
            "misc_nocol_obj",
            ICON_FA_CUBE,
            T("No collision: objects",
              "Без коллизии: объекты",
              "Без колізії: об'єкти"),
            &s.disableObjectCollision);
        MenuUi::EndInspectorIsland();

    }

    static void RenderPlayerMovePage()
    {
        auto& s = MiscFeatures::GetSettings();

        MenuUi::BeginInspectorIsland(
            "player_movement",
            T("MOVEMENT", "ДВИЖЕНИЕ", "РУХ"),
            0.f);
        MiscRow(
            "misc_speedhack",
            ICON_FA_PERSON_RUNNING,
            T("Speedhack (on foot)", "Спидхак (пешком)", "Спідхак (пішки)"),
            &s.speedhack);
        DrawAimAnimatedSlider(
            "misc_speedhack_value",
            s.speedhack,
            ICON_FA_GAUGE_HIGH,
            T("Run multiplier###misc_speedhack_value",
              "Множитель бега###misc_speedhack_value",
              "Множник бігу###misc_speedhack_value"),
            &s.speedhackValue,
            1.f,
            10.f,
            "%.1fx");

        MiscRow(
            "misc_noclip",
            ICON_FA_GHOST,
            "Noclip",
            &s.noclip);
        if (BeginAimReveal(
                "misc_noclip_details",
                s.noclip,
                42.f))
        {
            gui->slider_float_icon(
                ICON_FA_GAUGE_HIGH,
                T("Speed###misc_noclip_speed",
                  "Скорость###misc_noclip_speed",
                  "Швидкість###misc_noclip_speed"),
                &s.noclipSpeed,
                0.2f,
                20.f,
                "%.1f");
            EndAimReveal(s.noclip);
        }

        MiscRow(
            "misc_freecam",
            ICON_FA_VIDEO,
            "Freecam",
            &s.freecam);
        if (BeginAimReveal(
                "misc_freecam_details",
                s.freecam,
                42.f))
        {
            gui->slider_float_icon(
                ICON_FA_GAUGE_HIGH,
                T("Speed###misc_freecam_speed",
                  "Скорость###misc_freecam_speed",
                  "Швидкість###misc_freecam_speed"),
                &s.freecamSpeed,
                0.2f,
                20.f,
                "%.1f");
            EndAimReveal(s.freecam);
        }


        MenuUi::EndInspectorIsland();

        MenuUi::NextPageColumn();
        MenuUi::BeginInspectorIsland(
            "player_teleport",
            T("TELEPORT", "ТЕЛЕПОРТ", "ТЕЛЕПОРТ"),
            0.f);
        if (MenuUi::ActionButton(
                "misc_tp_waypoint",
                T("Teleport to Waypoint", "ТП по метке", "ТП по мітці"),
                ImVec2(
                    ImGui::GetContentRegionAvail().x,
                    36.f * UIScale()),
                true,
                ICON_FA_LOCATION_DOT))
            MiscFeatures::RequestTeleportWaypoint();
        gui->keybind(
            T("Waypoint key###misc_tp_waypoint_key",
              "Клавиша ТП###misc_tp_waypoint_key",
              "Клавіша ТП###misc_tp_waypoint_key"),
            &s.tpWaypointKey,
            ICON_FA_KEY);
        MiscRow(
            "player_clickwarp",
            ICON_FA_COMPUTER_MOUSE,
            T("Click warp", "ТП по клику", "ТП по кліку"),
            &s.clickWarp);
        if (BeginAimReveal(
                "player_clickwarp_details",
                s.clickWarp,
                56.f))
        {
            gui->keybind(
                T("Warp key###player_clickwarp_key",
                  "Клавиша варпа###player_clickwarp_key",
                  "Клавіша варпу###player_clickwarp_key"),
                &s.clickWarpKey,
                ICON_FA_KEY);
            EndAimReveal(s.clickWarp);
        }
        MenuUi::EndInspectorIsland();

        s.cameraFovEnabled = false;
        s.aspectEnabled = false;
    }

    // Opened from a button inside the page's islands, which sit under pushed
    // IDs; OpenPopup has to be called at the same level as BeginPopup, so the
    // click only raises a flag and the window is opened after the columns end.
    static bool s_LootFilterOpenRequest = false;
    static char s_LootNameInput[AutoLoot::kFilterLength]{};

    // A bare glyph rather than ActionButton: that one always lays a filled
    // plate and a border under its icon, which turned the list into a column of
    // little boxes. Tinted on hover so it still reads as clickable.
    static bool LootTrashButton(float size) noexcept
    {
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const bool pressed = ImGui::InvisibleButton(
            "##loot_trash",
            ImVec2(size, size));
        const bool hovered = ImGui::IsItemHovered();
        if (set && set->icon)
        {
            const MenuTheme::Palette& tokens = MenuTheme::Colors();
            const float glyph = size * 0.72f;
            const ImVec2 extent = set->icon->CalcTextSizeA(
                glyph,
                FLT_MAX,
                0.f,
                ICON_FA_TRASH);
            ImGui::GetWindowDrawList()->AddText(
                set->icon,
                glyph,
                min + (ImVec2(size, size) - extent) * 0.5f,
                gui->get_clr(
                    hovered ? tokens.error : tokens.textFaint,
                    1.f),
                ICON_FA_TRASH);
        }
        return pressed;
    }

    static const char* LootFilterPopupId() noexcept
    {
        return T(
            "Item list###loot_filter_editor",
            "Список предметов###loot_filter_editor",
            "Список предметів###loot_filter_editor");
    }

    static void RenderLootFilterEditor(float scale) noexcept
    {
        ImGui::SetNextWindowSize(
            ImVec2(430.f, 0.f) * scale,
            ImGuiCond_Appearing);
        // A popup, not a modal: clicking away is how it is meant to close, and
        // a modal would swallow that click by design.
        if (!ImGui::BeginPopup(
                LootFilterPopupId(),
                ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiWindowFlags_NoSavedSettings))
            return;

        {
            const float closeSize = 26.f * scale;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(
                T("Item list", "Список предметов", "Список предметів"));
            ImGui::SameLine(0.f, 0.f);
            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() +
                ImMax(0.f, ImGui::GetContentRegionAvail().x - closeSize));
            if (MenuUi::GhostIconButton(
                    "loot_filter_x",
                    ICON_FA_XMARK,
                    ImVec2(closeSize, closeSize)))
            {
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return;
            }
        }

        const int count = AutoLoot::FilterCount();
        const bool full = count >= AutoLoot::kMaxFilters;

        MenuUi::BeginInspectorIsland(
            "loot_filter_add_island",
            T("ADD ITEM", "ДОБАВИТЬ ПРЕДМЕТ", "ДОДАТИ ПРЕДМЕТ"),
            8.f,
            T("Part of the name is enough: the server appends state, as in "
              "\"Carbine Rifle (96%)\".",
              "Достаточно части названия: сервер дописывает состояние, "
              "например \"Carbine Rifle (96%)\".",
              "Достатньо частини назви: сервер дописує стан, наприклад "
              "\"Carbine Rifle (96%)\"."));
        const float addWidth = 92.f * scale;
        gui->text_field(
            "##loot_add_name",
            T("Name", "Название", "Назва"),
            ImVec2(
                ImMax(110.f * scale,
                      ImGui::GetContentRegionAvail().x -
                          addWidth - 7.f * scale),
                32.f * scale),
            s_LootNameInput,
            sizeof(s_LootNameInput));
        ImGui::SameLine(0.f, 7.f * scale);
        if (MenuUi::ActionButton(
                "loot_filter_add",
                T("Add", "Добавить", "Додати"),
                ImVec2(addWidth, 32.f * scale),
                !full,
                ICON_FA_CIRCLE_PLUS) &&
            !full)
        {
            if (AutoLoot::AddFilter(s_LootNameInput))
                s_LootNameInput[0] = '\0';
        }
        if (full)
            ImGui::TextDisabled(
                "%s",
                T("List is full.", "Список заполнен.",
                  "Список заповнений."));
        MenuUi::EndInspectorIsland();

        MenuUi::BeginInspectorIsland(
            "loot_filter_list_island",
            T("ENTRIES", "ЗАПИСИ", "ЗАПИСИ"),
            8.f);
        if (count <= 0)
        {
            // Stated plainly: an empty list looks exactly like a broken one.
            const char* empty = T(
                "Empty - nothing will be picked up",
                "Пусто - ничего не будет подбираться",
                "Порожньо - нічого не підбиратиметься");
            const float width = ImGui::CalcTextSize(empty).x;
            ImGui::Dummy(ImVec2(0.f, 6.f * scale));
            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() +
                ImMax(0.f,
                      (ImGui::GetContentRegionAvail().x - width) * 0.5f));
            ImGui::TextDisabled("%s", empty);
            ImGui::Dummy(ImVec2(0.f, 6.f * scale));
        }
        // The seeded list runs to several dozen weapons, so it scrolls instead
        // of growing the window past the screen.
        ImGui::BeginChild(
            "loot_filter_rows",
            ImVec2(0.f, ImMin(300.f, count * 26.f + 8.f) * scale),
            false);
        int remove = -1;
        for (int index = 0; index < count; ++index)
        {
            ImGui::PushID(index);
            const float trashSize = 22.f * scale;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(AutoLoot::FilterAt(index));
            ImGui::SameLine(0.f, 0.f);
            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() +
                ImMax(0.f, ImGui::GetContentRegionAvail().x - trashSize));
            if (LootTrashButton(trashSize))
                remove = index;
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (remove >= 0)
            AutoLoot::RemoveFilter(remove);
        MenuUi::EndInspectorIsland();

        ImGui::EndPopup();
    }

    static void RenderPlayerLootPage()
    {
        auto& s = AutoLoot::Access();
        const AutoLoot::Status status = AutoLoot::GetStatus();

        MenuUi::BeginInspectorIsland(
            "loot_main",
            T("AUTO LOOT", "АВТОЛУТ", "АВТОЛУТ"),
            0.f);
        MiscRow(
            "loot_enabled",
            ICON_FA_HAND_HOLDING,
            T("Pick up ground items",
              "Подбирать предметы с земли",
              "Підбирати предмети з землі"),
            &s.enabled);
        gui->slider_float_icon(
            ICON_FA_RULER_HORIZONTAL,
            T("Radius###loot_radius",
              "Радиус###loot_radius",
              "Радіус###loot_radius"),
            &s.radius,
            AutoLoot::kMinRadius,
            AutoLoot::kMaxRadius,
            "%.1f m");
        // The ring is drawn on the ground, and past the server's own limit it
        // goes dashed with the honoured part inside it, so the two distances
        // are one picture instead of a number and a warning.
        MiscRow(
            "loot_show_radius",
            ICON_FA_CIRCLE_NOTCH,
            T("Show radius", "Показывать радиус", "Показувати радіус"),
            &s.showRadius);
        // Past the measured limit the slider still moves, but it is worth
        // saying whose rule it is, since a refusal there is not a bug here.
        if (s.radius > AutoLoot::kServerLimit)
            MenuUi::LabelWithHelp(
                T("Past the server limit",
                  "Больше лимита сервера",
                  "Більше ліміту сервера"),
                "Takes were accepted at 7 m and refused at 12, so most items "
                "out here will be turned down. Refused items back off instead "
                "of being retried.",
                "Подъём принимался на 7 м и отклонялся на 12, так что дальше "
                "почти всё будет отклонено. Отказавшие предметы уходят в "
                "паузу, а не долбятся повторно.",
                "Підйом приймався на 7 м і відхилявся на 12, тож далі майже "
                "все буде відхилено. Відмовлені предмети йдуть у паузу, а не "
                "запитуються повторно.");
        float delay = static_cast<float>(s.intervalMs);
        if (gui->slider_float_icon(
                ICON_FA_STOPWATCH,
                T("Delay###loot_delay",
                  "Задержка###loot_delay",
                  "Затримка###loot_delay"),
                &delay,
                250.f,
                3000.f,
                "%.0f ms"))
            s.intervalMs = static_cast<int>(delay);
        MenuUi::EndInspectorIsland();

        MenuUi::NextPageColumn();

        MenuUi::BeginInspectorIsland(
            "loot_filter",
            T("WHAT TO TAKE", "ЧТО ПОДБИРАТЬ", "ЩО ПІДБИРАТИ"),
            0.f);
        MiscRow(
            "loot_take_all",
            ICON_FA_BOXES_STACKED,
            T("Take everything", "Собирать всё", "Збирати все"),
            &s.takeAll);
        char listLabel[64]{};
        _snprintf_s(
            listLabel,
            sizeof(listLabel),
            _TRUNCATE,
            "%s  %d",
            T("Edit list", "Список предметов", "Список предметів"),
            AutoLoot::FilterCount());
        if (MenuUi::ActionButton(
                "loot_open_list",
                listLabel,
                ImVec2(
                    ImGui::GetContentRegionAvail().x,
                    34.f * UIScale()),
                !s.takeAll,
                ICON_FA_FILTER))
            s_LootFilterOpenRequest = true;
        if (s.takeAll)
            ImGui::TextDisabled(
                "%s",
                T("The list applies once this is off",
                  "Список работает, когда это выключено",
                  "Список працює, коли це вимкнено"));
        MenuUi::EndInspectorIsland();

        MenuUi::BeginInspectorIsland(
            "loot_status",
            T("STATUS", "СОСТОЯНИЕ", "СТАН"),
            0.f);
        // Reachable next to known: an idle run on an empty street reads exactly
        // like a filter that matches nothing unless both numbers are shown.
        char summary[160]{};
        _snprintf_s(
            summary,
            sizeof(summary),
            _TRUNCATE,
            T("In range: %d of %d known\nTaken %d, refused %d",
              "В радиусе: %d из %d известных\nПоднято %d, отказов %d",
              "У радіусі: %d з %d відомих\nПіднято %d, відмов %d"),
            status.reachable,
            status.knownItems,
            status.taken,
            status.refused);
        ImGui::TextUnformatted(summary);
        if (status.slotsKnown)
        {
            char slots[96]{};
            _snprintf_s(
                slots,
                sizeof(slots),
                _TRUNCATE,
                T("Free inventory slots: %d",
                  "Свободных слотов: %d",
                  "Вільних слотів: %d"),
                status.freeSlots);
            ImGui::TextUnformatted(slots);
        }
        if (status.detail[0])
            ImGui::TextUnformatted(status.detail);
        MenuUi::EndInspectorIsland();
    }

    void RenderPlayerPage()
    {
        if (s_MiscSubTab < 0 || s_MiscSubTab > 2)
            s_MiscSubTab = 0;

        MenuUi::BeginPageColumns("player_columns");
        if (s_MiscSubTab == 2)
            RenderPlayerLootPage();
        else if (s_MiscSubTab == 1)
            RenderPlayerMovePage();
        else
            RenderPlayerSelfPage();
        MenuUi::EndPageColumns();

        if (s_MiscSubTab == 2)
        {
            if (s_LootFilterOpenRequest)
            {
                s_LootFilterOpenRequest = false;
                ImGui::OpenPopup(LootFilterPopupId());
            }
            RenderLootFilterEditor(UIScale());
        }
    }

    void RenderMiscPage()
    {
        RenderPlayerPage();
    }
}
