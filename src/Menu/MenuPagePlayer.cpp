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
#include "../Features/AntiCheatShield.h"
#include "../Features/MiscFeatures.h"

namespace Menu
{
    static void RenderPlayerSelfPage()
    {
        auto& s = MiscFeatures::GetSettings();
        const float scale = UIScale();

        MenuUi::BeginInspectorIsland(
            "player_protection",
            T("PROTECTION", "ЗАЩИТА", "ЗАХИСТ"),
            2.f);
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
            "player_greenzone",
            ICON_FA_LEAF,
            T("Greenzone bypass", "Обход зелёнки", "Обхід зеленки"),
            &s.greenzoneBypass);
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
            "player_combat_globals",
            T("WEAPON GLOBALS", "ОРУЖИЕ (ГЛОБАЛЬНО)", "ЗБРОЯ (ГЛОБАЛЬНО)"),
            0.f);
        MiscRow(
            "player_nospread",
            ICON_FA_BULLSEYE,
            T("No spread", "Без разброса", "Без розкиду"),
            &s.noSpread);
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
        MiscRow(
            "player_norecoil",
            ICON_FA_CROSSHAIRS,
            T("No recoil", "Без отдачи", "Без віддачі"),
            &s.noRecoil);
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
        MiscRow(
            "player_reload",
            ICON_FA_ROTATE,
            T("Reload speed", "Скорость перезарядки", "Швидкість перезарядки"),
            &s.reloadSpeed);
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
        MiscRow(
            "player_noreload",
            ICON_FA_BAN,
            T("No reload", "Без перезарядки", "Без перезарядки"),
            &s.noReload);
        MiscRow(
            "player_infammo",
            ICON_FA_INFINITY,
            T("Infinite ammo", "Бесконечные патроны", "Нескінченні набої"),
            &s.infiniteAmmo);
        MiscRow(
            "player_infclip",
            ICON_FA_LAYER_GROUP,
            T("Infinite clip", "Бесконечный магазин", "Нескінченний магазин"),
            &s.infiniteClip);
        MenuUi::EndInspectorIsland();
    }

    static void RenderPlayerMovePage()
    {
        auto& s = MiscFeatures::GetSettings();

        MenuUi::BeginInspectorIsland(
            "player_movement",
            T("MOVEMENT", "ДВИЖЕНИЕ", "РУХ"),
            2.f);
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
                82.f))
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
            gui->keybind(
                T("Activation key###misc_noclip_key",
                  "Клавиша###misc_noclip_key",
                  "Клавіша###misc_noclip_key"),
                &s.noclipKey,
                ICON_FA_KEY);
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
                82.f))
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
            gui->keybind(
                T("Activation key###misc_freecam_key",
                  "Клавиша###misc_freecam_key",
                  "Клавіша###misc_freecam_key"),
                &s.freecamKey,
                ICON_FA_KEY);
            EndAimReveal(s.freecam);
        }

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

    void RenderPlayerPage()
    {
        if (s_MiscSubTab < 0 || s_MiscSubTab > 1)
            s_MiscSubTab = 0;

        MenuUi::BeginPageColumns("player_columns");
        if (s_MiscSubTab == 1)
            RenderPlayerMovePage();
        else
            RenderPlayerSelfPage();
        MenuUi::EndPageColumns();
    }

    void RenderMiscPage()
    {
        RenderPlayerPage();
    }
}
