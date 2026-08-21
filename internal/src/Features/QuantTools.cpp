#include "QuantTools.h"

#include "QuantToolsScript.h"
#include "RageV8Runtime.h"
#include "../SDK/RageBuildFlavor.h"
#include "../Menu/Menu.h"
#include "../Menu/IconsFontAwesome6.h"
#include "../Menu/Leyawin/custom_widgets.hpp"
#include "../imgui/imgui.h"

#include <Windows.h>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

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
        AirDump,
        CefDump,
        Detect
    };

    enum class ServerKind : std::uint8_t
    {
        Unknown,
        Quant,
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

    struct AdminInfo final
    {
        int id{};
        char name[64]{};
        int level{};
        int distance{};
        bool invisible{};
        bool spectating{};
        bool offDuty{};
    };

    struct OnlineAdminInfo final
    {
        int id{};
        char name[64]{};
        int level{};
    };

    struct FarmStatus final
    {
        bool active{};
        char status[128]{"Not loaded"};
        bool tokenCaptured{};
        int version{1};
        int boxes{};
        int totalBoxes{};
        int rounds{};
        int earned{};
        int perHour{};
        int airMinutes{-1};
        bool adminDetector{};
        bool adminEsp{};
        // 1 = Aquanaut layout, 2 = Firma (trucker) layout
        int farmModule{1};
        // Junk seller loot prices (-1 = unknown / use fallback in JS)
        int lootPearl{-1};
        int lootStarfish{-1};
        int lootRapana{-1};
        std::int64_t lootExpiryMs{};
        bool lootPricesStale{};
        OnlineAdminInfo onlineAdmins[64]{};
        int onlineAdminCount{};
        AdminInfo admins[32]{};
        int adminCount{};
        std::uint32_t criticalAdminJoinSerial{};
        int criticalAdminJoinId{};
        char criticalAdminJoinName[64]{};
        int criticalAdminJoinLevel{};
        ULONGLONG updatedAt{};
    };

    struct QuantSettings final
    {
        int botVersion{2}; // v4: locked v2-style
        bool safeTp{true};
        bool skipOccupied{true};
        bool invisibility{true};
        int orderMode{2}; // 0=price 1=zone 2=last (default last)
        char zoneFilter[64]{};
        int zoneMaxDistance{1500};

        bool armourBypass{};
        int armourInterval{2000};
        bool autoPickup{};
        int autoPickupMode{};
        int autoPickupDelay{300};
        bool antiFiring{};
        bool noFalloff{};
        bool noDrop{};
        bool damageCounter{};

        bool hitFx{};
        bool hitNumbers{true};
        bool hitSound{true};
        bool headSound{true};
        bool hitOutline{true};
        int outlineWidth{2};
        bool hitShadow{true};
        bool originalColors{};
        bool distanceScale{true};
        float hitFontSize{0.55f};
        int hitFont{4};
        float hitStretchX{1.f};
        float hitStretchY{1.f};
        int hitDuration{1500};
        float hitFloatDistance{1.2f};
        float hitSpread{0.3f};
        float hitHeight{0.85f};
        float hitSoundVolume{0.5f};
        float hitColor[4]{1.f, 1.f, 1.f, 1.f};

        bool strafe{};
        float strafeSpeed{3.8f};
        bool strafeAimOnly{true};
        bool strafeForwardBack{true};
        bool adTiming{};
        int adTargetA{228};
        int adTargetD{228};
        int adJitter{8};
        int adUsefulDelay{};
        bool adAimOnly{};
        bool antiRecoil{};
        float recoilStrength{0.65f};
        float recoilSmoothing{0.4f};
        float recoilJitter{0.15f};
        bool recoilAimOnly{true};
        bool heliStabilizer{};
        float heliDamping{0.92f};

        int crawlKey{};
        int cheatSheetKey{};
        int cheatSheetScale{100};
        int cheatSheetOpacity{90};
        int cheatSheetDim{85};
        int cheatSheetVariant{2};

        bool adminDetector{true};
        bool adminEsp{};
        bool showAquanautWidget{true};
        bool pinAquanautWidget{};
        bool showAdminWidget{true};
        bool pinAdminWidget{};
        std::uint32_t adminGuardFlags{};

        // Majestic-inspired Quant CEF restyle (HUD/chat/inventory).
        // Server UI Theme (Majestic redesign) — disabled for now
        bool uiTheme{false};
        float uiThemeOpacity{1.f};
    };

    struct SettingsHeader final
    {
        std::uint32_t magic{};
        std::uint32_t version{};
        std::uint32_t size{};
    };

    constexpr int kMaxPending = 16;
    constexpr std::uint32_t kSettingsMagic = 0x53545151U;
    constexpr std::uint32_t kSettingsVersion = 4;
    constexpr std::uint32_t kLegacySettingsVersion = 2;
    constexpr std::uint32_t kLegacySettingsSize =
        static_cast<std::uint32_t>(
            offsetof(QuantSettings, adminGuardFlags));
    constexpr std::uint32_t kLegacySettingsVersionV3 = 3;
    constexpr std::uint32_t kLegacySettingsSizeV3 =
        static_cast<std::uint32_t>(
            offsetof(QuantSettings, uiTheme));
    constexpr std::uint32_t kAdminGuardExitFlag = 1U;
    static_assert(
        kLegacySettingsSize < sizeof(QuantSettings),
        "Admin guard settings must append to v2 layout");
    static_assert(
        kLegacySettingsSize == 0x100,
        "Quant settings v2 layout changed unexpectedly");
    static_assert(
        kLegacySettingsSizeV3 < sizeof(QuantSettings),
        "UI theme settings must append to v3 layout");
    constexpr wchar_t kSettingsPath[] =
        L"C:\\RAGEMP\\quant_menu_settings.bin";
    PendingRequest g_Pending[kMaxPending]{};
    int g_PendingCount = 0;
    FarmStatus g_Status{};
    QuantSettings g_Settings{};
    int g_SettingsTab = 0;
    bool g_Loaded = false;
    bool g_LoadRequested = false;
    ServerKind g_ServerKind = ServerKind::Unknown;
    bool g_DetectRequested = false;
    ULONGLONG g_LastDetectAttempt = 0;
    ULONGLONG g_LastStatusRequest = 0;
    ULONGLONG g_LastLoadAttempt = 0;
    ULONGLONG g_LastStatusLog = 0;
    ULONGLONG g_AirDumpFetchAt = 0;
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
    char g_LastLoggedStatus[128]{};
    char g_RawCommand[192]{};
    std::uint32_t g_LastCriticalAdminJoinSerial = 0;
    bool g_AdminExitPending = false;
    ULONGLONG g_AdminExitDeadline = 0;
    bool g_AdminGuardCheckbox = false;
    std::uint64_t g_RuntimeGeneration = 0;
    bool g_WasRuntimeReady = false;

    void ResetRuntimeSession() noexcept
    {
        for (auto& pending : g_Pending)
            pending = {};
        g_PendingCount = 0;
        g_Loaded = false;
        g_LoadRequested = false;
        g_ServerKind = ServerKind::Unknown;
        g_DetectRequested = false;
        g_LastDetectAttempt = 0;
        g_LastStatusRequest = 0;
        // Allow immediate QueueLoad on next Ready edge (not 2s later).
        g_LastLoadAttempt = 0;
        g_LastStatusLog = 0;
        g_AirDumpFetchAt = 0;
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
        g_LastLoggedStatus[0] = 0;
        g_LastCriticalAdminJoinSerial = 0;
        g_AdminExitPending = false;
        g_AdminExitDeadline = 0;
        g_Status = {};
        g_Status.airMinutes = -1;
        lstrcpynA(
            g_Status.status,
            "Not loaded",
            static_cast<int>(sizeof(g_Status.status)));
    }

    int ParseInteger(const char* text) noexcept
    {
        if (!text || !text[0])
            return 0;
        return static_cast<int>(
            std::strtol(text, nullptr, 10));
    }

    ImVec4 AdminLevelColor(int level) noexcept
    {
        switch (level)
        {
        case 1:
            return ImVec4(0.35f, 0.88f, 0.45f, 1.f);
        case 2:
            return ImVec4(0.25f, 0.82f, 0.88f, 1.f);
        case 3:
            return ImVec4(0.32f, 0.58f, 1.f, 1.f);
        case 4:
            return ImVec4(0.67f, 0.42f, 1.f, 1.f);
        case 5:
            return ImVec4(1.f, 0.72f, 0.2f, 1.f);
        case 6:
            return ImVec4(1.f, 0.4f, 0.18f, 1.f);
        case 7:
            return ImVec4(1.f, 0.25f, 0.12f, 1.f);
        case 8:
            return ImVec4(1.f, 0.14f, 0.24f, 1.f);
        case 9:
            return ImVec4(0.96f, 0.2f, 0.62f, 1.f);
        default:
            return level >= 10
                ? ImVec4(0.78f, 0.28f, 1.f, 1.f)
                : ImVec4(0.62f, 0.62f, 0.68f, 1.f);
        }
    }

    bool SaveSettings() noexcept
    {
        // Nothing is written to disk in this build: the server tool settings
        // live in RAM and are persisted through the cloud config instead.
        (void)kSettingsPath;
        return true;
    }
    void LoadSettings() noexcept
    {
        g_Settings = QuantSettings{};
        (void)kSettingsPath;
    }
    int SplitTabs(
        char* line,
        char** fields,
        int capacity) noexcept
    {
        if (!line || !fields || capacity <= 0)
            return 0;
        int count = 0;
        fields[count++] = line;
        for (char* cursor = line;
             *cursor && count < capacity;
             ++cursor)
        {
            if (*cursor != '\t')
                continue;
            *cursor = 0;
            fields[count++] = cursor + 1;
        }
        return count;
    }

    bool IsAdminGuardEnabled() noexcept
    {
        return (g_Settings.adminGuardFlags &
            kAdminGuardExitFlag) != 0;
    }

    void QueueAdminGuardExit(
        std::uint32_t serial,
        int adminId,
        const char* adminName,
        int adminLevel) noexcept
    {
        // Only while Aquanaut bot is actually running (Start → ACTIVE).
        // Idle / menu-only sessions must not kill the game on admin join.
        if (!IsAdminGuardEnabled() ||
            !g_Status.active ||
            adminLevel < 5 ||
            g_AdminExitPending)
            return;

        char message[192]{};
        _snprintf(
            message,
            sizeof(message) - 1,
            "Level %d administrator %s [ID:%d] detected. "
            "Closing GTA.",
            adminLevel,
            adminName && adminName[0]
                ? adminName
                : "Unknown",
            adminId);
        message[sizeof(message) - 1] = 0;

        char logLine[256]{};
        _snprintf(
            logLine,
            sizeof(logLine) - 1,
            "[QuantTools] ADMIN_GUARD_EXIT serial=%u "
            "id=%d level=%d name='%s'",
            serial,
            adminId,
            adminLevel,
            adminName ? adminName : "");
        logLine[sizeof(logLine) - 1] = 0;
        hooks::HLog(logLine);
        Menu::PushNotification(
            "Admin Guard",
            message,
            false,
            1000);
        g_AdminExitPending = true;
        g_AdminExitDeadline = GetTickCount64() + 350;
    }

    void HandleCriticalAdminJoin(
        const FarmStatus& status) noexcept
    {
        if (!status.criticalAdminJoinSerial ||
            status.criticalAdminJoinSerial ==
                g_LastCriticalAdminJoinSerial)
            return;

        g_LastCriticalAdminJoinSerial =
            status.criticalAdminJoinSerial;
        QueueAdminGuardExit(
            status.criticalAdminJoinSerial,
            status.criticalAdminJoinId,
            status.criticalAdminJoinName,
            status.criticalAdminJoinLevel);
    }

    void ParseStatus(char* text) noexcept
    {
        if (!text || text[0] != 'S' ||
            text[1] != '\t')
            return;

        FarmStatus parsed{};
        parsed.airMinutes = -1;
        char* cursor = text;
        bool headerParsed = false;
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

            char* fields[16]{};
            const int count = SplitTabs(
                cursor,
                fields,
                16);
            if (count >= 13 &&
                fields[0][0] == 'S')
            {
                parsed.active =
                    ParseInteger(fields[1]) != 0;
                lstrcpynA(
                    parsed.status,
                    fields[2],
                    static_cast<int>(
                        sizeof(parsed.status)));
                parsed.tokenCaptured =
                    ParseInteger(fields[3]) != 0;
                parsed.version =
                    ParseInteger(fields[4]);
                parsed.boxes =
                    ParseInteger(fields[5]);
                parsed.totalBoxes =
                    ParseInteger(fields[6]);
                parsed.rounds =
                    ParseInteger(fields[7]);
                parsed.earned =
                    ParseInteger(fields[8]);
                parsed.perHour =
                    ParseInteger(fields[9]);
                parsed.airMinutes =
                    ParseInteger(fields[10]);
                parsed.adminDetector =
                    ParseInteger(fields[11]) != 0;
                parsed.adminEsp =
                    ParseInteger(fields[12]) != 0;
                parsed.farmModule =
                    count >= 15
                        ? ParseInteger(fields[14])
                        : 1;
                if (parsed.farmModule != 1 &&
                    parsed.farmModule != 2)
                    parsed.farmModule = 1;
                headerParsed = true;
            }
            else if (count >= 6 &&
                fields[0][0] == 'P')
            {
                parsed.lootPearl =
                    ParseInteger(fields[1]);
                parsed.lootStarfish =
                    ParseInteger(fields[2]);
                parsed.lootRapana =
                    ParseInteger(fields[3]);
                parsed.lootExpiryMs =
                    static_cast<std::int64_t>(
                        _strtoi64(fields[4], nullptr, 10));
                parsed.lootPricesStale =
                    ParseInteger(fields[5]) != 0;
            }
            else if (count >= 4 &&
                fields[0][0] == 'O' &&
                parsed.onlineAdminCount <
                    static_cast<int>(
                        sizeof(parsed.onlineAdmins) /
                        sizeof(parsed.onlineAdmins[0])))
            {
                OnlineAdminInfo& admin =
                    parsed.onlineAdmins[
                        parsed.onlineAdminCount++];
                admin.id = ParseInteger(fields[1]);
                lstrcpynA(
                    admin.name,
                    fields[2],
                    static_cast<int>(
                        sizeof(admin.name)));
                admin.level =
                    ParseInteger(fields[3]);
            }
            else if (count >= 8 &&
                fields[0][0] == 'A' &&
                parsed.adminCount <
                    static_cast<int>(
                        sizeof(parsed.admins) /
                        sizeof(parsed.admins[0])))
            {
                AdminInfo& admin =
                    parsed.admins[
                        parsed.adminCount++];
                admin.id = ParseInteger(fields[1]);
                lstrcpynA(
                    admin.name,
                    fields[2],
                    static_cast<int>(
                        sizeof(admin.name)));
                admin.level =
                    ParseInteger(fields[3]);
                admin.distance =
                    ParseInteger(fields[4]);
                admin.invisible =
                    ParseInteger(fields[5]) != 0;
                admin.spectating =
                    ParseInteger(fields[6]) != 0;
                admin.offDuty =
                    ParseInteger(fields[7]) != 0;
            }
            else if (count >= 5 &&
                fields[0][0] == 'J')
            {
                parsed.criticalAdminJoinSerial =
                    static_cast<std::uint32_t>(
                        ParseInteger(fields[1]));
                parsed.criticalAdminJoinId =
                    ParseInteger(fields[2]);
                lstrcpynA(
                    parsed.criticalAdminJoinName,
                    fields[3],
                    static_cast<int>(
                        sizeof(
                            parsed.criticalAdminJoinName)));
                parsed.criticalAdminJoinLevel =
                    ParseInteger(fields[4]);
            }
            cursor = next;
        }

        if (headerParsed)
        {
            parsed.updatedAt = GetTickCount64();
            HandleCriticalAdminJoin(parsed);
            g_Status = parsed;
            g_Loaded = true;
            if (parsed.updatedAt - g_LastStatusLog >= 5000 ||
                lstrcmpA(
                    parsed.status,
                    g_LastLoggedStatus) != 0)
            {
                char line[320]{};
                _snprintf(
                    line,
                    sizeof(line) - 1,
                    "[QuantTools] active=%d token=%d v=%d "
                    "boxes=%d total=%d rounds=%d earned=%d "
                    "admins_online=%d nearby=%d status='%s'",
                    parsed.active ? 1 : 0,
                    parsed.tokenCaptured ? 1 : 0,
                    parsed.version,
                    parsed.boxes,
                    parsed.totalBoxes,
                    parsed.rounds,
                    parsed.earned,
                    parsed.onlineAdminCount,
                    parsed.adminCount,
                    parsed.status);
                hooks::HLog(line);
                lstrcpynA(
                    g_LastLoggedStatus,
                    parsed.status,
                    static_cast<int>(
                        sizeof(g_LastLoggedStatus)));
                g_LastStatusLog = parsed.updatedAt;
            }
        }
    }

    bool TrackRequest(
        std::uint64_t id,
        RequestKind kind,
        const char* label,
        bool silent = false) noexcept
    {
        if (!id || g_PendingCount >= kMaxPending)
            return false;
        PendingRequest& pending =
            g_Pending[g_PendingCount++];
        pending = {};
        pending.id = id;
        pending.kind = kind;
        pending.timestamp = GetTickCount64();
        pending.silent = silent;
        lstrcpynA(
            pending.label,
            label ? label : "Quant Tools",
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
        if (!command || !command[0] ||
            !output || capacity < 128)
            return false;
        char escaped[512]{};
        int write = 0;
        for (int read = 0;
             command[read] &&
             write < static_cast<int>(
                 sizeof(escaped)) - 2;
             ++read)
        {
            const char value = command[read];
            if (value == '\\' || value == '\'')
                escaped[write++] = '\\';
            if (value == '\r' || value == '\n' ||
                value == '\t')
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
            "return global.__qtCommand ? "
            "global.__qtCommand('%s') : "
            "'QT_NOT_LOADED';",
            escaped);
        output[capacity - 1] = 0;
        return length > 0 && length < capacity;
    }

    void EnqueueCommand(
        const char* command,
        const char* label = nullptr,
        bool silent = false) noexcept
    {
#if !RAGEMP_DEV_BUILD
        if (command &&
            (lstrcmpA(command, "dump_air") == 0 ||
             lstrcmpiA(command, "dump_air") == 0 ||
             lstrcmpA(command, "dump_firma") == 0 ||
             lstrcmpiA(command, "dump_firma") == 0 ||
             lstrcmpA(command, "dump_cef_all") == 0 ||
             lstrcmpA(command, "dump_cef") == 0 ||
             lstrcmpA(command, "dump_cef_scripts") == 0))
        {
            // User build: never queue dump commands (no disk writes).
            return;
        }
#endif
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
            {
                Menu::PushNotification(
                    "Quant Tools",
                    "Unable to queue command.",
                    false);
            }
            return;
        }
#if RAGEMP_DEV_BUILD
        if (command &&
            (lstrcmpA(command, "dump_air") == 0 ||
             lstrcmpiA(command, "dump_air") == 0))
        {
            // Capture window is 20s in JS; fetch shortly after.
            g_AirDumpFetchAt = GetTickCount64() + 21500;
            hooks::HLog(
                "[QuantTools] air HUD dump capture started");
        }
        if (command &&
            (lstrcmpA(command, "dump_firma") == 0 ||
             lstrcmpiA(command, "dump_firma") == 0))
        {
            // JS fetches orders async then stores into cefDumpFiles.
            CloseCefDumpFile();
            g_CefDumpDraining = false;
            g_CefDumpActive = true;
            g_CefDumpFilesWritten = 0;
            g_CefDumpStartedAt = GetTickCount64();
            g_CefDumpFetchAt = GetTickCount64() + 1500;
            lstrcpynA(
                g_CefDumpStatus,
                "Firma orders dump...",
                sizeof(g_CefDumpStatus));
            hooks::HLog(
                "[QuantTools] firma orders dump started");
        }
        if (command &&
            (lstrcmpA(command, "dump_cef_all") == 0 ||
             lstrcmpA(command, "dump_cef") == 0 ||
             lstrcmpA(command, "dump_cef_scripts") == 0))
        {
            CloseCefDumpFile();
            g_CefDumpDraining = false;
            g_CefDumpActive = true;
            g_CefDumpFilesWritten = 0;
            g_CefDumpStartedAt = GetTickCount64();
            // Stream early: JS can drain files already assembled while CEF
            // still transfers the rest (old ~30MB dumps never fit a late bulk pull).
            // Give serialized CEF jobs time before Present-thread drain starts.
            g_CefDumpFetchAt = g_CefDumpStartedAt + 12000;
            lstrcpynA(
                g_CefDumpStatus,
                "Capturing CEF...",
                sizeof(g_CefDumpStatus));
            hooks::HLog("[QuantTools] CEF dump capture started (ESP paused)");
        }
#endif
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

    void CloseCefDumpFile() noexcept
    {
        if (g_CefDumpFile != INVALID_HANDLE_VALUE)
        {
            CloseHandle(g_CefDumpFile);
            g_CefDumpFile = INVALID_HANDLE_VALUE;
        }
        g_CefDumpFileName[0] = 0;
    }

    // Same rule as the Ukraine tools: the only function that could create the
    // dump file refuses, so the capture cannot reach the disk from any caller.
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
                ? "try{if(global.__qtCefDumpForce)global.__qtCefDumpForce();}"
                  "catch(e){}"
                  "return global.__qtCefDumpPull ? "
                  "global.__qtCefDumpPull() : 'CEF_IDLE:0';"
                : "return global.__qtCefDumpPull ? "
                  "global.__qtCefDumpPull() : 'CEF_IDLE:0';",
            RequestKind::CefDump,
            "CEF Dump Pull");
        if (!queued)
        {
            // Pending queue full / enqueue failed — retry shortly.
            ScheduleCefDumpRetry(600);
            hooks::HLog("[QuantTools] CEF dump pull queue full, retry");
        }
    }

    void HandleCefDumpResult(const char* text) noexcept
    {
#if !RAGEMP_DEV_BUILD
        (void)text;
#else
        if (!text || !text[0])
        {
            ScheduleCefDumpRetry(600);
            return;
        }

        if (strncmp(text, "CEF_BUSY:", 9) == 0)
        {
            // Still assembling from CEF — keep Present light.
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
                "CEF Dump",
                notify,
                g_CefDumpFilesWritten > 0,
                6000);
            char line[128]{};
            _snprintf(
                line,
                sizeof(line) - 1,
                "[QuantTools] CEF dump finished (%d files)",
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
        lstrcpynA(
            copy,
            text,
            static_cast<int>(sizeof(copy)));
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
            hooks::HLog("[QuantTools] CEF dump chunk truncated, retry");
            ScheduleCefDumpRetry(500);
            return;
        }

        char fileName[180]{};
        char chunk[RageV8Runtime::kMaxResultLength]{};
        DecodeUriComponent(parts[2], fileName, sizeof(fileName));
        const char* packed = parts[6] ? parts[6] : "";
        int chunkLen = 0;
        // R = raw ASCII payload, E = encodeURIComponent payload.
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
            // Backward compatible with older encoded-only pulls.
            chunkLen = DecodeUriComponent(
                packed, chunk, sizeof(chunk));
        }
        const int complete = ParseInteger(parts[5]);

        if (!EnsureCefDumpFile(fileName))
        {
            hooks::HLog("[QuantTools] CEF dump create file FAILED");
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

        // Slow Present-thread drain — V8 pull + WriteFile under ESP load
        // previously AV'd at GTA5+0x79eef6 mid bundle (part_006).
        g_CefDumpDraining = false;
        ScheduleCefDumpRetry(complete ? 850ull : 700ull);
#endif
    }

    void WriteAirHudDumpFile(const char* text) noexcept
    {
        // Dump files are a research artefact; this build produces none.
        (void)text;
    }

    void QueueAirDumpFetch() noexcept
    {
        QueueScript(
            "return global.__qtGetAirDump ? "
            "global.__qtGetAirDump() : 'AIRDUMP_EMPTY';",
            RequestKind::AirDump,
            "Air Dump Save");
    }

    void QueueSettingsSync() noexcept
    {
        char zone[128]{};
        int zoneWrite = 0;
        for (int index = 0;
             g_Settings.zoneFilter[index] &&
             zoneWrite < static_cast<int>(
                 sizeof(zone)) - 2;
             ++index)
        {
            const char value =
                g_Settings.zoneFilter[index];
            if (value == '\\' || value == '\'')
                zone[zoneWrite++] = '\\';
            zone[zoneWrite++] =
                (value == '\r' || value == '\n')
                ? ' '
                : value;
        }
        zone[zoneWrite] = 0;

        const int red = static_cast<int>(
            g_Settings.hitColor[0] * 255.f);
        const int green = static_cast<int>(
            g_Settings.hitColor[1] * 255.f);
        const int blue = static_cast<int>(
            g_Settings.hitColor[2] * 255.f);
        const int alpha = static_cast<int>(
            g_Settings.hitColor[3] * 255.f);
        char script[8192]{};
        _snprintf(
            script,
            sizeof(script) - 1,
            "if(!global.__qtCommand)return 'QT_NOT_LOADED';"
            "const q=global.__qtCommand;"
            "q('bot_version:%d');"
            "q('%s');q('%s');q('%s');"
            "q('zone_filter:%s');q('zone_max_dist:%d');"
            "q('%s');q('armour_interval:%d');"
            "q('auto_pickup_cfg:{\"enabled\":%s,"
            "\"mode\":\"%s\",\"allowed\":null,"
            "\"priority\":[],\"delay\":%d}');"
            "q('%s');q('%s');q('%s');q('%s');"
            "q('hitfx_cfg:{\"enabled\":%s,"
            "\"showNumbers\":%s,\"sound\":%s,"
            "\"headSound\":%s,\"outline\":%s,"
            "\"outlineWidth\":%d,\"shadow\":%s,"
            "\"originalColors\":%s,\"distScale\":%s,"
            "\"fontSize\":%.3f,\"fontId\":%d,"
            "\"stretchX\":%.3f,\"stretchY\":%.3f,"
            "\"duration\":%d,\"floatDist\":%.3f,"
            "\"spread\":%.3f,\"heightOffset\":%.3f,"
            "\"soundVol\":%.3f,"
            "\"color\":[%d,%d,%d,%d]}');"
            "q('uitheme_cfg:{\"enabled\":%s,\"opacity\":%.3f}');"
            "q('strafe_speed:%.2f');"
            "q('strafe_aimonly:%d');"
            "q('strafe_fwdback:%d');q('%s');"
            "q('ad_timing_cfg:{\"targetA\":%d,"
            "\"targetD\":%d,\"jitter\":%d,"
            "\"usefulDelay\":%d,\"aimOnly\":%s}');q('%s');"
            "q('ar_cfg:{\"strength\":%.3f,"
            "\"smoothing\":%.3f,\"jitter\":%.3f,"
            "\"aimOnly\":%s}');q('%s');"
            "q('heli_stab:%d');q('heli_stab_damp:%.3f');"
            "q('crawl_key:%d');q('cheatsheet_key:%d');"
            "q('cs_scale:%d');q('cs_opacity:%d');"
            "q('cs_dim:%d');q('cs_variant:%d');"
            "q('%s');q('%s');q('%s');"
            "return 'Quant settings synchronized';",
            2, // bot_version locked v4/v2-style
            g_Settings.safeTp
                ? "safe_tp_on" : "safe_tp_off",
            g_Settings.skipOccupied
                ? "box_skip_on" : "box_skip_off",
            g_Settings.orderMode == 1
                ? "order_mode_zone"
                : (g_Settings.orderMode == 0
                    ? "order_mode_price"
                    : "order_mode_last"),
            zone,
            g_Settings.zoneMaxDistance,
            g_Settings.armourBypass
                ? "armour_set:1" : "armour_set:0",
            g_Settings.armourInterval,
            g_Settings.autoPickup ? "true" : "false",
            g_Settings.autoPickupMode == 1
                ? "all" : "weapons",
            g_Settings.autoPickupDelay,
            g_Settings.antiFiring
                ? "antifiring_on" : "antifiring_off",
            g_Settings.noFalloff
                ? "sg_nofalloff_on" : "sg_nofalloff_off",
            g_Settings.noDrop
                ? "nodrop:on" : "nodrop:off",
            g_Settings.damageCounter
                ? "dmg_counter_on" : "dmg_counter_off",
            g_Settings.hitFx ? "true" : "false",
            g_Settings.hitNumbers ? "true" : "false",
            g_Settings.hitSound ? "true" : "false",
            g_Settings.headSound ? "true" : "false",
            g_Settings.hitOutline ? "true" : "false",
            g_Settings.outlineWidth,
            g_Settings.hitShadow ? "true" : "false",
            g_Settings.originalColors ? "true" : "false",
            g_Settings.distanceScale ? "true" : "false",
            g_Settings.hitFontSize,
            g_Settings.hitFont,
            g_Settings.hitStretchX,
            g_Settings.hitStretchY,
            g_Settings.hitDuration,
            g_Settings.hitFloatDistance,
            g_Settings.hitSpread,
            g_Settings.hitHeight,
            g_Settings.hitSoundVolume,
            red,
            green,
            blue,
            alpha,
            "false",
            g_Settings.uiThemeOpacity,
            g_Settings.strafeSpeed,
            g_Settings.strafeAimOnly ? 1 : 0,
            g_Settings.strafeForwardBack ? 1 : 0,
            g_Settings.strafe
                ? "strafe_on" : "strafe_off",
            g_Settings.adTargetA,
            g_Settings.adTargetD,
            g_Settings.adJitter,
            g_Settings.adUsefulDelay,
            g_Settings.adAimOnly ? "true" : "false",
            g_Settings.adTiming
                ? "ad_timing_on" : "ad_timing_off",
            g_Settings.recoilStrength,
            g_Settings.recoilSmoothing,
            g_Settings.recoilJitter,
            g_Settings.recoilAimOnly ? "true" : "false",
            g_Settings.antiRecoil
                ? "ar_on" : "ar_off",
            g_Settings.heliStabilizer ? 1 : 0,
            g_Settings.heliDamping,
            g_Settings.crawlKey,
            g_Settings.cheatSheetKey,
            g_Settings.cheatSheetScale,
            g_Settings.cheatSheetOpacity,
            g_Settings.cheatSheetDim,
            g_Settings.cheatSheetVariant,
            g_Settings.invisibility
                ? "invis_on" : "invis_off",
            g_Settings.adminDetector
                ? "admin_detect_on"
                : "admin_detect_off",
            g_Settings.adminEsp
                ? "admin_esp_on"
                : "admin_esp_off");
        script[sizeof(script) - 1] = 0;
        QueueScript(
            script,
            RequestKind::Command,
            "Quant settings");
    }

    bool ToggleCommand(
        const char* label,
        bool& value,
        const char* enabledCommand,
        const char* disabledCommand)
    {
        UNREFERENCED_PARAMETER(enabledCommand);
        UNREFERENCED_PARAMETER(disabledCommand);
        if (!gui->checkbox(label, &value))
            return false;
        return true;
    }

    // Fingerprint Quant RP via pause header / browser URL (set by their client).
    constexpr const char* kDetectScript =
        "return (function(){"
        "try{"
        "var h='';"
        "try{h=String(mp.game.gxt.get('PM_PAUSE_HDR')||'');}catch(e){}"
        "var s=h.toLowerCase();"
        "if(s.indexOf('quant')!==-1||s.indexOf('quant5')!==-1)return 'QUANT';"
        "try{"
        "if(typeof mp.browsers!=='undefined'&&mp.browsers.forEach){"
        "var hit=false;"
        "mp.browsers.forEach(function(b){"
        "try{var u=String(b.url||'').toLowerCase();"
        "if(u.indexOf('quant5')!==-1)hit=true;}catch(e){}"
        "});"
        "if(hit)return 'QUANT';"
        "}"
        "}catch(e){}"
        "return 'OTHER';"
        "}catch(e){return 'OTHER';}"
        "})();";

    void QueueDetectServer() noexcept
    {
        if (g_DetectRequested)
            return;
        g_LastDetectAttempt = GetTickCount64();
        if (!QueueScript(
                kDetectScript,
                RequestKind::Detect,
                "Detect Quant RP",
                true))
            return;
        g_DetectRequested = true;
    }

    void QueueLoad() noexcept
    {
        if (g_LoadRequested)
            return;
        if (g_ServerKind != ServerKind::Quant)
            return;
        g_LastLoadAttempt = GetTickCount64();
        if (!QueueScript(
                QuantToolsScript::kSource,
                RequestKind::Load,
                "Load Quant Tools"))
        {
            Menu::PushNotification(
                "Quant Tools",
                "Runtime is not ready or the command queue is full.",
                false);
            return;
        }
        g_LoadRequested = true;
    }

    bool CommandButton(
        const char* label,
        const char* command,
        float width = 108.f)
    {
        if (!gui->button(
                label,
                ImVec2(width, 30.f)))
            return false;
        EnqueueCommand(command, label);
        return true;
    }

    void SameLineIfRoom(float nextWidth = 116.f)
    {
        if (ImGui::GetContentRegionAvail().x >
            nextWidth)
            ImGui::SameLine();
    }

    void RenderTokenStatusRow(bool runtimeReady)
    {
        const ImVec2 position =
            ImGui::GetCursorScreenPos();
        const ImVec2 size(
            ImGui::GetContentRegionAvail().x,
            44.f);
        const bool captured =
            g_Loaded && g_Status.tokenCaptured;
        const ImVec4 stateColor = captured
            ? ImVec4(0.25f, 0.9f, 0.45f, 1.f)
            : ImVec4(1.f, 0.68f, 0.2f, 1.f);
        ImDrawList* draw =
            ImGui::GetWindowDrawList();
        draw->AddRectFilled(
            position,
            position + size,
            gui->get_clr(clr->child_bottom_bar),
            set->selection_rounding);
        draw->AddRect(
            position,
            position + size,
            gui->get_clr(clr->child_stroke_bar),
            set->selection_rounding);
        draw->AddCircleFilled(
            position + ImVec2(18.f, 22.f),
            5.f,
            gui->get_clr(stateColor));
        gui->render_text_clipped(
            set->poppins_selection,
            position + ImVec2(32.f, 0.f),
            ImVec2(position.x + 210.f, position.y + 44.f),
            gui->get_clr(clr->text_active),
            "Session Token",
            {0.f, 0.5f});
        const char* state = captured
            ? "CAPTURED"
            : (!runtimeReady
#if RAGEMP_DEV_BUILD
                ? "WAITING FOR V8"
#else
                ? "WAITING"
#endif
                : (g_Loaded
                    ? "NOT CAPTURED"
                    : "LOADING MODULE"));
        gui->render_text_clipped(
            set->poppins_widget,
            ImVec2(position.x + 210.f, position.y),
            position + size - ImVec2(12.f, 0.f),
            gui->get_clr(stateColor),
            state,
            {1.f, 0.5f});
        ImGui::Dummy(size);
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
                SaveSettings();
            }
            if (WidgetIconButton(
                    "close",
                    ICON_FA_XMARK,
                    ImVec2(
                        position.x + size.x - 34.f,
                        position.y + 6.f)))
            {
                visible = false;
                SaveSettings();
            }
        }
        ImGui::SetCursorScreenPos(
            position + ImVec2(16.f, kWidgetHeaderH + 12.f));
    }

    void RenderFarmWidget()
    {
        if (!g_Settings.showAquanautWidget)
            return;
        const bool firma =
            g_Status.farmModule == 2;
        ImGui::SetNextWindowPos(
            ImVec2(18.f, 110.f),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(
            ImVec2(330.f, firma ? 292.f : 338.f),
            ImGuiCond_Always);
        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar;
        if (g_Settings.pinAquanautWidget)
            flags |= ImGuiWindowFlags_NoMove;
        if (!Menu::IsVisible())
            flags |= ImGuiWindowFlags_NoInputs;

        const float appear =
            WidgetAppearAlpha(0xA01A001u);
        ImGui::SetNextWindowBgAlpha(0.92f * appear);
        PushWidgetStyle();
        ImGui::PushStyleVar(
            ImGuiStyleVar_Alpha,
            appear);
        if (ImGui::Begin(
                "##quant_farm_widget",
                nullptr,
                flags))
        {
            RenderWidgetHeader(
                "Farm",
                g_Settings.showAquanautWidget,
                g_Settings.pinAquanautWidget);

            const ImVec4 activeColor =
                g_Status.active
                    ? ImVec4(0.3f, 0.9f, 0.45f, 1.f)
                    : ImVec4(0.72f, 0.72f, 0.78f, 1.f);
            DrawStatusPill(
                g_Status.active ? "ACTIVE" : "IDLE",
                activeColor,
                g_Status.active);
            ImGui::SameLine(0.f, 8.f);
            const ImVec4 modColor =
                firma
                    ? ImVec4(0.95f, 0.72f, 0.28f, 1.f)
                    : ImVec4(0.45f, 0.75f, 0.98f, 1.f);
            DrawStatusPill(
                firma ? "FIRMA" : "AQUANAUT",
                modColor,
                g_Status.active);
            ImGui::SameLine(0.f, 8.f);
            ImGui::SetCursorPosY(
                ImGui::GetCursorPosY() + 3.f);
            if (firma)
            {
                ImGui::TextDisabled(
                    "token %s",
                    g_Status.tokenCaptured
                        ? "ok"
                        : "—");
            }
            else
            {
                ImGui::TextDisabled(
                    "token %s  ·  v%d",
                    g_Status.tokenCaptured
                        ? "ok"
                        : "—",
                    g_Status.version);
            }

            ImGui::Spacing();
            const bool guardArmed =
                IsAdminGuardEnabled() && g_Status.active;
            const ImVec4 guardColor =
                !IsAdminGuardEnabled()
                    ? ImVec4(0.55f, 0.55f, 0.6f, 1.f)
                    : (g_Status.active
                        ? ImVec4(0.95f, 0.62f, 0.18f, 1.f)
                        : ImVec4(0.45f, 0.78f, 0.95f, 1.f));
            const char* guardLabel =
                !IsAdminGuardEnabled()
                    ? "GUARD OFF"
                    : (g_Status.active
                        ? "GUARD ARMED"
                        : "GUARD ON");
            DrawStatusPill(
                guardLabel,
                guardColor,
                guardArmed);
            ImGui::SameLine(0.f, 8.f);
            ImGui::SetCursorPosY(
                ImGui::GetCursorPosY() + 3.f);
            ImGui::TextDisabled(
                "%s",
                !IsAdminGuardEnabled()
                    ? "admin exit disabled"
                    : (g_Status.active
                        ? "level 5+ exit ready"
                        : "waiting for bot"));

            ImGui::Spacing();
            DrawInsetPanel(g_Status.status, 44.f);
            ImGui::Spacing();

            const float gap = 8.f;
            const float chipW =
                (ImGui::GetContentRegionAvail().x - gap) *
                0.5f;
            const ImVec2 chipSize(chipW, 52.f);
            char aBuf[32]{};
            char bBuf[48]{};
            char cBuf[32]{};
            char dBuf[32]{};

            if (firma)
            {
                std::snprintf(
                    aBuf, sizeof(aBuf), "%d",
                    g_Status.boxes);
                std::snprintf(
                    bBuf, sizeof(bBuf), "%s",
                    g_Status.tokenCaptured
                        ? "ready"
                        : "need token");
                std::snprintf(
                    cBuf, sizeof(cBuf), "%s",
                    g_Status.active ? "RUN" : "STOP");
                std::snprintf(
                    dBuf, sizeof(dBuf), "%s",
                    "under TP");

                const ImVec2 row0 =
                    ImGui::GetCursorScreenPos();
                DrawMetricChip(
                    "deliv", "Deliveries", aBuf, chipSize);
                ImGui::SetCursorScreenPos(
                    row0 + ImVec2(chipW + gap, 0.f));
                DrawMetricChip(
                    "tok", "Session", bBuf, chipSize);
                ImGui::SetCursorScreenPos(
                    row0 + ImVec2(0.f, chipSize.y + gap));
                const ImVec2 row1 =
                    ImGui::GetCursorScreenPos();
                DrawMetricChip(
                    "state", "Bot", cBuf, chipSize);
                ImGui::SetCursorScreenPos(
                    row1 + ImVec2(chipW + gap, 0.f));
                DrawMetricChip(
                    "mode", "Mode", dBuf, chipSize);
                ImGui::SetCursorScreenPos(
                    row1 + ImVec2(0.f, chipSize.y + gap));
                ImGui::TextDisabled(
                    "Firma · trailer success → pay");
            }
            else
            {
                std::snprintf(
                    aBuf, sizeof(aBuf), "%d",
                    g_Status.boxes);
                std::snprintf(
                    bBuf, sizeof(bBuf), "%d / %d",
                    g_Status.totalBoxes,
                    g_Status.rounds);
                std::snprintf(
                    cBuf, sizeof(cBuf), "$%d",
                    g_Status.earned);
                std::snprintf(
                    dBuf, sizeof(dBuf), "$%d/h",
                    g_Status.perHour);

                const ImVec2 row0 =
                    ImGui::GetCursorScreenPos();
                DrawMetricChip(
                    "boxes", "Round boxes", aBuf, chipSize);
                ImGui::SetCursorScreenPos(
                    row0 + ImVec2(chipW + gap, 0.f));
                DrawMetricChip(
                    "total", "Total / Rounds", bBuf, chipSize);
                ImGui::SetCursorScreenPos(
                    row0 + ImVec2(0.f, chipSize.y + gap));
                const ImVec2 row1 =
                    ImGui::GetCursorScreenPos();
                DrawMetricChip(
                    "earned", "Earned", cBuf, chipSize);
                ImGui::SetCursorScreenPos(
                    row1 + ImVec2(chipW + gap, 0.f));
                DrawMetricChip(
                    "rate", "Rate", dBuf, chipSize);
                ImGui::SetCursorScreenPos(
                    row1 + ImVec2(0.f, chipSize.y + gap));

                char airBuf[48]{};
                if (g_Status.airMinutes >= 0)
                {
                    std::snprintf(
                        airBuf,
                        sizeof(airBuf),
                        "Air tracked · %d min",
                        g_Status.airMinutes);
                }
                else
                {
                    std::snprintf(
                        airBuf,
                        sizeof(airBuf),
                        "Air · unknown");
                }
                ImGui::TextDisabled("%s", airBuf);

                char lootBuf[96]{};
                const int pearl =
                    g_Status.lootPearl >= 0
                        ? g_Status.lootPearl
                        : 10038;
                const int star =
                    g_Status.lootStarfish >= 0
                        ? g_Status.lootStarfish
                        : 750;
                const int rapana =
                    g_Status.lootRapana >= 0
                        ? g_Status.lootRapana
                        : 350;
                const bool anyLive =
                    g_Status.lootPearl >= 0 ||
                    g_Status.lootStarfish >= 0 ||
                    g_Status.lootRapana >= 0;
                if (!anyLive)
                {
                    std::snprintf(
                        lootBuf,
                        sizeof(lootBuf),
                        "Loot $ · open junk seller");
                }
                else if (g_Status.lootPricesStale)
                {
                    std::snprintf(
                        lootBuf,
                        sizeof(lootBuf),
                        "P %d  S %d  R %d · stale",
                        pearl,
                        star,
                        rapana);
                }
                else
                {
                    int remSec = 0;
                    if (g_Status.lootExpiryMs > 0)
                    {
                        const auto nowMs =
                            static_cast<std::int64_t>(
                                std::chrono::duration_cast<
                                    std::chrono::milliseconds>(
                                    std::chrono::system_clock::now()
                                        .time_since_epoch())
                                    .count());
                        if (g_Status.lootExpiryMs > nowMs)
                            remSec = static_cast<int>(
                                (g_Status.lootExpiryMs - nowMs) /
                                1000);
                    }
                    if (remSec > 0)
                    {
                        const int mm = remSec / 60;
                        const int ss = remSec % 60;
                        std::snprintf(
                            lootBuf,
                            sizeof(lootBuf),
                            "P %d  S %d  R %d · %d:%02d",
                            pearl,
                            star,
                            rapana,
                            mm,
                            ss);
                    }
                    else
                    {
                        std::snprintf(
                            lootBuf,
                            sizeof(lootBuf),
                            "P %d  S %d  R %d",
                            pearl,
                            star,
                            rapana);
                    }
                }
                ImGui::TextDisabled("%s", lootBuf);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
        PopWidgetStyle();
    }

    void RenderAdminWidget()
    {
        if (!g_Settings.showAdminWidget)
            return;
        const ImVec2 display =
            ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(
            ImVec2(display.x - 375.f, 110.f),
            ImGuiCond_FirstUseEver);
        const int adminRows =
            g_Status.onlineAdminCount > 0
                ? g_Status.onlineAdminCount
                : 1;
        float adminWidgetHeight =
            118.f + static_cast<float>(adminRows) * 46.f;
        const float maxAdminWidgetHeight =
            display.y > 290.f
                ? display.y - 130.f
                : 160.f;
        if (adminWidgetHeight < 170.f)
            adminWidgetHeight = 170.f;
        if (adminWidgetHeight > maxAdminWidgetHeight)
            adminWidgetHeight = maxAdminWidgetHeight;
        ImGui::SetNextWindowSize(
            ImVec2(355.f, adminWidgetHeight),
            ImGuiCond_Always);
        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings;
        if (g_Settings.pinAdminWidget)
            flags |= ImGuiWindowFlags_NoMove;
        if (!Menu::IsVisible())
            flags |= ImGuiWindowFlags_NoInputs;

        const float appear =
            WidgetAppearAlpha(0xA01A002u);
        ImGui::SetNextWindowBgAlpha(0.92f * appear);
        PushWidgetStyle();
        ImGui::PushStyleVar(
            ImGuiStyleVar_Alpha,
            appear);
        if (ImGui::Begin(
                "##quant_admin_widget",
                nullptr,
                flags))
        {
            RenderWidgetHeader(
                "Script Admin Detector",
                g_Settings.showAdminWidget,
                g_Settings.pinAdminWidget);

            const ImVec4 scanColor =
                g_Status.adminDetector
                    ? ImVec4(0.3f, 0.9f, 0.45f, 1.f)
                    : ImVec4(0.9f, 0.55f, 0.3f, 1.f);
            DrawStatusPill(
                g_Status.adminDetector
                    ? "SCANNING"
                    : "DISABLED",
                scanColor,
                g_Status.adminDetector);
            ImGui::SameLine(0.f, 8.f);
            ImGui::SetCursorPosY(
                ImGui::GetCursorPosY() + 3.f);
            ImGui::TextDisabled(
                "%d online  ·  %d nearby",
                g_Status.onlineAdminCount,
                g_Status.adminCount);

            ImGui::Spacing();

            if (!g_Status.onlineAdminCount)
            {
                DrawInsetPanel(
                    "No administrators are online.",
                    40.f);
            }

            for (int index = 0;
                 index < g_Status.onlineAdminCount;
                 ++index)
            {
                const OnlineAdminInfo& online =
                    g_Status.onlineAdmins[index];
                const AdminInfo* nearby = nullptr;
                for (int nearbyIndex = 0;
                     nearbyIndex < g_Status.adminCount;
                     ++nearbyIndex)
                {
                    if (g_Status.admins[nearbyIndex].id ==
                        online.id)
                    {
                        nearby =
                            &g_Status.admins[nearbyIndex];
                        break;
                    }
                }
                const ImVec4 color =
                    AdminLevelColor(online.level);
                const ImVec2 row =
                    ImGui::GetCursorScreenPos();
                const float rowWidth =
                    ImGui::GetContentRegionAvail().x;
                const float rowH = 42.f;

                ImGui::PushID(online.id);
                ImGui::InvisibleButton(
                    "##admin_row",
                    ImVec2(rowWidth, rowH));
                const bool hovered =
                    ImGui::IsItemHovered();
                float& hot =
                    LEYAWIN_ANIM(ImGuiID, float)
                        .get(ImGui::GetItemID());
                hot = ImLerp(
                    hot,
                    hovered ? 1.f : 0.f,
                    gui->fixed_speed(14.f));
                ImGui::PopID();

                ImDrawList* draw =
                    ImGui::GetWindowDrawList();
                const ImVec4 rowBg = ImLerp(
                    clr->child_bottom_bar,
                    clr->element_background,
                    hot * 0.85f);
                draw->AddRectFilled(
                    row,
                    row + ImVec2(rowWidth, rowH),
                    gui->get_clr(rowBg),
                    kWidgetRowRounding);
                draw->AddRect(
                    row,
                    row + ImVec2(rowWidth, rowH),
                    gui->get_clr(
                        clr->child_stroke_bar,
                        0.4f + 0.35f * hot),
                    kWidgetRowRounding);
                draw->AddRectFilled(
                    ImVec2(row.x + 2.f, row.y + 8.f),
                    ImVec2(row.x + 5.f, row.y + rowH - 8.f),
                    gui->get_clr(color, 0.7f + 0.3f * hot),
                    2.f);

                ImGui::SetCursorScreenPos(
                    row + ImVec2(12.f, 4.f));
                ImGui::TextColored(
                    color,
                    "%s [ID:%d]",
                    online.name,
                    online.id);
                ImGui::SetCursorScreenPos(
                    row + ImVec2(12.f, 22.f));
                ImGui::TextColored(
                    color,
                    "Level %d",
                    online.level);
                ImGui::SameLine(0.f, 0.f);
                ImGui::TextDisabled(
                    "%s%s%s%s",
                    nearby
                        ? " · nearby "
                        : " · not streamed",
                    nearby
                        ? (nearby->offDuty
                            ? "OFF-DUTY "
                            : "")
                        : "",
                    nearby && nearby->invisible
                        ? "INVISIBLE "
                        : "",
                    nearby && nearby->spectating
                        ? "SPECTATING"
                        : "");
                if (nearby)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled(
                        "%dm",
                        nearby->distance);
                }
                ImGui::SetCursorScreenPos(
                    row + ImVec2(0.f, rowH + 4.f));
            }
        }
        ImGui::End();
        ImGui::PopStyleVar();
        PopWidgetStyle();
    }
}

namespace QuantTools
{
    void QueueCommand(
        const char* command,
        const char* label,
        bool silent) noexcept
    {
        if (g_ServerKind != ServerKind::Quant)
            return;
        EnqueueCommand(command, label, silent);
    }

    void Initialize() noexcept
    {
        LoadSettings();
        g_AdminGuardCheckbox =
            IsAdminGuardEnabled();
        g_RuntimeGeneration = 0;
        ResetRuntimeSession();
    }

    bool IsCefDumpBusy() noexcept
    {
        return g_CefDumpActive ||
            g_CefDumpDraining ||
            g_CefDumpFetchAt != 0;
    }

    bool IsQuantServer() noexcept
    {
        return g_ServerKind == ServerKind::Quant;
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
                "[QuantTools] V8 generation changed; reloading");
        }
        if (g_AdminExitPending)
        {
            // Bot stopped after arming — cancel the kill.
            if (!g_Status.active)
            {
                g_AdminExitPending = false;
                g_AdminExitDeadline = 0;
                hooks::HLog(
                    "[QuantTools] Admin Guard exit cancelled "
                    "(aquanaut bot inactive)");
            }
            else if (now >= g_AdminExitDeadline)
            {
                hooks::HLog(
                    "[QuantTools] Admin Guard controlled exit");
                TerminateProcess(
                    GetCurrentProcess(),
                    0xAD05);
                return;
            }
        }

        const bool runtimeReady =
            runtime.state == RageV8Runtime::State::Ready &&
            runtime.rootCaptured;
        if (!runtimeReady)
        {
            g_Loaded = false;
            g_LoadRequested = false;
            g_DetectRequested = false;
            // Keep last g_ServerKind so the Quant tab does not flicker
            // during short V8 blips; full reset is on generation change.
            g_PendingCount = 0;
            g_WasRuntimeReady = false;
            return;
        }

        // Ready edge after fault / generation change → reload immediately.
        if (!g_WasRuntimeReady)
        {
            g_WasRuntimeReady = true;
            g_LastLoadAttempt = 0;
            g_LoadRequested = false;
            g_LastDetectAttempt = 0;
            g_DetectRequested = false;
            g_ServerKind = ServerKind::Unknown;
        }

        // Soft-reconnect key: do not enqueue more JS while CEF tears V8 down.
        if ((GetAsyncKeyState(VK_F1) & 0x8000) != 0)
            return;

        int write = 0;
        bool synchronizeSettings = false;
        for (int index = 0;
             index < g_PendingCount;
             ++index)
        {
            if (now - g_Pending[index].timestamp >= 3500)
            {
                if (g_Pending[index].kind == RequestKind::CefDump)
                {
                    // V8 can stall during huge CEF transfers; never leave drain stuck.
                    hooks::HLog(
                        "[QuantTools] CEF dump pull timeout, retry");
                    ScheduleCefDumpRetry(700);
                    continue;
                }
                if (g_Pending[index].kind == RequestKind::Detect)
                {
                    g_DetectRequested = false;
                    continue;
                }
                if (g_Pending[index].kind == RequestKind::Status ||
                    g_Pending[index].kind == RequestKind::Load)
                {
                    g_Loaded = false;
                    g_LoadRequested = false;
                }
                continue;
            }

            RageV8Runtime::Result result{};
            if (!RageV8Runtime::PollResultFor(
                    g_Pending[index].id,
                    result))
            {
                if (write != index)
                    g_Pending[write] =
                        g_Pending[index];
                ++write;
                continue;
            }

            const PendingRequest request =
                g_Pending[index];
            if (request.kind == RequestKind::Status)
            {
                if (result.success &&
                    result.text[0] == 'S' &&
                    result.text[1] == '\t')
                {
                    ParseStatus(result.text);
                }
                else
                {
                    g_Loaded = false;
                    g_LoadRequested = false;
                }
                continue;
            }

            if (request.kind == RequestKind::Detect)
            {
                g_DetectRequested = false;
                const bool isQuant =
                    result.success &&
                    result.text[0] &&
                    (strncmp(result.text, "QUANT", 5) == 0);
                const ServerKind next =
                    isQuant ? ServerKind::Quant : ServerKind::Other;
                if (g_ServerKind != next)
                {
                    hooks::HLog(
                        isQuant
                            ? "[QuantTools] server detected: Quant RP"
                            : "[QuantTools] server detected: other");
                }
                g_ServerKind = next;
                if (!isQuant)
                {
                    g_Loaded = false;
                    g_LoadRequested = false;
                }
                continue;
            }

            if (request.kind == RequestKind::Load)
            {
                g_LoadRequested = false;
                g_Loaded = result.success;
                if (result.success)
                    g_LastCriticalAdminJoinSerial = 0;
                hooks::HLog(
                    result.success
                    ? "[QuantTools] embedded module loaded"
                    : "[QuantTools] embedded module load failed");
                synchronizeSettings = result.success;
            }
            else if (request.kind == RequestKind::AirDump)
            {
#if RAGEMP_DEV_BUILD
                if (result.success && result.text[0])
                {
                    const char* body = result.text;
                    if (strncmp(body, "AIRDUMP\n", 8) == 0)
                        body += 8;
                    else if (
                        strncmp(
                            body,
                            "AIRDUMP_BUSY\n",
                            13) == 0)
                    {
                        // Still capturing — retry soon.
                        g_AirDumpFetchAt =
                            GetTickCount64() + 1500;
                        hooks::HLog(
                            "[QuantTools] air dump still busy, retry");
                        continue;
                    }
                    WriteAirHudDumpFile(body);
                    Menu::PushNotification(
                        "Air Dump",
                        "Saved C:\\RAGEMP\\air_hud_dump.txt",
                        true,
                        5000);
                }
                else
                {
                    Menu::PushNotification(
                        "Air Dump",
                        "Dump empty or failed",
                        false,
                        5000);
                }
#endif
                continue;
            }
            else if (request.kind == RequestKind::CefDump)
            {
#if RAGEMP_DEV_BUILD
                if (result.success && result.text[0])
                    HandleCefDumpResult(result.text);
                else
                {
                    hooks::HLog(
                        "[QuantTools] CEF dump pull failed, retry");
                    ScheduleCefDumpRetry(700);
                    lstrcpynA(
                        g_CefDumpStatus,
                        "Pull retry...",
                        sizeof(g_CefDumpStatus));
                }
#endif
                continue;
            }
            // Misc slider sync and other silent commands must not toast.
            if (request.silent)
                continue;
            if (request.kind == RequestKind::Command &&
                result.success &&
                result.text[0] &&
                (strncmp(result.text, "accepted:", 9) == 0) &&
                (strstr(result.text, "misc_") != nullptr ||
                 strncmp(request.label, "misc_", 5) == 0))
                continue;
            Menu::PushNotification(
                request.label,
                result.text[0]
                    ? result.text
                    : "<empty result>",
                result.success,
                result.success ? 3500 : 7000);
        }
        g_PendingCount = write;
        if (synchronizeSettings)
            QueueSettingsSync();

        if (g_AirDumpFetchAt != 0 &&
            now >= g_AirDumpFetchAt &&
            g_Loaded)
        {
#if RAGEMP_DEV_BUILD
            g_AirDumpFetchAt = 0;
            QueueAirDumpFetch();
#else
            g_AirDumpFetchAt = 0;
#endif
        }

        if (g_CefDumpFetchAt != 0 &&
            now >= g_CefDumpFetchAt &&
            g_Loaded &&
            !g_CefDumpDraining)
        {
#if RAGEMP_DEV_BUILD
            g_CefDumpFetchAt = 0;
            g_CefDumpDraining = true;
            // Force only as a last resort after 5 minutes.
            const bool force =
                g_CefDumpStartedAt != 0 &&
                now >= g_CefDumpStartedAt + 300000;
            QueueCefDumpPull(force);
            lstrcpynA(
                g_CefDumpStatus,
                force ? "Force saving..." : "Saving...",
                sizeof(g_CefDumpStatus));
#else
            g_CefDumpFetchAt = 0;
            g_CefDumpDraining = false;
#endif
        }

        // Probe server identity before injecting Quant Tools.
        {
            ULONGLONG detectInterval = 1000;
            if (g_ServerKind == ServerKind::Quant)
                detectInterval = 15000;
            else if (g_ServerKind == ServerKind::Other)
                detectInterval = 4000;
            if (!g_DetectRequested &&
                (g_LastDetectAttempt == 0 ||
                 now - g_LastDetectAttempt >= detectInterval))
            {
                QueueDetectServer();
            }
        }

        // First attempt immediately after Quant detect; retries every 2s on failure.
        if (g_ServerKind == ServerKind::Quant &&
            !g_Loaded &&
            !g_LoadRequested &&
            runtime.state ==
                RageV8Runtime::State::Ready &&
            runtime.rootCaptured &&
            (g_LastLoadAttempt == 0 ||
             now - g_LastLoadAttempt >= 2000))
        {
            QueueLoad();
        }
        if (g_Loaded && g_ServerKind == ServerKind::Quant)
        {
            const ULONGLONG statusInterval =
                (g_CefDumpActive || g_CefDumpDraining ||
                 g_CefDumpFetchAt != 0)
                    ? 5000ull
                    : 500ull;
            if (now - g_LastStatusRequest >= statusInterval)
            {
                bool hasStatusPending = false;
                for (int index = 0;
                     index < g_PendingCount;
                     ++index)
                {
                    if (g_Pending[index].kind ==
                        RequestKind::Status)
                    {
                        hasStatusPending = true;
                        break;
                    }
                }
                if (!hasStatusPending)
                {
                    g_LastStatusRequest = now;
                    QueueScript(
                        "return global.__qtGetStatus ? "
                        "global.__qtGetStatus() : "
                        "'QT_NOT_LOADED';",
                        RequestKind::Status,
                        "Quant status");
                }
            }
        }
    }

    void RenderUI()
    {
        const auto runtime =
            RageV8Runtime::GetStatus();
        const bool runtimeReady =
            runtime.state ==
            RageV8Runtime::State::Ready;

        ImGui::Text("Quant Tools");
        ImGui::SameLine();
        {
            const char* stateText = "Detecting server...";
            ImVec4 stateColor(0.95f, 0.58f, 0.25f, 1.f);
            if (g_ServerKind == ServerKind::Other)
            {
                stateText = "Not Quant RP";
                stateColor = ImVec4(0.85f, 0.35f, 0.35f, 1.f);
            }
            else if (g_Loaded)
            {
                stateText = "Loaded · Quant RP";
                stateColor = ImVec4(0.3f, 0.9f, 0.45f, 1.f);
            }
            else if (g_LoadRequested)
            {
                stateText = "Loading...";
            }
            else if (g_ServerKind == ServerKind::Quant)
            {
                stateText = "Quant RP · waiting inject";
            }
            ImGui::TextColored(stateColor, "%s", stateText);
        }
        ImGui::TextDisabled(
            "Local-only embedded build; excluded network, "
            "roulette, radar, and map components.");

        RenderTokenStatusRow(runtimeReady);
        ImGui::Separator();

        if (!g_Loaded)
        {
#if RAGEMP_DEV_BUILD
            ImGui::TextWrapped(
                "Quant Tools loads automatically as soon as the "
                "RAGE V8 runtime is ready. The token hook is "
                "installed automatically with the module.");
#else
            ImGui::TextWrapped(
                "Quant Tools loads automatically when the "
                "session is ready.");
#endif
            return;
        }

        // Stable indices: Aquanaut/Firma/Combat/Movement/Admin/Tools[/Dev].
#if RAGEMP_DEV_BUILD
        static const char* kTabs[] = {
            "Aquanaut##qt", "Firma##qt", "Combat##qt", "Movement##qt",
            "Admin##qt", "Tools##qt", "Dev##qt"
        };
        static const char* kTabIcons[] = {
            ICON_FA_SHIP,
            ICON_FA_TRUCK,
            ICON_FA_CROSSHAIRS,
            ICON_FA_PERSON_RUNNING,
            ICON_FA_USER_SHIELD,
            ICON_FA_TOOLBOX,
            ICON_FA_DOWNLOAD
        };
        constexpr int kMaxTab = 6;
#else
        static const char* kTabs[] = {
            "Aquanaut##qt", "Firma##qt", "Combat##qt", "Movement##qt",
            "Admin##qt", "Tools##qt"
        };
        static const char* kTabIcons[] = {
            ICON_FA_SHIP,
            ICON_FA_TRUCK,
            ICON_FA_CROSSHAIRS,
            ICON_FA_PERSON_RUNNING,
            ICON_FA_USER_SHIELD,
            ICON_FA_TOOLBOX
        };
        constexpr int kMaxTab = 5;
#endif
        const float tabSpacing =
            ImGui::GetStyle().ItemSpacing.x;
        const float tabWidth =
            (ImGui::GetContentRegionAvail().x -
                tabSpacing *
                    (IM_ARRAYSIZE(kTabs) - 1)) /
            IM_ARRAYSIZE(kTabs);
        if (g_SettingsTab < 0 || g_SettingsTab > kMaxTab)
            g_SettingsTab = 0;
        ImGui::PushID("QuantToolsUI");
        gui->begin_group();
        for (int index = 0;
             index < IM_ARRAYSIZE(kTabs);
             ++index)
        {
            gui->sub_selection(
                kTabIcons[index],
                kTabs[index],
                index,
                g_SettingsTab,
                ImVec2(tabWidth, 38.f));
            if (index + 1 < IM_ARRAYSIZE(kTabs))
                gui->sameline();
        }
        gui->end_group();
        gui->separator();

        bool dirty = false;
        const float width =
            (ImGui::GetContentRegionAvail().x - 10.f) *
            0.5f;
        const float height =
            ImGui::GetContentRegionAvail().y;
        static const char* kOrderModes[] = {
            "Highest price", "Zone", "Last tablet order"
        };
        static const char* kPickupModes[] = {
            "Weapons only", "All items"
        };
        static const char* kHitFonts[] = {
            "Chalet", "Script", "Monospace", "Chalet Comprime",
            "Pricedown"
        };

        if (g_SettingsTab == 0)
        {
            gui->begin_group();
            gui->begin_child(
                "Q",
                "Aquanaut Bot",
                ImVec2(width, height));
            CommandButton("Start", "start", 100.f);
            gui->sameline();
            CommandButton("Stop", "stop", 100.f);
            gui->sameline();
            CommandButton("Load Air", "air", 100.f);
            CommandButton("Reset Stats", "reset", 100.f);
            gui->sameline();
            CommandButton("Eat + Drink", "eat", 100.f);
            ImGui::TextDisabled(
                "Aquanaut v4 — v2 surface + last order");
            g_Settings.botVersion = 2;
            dirty |= ToggleCommand(
                "Safe TP",
                g_Settings.safeTp,
                "safe_tp_on",
                "safe_tp_off");
            dirty |= ToggleCommand(
                "Skip Occupied Boxes",
                g_Settings.skipOccupied,
                "box_skip_on",
                "box_skip_off");
            dirty |= ToggleCommand(
                "Server Invisibility",
                g_Settings.invisibility,
                "invis_on",
                "invis_off");
            if (gui->checkbox(
                    "Exit on Admin Level 5+ (while bot active)",
                    &g_AdminGuardCheckbox))
            {
                if (g_AdminGuardCheckbox)
                {
                    g_Settings.adminGuardFlags |=
                        kAdminGuardExitFlag;
                }
                else
                {
                    g_Settings.adminGuardFlags &=
                        ~kAdminGuardExitFlag;
                }
                SaveSettings();
                hooks::HLog(
                    g_AdminGuardCheckbox
                    ? "[QuantTools] Admin Guard enabled "
                      "(aquanaut bot only)"
                    : "[QuantTools] Admin Guard disabled");
            }
            if (gui->checkbox(
                    "Show Farm Widget",
                    &g_Settings.showAquanautWidget))
                SaveSettings();
            if (gui->checkbox(
                    "Pin Farm Widget",
                    &g_Settings.pinAquanautWidget))
                SaveSettings();
            ImGui::TextDisabled(
                "Widget switches Aquanaut / Firma by active bot");
            gui->end_child();
            gui->end_group();

            gui->sameline();
            gui->begin_group();
            gui->begin_child(
                "Q",
                "Orders and Zone",
                ImVec2(width, height));
            if (gui->dropdown(
                    "Order Mode",
                    &g_Settings.orderMode,
                    kOrderModes,
                    IM_ARRAYSIZE(kOrderModes)))
                dirty = true;
            gui->slider_int(
                "Zone Max Distance",
                &g_Settings.zoneMaxDistance,
                0,
                5000,
                "%dm");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            gui->text_field(
                "##zone_filter",
                "Zone name filter",
                ImVec2(-1.f, 36.f),
                g_Settings.zoneFilter,
                sizeof(g_Settings.zoneFilter));
            if (gui->button(
                    "Apply Zone",
                    ImVec2(120.f, 32.f)))
                dirty = true;
            ImGui::Spacing();
            ImGui::TextDisabled(
                "Token: %s",
                g_Status.tokenCaptured
                    ? "captured"
                    : "waiting");
            ImGui::TextDisabled(
                "Status: %s",
                g_Status.status);
            ImGui::TextDisabled(
                "Boxes %d | Rounds %d | $%d",
                g_Status.totalBoxes,
                g_Status.rounds,
                g_Status.earned);
            gui->end_child();
            gui->end_group();
        }
        else if (g_SettingsTab == 1)
        {
            gui->begin_group();
            gui->begin_child(
                "Q",
                "Firma",
                ImVec2(
                    ImGui::GetContentRegionAvail().x,
                    height));
            RenderFirmaUI();
            gui->end_child();
            gui->end_group();
        }
        else if (g_SettingsTab == 2)
        {
            gui->begin_group();
            gui->begin_child(
                "Q",
                "Combat",
                ImVec2(width, height));
            dirty |= ToggleCommand(
                "Anti Disable Firing",
                g_Settings.antiFiring,
                "antifiring_on",
                "antifiring_off");
            dirty |= ToggleCommand(
                "Assault SG No Falloff",
                g_Settings.noFalloff,
                "sg_nofalloff_on",
                "sg_nofalloff_off");
            dirty |= ToggleCommand(
                "NoDrop on Death",
                g_Settings.noDrop,
                "nodrop:on",
                "nodrop:off");
            dirty |= ToggleCommand(
                "Damage Counter",
                g_Settings.damageCounter,
                "dmg_counter_on",
                "dmg_counter_off");
            dirty |= ToggleCommand(
                "Armour Bypass",
                g_Settings.armourBypass,
                "armour_set:1",
                "armour_set:0");
            gui->slider_int(
                "Armour Interval",
                &g_Settings.armourInterval,
                100,
                5000,
                "%dms");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            dirty |= ToggleCommand(
                "Auto Pickup",
                g_Settings.autoPickup,
                "auto_pickup:on",
                "auto_pickup:off");
            if (gui->dropdown(
                    "Pickup Mode",
                    &g_Settings.autoPickupMode,
                    kPickupModes,
                    IM_ARRAYSIZE(kPickupModes)))
                dirty = true;
            gui->slider_int(
                "Pickup Delay",
                &g_Settings.autoPickupDelay,
                50,
                1500,
                "%dms");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            ImGui::Spacing();
            ImGui::TextDisabled("Attachments → Weapon Manager");
            if (gui->button(
                    "Open Weapon Manager",
                    ImVec2(200.f, 32.f)))
                Menu::OpenWeaponsTab();
            gui->end_child();
            gui->end_group();

            gui->sameline();
            gui->begin_group();
            gui->begin_child(
                "Q",
                "Custom HitFX",
                ImVec2(width, height));
            dirty |= ToggleCommand(
                "Enable HitFX",
                g_Settings.hitFx,
                "hitfx:on",
                "hitfx:off");
            dirty |= ToggleCommand(
                "Damage Numbers",
                g_Settings.hitNumbers,
                "hitfx_nums:on",
                "hitfx_nums:off");
            dirty |= ToggleCommand(
                "Sound",
                g_Settings.hitSound,
                "hitfx_sound:on",
                "hitfx_sound:off");
            dirty |= ToggleCommand(
                "Head Sound",
                g_Settings.headSound,
                "hitfx_hsound:on",
                "hitfx_hsound:off");
            dirty |= ToggleCommand(
                "Outline",
                g_Settings.hitOutline,
                "hitfx_outline:on",
                "hitfx_outline:off");
            dirty |= ToggleCommand(
                "Shadow",
                g_Settings.hitShadow,
                "hitfx_shadow:on",
                "hitfx_shadow:off");
            dirty |= ToggleCommand(
                "Original Colors",
                g_Settings.originalColors,
                "hitfx_origclr:on",
                "hitfx_origclr:off");
            dirty |= ToggleCommand(
                "Distance Scale",
                g_Settings.distanceScale,
                "hitfx_dscale:on",
                "hitfx_dscale:off");
            gui->color_edit(
                "Hit Color",
                g_Settings.hitColor);
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            gui->slider_float(
                "Font Size",
                &g_Settings.hitFontSize,
                0.15f,
                2.f,
                "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            int fontIndex = g_Settings.hitFont == 7
                ? 4
                : (g_Settings.hitFont == 4
                    ? 3
                    : g_Settings.hitFont);
            if (gui->dropdown(
                    "Font",
                    &fontIndex,
                    kHitFonts,
                    IM_ARRAYSIZE(kHitFonts)))
            {
                static const int ids[] = {0, 1, 2, 4, 7};
                g_Settings.hitFont = ids[fontIndex];
                dirty = true;
            }
            gui->slider_int(
                "Outline Width",
                &g_Settings.outlineWidth,
                1,
                5,
                "%d");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            gui->slider_float(
                "Stretch X",
                &g_Settings.hitStretchX,
                0.5f,
                2.f,
                "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            gui->slider_float(
                "Stretch Y",
                &g_Settings.hitStretchY,
                0.5f,
                2.f,
                "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            gui->slider_int(
                "Duration",
                &g_Settings.hitDuration,
                250,
                4000,
                "%dms");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            gui->slider_float(
                "Float Distance",
                &g_Settings.hitFloatDistance,
                0.f,
                3.f,
                "%.1f");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            gui->slider_float(
                "Spread",
                &g_Settings.hitSpread,
                0.f,
                2.f,
                "%.1f");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            gui->slider_float(
                "Height",
                &g_Settings.hitHeight,
                0.f,
                3.f,
                "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            gui->slider_float(
                "Sound Volume",
                &g_Settings.hitSoundVolume,
                0.f,
                1.f,
                "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            gui->end_child();
            gui->end_group();
        }
        else if (g_SettingsTab == 3)
        {
            gui->begin_group();
            gui->begin_child(
                "Q",
                "Strafe and A/D Timing",
                ImVec2(width, height));
            dirty |= ToggleCommand(
                "Counter Strafe",
                g_Settings.strafe,
                "strafe_on",
                "strafe_off");
            gui->slider_float(
                "Strafe Speed",
                &g_Settings.strafeSpeed,
                0.5f,
                10.f,
                "%.1f");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            if (gui->checkbox(
                    "Strafe Aim Only",
                    &g_Settings.strafeAimOnly))
                dirty = true;
            if (gui->checkbox(
                    "Forward / Back",
                    &g_Settings.strafeForwardBack))
                dirty = true;
            dirty |= ToggleCommand(
                "A/D Timing",
                g_Settings.adTiming,
                "ad_timing_on",
                "ad_timing_off");
            gui->slider_int(
                "Target A",
                &g_Settings.adTargetA,
                50,
                1000,
                "%dms");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            gui->slider_int(
                "Target D",
                &g_Settings.adTargetD,
                50,
                1000,
                "%dms");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            gui->slider_int(
                "Timing Jitter",
                &g_Settings.adJitter,
                0,
                50,
                "%dms");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            if (gui->checkbox(
                    "Timing Aim Only",
                    &g_Settings.adAimOnly))
                dirty = true;
            gui->end_child();
            gui->end_group();

            gui->sameline();
            gui->begin_group();
            gui->begin_child(
                "Q",
                "Recoil and Helicopter",
                ImVec2(width, height));
            dirty |= ToggleCommand(
                "Anti Recoil",
                g_Settings.antiRecoil,
                "ar_on",
                "ar_off");
            gui->slider_float(
                "Strength",
                &g_Settings.recoilStrength,
                0.f,
                1.f,
                "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            gui->slider_float(
                "Smoothing",
                &g_Settings.recoilSmoothing,
                0.f,
                0.95f,
                "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            gui->slider_float(
                "Recoil Jitter",
                &g_Settings.recoilJitter,
                0.f,
                0.5f,
                "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            if (gui->checkbox(
                    "Recoil Aim Only",
                    &g_Settings.recoilAimOnly))
                dirty = true;
            dirty |= ToggleCommand(
                "Helicopter Stabilizer",
                g_Settings.heliStabilizer,
                "heli_stab:1",
                "heli_stab:0");
            gui->slider_float(
                "Heli Damping",
                &g_Settings.heliDamping,
                0.5f,
                0.99f,
                "%.2f");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            gui->end_child();
            gui->end_group();
        }
        else if (g_SettingsTab == 4)
        {
            gui->begin_group();
            gui->begin_child(
                "Q",
                "Script Admin Detector",
                ImVec2(width, height));
            dirty |= ToggleCommand(
                "Script Admin Detector",
                g_Settings.adminDetector,
                "admin_detect_on",
                "admin_detect_off");
            dirty |= ToggleCommand(
                "Script Admin Lines (not Visuals ESP)",
                g_Settings.adminEsp,
                "admin_esp_on",
                "admin_esp_off");
            ImGui::TextDisabled(
                "Different from Visuals → Admin ESP profile.");
            if (gui->checkbox(
                    "Show Admin Widget",
                    &g_Settings.showAdminWidget))
                SaveSettings();
            if (gui->checkbox(
                    "Pin Admin Widget",
                    &g_Settings.pinAdminWidget))
                SaveSettings();
            ImGui::TextDisabled(
                "Online: %d | Nearby: %d",
                g_Status.onlineAdminCount,
                g_Status.adminCount);
            gui->end_child();
            gui->end_group();

            gui->sameline();
            gui->begin_group();
            gui->begin_child(
                "Q",
                "Administrators Online",
                ImVec2(width, height));
            if (!g_Status.onlineAdminCount)
                ImGui::TextDisabled("No administrators online.");
            for (int index = 0;
                 index < g_Status.onlineAdminCount;
                 ++index)
            {
                const OnlineAdminInfo& admin =
                    g_Status.onlineAdmins[index];
                const ImVec4 levelColor =
                    AdminLevelColor(admin.level);
                ImGui::TextColored(
                    levelColor,
                    "%s",
                    admin.name);
                ImGui::SameLine();
                ImGui::TextDisabled(
                    "ID %d |",
                    admin.id);
                ImGui::SameLine();
                ImGui::TextColored(
                    levelColor,
                    "Level %d",
                    admin.level);
            }
            gui->end_child();
            gui->end_group();
        }
        else if (g_SettingsTab == 5)
        {
            gui->begin_group();
            gui->begin_child(
                "Q",
                "Keys and CheatSheet",
                ImVec2(width, height));
            if (gui->keybind(
                    "Crawl Key",
                    &g_Settings.crawlKey))
                dirty = true;
            if (gui->keybind(
                    "CheatSheet Key",
                    &g_Settings.cheatSheetKey))
                dirty = true;
            gui->slider_int(
                "CheatSheet Scale",
                &g_Settings.cheatSheetScale,
                30,
                100,
                "%d%%");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            gui->slider_int(
                "CheatSheet Opacity",
                &g_Settings.cheatSheetOpacity,
                10,
                100,
                "%d%%");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            gui->slider_int(
                "Backdrop Dim",
                &g_Settings.cheatSheetDim,
                0,
                100,
                "%d%%");
            if (ImGui::IsItemDeactivatedAfterEdit())
                dirty = true;
            static const char* variants[] = {
                "Dark Table", "Neon Cards"
            };
            int variant =
                g_Settings.cheatSheetVariant - 2;
            if (variant < 0 || variant > 1)
                variant = 0;
            if (gui->dropdown(
                    "CheatSheet Style",
                    &variant,
                    variants,
                    IM_ARRAYSIZE(variants)))
            {
                g_Settings.cheatSheetVariant =
                    variant + 2;
                dirty = true;
            }
            CommandButton("Crawl Toggle", "crawl_toggle", 110.f);
            gui->sameline();
            CommandButton("Self Revive", "selfrevive", 110.f);
            gui->sameline();
            CommandButton("Force Death", "die", 110.f);
            gui->end_child();
            gui->end_group();

            gui->sameline();
            gui->begin_group();
            gui->begin_child(
                "Q",
                "Actions and Command",
                ImVec2(width, height));
            CommandButton("Buy Pills", "pills", 100.f);
            gui->sameline();
            CommandButton("Buy Aid", "aid", 100.f);
            gui->sameline();
            CommandButton("Amp Bot", "amp_toggle", 100.f);
            CommandButton("Auto Buy", "autobuy_toggle", 100.f);
            gui->sameline();
            CommandButton("Duty", "duty", 100.f);
            gui->sameline();
            CommandButton("Scan Players", "player_scan", 100.f);
            gui->sameline();
            CommandButton("Eat + Drink", "eat", 100.f);
            ImGui::Spacing();
            gui->text_field(
                "##quant_raw_command",
                "Local command",
                ImVec2(-1.f, 36.f),
                g_RawCommand,
                sizeof(g_RawCommand));
            if (gui->button(
                    "Run Command",
                    ImVec2(130.f, 32.f)) &&
                g_RawCommand[0])
            {
                EnqueueCommand(
                    g_RawCommand,
                    "Quant command");
            }
            gui->end_child();
            gui->end_group();
        }
#if RAGEMP_DEV_BUILD
        else if (g_SettingsTab == 6)
        {
            gui->begin_group();
            gui->begin_child(
                "Q",
                "CEF Dump",
                ImVec2(width, height));
            ImGui::TextWrapped(
                "Capture is disabled in this build: nothing is "
                "written to disk.");
            ImGui::Spacing();
            if (gui->button(
                    "Dump All CEF",
                    ImVec2(
                        ImGui::GetContentRegionAvail().x,
                        40.f)))
            {
                EnqueueCommand("dump_cef_all", "CEF Full Dump");
            }
            ImGui::Spacing();
            CommandButton(
                "Dump browserMenu HTML",
                "dump_cef",
                220.f);
            CommandButton(
                "Dump browserMenu Scripts",
                "dump_cef_scripts",
                220.f);
            ImGui::Spacing();
            ImGui::TextDisabled("Status: %s", g_CefDumpStatus);
            ImGui::TextDisabled(
                "Files written: %d",
                g_CefDumpFilesWritten);
            gui->end_child();
            gui->end_group();

            gui->sameline();
            gui->begin_group();
            gui->begin_child(
                "Q",
                "Other Dumps",
                ImVec2(width, height));
            CommandButton("Dump Air HUD", "dump_air", 140.f);
            ImGui::TextDisabled(
                "Saves C:\\RAGEMP\\air_hud_dump.txt");
            ImGui::Spacing();
            ImGui::TextWrapped(
                "Full = HTML of all browsers + browserMenu "
                "scripts (QTools). Do not spam Scripts.");
            gui->end_child();
            gui->end_group();
        }
#endif

        if (dirty)
        {
            SaveSettings();
            QueueSettingsSync();
        }
        ImGui::PopID();
    }

    void RenderReconnectShell(
        const char* windowId,
        const char* title,
        bool& visible,
        bool& pinned,
        const ImVec2& firstPos) noexcept
    {
        if (!visible)
            return;
        ImGui::SetNextWindowPos(firstPos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(280.f, 96.f), ImGuiCond_Always);
        ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoScrollbar;
        if (pinned)
            flags |= ImGuiWindowFlags_NoMove;
        if (!Menu::IsVisible())
            flags |= ImGuiWindowFlags_NoInputs;

        PushWidgetStyle();
        ImGui::SetNextWindowBgAlpha(0.88f);
        if (ImGui::Begin(windowId, nullptr, flags))
        {
            RenderWidgetHeader(title, visible, pinned);
            DrawStatusPill(
                "RECONNECTING",
                ImVec4(0.95f, 0.62f, 0.18f, 1.f),
                true);
            ImGui::SameLine(0.f, 8.f);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.f);
            ImGui::TextDisabled("waiting for V8…");
        }
        ImGui::End();
        PopWidgetStyle();
    }

    void RenderWidgets()
    {
        if (g_ServerKind != ServerKind::Quant)
            return;

        if (g_Loaded)
        {
            RenderFarmWidget();
            RenderAdminWidget();
            return;
        }

        // Keep HUD visible while runtime reloads after Present/V8 faults.
        const auto runtime = RageV8Runtime::GetStatus();
        const bool lifting =
            runtime.state == RageV8Runtime::State::Ready ||
            runtime.state == RageV8Runtime::State::WaitingForApi ||
            runtime.state == RageV8Runtime::State::Resolving ||
            g_LoadRequested;
        if (!lifting)
            return;

        RenderReconnectShell(
            "##quant_farm_reconnect",
            "Farm",
            g_Settings.showAquanautWidget,
            g_Settings.pinAquanautWidget,
            ImVec2(18.f, 110.f));
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        RenderReconnectShell(
            "##quant_admin_reconnect",
            "Script Admin Detector",
            g_Settings.showAdminWidget,
            g_Settings.pinAdminWidget,
            ImVec2(display.x - 300.f, 110.f));
    }

    void RenderFirmaUI()
    {
        ImGui::PushID("firma_ui_tab");
        CommandButton("Firma Start", "company_start", 110.f);
        gui->sameline();
        CommandButton("Firma Stop", "company_stop", 110.f);
        gui->sameline();
        CommandButton("Dump Orders", "dump_firma", 120.f);
        ImGui::TextDisabled(
            "Dump to disk is disabled in this build.");

        ImGui::Spacing();
        CommandButton("Firma Scan", "company_scan", 110.f);
        gui->sameline();
        CommandButton("Firma Take", "company_take", 110.f);
        gui->sameline();
        CommandButton("Firma Attach", "company_attach", 110.f);
        gui->sameline();
        CommandButton("Firma Pay", "company_pay", 110.f);
        gui->sameline();
        CommandButton("Eat + Drink", "eat", 110.f);
        ImGui::Spacing();

        static int firmaWeight = 0;
        static int firmaSort = 0;
        static bool firmaOnlyFree = true;
        static bool firmaTypes[12] = {
            true, true, true, true, true, true,
            true, true, true, true, true, true
        };
        static const char* kFirmaWeights[] = {
            "all", "light", "medium", "heavy"
        };
        static const char* kFirmaSorts[] = {
            "price", "distance", "weight"
        };
        static const char* kFirmaTypeNames[] = {
            "tools", "dangerous", "fuel", "food",
            "household", "recycling", "explosive",
            "radioactive", "corrosive", "gas",
            "wood", "cars"
        };
        auto syncFirmaFilter = [&]() {
            char types[256]{};
            int tw = 0;
            for (int i = 0; i < 12; ++i) {
                if (!firmaTypes[i])
                    continue;
                if (tw > 0 && tw < static_cast<int>(sizeof(types)) - 1)
                    types[tw++] = ',';
                const char* name = kFirmaTypeNames[i];
                for (int c = 0; name[c] && tw < static_cast<int>(sizeof(types)) - 1; ++c)
                    types[tw++] = name[c];
            }
            types[tw] = 0;
            if (!types[0])
                lstrcpynA(types, "all", sizeof(types));
            char cmd[384]{};
            _snprintf(
                cmd,
                sizeof(cmd) - 1,
                "company_filter:weight=%s;sort=%s;free=%d;types=%s",
                kFirmaWeights[firmaWeight < 0 || firmaWeight > 3 ? 0 : firmaWeight],
                kFirmaSorts[firmaSort < 0 || firmaSort > 2 ? 0 : firmaSort],
                firmaOnlyFree ? 1 : 0,
                types);
            cmd[sizeof(cmd) - 1] = 0;
            EnqueueCommand(cmd, "Firma filter", true);
        };

        if (gui->dropdown("Firma Weight", &firmaWeight, kFirmaWeights, IM_ARRAYSIZE(kFirmaWeights)))
            syncFirmaFilter();
        if (gui->dropdown("Firma Sort", &firmaSort, kFirmaSorts, IM_ARRAYSIZE(kFirmaSorts)))
            syncFirmaFilter();
        if (gui->checkbox("Firma Only Free", &firmaOnlyFree))
            syncFirmaFilter();

        ImGui::TextDisabled("Cargo types:");
        for (int i = 0; i < 12; ++i) {
            if (gui->checkbox(kFirmaTypeNames[i], &firmaTypes[i]))
                syncFirmaFilter();
            if ((i % 3) != 2 && i + 1 < 12)
                gui->sameline();
        }
        ImGui::PopID();
    }
}
