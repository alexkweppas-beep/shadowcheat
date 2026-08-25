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
#include "FactionColors.h"
#include "../SDK/RageBuildFlavor.h"
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
#include "../Features/ServerProfile.h"
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
    // Vehicles and objects used to share the World tab; the scope row splits
    // them so every ESP target is one click away.
    static int s_VisualWorldScope = 0;

    // Style tables shared by the inspector rows and the preview panel.
    static const char* const* BoxStyleNames() noexcept
    {
        static const char* kEn[] = { "Full", "Corners", "Filled" };
        static const char* kRu[] = { "Полный", "Углы", "Заливка" };
        static const char* kUk[] = { "Повний", "Кути", "Заливка" };
        return s_Language == Language::Russian ? kRu
            : (s_Language == Language::Ukrainian ? kUk : kEn);
    }

    static const char* const* SnapOriginNames() noexcept
    {
        static const char* kEn[] = { "Bottom", "Top", "Crosshair" };
        static const char* kRu[] = { "Снизу", "Сверху", "Перекрестие" };
        static const char* kUk[] = { "Знизу", "Згори", "Перехрестя" };
        return s_Language == Language::Russian ? kRu
            : (s_Language == Language::Ukrainian ? kUk : kEn);
    }

    static const char* const* HiddenStyleNames() noexcept
    {
        static const char* kEn[] = {
            "Everything", "Box + skeleton only" };
        static const char* kRu[] = {
            "Всё", "Только бокс и скелет" };
        static const char* kUk[] = {
            "Все", "Тільки бокс і скелет" };
        return s_Language == Language::Russian ? kRu
            : (s_Language == Language::Ukrainian ? kUk : kEn);
    }

    static const char* const* BarStyleNames() noexcept
    {
        static const char* kEn[] = {
            "Solid", "Gradient", "Bar + value", "Text", "Dynamic" };
        static const char* kRu[] = {
            "Сплошной", "Градиент", "Полоса + число", "Текст", "Динамический" };
        static const char* kUk[] = {
            "Суцільний", "Градієнт", "Смужка + число", "Текст", "Динамічний" };
        return s_Language == Language::Russian ? kRu
            : (s_Language == Language::Ukrainian ? kUk : kEn);
    }

    static const char* const* BarPosNames() noexcept
    {
        static const char* kEn[] = { "Left", "Right", "Top", "Bottom" };
        static const char* kRu[] = { "Слева", "Справа", "Сверху", "Снизу" };
        static const char* kUk[] = { "Зліва", "Справа", "Зверху", "Знизу" };
        return s_Language == Language::Russian ? kRu
            : (s_Language == Language::Ukrainian ? kUk : kEn);
    }

    static const char* const* WeaponDisplayNames() noexcept
    {
        static const char* kEn[] = { "Text", "Icon", "Text + Icon" };
        static const char* kRu[] = { "Текст", "Иконка", "Текст + иконка" };
        static const char* kUk[] = { "Текст", "Іконка", "Текст + іконка" };
        return s_Language == Language::Russian ? kRu
            : (s_Language == Language::Ukrainian ? kUk : kEn);
    }

    static constexpr float kVisualScopeRowHeight = 40.f;

    // Every ESP scope lives in one strip: peds, world and radar no longer need a
    // navigation level of their own.
    static void RenderVisualsScopeRow()
    {
        struct ScopeEntry final
        {
            const char* id;
            const char* icon;
            const char* label;
            VisualCategory category;
            int value;
        };
        const ScopeEntry entries[] = {
            { "players", ICON_FA_USER,
              T("Players", "Игроки", "Гравці"),
              VisualCategory::Peds,
              static_cast<int>(EspProfileId::Players) },
            { "admins", ICON_FA_SHIELD_HALVED,
              T("Admins", "Админы", "Адміни"),
              VisualCategory::Peds,
              static_cast<int>(EspProfileId::Admin) },
            { "friends", ICON_FA_USER_GROUP,
              T("Friends", "Друзья", "Друзі"),
              VisualCategory::Peds,
              static_cast<int>(EspProfileId::Friends) },
            { "npc", ICON_FA_PERSON,
              T("NPC", "NPC", "NPC"),
              VisualCategory::Peds,
              static_cast<int>(EspProfileId::Npc) },
            { "faction", ICON_FA_USERS,
              T("Faction", "Фракция", "Фракція"),
              VisualCategory::Peds,
              static_cast<int>(EspProfileId::FactionPlayers) },
            { "family", ICON_FA_HOUSE,
              T("Family", "Семья", "Сім'я"),
              VisualCategory::Peds,
              static_cast<int>(EspProfileId::FamilyPlayers) },
            { "animals", ICON_FA_PAW,
              T("Animals", "Животные", "Тварини"),
              VisualCategory::Peds,
              static_cast<int>(EspProfileId::Animal) },
            { "vehicles", ICON_FA_CAR,
              T("Vehicles", "Транспорт", "Транспорт"),
              VisualCategory::World, 0 },
            { "objects", ICON_FA_CUBE,
              T("Objects", "Объекты", "Об'єкти"),
              VisualCategory::World, 1 },
            { "radar", ICON_FA_LOCATION_CROSSHAIRS,
              T("Radar", "Радар", "Радар"),
              VisualCategory::Radar, 0 }
        };
        constexpr int kScopeCount = IM_ARRAYSIZE(entries);

        MenuUi::IconTabDesc tabs[kScopeCount]{};
        int selected = 0;
        for (int i = 0; i < kScopeCount; ++i)
        {
            tabs[i].id = entries[i].id;
            tabs[i].icon = entries[i].icon;
            tabs[i].label = entries[i].label;
            if (s_VisualCategoryTab !=
                static_cast<int>(entries[i].category))
                continue;
            const bool valueMatches =
                entries[i].category == VisualCategory::Peds
                    ? entries[i].value == s_VisualPedProfile
                    : (entries[i].category == VisualCategory::World
                           ? entries[i].value == s_VisualWorldScope
                           : true);
            if (valueMatches)
                selected = i;
        }

        if (MenuUi::SubNavTabs(
                "visual_scope",
                tabs,
                kScopeCount,
                &selected,
                kVisualScopeRowHeight))
        {
            const ScopeEntry& picked =
                entries[ImClamp(selected, 0, kScopeCount - 1)];
            s_VisualCategoryTab = static_cast<int>(picked.category);
            if (picked.category == VisualCategory::Peds)
                s_VisualPedProfile = picked.value;
            else if (picked.category == VisualCategory::World)
                s_VisualWorldScope = picked.value;
        }
    }

    static MenuUi::FeatureRowResult VisualsRow(
        const char* id,
        const char* icon,
        const char* label,
        bool* enabled,
        float* color,
        bool overflow)
    {
        MenuUi::FeatureRowDesc desc{};
        desc.icon = icon;
        desc.label = label;
        desc.enabled = enabled;
        desc.color = color;
        desc.overflow = overflow;
        return MenuUi::FeatureRow(id, desc);
    }

    static bool ParseVisualModelHash(
        const char* text,
        std::uint32_t& out) noexcept
    {
        if (!text || !text[0])
            return false;
        char* end = nullptr;
        const unsigned long value = std::strtoul(text, &end, 0);
        if (!end || *end != '\0' || value == 0ul)
            return false;
        out = static_cast<std::uint32_t>(value);
        return true;
    }

    static void RenderModelHashCollection(
        const char* stableId,
        const char* hint,
        std::uint32_t* values,
        int& count,
        char* input,
        std::size_t inputCapacity) noexcept
    {
        if (!stableId || !values || !input || inputCapacity < 2)
            return;
        count = ImClamp(count, 0, kMaxVisualModelFilters);
        ImGui::PushID(stableId);
        const float scale = UIScale();
        const float addWidth = 82.f * scale;
        gui->text_field(
            "##model_hash",
            hint,
            ImVec2(
                ImMax(
                    100.f * scale,
                    ImGui::GetContentRegionAvail().x -
                        addWidth - 7.f * scale),
                32.f * scale),
            input,
            inputCapacity);
        ImGui::SameLine(0.f, 7.f * scale);
        if (MenuUi::ActionButton(
                "add_model_hash",
                T("Add", "Добавить", "Додати"),
                ImVec2(addWidth, 32.f * scale),
                false,
                ICON_FA_CIRCLE_PLUS))
        {
            std::uint32_t parsed = 0;
            bool duplicate = false;
            if (ParseVisualModelHash(input, parsed))
            {
                for (int i = 0; i < count; ++i)
                    duplicate = duplicate || values[i] == parsed;
                if (!duplicate && count < kMaxVisualModelFilters)
                {
                    values[count++] = parsed;
                    input[0] = '\0';
                }
            }
        }

        for (int i = 0; i < count; ++i)
        {
            ImGui::PushID(i);
            char hashLabel[24]{};
            _snprintf(
                hashLabel,
                sizeof(hashLabel) - 1,
                "0x%08X",
                static_cast<unsigned>(values[i]));
            const float removeWidth = 34.f * scale;
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(
                ImMax(80.f * scale, ImGui::GetContentRegionAvail().x -
                    removeWidth - 6.f * scale),
                30.f * scale));
            ImDrawList* draw = ImGui::GetWindowDrawList();
            const ImVec2 rowMax(
                rowMin.x + ImGui::GetItemRectSize().x,
                rowMin.y + 30.f * scale);
            draw->AddRectFilled(
                rowMin,
                rowMax,
                gui->get_clr(clr->element_background, 0.72f),
                7.f * scale);
            draw->AddText(
                rowMin + ImVec2(10.f, 7.f) * scale,
                gui->get_clr(clr->text_active),
                hashLabel);
            ImGui::SameLine(0.f, 6.f * scale);
            if (MenuUi::ActionButton(
                    "remove_model_hash",
                    "",
                    ImVec2(removeWidth, 30.f * scale),
                    false,
                    ICON_FA_XMARK))
            {
                for (int j = i; j + 1 < count; ++j)
                    values[j] = values[j + 1];
                values[--count] = 0;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    static void RenderRadarInspector() noexcept
    {
        RadarSettings& radar = s_RadarSettings;
        MenuUi::BeginInspectorIsland(
            "radar_general",
            T("RADAR", "РАДАР", "РАДАР"),
            2.f);
        VisualsRow(
            "radar_enabled",
            ICON_FA_LOCATION_CROSSHAIRS,
            T("Radar Overlay", "Радар", "Радар"),
            &radar.enabled,
            radar.enemyColor,
            false);
        MenuUi::EndInspectorIsland();

        MenuUi::BeginInspectorIsland(
            "radar_sources",
            T("SOURCES", "ИСТОЧНИКИ", "ДЖЕРЕЛА"));
        VisualsRow(
            "radar_players",
            ICON_FA_USER,
            T("Players", "Игроки", "Гравці"),
            &radar.showPlayers,
            radar.enemyColor,
            false);
        VisualsRow(
            "radar_friends",
            ICON_FA_USER_GROUP,
            T("Friends", "Друзья", "Друзі"),
            &radar.showFriends,
            radar.friendColor,
            false);
        VisualsRow(
            "radar_npcs",
            ICON_FA_PERSON,
            T("NPC", "NPC", "NPC"),
            &radar.showNpcs,
            radar.npcColor,
            false);
        VisualsRow(
            "radar_vehicles",
            ICON_FA_CAR,
            T("Vehicles", "Машины", "Авто"),
            &radar.showVehicles,
            radar.vehicleColor,
            false);
        MenuUi::EndInspectorIsland();

        MenuUi::NextPageColumn();
        MenuUi::BeginInspectorIsland(
            "radar_geometry",
            T("GEOMETRY", "ГЕОМЕТРИЯ", "ГЕОМЕТРІЯ"),
            2.f);
        if (radar.size < 80.f)
            radar.size = 160.f;
        gui->slider_float_icon(
            ICON_FA_RULER,
            T("Range###radar_range", "Дальность###radar_range", "Дальність###radar_range"),
            &radar.range,
            25.f,
            800.f,
            "%.0f m");
        gui->slider_float_icon(
            ICON_FA_EXPAND,
            T("Size###radar_size", "Размер###radar_size", "Розмір###radar_size"),
            &radar.size,
            80.f,
            360.f,
            "%.0f px");
        gui->slider_float_icon(
            ICON_FA_CIRCLE,
            T("Marker size###radar_marker", "Размер маркера###radar_marker", "Розмір маркера###radar_marker"),
            &radar.markerSize,
            2.f,
            9.f,
            "%.1f px");
        gui->slider_float_icon(
            ICON_FA_COMPASS,
            T("Direction length###radar_direction", "Длина направления###radar_direction", "Довжина напрямку###radar_direction"),
            &radar.directionLength,
            4.f,
            22.f,
            "%.0f px");
        gui->slider_float_icon(
            ICON_FA_EYE,
            T("Background opacity###radar_opacity", "Прозрачность фона###radar_opacity", "Прозорість фону###radar_opacity"),
            &radar.backgroundOpacity,
            0.15f,
            0.95f,
            "%.2f");
        if (MenuUi::ActionButton(
                "radar_reset_position",
                T("Reset position", "Сбросить позицию", "Скинути позицію"),
                ImVec2(ImGui::GetContentRegionAvail().x, 32.f * UIScale()),
                false,
                ICON_FA_ROTATE_LEFT))
        {
            radar.posX = -1.f;
            radar.posY = -1.f;
        }
        MenuUi::EndInspectorIsland();
    }

    static void RenderVehicleVisualsInspector() noexcept
    {
        VehicleVisualSettings& visual = s_VehicleVisualSettings;
        static char includeInput[24]{};
        static char excludeInput[24]{};

        MenuUi::BeginInspectorIsland(
            "vehicle_general",
            T("VEHICLES", "МАШИНЫ", "АВТО"),
            2.f);
        VisualsRow(
            "vehicle_enabled",
            ICON_FA_POWER_OFF,
            T("Enabled", "Включено", "Увімкнено"),
            &visual.enabled,
            nullptr,
            false);
        gui->slider_float_icon(
            ICON_FA_RULER,
            T("Max distance###vehicle_visual_range", "Макс. дистанция###vehicle_visual_range", "Макс. дистанція###vehicle_visual_range"),
            &visual.maxDistance,
            25.f,
            1200.f,
            "%.0f m");
        MenuUi::EndInspectorIsland();

        MenuUi::BeginInspectorIsland(
            "vehicle_draw",
            T("DRAW", "ОТРИСОВКА", "ВІДОБРАЖЕННЯ"));
        VisualsRow("vehicle_box", ICON_FA_VECTOR_SQUARE,
            T("Box", "Рамка", "Рамка"), &visual.box, visual.boxColor, false);
        VisualsRow("vehicle_name", ICON_FA_TAG,
            T("Model", "Модель", "Модель"), &visual.name, visual.textColor, false);
        VisualsRow("vehicle_distance", ICON_FA_RULER,
            T("Distance", "Дистанция", "Відстань"), &visual.distance, visual.textColor, false);
        VisualsRow("vehicle_id", ICON_FA_HASHTAG,
            T("Remote ID", "Remote ID", "Remote ID"), &visual.remoteId, visual.textColor, false);
        VisualsRow("vehicle_health", ICON_FA_HEART,
            T("Health", "Прочность", "Міцність"), &visual.health, visual.textColor, false);
        VisualsRow("vehicle_trunk", ICON_FA_BOX_OPEN,
            T("Open trunk", "Открытый багажник", "Відкритий багажник"),
            &visual.trunk, visual.textColor, false);
        VisualsRow("vehicle_snapline", ICON_FA_ARROWS_UP_DOWN,
            T("Snap line", "Снеп-линия", "Снеп-лінія"),
            &visual.snapLine, visual.snapLineColor, true);
        if (MenuUi::BeginOverflowPopup("vehicle_snapline"))
        {
            gui->dropdown(
                T("Origin", "Начало", "Початок"),
                &visual.snapLineOrigin,
                SnapOriginNames(),
                static_cast<int>(EspSnapOrigin::Count));
            if (visual.snapLineOrigin < 0 ||
                visual.snapLineOrigin >=
                    static_cast<int>(EspSnapOrigin::Count))
                visual.snapLineOrigin = 0;
            MenuUi::EndOverflowPopup();
        }
        MenuUi::EndInspectorIsland();

        MenuUi::NextPageColumn();

        // Same leading gap as the first island of the left column, otherwise
        // the two columns start on different lines.
        MenuUi::BeginInspectorIsland(
            "vehicle_filters",
            T("FILTERS", "ФИЛЬТРЫ", "ФІЛЬТРИ"),
            2.f);
        VisualsRow("vehicle_occupied", ICON_FA_USER_GROUP,
            T("Occupied", "С водителем", "З водієм"), &visual.showOccupied, nullptr, false);
        VisualsRow("vehicle_empty", ICON_FA_CAR,
            T("Empty", "Пустые", "Порожні"), &visual.showEmpty, nullptr, false);
        VisualsRow("vehicle_cars", ICON_FA_CAR_SIDE,
            T("Cars", "Автомобили", "Автомобілі"), &visual.showCars, nullptr, false);
        VisualsRow("vehicle_bikes", ICON_FA_MOTORCYCLE,
            T("Bikes", "Мотоциклы", "Мотоцикли"), &visual.showBikes, nullptr, false);
        VisualsRow("vehicle_boats", ICON_FA_SHIP,
            T("Boats", "Лодки", "Човни"), &visual.showBoats, nullptr, false);
        VisualsRow("vehicle_air", ICON_FA_PLANE,
            T("Aircraft", "Авиация", "Авіація"), &visual.showAircraft, nullptr, false);
        VisualsRow("vehicle_other", ICON_FA_CUBES,
            T("Other", "Прочее", "Інше"), &visual.showOther, nullptr, false);
        MenuUi::EndInspectorIsland();

        MenuUi::BeginInspectorIsland(
            "vehicle_models",
            T("MODEL HASHES", "ХЕШИ МОДЕЛЕЙ", "ХЕШІ МОДЕЛЕЙ"));
        RenderModelHashCollection(
            "vehicle_include",
            "Include: 0x12345678",
            visual.includeModels,
            visual.includeModelCount,
            includeInput,
            sizeof(includeInput));
        RenderModelHashCollection(
            "vehicle_exclude",
            "Exclude: 0x12345678",
            visual.excludeModels,
            visual.excludeModelCount,
            excludeInput,
            sizeof(excludeInput));
        MenuUi::EndInspectorIsland();
    }

    // One row of the object filter editor: the name is edited in place, the
    // hash is what actually does the filtering and is shown as it is.
    static bool RenderModelFilterRow(
        std::uint32_t* values,
        int& count,
        int index,
        float scale) noexcept
    {
        bool removed = false;
        ImGui::PushID(index);
        const std::uint32_t hash = values[index];

        char name[28]{};
        lstrcpynA(
            name,
            ModelLabelFor(hash),
            static_cast<int>(sizeof(name)));

        const float removeWidth = 32.f * scale;
        const float hashWidth = 96.f * scale;
        const float nameWidth = ImMax(
            90.f * scale,
            ImGui::GetContentRegionAvail().x -
                hashWidth - removeWidth - 14.f * scale);

        if (gui->text_field(
                "##model_name",
                T("Name", "Название", "Назва"),
                ImVec2(nameWidth, 30.f * scale),
                name,
                sizeof(name)))
            SetModelLabel(hash, name);

        ImGui::SameLine(0.f, 7.f * scale);
        char hashLabel[16]{};
        _snprintf(
            hashLabel,
            sizeof(hashLabel) - 1,
            "0x%08X",
            static_cast<unsigned>(hash));
        const ImVec2 hashMin = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(hashWidth, 30.f * scale));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(
            hashMin,
            hashMin + ImVec2(hashWidth, 30.f * scale),
            gui->get_clr(clr->element_background, 0.72f),
            7.f * scale);
        // Dimmer than the name: the name is what the row is now about, and the
        // hash is the identifier behind it.
        draw->AddText(
            hashMin + ImVec2(9.f, 7.f) * scale,
            gui->get_clr(clr->text),
            hashLabel);

        ImGui::SameLine(0.f, 7.f * scale);
        if (MenuUi::ActionButton(
                "remove_model",
                "",
                ImVec2(removeWidth, 30.f * scale),
                false,
                ICON_FA_TRASH))
        {
            for (int move = index; move + 1 < count; ++move)
                values[move] = values[move + 1];
            values[--count] = 0;
            removed = true;
        }
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0.f, 3.f * scale));
        return removed;
    }

    // The two lists used to sit on the page as bare hash fields, which meant a
    // filter was a column of hex numbers with nothing saying what any of them
    // was. They live in their own window now, with a name beside each hash.
    static char s_FilterHashInput[24]{};
    static char s_FilterNameInput[28]{};
    static int  s_FilterListChoice = 0;

    static const char* ObjectFilterPopupId() noexcept
    {
        return T(
            "Object filters###object_filter_editor",
            "Фильтры объектов###object_filter_editor",
            "Фільтри об'єктів###object_filter_editor");
    }

    static void RenderObjectFilterEditor(float scale) noexcept
    {
        ObjectVisualSettings& visual = s_ObjectVisualSettings;
        const char* popup = ObjectFilterPopupId();

        ImGui::SetNextWindowSize(
            ImVec2(520.f, 0.f) * scale,
            ImGuiCond_Appearing);
        // A popup rather than a modal: a modal swallows every click outside
        // itself by design, and clicking away is how this one is meant to be
        // dismissed. The title row is drawn below instead of using a title
        // bar, which a popup does not have.
        if (!ImGui::BeginPopup(
                popup,
                ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiWindowFlags_NoSavedSettings))
            return;

        {
            const float closeSize = 26.f * scale;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(
                T("Object filters", "Фильтры объектов",
                  "Фільтри об'єктів"));
            ImGui::SameLine(0.f, 0.f);
            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() +
                ImMax(0.f,
                      ImGui::GetContentRegionAvail().x - closeSize));
            // A bare glyph: a filled button behind a cross reads as another
            // control competing with the list, and the plate was the only thing
            // in the header with a surface of its own.
            if (MenuUi::GhostIconButton(
                    "object_filter_x",
                    ICON_FA_XMARK,
                    ImVec2(closeSize, closeSize)))
            {
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return;
            }
        }

        char* const hashInput = s_FilterHashInput;
        char* const nameInput = s_FilterNameInput;
        int& listChoice = s_FilterListChoice;

        visual.includeModelCount =
            ImClamp(visual.includeModelCount, 0, kMaxVisualModelFilters);
        visual.excludeModelCount =
            ImClamp(visual.excludeModelCount, 0, kMaxVisualModelFilters);

        // Which list is being edited carries the count, so the choice and the
        // state of both lists read as one thing instead of a label, a dropdown
        // and a paragraph explaining them.
        char shownLabel[40]{};
        char hiddenLabel[40]{};
        _snprintf(
            shownLabel,
            sizeof(shownLabel) - 1,
            "%s  %d",
            T("Shown", "Показывать", "Показувати"),
            visual.includeModelCount);
        _snprintf(
            hiddenLabel,
            sizeof(hiddenLabel) - 1,
            "%s  %d",
            T("Hidden", "Скрывать", "Ховати"),
            visual.excludeModelCount);
        const MenuUi::IconTabDesc lists[2] = {
            { "shown", ICON_FA_EYE, shownLabel },
            { "hidden", ICON_FA_EYE_SLASH, hiddenLabel },
        };
        MenuUi::ChipTabs("object_filter_lists", lists, 2, &listChoice);
        if (listChoice < 0 || listChoice > 1)
            listChoice = 0;

        std::uint32_t* values = listChoice == 0
            ? visual.includeModels
            : visual.excludeModels;
        int& count = listChoice == 0
            ? visual.includeModelCount
            : visual.excludeModelCount;

        MenuUi::BeginInspectorIsland(
            "object_filter_add_island",
            T("ADD MODEL", "ДОБАВИТЬ МОДЕЛЬ", "ДОДАТИ МОДЕЛЬ"),
            8.f,
            listChoice == 0
                ? T("Only these models are drawn.",
                    "Рисуются только эти модели.",
                    "Малюються лише ці моделі.")
                : T("Everything except these is drawn.",
                    "Рисуется всё, кроме этих.",
                    "Малюється усе, крім цих."));
        const float addWidth = 92.f * scale;
        const float half = ImMax(
            88.f * scale,
            (ImGui::GetContentRegionAvail().x -
                addWidth - 14.f * scale) * 0.5f);
        gui->text_field(
            "##add_hash",
            "0x12345678",
            ImVec2(half, 32.f * scale),
            hashInput,
            sizeof(s_FilterHashInput));
        ImGui::SameLine(0.f, 7.f * scale);
        gui->text_field(
            "##add_name",
            T("Name", "Название", "Назва"),
            ImVec2(half, 32.f * scale),
            nameInput,
            sizeof(s_FilterNameInput));
        ImGui::SameLine(0.f, 7.f * scale);
        const bool full = count >= kMaxVisualModelFilters;
        // Not ICON_FA_CIRCLE_PLUS: that one is defined as the ASCII "+", and the icon
        // font is rasterised only over the Font Awesome range starting at
        // 0xE005, so it comes out as an empty box. CIRCLE_PLUS is U+F055 and
        // is actually in the atlas.
        if (MenuUi::ActionButton(
                "object_filter_add",
                T("Add", "Добавить", "Додати"),
                ImVec2(addWidth, 32.f * scale),
                !full,
                ICON_FA_CIRCLE_PLUS) &&
            !full)
        {
            std::uint32_t parsed = 0;
            if (ParseVisualModelHash(hashInput, parsed))
            {
                bool duplicate = false;
                for (int index = 0; index < count; ++index)
                    duplicate = duplicate || values[index] == parsed;
                if (!duplicate)
                {
                    values[count++] = parsed;
                    SetModelLabel(parsed, nameInput);
                    hashInput[0] = '\0';
                    nameInput[0] = '\0';
                }
            }
        }
        if (full)
            ImGui::TextDisabled(
                "%s",
                T("List is full.", "Список заполнен.",
                  "Список заповнений."));
        MenuUi::EndInspectorIsland();

        MenuUi::BeginInspectorIsland(
            "object_filter_list_island",
            T("ENTRIES", "ЗАПИСИ", "ЗАПИСИ"),
            8.f);
        if (count <= 0)
        {
            // Centred and stated plainly: an empty filter list looks exactly
            // like a broken one otherwise.
            // A hyphen, not a dash: the atlas carries Latin and Cyrillic, and
            // an em dash came out as a question mark.
            const char* empty = T(
                "Empty - this list filters nothing",
                "Пусто - этот список ничего не фильтрует",
                "Порожньо - цей список нічого не фільтрує");
            const float width = ImGui::CalcTextSize(empty).x;
            ImGui::Dummy(ImVec2(0.f, 6.f * scale));
            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() +
                ImMax(0.f,
                      (ImGui::GetContentRegionAvail().x - width) * 0.5f));
            ImGui::TextDisabled("%s", empty);
            ImGui::Dummy(ImVec2(0.f, 6.f * scale));
        }
        for (int index = 0; index < count;)
        {
            if (!RenderModelFilterRow(values, count, index, scale))
                ++index;
        }
        MenuUi::EndInspectorIsland();

        // No Done button. With a cross in the corner and a click anywhere
        // outside both closing the window, a third way to do the same thing is
        // only one more thing to read.
        ImGui::EndPopup();
    }

    static void RenderObjectVisualsInspector() noexcept
    {
        ObjectVisualSettings& visual = s_ObjectVisualSettings;

        MenuUi::BeginInspectorIsland(
            "object_general",
            T("OBJECTS", "ОБЪЕКТЫ", "ОБ'ЄКТИ"),
            2.f);
        VisualsRow(
            "object_enabled",
            ICON_FA_POWER_OFF,
            T("Enabled", "Включено", "Увімкнено"),
            &visual.enabled,
            nullptr,
            false);
        gui->slider_float_icon(
            ICON_FA_RULER,
            T("Max distance###object_visual_range", "Макс. дистанция###object_visual_range", "Макс. дистанція###object_visual_range"),
            &visual.maxDistance,
            10.f,
            600.f,
            "%.0f m");
        MenuUi::EndInspectorIsland();

        MenuUi::BeginInspectorIsland(
            "object_draw",
            T("DRAW", "ОТРИСОВКА", "ВІДОБРАЖЕННЯ"));
        VisualsRow("object_box", ICON_FA_VECTOR_SQUARE,
            T("Box", "Рамка", "Рамка"), &visual.box, visual.objectColor, false);
        VisualsRow("object_name", ICON_FA_TAG,
            T("Model hash", "Хеш модели", "Хеш моделі"), &visual.name, visual.textColor, false);
        VisualsRow("object_distance", ICON_FA_RULER,
            T("Distance", "Дистанция", "Відстань"), &visual.distance, visual.textColor, false);
        VisualsRow("object_id", ICON_FA_HASHTAG,
            T("Remote ID", "Remote ID", "Remote ID"), &visual.remoteId, visual.textColor, false);
        VisualsRow("object_snapline", ICON_FA_ARROWS_UP_DOWN,
            T("Snap line", "Снеп-линия", "Снеп-лінія"),
            &visual.snapLine, visual.snapLineColor, true);
        if (MenuUi::BeginOverflowPopup("object_snapline"))
        {
            gui->dropdown(
                T("Origin", "Начало", "Початок"),
                &visual.snapLineOrigin,
                SnapOriginNames(),
                static_cast<int>(EspSnapOrigin::Count));
            if (visual.snapLineOrigin < 0 ||
                visual.snapLineOrigin >=
                    static_cast<int>(EspSnapOrigin::Count))
                visual.snapLineOrigin = 0;
            MenuUi::EndOverflowPopup();
        }
        MenuUi::EndInspectorIsland();

        MenuUi::NextPageColumn();

        MenuUi::BeginInspectorIsland(
            "object_filters",
            T("FILTERS", "ФИЛЬТРЫ", "ФІЛЬТРИ"),
            2.f);
        VisualsRow("objects_regular", ICON_FA_CUBE,
            T("Network objects", "Сетевые объекты", "Мережеві об'єкти"),
            &visual.showObjects, visual.objectColor, false);
        VisualsRow("objects_pickups", ICON_FA_BOX_OPEN,
            T("Pickups", "Подбираемые", "Підбирані"),
            &visual.showPickups, visual.pickupColor, false);
        VisualsRow("objects_show_all", ICON_FA_EYE,
            T("Show all", "Показывать всё", "Показувати все"),
            &visual.showAll, nullptr, false);
        MenuUi::EndInspectorIsland();

        MenuUi::BeginInspectorIsland(
            "object_models",
            T("MODEL FILTERS", "ФИЛЬТРЫ МОДЕЛЕЙ", "ФІЛЬТРИ МОДЕЛЕЙ"));
        const float scale = UIScale();
        char summary[96]{};
        _snprintf(
            summary,
            sizeof(summary) - 1,
            "%s  %d / %d",
            T("Edit list", "Изменить список", "Змінити список"),
            visual.includeModelCount,
            visual.excludeModelCount);
        if (MenuUi::ActionButton(
                "object_filter_open",
                summary,
                ImVec2(
                    ImGui::GetContentRegionAvail().x,
                    34.f * scale),
                false,
                ICON_FA_FILTER))
            s_ObjectFilterOpenRequest = true;
        // The window itself is drawn by the shell, once, so that a
        // double-click opens it from any page.
        MenuUi::EndInspectorIsland();
    }

    static void RenderMeshSilhouetteInspector() noexcept
    {
        auto& mesh = MeshSilhouette::GetSettings();
        MenuUi::BeginInspectorIsland(
            "mesh_silhouette",
            T("MESH CHAMS / GLOW",
              "MESH CHAMS / GLOW",
              "MESH CHAMS / GLOW"));
        VisualsRow(
            "mesh_silhouette_enabled",
            ICON_FA_PERSON_RAYS,
            T("Experimental mesh pass",
              "Экспериментальный mesh pass",
              "Експериментальний mesh pass"),
            &mesh.enabled,
            &mesh.visibleColor.r,
            false);

        if (BeginAimReveal(
                "mesh_silhouette_details",
                mesh.enabled,
                250.f))
        {
            VisualsRow(
                "mesh_fill",
                ICON_FA_FILL_DRIP,
                T("Filled chams", "Заливка chams", "Заливка chams"),
                &mesh.fill,
                &mesh.visibleColor.r,
                false);
            VisualsRow(
                "mesh_edge",
                ICON_FA_WAND_MAGIC_SPARKLES,
                T("Glow edge", "Glow-контур", "Glow-контур"),
                &mesh.edge,
                &mesh.occludedColor.r,
                false);
            gui->color_edit(
                T("Visible color###mesh_visible_color",
                  "Видимый цвет###mesh_visible_color",
                  "Видимий колір###mesh_visible_color"),
                &mesh.visibleColor.r);
            gui->color_edit(
                T("Occluded color###mesh_occluded_color",
                  "Цвет за стеной###mesh_occluded_color",
                  "Колір за стіною###mesh_occluded_color"),
                &mesh.occludedColor.r);
            gui->slider_float_icon(
                ICON_FA_CIRCLE_HALF_STROKE,
                T("Opacity###mesh_opacity",
                  "Прозрачность###mesh_opacity",
                  "Прозорість###mesh_opacity"),
                &mesh.opacity,
                0.05f,
                1.f,
                "%.2f");
            gui->slider_float_icon(
                ICON_FA_EXPAND,
                T("Edge width###mesh_edge_width",
                  "Ширина контура###mesh_edge_width",
                  "Ширина контуру###mesh_edge_width"),
                &mesh.edgeWidth,
                0.5f,
                8.f,
                "%.1f px");
            gui->slider_float_icon(
                ICON_FA_SUN,
                T("Softness###mesh_softness",
                  "Мягкость###mesh_softness",
                  "М'якість###mesh_softness"),
                &mesh.softness,
                0.f,
                4.f,
                "%.1f");
            EndAimReveal(mesh.enabled);
        }

        const MeshSilhouette::Status status =
            MeshSilhouette::GetStatus();
        const bool active =
            status == MeshSilhouette::Status::Ready ||
            status ==
                MeshSilhouette::Status::CompositeSubmitted;
        MenuUi::StatusChip(
            "mesh_status",
            MeshSilhouette::StatusText(status),
            active);
        if (status ==
            MeshSilhouette::Status::ClassifierUnavailable)
        {
            ImGui::TextDisabled(
                "%s",
                T("Fail-closed: this GTA build has no verified ped draw classifier.",
                  "Fail-closed: для этого билда GTA нет проверенного классификатора ped draw.",
                  "Fail-closed: для цього білда GTA немає перевіреного класифікатора ped draw."));
        }
        MenuUi::EndInspectorIsland();
    }

    static void RenderVisualsInspector()
    {
        if (s_VisualCategoryTab < 0 ||
            s_VisualCategoryTab >= static_cast<int>(VisualCategory::Count))
            s_VisualCategoryTab = 0;
        if (s_VisualPedProfile < 0 ||
            s_VisualPedProfile >= static_cast<int>(EspProfileId::Count))
            s_VisualPedProfile = 0;

        if (s_VisualCategoryTab ==
            static_cast<int>(VisualCategory::Radar))
        {
            RenderRadarInspector();
            return;
        }
        if (s_VisualCategoryTab ==
            static_cast<int>(VisualCategory::World))
        {
            if (s_VisualWorldScope == 1)
                RenderObjectVisualsInspector();
            else
                RenderVehicleVisualsInspector();
            return;
        }

        EspProfileSettings& profile =
            s_EspProfiles[s_VisualPedProfile];

        // Animals are not players: they have no faction, no family, no armor,
        // no weapon and no server ids. Those rows are hidden here instead of
        // sitting in the list doing nothing, and the flags are forced off so a
        // profile carried over from another scope cannot leave them enabled.
        const bool isAnimal =
            s_VisualPedProfile ==
            static_cast<int>(EspProfileId::Animal);
        if (isAnimal)
        {
            profile.factionTag = false;
            profile.familyTag = false;
            profile.armorBar = false;
            profile.weaponTag = false;
            profile.dynamicIdTag = false;
            profile.staticIdTag = false;
        }

        const char* const* boxStyles = BoxStyleNames();
        const char* const* barStyles = BarStyleNames();
        const char* const* barPos = BarPosNames();
        const char* const* weaponDisp = WeaponDisplayNames();

        MenuUi::BeginInspectorIsland(
            "island_general",
            T("GENERAL", "ОБЩЕЕ", "ЗАГАЛЬНЕ"));
        VisualsRow(
            "enabled",
            ICON_FA_POWER_OFF,
            T("Enabled", "Включено", "Увімкнено"),
            &profile.enabled,
            nullptr,
            false);
        VisualsRow(
            "visible",
            ICON_FA_EYE,
            T("Visible Check", "Проверка видимости", "Перевірка видимості"),
            &profile.visibleCheck,
            profile.hiddenColor,
            true);
        if (MenuUi::BeginOverflowPopup("visible"))
        {
            gui->dropdown(
                T("Recolour", "Перекрашивать", "Перефарбовувати"),
                &profile.hiddenStyle,
                HiddenStyleNames(),
                EspHiddenStyleCount);
            if (profile.hiddenStyle < 0 ||
                profile.hiddenStyle >= EspHiddenStyleCount)
                profile.hiddenStyle = EspHiddenTintAll;
            MenuUi::EndOverflowPopup();
        }
        VisualsRow(
            "dead",
            ICON_FA_SKULL,
            T("Show Dead", "Показывать мёртвых", "Показувати мертвих"),
            &profile.showDead,
            profile.deadColor,
            false);
        MenuUi::EndInspectorIsland();

        MenuUi::BeginInspectorIsland(
            "island_draw",
            T("DRAW", "ОТРИСОВКА", "ВІДОБРАЖЕННЯ"));
        VisualsRow(
            "box",
            ICON_FA_VECTOR_SQUARE,
            T("Box", "Рамка", "Рамка"),
                    &profile.box,
                    profile.boxColor,
            true);
        if (MenuUi::BeginOverflowPopup("box"))
                {
                    gui->dropdown(
                        T("Box Style", "Стиль рамки", "Стиль рамки"),
                        &profile.boxStyle,
                        boxStyles,
                        EspBoxStyleCount);
            if (profile.boxStyle < 0 ||
                profile.boxStyle >= EspBoxStyleCount)
                        profile.boxStyle = EspBoxFull;
                    if (profile.boxStyle == EspBoxFilled)
                    {
                        gui->color_edit(
                            T("Box Fill", "Заливка рамки", "Заливка рамки"),
                            profile.boxFillColor);
                    }
            gui->slider_float(
                T("Rounding", "Скругление", "Заокруглення"),
                &profile.boxRounding,
                0.f,
                16.f);
            gui->slider_float(
                T("Thickness", "Толщина", "Товщина"),
                &profile.boxThickness,
                0.5f,
                8.f);
            MenuUi::EndOverflowPopup();
        }
        VisualsRow(
            "skeleton",
            ICON_FA_BONE,
            T("Skeleton", "Скелет", "Скелет"),
            &profile.skeleton,
            profile.skeletonColor,
            true);
        if (MenuUi::BeginOverflowPopup("skeleton"))
        {
            gui->slider_float(
                T("Thickness", "Толщина", "Товщина"),
                &profile.skeletonThickness,
                0.5f,
                8.f);
            MenuUi::EndOverflowPopup();
        }
        VisualsRow(
            "snapline",
            ICON_FA_ARROWS_UP_DOWN,
            T("Snap line", "Снеп-линия", "Снеп-лінія"),
            &profile.snapLine,
            profile.snapLineColor,
            true);
        if (MenuUi::BeginOverflowPopup("snapline"))
        {
            gui->dropdown(
                T("Origin", "Начало", "Початок"),
                &profile.snapLineOrigin,
                SnapOriginNames(),
                static_cast<int>(EspSnapOrigin::Count));
            if (profile.snapLineOrigin < 0 ||
                profile.snapLineOrigin >=
                    static_cast<int>(EspSnapOrigin::Count))
                profile.snapLineOrigin = 0;
            MenuUi::EndOverflowPopup();
        }
        MenuUi::EndInspectorIsland();

        MenuUi::BeginInspectorIsland(
            "island_bars",
            T("BARS", "ПОЛОСЫ", "СМУЖКИ"));
        VisualsRow(
            "health",
            ICON_FA_HEART,
            T("Health", "Здоровье", "Здоров'я"),
                    &profile.healthBar,
                    profile.healthColor,
            true);
        if (MenuUi::BeginOverflowPopup("health"))
                {
                    gui->dropdown(
                        T("Health Style", "Стиль HP", "Стиль HP"),
                        &profile.healthStyle,
                        barStyles,
                        EspBarStyleCount);
            if (profile.healthStyle < 0 ||
                profile.healthStyle >= EspBarStyleCount)
                        profile.healthStyle = EspBarSolid;
            gui->checkbox(
                T("Show value", "Показывать значение", "Показувати значення"),
                &profile.healthShowValue);
            MenuUi::EndOverflowPopup();
        }
        if (!isAnimal)
        {
        VisualsRow(
            "armor",
            ICON_FA_SHIELD,
            T("Armor", "Броня", "Броня"),
                    &profile.armorBar,
                    profile.armorColor,
            true);
        if (MenuUi::BeginOverflowPopup("armor"))
        {
                    gui->dropdown(
                        T("Armor Style", "Стиль брони", "Стиль броні"),
                        &profile.armorStyle,
                        barStyles,
                        EspBarStyleCount);
            if (profile.armorStyle < 0 ||
                profile.armorStyle >= EspBarStyleCount)
                        profile.armorStyle = EspBarSolid;
                    gui->dropdown(
                        T("Armor Position", "Позиция брони", "Позиція броні"),
                        &profile.armorBarPos,
                        barPos,
                        4);
                    if (profile.armorBarPos < 0 || profile.armorBarPos > 3)
                        profile.armorBarPos = EspHealthBarLeft;
            gui->checkbox(
                T("Show value", "Показывать значение", "Показувати значення"),
                &profile.armorShowValue);
            MenuUi::EndOverflowPopup();
        }
        }
        MenuUi::EndInspectorIsland();

        MenuUi::BeginInspectorIsland(
            "island_tags",
            T("TAGS", "ПОДПИСИ", "ПІДПИСИ"));
        VisualsRow(
            "name",
            ICON_FA_TAG,
            T("Name", "Имя", "Ім'я"),
            &profile.nameTag,
            profile.nameColor,
            false);
        if (!isAnimal)
        {
            VisualsRow(
                "faction_tag",
                ICON_FA_FLAG,
                T("Faction", "Фракция", "Фракція"),
                &profile.factionTag,
                profile.factionColor,
                false);
            VisualsRow(
                "family_tag",
                ICON_FA_HOUSE,
                T("Family", "Семья", "Сім'я"),
                &profile.familyTag,
                profile.familyColor,
                false);
        }
        VisualsRow(
            "distance",
            ICON_FA_RULER,
                    T("Distance", "Дистанция", "Відстань"),
                    &profile.distance,
                    profile.distanceColor,
            false);
        if (!isAnimal)
        {
            VisualsRow(
                "weapon",
                ICON_FA_GUN,
                T("Weapon", "Оружие", "Зброя"),
                &profile.weaponTag,
                profile.weaponColor,
                true);
            if (MenuUi::BeginOverflowPopup("weapon"))
            {
                gui->dropdown(
                    T("Weapon Display",
                      "Отображение оружия",
                      "Відображення зброї"),
                    &profile.weaponDisplay,
                    weaponDisp,
                    EspWeaponDisplayCount);
                if (profile.weaponDisplay < 0 ||
                    profile.weaponDisplay >= EspWeaponDisplayCount)
                    profile.weaponDisplay = EspWeaponText;
                MenuUi::EndOverflowPopup();
            }
            VisualsRow(
                "dyn_id",
                ICON_FA_HASHTAG,
                T("Dynamic ID", "Динамический ID", "Динамічний ID"),
                &profile.dynamicIdTag,
                profile.dynamicIdColor,
                false);
            VisualsRow(
                "stat_id",
                ICON_FA_ID_CARD,
                T("Static ID", "Статический ID", "Статичний ID"),
                &profile.staticIdTag,
                profile.staticIdColor,
                false);
        }
        ImGui::Dummy(
            ImVec2(0.f, MenuTheme::Sizes().space1 * UIScale()));
        MenuUi::EndInspectorIsland();

        if (s_VisualPedProfile == static_cast<int>(EspProfileId::Admin))
        {
            MenuUi::BeginInspectorIsland(
                "island_admin",
                T("ADMIN", "АДМИН", "АДМІН"));
            gui->text_field(
                "##admin_variable_key",
                T("Variable key", "Ключ переменной", "Ключ змінної"),
                ImVec2(
                    ImGui::GetContentRegionAvail().x,
                    MenuTheme::Sizes().controlHeight * UIScale()),
                s_AdminVariableKey,
                sizeof(s_AdminVariableKey));
            ImGui::Dummy(
                ImVec2(0.f, MenuTheme::Sizes().space2 * UIScale()));
            gui->slider_int(
                T("Admin threshold",
                  "Порог администратора",
                  "Поріг адміністратора"),
                &s_AdminLevelThreshold,
                0,
                20);
            MenuUi::EndInspectorIsland();
        }

        // Per-faction text colours belong to the faction tag, which any scope
        // can draw, but the settings for it live here where the faction scope
        // is: putting them on every scope would repeat one shared table.
        if (s_VisualPedProfile ==
            static_cast<int>(EspProfileId::FactionPlayers))
        {
            const float scale = UIScale();
            MenuUi::BeginInspectorIsland(
                "island_faction_colors",
                T("FACTION COLOURS", "ЦВЕТА ФРАКЦИЙ", "КОЛЬОРИ ФРАКЦІЙ"));
            MenuUi::CheckWithHelp(
                T("Colour tag per faction##faction_colors_on",
                  "Свой цвет для каждой фракции##faction_colors_on",
                  "Свій колір для кожної фракції##faction_colors_on"),
                &FactionColors::Enabled(),
                "Off: every faction tag uses the scope's faction colour. "
                "On: each faction gets its own, and a faction this build "
                "does not know still gets a distinct colour from its key.",
                "Выключено: все теги фракций красятся общим цветом области. "
                "Включено: у каждой фракции свой цвет, а незнакомая "
                "фракция получает отдельный цвет по своему ключу.",
                "Вимкнено: усі теги фракцій фарбуються спільним кольором. "
                "Увімкнено: кожна фракція має свій колір, а незнайома "
                "фракція отримує окремий колір за своїм ключем.");

            if (FactionColors::Enabled())
            {
                ImGui::Dummy(ImVec2(0.f, 4.f * scale));
                const int count = FactionColors::Count();
                for (int index = 0; index < count; ++index)
                {
                    FactionColors::Entry* entry =
                        FactionColors::At(index);
                    if (!entry)
                        continue;
                    ImGui::PushID(index);
                    char label[80]{};
                    _snprintf(
                        label,
                        sizeof(label) - 1,
                        "%s##faction_color_%d",
                        entry->label[0] ? entry->label : entry->key,
                        index);
                    if (gui->color_edit(label, entry->color))
                        entry->custom = true;
                    ImGui::PopID();
                }
                ImGui::Dummy(ImVec2(0.f, 4.f * scale));
                if (MenuUi::ActionButton(
                        "faction_colors_reset",
                        T("Reset colours",
                          "Сбросить цвета",
                          "Скинути кольори"),
                        ImVec2(
                            ImGui::GetContentRegionAvail().x,
                            30.f * scale),
                        false,
                        ICON_FA_ROTATE_LEFT))
                    FactionColors::ResetToDefaults();
            }
            MenuUi::EndInspectorIsland();
        }

        ImGui::Dummy(
            ImVec2(0.f, MenuTheme::Sizes().space2 * UIScale()));
    }

    static void RenderEspPreviewDocked(EspProfileSettings& profile);

    static bool s_EspElementsOpen = false;

    // Holds what the row overflow menus ("...") offer: styles and per element
    // options. Toggles and colours stay on the rows themselves.
    static void RenderEspElementsPanel(
        EspProfileSettings& profile,
        const ImVec2& bottomLeft,
        float width,
        float maxHeight,
        float scale) noexcept
    {
        static float s_Phase = 0.f;
        s_Phase = MenuMotion::Advance(s_Phase, s_EspElementsOpen, 0.16f);
        const float reveal = MenuMotion::EaseOut(s_Phase);
        if (reveal <= 0.001f)
            return;

        const MenuTheme::Palette& tokens = MenuTheme::Colors();
        const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
        // Sized to the seven controls it holds, clamped to the stage.
        const float height =
            ImMin(maxHeight, 300.f * scale) * reveal;
        const ImVec2 panelMin(bottomLeft.x, bottomLeft.y - height);
        const ImVec2 panelMax(bottomLeft.x + width, bottomLeft.y);

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(
            panelMin,
            panelMax,
            gui->get_clr(tokens.panel, 0.97f),
            metrics.radiusPanel * scale);
        draw->AddRect(
            panelMin,
            panelMax,
            gui->get_clr(tokens.borderSubtle, 1.f),
            metrics.radiusPanel * scale,
            0,
            metrics.stroke);

        const float pad = 10.f * scale;
        ImGui::SetCursorScreenPos(panelMin + ImVec2(pad, pad));
        ImGui::PushStyleVar(
            ImGuiStyleVar_ItemSpacing,
            ImVec2(0.f, 2.f * scale));
        if (ImGui::BeginChild(
                "##esp_elements",
                ImVec2(width - pad * 2.f, ImMax(1.f, height - pad * 2.f)),
                ImGuiChildFlags_None,
                ImGuiWindowFlags_NoScrollbar))
        {
            gui->dropdown(
                T("Box Style", "Стиль рамки", "Стиль рамки"),
                &profile.boxStyle,
                BoxStyleNames(),
                EspBoxStyleCount);
            if (profile.boxStyle < 0 || profile.boxStyle >= EspBoxStyleCount)
                profile.boxStyle = EspBoxFull;
            if (profile.boxStyle == EspBoxFilled)
            {
                gui->color_edit(
                    T("Box Fill", "Заливка рамки", "Заливка рамки"),
                    profile.boxFillColor);
            }

            gui->dropdown(
                T("Health Style", "Стиль HP", "Стиль HP"),
                &profile.healthStyle,
                BarStyleNames(),
                EspBarStyleCount);
            if (profile.healthStyle < 0 ||
                profile.healthStyle >= EspBarStyleCount)
                profile.healthStyle = EspBarSolid;
            gui->checkbox(
                T("Health value", "Значение HP", "Значення HP"),
                &profile.healthShowValue);

            gui->dropdown(
                T("Armor Style", "Стиль брони", "Стиль броні"),
                &profile.armorStyle,
                BarStyleNames(),
                EspBarStyleCount);
            if (profile.armorStyle < 0 ||
                profile.armorStyle >= EspBarStyleCount)
                profile.armorStyle = EspBarSolid;
            gui->dropdown(
                T("Armor Position", "Позиция брони", "Позиція броні"),
                &profile.armorBarPos,
                BarPosNames(),
                4);
            if (profile.armorBarPos < 0 || profile.armorBarPos > 3)
                profile.armorBarPos = EspHealthBarLeft;
            gui->checkbox(
                T("Armor value", "Значение брони", "Значення броні"),
                &profile.armorShowValue);

            gui->dropdown(
                T("Weapon Display", "Отображение оружия", "Відображення зброї"),
                &profile.weaponDisplay,
                WeaponDisplayNames(),
                EspWeaponDisplayCount);
            if (profile.weaponDisplay < 0 ||
                profile.weaponDisplay >= EspWeaponDisplayCount)
                profile.weaponDisplay = EspWeaponText;
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    void RenderObjectFilterPopup() noexcept
    {
        // A pick arrives from the world, where there is no page context, so it
        // is turned into an open request here and the hash is put straight
        // into the field the user would otherwise have typed it into.
        if (s_PickedObjectModel != 0)
        {
            _snprintf(
                s_FilterHashInput,
                sizeof(s_FilterHashInput) - 1,
                "0x%08X",
                static_cast<unsigned>(s_PickedObjectModel));
            lstrcpynA(
                s_FilterNameInput,
                ModelLabelFor(s_PickedObjectModel),
                static_cast<int>(sizeof(s_FilterNameInput)));
            s_FilterListChoice = 0;
            s_PickedObjectModel = 0;
            s_ObjectFilterOpenRequest = true;
        }
        if (s_ObjectFilterOpenRequest)
        {
            s_ObjectFilterOpenRequest = false;
            ImGui::OpenPopup(ObjectFilterPopupId());
        }
        RenderObjectFilterEditor(UIScale());
    }

    void RenderVisualsPage()
    {
        const float scale = UIScale();

        // Positioned absolutely so the cards sit as close to the strip as they
        // do under the Aim sub-tab row.
        const ImVec2 scopeOrigin = ImGui::GetCursorScreenPos();
        RenderVisualsScopeRow();
        ImGui::SetCursorScreenPos(
            ImVec2(
                scopeOrigin.x,
                scopeOrigin.y + kVisualScopeRowHeight * scale));

        const bool pedCategory =
            s_VisualCategoryTab ==
            static_cast<int>(VisualCategory::Peds);
        if (!pedCategory)
            PedPreview::SetActive(false);

        // The faction and family scopes stay reachable everywhere, because the
        // drawing settings behind them are ours. What they colour by, though,
        // comes from the server, and on one that publishes neither the tags
        // simply never arrive: said here rather than left to look broken.
        if (pedCategory)
        {
            const bool factionScope =
                s_VisualPedProfile ==
                static_cast<int>(EspProfileId::FactionPlayers);
            const bool familyScope =
                s_VisualPedProfile ==
                static_cast<int>(EspProfileId::FamilyPlayers);
            const bool haveFactions =
                ServerProfile::IdentitySource() !=
                ServerProfile::Identity::None;
            if ((factionScope && !haveFactions) ||
                (familyScope && !ServerProfile::HasFamilies()))
            {
                MenuUi::StatusChip(
                    "visual_scope_unsupported",
                    T("Not published by this server",
                      "Этот сервер их не отдаёт",
                      "Цей сервер їх не віддає"),
                    false);
                ImGui::TextDisabled(
                    "%s",
                    familyScope
                        ? T("Families exist on GTA5RP only",
                            "Семьи есть только на GTA5RP",
                            "Сім'ї є лише на GTA5RP")
                        : T("Factions are read on GTA5RP, Quant and Ukraine",
                            "Фракции читаются на GTA5RP, Quant и Ukraine",
                            "Фракції читаються на GTA5RP, Quant і Ukraine"));
            }
        }

        const int profileIndex = ImClamp(
            s_VisualPedProfile,
            0,
            static_cast<int>(EspProfileId::Count) - 1);
        const float available =
            ImMax(1.f, ImGui::GetContentRegionAvail().x);
        const float paneH =
            ImMax(1.f, ImGui::GetContentRegionAvail().y);
        const float gap = MenuTheme::Sizes().space3 * scale;
        const bool sideBySide = available >= 560.f * scale;

        if (!pedCategory)
        {
            // Same layout contract as every other settings page: left aligned,
            // flowed into columns, no centred column with a dead half-page.
            MenuUi::BeginPageColumns("visual_world_columns");
            RenderVisualsInspector();
            MenuUi::EndPageColumns();
            return;
        }

        if (sideBySide)
        {
            const float previewW = ImClamp(
                available * 0.38f,
                260.f * scale,
                380.f * scale);
            const float inspectorW =
                ImMax(1.f, available - previewW - gap);
            ImGui::PushStyleVar(
                ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
            ImGui::BeginChild(
                "##visual_settings",
                ImVec2(inspectorW, paneH),
                ImGuiChildFlags_None);
            RenderVisualsInspector();
            ImGui::EndChild();
            ImGui::SameLine(0.f, gap);
            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowPadding,
                ImVec2(MenuTheme::Sizes().space3, MenuTheme::Sizes().space2) *
                    scale);
            ImGui::BeginChild(
                "##visual_preview_docked",
                ImVec2(previewW, paneH),
                ImGuiChildFlags_None |
                    ImGuiChildFlags_AlwaysUseWindowPadding,
                ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse);
            RenderEspPreviewDocked(s_EspProfiles[profileIndex]);
            ImGui::EndChild();
            ImGui::PopStyleVar(2);
            return;
        }

        const float previewH = ImMin(
            paneH * 0.52f,
            ImMax(200.f * scale, paneH - 160.f * scale));
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(MenuTheme::Sizes().space3, MenuTheme::Sizes().space2) *
                scale);
        ImGui::BeginChild(
            "##visual_preview_docked",
            ImVec2(0.f, previewH),
            ImGuiChildFlags_None |
                ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse);
        RenderEspPreviewDocked(s_EspProfiles[profileIndex]);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::BeginChild(
            "##visual_settings",
            ImVec2(0.f, 0.f),
            ImGuiChildFlags_None);
        RenderVisualsInspector();
        ImGui::EndChild();
    }

    static void RenderEspPreviewDocked(
        EspProfileSettings& profile)
    {
        PedPreview::SetActive(true);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const float scale = ImClamp(
            static_cast<float>(s_UIScalePercent) / 100.f,
            1.f,
            2.f);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x < 120.f || avail.y < 160.f)
        {
            PedPreview::SetActive(false);
            return;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 0.f));
        const float titleH = 18.f * scale;
        const float btnH = 36.f * scale;
        const float gap = 10.f * scale;
        const float stageW = avail.x;
        const float sceneH = ImMax(
            80.f * scale,
            avail.y - titleH - btnH - gap);

        const ImVec2 titleMin = ImGui::GetCursorScreenPos();
        gui->render_text_clipped(
            set->poppins_widget,
            titleMin,
            titleMin + ImVec2(stageW, titleH),
            gui->get_clr(clr->text, 0.48f),
            T("ESP PREVIEW", "ПРЕДПРОСМОТР ESP", "ПОПЕРЕДНІЙ ПЕРЕГЛЯД ESP"),
            {0.f, 0.5f});
        ImGui::Dummy(ImVec2(stageW, titleH));

        const ImVec2 sceneMin = ImGui::GetCursorScreenPos();
        const ImVec2 sceneMax = sceneMin + ImVec2(stageW, sceneH);
        ImGui::Dummy(ImVec2(stageW, sceneH));
        const ImVec2 afterScene = ImGui::GetCursorScreenPos();

        // The stage steps away from the page: darker under the dark theme,
        // brighter under the light one. Always lerping to black left a muddy
        // grey box in the middle of a white sheet.
        const ImVec4 stageFill = MenuTheme::IsLight()
            ? ImLerp(clr->window_background, clr->white_color, 0.85f)
            : ImLerp(clr->window_background, clr->black_color, 0.42f);
        draw->AddRectFilled(
            sceneMin,
            sceneMax,
            gui->get_clr(stageFill, 1.f),
            10.f * scale);
        draw->AddRect(
            sceneMin,
            sceneMax,
            gui->get_clr(clr->child_stroke_bar, 0.38f),
            10.f * scale,
            0,
            1.f);

        PedPreview::RenderIntoScene(
            draw, sceneMin, sceneMax, profile, true);

        if (!profile.enabled)
        {
            draw->AddRectFilled(
                sceneMin,
                sceneMax,
                MenuTheme::IsLight()
                    ? IM_COL32(255, 255, 255, 175)
                    : IM_COL32(0, 0, 0, 150),
                10.f * scale);
            gui->render_text_clipped(
                set->poppins_widget,
                sceneMin,
                sceneMax,
                gui->get_clr(clr->text),
                T("ESP disabled", "ESP выключен", "ESP вимкнено"),
                {0.5f, 0.5f});
        }

        ImGui::SetCursorScreenPos(afterScene);
        ImGui::Dummy(ImVec2(stageW, gap));
        if (MenuUi::ActionButton(
                "esp_open_settings",
                s_EspElementsOpen
                    ? T("Hide settings", "Скрыть настройки", "Сховати налаштування")
                    : T("Open settings", "Открыть настройки", "Відкрити налаштування"),
                ImVec2(stageW, btnH),
                s_EspElementsOpen,
                ICON_FA_GEAR))
        {
            s_EspElementsOpen = !s_EspElementsOpen;
        }
        const ImVec2 buttonMin = ImGui::GetItemRectMin();
        ImGui::PopStyleVar();

        // Grows upward over the stage instead of a floating popup, so the
        // settings appear where the button is.
        RenderEspElementsPanel(
            profile,
            ImVec2(buttonMin.x, buttonMin.y - gap),
            stageW,
            ImMax(120.f * scale, sceneH - 24.f * scale),
            scale);
    }
}
