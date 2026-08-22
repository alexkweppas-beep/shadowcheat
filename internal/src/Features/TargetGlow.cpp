#include "TargetGlow.h"

#include <cmath>

#include "../Main/Hooks.h"
#include "../Menu/MenuTexture.h"
#include "../SDK/Game.h"
#include "../imgui/imgui.h"

namespace TargetGlow
{
    namespace
    {
        ID3D11BlendState* g_additive = nullptr;
        bool g_additiveTried = false;

        ID3D11BlendState* AdditiveState() noexcept
        {
            if (g_additive || g_additiveTried)
                return g_additive;
            g_additiveTried = true;
            if (!hooks::g_Device)
                return nullptr;

            D3D11_BLEND_DESC desc{};
            desc.RenderTarget[0].BlendEnable = TRUE;
            desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            // ONE rather than INV_SRC_ALPHA: that one word is the whole
            // difference between a stack of discs and a glow.
            desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
            desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
            desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            desc.RenderTarget[0].RenderTargetWriteMask =
                D3D11_COLOR_WRITE_ENABLE_ALL;
            if (FAILED(hooks::g_Device->CreateBlendState(
                    &desc, &g_additive)))
                g_additive = nullptr;
            return g_additive;
        }

        void ApplyAdditive(
            const ImDrawList*, const ImDrawCmd*) noexcept
        {
            ID3D11BlendState* state = AdditiveState();
            if (!state || !hooks::g_Context)
                return;
            const float factor[4]{0.f, 0.f, 0.f, 0.f};
            hooks::g_Context->OMSetBlendState(
                state, factor, 0xFFFFFFFFu);
        }
    }

    bool Begin(ImDrawList* draw) noexcept
    {
        if (!draw || !MenuTexture::GetGlowImGui() ||
            !AdditiveState())
            return false;
        draw->AddCallback(ApplyAdditive, nullptr);
        return true;
    }

    void End(ImDrawList* draw) noexcept
    {
        if (!draw)
            return;
        // ImGui's own reset restores whatever the backend set up, so the rest of
        // the overlay is unaffected by the span above.
        draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
    }

    void Sprite(
        ImDrawList* draw,
        const Vector3& world,
        float radius,
        const float color[3],
        float intensity) noexcept
    {
        if (!draw || !color || !(radius > 0.f) || !(intensity > 0.f))
            return;
        void* texture = MenuTexture::GetGlowImGui();
        if (!texture)
            return;

        const auto centre = Game::WorldToScreen(world);
        if (!centre)
            return;
        // The on-screen radius comes from projecting a second point one radius
        // above the first, which is what makes the sprite shrink with distance
        // on its own instead of needing a distance term.
        Vector3 edge = world;
        edge.z += radius;
        const auto edgeScreen = Game::WorldToScreen(edge);
        if (!edgeScreen)
            return;
        const float pixels =
            std::fabs(edgeScreen->y - centre->y);
        if (!(pixels > 0.4f) || !std::isfinite(pixels) ||
            pixels > 400.f)
            return;

        const auto channel = [&](int index) noexcept {
            const float value = color[index] * intensity;
            return static_cast<int>(
                (value < 0.f ? 0.f : (value > 1.f ? 1.f : value)) *
                255.f);
        };
        const float alpha = intensity > 1.f ? 1.f : intensity;
        const ImU32 tint = IM_COL32(
            channel(0),
            channel(1),
            channel(2),
            static_cast<int>(alpha * 255.f));

        const ImVec2 at(centre->x, centre->y);
        draw->AddImage(
            texture,
            ImVec2(at.x - pixels, at.y - pixels),
            ImVec2(at.x + pixels, at.y + pixels),
            ImVec2(0.f, 0.f),
            ImVec2(1.f, 1.f),
            tint);
    }
}
