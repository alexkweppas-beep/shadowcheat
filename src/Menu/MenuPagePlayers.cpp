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
#include "../Features/FiveRpAdminScan.h"
#include "../Features/ServerProfile.h"
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
            T("Around you", "Вокруг тебя", "Навколо тебе"),
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
        MiscRow(
            "players_show_admin_list",
            ICON_FA_LIST,
            T("Admin list", "Список админов", "Список адмінів"),
            &s.showAdminList);
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
            T("Admin list", "Список админов", "Список адмінів"),
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
                96.f);
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

    void RenderPlayersPage()
    {
        static char s_FriendInput[48]{};
        static float s_AddRadius = 50.f;
        EnsureAimProfilesInitialized();

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
                // Removal is deferred by one animation: the row has to be able
                // to collapse before the entry behind it disappears. Copied,
                // not referenced, because Friends::Remove compacts the very
                // array the name lives in.
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

                    // Enter grows the row and fades it in; leave does the same
                    // in reverse, so the list closes the gap by itself.
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
}
