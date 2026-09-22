#include "UkraineTools.h"

#include "FeatureBundles.h"
#include "RageV8Runtime.h"
#include "ServerProfile.h"
#include "../Menu/Menu.h"
#include "../Menu/IconsFontAwesome6.h"
#include "../Menu/Leyawin/custom_widgets.hpp"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "../imgui/imgui.h"

#include <Windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cfloat>

namespace hooks
{
    void HLog(const char* message);
}

namespace
{
    enum class RequestKind : std::uint8_t
    {
        Load,
        Command,
        Status,
        CefDump
    };

    enum class ServerKind : std::uint8_t
    {
        Unknown,
        Ukraine,
        Other
    };

    struct PendingRequest final
    {
        std::uint64_t id{};
        RequestKind kind{};
        ULONGLONG timestamp{};
        bool silent{};
        char label[64]{};
    };

    constexpr int kMaxPending = 32;
    PendingRequest g_Pending[kMaxPending]{};
    int g_PendingCount = 0;

    bool g_Loaded = false;
    bool g_LoadRequested = false;
    ServerKind g_ServerKind = ServerKind::Unknown;
    ULONGLONG g_LastLoadAttempt = 0;
    ULONGLONG g_LastStatusRequest = 0;
    std::uint64_t g_RuntimeGeneration = 0;
    bool g_WasRuntimeReady = false;
    int g_UiTab = 0;
    char g_StatusLine[160]{"Idle"};
    char g_LastResult[160]{};

    struct WoodmanStatus final
    {
        bool on{false};
        char phase[32]{"idle"};
        int hitSlot{0};
        int hitCount{0};
        float weight{-1.f};
        float maxWeight{-1.f};
        int axeHealth{-1};
        int axeSqlId{-1};
        int woodCount{-1};
        bool overweight{false};
        char lastMsg[96]{};
    };

    struct BusStatus final
    {
        bool on{false};
        char phase[32]{"idle"};
        int routeId{6};
        int markersSeenLap{0};
        int markersPerLap{53};
        int stopsSeenLap{0};
        int stopsPerLap{4};
        bool calibrated{true};
        int lapIndex{0};
        int spamCount{0};
        int spamLeft{0};
        int earnedSession{0};
        bool currentIsStop{false};
        char style[24]{"segment_skip"};
        int speedId{1};
        int segIndex{0};
        int earnedLap{0};
        int lastLapEarned{0};
        int perHour{0};
        bool underground{false};
        char stopBursts[32]{"10,15,13"};
        char lastMsg[96]{};
    };

    struct HandymanStatus final
    {
        bool on{false};
        char phase[32]{"idle"};
        char step[24]{"-"};
        int qi{0};
        int queueLen{0};
        int skimmerHp{-1};
        int towelHp{-1};
        int chlorineQty{0};
        int chlorineNeed{0};
        char summary[64]{"-"};
        char lastMsg[96]{};
    };

    struct CarrierStatus final
    {
        bool on{false};
        bool autoMode{false};
        char phase[32]{"idle"};
        char prerequisite[32]{"off"};
        int phaseAge{0};
        int retries{0};
        int orderId{0};
        int orderWeight{0};
        int orderPrice{0};
        int knownVehWeight{-1};
        int maxWeight{-1};
        char capacitySource[24]{"unknown"};
        int routeIndex{0};
        int routeLen{0};
        int vehRemoteId{-1};
        int parkPosIndex{-1};
        bool avoidPlayers{true};
        float clearDistance{35.f};
        float pauseDistance{20.f};
        float maxUnderOffset{25.f};
        int nearestPlayerId{-1};
        float nearestPlayerDistance{-1.f};
        char nearestPlayerName[48]{};
        bool waitingClear{false};
        char lastServerAnswer[64]{"-"};
        char deliveryAck[64]{"idle"};
        char lastMsg[96]{};
    };

    struct OnlineAdminInfo final
    {
        int id{-1};
        char name[48]{};
        int level{0};
    };

    struct NearbyAdminInfo final
    {
        int id{-1};
        char name[48]{};
        int level{0};
        int dist{-1};
        bool invis{false};
        bool spectating{false};
    };

    struct AdminDetStatus final
    {
        bool on{false};
        int onlineCount{0};
        int nearbyCount{0};
        int nearestId{-1};
        int nearestDist{-1};
        char nearestName[48]{};
        int nearestLvl{0};
        int spectating{0};
        OnlineAdminInfo online[64]{};
        int onlineListed{0};
        NearbyAdminInfo nearby[32]{};
        int nearbyListed{0};
    };

    struct UkraineSettings final
    {
        bool showJobsWidget{true};
        bool pinJobsWidget{false};
        bool showAdminWidget{true};
        bool pinAdminWidget{false};
    };

    WoodmanStatus g_Woodman{};
    BusStatus g_Bus{};
    HandymanStatus g_Handyman{};
    CarrierStatus g_Carrier{};
    AdminDetStatus g_AdminDet{};
    UkraineSettings g_Settings{};

    // UI inputs
    int g_ContainerIndex = 0;
    bool g_ContainerSell = false;
    int g_AirDropItemId = 0;
    int g_BpDonate = 0;
    char g_BpAction[32]{"get"};
    int g_BpLvl = 0;
    int g_BpIndex = 0;
    int g_ParkingPlace = 0;
    int g_ParkingVehId = 0;
    int g_CarrierMaxWeight = 0;
    int g_CarrierSpamMs = 100;
    bool g_CarrierAvoidPlayers = true;
    float g_CarrierClearDistance = 35.f;
    float g_CarrierPauseDistance = 20.f;
    float g_CarrierMaxUnderOffset = 25.f;
    bool g_CarrierSkipIllegal = true;
    bool g_CarrierAutoDesired = false;
    bool g_CarrierSafetyUiInitialized = false;
    bool g_CarrierAutoReapplyPending = false;
    bool g_CarrierAutoReapplyReady = false;
    bool g_CarrierAutoCommandPending = false;
    int g_AimTargetId = -1;
    char g_RawEvent[96]{};
    char g_RawArgs[192]{"[]"};
    int g_WcfAction = 0;
    char g_CefSearch[96]{"item.add"};
    int g_BusStyleIdx = 1;
    int g_BusSpeedIdx = 1;

    // CEF dump drain -> C:/RAGEMP/ua_cef_dump/
    ULONGLONG g_CefDumpFetchAt = 0;
    ULONGLONG g_CefDumpStartedAt = 0;
    bool g_CefDumpDraining = false;
    bool g_CefDumpActive = false;
    int g_CefDumpFilesWritten = 0;
    HANDLE g_CefDumpFile = INVALID_HANDLE_VALUE;
    char g_CefDumpFileName[160]{};
    char g_CefDumpStatus[96]{"Idle"};

    void CloseCefDumpFile() noexcept;
    void ScheduleCefDumpRetry(ULONGLONG delayMs) noexcept;
    void QueueCefDumpPull(bool forceReady = false) noexcept;
    void HandleCefDumpResult(const char* text) noexcept;
    void BeginCefDumpCapture(const char* command) noexcept;

    void ResetRuntimeSession() noexcept
    {
        for (auto& pending : g_Pending)
            pending = {};
        g_PendingCount = 0;
        g_Loaded = false;
        g_LoadRequested = false;
        g_ServerKind = ServerKind::Unknown;
        g_LastLoadAttempt = 0;
        g_LastStatusRequest = 0;
        lstrcpynA(g_StatusLine, "Idle", sizeof(g_StatusLine));
        g_LastResult[0] = 0;
        g_Woodman = WoodmanStatus{};
        g_Bus = BusStatus{};
        g_Handyman = HandymanStatus{};
        g_Carrier = CarrierStatus{};
        g_CarrierMaxWeight = 0;
        g_CarrierSpamMs = 100;
        g_CarrierClearDistance = 35.f;
        g_CarrierPauseDistance = 20.f;
        g_CarrierMaxUnderOffset = 25.f;
        g_CarrierSkipIllegal = true;
        g_CarrierAutoReapplyPending =
            g_CarrierAutoDesired;
        g_CarrierAutoReapplyReady = false;
        g_CarrierAutoCommandPending = false;
        g_AdminDet = AdminDetStatus{};
        g_CefDumpFetchAt = 0;
        g_CefDumpStartedAt = 0;
        g_CefDumpDraining = false;
        g_CefDumpActive = false;
        g_CefDumpFilesWritten = 0;
        if (g_CefDumpFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(g_CefDumpFile);
            g_CefDumpFile = INVALID_HANDLE_VALUE;
        }
        g_CefDumpFileName[0] = 0;
        lstrcpynA(g_CefDumpStatus, "Idle", sizeof(g_CefDumpStatus));
    }

    bool TrackRequest(
        std::uint64_t id,
        RequestKind kind,
        const char* label,
        bool silent = false) noexcept
    {
        if (!id || g_PendingCount >= kMaxPending)
            return false;
        PendingRequest& pending = g_Pending[g_PendingCount++];
        pending = {};
        pending.id = id;
        pending.kind = kind;
        pending.timestamp = GetTickCount64();
        pending.silent = silent;
        lstrcpynA(
            pending.label,
            label ? label : "Ukraine Tools",
            static_cast<int>(sizeof(pending.label)));
        return true;
    }

    bool QueueScript(
        const char* script,
        RequestKind kind,
        const char* label,
        bool silent = false) noexcept
    {
        std::uint64_t id = 0;
        if (!RageV8Runtime::Enqueue(script, &id))
            return false;
        return TrackRequest(id, kind, label, silent);
    }

    bool BuildCommandScript(
        const char* command,
        char* output,
        int capacity) noexcept
    {
        if (!command || !command[0] || !output || capacity < 128)
            return false;
        char escaped[512]{};
        int write = 0;
        for (int read = 0;
             command[read] &&
             write < static_cast<int>(sizeof(escaped)) - 2;
             ++read)
        {
            const char value = command[read];
            if (value == '\\' || value == '\'')
                escaped[write++] = '\\';
            if (value == '\r' || value == '\n' || value == '\t')
            {
                escaped[write++] = ' ';
                continue;
            }
            escaped[write++] = value;
        }
        escaped[write] = 0;
        const int length = _snprintf(
            output,
            capacity - 1,
            "return global.__uaCommand ? "
            "global.__uaCommand('%s') : "
            "'UA_NOT_LOADED';",
            escaped);
        output[capacity - 1] = 0;
        return length > 0 && length < capacity;
    }

    int DecodeUriComponent(
        const char* input,
        char* output,
        int capacity) noexcept
    {
        if (!output || capacity <= 0)
            return 0;
        output[0] = 0;
        if (!input)
            return 0;
        int write = 0;
        for (int read = 0; input[read] && write + 1 < capacity; ++read)
        {
            const char c = input[read];
            if (c == '%' && input[read + 1] && input[read + 2])
            {
                char hex[3] = { input[read + 1], input[read + 2], 0 };
                char* end = nullptr;
                const long value = std::strtol(hex, &end, 16);
                if (end && end != hex)
                {
                    output[write++] = static_cast<char>(value & 0xFF);
                    read += 2;
                    continue;
                }
            }
            if (c == '+')
            {
                output[write++] = ' ';
                continue;
            }
            output[write++] = c;
        }
        output[write] = 0;
        return write;
    }

    int ParseInteger(const char* text) noexcept
    {
        if (!text || !text[0])
            return 0;
        return static_cast<int>(std::strtol(text, nullptr, 10));
    }

    void CloseCefDumpFile() noexcept
    {
        if (g_CefDumpFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(g_CefDumpFile);
            g_CefDumpFile = INVALID_HANDLE_VALUE;
        }
        g_CefDumpFileName[0] = 0;
    }

    // Pulling the server's CEF bundle out to disk is a research task, and this
    // build writes nothing to disk. The refusal lives here, at the only place
    // that could create the file, so no caller can bring it back.
    bool EnsureCefDumpFile(const char* name) noexcept
    {
        (void)name;
        return false;
    }

    void ScheduleCefDumpRetry(ULONGLONG delayMs) noexcept
    {
        g_CefDumpDraining = false;
        g_CefDumpFetchAt = GetTickCount64() + delayMs;
    }

    void QueueCefDumpPull(bool forceReady) noexcept
    {
        const bool queued = QueueScript(
            forceReady
                ? "try{if(global.__uaCefDumpForce)global.__uaCefDumpForce();}"
                  "catch(e){}"
                  "return global.__uaCefDumpPull ? "
                  "global.__uaCefDumpPull() : 'CEF_IDLE:0';"
                : "return global.__uaCefDumpPull ? "
                  "global.__uaCefDumpPull() : 'CEF_IDLE:0';",
            RequestKind::CefDump,
            "UA CEF Dump Pull",
            true);
        if (!queued)
        {
            ScheduleCefDumpRetry(600);
            hooks::HLog("[UkraineTools] CEF dump pull queue full, retry");
        }
    }

    void HandleCefDumpResult(const char* text) noexcept
    {
        if (!text || !text[0])
        {
            ScheduleCefDumpRetry(600);
            return;
        }

        if (strncmp(text, "CEF_BUSY:", 9) == 0)
        {
            ScheduleCefDumpRetry(900);
            _snprintf(
                g_CefDumpStatus,
                sizeof(g_CefDumpStatus) - 1,
                "Capturing %s",
                text + 9);
            g_CefDumpStatus[sizeof(g_CefDumpStatus) - 1] = 0;
            return;
        }

        if (strncmp(text, "CEF_IDLE:", 9) == 0)
        {
            CloseCefDumpFile();
            g_CefDumpDraining = false;
            g_CefDumpActive = false;
            g_CefDumpFetchAt = 0;
            g_CefDumpStartedAt = 0;
            _snprintf(
                g_CefDumpStatus,
                sizeof(g_CefDumpStatus) - 1,
                "Done (%d files)",
                g_CefDumpFilesWritten);
            g_CefDumpStatus[sizeof(g_CefDumpStatus) - 1] = 0;
            char notify[96]{};
            _snprintf(
                notify,
                sizeof(notify) - 1,
                "Captured %d files",
                g_CefDumpFilesWritten);
            notify[sizeof(notify) - 1] = 0;
            Menu::PushNotification(
                "UA CEF Dump",
                notify,
                g_CefDumpFilesWritten > 0,
                6000);
            char line[128]{};
            _snprintf(
                line,
                sizeof(line) - 1,
                "[UkraineTools] CEF dump finished (%d files)",
                g_CefDumpFilesWritten);
            line[sizeof(line) - 1] = 0;
            hooks::HLog(line);
            return;
        }

        if (strncmp(text, "CEF_CHUNK\t", 10) != 0)
        {
            ScheduleCefDumpRetry(600);
            return;
        }

        char copy[RageV8Runtime::kMaxResultLength]{};
        lstrcpynA(copy, text, static_cast<int>(sizeof(copy)));
        char* parts[8]{};
        int partCount = 0;
        char* cursor = copy;
        while (partCount < 8)
        {
            parts[partCount++] = cursor;
            char* tab = strchr(cursor, '\t');
            if (!tab)
                break;
            *tab = 0;
            cursor = tab + 1;
        }
        if (partCount < 7)
        {
            hooks::HLog("[UkraineTools] CEF dump chunk truncated, retry");
            ScheduleCefDumpRetry(500);
            return;
        }

        char fileName[180]{};
        char chunk[RageV8Runtime::kMaxResultLength]{};
        DecodeUriComponent(parts[2], fileName, sizeof(fileName));
        const char* packed = parts[6] ? parts[6] : "";
        int chunkLen = 0;
        if (packed[0] == 'R')
        {
            lstrcpynA(
                chunk,
                packed + 1,
                static_cast<int>(sizeof(chunk)));
            chunkLen = static_cast<int>(strlen(chunk));
        }
        else if (packed[0] == 'E')
        {
            chunkLen = DecodeUriComponent(
                packed + 1, chunk, sizeof(chunk));
        }
        else
        {
            chunkLen = DecodeUriComponent(
                packed, chunk, sizeof(chunk));
        }
        const int complete = ParseInteger(parts[5]);

        if (!EnsureCefDumpFile(fileName))
        {
            hooks::HLog("[UkraineTools] CEF dump create file FAILED");
            ScheduleCefDumpRetry(500);
            return;
        }

        if (chunkLen > 0)
        {
            DWORD written = 0;
            WriteFile(
                g_CefDumpFile,
                chunk,
                static_cast<DWORD>(chunkLen),
                &written,
                nullptr);
        }

        if (complete)
        {
            CloseCefDumpFile();
            ++g_CefDumpFilesWritten;
            _snprintf(
                g_CefDumpStatus,
                sizeof(g_CefDumpStatus) - 1,
                "Writing %s (%d)",
                fileName,
                g_CefDumpFilesWritten);
            g_CefDumpStatus[sizeof(g_CefDumpStatus) - 1] = 0;
        }

        g_CefDumpDraining = false;
        ScheduleCefDumpRetry(complete ? 850ull : 700ull);
    }

    void BeginCefDumpCapture(const char* command) noexcept
    {
        CloseCefDumpFile();
        g_CefDumpDraining = false;
        g_CefDumpActive = true;
        g_CefDumpFilesWritten = 0;
        g_CefDumpStartedAt = GetTickCount64();
        g_CefDumpFetchAt = g_CefDumpStartedAt + 8000;
        lstrcpynA(
            g_CefDumpStatus,
            "Capturing CEF...",
            sizeof(g_CefDumpStatus));
        (void)command;
        hooks::HLog("[UkraineTools] CEF dump started");
    }

    void EnqueueCommand(
        const char* command,
        const char* label = nullptr,
        bool silent = false) noexcept
    {
        if (g_ServerKind != ServerKind::Ukraine)
            return;
        char script[768]{};
        if (!BuildCommandScript(
                command,
                script,
                static_cast<int>(sizeof(script))) ||
            !QueueScript(
                script,
                RequestKind::Command,
                label ? label : command,
                silent))
        {
            if (!silent)
                Menu::PushNotification(
                    "Ukraine Tools",
                    "Queue failed or runtime not ready",
                    false);
            return;
        }

        if (command &&
            (strncmp(command, "dump_cef", 8) == 0))
        {
            BeginCefDumpCapture(command);
        }
    }

    // Server identity comes from ServerProfile, which fingerprints once for
    // every module; this mirrors its answer into the flag used below.
    void SyncServerKind() noexcept
    {
        const ServerProfile::Kind kind = ServerProfile::Current();
        const ServerKind next =
            kind == ServerProfile::Kind::Ukraine
                ? ServerKind::Ukraine
                : (kind == ServerProfile::Kind::Unknown
                       ? ServerKind::Unknown
                       : ServerKind::Other);
        if (g_ServerKind == next)
            return;
        if (next == ServerKind::Ukraine)
        {
            hooks::HLog("[UkraineTools] server: Ukraine GTA");
            // Their nametags and admin icons ride the shared data "redNick".
            lstrcpynA(
                Menu::s_AdminVariableKey,
                "redNick",
                sizeof(Menu::s_AdminVariableKey));
        }
        else if (next == ServerKind::Other)
            hooks::HLog("[UkraineTools] server: other");
        g_ServerKind = next;
        if (next != ServerKind::Ukraine)
        {
            g_Loaded = false;
            g_LoadRequested = false;
        }
    }

    __declspec(noinline) void QueueLoad() noexcept
    {
        if (g_LoadRequested)
            return;
        if (g_ServerKind != ServerKind::Ukraine)
            return;
        g_LastLoadAttempt = GetTickCount64();
        FeatureBundles::Request(FeatureBundles::Id::Ukraine);
        SecureScriptBlob::Plaintext source;
        if (!FeatureBundles::Consume(
                FeatureBundles::Id::Ukraine,
                source))
            return;
        bool queued = false;
        if (source.Data()) {
            queued = QueueScript(
                source.Data(),
                RequestKind::Load,
                "Load Ukraine Tools");
        }
        source.Clear();
        if (!queued)
        {
            Menu::PushNotification(
                "Ukraine Tools",
                "Runtime is not ready or the command queue is full.",
                false);
            return;
        }
        g_LoadRequested = true;
    }

    bool CommandButton(
        const char* label,
        const char* command,
        float width = 140.f)
    {
        if (!gui->button(label, ImVec2(width, 30.f)))
            return false;
        EnqueueCommand(command, label);
        return true;
    }

    void SameLineIfRoom(float nextWidth = 148.f)
    {
        if (ImGui::GetContentRegionAvail().x > nextWidth)
            ImGui::SameLine();
    }

    int SplitTabs(
        char* line,
        char** fields,
        int maxFields) noexcept
    {
        if (!line || !fields || maxFields <= 0)
            return 0;
        int count = 0;
        fields[count++] = line;
        for (char* cursor = line; *cursor && count < maxFields; ++cursor)
        {
            if (*cursor != '\t')
                continue;
            *cursor = 0;
            fields[count++] = cursor + 1;
        }
        return count;
    }

    float ParseFloat(const char* text) noexcept
    {
        if (!text || !text[0])
            return 0.f;
        return static_cast<float>(std::atof(text));
    }

    void ParseStatus(char* text) noexcept
    {
        if (!text || text[0] != 'S' || text[1] != '\t')
            return;

        WoodmanStatus wood{};
        BusStatus bus{};
        HandymanStatus hand{};
        CarrierStatus car{};
        AdminDetStatus adm{};
        char* cursor = text;
        bool headerParsed = false;
        bool busParsed = false;
        bool handParsed = false;
        bool carParsed = false;
        bool admParsed = false;
        while (cursor && *cursor)
        {
            char* next = cursor;
            while (*next && *next != '\n')
                ++next;
            if (*next)
            {
                *next = 0;
                ++next;
            }
            else
            {
                next = nullptr;
            }

            char* fields[36]{};
            const int count = SplitTabs(cursor, fields, 36);
            if (count >= 2 && fields[0][0] == 'S')
            {
                char line[160]{};
                if (count >= 3 && fields[2] && fields[2][0])
                {
                    _snprintf(
                        line,
                        sizeof(line) - 1,
                        "%s · %s",
                        fields[1] ? fields[1] : "ready",
                        fields[2]);
                }
                else
                {
                    lstrcpynA(
                        line,
                        fields[1] ? fields[1] : "ready",
                        sizeof(line));
                }
                lstrcpynA(g_StatusLine, line, sizeof(g_StatusLine));
                headerParsed = true;
            }
            else if (count >= 11 && fields[0][0] == 'W')
            {
                wood.on = ParseInteger(fields[1]) != 0;
                lstrcpynA(
                    wood.phase,
                    fields[2] ? fields[2] : "idle",
                    sizeof(wood.phase));
                wood.hitSlot = ParseInteger(fields[3]);
                wood.hitCount = ParseInteger(fields[4]);
                wood.weight = ParseFloat(fields[5]);
                wood.maxWeight = ParseFloat(fields[6]);
                wood.axeHealth = ParseInteger(fields[7]);
                wood.axeSqlId = ParseInteger(fields[8]);
                wood.woodCount = ParseInteger(fields[9]);
                wood.overweight = ParseInteger(fields[10]) != 0;
                if (count >= 12 && fields[11])
                {
                    lstrcpynA(
                        wood.lastMsg,
                        fields[11],
                        sizeof(wood.lastMsg));
                }
            }
            else if (count >= 14 && fields[0][0] == 'B')
            {
                bus.on = ParseInteger(fields[1]) != 0;
                lstrcpynA(
                    bus.phase,
                    fields[2] ? fields[2] : "idle",
                    sizeof(bus.phase));
                bus.routeId = ParseInteger(fields[3]);
                bus.markersSeenLap = ParseInteger(fields[4]);
                bus.markersPerLap = ParseInteger(fields[5]);
                bus.stopsSeenLap = ParseInteger(fields[6]);
                bus.stopsPerLap = ParseInteger(fields[7]);
                bus.calibrated = ParseInteger(fields[8]) != 0;
                bus.lapIndex = ParseInteger(fields[9]);
                bus.spamCount = ParseInteger(fields[10]);
                bus.spamLeft = ParseInteger(fields[11]);
                bus.earnedSession = ParseInteger(fields[12]);
                bus.currentIsStop = ParseInteger(fields[13]) != 0;
                if (count >= 22 && fields[14])
                {
                    lstrcpynA(
                        bus.style,
                        fields[14],
                        sizeof(bus.style));
                    bus.speedId = ParseInteger(fields[15]);
                    bus.segIndex = ParseInteger(fields[16]);
                    bus.earnedLap = ParseInteger(fields[17]);
                    bus.lastLapEarned = ParseInteger(fields[18]);
                    bus.perHour = ParseInteger(fields[19]);
                    bus.underground = ParseInteger(fields[20]) != 0;
                    if (fields[21])
                        lstrcpynA(
                            bus.stopBursts,
                            fields[21],
                            sizeof(bus.stopBursts));
                    if (count >= 23 && fields[22])
                        lstrcpynA(
                            bus.lastMsg,
                            fields[22],
                            sizeof(bus.lastMsg));
                }
                else if (count >= 15 && fields[14])
                {
                    lstrcpynA(
                        bus.lastMsg,
                        fields[14],
                        sizeof(bus.lastMsg));
                }
                busParsed = true;
            }
            else if (count >= 10 && fields[0][0] == 'H')
            {
                hand.on = ParseInteger(fields[1]) != 0;
                lstrcpynA(
                    hand.phase,
                    fields[2] ? fields[2] : "idle",
                    sizeof(hand.phase));
                lstrcpynA(
                    hand.step,
                    fields[3] ? fields[3] : "-",
                    sizeof(hand.step));
                hand.qi = ParseInteger(fields[4]);
                hand.queueLen = ParseInteger(fields[5]);
                hand.skimmerHp = ParseInteger(fields[6]);
                hand.towelHp = ParseInteger(fields[7]);
                hand.chlorineQty = ParseInteger(fields[8]);
                hand.chlorineNeed = ParseInteger(fields[9]);
                if (count >= 12 && fields[10])
                {
                    lstrcpynA(
                        hand.summary,
                        fields[10],
                        sizeof(hand.summary));
                    if (fields[11])
                        lstrcpynA(
                            hand.lastMsg,
                            fields[11],
                            sizeof(hand.lastMsg));
                }
                else if (count >= 11 && fields[10])
                {
                    lstrcpynA(
                        hand.lastMsg,
                        fields[10],
                        sizeof(hand.lastMsg));
                }
                handParsed = true;
            }
            else if (count >= 11 && fields[0][0] == 'C')
            {
                car.on = ParseInteger(fields[1]) != 0;
                lstrcpynA(
                    car.phase,
                    fields[2] ? fields[2] : "idle",
                    sizeof(car.phase));
                car.orderId = ParseInteger(fields[3]);
                car.orderWeight = ParseInteger(fields[4]);
                car.orderPrice = ParseInteger(fields[5]);
                car.knownVehWeight = ParseInteger(fields[6]);
                car.maxWeight = ParseInteger(fields[7]);
                car.routeIndex = ParseInteger(fields[8]);
                car.routeLen = ParseInteger(fields[9]);
                car.vehRemoteId = ParseInteger(fields[10]);
                if (count >= 12)
                    car.parkPosIndex = ParseInteger(fields[11]);
                if (count >= 13 && fields[12])
                {
                    lstrcpynA(
                        car.lastMsg,
                        fields[12],
                        sizeof(car.lastMsg));
                }
                if (count >= 21)
                {
                    car.avoidPlayers =
                        ParseInteger(fields[13]) != 0;
                    car.clearDistance = ParseFloat(fields[14]);
                    car.pauseDistance = ParseFloat(fields[15]);
                    car.maxUnderOffset = ParseFloat(fields[16]);
                    car.nearestPlayerId = ParseInteger(fields[17]);
                    car.nearestPlayerDistance = ParseFloat(fields[18]);
                    if (fields[19])
                    {
                        lstrcpynA(
                            car.nearestPlayerName,
                            fields[19],
                            sizeof(car.nearestPlayerName));
                    }
                    car.waitingClear =
                        ParseInteger(fields[20]) != 0;
                }
                if (count >= 28)
                {
                    car.autoMode =
                        ParseInteger(fields[21]) != 0;
                    if (fields[22])
                    {
                        lstrcpynA(
                            car.prerequisite,
                            fields[22],
                            sizeof(car.prerequisite));
                    }
                    car.phaseAge = ParseInteger(fields[23]);
                    car.retries = ParseInteger(fields[24]);
                    if (fields[25])
                    {
                        lstrcpynA(
                            car.capacitySource,
                            fields[25],
                            sizeof(car.capacitySource));
                    }
                    if (fields[26])
                    {
                        lstrcpynA(
                            car.lastServerAnswer,
                            fields[26],
                            sizeof(car.lastServerAnswer));
                    }
                    if (fields[27])
                    {
                        lstrcpynA(
                            car.deliveryAck,
                            fields[27],
                            sizeof(car.deliveryAck));
                    }
                }
                carParsed = true;
            }
            else if (count >= 5 && fields[0][0] == 'A')
            {
                adm.on = ParseInteger(fields[1]) != 0;
                if (count >= 9)
                {
                    adm.onlineCount = ParseInteger(fields[2]);
                    adm.nearbyCount = ParseInteger(fields[3]);
                    adm.nearestId = ParseInteger(fields[4]);
                    adm.nearestDist = ParseInteger(fields[5]);
                    if (fields[6])
                        lstrcpynA(
                            adm.nearestName,
                            fields[6],
                            sizeof(adm.nearestName));
                    adm.nearestLvl = ParseInteger(fields[7]);
                    adm.spectating = ParseInteger(fields[8]);
                }
                else
                {
                    adm.onlineCount = ParseInteger(fields[2]);
                    adm.nearbyCount = 0;
                    adm.nearestId = ParseInteger(fields[3]);
                    adm.nearestDist = ParseInteger(fields[4]);
                    if (count >= 6 && fields[5])
                        lstrcpynA(
                            adm.nearestName,
                            fields[5],
                            sizeof(adm.nearestName));
                }
                admParsed = true;
            }
            else if (count >= 4 && fields[0][0] == 'O' &&
                adm.onlineListed <
                    static_cast<int>(
                        sizeof(adm.online) / sizeof(adm.online[0])))
            {
                OnlineAdminInfo& entry =
                    adm.online[adm.onlineListed++];
                entry.id = ParseInteger(fields[1]);
                lstrcpynA(
                    entry.name,
                    fields[2] ? fields[2] : "",
                    sizeof(entry.name));
                entry.level = ParseInteger(fields[3]);
                admParsed = true;
            }
            else if (count >= 5 && fields[0][0] == 'N' &&
                adm.nearbyListed <
                    static_cast<int>(
                        sizeof(adm.nearby) / sizeof(adm.nearby[0])))
            {
                NearbyAdminInfo& entry =
                    adm.nearby[adm.nearbyListed++];
                entry.id = ParseInteger(fields[1]);
                lstrcpynA(
                    entry.name,
                    fields[2] ? fields[2] : "",
                    sizeof(entry.name));
                entry.level = ParseInteger(fields[3]);
                entry.dist = ParseInteger(fields[4]);
                entry.invis =
                    count >= 6 && ParseInteger(fields[5]) != 0;
                entry.spectating =
                    count >= 7 && ParseInteger(fields[6]) != 0;
                admParsed = true;
            }

            cursor = next;
        }

        if (headerParsed)
            g_Woodman = wood;
        if (busParsed)
            g_Bus = bus;
        if (handParsed)
            g_Handyman = hand;
        if (carParsed)
        {
            g_Carrier = car;
            if (g_CarrierAutoReapplyPending)
            {
                if (car.autoMode ==
                    g_CarrierAutoDesired)
                {
                    g_CarrierAutoReapplyPending =
                        false;
                    g_CarrierAutoReapplyReady =
                        false;
                    g_CarrierAutoCommandPending =
                        false;
                }
                else if (g_CarrierAutoDesired)
                {
                    g_CarrierAutoReapplyReady =
                        true;
                }
                else
                {
                    g_CarrierAutoReapplyPending =
                        false;
                }
            }
            else if (g_CarrierAutoCommandPending)
            {
                if (car.autoMode ==
                    g_CarrierAutoDesired)
                    g_CarrierAutoCommandPending =
                        false;
            }
            else
            {
                g_CarrierAutoDesired =
                    car.autoMode;
            }
            if (!g_CarrierSafetyUiInitialized)
            {
                g_CarrierAvoidPlayers = car.avoidPlayers;
                g_CarrierClearDistance = car.clearDistance;
                g_CarrierPauseDistance = car.pauseDistance;
                g_CarrierMaxUnderOffset = car.maxUnderOffset;
                g_CarrierSafetyUiInitialized = true;
            }
        }
        if (admParsed)
        {
            if (adm.onlineListed > 0)
                adm.onlineCount = adm.onlineListed;
            if (adm.nearbyListed > 0)
                adm.nearbyCount = adm.nearbyListed;
            g_AdminDet = adm;
        }
    }

    constexpr float kWidgetRounding = 14.f;
    constexpr float kWidgetHeaderH = 40.f;
    constexpr float kWidgetIconRounding = 8.f;
    constexpr float kWidgetRowRounding = 10.f;

    float WidgetAppearAlpha(ImGuiID id)
    {
        float& alpha =
            LEYAWIN_ANIM(ImGuiID, float).get(id);
        alpha = ImLerp(
            alpha,
            1.f,
            gui->fixed_speed(10.f));
        return ImClamp(alpha, 0.f, 1.f);
    }

    float DrawStatusPill(
        const char* label,
        const ImVec4& color,
        bool pulse)
    {
        ImFont* font =
            set->poppins_widget
                ? set->poppins_widget
                : ImGui::GetFont();
        const float fontSize =
            font ? font->FontSize : ImGui::GetFontSize();
        const ImVec2 textSize =
            font->CalcTextSizeA(
                fontSize,
                FLT_MAX,
                0.f,
                label);
        const float padX = 10.f;
        const float padY = 4.f;
        const float h = textSize.y + padY * 2.f;
        const float w = textSize.x + padX * 2.f;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();

        float glow = 1.f;
        if (pulse)
        {
            glow =
                0.62f +
                0.38f *
                    (0.5f +
                     0.5f *
                         sinf(static_cast<float>(
                                  ImGui::GetTime()) *
                              5.f));
        }

        draw->AddRectFilled(
            pos,
            pos + ImVec2(w, h),
            gui->get_clr(color, 0.16f * glow),
            h * 0.5f);
        draw->AddRect(
            pos,
            pos + ImVec2(w, h),
            gui->get_clr(color, 0.55f * glow),
            h * 0.5f);
        gui->render_text_clipped(
            font,
            pos,
            pos + ImVec2(w, h),
            gui->get_clr(color, 0.85f + 0.15f * glow),
            label,
            {0.5f, 0.5f});
        ImGui::Dummy(ImVec2(w, h));
        return w;
    }

    void DrawMetricChip(
        const char* id,
        const char* label,
        const char* value,
        const ImVec2& size)
    {
        ImGui::PushID(id);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImGui::InvisibleButton("##chip", size);
        const bool hovered = ImGui::IsItemHovered();
        float& hot =
            LEYAWIN_ANIM(ImGuiID, float)
                .get(ImGui::GetItemID());
        hot = ImLerp(
            hot,
            hovered ? 1.f : 0.f,
            gui->fixed_speed(14.f));

        draw->AddRectFilled(
            pos,
            pos + size,
            gui->get_clr(
                clr->child_bottom_bar,
                0.92f + 0.08f * hot),
            kWidgetRowRounding);
        draw->AddRect(
            pos,
            pos + size,
            gui->get_clr(
                hot > 0.01f
                    ? clr->accent_color
                    : clr->child_stroke_bar,
                0.35f + 0.45f * hot),
            kWidgetRowRounding);
        draw->AddRectFilled(
            ImVec2(pos.x, pos.y + 8.f),
            ImVec2(pos.x + 3.f, pos.y + size.y - 8.f),
            gui->get_clr(clr->accent_color, 0.55f + 0.45f * hot),
            2.f);

        ImFont* labelFont =
            set->poppins_widget
                ? set->poppins_widget
                : ImGui::GetFont();
        ImFont* valueFont =
            set->poppins_selection
                ? set->poppins_selection
                : labelFont;
        gui->render_text_clipped(
            labelFont,
            pos + ImVec2(12.f, 6.f),
            pos + ImVec2(size.x - 8.f, size.y * 0.5f),
            gui->get_clr(clr->text, 0.75f),
            label,
            {0.f, 0.5f});
        gui->render_text_clipped(
            valueFont,
            pos + ImVec2(12.f, size.y * 0.42f),
            pos + ImVec2(size.x - 8.f, size.y - 4.f),
            gui->get_clr(clr->text_active),
            value,
            {0.f, 0.5f});
        ImGui::PopID();
    }

    void DrawInsetPanel(
        const char* text,
        float height)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width =
            ImGui::GetContentRegionAvail().x;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(
            pos,
            pos + ImVec2(width, height),
            gui->get_clr(clr->combo_color, 0.85f),
            kWidgetRowRounding);
        draw->AddRect(
            pos,
            pos + ImVec2(width, height),
            gui->get_clr(clr->child_stroke_bar, 0.65f),
            kWidgetRowRounding);
        ImFont* font =
            set->poppins_widget
                ? set->poppins_widget
                : ImGui::GetFont();
        gui->render_text_clipped(
            font,
            pos + ImVec2(10.f, 6.f),
            pos + ImVec2(width - 10.f, height - 6.f),
            gui->get_clr(clr->text_active, 0.9f),
            text ? text : "",
            {0.f, 0.f});
        ImGui::Dummy(ImVec2(width, height));
    }

    void PushWidgetStyle()
    {
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(16.f, 14.f));
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowRounding,
            kWidgetRounding);
        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowBorderSize,
            1.f);
        ImGui::PushStyleColor(
            ImGuiCol_WindowBg,
            clr->window_background);
        ImGui::PushStyleColor(
            ImGuiCol_Border,
            clr->child_stroke_bar);
        ImGui::PushStyleColor(
            ImGuiCol_TitleBg,
            clr->child_top_bar);
        ImGui::PushStyleColor(
            ImGuiCol_TitleBgActive,
            clr->child_top_bar);
    }

    void PopWidgetStyle()
    {
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(3);
    }

    bool WidgetIconButton(
        const char* id,
        const char* icon,
        const ImVec2& position)
    {
        ImGui::SetCursorScreenPos(position);
        ImGui::PushID(id);
        const ImVec2 size(28.f, 28.f);
        const bool pressed =
            ImGui::InvisibleButton(
                "##widget_icon",
                size);
        const bool hovered =
            ImGui::IsItemHovered();
        float& hot =
            LEYAWIN_ANIM(ImGuiID, float)
                .get(ImGui::GetItemID());
        hot = ImLerp(
            hot,
            hovered ? 1.f : 0.f,
            gui->fixed_speed(14.f));

        ImDrawList* draw =
            ImGui::GetWindowDrawList();
        const ImVec4 fill = ImLerp(
            clr->combo_color,
            clr->element_background,
            hot);
        const ImVec4 border = ImLerp(
            clr->child_stroke_bar,
            clr->accent_color,
            hot);
        const ImVec4 iconColor = ImLerp(
            clr->text,
            clr->text_active,
            hot);
        draw->AddRectFilled(
            position,
            position + size,
            gui->get_clr(fill),
            kWidgetIconRounding);
        draw->AddRect(
            position,
            position + size,
            gui->get_clr(border, 0.55f + 0.45f * hot),
            kWidgetIconRounding);
        gui->render_text_clipped(
            set->icon,
            position,
            position + size,
            gui->get_clr(iconColor),
            icon,
            {0.5f, 0.5f});
        ImGui::PopID();
        return pressed;
    }

    void RenderWidgetHeader(
        const char* title,
        bool& visible,
        bool& pinned)
    {
        const ImVec2 position =
            ImGui::GetWindowPos();
        const ImVec2 size =
            ImGui::GetWindowSize();
        ImDrawList* draw =
            ImGui::GetWindowDrawList();
        draw->AddRectFilled(
            position,
            ImVec2(
                position.x + size.x,
                position.y + kWidgetHeaderH),
            gui->get_clr(clr->child_top_bar),
            kWidgetRounding,
            ImDrawFlags_RoundCornersTop);
        draw->AddLine(
            ImVec2(
                position.x,
                position.y + kWidgetHeaderH),
            ImVec2(
                position.x + size.x,
                position.y + kWidgetHeaderH),
            gui->get_clr(clr->child_stroke_bar, 0.85f));

        const float pulse =
            0.55f +
            0.45f *
                (0.5f +
                 0.5f *
                     sinf(static_cast<float>(
                              ImGui::GetTime()) *
                          3.2f));
        draw->AddRectFilled(
            ImVec2(position.x + 2.f, position.y + 10.f),
            ImVec2(position.x + 5.f, position.y + 30.f),
            gui->get_clr(clr->accent_color, pulse),
            2.f);
        gui->render_text_clipped(
            set->poppins_selection,
            position + ImVec2(14.f, 0.f),
            ImVec2(
                position.x + size.x - 78.f,
                position.y + kWidgetHeaderH),
            gui->get_clr(clr->text_active),
            title,
            {0.f, 0.5f});

        if (Menu::IsVisible())
        {
            ImGui::SetCursorScreenPos(position);
            ImGui::InvisibleButton(
                "##widget_drag",
                ImVec2(size.x - 78.f, kWidgetHeaderH));
            if (!pinned && ImGui::IsItemActive() &&
                ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                ImGui::SetWindowPos(
                    position +
                        ImGui::GetIO().MouseDelta,
                    ImGuiCond_Always);
            }
            if (WidgetIconButton(
                    "pin",
                    pinned
                        ? ICON_FA_LOCK
                        : ICON_FA_THUMBTACK,
                    ImVec2(
                        position.x + size.x - 66.f,
                        position.y + 6.f)))
            {
                pinned = !pinned;
            }
            if (WidgetIconButton(
                    "close",
                    ICON_FA_XMARK,
                    ImVec2(
                        position.x + size.x - 34.f,
                        position.y + 6.f)))
            {
                visible = false;
            }
        }
        ImGui::SetCursorScreenPos(
            position + ImVec2(16.f, kWidgetHeaderH + 12.f));
    }

    ImVec4 AdminLevelColor(int level)
    {
        if (level >= 8)
            return ImVec4(0.95f, 0.25f, 0.3f, 1.f);
        if (level >= 5)
            return ImVec4(0.95f, 0.55f, 0.2f, 1.f);
        if (level >= 3)
            return ImVec4(0.95f, 0.85f, 0.25f, 1.f);
        return ImVec4(0.45f, 0.78f, 0.95f, 1.f);
    }

    void SyncBusUiFromStatus()
    {
        if (strcmp(g_Bus.style, "per_point") == 0)
            g_BusStyleIdx = 0;
        else if (strcmp(g_Bus.style, "remain_window") == 0)
            g_BusStyleIdx = 2;
        else
            g_BusStyleIdx = 1;
        g_BusSpeedIdx = g_Bus.speedId;
        if (g_BusSpeedIdx < 0)
            g_BusSpeedIdx = 0;
        if (g_BusSpeedIdx > 3)
            g_BusSpeedIdx = 3;
    }

    void RenderWoodmanJobsBody()
    {
        const ImVec4 activeColor =
            g_Woodman.on
                ? ImVec4(0.3f, 0.9f, 0.45f, 1.f)
                : ImVec4(0.72f, 0.72f, 0.78f, 1.f);
        DrawStatusPill(
            g_Woodman.on ? "ACTIVE" : "IDLE",
            activeColor,
            g_Woodman.on);
        ImGui::SameLine(0.f, 8.f);
        DrawStatusPill(
            "WOOD",
            ImVec4(0.45f, 0.78f, 0.55f, 1.f),
            g_Woodman.on);
        ImGui::SameLine(0.f, 8.f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.f);
        ImGui::TextDisabled(
            "phase %s",
            g_Woodman.phase[0] ? g_Woodman.phase : "idle");

        ImGui::Spacing();
        char statusText[128]{};
        if (g_Woodman.lastMsg[0])
            lstrcpynA(statusText, g_Woodman.lastMsg, sizeof(statusText));
        else if (g_Woodman.on &&
                 strcmp(g_Woodman.phase, "wait_capture") == 0)
            lstrcpynA(
                statusText,
                "Chop one log manually (logs.add)",
                sizeof(statusText));
        else if (g_Woodman.on)
            _snprintf(
                statusText,
                sizeof(statusText) - 1,
                "Running · %s",
                g_Woodman.phase);
        else
            lstrcpynA(
                statusText,
                "Start bot, then chop once",
                sizeof(statusText));
        DrawInsetPanel(statusText, 44.f);
        ImGui::Spacing();

        const float gap = 8.f;
        const float chipW =
            (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
        const ImVec2 chipSize(chipW, 52.f);
        char aBuf[32]{};
        char bBuf[32]{};
        char cBuf[32]{};
        char dBuf[32]{};

        if (g_Woodman.weight >= 0.f && g_Woodman.maxWeight > 0.f)
            _snprintf(
                aBuf,
                sizeof(aBuf) - 1,
                "%.1f / %.0f",
                g_Woodman.weight,
                g_Woodman.maxWeight);
        else
            lstrcpynA(aBuf, "—", sizeof(aBuf));

        if (g_Woodman.axeHealth >= 0)
            _snprintf(
                bBuf, sizeof(bBuf) - 1, "%d%%", g_Woodman.axeHealth);
        else
            lstrcpynA(bBuf, "—", sizeof(bBuf));

        if (g_Woodman.woodCount >= 0)
            _snprintf(
                cBuf, sizeof(cBuf) - 1, "%d", g_Woodman.woodCount);
        else
            lstrcpynA(cBuf, "—", sizeof(cBuf));

        if (g_Woodman.on)
            _snprintf(
                dBuf,
                sizeof(dBuf) - 1,
                "%d × %d",
                g_Woodman.hitSlot,
                g_Woodman.hitCount);
        else
            lstrcpynA(dBuf, "—", sizeof(dBuf));

        const ImVec2 row0 = ImGui::GetCursorScreenPos();
        DrawMetricChip("weight", "Weight", aBuf, chipSize);
        ImGui::SetCursorScreenPos(row0 + ImVec2(chipW + gap, 0.f));
        DrawMetricChip("axe", "Axe HP", bBuf, chipSize);
        ImGui::SetCursorScreenPos(row0 + ImVec2(0.f, chipSize.y + gap));
        const ImVec2 row1 = ImGui::GetCursorScreenPos();
        DrawMetricChip("wood", "Wood", cBuf, chipSize);
        ImGui::SetCursorScreenPos(row1 + ImVec2(chipW + gap, 0.f));
        DrawMetricChip("hit", "Slot / Hit", dBuf, chipSize);
        ImGui::SetCursorScreenPos(row1 + ImVec2(0.f, chipSize.y + gap));

        if (g_Woodman.overweight)
            ImGui::TextColored(
                ImVec4(0.95f, 0.55f, 0.25f, 1.f),
                "Overweight — selling…");
        else if (g_Woodman.axeSqlId > 0)
            ImGui::TextDisabled("axe sqlId %d", g_Woodman.axeSqlId);
        else
            ImGui::TextDisabled("probe inv after Start");

        if (Menu::IsVisible())
        {
            ImGui::Spacing();
            if (gui->button(
                    g_Woodman.on ? "Stop" : "Start",
                    ImVec2(100.f, 30.f)))
            {
                EnqueueCommand(
                    g_Woodman.on
                        ? "woodman_bot_stop"
                        : "woodman_bot_start",
                    g_Woodman.on ? "Woodman Stop" : "Woodman Start");
                if (!g_Woodman.on)
                    g_Settings.showJobsWidget = true;
            }
        }
    }

    void RenderBusJobsBody()
    {
        const ImVec4 activeColor =
            g_Bus.on
                ? ImVec4(0.3f, 0.9f, 0.45f, 1.f)
                : ImVec4(0.72f, 0.72f, 0.78f, 1.f);
        DrawStatusPill(
            g_Bus.on ? "ACTIVE" : "IDLE", activeColor, g_Bus.on);
        ImGui::SameLine(0.f, 6.f);
        DrawStatusPill(
            "OKRUG", ImVec4(0.45f, 0.7f, 0.95f, 1.f), false);
        ImGui::SameLine(0.f, 6.f);
        if (g_Bus.underground)
        {
            DrawStatusPill(
                "UNDER",
                ImVec4(0.55f, 0.45f, 0.95f, 1.f),
                true);
            ImGui::SameLine(0.f, 6.f);
        }
        char stylePill[24]{};
        if (strcmp(g_Bus.style, "per_point") == 0)
            lstrcpynA(stylePill, "PER_PT", sizeof(stylePill));
        else if (strcmp(g_Bus.style, "remain_window") == 0)
            lstrcpynA(stylePill, "REMAIN", sizeof(stylePill));
        else
            lstrcpynA(stylePill, "SEG_SKIP", sizeof(stylePill));
        DrawStatusPill(
            stylePill, ImVec4(0.95f, 0.7f, 0.35f, 1.f), g_Bus.on);
        ImGui::SameLine(0.f, 6.f);
        static const char* kSpd[] = {
            "SLOW", "NORM", "FAST", "TURBO"
        };
        const int spd =
            (g_Bus.speedId >= 0 && g_Bus.speedId <= 3)
                ? g_Bus.speedId
                : 1;
        DrawStatusPill(kSpd[spd], ImVec4(0.65f, 0.85f, 0.45f, 1.f), false);

        ImGui::Spacing();
        char statusText[160]{};
        if (g_Bus.lastMsg[0])
            lstrcpynA(statusText, g_Bus.lastMsg, sizeof(statusText));
        else
            lstrcpynA(
                statusText,
                "Okruzhnyi · underground segment farm",
                sizeof(statusText));
        DrawInsetPanel(statusText, 44.f);
        ImGui::Spacing();

        const float gap = 8.f;
        const float chipW =
            (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
        const ImVec2 chipSize(chipW, 48.f);
        char aBuf[32]{};
        char bBuf[32]{};
        char cBuf[32]{};
        char dBuf[32]{};
        _snprintf(
            aBuf,
            sizeof(aBuf) - 1,
            "%d / %d",
            g_Bus.markersSeenLap,
            g_Bus.markersPerLap > 0 ? g_Bus.markersPerLap : 53);
        _snprintf(
            bBuf,
            sizeof(bBuf) - 1,
            "%d / %d",
            g_Bus.stopsSeenLap,
            g_Bus.stopsPerLap > 0 ? g_Bus.stopsPerLap : 4);
        _snprintf(cBuf, sizeof(cBuf) - 1, "%d", g_Bus.spamLeft);
        _snprintf(dBuf, sizeof(dBuf) - 1, "$%d", g_Bus.earnedSession);

        const ImVec2 row0 = ImGui::GetCursorScreenPos();
        DrawMetricChip("pts", "Points", aBuf, chipSize);
        ImGui::SetCursorScreenPos(row0 + ImVec2(chipW + gap, 0.f));
        DrawMetricChip("stops", "Stops", bBuf, chipSize);
        ImGui::SetCursorScreenPos(row0 + ImVec2(0.f, chipSize.y + gap));
        const ImVec2 row1 = ImGui::GetCursorScreenPos();
        DrawMetricChip("burst", "Burst left", cBuf, chipSize);
        ImGui::SetCursorScreenPos(row1 + ImVec2(chipW + gap, 0.f));
        DrawMetricChip("earn", "Earned", dBuf, chipSize);
        ImGui::SetCursorScreenPos(row1 + ImVec2(0.f, chipSize.y + gap));

        ImGui::TextDisabled(
            "lap #%d · stop %d/%d · lap $%d · last $%d · $%d/hr",
            g_Bus.lapIndex,
            g_Bus.stopsPerLap > 0
                ? (g_Bus.segIndex % g_Bus.stopsPerLap) + 1
                : 1,
            g_Bus.stopsPerLap > 0 ? g_Bus.stopsPerLap : 3,
            g_Bus.earnedLap,
            g_Bus.lastLapEarned,
            g_Bus.perHour);

        if (Menu::IsVisible())
        {
            ImGui::Spacing();
            if (gui->button(
                    g_Bus.on ? "Stop" : "Start", ImVec2(100.f, 30.f)))
            {
                EnqueueCommand(
                    g_Bus.on ? "bus_bot_stop" : "bus_bot_start",
                    g_Bus.on ? "Bus Bot Stop" : "Bus Bot Start");
                if (!g_Bus.on)
                    g_Settings.showJobsWidget = true;
            }
        }
    }

    void RenderHandymanJobsBody()
    {
        const ImVec4 activeColor =
            g_Handyman.on
                ? ImVec4(0.3f, 0.9f, 0.45f, 1.f)
                : ImVec4(0.72f, 0.72f, 0.78f, 1.f);
        DrawStatusPill(
            g_Handyman.on ? "ACTIVE" : "IDLE",
            activeColor,
            g_Handyman.on);
        ImGui::SameLine(0.f, 8.f);
        DrawStatusPill(
            "HANDY",
            ImVec4(0.55f, 0.75f, 0.95f, 1.f),
            g_Handyman.on);
        ImGui::SameLine(0.f, 8.f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.f);
        ImGui::TextDisabled(
            "phase %s · %s · %s",
            g_Handyman.phase[0] ? g_Handyman.phase : "idle",
            g_Handyman.step[0] ? g_Handyman.step : "-",
            g_Handyman.summary[0] ? g_Handyman.summary : "-");

        ImGui::Spacing();
        char statusText[160]{};
        if (g_Handyman.lastMsg[0])
            lstrcpynA(
                statusText, g_Handyman.lastMsg, sizeof(statusText));
        else if (g_Handyman.on)
            lstrcpynA(
                statusText,
                "waiting markers (work.start / blips)…",
                sizeof(statusText));
        else
            lstrcpynA(
                statusText,
                "Start in Jobs tab (calls work.start)",
                sizeof(statusText));
        DrawInsetPanel(statusText, 44.f);
        ImGui::Spacing();

        const float gap = 8.f;
        const float chipW =
            (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
        const ImVec2 chipSize(chipW, 48.f);
        char aBuf[32]{};
        char bBuf[32]{};
        char cBuf[32]{};
        char dBuf[32]{};
        _snprintf(
            aBuf,
            sizeof(aBuf) - 1,
            "%d / %d",
            g_Handyman.qi,
            g_Handyman.queueLen);
        if (g_Handyman.skimmerHp >= 0)
            _snprintf(
                bBuf, sizeof(bBuf) - 1, "%d", g_Handyman.skimmerHp);
        else
            lstrcpynA(bBuf, "—", sizeof(bBuf));
        if (g_Handyman.towelHp >= 0)
            _snprintf(
                cBuf, sizeof(cBuf) - 1, "%d", g_Handyman.towelHp);
        else
            lstrcpynA(cBuf, "—", sizeof(cBuf));
        _snprintf(
            dBuf,
            sizeof(dBuf) - 1,
            "%d / %d",
            g_Handyman.chlorineQty,
            g_Handyman.chlorineNeed);

        const ImVec2 row0 = ImGui::GetCursorScreenPos();
        DrawMetricChip("q", "Step", aBuf, chipSize);
        ImGui::SetCursorScreenPos(row0 + ImVec2(chipW + gap, 0.f));
        DrawMetricChip("sk", "Skimmer HP", bBuf, chipSize);
        ImGui::SetCursorScreenPos(row0 + ImVec2(0.f, chipSize.y + gap));
        const ImVec2 row1 = ImGui::GetCursorScreenPos();
        DrawMetricChip("tw", "Towel HP", cBuf, chipSize);
        ImGui::SetCursorScreenPos(row1 + ImVec2(chipW + gap, 0.f));
        DrawMetricChip("cl", "Chlorine", dBuf, chipSize);
        ImGui::SetCursorScreenPos(row1 + ImVec2(0.f, chipSize.y + gap));

        ImGui::TextDisabled(
            "244 Cl · 245 skimmer · 246 towel · settings in Jobs tab");

        if (Menu::IsVisible())
        {
            ImGui::Spacing();
            if (gui->button(
                    g_Handyman.on ? "Stop" : "Start",
                    ImVec2(100.f, 30.f)))
            {
                EnqueueCommand(
                    g_Handyman.on
                        ? "handyman_bot_stop"
                        : "handyman_bot_start",
                    g_Handyman.on
                        ? "Handyman Stop"
                        : "Handyman Start");
                if (!g_Handyman.on)
                    g_Settings.showJobsWidget = true;
            }
        }
    }

    void RenderCarrierJobsBody()
    {
        const bool enabled =
            g_CarrierAutoDesired ||
            g_Carrier.autoMode ||
            g_Carrier.on;
        DrawStatusPill(
            enabled ? "AUTO" : "IDLE",
            enabled
                ? ImVec4(0.3f, 0.9f, 0.45f, 1.f)
                : ImVec4(0.72f, 0.72f, 0.78f, 1.f),
            enabled);
        ImGui::SameLine(0.f, 8.f);
        DrawStatusPill(
            "CARRIER",
            ImVec4(0.55f, 0.75f, 0.95f, 1.f),
            enabled);
        ImGui::SameLine(0.f, 8.f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.f);
        ImGui::TextDisabled(
            "%s · %ds · %s",
            g_Carrier.phase[0] ? g_Carrier.phase : "idle",
            g_Carrier.phaseAge,
            g_Carrier.prerequisite[0]
                ? g_Carrier.prerequisite : "off");

        ImGui::Spacing();
        DrawInsetPanel(
            g_Carrier.lastMsg[0]
                ? g_Carrier.lastMsg
                : "Enable Auto Carrier in the Carrier tab",
            44.f);
        ImGui::Spacing();

        const float gap = 8.f;
        const float chipW =
            (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
        const ImVec2 chipSize(chipW, 48.f);
        char order[32]{};
        char cargo[32]{};
        char route[32]{};
        char capacity[48]{};
        _snprintf(
            order,
            sizeof(order) - 1,
            "#%d",
            g_Carrier.orderId);
        _snprintf(
            cargo,
            sizeof(cargo) - 1,
            "%dT · $%d",
            g_Carrier.orderWeight,
            g_Carrier.orderPrice);
        _snprintf(
            route,
            sizeof(route) - 1,
            "%d / %d",
            g_Carrier.routeIndex,
            g_Carrier.routeLen);
        const int cap =
            g_Carrier.knownVehWeight > 0
                ? g_Carrier.knownVehWeight
                : g_Carrier.maxWeight;
        if (cap > 0)
        {
            _snprintf(
                capacity,
                sizeof(capacity) - 1,
                "%s · %dT",
                g_Carrier.capacitySource[0]
                    ? g_Carrier.capacitySource : "unknown",
                cap);
        }
        else
        {
            _snprintf(
                capacity,
                sizeof(capacity) - 1,
                "%s · auto",
                g_Carrier.capacitySource[0]
                    ? g_Carrier.capacitySource : "unknown");
        }

        const ImVec2 row0 = ImGui::GetCursorScreenPos();
        DrawMetricChip("co", "Order", order, chipSize);
        ImGui::SetCursorScreenPos(row0 + ImVec2(chipW + gap, 0.f));
        DrawMetricChip("cw", "Cargo", cargo, chipSize);
        ImGui::SetCursorScreenPos(row0 + ImVec2(0.f, chipSize.y + gap));
        const ImVec2 row1 = ImGui::GetCursorScreenPos();
        DrawMetricChip("cr", "Route", route, chipSize);
        ImGui::SetCursorScreenPos(row1 + ImVec2(chipW + gap, 0.f));
        DrawMetricChip("cc", "Capacity", capacity, chipSize);
        ImGui::SetCursorScreenPos(row1 + ImVec2(0.f, chipSize.y + gap));

        ImGui::TextDisabled(
            "ack %s · answer %s · retries %d",
            g_Carrier.deliveryAck[0]
                ? g_Carrier.deliveryAck : "idle",
            g_Carrier.lastServerAnswer[0]
                ? g_Carrier.lastServerAnswer : "-",
            g_Carrier.retries);

        if (Menu::IsVisible())
        {
            ImGui::Spacing();
            if (gui->button(
                    enabled ? "Stop" : "Start",
                    ImVec2(100.f, 30.f)))
            {
                g_CarrierAutoDesired = !enabled;
                g_CarrierAutoCommandPending = true;
                EnqueueCommand(
                    enabled
                        ? "carrier_auto_toggle:0"
                        : "carrier_auto_toggle:1",
                    enabled
                        ? "Auto Carrier Stop"
                        : "Auto Carrier Start");
                if (!enabled)
                    g_Settings.showJobsWidget = true;
            }
        }
    }

    void RenderJobsIdleBody()
    {
        DrawStatusPill(
            "IDLE", ImVec4(0.72f, 0.72f, 0.78f, 1.f), false);
        ImGui::Spacing();
        DrawInsetPanel(
            "No job bot · start Woodman / Bus / Handyman / Carrier",
            44.f);
    }

    void RenderJobsWidget()
    {
        if (!g_Settings.showJobsWidget)
            return;

        const bool carrierOn =
            g_CarrierAutoDesired ||
            g_Carrier.autoMode ||
            g_Carrier.on;
        const bool handyOn = g_Handyman.on;
        const bool busOn = g_Bus.on;
        const bool woodOn = g_Woodman.on;
        const char* sub =
            carrierOn ? "Carrier"
                : handyOn ? "Handyman"
                : busOn ? "Bus"
                : woodOn ? "Woodman"
                : "Idle";
        char title[48]{};
        _snprintf(title, sizeof(title) - 1, "Jobs · %s", sub);

        const float winH =
            carrierOn
                ? 320.f
                : (busOn
                    ? 300.f
                    : (handyOn
                        ? 300.f
                        : (woodOn ? 300.f : 160.f)));

        ImGui::SetNextWindowPos(
            ImVec2(18.f, 110.f),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(
            ImVec2(360.f, winH),
            ImGuiCond_Always);
        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar;
        if (g_Settings.pinJobsWidget)
            flags |= ImGuiWindowFlags_NoMove;
        if (!Menu::IsVisible())
            flags |= ImGuiWindowFlags_NoInputs;

        const float appear = WidgetAppearAlpha(0xA01C001u);
        ImGui::SetNextWindowBgAlpha(0.92f * appear);
        PushWidgetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, appear);
        if (ImGui::Begin("##ua_jobs_widget", nullptr, flags))
        {
            RenderWidgetHeader(
                title,
                g_Settings.showJobsWidget,
                g_Settings.pinJobsWidget);

            if (carrierOn)
                RenderCarrierJobsBody();
            else if (handyOn)
                RenderHandymanJobsBody();
            else if (busOn)
                RenderBusJobsBody();
            else if (woodOn)
                RenderWoodmanJobsBody();
            else
                RenderJobsIdleBody();
        }
        ImGui::End();
        ImGui::PopStyleVar();
        PopWidgetStyle();
    }

    void RenderAdminWidget()
    {
        if (!g_Settings.showAdminWidget)
            return;

        ImGui::SetNextWindowPos(
            ImVec2(18.f, 450.f),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(
            ImVec2(330.f, 280.f),
            ImGuiCond_Always);
        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar;
        if (g_Settings.pinAdminWidget)
            flags |= ImGuiWindowFlags_NoMove;
        if (!Menu::IsVisible())
            flags |= ImGuiWindowFlags_NoInputs;

        const float appear =
            WidgetAppearAlpha(0xA01C003u);
        ImGui::SetNextWindowBgAlpha(0.92f * appear);
        PushWidgetStyle();
        ImGui::PushStyleVar(
            ImGuiStyleVar_Alpha,
            appear);
        if (ImGui::Begin(
                "##ua_admin_widget",
                nullptr,
                flags))
        {
            RenderWidgetHeader(
                "Admin Detector",
                g_Settings.showAdminWidget,
                g_Settings.pinAdminWidget);

            const ImVec4 scanColor =
                g_AdminDet.on
                    ? ImVec4(0.3f, 0.9f, 0.45f, 1.f)
                    : ImVec4(0.9f, 0.55f, 0.3f, 1.f);
            DrawStatusPill(
                g_AdminDet.on ? "SCANNING" : "DISABLED",
                scanColor,
                g_AdminDet.on);
            ImGui::SameLine(0.f, 8.f);
            ImGui::SetCursorPosY(
                ImGui::GetCursorPosY() + 3.f);
            ImGui::TextDisabled(
                "%d online  ·  %d nearby",
                g_AdminDet.onlineCount,
                g_AdminDet.nearbyCount);
            if (g_AdminDet.spectating > 0)
            {
                ImGui::SameLine(0.f, 8.f);
                DrawStatusPill(
                    "SPECT",
                    ImVec4(0.95f, 0.25f, 0.3f, 1.f),
                    true);
            }

            ImGui::Spacing();
            if (!g_AdminDet.onlineListed)
            {
                DrawInsetPanel(
                    "No administrators online.",
                    40.f);
            }
            else
            {
                char nearLine[128]{};
                if (g_AdminDet.nearestId >= 0)
                {
                    _snprintf(
                        nearLine,
                        sizeof(nearLine) - 1,
                        "Nearest: %s [ID:%d] L%d · %dm",
                        g_AdminDet.nearestName[0]
                            ? g_AdminDet.nearestName
                            : "-",
                        g_AdminDet.nearestId,
                        g_AdminDet.nearestLvl,
                        g_AdminDet.nearestDist);
                }
                else
                {
                    lstrcpynA(
                        nearLine,
                        "Administrators online (server-wide)",
                        sizeof(nearLine));
                }
                DrawInsetPanel(nearLine, 40.f);
            }

            ImGui::Spacing();
            const int showMax = 8;
            for (int i = 0;
                 i < g_AdminDet.onlineListed && i < showMax;
                 ++i)
            {
                const OnlineAdminInfo& admin =
                    g_AdminDet.online[i];
                const ImVec4 color =
                    AdminLevelColor(admin.level);
                int nearDist = -1;
                bool nearInvis = false;
                bool nearSpect = false;
                for (int n = 0; n < g_AdminDet.nearbyListed; ++n)
                {
                    if (g_AdminDet.nearby[n].id == admin.id)
                    {
                        nearDist = g_AdminDet.nearby[n].dist;
                        nearInvis = g_AdminDet.nearby[n].invis;
                        nearSpect =
                            g_AdminDet.nearby[n].spectating;
                        break;
                    }
                }
                ImGui::TextColored(
                    color,
                    "%s [ID:%d]  L%d",
                    admin.name,
                    admin.id,
                    admin.level);
                ImGui::SameLine();
                if (nearDist >= 0)
                {
                    ImGui::TextDisabled(
                        "%dm%s%s",
                        nearDist,
                        nearInvis ? " INV" : "",
                        nearSpect ? " SPECT" : "");
                }
                else
                {
                    ImGui::TextDisabled("online");
                }
            }
            if (g_AdminDet.onlineListed > showMax)
            {
                ImGui::TextDisabled(
                    "+%d more (see Admin tab)",
                    g_AdminDet.onlineListed - showMax);
            }

            if (Menu::IsVisible())
            {
                ImGui::Spacing();
                if (gui->button(
                        g_AdminDet.on ? "Stop" : "Start",
                        ImVec2(100.f, 30.f)))
                {
                    EnqueueCommand(
                        g_AdminDet.on
                            ? "admin_detector_stop"
                            : "admin_detector_start",
                        g_AdminDet.on
                            ? "Admin Det Stop"
                            : "Admin Det Start");
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
        PopWidgetStyle();
    }
}

namespace UkraineTools
{
    void QueueCommand(
        const char* command,
        const char* label,
        bool silent) noexcept
    {
        if (g_ServerKind != ServerKind::Ukraine)
            return;
        EnqueueCommand(command, label, silent);
    }

    void OpenUiTab(int tab) noexcept
    {
        if (tab < 0)
            tab = 0;
        if (tab > 8)
            tab = 8;
        g_UiTab = tab;
    }

    void RenderWidgets()
    {
        if (g_ServerKind != ServerKind::Ukraine)
            return;
        if (!g_Loaded && !g_LoadRequested)
            return;
        RenderJobsWidget();
        RenderAdminWidget();
    }

    void Initialize() noexcept
    {
        g_RuntimeGeneration = 0;
        ResetRuntimeSession();
        lstrcpynA(g_RawEvent, "bizStealing.get.money", sizeof(g_RawEvent));
        lstrcpynA(g_RawArgs, "[]", sizeof(g_RawArgs));
        lstrcpynA(g_BpAction, "get", sizeof(g_BpAction));
        lstrcpynA(g_CefSearch, "item.add", sizeof(g_CefSearch));
    }

    bool IsUkraineServer() noexcept
    {
        return g_ServerKind == ServerKind::Ukraine;
    }

    bool IsCefDumpBusy() noexcept
    {
        return g_CefDumpActive ||
            g_CefDumpDraining ||
            g_CefDumpFetchAt != 0;
    }

    void Update() noexcept
    {
        const ULONGLONG now = GetTickCount64();
        const auto runtime = RageV8Runtime::GetStatus();
        if (runtime.generation != g_RuntimeGeneration)
        {
            g_RuntimeGeneration = runtime.generation;
            ResetRuntimeSession();
            hooks::HLog(
                "[UkraineTools] V8 generation changed; reloading");
        }

        const bool runtimeReady =
            runtime.state == RageV8Runtime::State::Ready &&
            runtime.rootCaptured;
        if (!runtimeReady)
        {
            g_Loaded = false;
            g_LoadRequested = false;
            g_PendingCount = 0;
            g_WasRuntimeReady = false;
            return;
        }

        if (!g_WasRuntimeReady)
        {
            g_WasRuntimeReady = true;
            g_LastLoadAttempt = 0;
            g_LoadRequested = false;
            g_ServerKind = ServerKind::Unknown;
        }

        if ((GetAsyncKeyState(VK_F1) & 0x8000) != 0)
            return;

        int write = 0;
        for (int index = 0; index < g_PendingCount; ++index)
        {
            if (now - g_Pending[index].timestamp >= 3500)
            {
                if (g_Pending[index].kind == RequestKind::Status ||
                    g_Pending[index].kind == RequestKind::Load)
                {
                    g_Loaded = false;
                    g_LoadRequested = false;
                }
                if (g_Pending[index].kind == RequestKind::CefDump)
                    ScheduleCefDumpRetry(500);
                continue;
            }

            RageV8Runtime::Result result{};
            if (!RageV8Runtime::PollResultFor(
                    g_Pending[index].id,
                    result))
            {
                if (write != index)
                    g_Pending[write] = g_Pending[index];
                ++write;
                continue;
            }

            const PendingRequest request = g_Pending[index];
            if (request.kind == RequestKind::Load)
            {
                g_LoadRequested = false;
                g_Loaded = result.success;
                lstrcpynA(
                    g_StatusLine,
                    result.success ? "Loaded" : "Load failed",
                    sizeof(g_StatusLine));
                hooks::HLog(
                    result.success
                        ? "[UkraineTools] module loaded"
                        : "[UkraineTools] module load failed");
                continue;
            }

            if (request.kind == RequestKind::Status)
            {
                if (result.success &&
                    result.text[0] == 'S' &&
                    result.text[1] == '\t')
                {
                    char statusBuf[RageV8Runtime::kMaxResultLength]{};
                    lstrcpynA(
                        statusBuf,
                        result.text,
                        sizeof(statusBuf));
                    ParseStatus(statusBuf);
                }
                else
                {
                    g_Loaded = false;
                    g_LoadRequested = false;
                }
                continue;
            }

            if (request.kind == RequestKind::CefDump)
            {
                if (result.success && result.text[0])
                    HandleCefDumpResult(result.text);
                else
                {
                    hooks::HLog(
                        "[UkraineTools] CEF dump pull failed, retry");
                    ScheduleCefDumpRetry(700);
                    lstrcpynA(
                        g_CefDumpStatus,
                        "Pull retry...",
                        sizeof(g_CefDumpStatus));
                }
                continue;
            }

            if (request.kind == RequestKind::Command)
            {
                lstrcpynA(
                    g_LastResult,
                    result.text[0] ? result.text : "<empty>",
                    sizeof(g_LastResult));
                if (!request.silent)
                {
                    Menu::PushNotification(
                        request.label,
                        g_LastResult,
                        result.success,
                        result.success ? 2500 : 5000);
                }
            }
        }
        g_PendingCount = write;

        SyncServerKind();

        if (g_ServerKind == ServerKind::Ukraine &&
            !g_Loaded &&
            !g_LoadRequested &&
            (g_LastLoadAttempt == 0 ||
             now - g_LastLoadAttempt >= 2000))
        {
            QueueLoad();
        }

        if (g_Loaded &&
            g_ServerKind == ServerKind::Ukraine &&
            g_CarrierAutoReapplyPending &&
            g_CarrierAutoReapplyReady)
        {
            g_CarrierAutoReapplyPending = false;
            g_CarrierAutoReapplyReady = false;
            g_CarrierAutoCommandPending = true;
            EnqueueCommand(
                "carrier_auto_toggle:1",
                "Auto Carrier resume",
                true);
            EnqueueCommand(
                g_CarrierAvoidPlayers
                    ? "carrier_avoid_players:1"
                    : "carrier_avoid_players:0",
                "Carrier avoid players resume",
                true);
        }

        if (g_CefDumpFetchAt != 0 &&
            now >= g_CefDumpFetchAt &&
            g_Loaded &&
            !g_CefDumpDraining)
        {
            g_CefDumpFetchAt = 0;
            g_CefDumpDraining = true;
            const bool force =
                g_CefDumpStartedAt != 0 &&
                now >= g_CefDumpStartedAt + 300000;
            QueueCefDumpPull(force);
            lstrcpynA(
                g_CefDumpStatus,
                force ? "Force saving..." : "Saving...",
                sizeof(g_CefDumpStatus));
        }

        if (g_Loaded && g_ServerKind == ServerKind::Ukraine)
        {
            const ULONGLONG statusInterval =
                IsCefDumpBusy() ? 5000ull : 1500ull;
            if (now - g_LastStatusRequest >= statusInterval)
            {
                bool hasStatusPending = false;
                for (int index = 0; index < g_PendingCount; ++index)
                {
                    if (g_Pending[index].kind == RequestKind::Status)
                    {
                        hasStatusPending = true;
                        break;
                    }
                }
                if (!hasStatusPending)
                {
                    g_LastStatusRequest = now;
                    QueueScript(
                        "return global.__uaGetStatus ? "
                        "global.__uaGetStatus() : "
                        "'UA_NOT_LOADED';",
                        RequestKind::Status,
                        "UA status",
                        true);
                }
            }
        }
    }

    void RenderUI()
    {
        ImGui::Text("Ukraine Tools");
        ImGui::SameLine();
        {
            const char* stateText = "Detecting server...";
            ImVec4 stateColor(0.95f, 0.58f, 0.25f, 1.f);
            if (g_ServerKind == ServerKind::Other)
            {
                stateText = "Not Ukraine GTA";
                stateColor = ImVec4(0.85f, 0.35f, 0.35f, 1.f);
            }
            else if (g_Loaded)
            {
                stateText = "Loaded · Ukraine GTA";
                stateColor = ImVec4(0.3f, 0.9f, 0.45f, 1.f);
            }
            else if (g_LoadRequested)
            {
                stateText = "Loading...";
            }
            else if (g_ServerKind == ServerKind::Ukraine)
            {
                stateText = "Ukraine GTA · waiting inject";
            }
            ImGui::TextColored(stateColor, "%s", stateText);
        }

        ImGui::TextDisabled("%s", g_StatusLine);
        if (g_LastResult[0])
            ImGui::TextDisabled("Last: %s", g_LastResult);

        if (!g_Loaded)
        {
            ImGui::Spacing();
            ImGui::TextWrapped(
                "Ukraine Tools injects automatically after Ukraine GTA "
                "is detected (bizStealing / ukraine-gta5 domain).");
            return;
        }

        const char* tabs[] = {
            "Crime##ua", "Jobs##ua", "Bus##ua", "Admin##ua",
            "Carrier##ua", "Prizes##ua", "AirDrop##ua", "CEF##ua", "Raw##ua"
        };
        ImGui::Spacing();
        for (int i = 0; i < IM_ARRAYSIZE(tabs); ++i)
        {
            if (i > 0)
                ImGui::SameLine(0.f, 4.f);
            if (i == 5)
                ImGui::NewLine();
            if (gui->sub_selection(
                    i == 0 ? ICON_FA_MASK :
                    i == 1 ? ICON_FA_BRIEFCASE :
                    i == 2 ? ICON_FA_BUS :
                    i == 3 ? ICON_FA_USER_SHIELD :
                    i == 4 ? ICON_FA_TRUCK :
                    i == 5 ? ICON_FA_GIFT :
                    i == 6 ? ICON_FA_PARACHUTE_BOX :
                    i == 7 ? ICON_FA_CODE :
                    ICON_FA_TERMINAL,
                    tabs[i],
                    i,
                    g_UiTab,
                    ImVec2(78.f, 34.f)))
            {
            }
        }
        gui->separator();

        ImGui::PushID("UkraineToolsUI");
        if (g_UiTab == 0)
        {
            ImGui::TextDisabled(
                "Appearance: donate change-view (may need DC / server check)");
            if (gui->button("Change appearance", ImVec2(170.f, 30.f)))
                EnqueueCommand("char_view_change", "Char view change");
            SameLineIfRoom(178.f);
            CommandButton("View remote only", "char_view_change_remote", 150.f);
            SameLineIfRoom(158.f);
            CommandButton("Cancel view", "char_view_cancel", 130.f);
            ImGui::Spacing();
            gui->separator();
            CommandButton("Biz Steal Money", "crime_biz_money");
            SameLineIfRoom();
            CommandButton("Biz NPC Death", "crime_biz_npc_death");
            SameLineIfRoom();
            CommandButton("Aim Start", "crime_biz_aim_start");
            ImGui::Spacing();
            ImGui::SetNextItemWidth(120.f);
            ImGui::InputInt("Aim target id (-1=near)", &g_AimTargetId);
            if (gui->button("Aim End", ImVec2(140.f, 30.f)))
            {
                char cmd[64]{};
                _snprintf(
                    cmd,
                    sizeof(cmd) - 1,
                    "crime_biz_aim_end:%d",
                    g_AimTargetId);
                EnqueueCommand(cmd, "Aim End");
            }
        }
        else if (g_UiTab == 1)
        {
            gui->checkbox("Jobs widget", &g_Settings.showJobsWidget);
            ImGui::SameLine();
            gui->checkbox("Pin jobs", &g_Settings.pinJobsWidget);
            ImGui::Spacing();
            ImGui::TextDisabled(
                "Woodman: Start → chop one log manually (logs.add), bot takes over");
            if (gui->button("Woodman Start", ImVec2(150.f, 30.f)))
            {
                g_Settings.showJobsWidget = true;
                EnqueueCommand("woodman_bot_start", "Woodman Start");
            }
            SameLineIfRoom(158.f);
            CommandButton("Woodman Stop", "woodman_bot_stop", 150.f);
            SameLineIfRoom(158.f);
            CommandButton("Inv probe", "woodman_inv_probe", 120.f);
            ImGui::Spacing();
            if (g_Woodman.on || g_Woodman.phase[0])
            {
                ImGui::TextDisabled(
                    "Bot: %s · phase %s · wood %d · axe %d%%",
                    g_Woodman.on ? "ON" : "OFF",
                    g_Woodman.phase,
                    g_Woodman.woodCount,
                    g_Woodman.axeHealth);
            }
            ImGui::Spacing();
            gui->separator();
            ImGui::TextDisabled(
                "Handyman: Start → work.start + tools check → TP markers");
            if (gui->button("Handyman Start", ImVec2(150.f, 30.f)))
            {
                g_Settings.showJobsWidget = true;
                EnqueueCommand(
                    "handyman_bot_start", "Handyman Start");
            }
            SameLineIfRoom(158.f);
            CommandButton(
                "Handyman Stop", "handyman_bot_stop", 150.f);
            SameLineIfRoom(158.f);
            CommandButton(
                "HM probe", "handyman_inv_probe", 120.f);
            SameLineIfRoom(128.f);
            CommandButton("Handyman OK", "job_handyman_ok", 120.f);
            if (g_Handyman.on || g_Handyman.phase[0])
            {
                ImGui::TextDisabled(
                    "HM: %s · %s · %s · %d/%d · %s · Cl %d/%d",
                    g_Handyman.on ? "ON" : "OFF",
                    g_Handyman.phase,
                    g_Handyman.step,
                    g_Handyman.qi,
                    g_Handyman.queueLen,
                    g_Handyman.summary[0] ? g_Handyman.summary : "-",
                    g_Handyman.chlorineQty,
                    g_Handyman.chlorineNeed);
            }
            ImGui::Spacing();
            gui->separator();
            CommandButton("Builder finish", "job_builder_finish");
            SameLineIfRoom();
            CommandButton("Incasation finish", "job_incasation_finish");
            CommandButton("Steal car finish", "job_steal_finish");
            SameLineIfRoom();
            CommandButton("Driving school", "job_school_finish");
            SameLineIfRoom();
            CommandButton("Graffiti finish", "job_graffiti_finish");
            CommandButton("Treasure finish", "job_treasure_finish");
            ImGui::Spacing();
            static const char* kWcf[] = { "cleanUp", "prepareForMatch" };
            ImGui::SetNextItemWidth(180.f);
            gui->dropdown("WCF action", &g_WcfAction, kWcf, 2);
            if (g_WcfAction < 0 || g_WcfAction > 1)
                g_WcfAction = 0;
            SameLineIfRoom(160.f);
            CommandButton(
                "WCF finish",
                g_WcfAction == 0 ? "job_wcf_cleanup" : "job_wcf_prepare");
        }
        else if (g_UiTab == 2)
        {
            ImGui::TextDisabled(
                "Okruzhnyi bus (id=6, fare=0) · underground park · fixed bursts");
            SyncBusUiFromStatus();
            static const char* kStyles[] = {
                "Per point", "Segment skip", "Remain 15s"
            };
            static const char* kSpeeds[] = {
                "Slow (200ms)", "Normal (100ms)",
                "Fast (50ms)", "Turbo (25ms)"
            };
            ImGui::SetNextItemWidth(160.f);
            if (gui->dropdown(
                    "Style",
                    &g_BusStyleIdx,
                    kStyles,
                    3))
            {
                static const char* kStyleCmd[] = {
                    "bus_style:per_point",
                    "bus_style:segment_skip",
                    "bus_style:remain_window"
                };
                if (g_BusStyleIdx < 0)
                    g_BusStyleIdx = 0;
                if (g_BusStyleIdx > 2)
                    g_BusStyleIdx = 2;
                EnqueueCommand(
                    kStyleCmd[g_BusStyleIdx],
                    "Bus style");
            }
            SameLineIfRoom(170.f);
            ImGui::SetNextItemWidth(160.f);
            if (gui->dropdown(
                    "Speed",
                    &g_BusSpeedIdx,
                    kSpeeds,
                    4))
            {
                if (g_BusSpeedIdx < 0)
                    g_BusSpeedIdx = 0;
                if (g_BusSpeedIdx > 3)
                    g_BusSpeedIdx = 3;
                char cmd[32]{};
                _snprintf(
                    cmd,
                    sizeof(cmd) - 1,
                    "bus_speed:%d",
                    g_BusSpeedIdx);
                EnqueueCommand(cmd, "Bus speed");
            }
            ImGui::TextDisabled(
                "Fixed: lead 18 → stop 10 → stop 15 → stop 13 → repeat");
            ImGui::Spacing();
            if (gui->button(
                    g_Bus.on ? "Bus Bot Stop" : "Bus Bot Start",
                    ImVec2(150.f, 30.f)))
            {
                g_Settings.showJobsWidget = true;
                EnqueueCommand(
                    g_Bus.on ? "bus_bot_stop" : "bus_bot_start",
                    g_Bus.on ? "Bus Bot Stop" : "Bus Bot Start");
            }
            SameLineIfRoom(158.f);
            if (gui->button("Start route 6", ImVec2(140.f, 30.f)))
                EnqueueCommand("bus_route_okrug", "Bus route 6");
            SameLineIfRoom(148.f);
            CommandButton("Employ", "bus_employ", 100.f);
            SameLineIfRoom(108.f);
            CommandButton("Rent OK", "bus_rent_accept", 100.f);
            SameLineIfRoom(108.f);
            CommandButton("CP once", "job_bus_checkpoint", 100.f);
            gui->checkbox("Jobs widget", &g_Settings.showJobsWidget);
            ImGui::SameLine();
            gui->checkbox("Pin jobs", &g_Settings.pinJobsWidget);
            ImGui::TextDisabled(
                "Bus: %s · %s · %s · spd %d · lap#%d · $%d ($%d/hr) · left %d%s",
                g_Bus.on ? "ON" : "OFF",
                g_Bus.phase[0] ? g_Bus.phase : "idle",
                g_Bus.style[0] ? g_Bus.style : "segment_skip",
                g_Bus.speedId,
                g_Bus.lapIndex,
                g_Bus.earnedSession,
                g_Bus.perHour,
                g_Bus.spamLeft,
                g_Bus.underground ? " · UNDER" : "");
            if (g_Bus.lastMsg[0])
                ImGui::TextWrapped("%s", g_Bus.lastMsg);
        }
        else if (g_UiTab == 3)
        {
            ImGui::TextDisabled(
                "Server-wide scan via redNick (Ukraine). Auto-starts on inject.");
            if (gui->button(
                    g_AdminDet.on ? "Detector Stop" : "Detector Start",
                    ImVec2(150.f, 30.f)))
            {
                g_Settings.showAdminWidget = true;
                EnqueueCommand(
                    g_AdminDet.on
                        ? "admin_detector_stop"
                        : "admin_detector_start",
                    g_AdminDet.on ? "Admin Det Stop" : "Admin Det Start");
            }
            SameLineIfRoom(158.f);
            gui->checkbox("Admin widget", &g_Settings.showAdminWidget);
            ImGui::SameLine();
            gui->checkbox("Pin admin", &g_Settings.pinAdminWidget);
            ImGui::TextDisabled(
                "%s · online %d · nearby %d · spect %d",
                g_AdminDet.on ? "SCANNING" : "DISABLED",
                g_AdminDet.onlineCount,
                g_AdminDet.nearbyCount,
                g_AdminDet.spectating);
            if (g_AdminDet.nearestId >= 0)
            {
                ImGui::TextDisabled(
                    "Nearest: %s [ID:%d] L%d · %dm",
                    g_AdminDet.nearestName[0]
                        ? g_AdminDet.nearestName
                        : "-",
                    g_AdminDet.nearestId,
                    g_AdminDet.nearestLvl,
                    g_AdminDet.nearestDist);
            }
            ImGui::Spacing();
            gui->separator();
            ImGui::TextDisabled("Administrators online (all server)");
            if (!g_AdminDet.onlineListed)
                ImGui::TextDisabled("No administrators online.");
            for (int i = 0; i < g_AdminDet.onlineListed; ++i)
            {
                const OnlineAdminInfo& admin = g_AdminDet.online[i];
                const ImVec4 color = AdminLevelColor(admin.level);
                int nearDist = -1;
                bool nearInvis = false;
                bool nearSpect = false;
                for (int n = 0; n < g_AdminDet.nearbyListed; ++n)
                {
                    if (g_AdminDet.nearby[n].id == admin.id)
                    {
                        nearDist = g_AdminDet.nearby[n].dist;
                        nearInvis = g_AdminDet.nearby[n].invis;
                        nearSpect =
                            g_AdminDet.nearby[n].spectating;
                        break;
                    }
                }
                ImGui::TextColored(
                    color,
                    "%s",
                    admin.name);
                ImGui::SameLine();
                ImGui::TextDisabled("ID %d |", admin.id);
                ImGui::SameLine();
                ImGui::TextColored(
                    color,
                    "Level %d",
                    admin.level);
                if (nearDist >= 0)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled(
                        "| %dm%s%s",
                        nearDist,
                        nearInvis ? " INV" : "",
                        nearSpect ? " SPECT" : "");
                }
            }
        }
        else if (g_UiTab == 4)
        {
            if (ImGui::Checkbox(
                    "Auto Carrier##ua_carrier_auto",
                    &g_CarrierAutoDesired))
            {
                const bool autoCarrier =
                    g_CarrierAutoDesired;
                g_Carrier.autoMode = autoCarrier;
                g_Carrier.on = autoCarrier;
                if (autoCarrier)
                {
                    g_CarrierMaxWeight = 0;
                    g_CarrierSpamMs = 100;
                    g_CarrierClearDistance = 35.f;
                    g_CarrierPauseDistance = 20.f;
                    g_CarrierMaxUnderOffset = 25.f;
                    g_CarrierSkipIllegal = true;
                }
                g_CarrierAutoCommandPending = true;
                EnqueueCommand(
                    autoCarrier
                        ? "carrier_auto_toggle:1"
                        : "carrier_auto_toggle:0",
                    autoCarrier
                        ? "Auto Carrier Start"
                        : "Auto Carrier Stop");
                if (autoCarrier)
                    g_Settings.showJobsWidget = true;
            }
            ImGui::SameLine();
            ImGui::Text(
                "%s · %s · %ds",
                g_Carrier.phase[0] ? g_Carrier.phase : "idle",
                g_Carrier.prerequisite[0]
                    ? g_Carrier.prerequisite : "off",
                g_Carrier.phaseAge);
            ImGui::TextDisabled(
                "Defaults: spam 100ms · avoid players · clear 35m · pause 20m · under offset 25m · legal only · capacity auto");
            if (g_Carrier.lastMsg[0])
                ImGui::TextWrapped("%s", g_Carrier.lastMsg);
            ImGui::TextDisabled(
                "cap %s · retries %d · server %s · delivery %s",
                g_Carrier.capacitySource[0]
                    ? g_Carrier.capacitySource : "unknown",
                g_Carrier.retries,
                g_Carrier.lastServerAnswer[0]
                    ? g_Carrier.lastServerAnswer : "-",
                g_Carrier.deliveryAck[0]
                    ? g_Carrier.deliveryAck : "idle");
            if (g_Carrier.waitingClear)
            {
                ImGui::TextColored(
                    ImVec4(0.95f, 0.62f, 0.2f, 1.f),
                    "WAITING CLEAR");
                ImGui::SameLine();
            }
            if (g_Carrier.nearestPlayerId >= 0)
            {
                if (g_Carrier.nearestPlayerName[0])
                {
                    ImGui::TextDisabled(
                        "Nearest player: %s [ID:%d] · %.1fm",
                        g_Carrier.nearestPlayerName,
                        g_Carrier.nearestPlayerId,
                        g_Carrier.nearestPlayerDistance);
                }
                else
                {
                    ImGui::TextDisabled(
                        "Nearest player: ID %d · %.1fm",
                        g_Carrier.nearestPlayerId,
                        g_Carrier.nearestPlayerDistance);
                }
            }
            else
            {
                ImGui::TextDisabled("Nearest player: none streamed");
            }
            ImGui::TextDisabled(
                "order #%d · %dT · $%d · cap %dT · route %d/%d · veh %d · park %d",
                g_Carrier.orderId,
                g_Carrier.orderWeight,
                g_Carrier.orderPrice,
                g_Carrier.knownVehWeight > 0
                    ? g_Carrier.knownVehWeight
                    : g_Carrier.maxWeight,
                g_Carrier.routeIndex,
                g_Carrier.routeLen,
                g_Carrier.vehRemoteId,
                g_Carrier.parkPosIndex);
            ImGui::Spacing();
            if (ImGui::CollapsingHeader(
                    "Advanced##ua_carrier_advanced"))
            {
            ImGui::TextDisabled(
                "Overrides apply until Auto Carrier is toggled again.");
            ImGui::SetNextItemWidth(90.f);
            if (ImGui::InputInt("Max T", &g_CarrierMaxWeight))
            {
                if (g_CarrierMaxWeight < 0)
                    g_CarrierMaxWeight = 0;
                char cmd[48]{};
                _snprintf(
                    cmd,
                    sizeof(cmd) - 1,
                    "carrier_max_weight:%d",
                    g_CarrierMaxWeight);
                EnqueueCommand(cmd, "Carrier max weight");
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.f);
            if (ImGui::InputInt("Spam ms", &g_CarrierSpamMs))
            {
                if (g_CarrierSpamMs < 50)
                    g_CarrierSpamMs = 50;
                char cmd[48]{};
                _snprintf(
                    cmd,
                    sizeof(cmd) - 1,
                    "carrier_spam_ms:%d",
                    g_CarrierSpamMs);
                EnqueueCommand(cmd, "Carrier spam ms");
            }
            ImGui::Spacing();
            gui->separator();
            if (gui->checkbox(
                    "Avoid players",
                    &g_CarrierAvoidPlayers))
            {
                g_Carrier.avoidPlayers =
                    g_CarrierAvoidPlayers;
                if (!g_CarrierAvoidPlayers)
                    g_Carrier.waitingClear = false;
                EnqueueCommand(
                    g_CarrierAvoidPlayers
                        ? "carrier_avoid_players:1"
                        : "carrier_avoid_players:0",
                    "Carrier avoid players");
            }
            ImGui::SameLine();
            if (gui->checkbox(
                    "Skip illegal cargo",
                    &g_CarrierSkipIllegal))
            {
                EnqueueCommand(
                    g_CarrierSkipIllegal
                        ? "carrier_skip_illegal:1"
                        : "carrier_skip_illegal:0",
                    "Carrier legal cargo filter");
            }
            ImGui::SetNextItemWidth(260.f);
            gui->slider_float(
                "Clear distance",
                &g_CarrierClearDistance,
                0.f,
                100.f,
                "%.0f m");
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                char cmd[64]{};
                _snprintf(
                    cmd,
                    sizeof(cmd) - 1,
                    "carrier_clear_distance:%.1f",
                    g_CarrierClearDistance);
                EnqueueCommand(cmd, "Carrier clear distance");
            }
            ImGui::SetNextItemWidth(260.f);
            gui->slider_float(
                "Pause distance",
                &g_CarrierPauseDistance,
                0.f,
                100.f,
                "%.0f m");
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                char cmd[64]{};
                _snprintf(
                    cmd,
                    sizeof(cmd) - 1,
                    "carrier_pause_distance:%.1f",
                    g_CarrierPauseDistance);
                EnqueueCommand(cmd, "Carrier pause distance");
            }
            ImGui::SetNextItemWidth(260.f);
            gui->slider_float(
                "Max underground offset",
                &g_CarrierMaxUnderOffset,
                0.f,
                25.f,
                "%.0f m");
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                char cmd[64]{};
                _snprintf(
                    cmd,
                    sizeof(cmd) - 1,
                    "carrier_max_under_offset:%.1f",
                    g_CarrierMaxUnderOffset);
                EnqueueCommand(
                    cmd,
                    "Carrier max underground offset");
            }
            ImGui::TextDisabled(
                "Active: clear %.0fm · pause %.0fm · offset %.0fm",
                g_Carrier.clearDistance,
                g_Carrier.pauseDistance,
                g_Carrier.maxUnderOffset);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Manual remotes");
            CommandButton("Next route point", "carrier_next", 160.f);
            SameLineIfRoom(168.f);
            CommandButton("Take garage veh", "carrier_take_veh", 160.f);
            CommandButton("Attach load", "carrier_attach");
            SameLineIfRoom();
            CommandButton("Detach load", "carrier_detach");
            SameLineIfRoom();
            CommandButton(
                "Start loading", "carrier_start_loading", 140.f);
            ImGui::Spacing();
            ImGui::SetNextItemWidth(100.f);
            ImGui::InputInt("Parking place", &g_ParkingPlace);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.f);
            ImGui::InputInt("Vehicle id", &g_ParkingVehId);
            if (gui->button("Parking end", ImVec2(140.f, 30.f)))
            {
                char cmd[80]{};
                _snprintf(
                    cmd,
                    sizeof(cmd) - 1,
                    "carrier_parking:%d:%d",
                    g_ParkingPlace,
                    g_ParkingVehId);
                EnqueueCommand(cmd, "Parking end");
            }
            }
        }
        else if (g_UiTab == 5)
        {
            ImGui::SetNextItemWidth(100.f);
            ImGui::InputInt("Container index", &g_ContainerIndex);
            ImGui::SameLine();
            gui->checkbox("Sell", &g_ContainerSell);
            if (gui->button("Receive prize", ImVec2(140.f, 30.f)))
            {
                char cmd[64]{};
                _snprintf(
                    cmd,
                    sizeof(cmd) - 1,
                    "prize_container:%d:%d",
                    g_ContainerIndex,
                    g_ContainerSell ? 1 : 0);
                EnqueueCommand(cmd, "Container prize");
            }
            SameLineIfRoom();
            CommandButton("Sell all prizes", "prize_container_sell_all", 150.f);
            ImGui::Spacing();
            ImGui::SetNextItemWidth(80.f);
            ImGui::InputInt("BP donate(0/1)", &g_BpDonate);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.f);
            ImGui::InputText("action", g_BpAction, sizeof(g_BpAction));
            ImGui::SetNextItemWidth(80.f);
            ImGui::InputInt("BP lvl", &g_BpLvl);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.f);
            ImGui::InputInt("BP index", &g_BpIndex);
            if (gui->button("BP prize get", ImVec2(140.f, 30.f)))
            {
                char cmd[128]{};
                _snprintf(
                    cmd,
                    sizeof(cmd) - 1,
                    "prize_bpass:%d:%s:%d:%d",
                    g_BpDonate ? 1 : 0,
                    g_BpAction[0] ? g_BpAction : "get",
                    g_BpLvl,
                    g_BpIndex);
                EnqueueCommand(cmd, "BP prize");
            }
            ImGui::Spacing();
            CommandButton("Package prize", "prize_package");
            SameLineIfRoom();
            CommandButton("Lucky finish", "prize_lucky_finish");
            SameLineIfRoom();
            CommandButton("Lucky spin", "prize_lucky_spin");
            SameLineIfRoom();
            CommandButton("WoF prize", "prize_wof");
        }
        else if (g_UiTab == 6)
        {
            CommandButton("AirDrop interact", "airdrop_interact", 160.f);
            SameLineIfRoom(168.f);
            CommandButton("AirDrop close", "airdrop_close", 160.f);
            ImGui::SetNextItemWidth(120.f);
            ImGui::InputInt("Item id", &g_AirDropItemId);
            if (gui->button("Get item", ImVec2(140.f, 30.f)))
            {
                char cmd[48]{};
                _snprintf(
                    cmd,
                    sizeof(cmd) - 1,
                    "airdrop_get:%d",
                    g_AirDropItemId);
                EnqueueCommand(cmd, "AirDrop get");
            }
        }
        else if (g_UiTab == 7)
        {
            ImGui::TextDisabled(
            "Capture to disk is disabled in this build.");
            ImGui::TextDisabled("Status: %s", g_CefDumpStatus);
            if (IsCefDumpBusy())
                ImGui::TextColored(
                    ImVec4(0.95f, 0.7f, 0.25f, 1.f),
                    "Dump in progress — keep game focused");

            CommandButton("Dump HTML", "dump_cef", 140.f);
            SameLineIfRoom();
            CommandButton("Dump Scripts", "dump_cef_scripts", 140.f);
            SameLineIfRoom();
            CommandButton("Dump ALL", "dump_cef_all", 140.f);
            CommandButton("Dump Logic probe", "dump_cef_logic", 160.f);
            ImGui::Spacing();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 150.f);
            ImGui::InputText("Search", g_CefSearch, sizeof(g_CefSearch));
            ImGui::SameLine();
            if (gui->button("Search CEF", ImVec2(140.f, 30.f)))
            {
                char cmd[160]{};
                _snprintf(
                    cmd,
                    sizeof(cmd) - 1,
                    "dump_cef_search:%s",
                    g_CefSearch[0] ? g_CefSearch : "item.add");
                EnqueueCommand(cmd, "CEF search");
            }
            ImGui::TextWrapped(
                "HTML/scripts from mp.browsers + mp.callCEFV. "
                "Logic probe checks inventory/selectMenu/vuex and "
                "needles like item.add / callRemote.");
        }
        else if (g_UiTab == 8)
        {
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputText("Event", g_RawEvent, sizeof(g_RawEvent));
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputText("Args JSON array", g_RawArgs, sizeof(g_RawArgs));
            ImGui::TextDisabled("Example: event=containers.prize.receive  args=[0,false]");
            if (gui->button("Send raw", ImVec2(140.f, 30.f)))
            {
                char cmd[320]{};
                _snprintf(
                    cmd,
                    sizeof(cmd) - 1,
                    "raw:%s:%s",
                    g_RawEvent[0] ? g_RawEvent : "log",
                    g_RawArgs[0] ? g_RawArgs : "[]");
                EnqueueCommand(cmd, "Raw remote");
            }
        }
        ImGui::PopID();
    }
}
