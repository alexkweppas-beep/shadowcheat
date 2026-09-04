#pragma once

#include "../imgui/imgui.h"
#include "../Menu/Menu.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11ShaderResourceView;

namespace PedPreview
{
    void SetActive(bool active) noexcept;
    bool IsActive() noexcept;

    void CaptureAndSanitizeBackbuffer(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        IDXGISwapChain* swap) noexcept;

    // 3D model + projected ESP editor (drag / wheel / RMB / rotate).
    void RenderIntoScene(
        ImDrawList* draw,
        const ImVec2& sceneMin,
        const ImVec2& sceneMax,
        Menu::EspProfileSettings& profile,
        bool interactive = false) noexcept;

    void OpenSettings(const ImVec2& pos) noexcept;

    ID3D11ShaderResourceView* ModelSrv() noexcept;

    void Shutdown() noexcept;
}
