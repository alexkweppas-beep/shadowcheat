#pragma once

#include "MeshSilhouette.h"

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct IDXGISwapChain;

namespace WorldOverlay
{
    // Post-OBS compose hook. This is deliberately fail-closed: it does not
    // touch the backbuffer until a verified classifier has begun a mask frame
    // and explicitly marked at least one private mask as written.
    void ComposeAfterCapture(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        IDXGISwapChain* swap) noexcept;

    // Compatibility alias for the former single glow mask.
    ID3D11RenderTargetView* GlowMaskRtv() noexcept;

    // Convenience facade for a future verified entity draw integration.
    bool BeginMaskFrame(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        std::uint32_t width,
        std::uint32_t height) noexcept;
    bool BeginMaskFrame(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        IDXGISwapChain* swap) noexcept;
    ID3D11RenderTargetView* VisibleMaskRtv() noexcept;
    ID3D11RenderTargetView* OccludedMaskRtv() noexcept;
    void MarkClassifierReady(bool ready = true) noexcept;
    void MarkMaskWritten(
        MeshSilhouette::MaskLayer layer =
            MeshSilhouette::MaskLayer::Both) noexcept;
    MeshSilhouette::Status MaskStatus() noexcept;
    const char* MaskStatusText() noexcept;

    void InvalidateDeviceObjects() noexcept;
    void Shutdown() noexcept;
}
