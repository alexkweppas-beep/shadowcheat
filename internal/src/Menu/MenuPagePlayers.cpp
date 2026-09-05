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
#include "FactionColors.h"
#include "../Features/Friends.h"
#include "../Features/MiscFeatures.h"
#include "../Features/QuantTools.h"
#include "../Features/UkraineTools.h"
#include "../Features/FiveRpFarmBot.h"
#include "../Features/RagePlayerNames.h"
#include "../Features/RageWorldEntities.h"
#include "../Features/VehicleMovement.h"
#include "../Features/SilentAim.h"
#include "../Features/Aimbot.h"
#include "../Features/FiveRpMetadataResolver.h"
#include "../Features/FiveRpAdminScan.h"
#if __has_include("../Features/ServerProfile.h")
#include "../Features/ServerProfile.h"
#endif
#if __has_include("../Features/AutoLoot.h")
#include "../Features/AutoLoot.h"
#define HAS_AUTOLOOT 1
#else
#define HAS_AUTOLOOT 0
#endif
#if __has_include("../Features/NativeInvoke.hpp")
#include "../Features/NativeInvoke.hpp"
#define HAS_NATIVE_INVOKE 1
#else
#define HAS_NATIVE_INVOKE 0
#endif
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
    // The server is identified on its own and nothing about it is asked of the
    // user, so there is no card for it here. The pin is still readable from a
    // config for a session that has to be forced onto another server's paths,
    // it just has no controls of its own.
    void RenderAroundPage()
    {
        auto& s = MiscFeatures::GetSettings();

        MenuUi::BeginPageColumns("around_columns");
        MenuUi::BeginInspectorIsland(
            "around_overlay",
            T("AROUND YOU", "ВОКРУГ ТЕБЯ", "НАВКОЛО ТЕБЕ"),
            0.f,
            T("On-screen counters",
              "Счётчики на экране",
              "Лічильники на екрані"));
        MiscRow(
            "players_show_nearby",
            ICON_FA_USERS,
            T("Players around", "Игроки рядом", "Гравці поруч"),
            &s.showPlayers);
        MiscRow(
            "players_show_admins",
            ICON_FA_USER_SHIELD,
            T("Admins around", "Админы рядом", "Адміни поруч"),
            &s.showAdmins);
#ifdef RAGEMP_MENU_PREVIEW
        static bool s_PreviewShowAdminList = false;
        bool* pShowAdminList = &s_PreviewShowAdminList;
#else
        bool* pShowAdminList = &s.showAdminList;
#endif
        MiscRow(
            "players_show_admin_list",
            ICON_FA_LIST,
            T("Admin list", "Список админов", "Список адмінів"),
            pShowAdminList);
        gui->slider_float_icon(
            ICON_FA_RULER,
            T("Overlay range###players_overlay_distance",
              "Радиус оверлея###players_overlay_distance",
              "Радіус оверлея###players_overlay_distance"),
            &s.playersDistance,
            25.f,
            1000.f,
            "%.0f m");
        MenuUi::EndInspectorIsland();

        MenuUi::NextPageColumn();
        MenuUi::BeginInspectorIsland(
            "around_admin_list",
            T("ADMIN LIST", "СПИСОК АДМИНОВ", "СПИСОК АДМІНІВ"),
            0.f,
            T("Admins currently on the server",
              "Админы на сервере сейчас",
              "Адміни на сервері зараз"));
        FiveRpAdminScan::Entry admins[FiveRpAdminScan::kCapacity]{};
        const int adminCount = FiveRpAdminScan::CopyList(
            admins,
            IM_ARRAYSIZE(admins));
        if (adminCount <= 0)
        {
            MenuUi::EmptyState(
                "around_admin_empty",
                ICON_FA_USER_SHIELD,
                T("No admins online",
                  "Админов нет",
                  "Адмінів немає"),
                T("The list fills in once the server reports them",
                  "Список заполнится, когда сервер их отдаст",
                  "Список заповниться, коли сервер їх віддасть"),
                166.f);
        }
        else
        {
            for (int i = 0; i < adminCount; ++i)
            {
                const auto& admin = admins[i];
                char rowId[32]{};
                _snprintf(rowId, sizeof(rowId) - 1, "admin_%d", i);
                MenuUi::ListRowDesc row{};
                row.title = admin.name[0] ? admin.name : "?";
                row.meta = admin.streamed
                    ? T("nearby", "рядом", "поруч")
                    : T("on server", "на сервере", "на сервері");
                row.accentDot = admin.streamed;
                row.height = 40.f;
                MenuUi::DrawListRow(rowId, row);
            }
        }
        MenuUi::EndInspectorIsland();
        MenuUi::EndPageColumns();
    }

    // Rows enter and leave over a few frames instead of appearing and vanishing
    // between them, so the list reads as something being changed rather than
    // redrawn. Keyed by name: indices shift when an entry is removed.
    struct FriendRowMotion final
    {
        char name[48]{};
        float appear{};
        float leaving{};
        bool alive{};
    };

    FriendRowMotion s_FriendMotion[Friends::kMaxFriends]{};

    FriendRowMotion* FindFriendMotion(const char* name) noexcept
    {
        if (!name || !name[0])
            return nullptr;
        FriendRowMotion* free = nullptr;
        for (auto& slot : s_FriendMotion)
        {
            if (slot.alive && lstrcmpiA(slot.name, name) == 0)
                return &slot;
            if (!slot.alive && !free)
                free = &slot;
        }
        if (!free)
            return nullptr;
        *free = FriendRowMotion{};
        lstrcpynA(free->name, name, sizeof(free->name));
        free->alive = true;
        return free;
    }

    static bool StrContainsCaseInsensitive(const char* haystack, const char* needle) noexcept
    {
        if (!needle || !*needle)
            return true;
        if (!haystack || !*haystack)
            return false;
        for (; *haystack; ++haystack)
        {
            const char* h = haystack;
            const char* n = needle;
            while (*h && *n && (tolower(static_cast<unsigned char>(*h)) == tolower(static_cast<unsigned char>(*n))))
            {
                ++h;
                ++n;
            }
            if (!*n)
                return true;
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // Sub-tab 0: Friends (Друзья)
    // -------------------------------------------------------------------------
    static void RenderFriendsSubPage(
        const MenuUi::MasterDetailMetrics& layout,
        float scale) noexcept
    {
        static char s_FriendInput[48]{};
        static float s_AddRadius = 50.f;
        EnsureAimProfilesInitialized();

        AimWeaponProfile& general = GetAimProfile(0);
        s_Aim_IgnoreFriends = general.ignoreFriends;

        MenuUi::BeginGlassPanel(
            "##friends_settings",
            ImVec2(layout.masterWidth, layout.masterHeight),
            T("Friend settings", "Настройки друзей", "Налаштування друзів"),
            T("Radius tools", "Добавление по радиусу", "Додавання за радіусом"),
            ImGuiWindowFlags_None,
            false);
        {
            if (MenuUi::ActionButton(
                    "friends_add_radius",
                    T("Add friends in radius",
                      "Добавить друзей в радиусе",
                      "Додати друзів у радіусі"),
                    ImVec2(
                        ImGui::GetContentRegionAvail().x,
                        34.f * scale),
                    true,
                    ICON_FA_USERS))
            {
                RagePlayerNames::SnapshotEntry entries[64]{};
                const int count = RagePlayerNames::CopySnapshot(
                    entries,
                    IM_ARRAYSIZE(entries));
                bool localFound = false;
                float localX = 0.f;
                float localY = 0.f;
                float localZ = 0.f;
                for (int i = 0; i < count; ++i)
                {
                    if (!entries[i].local)
                        continue;
                    localFound = true;
                    localX = entries[i].x;
                    localY = entries[i].y;
                    localZ = entries[i].z;
                    break;
                }
                int added = 0;
                if (localFound)
                {
                    const float radiusSq =
                        s_AddRadius * s_AddRadius;
                    for (int i = 0; i < count; ++i)
                    {
                        const auto& entry = entries[i];
                        if (entry.local ||
                            entry.kind !=
                                RagePlayerNames::EntityKind::Player)
                            continue;
                        const float dx = entry.x - localX;
                        const float dy = entry.y - localY;
                        const float dz = entry.z - localZ;
                        if (dx * dx + dy * dy + dz * dz >
                            radiusSq)
                            continue;
                        const char* name = entry.nickname[0]
                            ? entry.nickname
                            : entry.name;
                        if (name && name[0] && Friends::Add(name))
                            ++added;
                    }
                }
                char message[96]{};
                _snprintf(
                    message,
                    sizeof(message) - 1,
                    localFound
                        ? T("Added %d friend(s).",
                            "Добавлено друзей: %d.",
                            "Додано друзів: %d.")
                        : T("Local player position is unavailable.",
                            "Позиция локального игрока недоступна.",
                            "Позиція локального гравця недоступна."),
                    added);
                PushNotification(
                    T("Friends", "Друзья", "Друзі"),
                    message,
                    localFound,
                    3000);
            }
            gui->slider_float_icon(
                ICON_FA_RULER,
                T("Add radius###friends_radius",
                  "Радиус добавления###friends_radius",
                  "Радіус додавання###friends_radius"),
                &s_AddRadius,
                1.f,
                250.f,
                "%.0f m");
        }
        MenuUi::EndGlassPanel();

        if (layout.sideBySide)
            ImGui::SameLine(0.f, layout.gap);

        char friendListSubtitle[128]{};
        _snprintf(
            friendListSubtitle,
            sizeof(friendListSubtitle) - 1,
            "%d / %d · %s",
            Friends::Count(),
            Friends::kMaxFriends,
            T("saved friends",
              "добавленные друзья",
              "додані друзі"));
        MenuUi::BeginGlassPanel(
            "##friends_list",
            ImVec2(layout.detailWidth, layout.detailHeight),
            T("Friend list", "Список друзей", "Список друзів"),
            friendListSubtitle,
            ImGuiWindowFlags_None,
            false);
        {
            if (Friends::Count() > 0)
            {
                const float deleteW = 108.f * scale;
                float startY = ImGui::GetCursorPosY();
                ImGui::SetCursorPosY(startY - 42.f * scale);
                ImGui::SetCursorPosX(
                    ImGui::GetCursorPosX() +
                    ImGui::GetContentRegionAvail().x -
                    deleteW);
                if (MenuUi::ActionButton(
                        "friends_delete_all",
                        T("Delete all",
                          "Удалить всех",
                          "Видалити всіх"),
                        ImVec2(deleteW, 28.f * scale),
                        false,
                        ICON_FA_TRASH_CAN))
                {
                    Friends::Clear();
                    for (auto& slot : s_FriendMotion)
                        slot = FriendRowMotion{};
                }
                ImGui::SetCursorPosY(startY);
            }

            const float addBarH = 46.f * scale;
            ImGui::BeginChild(
                "##friends_saved_scroll",
                ImVec2(
                    0.f,
                    ImMax(
                        100.f * scale,
                        ImGui::GetContentRegionAvail().y -
                            addBarH)),
                ImGuiChildFlags_None,
                ImGuiWindowFlags_NoSavedSettings);
            if (Friends::Count() == 0)
            {
                MenuUi::EmptyState(
                    "friends_saved_empty",
                    ICON_FA_ADDRESS_BOOK,
                    T("No friends yet",
                      "Друзей пока нет",
                      "Друзів поки немає"),
                    T("Add by radius or enter a nickname below",
                      "Добавь по радиусу или введи ник ниже",
                      "Додай за радіусом або введи нік нижче"),
                    140.f);
            }
            else
            {
                char pendingRemoval[48]{};
                for (int i = 0; i < Friends::Count(); ++i)
                {
                    const char* name = Friends::Get(i);
                    FriendRowMotion* motion = FindFriendMotion(name);
                    const float appear = motion
                        ? (motion->appear = MenuMotion::Approach(
                               motion->appear,
                               1.f,
                               MenuMotion::kReveal))
                        : 1.f;
                    const float leaving = motion ? motion->leaving : 0.f;

                    const float presence =
                        ImClamp(appear * (1.f - leaving), 0.f, 1.f);
                    const float rowH =
                        ImMax(1.f, 42.f * scale * presence);

                    ImGui::PushID(i);
                    ImGui::PushStyleVar(
                        ImGuiStyleVar_Alpha,
                        ImGui::GetStyle().Alpha * presence);
                    const float removeW = 38.f * scale;
                    const float rowW = ImMax(
                        100.f * scale,
                        ImGui::GetContentRegionAvail().x -
                            removeW - 6.f * scale);
                    MenuUi::ListRowDesc row{};
                    row.title = name;
                    row.height = 42.f;
                    ImGui::BeginChild(
                        "##friend_row",
                        ImVec2(rowW, rowH),
                        ImGuiChildFlags_None,
                        ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoScrollWithMouse);
                    MenuUi::DrawListRow("saved_friend", row);
                    ImGui::EndChild();
                    ImGui::SameLine(0.f, 6.f * scale);
                    if (MenuUi::GhostIconButton(
                            "remove_friend",
                            ICON_FA_XMARK,
                            ImVec2(removeW, rowH),
                            true) &&
                        motion &&
                        motion->leaving <= 0.f)
                    {
                        motion->leaving = 0.001f;
                    }
                    ImGui::PopStyleVar();
                    ImGui::PopID();

                    if (motion && motion->leaving > 0.f)
                    {
                        motion->leaving = MenuMotion::Approach(
                            motion->leaving,
                            1.f,
                            MenuMotion::kReveal);
                        if (motion->leaving > 0.97f)
                        {
                            lstrcpynA(
                                pendingRemoval,
                                motion->name,
                                sizeof(pendingRemoval));
                            *motion = FriendRowMotion{};
                        }
                    }
                }
                if (pendingRemoval[0])
                    Friends::Remove(pendingRemoval);
            }
            ImGui::EndChild();

            MenuUi::BeginToolbar(
                "##friends_manual_add",
                ImVec2(0.f, addBarH));
            const float addW = 118.f * scale;
            gui->text_field(
                "##friends_manual_input",
                T("Nickname or Dynamic ID",
                  "Ник или Dynamic ID",
                  "Нік або Dynamic ID"),
                ImVec2(
                    ImMax(
                        100.f * scale,
                        ImGui::GetContentRegionAvail().x -
                            addW - 7.f * scale),
                    32.f * scale),
                s_FriendInput,
                sizeof(s_FriendInput));
            ImGui::SameLine(0.f, 7.f * scale);
            if (MenuUi::ActionButton(
                    "friends_manual_add_button",
                    T("Add friend",
                      "Добавить",
                      "Додати"),
                    ImVec2(addW, 32.f * scale),
                    true,
                    ICON_FA_USER_PLUS))
            {
                const char* resolved = s_FriendInput;
                char resolvedName[48]{};
                char* end = nullptr;
                const long id = strtol(
                    s_FriendInput,
                    &end,
                    10);
                if (s_FriendInput[0] &&
                    end &&
                    *end == '\0' &&
                    id >= 0)
                {
                    RagePlayerNames::SnapshotEntry entries[64]{};
                    const int count = RagePlayerNames::CopySnapshot(
                        entries,
                        IM_ARRAYSIZE(entries));
                    for (int i = 0; i < count; ++i)
                    {
                        if (entries[i].remoteId != id &&
                            entries[i].sqlId != id)
                            continue;
                        lstrcpynA(
                            resolvedName,
                            entries[i].nickname[0]
                                ? entries[i].nickname
                                : entries[i].name,
                            sizeof(resolvedName));
                        resolved = resolvedName;
                        break;
                    }
                }
                if (Friends::Add(resolved))
                    s_FriendInput[0] = 0;
            }
            MenuUi::EndToolbar();
        }
        MenuUi::EndGlassPanel();
    }

    // -------------------------------------------------------------------------
    // Sub-tab 1: Factions (Фракции)
    // -------------------------------------------------------------------------
    static char s_FactionSearch[64]{};

    static void RenderFactionsSubPage(
        const MenuUi::MasterDetailMetrics& layout,
        float scale) noexcept
    {
        MenuUi::BeginGlassPanel(
            "##faction_settings",
            ImVec2(layout.masterWidth, layout.masterHeight),
            T("Faction settings", "Настройки фракций", "Налаштування фракцій"),
            T("Tags & Colors", "Теги и цвета", "Теги та кольори"),
            ImGuiWindowFlags_None,
            false);
        {
            bool& enabled = FactionColors::Enabled();
            gui->checkbox(
                T("Enable faction colors", "Цвета фракций", "Кольори фракцій"),
                &enabled);

            ImGui::Dummy(ImVec2(0.f, 6.f * scale));

            if (MenuUi::ActionButton(
                    "faction_reset_btn",
                    T("Reset to defaults", "Сбросить цвета", "Скинути кольори"),
                    ImVec2(ImGui::GetContentRegionAvail().x, 34.f * scale),
                    false,
                    ICON_FA_ROTATE_LEFT))
            {
                FactionColors::ResetToDefaults();
                PushNotification(
                    T("Filters", "Фильтры", "Фільтри"),
                    T("Faction colors reset to defaults.", "Цвета фракций сброшены к исходным.", "Кольори фракцій скинуто до початкових."),
                    true,
                    2500);
            }

            ImGui::Dummy(ImVec2(0.f, 8.f * scale));

            gui->text_field(
                "##faction_search_input",
                T("Search faction...", "Поиск фракции...", "Пошук фракції..."),
                ImVec2(ImGui::GetContentRegionAvail().x, 32.f * scale),
                s_FactionSearch,
                sizeof(s_FactionSearch));

            ImGui::Dummy(ImVec2(0.f, 8.f * scale));

            char statsBuf[64]{};
            _snprintf(statsBuf, sizeof(statsBuf) - 1, "%d %s", FactionColors::Count(), T("presets", "пресетов", "пресетів"));
            MenuUi::StatusChip("factions_loaded_chip", statsBuf, enabled);
        }
        MenuUi::EndGlassPanel();

        if (layout.sideBySide)
            ImGui::SameLine(0.f, layout.gap);

        char factionListSubtitle[96]{};
        _snprintf(
            factionListSubtitle,
            sizeof(factionListSubtitle) - 1,
            "%d %s",
            FactionColors::Count(),
            T("factions registered", "фракций в списке", "фракцій у списку"));

        MenuUi::BeginGlassPanel(
            "##factions_list",
            ImVec2(layout.detailWidth, layout.detailHeight),
            T("Factions", "Список фракций", "Список фракцій"),
            factionListSubtitle,
            ImGuiWindowFlags_None,
            false);
        {
            ImGui::BeginChild(
                "##factions_scroll",
                ImVec2(0.f, 0.f),
                ImGuiChildFlags_None,
                ImGuiWindowFlags_NoSavedSettings);

            const int count = FactionColors::Count();
            int drawnCount = 0;
            for (int i = 0; i < count; ++i)
            {
                FactionColors::Entry* entry = FactionColors::At(i);
                if (!entry || !entry->used)
                    continue;

                const char* labelName = entry->label[0] ? entry->label : entry->key;
                if (s_FactionSearch[0] != '\0')
                {
                    if (!StrContainsCaseInsensitive(labelName, s_FactionSearch) &&
                        !StrContainsCaseInsensitive(entry->key, s_FactionSearch))
                        continue;
                }

                ImGui::PushID(i);
                const float rowH = 40.f * scale;
                const float colorPickerW = 120.f * scale;
                const float labelW = ImMax(100.f * scale, ImGui::GetContentRegionAvail().x - colorPickerW - 8.f * scale);

                MenuUi::ListRowDesc row{};
                row.title = labelName;
                row.meta = nullptr;
                row.accentDot = entry->custom;
                row.height = rowH;

                ImGui::BeginChild(
                    "##faction_row_title",
                    ImVec2(labelW, rowH),
                    ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                MenuUi::DrawListRow("faction_row", row);
                ImGui::EndChild();

                ImGui::SameLine(0.f, 8.f * scale);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.f * scale);

                char colorId[48]{};
                _snprintf(colorId, sizeof(colorId) - 1, "##f_clr_%d", i);
                if (gui->color_edit(colorId, entry->color))
                {
                    entry->custom = true;
                }

                ImGui::PopID();
                ImGui::Dummy(ImVec2(0.f, 3.f * scale));
                ++drawnCount;
            }

            if (drawnCount == 0)
            {
                MenuUi::EmptyState(
                    "factions_empty",
                    ICON_FA_SHIELD_HALVED,
                    T("No factions found", "Фракции не найдены", "Фракції не знайдені"),
                    T("Try changing your search filter", "Попробуйте изменить поисковый запрос", "Спробуйте змінити пошуковий запит"),
                    140.f);
            }

            ImGui::EndChild();
        }
        MenuUi::EndGlassPanel();
    }

    // -------------------------------------------------------------------------
    // Sub-tab 2: Objects (Объекты)
    // -------------------------------------------------------------------------
    static char s_ObjHashInput[32]{};
    static char s_ObjNameInput[32]{};
    static int s_ObjListChoice = 0; // 0 = Shown (Whitelist), 1 = Hidden (Blacklist)

    static bool ParseObjectHash(const char* text, std::uint32_t& out) noexcept
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

    static void RenderObjectsSubPage(
        const MenuUi::MasterDetailMetrics& layout,
        float scale) noexcept
    {
        SeedDefaultObjectFilters();
        ObjectVisualSettings& visual = s_ObjectVisualSettings;
        visual.includeModelCount = ImClamp(visual.includeModelCount, 0, kMaxVisualModelFilters);
        visual.excludeModelCount = ImClamp(visual.excludeModelCount, 0, kMaxVisualModelFilters);

        MenuUi::BeginGlassPanel(
            "##object_settings",
            ImVec2(layout.masterWidth, layout.masterHeight),
            T("Object settings", "Настройки объектов", "Налаштування об'єктів"),
            T("Rendering & Filter mode", "Отрисовка и режим", "Відображення та режим"),
            ImGuiWindowFlags_None,
            false);
        {
            gui->checkbox(
                T("Objects ESP", "ESP объектов", "ESP об'єктів"),
                &visual.enabled);

            ImGui::Dummy(ImVec2(0.f, 4.f * scale));

            gui->checkbox(
                T("Network objects", "Сетевые объекты", "Мережеві об'єкти"),
                &visual.showObjects);
            gui->checkbox(
                T("Pickups", "Подбираемые", "Підбирані"),
                &visual.showPickups);
            gui->checkbox(
                T("Show all (Bypass filter)", "Показывать всё (в обход фильтра)", "Показувати все (в обхід фільтра)"),
                &visual.showAll);

            ImGui::Dummy(ImVec2(0.f, 4.f * scale));
            gui->slider_float_icon(
                ICON_FA_RULER,
                T("Max distance###obj_max_distance", "Дистанция###obj_max_distance", "Дистанція###obj_max_distance"),
                &visual.maxDistance,
                10.f,
                600.f,
                "%.0f m");

            ImGui::Dummy(ImVec2(0.f, 8.f * scale));

            char shownLabel[48]{};
            char hiddenLabel[48]{};
            _snprintf(shownLabel, sizeof(shownLabel) - 1, "%s  %d", T("Shown", "Показывать", "Показувати"), visual.includeModelCount);
            _snprintf(hiddenLabel, sizeof(hiddenLabel) - 1, "%s  %d", T("Hidden", "Скрывать", "Ховати"), visual.excludeModelCount);
            const MenuUi::IconTabDesc listTabs[2] = {
                { "shown", ICON_FA_EYE, shownLabel },
                { "hidden", ICON_FA_EYE_SLASH, hiddenLabel }
            };
            MenuUi::ChipTabs("obj_sub_filter_mode", listTabs, 2, &s_ObjListChoice);
            if (s_ObjListChoice < 0 || s_ObjListChoice > 1)
                s_ObjListChoice = 0;
        }
        MenuUi::EndGlassPanel();

        if (layout.sideBySide)
            ImGui::SameLine(0.f, layout.gap);

        std::uint32_t* models = s_ObjListChoice == 0 ? visual.includeModels : visual.excludeModels;
        int& modelCount = s_ObjListChoice == 0 ? visual.includeModelCount : visual.excludeModelCount;

        char objectListSubtitle[96]{};
        _snprintf(
            objectListSubtitle,
            sizeof(objectListSubtitle) - 1,
            "%d / %d %s",
            modelCount,
            kMaxVisualModelFilters,
            s_ObjListChoice == 0
                ? T("whitelisted models", "в белом списке", "у білому списку")
                : T("blacklisted models", "в чёрном списке", "у чорному списку"));

        MenuUi::BeginGlassPanel(
            "##objects_list_panel",
            ImVec2(layout.detailWidth, layout.detailHeight),
            s_ObjListChoice == 0
                ? T("Whitelist (Shown)", "Белый список (Показ)", "Білий список (Показ)")
                : T("Blacklist (Hidden)", "Чёрный список (Скрытие)", "Чорний список (Сховання)"),
            objectListSubtitle,
            ImGuiWindowFlags_None,
            false);
        {
            const float addBarH = 46.f * scale;

            ImGui::BeginChild(
                "##objects_scroll",
                ImVec2(0.f, ImMax(100.f * scale, ImGui::GetContentRegionAvail().y - addBarH)),
                ImGuiChildFlags_None,
                ImGuiWindowFlags_NoSavedSettings);

            if (modelCount == 0)
            {
                MenuUi::EmptyState(
                    "objects_empty",
                    ICON_FA_CUBE,
                    T("No models in list", "В списке нет моделей", "У списку немає моделей"),
                    s_ObjListChoice == 0
                        ? T("Add model hashes below to filter world objects", "Добавь хеш модели снизу для фильтрации", "Додай хеш моделі знизу для фільтрації")
                        : T("Add model hashes below to hide them from view", "Добавь хеш модели снизу, чтобы скрыть её", "Додай хеш моделі знизу, щоб приховати її"),
                    140.f);
            }
            else
            {
                for (int i = 0; i < modelCount; )
                {
                    ImGui::PushID(i);
                    const std::uint32_t hash = models[i];
                    char name[32]{};
                    lstrcpynA(name, ModelLabelFor(hash), sizeof(name));

                    const float removeW = 34.f * scale;
                    const float hashW = 96.f * scale;
                    const float nameW = ImMax(90.f * scale, ImGui::GetContentRegionAvail().x - hashW - removeW - 14.f * scale);

                    if (gui->text_field(
                            "##obj_name",
                            T("Name", "Название", "Назва"),
                            ImVec2(nameW, 32.f * scale),
                            name,
                            sizeof(name)))
                    {
                        SetModelLabel(hash, name);
                    }

                    ImGui::SameLine(0.f, 7.f * scale);

                    char hashStr[20]{};
                    _snprintf(hashStr, sizeof(hashStr) - 1, "0x%08X", static_cast<unsigned>(hash));
                    const ImVec2 hashPos = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(hashW, 32.f * scale));
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(
                        hashPos,
                        hashPos + ImVec2(hashW, 32.f * scale),
                        gui->get_clr(clr->element_background, 0.75f),
                        7.f * scale);
                    dl->AddText(
                        hashPos + ImVec2(10.f, 8.f) * scale,
                        gui->get_clr(clr->text),
                        hashStr);

                    ImGui::SameLine(0.f, 7.f * scale);
                    bool removed = false;
                    if (MenuUi::GhostIconButton("remove_obj", ICON_FA_TRASH, ImVec2(removeW, 32.f * scale), false))
                    {
                        for (int m = i; m + 1 < modelCount; ++m)
                            models[m] = models[m + 1];
                        models[--modelCount] = 0;
                        removed = true;
                    }

                    ImGui::PopID();
                    ImGui::Dummy(ImVec2(0.f, 3.f * scale));

                    if (!removed)
                        ++i;
                }
            }
            ImGui::EndChild();

            // Bottom Add Bar
            MenuUi::BeginToolbar("##objects_add_bar", ImVec2(0.f, addBarH));
            const float addBtnW = 100.f * scale;
            const float totalInputW = ImMax(120.f * scale, ImGui::GetContentRegionAvail().x - addBtnW - 14.f * scale);
            const float hashInputW = ImMax(60.f * scale, totalInputW * 0.45f);
            const float nameInputW = ImMax(60.f * scale, totalInputW - hashInputW);

            gui->text_field(
                "##add_obj_hash",
                "0x12345678",
                ImVec2(hashInputW, 32.f * scale),
                s_ObjHashInput,
                sizeof(s_ObjHashInput));
            ImGui::SameLine(0.f, 7.f * scale);
            gui->text_field(
                "##add_obj_name",
                T("Name", "Название", "Назва"),
                ImVec2(nameInputW, 32.f * scale),
                s_ObjNameInput,
                sizeof(s_ObjNameInput));
            ImGui::SameLine(0.f, 7.f * scale);

            const bool canAdd = modelCount < kMaxVisualModelFilters;
            if (MenuUi::ActionButton(
                    "btn_add_obj",
                    T("Add", "Добавить", "Додати"),
                    ImVec2(addBtnW, 32.f * scale),
                    canAdd,
                    ICON_FA_CIRCLE_PLUS) && canAdd)
            {
                std::uint32_t parsed = 0;
                if (ParseObjectHash(s_ObjHashInput, parsed))
                {
                    bool duplicate = false;
                    for (int d = 0; d < modelCount; ++d)
                    {
                        if (models[d] == parsed)
                        {
                            duplicate = true;
                            break;
                        }
                    }
                    if (!duplicate)
                    {
                        models[modelCount++] = parsed;
                        if (s_ObjNameInput[0])
                            SetModelLabel(parsed, s_ObjNameInput);
                        s_ObjHashInput[0] = '\0';
                        s_ObjNameInput[0] = '\0';
                    }
                }
            }
            MenuUi::EndToolbar();
        }
        MenuUi::EndGlassPanel();
    }

    // -------------------------------------------------------------------------
    // Sub-tab 3: Items (Предметы)
    // -------------------------------------------------------------------------
    struct GroundItemEntry final
    {
        char name[64]{};
        std::uint32_t modelHash{};
        float x{};
        float y{};
        float z{};
        float distance{};
        const char* icon{ ICON_FA_BOX_OPEN };
    };

    static char s_ItemSearch[48]{};
    static float s_ItemMaxDist = 1000.f;
    static bool s_ItemFilterByObjectList = true;

    static void RenderItemsSubPage(
        const MenuUi::MasterDetailMetrics& layout,
        float scale) noexcept
    {
        SeedDefaultObjectFilters();
        ObjectVisualSettings& visual = s_ObjectVisualSettings;

        // 1. Gather Items
        GroundItemEntry items[512]{};
        int itemCount = 0;

        float localX = 0.f, localY = 0.f, localZ = 0.f;
        bool haveLocal = false;
        RagePlayerNames::SnapshotEntry pEntries[64]{};
        const int pCount = RagePlayerNames::CopySnapshot(pEntries, IM_ARRAYSIZE(pEntries));
        for (int i = 0; i < pCount; ++i)
        {
            if (pEntries[i].local)
            {
                localX = pEntries[i].x;
                localY = pEntries[i].y;
                localZ = pEntries[i].z;
                haveLocal = true;
                break;
            }
        }

        RageWorldEntities::Entry worldEnts[RageWorldEntities::kMaxEntries]{};
        const int entCount = RageWorldEntities::CopySnapshot(worldEnts, IM_ARRAYSIZE(worldEnts));
        for (int i = 0; i < entCount && itemCount < IM_ARRAYSIZE(items); ++i)
        {
            const auto& e = worldEnts[i];
            if (!e.valid)
                continue;
            if (e.kind != RageWorldEntities::EntityKind::Pickup && e.kind != RageWorldEntities::EntityKind::Object)
                continue;

            float dist = 0.f;
            if (haveLocal)
            {
                const float dx = e.x - localX;
                const float dy = e.y - localY;
                const float dz = e.z - localZ;
                dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            }
            if (dist > s_ItemMaxDist)
                continue;

            const bool pickup = (e.kind == RageWorldEntities::EntityKind::Pickup);
            if (s_ItemFilterByObjectList)
            {
                if (pickup && !visual.showPickups)
                    continue;
                if (!pickup && !visual.showObjects)
                    continue;
                if (!PassesModelFilter(
                        e.modelHash,
                        visual.includeModels,
                        visual.showAll ? 0 : visual.includeModelCount,
                        visual.excludeModels,
                        visual.excludeModelCount))
                    continue;
            }

            auto& it = items[itemCount++];
            it.modelHash = e.modelHash;
            it.x = e.x;
            it.y = e.y;
            it.z = e.z;
            it.distance = dist;
            it.icon = (e.kind == RageWorldEntities::EntityKind::Pickup) ? ICON_FA_BOX_OPEN : ICON_FA_CUBE;

            const char* label = ModelLabelFor(e.modelHash);
            if (label && label[0])
            {
                lstrcpynA(it.name, label, sizeof(it.name));
            }
            else
            {
                bool foundWpn = false;
                for (const auto& w : WeaponCatalog::kWeapons)
                {
                    if (w.hash == e.modelHash)
                    {
                        lstrcpynA(it.name, w.name, sizeof(it.name));
                        it.icon = ICON_FA_GUN;
                        foundWpn = true;
                        break;
                    }
                }
                if (!foundWpn)
                {
                    _snprintf(it.name, sizeof(it.name) - 1, "%s 0x%08X",
                        e.kind == RageWorldEntities::EntityKind::Pickup ? "Pickup" : "Object",
                        static_cast<unsigned>(e.modelHash));
                }
            }

            if (it.name[0])
            {
                if (strstr(it.name, "Rifle") || strstr(it.name, "Carbine") || strstr(it.name, "Pistol") || strstr(it.name, "Gun") || strstr(it.name, "Shotgun") || strstr(it.name, "SMG"))
                    it.icon = ICON_FA_GUN;
                else if (strstr(it.name, "Armor") || strstr(it.name, "Броня"))
                    it.icon = ICON_FA_SHIELD_HALVED;
                else if (strstr(it.name, "Kit") || strstr(it.name, "Аптечка"))
                    it.icon = ICON_FA_KIT_MEDICAL;
                else if (strstr(it.name, "Spank") || strstr(it.name, "Kosyak") || strstr(it.name, "Analgetic"))
                    it.icon = ICON_FA_PILLS;
            }
        }

        // Realistic mock items for MenuPreview or when no world items exist
        if (itemCount == 0)
        {
            struct MockItem {
                const char* name;
                std::uint32_t hash;
                float dist;
                float ox, oy, oz;
                const char* icon;
            };
            static const MockItem kMocks[] = {
                { "Carbine Rifle", 0x3D2E1AE8u, 14.f, 10.f, 8.f, 0.f, ICON_FA_GUN },
                { "Special Carbine", 0xC0A3098Du, 22.f, -15.f, 12.f, 0.f, ICON_FA_GUN },
                { "Spank/Kosyak/Analgetic", 0x2D9A5028u, 31.f, 20.f, -18.f, 0.f, ICON_FA_PILLS },
                { "First Aid Kit", 0xE210FECFu, 45.f, 35.f, 22.f, 0.f, ICON_FA_KIT_MEDICAL },
                { "Armor", 0x29CB0F3Cu, 58.f, -42.f, -32.f, 0.f, ICON_FA_SHIELD_HALVED },
                { "Assault Rifle Ammo (x120)", 0x116A8626u, 82.f, 60.f, 45.f, 0.f, ICON_FA_BOX_OPEN },
                { "Repair Kit", 0x84D1C34Au, 105.f, -75.f, 62.f, 0.f, ICON_FA_WRENCH },
                { "Jerry Can", 0x34A67B97u, 130.f, 90.f, -85.f, 0.f, ICON_FA_GAS_PUMP },
                { "Baseball Bat", 0x958A4A8Fu, 155.f, -75.f, 62.f, 0.f, ICON_FA_BASEBALL_BAT_BALL },
                { "Cash Bag ($15,000)", 0x113FD533u, 190.f, 90.f, -85.f, 0.f, ICON_FA_SACK_DOLLAR },
            };

            for (const auto& m : kMocks)
            {
                if (m.dist > s_ItemMaxDist)
                    continue;
                if (s_ItemFilterByObjectList &&
                    !PassesModelFilter(
                        m.hash,
                        visual.includeModels,
                        visual.showAll ? 0 : visual.includeModelCount,
                        visual.excludeModels,
                        visual.excludeModelCount))
                    continue;
                auto& it = items[itemCount++];
                lstrcpynA(it.name, m.name, sizeof(it.name));
                it.modelHash = m.hash;
                it.distance = m.dist;
                it.x = localX + m.ox;
                it.y = localY + m.oy;
                it.z = localZ + m.oz;
                it.icon = m.icon;
            }
        }

        // Sort by distance ascending
        std::sort(items, items + itemCount, [](const GroundItemEntry& a, const GroundItemEntry& b) {
            return a.distance < b.distance;
        });

        // 2. Master Panel (Left)
        MenuUi::BeginGlassPanel(
            "##items_settings",
            ImVec2(layout.masterWidth, layout.masterHeight),
            T("Item tools", "Настройки предметов", "Налаштування предметів"),
            T("Search & Teleport", "Поиск и телепорт", "Пошук та телепорт"),
            ImGuiWindowFlags_None,
            false);
        {
            gui->text_field(
                "##item_search_input",
                T("Search item...", "Поиск предмета...", "Пошук предмета..."),
                ImVec2(ImGui::GetContentRegionAvail().x, 32.f * scale),
                s_ItemSearch,
                sizeof(s_ItemSearch));

            ImGui::Dummy(ImVec2(0.f, 6.f * scale));

            gui->slider_float_icon(
                ICON_FA_RULER,
                T("Scan range###items_range", "Радиус поиска###items_range", "Радіус пошуку###items_range"),
                &s_ItemMaxDist,
                10.f,
                10000.f,
                "%.0f m");

            ImGui::Dummy(ImVec2(0.f, 6.f * scale));

            gui->checkbox(
                T("Filter by object list", "Фильтр по списку объектов", "Фільтр за списком об'єктів"),
                &s_ItemFilterByObjectList);

            if (s_ItemFilterByObjectList)
            {
                char filterInfo[64]{};
                _snprintf(
                    filterInfo,
                    sizeof(filterInfo) - 1,
                    "%s: %d %s",
                    T("Whitelist", "Белый список", "Білий список"),
                    visual.showAll ? 0 : visual.includeModelCount,
                    T("models", "моделей", "моделей"));
                ImGui::TextDisabled("%s", filterInfo);
            }

            ImGui::Dummy(ImVec2(0.f, 4.f * scale));

            if (MenuUi::ActionButton(
                    "items_goto_object_filters",
                    T("Edit object filters", "Настроить фильтры объектов", "Налаштувати фільтри об'єктів"),
                    ImVec2(ImGui::GetContentRegionAvail().x, 32.f * scale),
                    true,
                    ICON_FA_FILTER))
            {
                s_FilterSubTab = 2;
            }

            ImGui::Dummy(ImVec2(0.f, 6.f * scale));

            // TP to nearest item
            if (MenuUi::ActionButton(
                    "items_tp_nearest",
                    T("TP to nearest item", "ТП к ближайшему", "ТП до найближчого"),
                    ImVec2(ImGui::GetContentRegionAvail().x, 34.f * scale),
                    itemCount > 0,
                    ICON_FA_BOLT))
            {
                if (itemCount > 0)
                {
                    const auto& target = items[0];
#if HAS_NATIVE_INVOKE
                    int ped = NativeInvoke::PlayerPedId();
                    if (ped)
                    {
                        int veh = NativeInvoke::GetVehiclePedIsIn(ped, false);
                        int ent = veh ? veh : ped;
                        NativeInvoke::SetEntityCoordsNoOffset(ent, target.x, target.y, target.z + 0.5f, false, false, false);
                    }
#endif
                    char tpScript[256]{};
                    _snprintf(tpScript, sizeof(tpScript) - 1,
                        "const p = mp.players.local; if (p) { const v = p.vehicle; if (v) v.position = new mp.Vector3(%f, %f, %f); else p.position = new mp.Vector3(%f, %f, %f); }",
                        target.x, target.y, target.z + 0.5f, target.x, target.y, target.z + 0.5f);
                    QuantTools::QueueCommand(tpScript, "TpItem", true);

                    char notif[128]{};
                    _snprintf(notif, sizeof(notif) - 1, "%s: %s",
                        T("Teleported to item", "Телепортирован к предмету", "Телепортовано до предмета"),
                        target.name);
                    PushNotification(T("Filters", "Фильтры", "Фільтри"), notif, true, 2500);
                }
            }

            ImGui::Dummy(ImVec2(0.f, 6.f * scale));

#if HAS_AUTOLOOT
            auto& loot = AutoLoot::Access();
            gui->checkbox(
                T("Auto-pickup", "Авто-лут предметов", "Авто-лут предметів"),
                &loot.enabled);
            gui->slider_float_icon(
                ICON_FA_RULER,
                T("Loot radius###autoloot_radius", "Радиус лута###autoloot_radius", "Радіус луту###autoloot_radius"),
                &loot.radius,
                1.f,
                AutoLoot::kMaxRadius,
                "%.0f m");
            gui->checkbox(
                T("Take all items", "Подбирать всё", "Підбирати все"),
                &loot.takeAll);
            ImGui::Dummy(ImVec2(0.f, 4.f * scale));
#endif

            char statsBuf[64]{};
            _snprintf(statsBuf, sizeof(statsBuf) - 1, "%d %s", itemCount, T("items found", "найдено предметов", "знайдено предметів"));
            MenuUi::StatusChip("items_count_chip", statsBuf, itemCount > 0);
        }
        MenuUi::EndGlassPanel();

        if (layout.sideBySide)
            ImGui::SameLine(0.f, layout.gap);

        // 3. Detail Panel (Right)
        char itemListSubtitle[96]{};
        _snprintf(
            itemListSubtitle,
            sizeof(itemListSubtitle) - 1,
            "%d %s",
            itemCount,
            T("items nearby", "предметов рядом", "предметів поруч"));

        MenuUi::BeginGlassPanel(
            "##items_list_panel",
            ImVec2(layout.detailWidth, layout.detailHeight),
            T("Ground Items", "Список предметов", "Список предметів"),
            itemListSubtitle,
            ImGuiWindowFlags_None,
            false);
        {
            ImGui::BeginChild(
                "##items_scroll",
                ImVec2(0.f, 0.f),
                ImGuiChildFlags_None,
                ImGuiWindowFlags_NoSavedSettings);

            const float btnH = 28.f * scale;
            const float markerW = 74.f * scale;
            const float tpW = 58.f * scale;
            const float rightSideW = markerW + tpW + 6.f * scale;

            int drawnCount = 0;
            for (int i = 0; i < itemCount; ++i)
            {
                const auto& item = items[i];

                char hexStr[20]{};
                _snprintf(hexStr, sizeof(hexStr) - 1, "0x%08X", static_cast<unsigned>(item.modelHash));

                if (s_ItemSearch[0] != '\0')
                {
                    if (!StrContainsCaseInsensitive(item.name, s_ItemSearch) &&
                        !StrContainsCaseInsensitive(hexStr, s_ItemSearch))
                        continue;
                }

                ImGui::PushID(i);

                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                const float availW = ImGui::GetContentRegionAvail().x;
                const float rowH = 46.f * scale;
                ImDrawList* dl = ImGui::GetWindowDrawList();

                // Background container
                dl->AddRectFilled(
                    p0,
                    p0 + ImVec2(availW, rowH),
                    gui->get_clr(clr->element_background, 0.45f),
                    8.f * scale);
                dl->AddRect(
                    p0,
                    p0 + ImVec2(availW, rowH),
                    gui->get_clr(clr->child_stroke_bar, 0.6f),
                    8.f * scale);

                // Title & Subtitle text (left-aligned without icon box)
                const ImVec2 textPos = p0 + ImVec2(14.f * scale, 6.f * scale);
                dl->AddText(
                    textPos,
                    gui->get_clr(clr->text),
                    item.name);

                char metaBuf[64]{};
                _snprintf(metaBuf, sizeof(metaBuf) - 1, "%.0f m  ·  %s", item.distance, hexStr);
                dl->AddText(
                    textPos + ImVec2(0.f, 18.f * scale),
                    gui->get_clr(clr->text, 0.65f),
                    metaBuf);

                // Action buttons on the right
                ImGui::SetCursorScreenPos(p0 + ImVec2(availW - rightSideW - 8.f * scale, 9.f * scale));

                char markerBtnId[32]{};
                _snprintf(markerBtnId, sizeof(markerBtnId) - 1, "wp_btn_%d", i);
                if (MenuUi::ActionButton(
                        markerBtnId,
                        T("Mark", "Метка", "Мітка"),
                        ImVec2(markerW, btnH),
                        true,
                        ICON_FA_LOCATION_DOT))
                {
#if HAS_NATIVE_INVOKE
                    NativeInvoke::Invoke<void>(0xFE43368D2896B380ull, item.x, item.y);
#endif
                    char wpScript[160]{};
                    _snprintf(wpScript, sizeof(wpScript) - 1,
                        "if (mp && mp.game && mp.game.ui) mp.game.ui.setNewWaypoint(%f, %f);",
                        item.x, item.y);
                    QuantTools::QueueCommand(wpScript, "SetWaypoint", true);

                    char notif[128]{};
                    _snprintf(notif, sizeof(notif) - 1, "%s: %s (%.0f m)",
                        T("Waypoint set", "Метка установлена", "Мітка встановлена"),
                        item.name, item.distance);
                    PushNotification(T("Filters", "Фильтры", "Фільтри"), notif, true, 2500);
                }

                ImGui::SameLine(0.f, 6.f * scale);

                char tpBtnId[32]{};
                _snprintf(tpBtnId, sizeof(tpBtnId) - 1, "tp_btn_%d", i);
                if (MenuUi::ActionButton(
                        tpBtnId,
                        T("TP", "ТП", "ТП"),
                        ImVec2(tpW, btnH),
                        true,
                        ICON_FA_BOLT))
                {
#if HAS_NATIVE_INVOKE
                    int ped = NativeInvoke::PlayerPedId();
                    if (ped)
                    {
                        int veh = NativeInvoke::GetVehiclePedIsIn(ped, false);
                        int ent = veh ? veh : ped;
                        NativeInvoke::SetEntityCoordsNoOffset(ent, item.x, item.y, item.z + 0.5f, false, false, false);
                    }
#endif
                    char tpScript[256]{};
                    _snprintf(tpScript, sizeof(tpScript) - 1,
                        "const p = mp.players.local; if (p) { const v = p.vehicle; if (v) v.position = new mp.Vector3(%f, %f, %f); else p.position = new mp.Vector3(%f, %f, %f); }",
                        item.x, item.y, item.z + 0.5f, item.x, item.y, item.z + 0.5f);
                    QuantTools::QueueCommand(tpScript, "TpItem", true);

                    char notif[128]{};
                    _snprintf(notif, sizeof(notif) - 1, "%s: %s",
                        T("Teleported to item", "Телепортирован к предмету", "Телепортовано до предмета"),
                        item.name);
                    PushNotification(T("Filters", "Фильтры", "Фільтри"), notif, true, 2500);
                }

                ImGui::PopID();
                ImGui::SetCursorScreenPos(p0 + ImVec2(0.f, rowH + 6.f * scale));
                ++drawnCount;
            }

            if (drawnCount == 0)
            {
                MenuUi::EmptyState(
                    "items_empty",
                    ICON_FA_BOX_OPEN,
                    T("No items found", "Предметы не найдены", "Предмети не знайдені"),
                    T("Try expanding the search range or changing filter", "Попробуйте увеличить радиус поиска или изменить фильтр", "Спробуйте збільшити радіус пошуку або змінити фільтр"),
                    140.f);
            }

            ImGui::EndChild();
        }
        MenuUi::EndGlassPanel();
    }

    // -------------------------------------------------------------------------
    // Main Entry: Filters Page
    // -------------------------------------------------------------------------
    void RenderPlayersPage()
    {
        const float availY = ImGui::GetContentRegionAvail().y;
        const float availX = ImGui::GetContentRegionAvail().x;
        const float scale = UIScale();
        const MenuUi::MasterDetailMetrics layout =
            MenuUi::CalculateMasterDetail(
                availX,
                availY,
                310.f,
                420.f,
                250.f);

        switch (s_FilterSubTab)
        {
        case 0:
            RenderFriendsSubPage(layout, scale);
            break;
        case 1:
            RenderFactionsSubPage(layout, scale);
            break;
        case 2:
            RenderObjectsSubPage(layout, scale);
            break;
        case 3:
            RenderItemsSubPage(layout, scale);
            break;
        default:
            RenderFriendsSubPage(layout, scale);
            break;
        }
    }
}
