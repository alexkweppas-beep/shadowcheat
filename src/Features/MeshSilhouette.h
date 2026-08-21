#pragma once

#include <cstdint>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11ShaderResourceView;
struct IDXGISwapChain;

namespace MeshSilhouette
{
    struct Color
    {
        float r;
        float g;
        float b;
        float a;
    };

    // Public tuning state. Enabling this only arms the foundation; rendering
    // remains fail-closed until a verified classifier explicitly marks itself
    // ready and marks a mask write in the current frame.
    struct Settings
    {
        bool enabled = false;
        bool fill = true;
        bool edge = true;
        Color visibleColor{0.20f, 0.90f, 0.42f, 1.0f};
        Color occludedColor{1.00f, 0.24f, 0.16f, 1.0f};
        float opacity = 0.80f;
        float edgeWidth = 2.0f;
        float softness = 1.0f;
    };

    extern Settings g_Settings;
    Settings& GetSettings() noexcept;

    // This is the authoritative runtime state exposed to UI/diagnostics.
    // ClassifierUnavailable is intentionally the initial state.
    enum class Status : std::uint8_t
    {
        ClassifierUnavailable,
        Disabled,
        WaitingForFrame,
        WaitingForMask,
        Ready,
        CompositeSubmitted,
        DeviceUnavailable,
        ResourceCreationFailed,
        ShaderCompilationFailed,
        CompositeTargetUnavailable
    };

    Status GetStatus() noexcept;
    const char* StatusText(Status status) noexcept;
    const char* StatusText() noexcept;
    const char* GetStatusText() noexcept;

    enum class MaskLayer : std::uint8_t
    {
        Visible = 1u,
        Occluded = 2u,
        Both = 3u
    };

    // Call once before the verified entity draws for a frame. This creates or
    // resizes both private masks and clears them without touching the game
    // backbuffer or changing its bound pipeline state.
    bool BeginFrame(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        std::uint32_t width,
        std::uint32_t height) noexcept;
    bool BeginFrame(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        IDXGISwapChain* swap) noexcept;

    // Borrowed pointers; callers must not Release them. They remain null until
    // a verified classifier is ready and BeginFrame succeeds.
    ID3D11RenderTargetView* VisibleMaskRtv() noexcept;
    ID3D11RenderTargetView* OccludedMaskRtv() noexcept;
    std::uint32_t MaskWidth() noexcept;
    std::uint32_t MaskHeight() noexcept;

    // A future verified entity draw hook owns these trust transitions.
    void MarkClassifierReady(bool ready = true) noexcept;
    bool IsClassifierReady() noexcept;
    void MarkMaskWritten(MaskLayer layer = MaskLayer::Both) noexcept;
    bool IsMaskWritten(MaskLayer layer = MaskLayer::Both) noexcept;

    // Safe to call for ResizeBuffers/device reset. BeginFrame also detects
    // device and size changes automatically.
    void InvalidateDeviceObjects() noexcept;
    void Shutdown() noexcept;

    namespace Detail
    {
        // Internal hand-off to WorldOverlay. All pointers are borrowed and are
        // valid only on the render thread until the next invalidation.
        struct CompositeInputs
        {
            ID3D11ShaderResourceView* visible = nullptr;
            ID3D11ShaderResourceView* occluded = nullptr;
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            bool visibleWritten = false;
            bool occludedWritten = false;
        };

        bool GetCompositeInputs(
            ID3D11Device* device,
            std::uint32_t width,
            std::uint32_t height,
            CompositeInputs& inputs) noexcept;
        void MarkCompositeSubmitted() noexcept;
        void MarkFailure(Status failure) noexcept;
    }
}
