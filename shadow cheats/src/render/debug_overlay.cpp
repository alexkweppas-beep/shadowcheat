#include "security/xorstr.hpp"
#include "debug_overlay.hpp"
#include "../core/globals.hpp"
#include "../config/settings.hpp"
#include "../render/overlay.hpp"

#include <imgui.h>
#include <atomic>
#include <Windows.h>

namespace features
{
    std::atomic<unsigned int> g_EntityLoopHz{ 0 };

    void DrawDebugOverlay()
    {
        if (!cfg::Misc_Debug) return;

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 10.f, vp->WorkPos.y + 400.f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.55f);

        ImGui::Begin(xorstr_("##debug_overlay"), nullptr,
            ImGuiWindowFlags_NoDecoration       |
            ImGuiWindowFlags_AlwaysAutoResize   |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav              |
            ImGuiWindowFlags_NoMove             |
            ImGuiWindowFlags_NoInputs           |
            ImGuiWindowFlags_NoSavedSettings);

        const ImGuiIO& io = ImGui::GetIO();

        ImGui::TextColored(ImVec4(0.4f, 1.f, 0.4f, 1.f), xorstr_("[shadow cheats DEBUG]"));
        ImGui::Separator();

        ImGui::Text(xorstr_("Render FPS    : %.1f (%.2f ms)"), io.Framerate, 1000.f / io.Framerate);
        ImGui::Text(xorstr_("Entity Hz     : %u"),            g_EntityLoopHz.load(std::memory_order_relaxed));
        size_t entity_count = 0;
        {
            std::lock_guard<std::mutex> lock(g.EntityListMutex);
            entity_count = g.EntityList.size();
        }
        ImGui::Text(xorstr_("Entities      : %zu"),           entity_count);

        ImGui::Separator();
        ImGui::Text(xorstr_("ImageBase     : 0x%llX"),        (unsigned long long)g.ImageBase);
        ImGui::Text(xorstr_("GWorld        : 0x%llX"),        (unsigned long long)g.GWorld);
        ImGui::Text(xorstr_("Viewport      : 0x%llX"),        (unsigned long long)g.Viewport);
        ImGui::Text(xorstr_("ReplayIface   : 0x%llX"),        (unsigned long long)g.Replayinterface);
        ImGui::Text(xorstr_("LocalPlayer   : 0x%llX"),        g.localplayer ? (unsigned long long)g.localplayer->ptr : 0ull);

        ImGui::Separator();
        ImGui::Text(xorstr_("Overlay       : %dx%d"),         overlay::width, overlay::height);
        ImGui::Text(xorstr_("GameRect      : %ld,%ld %ldx%ld"),
            g.GameRect.left, g.GameRect.top,
            g.GameRect.right - g.GameRect.left,
            g.GameRect.bottom - g.GameRect.top);

        const HWND fg = GetForegroundWindow();
        ImGui::Text(xorstr_("Game focused  : %s"),            (fg == g.GameHwnd) ? xorstr_("yes") : xorstr_("no"));
        ImGui::Text(xorstr_("Menu open     : %s"),            overlay::menu_open ? xorstr_("yes") : xorstr_("no"));

        ImGui::Separator();
        ImGui::Text(xorstr_("ESP_Skeleton  : %s"), cfg::ESP_Skeleton ? xorstr_("on") : xorstr_("off"));
        ImGui::Text(xorstr_("ESP_SkelLines : %s"), cfg::ESP_SkeletonLines ? xorstr_("on") : xorstr_("off"));
        ImGui::Text(xorstr_("EntityDelayMs : %d"), cfg::EntityUpdateDelayMs);
        ImGui::Text(xorstr_("MaxEntities   : %d"), cfg::MaxEntities);

        ImGui::End();
    }
}

