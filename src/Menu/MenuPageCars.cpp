#include "MenuPages.h"

#define IMGUI_DEFINE_MATH_OPERATORS

#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"
#include "Leyawin/leyawin_globals.h"
#include "Leyawin/leyawin_init.h"
#include "IconsFontAwesome6.h"
#include "MenuUiHelpers.h"
#include "MenuState.h"
#include "../Features/MiscFeatures.h"
#include "../Features/VehicleMovement.h"

namespace Menu
{
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

    void RenderCarsPage()
    {
        auto& vm = VehicleMovement::GetSettings();
        const float scale = UIScale();

        MenuUi::BeginPageColumns("cars_columns");
        MenuUi::BeginInspectorIsland(
            "cars_motion",
            T("MOTION", "ДВИЖЕНИЕ", "РУХ"),
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
            10.f,
            "%.1fx");
        MiscRow(
            "cars_fly",
            ICON_FA_PLANE,
            T("Fly", "Полёт", "Політ"),
            &vm.fly);
        DrawAimAnimatedSlider(
            "cars_fly_speed",
            vm.fly,
            ICON_FA_GAUGE_HIGH,
            T("Fly speed###cars_fly_speed",
              "Скорость полёта###cars_fly_speed",
              "Швидкість польоту###cars_fly_speed"),
            &vm.flySpeed,
            0.2f,
            20.f,
            "%.1f");
        MiscRow(
            "cars_seatbelt",
            ICON_FA_SHIELD,
            T("Seatbelt", "Ремень", "Ремінь"),
            &vm.seatbelt);
        MiscRow(
            "cars_skip_anim",
            ICON_FA_FORWARD,
            T("Skip enter / exit anim", "Пропуск анимации входа", "Пропуск анімації входу"),
            &vm.skipEnterAnim);
        MiscRow(
            "cars_autopilot",
            ICON_FA_ROUTE,
            T("Autopilot to waypoint", "Автопилот к метке", "Автопілот до мітки"),
            &vm.autopilot);
        MiscRow(
            "cars_engine_force",
            ICON_FA_FIRE,
            T("Keep engine on", "Держать мотор", "Тримати мотор"),
            &vm.engineForceOn);
        const VehicleMovement::Status motionStatus =
            VehicleMovement::GetStatus();
        MenuUi::StatusChip(
            "cars_motion_status",
            VehicleMovementStatusText(motionStatus),
            motionStatus == VehicleMovement::Status::Active);
        MenuUi::EndInspectorIsland();

        MenuUi::NextPageColumn();
        MenuUi::BeginInspectorIsland(
            "cars_service",
            T("SERVICE", "ОБСЛУЖИВАНИЕ", "ОБСЛУГОВУВАННЯ"),
            0.f);
        const float gap = 8.f * scale;
        const float half = ImMax(
            120.f * scale,
            (ImGui::GetContentRegionAvail().x - gap) * 0.5f);
        if (MenuUi::ActionButton(
                "misc_vehicle_repair",
                T("Repair", "Починить", "Полагодити"),
                ImVec2(half, 34.f * scale),
                false,
                ICON_FA_WRENCH))
            MiscFeatures::RequestRepairVehicle();
        ImGui::SameLine(0.f, gap);
        if (MenuUi::ActionButton(
                "cars_engine",
                T("Start engine", "Завести", "Завести"),
                ImVec2(half, 34.f * scale),
                false,
                ICON_FA_KEY))
            VehicleMovement::RequestEngineOn();
        if (MenuUi::ActionButton(
                "cars_stop",
                T("Stop", "Стоп", "Стоп"),
                ImVec2(half, 34.f * scale),
                false,
                ICON_FA_HAND))
            VehicleMovement::RequestStop();
        ImGui::SameLine(0.f, gap);
        if (MenuUi::ActionButton(
                "cars_flip",
                T("Flip", "Перевернуть", "Перевернути"),
                ImVec2(half, 34.f * scale),
                false,
                ICON_FA_ARROWS_ROTATE))
            VehicleMovement::RequestFlip();
        if (MenuUi::ActionButton(
                "cars_crash",
                T("Crash / explode", "Авария / взрыв", "Аварія / вибух"),
                ImVec2(ImGui::GetContentRegionAvail().x, 34.f * scale),
                false,
                ICON_FA_EXPLOSION))
            VehicleMovement::RequestCrash();
        MenuUi::EndInspectorIsland();
        MenuUi::EndPageColumns();
    }
}
