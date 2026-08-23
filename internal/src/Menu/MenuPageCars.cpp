#include "MenuPages.h"

#define IMGUI_DEFINE_MATH_OPERATORS

#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"
#include "Leyawin/leyawin_globals.h"
#include "Leyawin/leyawin_init.h"
#include "IconsFontAwesome6.h"
#include "MenuUiHelpers.h"
#include "MenuState.h"
#include "MenuTheme.h"
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

    // Vehicle detection used to be a bare pill that only ever said "Enter a
    // vehicle", which told you nothing when the detection itself was the thing
    // that was broken. This shows what was detected, the seat, and which source
    // answered, so a wrong answer is visible instead of silent.
    static void DrawVehicleDetectionCard() noexcept
    {
        const VehicleMovement::LocalVehicleState state =
            VehicleMovement::GetLocalVehicleState();
        const VehicleMovement::Status status =
            VehicleMovement::GetStatus();
        const MenuTheme::Palette& tokens = MenuTheme::Colors();
        const float scale = UIScale();

        const char* headline = nullptr;
        const char* detail = nullptr;
        const char* glyph = ICON_FA_PERSON_WALKING;
        ImVec4 tint = tokens.textMuted;

        if (!state.valid)
        {
            headline = T("Detecting…", "Определение…", "Визначення…");
            detail = T("Waiting for the script thread",
                       "Ожидание script-потока",
                       "Очікування script-потоку");
            glyph = ICON_FA_HOURGLASS_HALF;
            tint = tokens.unknown;
        }
        else if (state.isDriver)
        {
            headline = T("Driver seat", "За рулём", "За кермом");
            detail = T("Vehicle features are available",
                       "Функции транспорта доступны",
                       "Функції транспорту доступні");
            glyph = ICON_FA_CAR;
            tint = tokens.success;
        }
        else if (state.inVehicle)
        {
            headline = T("Passenger", "Пассажир", "Пасажир");
            detail = T("Only the driver can use these",
                       "Работает только у водителя",
                       "Працює лише у водія");
            glyph = ICON_FA_CAR_SIDE;
            tint = tokens.warning;
        }
        else if (state.isEntering)
        {
            headline = T("Getting in…", "Садится…", "Сідає…");
            detail = T("Enter animation in progress",
                       "Идёт анимация входа",
                       "Триває анімація входу");
            glyph = ICON_FA_DOOR_OPEN;
            tint = tokens.unknown;
        }
        else
        {
            headline = T("On foot", "Пешком", "Пішки");
            detail = T("No vehicle detected",
                       "Транспорт не обнаружен",
                       "Транспорт не виявлено");
            glyph = ICON_FA_PERSON_WALKING;
            tint = tokens.textMuted;
        }

        const float width = ImGui::GetContentRegionAvail().x;
        const float height = 62.f * scale;
        const ImVec2 cardMin = ImGui::GetCursorScreenPos();
        const ImVec2 cardMax(cardMin.x + width, cardMin.y + height);
        ImDrawList* draw = ImGui::GetWindowDrawList();

        draw->AddRectFilled(
            cardMin,
            cardMax,
            ImGui::GetColorU32(tokens.raised),
            10.f * scale);
        // A thin accent edge instead of a filled block: it reads as state
        // without turning the card into a banner.
        draw->AddRectFilled(
            cardMin,
            ImVec2(cardMin.x + 3.f * scale, cardMax.y),
            ImGui::GetColorU32(tint),
            10.f * scale);

        const float iconX = cardMin.x + 16.f * scale;
        const float centerY = cardMin.y + height * 0.5f;
        draw->AddCircleFilled(
            ImVec2(iconX + 9.f * scale, centerY),
            15.f * scale,
            ImGui::GetColorU32(ImVec4(tint.x, tint.y, tint.z, 0.16f)),
            20);
        const ImVec2 glyphSize = ImGui::CalcTextSize(glyph);
        draw->AddText(
            ImVec2(
                iconX + 9.f * scale - glyphSize.x * 0.5f,
                centerY - glyphSize.y * 0.5f),
            ImGui::GetColorU32(tint),
            glyph);

        const float textX = iconX + 34.f * scale;
        const float lineGap = 3.f * scale;
        const float headHeight = ImGui::GetTextLineHeight();
        const float textTop =
            centerY - (headHeight * 2.f + lineGap) * 0.5f;
        draw->AddText(
            ImVec2(textX, textTop),
            ImGui::GetColorU32(tokens.textPrimary),
            headline);
        draw->AddText(
            ImVec2(textX, textTop + headHeight + lineGap),
            ImGui::GetColorU32(tokens.textFaint),
            detail);

        // Right side: where the answer came from, and the handle when seated.
        char source[64]{};
        if (state.valid)
        {
            if (state.handle)
                sprintf_s(source, "script  #%d", state.handle);
            else
                sprintf_s(
                    source,
                    "%s",
                    T("script", "script", "script"));
        }
        else
        {
            sprintf_s(
                source,
                "%s",
                T("fallback", "fallback", "fallback"));
        }
        const ImVec2 sourceSize = ImGui::CalcTextSize(source);
        draw->AddText(
            ImVec2(
                cardMax.x - sourceSize.x - 14.f * scale,
                cardMin.y + 10.f * scale),
            ImGui::GetColorU32(tokens.textFaint),
            source);

        const char* statusText = VehicleMovementStatusText(status);
        const ImVec2 statusSize = ImGui::CalcTextSize(statusText);
        const bool active =
            status == VehicleMovement::Status::Active;
        draw->AddText(
            ImVec2(
                cardMax.x - statusSize.x - 14.f * scale,
                cardMax.y - statusSize.y - 10.f * scale),
            ImGui::GetColorU32(active ? tokens.success : tokens.textMuted),
            statusText);

        ImGui::Dummy(ImVec2(width, height));
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
            VehicleMovement::Settings::kMaxGravity,
            "%.1fx");
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
        DrawVehicleDetectionCard();
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
                ICON_FA_WRENCH,
                &MiscFeatures::RequestRepairVehicle))
            MiscFeatures::RequestRepairVehicle();
        ImGui::SameLine(0.f, gap);
        if (MenuUi::ActionButton(
                "cars_engine",
                T("Start engine", "Завести", "Завести"),
                ImVec2(half, 34.f * scale),
                false,
                ICON_FA_KEY,
                &VehicleMovement::RequestEngineOn))
            VehicleMovement::RequestEngineOn();
        if (MenuUi::ActionButton(
                "cars_stop",
                T("Stop", "Стоп", "Стоп"),
                ImVec2(half, 34.f * scale),
                false,
                ICON_FA_HAND,
                &VehicleMovement::RequestStop))
            VehicleMovement::RequestStop();
        ImGui::SameLine(0.f, gap);
        if (MenuUi::ActionButton(
                "cars_flip",
                T("Flip", "Перевернуть", "Перевернути"),
                ImVec2(half, 34.f * scale),
                false,
                ICON_FA_ARROWS_ROTATE,
                &VehicleMovement::RequestFlip))
            VehicleMovement::RequestFlip();
        if (MenuUi::ActionButton(
                "cars_crash",
                T("Crash / explode", "Авария / взрыв", "Аварія / вибух"),
                ImVec2(ImGui::GetContentRegionAvail().x, 34.f * scale),
                false,
                ICON_FA_EXPLOSION,
                &VehicleMovement::RequestCrash))
            VehicleMovement::RequestCrash();
        MenuUi::EndInspectorIsland();
        MenuUi::EndPageColumns();
    }
}
