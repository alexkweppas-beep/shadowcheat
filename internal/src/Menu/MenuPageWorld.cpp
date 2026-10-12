#include "MenuPages.h"

#define IMGUI_DEFINE_MATH_OPERATORS

#include "../imgui/imgui.h"
#include "Leyawin/leyawin_globals.h"
#include "Leyawin/leyawin_init.h"
#include "Leyawin/custom_widgets.hpp"
#include "IconsFontAwesome6.h"
#include "MenuUiHelpers.h"
#include "MenuState.h"
#include "../Features/MiscFeatures.h"

namespace Menu
{
    void RenderWorldPage()
    {
        auto& s = MiscFeatures::GetSettings();

        MenuUi::BeginPageColumns("world_columns");
        MenuUi::BeginInspectorIsland(
            "world_weather",
            T("WEATHER", "ПОГОДА", "ПОГОДА"),
            0.f);
        MiscRow(
            "world_weather_on",
            ICON_FA_CLOUD_SUN,
            T("Override weather", "Своя погода", "Своя погода"),
            &s.weatherOverride);
        static const char* kWeather[] = {
            "Extrasunny", "Clear", "Clouds", "Smog", "Foggy",
            "Overcast", "Rain", "Thunder", "Clearing", "Neutral",
            "Snow", "Blizzard", "Snowlight", "Xmas", "Halloween"
        };
        if (s.weatherIndex < 0 ||
            s.weatherIndex >= IM_ARRAYSIZE(kWeather))
            s.weatherIndex = 0;
        gui->dropdown(
            T("Type###world_weather_type",
              "Тип###world_weather_type",
              "Тип###world_weather_type"),
            &s.weatherIndex,
            kWeather,
            IM_ARRAYSIZE(kWeather),
            8);

        MiscRow(
            "world_nofog",
            ICON_FA_SUN,
            T("No fog", "Без тумана", "Без туману"),
            &s.noFog);
        MenuUi::EndInspectorIsland();

        MenuUi::BeginInspectorIsland(
            "world_camera_settings",
            T("WORLD", "МИР", "СВІТ"),
            0.f);
        MiscRow(
            "world_fov_on",
            ICON_FA_CAMERA,
            T("Override FOV", "Изменить FOV", "Змінити FOV"),
            &s.cameraFovEnabled);
        DrawAimAnimatedSlider(
            "world_fov_value",
            s.cameraFovEnabled,
            ICON_FA_CAMERA,
            T("FOV###world_fov_value",
              "FOV###world_fov_value",
              "FOV###world_fov_value"),
            &s.cameraFov,
            10.f,
            120.f,
            "%.0f");

        MiscRow(
            "world_aspect_on",
            ICON_FA_DISPLAY,
            T("Aspect Ratio", "Растяг экрана", "Розтягнення екрану"),
            &s.aspectEnabled);
        DrawAimAnimatedSlider(
            "world_aspect_value",
            s.aspectEnabled,
            ICON_FA_DISPLAY,
            T("Ratio###world_aspect_value",
              "Пропорции###world_aspect_value",
              "Пропорції###world_aspect_value"),
            &s.aspectRatio,
            0.5f,
            2.0f,
            "%.2f");
        const float scale = ImClamp(
            static_cast<float>(s_UIScalePercent) / 100.f,
            1.f,
            2.f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 6.f * scale);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        ImGui::TextWrapped(
            "%s",
            T("(applies on alt-tab)", 
              "(значение применяется при alt tab)", 
              "(значення застосовується при alt tab)"));
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.f, 2.f * scale));
        MenuUi::EndInspectorIsland();

        MenuUi::NextPageColumn();
        MenuUi::BeginInspectorIsland(
            "world_time",
            T("TIME", "ВРЕМЯ", "ЧАС"),
            0.f);
        MiscRow(
            "world_time_on",
            ICON_FA_CLOCK,
            T("Override clock", "Своё время", "Свій час"),
            &s.timeOverride);
        float hour = static_cast<float>(s.timeHour);
        float minute = static_cast<float>(s.timeMinute);
        DrawAimAnimatedSlider(
            "world_time_hour",
            s.timeOverride,
            ICON_FA_CLOCK,
            T("Hour###world_time_hour",
              "Час###world_time_hour",
              "Година###world_time_hour"),
            &hour,
            0.f,
            23.f,
            "%.0f");
        DrawAimAnimatedSlider(
            "world_time_minute",
            s.timeOverride,
            ICON_FA_CLOCK,
            T("Minute###world_time_minute",
              "Минута###world_time_minute",
              "Хвилина###world_time_minute"),
            &minute,
            0.f,
            59.f,
            "%.0f");
        s.timeHour = static_cast<int>(hour);
        s.timeMinute = static_cast<int>(minute);
        MiscRow(
            "world_time_freeze",
            ICON_FA_PAUSE,
            T("Freeze clock", "Заморозить часы", "Заморозити годинник"),
            &s.freezeTime);
        MenuUi::EndInspectorIsland();
        MenuUi::EndPageColumns();
    }
}
