#include "MenuSearch.h"

#define IMGUI_DEFINE_MATH_OPERATORS

#include <Windows.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"
#include "Menu.h"
#include "MenuState.h"
#include "../SDK/RageBuildFlavor.h"
#include "../Features/FiveRpMetadataResolver.h"
#include "../Features/ScriptBridge.h"
#include "../Features/UkraineTools.h"

namespace Menu
{
    enum class SearchRouteKind : std::uint8_t
    {
        Main,
        Combat,
        VisualCategory,
        VisualPedProfile,
        Misc,
        Settings,
        ScriptPage,
        ServerTab
    };

    enum class SearchAvailability : std::uint8_t
    {
        Always,
        Quant,
        Ukraine,
        FiveRp,
        RpServer,
        Developer
    };

    struct SearchDestination final
    {
        const char* id;
        const char* titleEn;
        const char* titleRu;
        const char* titleUa;
        const char* categoryEn;
        const char* categoryRu;
        const char* categoryUa;
        const char* keywords;
        SearchRouteKind route;
        MainPage page;
        int value;
        SearchAvailability availability;
    };

    static const SearchDestination kSearchDestinations[] = {
        {
            "combat", "Aim", "Аим", "Аім",
            "Aim", "Аим", "Аім",
            "aim aimbot targeting",
            SearchRouteKind::Main, MainPage::Combat, 0,
            SearchAvailability::Always
        },
        {
            "combat_global", "Vector", "Вектор", "Вектор",
            "Aim / Main", "Аим / Основное", "Аім / Основне",
            "vector smooth fov humanize targeting",
            SearchRouteKind::Combat, MainPage::Combat,
            0,
            SearchAvailability::Always
        },
        {
            "weapons",
            "Arsenal", "Арсенал", "Арсенал",
            "Aim / Arsenal", "Аим / Арсенал", "Аім / Арсенал",
            "weapons arsenal guns overrides attachments recoil spread",
            SearchRouteKind::Combat, MainPage::Combat,
            3,
            SearchAvailability::Always
        },
        {
            "combat_silent", "Silent", "Silent", "Silent",
            "Aim / Main", "Аим / Основное", "Аім / Основне",
            "hitchance lock mark bullet redirect wallshot walls",
            SearchRouteKind::Combat, MainPage::Combat,
            1,
            SearchAvailability::Always
        },
        {
            "combat_wallshot", "Wallshot", "Wallshot", "Wallshot",
            "Aim / Main", "Аим / Основное", "Аім / Основне",
            "wallshot walls ignore visible silent",
            SearchRouteKind::Combat, MainPage::Combat,
            1,
            SearchAvailability::Always
        },
        {
            "combat_humanize", "Humanize", "Хуманизация", "Гуманізація",
            "Aim / Main", "Аим / Основное", "Аім / Основне",
            "reaction jitter overshoot fatigue smooth camera",
            SearchRouteKind::Combat, MainPage::Combat,
            0,
            SearchAvailability::Always
        },
        {
            "combat_trigger", "Trigger", "Триггер", "Тригер",
            "Aim / Trigger", "Аим / Триггер", "Аім / Тригер",
            "trigger fire delay cooldown hitbox",
            SearchRouteKind::Combat, MainPage::Combat,
            2,
            SearchAvailability::Always
        },
        {
            "combat_damager", "Damager", "Дамагер", "Дамагер",
            "Aim / Damager", "Аим / Дамагер", "Аім / Дамагер",
            "damager damage fist punch melee kill fov",
            SearchRouteKind::Combat, MainPage::Combat,
            4,
            SearchAvailability::Always
        },
        {
            "visuals", "Visuals", "Визуалы", "Візуали",
            "Visuals", "Визуалы", "Візуали",
            "esp overlay preview",
            SearchRouteKind::Main, MainPage::Visuals, 0,
            SearchAvailability::Always
        },
        {
            "visual_players", "Players", "Игроки", "Гравці",
            "Visuals / Peds", "Визуалы / Педы", "Візуали / Педи",
            "player esp",
            SearchRouteKind::VisualPedProfile, MainPage::Visuals,
            static_cast<int>(EspProfileId::Players),
            SearchAvailability::Always
        },
        {
            "visual_faction", "Faction", "Фракция", "Фракція",
            "Visuals / Peds", "Визуалы / Педы", "Візуали / Педи",
            "faction players esp",
            SearchRouteKind::VisualPedProfile, MainPage::Visuals,
            static_cast<int>(EspProfileId::FactionPlayers),
            SearchAvailability::RpServer
        },
        {
            "visual_family", "Family", "Семья", "Сім'я",
            "Visuals / Peds", "Визуалы / Педы", "Візуали / Педи",
            "family players esp",
            SearchRouteKind::VisualPedProfile, MainPage::Visuals,
            static_cast<int>(EspProfileId::FamilyPlayers),
            SearchAvailability::RpServer
        },
        {
            "visual_npc", "NPC", "NPC", "NPC",
            "Visuals / Peds", "Визуалы / Педы", "Візуали / Педи",
            "ped non player",
            SearchRouteKind::VisualPedProfile, MainPage::Visuals,
            static_cast<int>(EspProfileId::Npc),
            SearchAvailability::Always
        },
        {
            "visual_admin", "Admin", "Админ", "Адмін",
            "Visuals / Peds", "Визуалы / Педы", "Візуали / Педи",
            "administrator esp",
            SearchRouteKind::VisualPedProfile, MainPage::Visuals,
            static_cast<int>(EspProfileId::Admin),
            SearchAvailability::Always
        },
        {
            "visual_animal", "Animal", "Животные", "Тварини",
            "Visuals / Peds", "Визуалы / Педы", "Візуали / Педи",
            "animals fauna esp",
            SearchRouteKind::VisualPedProfile, MainPage::Visuals,
            static_cast<int>(EspProfileId::Animal),
            SearchAvailability::Always
        },
        {
            "visual_mesh", "Chams and glow", "Chams и glow", "Chams і glow",
            "Visuals / Peds", "Визуалы / Педы", "Візуали / Педи",
            "chams glow mesh silhouette outline",
            SearchRouteKind::VisualCategory, MainPage::Visuals,
            static_cast<int>(VisualCategory::Peds),
            SearchAvailability::Always
        },
        {
            "visual_radar", "Radar", "Радар", "Радар",
            "Visuals", "Визуалы", "Візуали",
            "radar minimap direction heading",
            SearchRouteKind::VisualCategory, MainPage::Visuals,
            static_cast<int>(VisualCategory::Radar),
            SearchAvailability::Always
        },
        {
            "visual_world", "World entities", "Объекты мира", "Об'єкти світу",
            "Visuals", "Визуалы", "Візуали",
            "vehicle car object pickup esp occupied health model hash",
            SearchRouteKind::VisualCategory, MainPage::Visuals,
            static_cast<int>(VisualCategory::World),
            SearchAvailability::Always
        },
        {
            "scripts", "Scripts", "Скрипты", "Скрипти",
            "Scripts", "Скрипты", "Скрипти",
            "javascript executor cloud runtime",
            SearchRouteKind::ScriptPage, MainPage::Scripts,
            static_cast<int>(ScriptBridge::Page::Scripts),
            SearchAvailability::Always
        },
#if RAGEMP_EVENT_LOGGER
        {
            "event_logger", "Event Logger", "Логгер событий", "Логер подій",
            "Scripts", "Скрипты", "Скрипти",
            "events incoming outgoing cef native browser",
            SearchRouteKind::ScriptPage, MainPage::Scripts,
            static_cast<int>(ScriptBridge::Page::EventLogger),
            SearchAvailability::Developer
        },
#endif
        {
            "filters", "Filters", "Фильтры", "Фільтри",
            "Filters", "Фильтры", "Фільтри",
            "filters friend list nearby factions colors objects items teleport waypoint",
            SearchRouteKind::Main, MainPage::Players, 0,
            SearchAvailability::Always
        },
        {
            "filters_friends", "Friends", "Друзья", "Друзі",
            "Filters", "Фильтры", "Фільтри",
            "friends list radius nick add",
            SearchRouteKind::Main, MainPage::Players, 0,
            SearchAvailability::Always
        },
        {
            "filters_factions", "Factions", "Фракции", "Фракції",
            "Filters", "Фильтры", "Фільтри",
            "faction colors tags esp",
            SearchRouteKind::Main, MainPage::Players, 1,
            SearchAvailability::Always
        },
        {
            "filters_objects", "Objects", "Объекты", "Об'єкти",
            "Filters", "Фильтры", "Фільтри",
            "object model filter hash include exclude",
            SearchRouteKind::Main, MainPage::Players, 2,
            SearchAvailability::Always
        },
        {
            "filters_items", "Items", "Предметы", "Предмети",
            "Filters", "Фильтры", "Фільтри",
            "ground items teleport waypoint autoloot pickup",
            SearchRouteKind::Main, MainPage::Players, 3,
            SearchAvailability::Always
        },
        {
            "player", "Player", "Игрок", "Гравець",
            "Misc", "Разное", "Різне",
            "self movement player stamina jump misc",
            SearchRouteKind::Main, MainPage::Player, 0,
            SearchAvailability::Always
        },
        {
            "misc_self", "Self", "Себя", "Себе",
            "Misc", "Разное", "Різне",
            "god health armor respawn stamina jump greenzone",
            SearchRouteKind::Misc, MainPage::Player, 0,
            SearchAvailability::Always
        },
        {
            "misc_move", "Movement", "Движение", "Рух",
            "Misc", "Разное", "Різне",
            "speedhack noclip freecam teleport waypoint click warp",
            SearchRouteKind::Misc, MainPage::Player, 1,
            SearchAvailability::Always
        },
        {
            "cars", "Cars", "Машины", "Авто",
            "Misc", "Разное", "Різне",
            "car vehicle gravity speed repair service seatbelt "
            "engine autopilot flip crash",
            SearchRouteKind::Main, MainPage::Cars, 0,
            SearchAvailability::Always
        },
        {
            "world", "World", "Мир", "Світ",
            "Misc", "Разное", "Різне",
            "weather time fog clock",
            SearchRouteKind::Main, MainPage::World, 0,
            SearchAvailability::Always
        },
        {
            "players_nearby", "Nearby overlay", "Оверлей рядом", "Оверлей поруч",
            "Misc", "Разное", "Різне",
            "players admins distance overlay",
            SearchRouteKind::Main, MainPage::Players, 0,
            SearchAvailability::Always
        },
        {
            "settings", "Settings", "Настройки", "Налаштування",
            "Configs", "Конфиги", "Конфіги",
            "configuration interface ui",
            SearchRouteKind::Main, MainPage::Settings, 0,
            SearchAvailability::Always
        },
        {
            "settings_configs", "Configs", "Конфиги", "Конфіги",
            "Configs", "Конфиги", "Конфіги",
            "profiles cloud save load import export",
            SearchRouteKind::Settings, MainPage::Settings, 0,
            SearchAvailability::Always
        },
        {
            "quant", "Quant", "Quant", "Quant",
            "Misc", "Разное", "Різне",
            "quant rp server tools",
            SearchRouteKind::Main, MainPage::Server, 0,
            SearchAvailability::Quant
        },
        {
            "ukraine", "Ukraine", "Ukraine", "Ukraine",
            "Misc", "Разное", "Різне",
            "ukraine gta server tools",
            SearchRouteKind::Main, MainPage::Server, 0,
            SearchAvailability::Ukraine
        },
        {
            "fiverp_farm", "5RP Farm", "5RP Коровы", "5RP Корови",
            "Misc", "Разное", "Різне",
            "gta5rp farm cow milk invis teleport бот коровы",
            SearchRouteKind::Main, MainPage::Server, 0,
            SearchAvailability::FiveRp
        },
        {
            "ukraine_jobs", "Ukraine Jobs",
            "Работы Ukraine", "Роботи Ukraine",
            "Misc", "Разное", "Різне",
            "jobs work routes",
            SearchRouteKind::ServerTab,
            MainPage::Server, 1,
            SearchAvailability::Ukraine
        },
        {
            "ukraine_bus", "Ukraine Bus",
            "Автобус Ukraine", "Автобус Ukraine",
            "Misc", "Разное", "Різне",
            "bus driver route автобус",
            SearchRouteKind::ServerTab,
            MainPage::Server, 3,
            SearchAvailability::Ukraine
        },
        {
            "ukraine_carrier", "Ukraine Carrier",
            "Дальнобойщик Ukraine",
            "Далекобійник Ukraine",
            "Misc", "Разное", "Різне",
            "carrier truck дальнобой далекобій loading cargo",
            SearchRouteKind::ServerTab,
            MainPage::Server, 4,
            SearchAvailability::Ukraine
        }
    };


    static char FoldSearchChar(char value) noexcept
    {
        return value >= 'A' && value <= 'Z'
            ? static_cast<char>(value + ('a' - 'A'))
            : value;
    }

    static void NormalizeSearchText(
        const char* source,
        char* output,
        int capacity) noexcept
    {
        if (!output || capacity <= 0)
            return;
        output[0] = 0;
        if (!source || !source[0])
            return;
        wchar_t wide[320]{};
        int wideLength = MultiByteToWideChar(
            CP_UTF8,
            0,
            source,
            -1,
            wide,
            static_cast<int>(
                sizeof(wide) / sizeof(wide[0])));
        if (wideLength <= 0)
            return;
        if (wideLength > 1)
            CharLowerBuffW(
                wide,
                static_cast<DWORD>(wideLength - 1));
        int write = 0;
        bool pendingSpace = false;
        for (int read = 0;
             wide[read] && write < capacity - 1;
             ++read)
        {
            const wchar_t value = wide[read];
            const bool separator =
                value == L' ' || value == L'\t' ||
                value == L'_' || value == L'-' ||
                value == L'/' || value == L'\\';
            if (separator)
            {
                pendingSpace = write > 0;
                continue;
            }
            if (pendingSpace && write < capacity - 1)
                output[write++] = ' ';
            pendingSpace = false;
            char encoded[4]{};
            const int encodedLength =
                WideCharToMultiByte(
                    CP_UTF8,
                    0,
                    &value,
                    1,
                    encoded,
                    static_cast<int>(
                        sizeof(encoded)),
                    nullptr,
                    nullptr);
            if (encodedLength <= 0 ||
                write + encodedLength >= capacity)
                continue;
            for (int index = 0;
                 index < encodedLength;
                 ++index)
                output[write++] = encoded[index];
        }
        output[write] = 0;
    }

    static bool SearchStartsWith(
        const char* text,
        const char* prefix) noexcept
    {
        if (!text || !prefix || !prefix[0])
            return false;
        int index = 0;
        while (prefix[index] && text[index] == prefix[index])
            ++index;
        return prefix[index] == 0;
    }

    static int SearchSubstringPosition(
        const char* text,
        const char* needle) noexcept
    {
        if (!text || !needle || !needle[0])
            return -1;
        for (int start = 0; text[start]; ++start)
        {
            int index = 0;
            while (needle[index] &&
                   text[start + index] == needle[index])
                ++index;
            if (!needle[index])
                return start;
        }
        return -1;
    }

    static bool SearchTokenPrefixes(
        const char* text,
        const char* query) noexcept
    {
        if (!text || !query || !query[0])
            return false;
        int queryStart = 0;
        while (query[queryStart])
        {
            while (query[queryStart] == ' ')
                ++queryStart;
            if (!query[queryStart])
                break;
            int queryEnd = queryStart;
            while (query[queryEnd] &&
                   query[queryEnd] != ' ')
                ++queryEnd;

            bool matched = false;
            for (int word = 0; text[word] && !matched;)
            {
                while (text[word] == ' ')
                    ++word;
                if (!text[word])
                    break;
                int q = queryStart;
                int t = word;
                while (q < queryEnd &&
                       text[t] &&
                       text[t] != ' ' &&
                       text[t] == query[q])
                {
                    ++q;
                    ++t;
                }
                matched = q == queryEnd;
                while (text[word] && text[word] != ' ')
                    ++word;
            }
            if (!matched)
                return false;
            queryStart = queryEnd;
        }
        return true;
    }

    static int BoundedEditDistance(
        const char* left,
        const char* right,
        int bound) noexcept
    {
        if (!left || !right || bound < 0)
            return bound + 1;
        const int leftLength =
            static_cast<int>(strlen(left));
        const int rightLength =
            static_cast<int>(strlen(right));
        if (leftLength > 63 || rightLength > 63 ||
            abs(leftLength - rightLength) > bound)
            return bound + 1;

        int previous[64]{};
        int current[64]{};
        for (int column = 0; column <= rightLength; ++column)
            previous[column] = column;
        for (int row = 1; row <= leftLength; ++row)
        {
            current[0] = row;
            int rowBest = current[0];
            for (int column = 1;
                 column <= rightLength;
                 ++column)
            {
                const int substitution =
                    previous[column - 1] +
                    (left[row - 1] == right[column - 1]
                        ? 0 : 1);
                current[column] = (std::min)(
                    substitution,
                    (std::min)(
                        previous[column] + 1,
                        current[column - 1] + 1));
                rowBest = (std::min)(rowBest, current[column]);
            }
            if (rowBest > bound)
                return bound + 1;
            for (int column = 0;
                 column <= rightLength;
                 ++column)
                previous[column] = current[column];
        }
        return previous[rightLength] <= bound
            ? previous[rightLength]
            : bound + 1;
    }

    static int BestWordEditDistance(
        const char* text,
        const char* query,
        int bound) noexcept
    {
        int best = bound + 1;
        for (int start = 0; text && text[start];)
        {
            while (text[start] == ' ')
                ++start;
            if (!text[start])
                break;
            char word[64]{};
            int write = 0;
            while (text[start] &&
                   text[start] != ' ' &&
                   write < static_cast<int>(sizeof(word)) - 1)
            {
                word[write++] = text[start++];
            }
            word[write] = 0;
            best = (std::min)(
                best,
                BoundedEditDistance(query, word, bound));
            while (text[start] && text[start] != ' ')
                ++start;
        }
        return best;
    }

    static bool SearchDestinationAvailable(
        const SearchDestination& destination,
        bool quantAvailable,
        bool ukraineAvailable,
        bool fiveRpAvailable) noexcept
    {
        switch (destination.availability)
        {
        case SearchAvailability::Quant:
            return quantAvailable;
        case SearchAvailability::Ukraine:
            return ukraineAvailable;
        case SearchAvailability::FiveRp:
            return fiveRpAvailable;
        case SearchAvailability::RpServer:
            return quantAvailable ||
                ukraineAvailable ||
                fiveRpAvailable ||
                FiveRpMetadataResolver::IsDetected();
        case SearchAvailability::Developer:
#if RAGEMP_DEV_BUILD
            return true;
#else
            return false;
#endif
        default:
            return true;
        }
    }

    static int ScoreSearchDestination(
        const SearchDestination& destination,
        const char* normalizedQuery) noexcept
    {
        char title[128]{};
        char identity[96]{};
        char searchable[320]{};
        NormalizeSearchText(
            T(
                destination.titleEn,
                destination.titleRu,
                destination.titleUa),
            title,
            static_cast<int>(sizeof(title)));
        NormalizeSearchText(
            destination.id,
            identity,
            static_cast<int>(sizeof(identity)));
        char source[320]{};
        _snprintf(
            source,
            sizeof(source) - 1,
            "%s %s %s",
            T(
                destination.titleEn,
                destination.titleRu,
                destination.titleUa),
            destination.id,
            destination.keywords ? destination.keywords : "");
        NormalizeSearchText(
            source,
            searchable,
            static_cast<int>(sizeof(searchable)));

        if (lstrcmpA(title, normalizedQuery) == 0 ||
            lstrcmpA(identity, normalizedQuery) == 0)
            return 0;
        if (SearchStartsWith(title, normalizedQuery))
            return 10 +
                static_cast<int>(strlen(title) - strlen(normalizedQuery));
        if (SearchStartsWith(identity, normalizedQuery))
            return 18 +
                static_cast<int>(strlen(identity) - strlen(normalizedQuery));
        if (SearchTokenPrefixes(searchable, normalizedQuery))
            return 90;

        const int titlePosition =
            SearchSubstringPosition(title, normalizedQuery);
        if (titlePosition >= 0)
            return 120 + titlePosition;
        const int broadPosition =
            SearchSubstringPosition(searchable, normalizedQuery);
        if (broadPosition >= 0)
            return 155 + (std::min)(broadPosition, 40);

        const int queryLength =
            static_cast<int>(strlen(normalizedQuery));
        const int bound =
            queryLength <= 4 ? 1 :
            (queryLength <= 8 ? 2 : 3);
        int distance = BoundedEditDistance(
            normalizedQuery,
            title,
            bound);
        distance = (std::min)(
            distance,
            BestWordEditDistance(title, normalizedQuery, bound));
        distance = (std::min)(
            distance,
            BestWordEditDistance(identity, normalizedQuery, bound));
        if (distance <= bound)
            return 300 + distance * 24;
        return 0x7FFFFFFF;
    }

    int BuildRankedSearchResults(
        const char* query,
        bool quantAvailable,
        bool ukraineAvailable,
        bool fiveRpAvailable,
        RankedSearchResult* output,
        int capacity) noexcept
    {
        if (!output || capacity <= 0)
            return 0;
        char normalized[64]{};
        NormalizeSearchText(
            query,
            normalized,
            static_cast<int>(sizeof(normalized)));
        if (!normalized[0])
            return 0;

        int count = 0;
        for (int index = 0;
             index < IM_ARRAYSIZE(kSearchDestinations);
             ++index)
        {
            const SearchDestination& destination =
                kSearchDestinations[index];
            if (!SearchDestinationAvailable(
                    destination,
                    quantAvailable,
                    ukraineAvailable,
                    fiveRpAvailable))
                continue;
            const int score =
                ScoreSearchDestination(destination, normalized);
            if (score == 0x7FFFFFFF)
                continue;

            int insert = count;
            while (insert > 0 &&
                   output[insert - 1].score > score)
                --insert;
            if (insert >= capacity)
                continue;
            const int moveEnd =
                (std::min)(count, capacity - 1);
            for (int move = moveEnd;
                 move > insert;
                 --move)
                output[move] = output[move - 1];
            output[insert] = {index, score};
            if (count < capacity)
                ++count;
        }
        return count;
    }

    static void NavigateToSearchDestination(
        const SearchDestination& destination) noexcept
    {
        s_MainPage = destination.page;
        switch (destination.route)
        {
        case SearchRouteKind::Combat:
            s_AimMode = ImClamp(destination.value, 0, 4);
            break;
        case SearchRouteKind::VisualCategory:
            s_VisualCategoryTab = ImClamp(
                destination.value,
                0,
                static_cast<int>(VisualCategory::Count) - 1);
            break;
        case SearchRouteKind::VisualPedProfile:
            s_VisualCategoryTab =
                static_cast<int>(VisualCategory::Peds);
            s_VisualPedProfile = ImClamp(
                destination.value,
                0,
                static_cast<int>(EspProfileId::Count) - 1);
            break;
        case SearchRouteKind::Misc:
            s_MiscSubTab = destination.value;
            break;
        case SearchRouteKind::Settings:
            s_ConfigSubTab = destination.value;
            break;
        case SearchRouteKind::ScriptPage:
            ScriptBridge::OpenPage(
                static_cast<ScriptBridge::Page>(
                    destination.value));
            break;
        case SearchRouteKind::ServerTab:
            UkraineTools::OpenUiTab(
                destination.value);
            break;
        default:
            break;
        }
    }

    const char* SearchDestinationId(int destination) noexcept
    {
        if (destination < 0 ||
            destination >= IM_ARRAYSIZE(kSearchDestinations))
            return "";
        return kSearchDestinations[destination].id;
    }

    const char* SearchDestinationTitle(int destination) noexcept
    {
        if (destination < 0 ||
            destination >= IM_ARRAYSIZE(kSearchDestinations))
            return "";
        const SearchDestination& item = kSearchDestinations[destination];
        return T(item.titleEn, item.titleRu, item.titleUa);
    }

    const char* SearchDestinationCategory(int destination) noexcept
    {
        if (destination < 0 ||
            destination >= IM_ARRAYSIZE(kSearchDestinations))
            return "";
        const SearchDestination& item = kSearchDestinations[destination];
        return T(item.categoryEn, item.categoryRu, item.categoryUa);
    }

    void NavigateToSearchDestination(int destination) noexcept
    {
        if (destination < 0 ||
            destination >= IM_ARRAYSIZE(kSearchDestinations))
            return;
        NavigateToSearchDestination(kSearchDestinations[destination]);
    }
}
