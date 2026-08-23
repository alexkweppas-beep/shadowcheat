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
    MenuUi::FeatureRowResult MiscRow(
        const char* id,
        const char* icon,
        const char* label,
        bool* enabled,
        bool overflow,
        bool bindable) noexcept
    {
        MenuUi::FeatureRowDesc row{};
        row.icon = icon;
        row.label = label;
        row.enabled = enabled;
        row.overflow = overflow;
        row.bindable = bindable;
        row.iconScale = 0.86f;
        return MenuUi::FeatureRow(id, row);
    }

#if 0
    static void RenderMiscSelfPage()
    {
        auto& s = MiscFeatures::GetSettings();
        const float scale = UIScale();

        MenuUi::BeginInspectorIsland(
            "misc_self_protection",
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
        MenuUi::EndInspectorIsland();

        MenuUi::NextPageColumn();
        MenuUi::BeginInspectorIsland(
            "misc_self_actions",
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
                T("Reset Animations", "Сброс анимаций", "Скинути анімації"),
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
        MenuUi::EndInspectorIsland();
    }

    static void RenderMiscMovePage()
    {
        auto& s = MiscFeatures::GetSettings();

        MenuUi::BeginInspectorIsland(
            "misc_move_character",
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
        MenuUi::EndInspectorIsland();

        MenuUi::NextPageColumn();
        MenuUi::BeginInspectorIsland(
            "misc_move_teleport",
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
        MenuUi::EndInspectorIsland();

        // Quarantined writers remain disabled and have no dead UI controls.
        s.cameraFovEnabled = false;
        s.aspectEnabled = false;
    }

    static const char* VehicleMovementStatusText(
        VehicleMovement::Status status) noexcept
    {
        switch (status)
        {
        case VehicleMovement::Status::Off:
            return T("Off", "Выключено", "Вимкнено");
        case VehicleMovement::Status::NoVehicle:
            return T("Enter a vehicle", "Сядь в машину", "Сядь в авто");
        case VehicleMovement::Status::Passenger:
            return T("Driver only", "Только для водителя", "Лише для водія");
        case VehicleMovement::Status::NativeUnavailable:
            return T("Native runtime unavailable",
                     "Native runtime недоступен",
                     "Native runtime недоступний");
        case VehicleMovement::Status::Active:
            return T("Active", "Активно", "Активно");
        default:
            return T("Unavailable", "Недоступно", "Недоступно");
        }
    }

    static void RenderMiscVehiclePage()
    {
        auto& vm = VehicleMovement::GetSettings();

        MenuUi::BeginInspectorIsland(
            "misc_vehicle_motion",
            T("VEHICLE MOTION", "ДВИЖЕНИЕ АВТО", "РУХ АВТО"),
            2.f);
        MiscRow(
            "misc_vehicle_speed",
            ICON_FA_GAUGE_HIGH,
            T("Speed", "Скорость", "Швидкість"),
            &vm.speedEnable);
        DrawAimAnimatedSlider(
            "misc_vehicle_speed_value",
            vm.speedEnable,
            ICON_FA_GAUGE_HIGH,
            T("Engine multiplier###misc_vehicle_speed_value",
              "Множитель двигателя###misc_vehicle_speed_value",
              "Множник двигуна###misc_vehicle_speed_value"),
            &vm.speedMult,
            1.f,
            10.f,
            "%.1fx");
        MiscRow(
            "misc_vehicle_gravity",
            ICON_FA_ARROW_DOWN,
            T("Gravity / Downforce", "Гравитация / прижим", "Гравітація / притиск"),
            &vm.gravityEnable);
        DrawAimAnimatedSlider(
            "misc_vehicle_gravity_value",
            vm.gravityEnable,
            ICON_FA_ARROW_DOWN,
            T("Downforce###misc_vehicle_gravity_value",
              "Сила прижима###misc_vehicle_gravity_value",
              "Сила притиску###misc_vehicle_gravity_value"),
            &vm.gravityMult,
            1.f,
            VehicleMovement::Settings::kMaxGravity,
            "%.1fx");
        const VehicleMovement::Status motionStatus =
            VehicleMovement::GetStatus();
        MenuUi::StatusChip(
            "misc_vehicle_motion_status",
            VehicleMovementStatusText(motionStatus),
            motionStatus == VehicleMovement::Status::Active);
        MenuUi::EndInspectorIsland();

        MenuUi::NextPageColumn();
        MenuUi::BeginInspectorIsland(
            "misc_vehicle_service",
            T("SERVICE", "ОБСЛУЖИВАНИЕ", "ОБСЛУГОВУВАННЯ"),
            0.f);
        if (MenuUi::ActionButton(
                "misc_vehicle_repair",
                T("Repair current vehicle",
                  "Починить текущую машину",
                  "Полагодити поточне авто"),
                ImVec2(
                    ImGui::GetContentRegionAvail().x,
                    38.f * UIScale()),
                true,
                ICON_FA_WRENCH,
                &MiscFeatures::RequestRepairVehicle))
            MiscFeatures::RequestRepairVehicle();
        MenuUi::EndInspectorIsland();
    }

    // Nearby overlay lives next to the friend list: both answer "who is around".

    void RenderMiscPage()
    {
        const MenuUi::IconTabDesc tabs[] = {
            { "self", ICON_FA_USER,
              T("Self", "Себя", "Себе") },
            { "movement", ICON_FA_PERSON_RUNNING,
              T("Movement", "Движение", "Рух") },
            { "vehicle", ICON_FA_CAR,
              T("Vehicle", "Авто", "Авто") }
        };
        constexpr int kMiscTabCount = IM_ARRAYSIZE(tabs);
        if (s_MiscSubTab < 0 || s_MiscSubTab >= kMiscTabCount)
            s_MiscSubTab = 0;
        MenuUi::IconTabStrip(
            "misc_pages",
            tabs,
            kMiscTabCount,
            &s_MiscSubTab);
        ImGui::Dummy(ImVec2(0.f, 4.f * UIScale()));

        MenuUi::BeginPageColumns("misc_columns");
        switch (s_MiscSubTab)
        {
        case 0:
            RenderMiscSelfPage();
            break;
        case 1:
            RenderMiscMovePage();
            break;
        case 2:
            RenderMiscVehiclePage();
            break;
        default:
            s_MiscSubTab = 0;
            break;
        }
        MenuUi::EndPageColumns();
    }
#endif

    void RenderServerPage()
    {
        if (QuantTools::IsQuantServer())
        {
            QuantTools::RenderUI();
            return;
        }
        if (UkraineTools::IsUkraineServer())
        {
            UkraineTools::RenderUI();
            return;
        }
        if (FiveRpFarmBot::IsFiveRpServer())
        {
            FiveRpFarmBot::RenderUI();
            return;
        }
        MenuUi::EmptyState(
            "server_unsupported",
            ICON_FA_SERVER,
            T("No supported server detected",
              "Поддерживаемый сервер не найден",
              "Підтримуваний сервер не знайдено"),
            T("Quant, Ukraine and GTA5RP tools appear here automatically.",
              "Инструменты Quant, Ukraine и GTA5RP появятся здесь автоматически.",
              "Інструменти Quant, Ukraine і GTA5RP з'являться тут автоматично."),
            220.f);
    }
}
