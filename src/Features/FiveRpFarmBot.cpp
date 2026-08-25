#include "FiveRpFarmBot.h"

#include "FiveRpFarmBotScript.h"
#include "FiveRpMetadataResolver.h"
#include "RageV8Runtime.h"
#include "ServerProfile.h"
#include "../Menu/Menu.h"
#include "../Menu/IconsFontAwesome6.h"
#include "../Menu/Leyawin/custom_widgets.hpp"
#include "../imgui/imgui.h"

#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
        Status
    };

    enum class ServerKind : std::uint8_t
    {
        Unknown,
        FiveRp,
        Other
    };

    struct PendingRequest final
    {
        std::uint64_t id{};
        RequestKind kind{};
        ULONGLONG timestamp{};
        bool silent{};
        char label[48]{};
    };

    struct FarmStatus final
    {
        bool active{};
        bool invis{true};
        bool teleport{true};
        bool onJob{};
        bool hasBucket{};
        bool filled{};
        bool farmObject{};
        int cow{-1};
        int cycles{};
        int cows{};
        char state[24]{"idle"};
        char message[96]{"Not loaded"};
    };

    PendingRequest g_Pending[12]{};
    int g_PendingCount = 0;
    ServerKind g_ServerKind = ServerKind::Unknown;
    FarmStatus g_Status{};
    std::uint64_t g_RuntimeGeneration{};
    ULONGLONG g_LastLoadAttempt{};
    ULONGLONG g_LastStatusRequest{};
    bool g_LoadRequested{};
    bool g_Loaded{};
    bool g_WasRuntimeReady{};

    void CopyText(
        char* output,
        int capacity,
        const char* text) noexcept
    {
        if (!output || capacity <= 0)
            return;
        lstrcpynA(output, text ? text : "", capacity);
    }

    void ResetSession() noexcept
    {
        g_PendingCount = 0;
        for (auto& pending : g_Pending)
            pending = {};
        g_Loaded = false;
        g_LoadRequested = false;
        g_LastLoadAttempt = 0;
        g_LastStatusRequest = 0;
        g_Status = {};
        g_Status.invis = true;
        g_Status.teleport = true;
        CopyText(
            g_Status.message,
            static_cast<int>(sizeof(g_Status.message)),
            "Not loaded");
    }

    bool TrackRequest(
        std::uint64_t id,
        RequestKind kind,
        const char* label,
        bool silent) noexcept
    {
        if (!id || g_PendingCount >= 12)
            return false;
        auto& pending = g_Pending[g_PendingCount++];
        pending = {};
        pending.id = id;
        pending.kind = kind;
        pending.timestamp = GetTickCount64();
        pending.silent = silent;
        CopyText(
            pending.label,
            static_cast<int>(sizeof(pending.label)),
            label ? label : "5RP Farm");
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

    // Mirrors the shared probe's answer: one fingerprint now serves every
    // module instead of each one asking the script runtime itself.
    void SyncServerKind() noexcept
    {
        const ServerProfile::Kind kind = ServerProfile::Current();
        const ServerKind next =
            kind == ServerProfile::Kind::FiveRp
                ? ServerKind::FiveRp
                : (kind == ServerProfile::Kind::Unknown
                       ? ServerKind::Unknown
                       : ServerKind::Other);
        if (g_ServerKind == next)
            return;
        g_ServerKind = next;
        if (next != ServerKind::FiveRp)
        {
            g_Loaded = false;
            g_LoadRequested = false;
        }
    }

    void QueueLoad() noexcept
    {
        if (g_LoadRequested ||
            g_ServerKind != ServerKind::FiveRp)
            return;
        g_LastLoadAttempt = GetTickCount64();
        if (!QueueScript(
                FiveRpFarmBotScript::kSource,
                RequestKind::Load,
                "Load 5RP farm bot"))
        {
            Menu::PushNotification(
                "5RP Farm",
                "Runtime is not ready or the queue is full.",
                false);
            return;
        }
        g_LoadRequested = true;
    }

    bool BuildCommandScript(
        const char* command,
        char* output,
        int capacity) noexcept
    {
        if (!command || !command[0] ||
            !output || capacity < 96)
            return false;
        char escaped[96]{};
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
            "return global.__fiveFarmCommand ? "
            "global.__fiveFarmCommand('%s') : "
            "'FARM_NOT_LOADED';",
            escaped);
        output[capacity - 1] = 0;
        return length > 0 && length < capacity;
    }

    void EnqueueCommand(
        const char* command,
        const char* label,
        bool silent) noexcept
    {
        char script[192]{};
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
                    "5RP Farm",
                    "Unable to queue command.",
                    false);
            }
        }
    }

    void ParseStatus(const char* text) noexcept
    {
        if (!text || strncmp(text, "FARM\t", 5) != 0)
            return;
        char copy[256]{};
        CopyText(copy, static_cast<int>(sizeof(copy)), text);
        char* fields[16]{};
        int count = 0;
        char* cursor = copy;
        while (cursor && count < 16)
        {
            fields[count++] = cursor;
            char* tab = strchr(cursor, '\t');
            if (!tab)
                break;
            *tab = 0;
            cursor = tab + 1;
        }
        if (count < 13)
            return;
        g_Status.active = fields[1][0] == '1';
        CopyText(
            g_Status.state,
            static_cast<int>(sizeof(g_Status.state)),
            fields[2]);
        g_Status.cow = atoi(fields[3]);
        g_Status.cycles = atoi(fields[4]);
        g_Status.invis = fields[5][0] == '1';
        g_Status.teleport = fields[6][0] == '1';
        g_Status.onJob = fields[7][0] == '1';
        g_Status.hasBucket = fields[8][0] == '1';
        g_Status.filled = fields[9][0] == '1';
        g_Status.farmObject = fields[10][0] == '1';
        g_Status.cows = atoi(fields[11]);
        CopyText(
            g_Status.message,
            static_cast<int>(sizeof(g_Status.message)),
            fields[12]);
    }

    void QueueStatus() noexcept
    {
        const ULONGLONG now = GetTickCount64();
        if (now - g_LastStatusRequest < 700)
            return;
        g_LastStatusRequest = now;
        QueueScript(
            "return global.__fiveFarmStatus ? "
            "global.__fiveFarmStatus() : 'FARM_NOT_LOADED';",
            RequestKind::Status,
            "5RP farm status",
            true);
    }

    void DrawPill(
        const char* text,
        ImVec4 color,
        bool filled) noexcept
    {
        const ImVec2 size = ImGui::CalcTextSize(text);
        const ImVec2 pad(8.f, 4.f);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 box(
            size.x + pad.x * 2.f,
            size.y + pad.y * 2.f);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImU32 bg = filled
            ? ImGui::ColorConvertFloat4ToU32(color)
            : IM_COL32(32, 32, 38, 180);
        draw->AddRectFilled(
            pos,
            ImVec2(pos.x + box.x, pos.y + box.y),
            bg,
            8.f);
        draw->AddText(
            ImVec2(pos.x + pad.x, pos.y + pad.y),
            filled
                ? IM_COL32(20, 20, 24, 255)
                : ImGui::ColorConvertFloat4ToU32(color),
            text);
        ImGui::Dummy(box);
    }
}

namespace FiveRpFarmBot
{
    void Initialize() noexcept
    {
        ResetSession();
        g_ServerKind = ServerKind::Unknown;
        g_RuntimeGeneration = 0;
        g_WasRuntimeReady = false;
    }

    bool IsFiveRpServer() noexcept
    {
        return g_ServerKind == ServerKind::FiveRp ||
            FiveRpMetadataResolver::IsDetected();
    }

    void QueueCommand(
        const char* command,
        const char* label,
        bool silent) noexcept
    {
        EnqueueCommand(command, label, silent);
    }

    void Update() noexcept
    {
        const ULONGLONG now = GetTickCount64();
        const auto runtime = RageV8Runtime::GetStatus();
        if (runtime.generation != g_RuntimeGeneration)
        {
            g_RuntimeGeneration = runtime.generation;
            const ServerKind keep = g_ServerKind;
            ResetSession();
            g_ServerKind = keep;
            hooks::HLog(
                "[5RP Farm] V8 generation changed; reloading");
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
            if (request.kind == RequestKind::Status)
            {
                if (result.success &&
                    strncmp(result.text, "FARM\t", 5) == 0)
                    ParseStatus(result.text);
                else
                {
                    g_Loaded = false;
                    g_LoadRequested = false;
                }
                continue;
            }
            if (request.kind == RequestKind::Load)
            {
                g_LoadRequested = false;
                g_Loaded = result.success &&
                    strstr(result.text, "five-farm-ready") !=
                        nullptr;
                hooks::HLog(
                    g_Loaded
                        ? "[5RP Farm] bot loaded"
                        : "[5RP Farm] bot load failed");
                continue;
            }
            if (request.kind == RequestKind::Command &&
                result.success &&
                strncmp(result.text, "FARM\t", 5) == 0)
            {
                ParseStatus(result.text);
            }
        }
        g_PendingCount = write;

        SyncServerKind();
        if (g_ServerKind == ServerKind::FiveRp &&
            !g_Loaded &&
            !g_LoadRequested &&
            now - g_LastLoadAttempt > 2000)
            QueueLoad();
        if (g_Loaded)
            QueueStatus();
    }

    void RenderUI()
    {
        const auto runtime = RageV8Runtime::GetStatus();
        const bool runtimeReady =
            runtime.state == RageV8Runtime::State::Ready;
        ImGui::Text("GTA5RP cow farm");
        ImGui::SameLine();
        {
            const char* stateText = "Detecting...";
            ImVec4 color(0.95f, 0.58f, 0.25f, 1.f);
            if (g_ServerKind == ServerKind::Other)
            {
                stateText = "Not GTA5RP";
                color = ImVec4(0.85f, 0.35f, 0.35f, 1.f);
            }
            else if (g_Loaded)
            {
                stateText = "Ready";
                color = ImVec4(0.3f, 0.9f, 0.45f, 1.f);
            }
            else if (g_LoadRequested)
                stateText = "Loading...";
            else if (g_ServerKind == ServerKind::FiveRp)
                stateText = "GTA5RP · waiting V8";
            ImGui::TextColored(color, "%s", stateText);
        }

        ImGui::TextWrapped(
            "5RP has no admin setInvisible RPC. Invis is local: "
            "setVisible / setAlpha / setEntityVisible. Other players "
            "can still see you. Teleport is required because the "
            "server checks 5m cow distance and 5s antiflood.");

        if (!g_Loaded)
        {
            ImGui::TextDisabled(
                runtimeReady
                    ? "Waiting for the farm script to inject."
                    : "Waiting for the RAGE V8 runtime.");
            return;
        }

        DrawPill(
            g_Status.active ? "ACTIVE" : "IDLE",
            ImVec4(0.35f, 0.9f, 0.5f, 1.f),
            g_Status.active);
        ImGui::SameLine(0.f, 6.f);
        DrawPill(
            g_Status.state,
            ImVec4(0.95f, 0.72f, 0.32f, 1.f),
            g_Status.active);
        ImGui::SameLine(0.f, 6.f);
        if (g_Status.invis)
        {
            DrawPill(
                "INVIS",
                ImVec4(0.62f, 0.55f, 1.f, 1.f),
                true);
            ImGui::SameLine(0.f, 6.f);
        }
        if (g_Status.teleport)
        {
            DrawPill(
                "TP",
                ImVec4(0.45f, 0.78f, 1.f, 1.f),
                true);
            ImGui::SameLine(0.f, 6.f);
        }
        char cycles[32]{};
        sprintf_s(cycles, "x%d", g_Status.cycles);
        DrawPill(
            cycles,
            ImVec4(0.85f, 0.85f, 0.9f, 1.f),
            false);

        ImGui::Spacing();
        ImGui::TextDisabled("%s", g_Status.message);
        ImGui::Text(
            "cow %d  /  %d  job:%s  bucket:%s  milk:%s  farm:%s",
            g_Status.cow,
            g_Status.cows,
            g_Status.onJob ? "yes" : "no",
            g_Status.hasBucket ? "yes" : "no",
            g_Status.filled ? "full" : "empty",
            g_Status.farmObject ? "live" : "coords");

        ImGui::Spacing();
        if (gui->button(
                g_Status.active ? "Stop bot" : "Start bot",
                ImVec2(140.f, 32.f)))
        {
            EnqueueCommand(
                g_Status.active ? "stop" : "start",
                g_Status.active ? "Stop farm bot" : "Start farm bot",
                false);
        }
        ImGui::SameLine();
        bool invis = g_Status.invis;
        if (gui->checkbox("Local invis", &invis) &&
            invis != g_Status.invis)
        {
            EnqueueCommand(
                invis ? "invis_on" : "invis_off",
                "Toggle farm invis",
                true);
        }
        ImGui::SameLine();
        bool teleport = g_Status.teleport;
        if (gui->checkbox("Teleport", &teleport) &&
            teleport != g_Status.teleport)
        {
            EnqueueCommand(
                teleport ? "tp_on" : "tp_off",
                "Toggle farm teleport",
                true);
        }
    }
}
