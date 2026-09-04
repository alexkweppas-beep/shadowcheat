#include "MeshSilhouette.h"

#include <atomic>
#include <d3d11.h>
#include <dxgi.h>

namespace MeshSilhouette
{
    Settings g_Settings{};

    namespace
    {
        struct MaskTarget
        {
            ID3D11Texture2D* texture = nullptr;
            ID3D11RenderTargetView* rtv = nullptr;
            ID3D11ShaderResourceView* srv = nullptr;
        };

        MaskTarget g_VisibleMask;
        MaskTarget g_OccludedMask;
        ID3D11Device* g_MaskDevice = nullptr;
        std::uint32_t g_MaskWidth = 0;
        std::uint32_t g_MaskHeight = 0;

        std::atomic<bool> g_ClassifierReady{false};
        std::atomic<bool> g_FrameBegun{false};
        std::atomic<bool> g_CompositeSubmitted{false};
        std::atomic<std::uint8_t> g_WrittenLayers{0};
        std::atomic<Status> g_Status{Status::ClassifierUnavailable};

        template <typename T>
        void SafeRelease(T*& object) noexcept
        {
            if (object) {
                object->Release();
                object = nullptr;
            }
        }

        void ReleaseTarget(MaskTarget& target) noexcept
        {
            SafeRelease(target.rtv);
            SafeRelease(target.srv);
            SafeRelease(target.texture);
        }

        void ResetFrameState() noexcept
        {
            g_FrameBegun.store(false, std::memory_order_release);
            g_CompositeSubmitted.store(false, std::memory_order_release);
            g_WrittenLayers.store(0, std::memory_order_release);
        }

        void ReleaseMaskResources() noexcept
        {
            ResetFrameState();
            ReleaseTarget(g_VisibleMask);
            ReleaseTarget(g_OccludedMask);
            SafeRelease(g_MaskDevice);
            g_MaskWidth = 0;
            g_MaskHeight = 0;
        }

        bool TargetReady(const MaskTarget& target) noexcept
        {
            return target.texture && target.rtv && target.srv;
        }

        bool ResourcesReady() noexcept
        {
            return g_MaskDevice &&
                g_MaskWidth != 0 &&
                g_MaskHeight != 0 &&
                TargetReady(g_VisibleMask) &&
                TargetReady(g_OccludedMask);
        }

        bool CreateTarget(
            ID3D11Device* device,
            const D3D11_TEXTURE2D_DESC& desc,
            MaskTarget& target) noexcept
        {
            if (FAILED(device->CreateTexture2D(
                    &desc, nullptr, &target.texture)) ||
                !target.texture)
                return false;

            if (FAILED(device->CreateRenderTargetView(
                    target.texture, nullptr, &target.rtv)) ||
                !target.rtv) {
                ReleaseTarget(target);
                return false;
            }

            if (FAILED(device->CreateShaderResourceView(
                    target.texture, nullptr, &target.srv)) ||
                !target.srv) {
                ReleaseTarget(target);
                return false;
            }

            return true;
        }

        DXGI_FORMAT ChooseMaskFormat(ID3D11Device* device) noexcept
        {
            UINT support = 0;
            if (SUCCEEDED(device->CheckFormatSupport(
                    DXGI_FORMAT_R8_UNORM, &support))) {
                constexpr UINT required =
                    D3D11_FORMAT_SUPPORT_TEXTURE2D |
                    D3D11_FORMAT_SUPPORT_RENDER_TARGET |
                    D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
                if ((support & required) == required)
                    return DXGI_FORMAT_R8_UNORM;
            }
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        }

        bool EnsureMaskResources(
            ID3D11Device* device,
            std::uint32_t width,
            std::uint32_t height) noexcept
        {
            if (ResourcesReady() &&
                g_MaskDevice == device &&
                g_MaskWidth == width &&
                g_MaskHeight == height)
                return true;

            ReleaseMaskResources();

            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = width;
            desc.Height = height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = ChooseMaskFormat(device);
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags =
                D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

            MaskTarget visible;
            MaskTarget occluded;
            if (!CreateTarget(device, desc, visible) ||
                !CreateTarget(device, desc, occluded)) {
                ReleaseTarget(visible);
                ReleaseTarget(occluded);
                g_Status.store(
                    Status::ResourceCreationFailed,
                    std::memory_order_release);
                return false;
            }

            g_VisibleMask = visible;
            g_OccludedMask = occluded;
            g_MaskDevice = device;
            g_MaskDevice->AddRef();
            g_MaskWidth = width;
            g_MaskHeight = height;
            return true;
        }

        bool ContextMatchesDevice(
            ID3D11Device* device,
            ID3D11DeviceContext* context) noexcept
        {
            ID3D11Device* contextDevice = nullptr;
            context->GetDevice(&contextDevice);
            const bool matches = contextDevice == device;
            SafeRelease(contextDevice);
            return matches;
        }

        std::uint8_t LayerBits(MaskLayer layer) noexcept
        {
            return static_cast<std::uint8_t>(layer) & 0x3u;
        }

        void SetWaitingStatus() noexcept
        {
            if (!g_Settings.enabled) {
                g_Status.store(Status::Disabled, std::memory_order_release);
            } else if (!g_ClassifierReady.load(std::memory_order_acquire)) {
                g_Status.store(
                    Status::ClassifierUnavailable,
                    std::memory_order_release);
            } else {
                g_Status.store(
                    Status::WaitingForFrame,
                    std::memory_order_release);
            }
        }
    }

    Settings& GetSettings() noexcept
    {
        return g_Settings;
    }

    Status GetStatus() noexcept
    {
        if (!g_Settings.enabled) {
            ResetFrameState();
            return Status::Disabled;
        }
        if (!g_ClassifierReady.load(std::memory_order_acquire))
            return Status::ClassifierUnavailable;
        return g_Status.load(std::memory_order_acquire);
    }

    const char* StatusText(Status status) noexcept
    {
        switch (status) {
        case Status::ClassifierUnavailable:
            return "Classifier unavailable";
        case Status::Disabled:
            return "Disabled";
        case Status::WaitingForFrame:
            return "Waiting for mask frame";
        case Status::WaitingForMask:
            return "Waiting for verified mask writes";
        case Status::Ready:
            return "Mask ready";
        case Status::CompositeSubmitted:
            return "Composite submitted";
        case Status::DeviceUnavailable:
            return "D3D11 device unavailable";
        case Status::ResourceCreationFailed:
            return "Mask resource creation failed";
        case Status::ShaderCompilationFailed:
            return "Composite shader unavailable";
        case Status::CompositeTargetUnavailable:
            return "Composite target unavailable";
        default:
            return "Unknown";
        }
    }

    const char* StatusText() noexcept
    {
        return StatusText(GetStatus());
    }

    const char* GetStatusText() noexcept
    {
        return StatusText();
    }

    bool BeginFrame(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        std::uint32_t width,
        std::uint32_t height) noexcept
    {
        ResetFrameState();

        if (!g_Settings.enabled) {
            g_Status.store(Status::Disabled, std::memory_order_release);
            return false;
        }
        if (!g_ClassifierReady.load(std::memory_order_acquire)) {
            g_Status.store(
                Status::ClassifierUnavailable,
                std::memory_order_release);
            return false;
        }
        if (!device || !context || width == 0 || height == 0 ||
            context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE ||
            !ContextMatchesDevice(device, context) ||
            FAILED(device->GetDeviceRemovedReason())) {
            g_Status.store(
                Status::DeviceUnavailable,
                std::memory_order_release);
            return false;
        }

        if (!EnsureMaskResources(device, width, height))
            return false;

        constexpr float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        context->ClearRenderTargetView(g_VisibleMask.rtv, clear);
        context->ClearRenderTargetView(g_OccludedMask.rtv, clear);

        g_FrameBegun.store(true, std::memory_order_release);
        g_Status.store(Status::WaitingForMask, std::memory_order_release);
        return true;
    }

    bool BeginFrame(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        IDXGISwapChain* swap) noexcept
    {
        if (!swap) {
            ResetFrameState();
            g_Status.store(
                Status::DeviceUnavailable,
                std::memory_order_release);
            return false;
        }

        ID3D11Texture2D* backbuffer = nullptr;
        if (FAILED(swap->GetBuffer(
                0,
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(&backbuffer))) ||
            !backbuffer) {
            ResetFrameState();
            g_Status.store(
                Status::DeviceUnavailable,
                std::memory_order_release);
            return false;
        }

        D3D11_TEXTURE2D_DESC desc{};
        backbuffer->GetDesc(&desc);
        backbuffer->Release();
        return BeginFrame(device, context, desc.Width, desc.Height);
    }

    ID3D11RenderTargetView* VisibleMaskRtv() noexcept
    {
        if (!g_Settings.enabled ||
            !g_ClassifierReady.load(std::memory_order_acquire) ||
            !g_FrameBegun.load(std::memory_order_acquire))
            return nullptr;
        return g_VisibleMask.rtv;
    }

    ID3D11RenderTargetView* OccludedMaskRtv() noexcept
    {
        if (!g_Settings.enabled ||
            !g_ClassifierReady.load(std::memory_order_acquire) ||
            !g_FrameBegun.load(std::memory_order_acquire))
            return nullptr;
        return g_OccludedMask.rtv;
    }

    std::uint32_t MaskWidth() noexcept
    {
        return ResourcesReady() ? g_MaskWidth : 0;
    }

    std::uint32_t MaskHeight() noexcept
    {
        return ResourcesReady() ? g_MaskHeight : 0;
    }

    void MarkClassifierReady(bool ready) noexcept
    {
        g_ClassifierReady.store(ready, std::memory_order_release);
        ResetFrameState();
        if (!ready) {
            g_Status.store(
                Status::ClassifierUnavailable,
                std::memory_order_release);
            return;
        }
        SetWaitingStatus();
    }

    bool IsClassifierReady() noexcept
    {
        return g_ClassifierReady.load(std::memory_order_acquire);
    }

    void MarkMaskWritten(MaskLayer layer) noexcept
    {
        if (!g_Settings.enabled ||
            !g_ClassifierReady.load(std::memory_order_acquire) ||
            !g_FrameBegun.load(std::memory_order_acquire) ||
            !ResourcesReady())
            return;

        const std::uint8_t bits = LayerBits(layer);
        if (bits == 0)
            return;

        g_WrittenLayers.fetch_or(bits, std::memory_order_acq_rel);
        g_CompositeSubmitted.store(false, std::memory_order_release);
        g_Status.store(Status::Ready, std::memory_order_release);
    }

    bool IsMaskWritten(MaskLayer layer) noexcept
    {
        const std::uint8_t bits = LayerBits(layer);
        if (bits == 0)
            return false;
        const std::uint8_t written =
            g_WrittenLayers.load(std::memory_order_acquire);
        return (written & bits) == bits;
    }

    void InvalidateDeviceObjects() noexcept
    {
        ReleaseMaskResources();
        SetWaitingStatus();
    }

    void Shutdown() noexcept
    {
        ReleaseMaskResources();
        g_ClassifierReady.store(false, std::memory_order_release);
        g_Status.store(
            Status::ClassifierUnavailable,
            std::memory_order_release);
    }

    namespace Detail
    {
        bool GetCompositeInputs(
            ID3D11Device* device,
            std::uint32_t width,
            std::uint32_t height,
            CompositeInputs& inputs) noexcept
        {
            inputs = {};

            if (!g_Settings.enabled) {
                ResetFrameState();
                g_Status.store(Status::Disabled, std::memory_order_release);
                return false;
            }
            if (!g_ClassifierReady.load(std::memory_order_acquire)) {
                g_Status.store(
                    Status::ClassifierUnavailable,
                    std::memory_order_release);
                return false;
            }
            if (!g_FrameBegun.load(std::memory_order_acquire)) {
                g_Status.store(
                    Status::WaitingForFrame,
                    std::memory_order_release);
                return false;
            }
            if (!device ||
                g_MaskDevice != device ||
                g_MaskWidth != width ||
                g_MaskHeight != height) {
                ResetFrameState();
                g_Status.store(
                    Status::WaitingForFrame,
                    std::memory_order_release);
                return false;
            }
            if (!ResourcesReady()) {
                ResetFrameState();
                g_Status.store(
                    Status::ResourceCreationFailed,
                    std::memory_order_release);
                return false;
            }

            const std::uint8_t written =
                g_WrittenLayers.load(std::memory_order_acquire);
            if (written == 0) {
                g_Status.store(
                    Status::WaitingForMask,
                    std::memory_order_release);
                return false;
            }
            if (g_CompositeSubmitted.load(std::memory_order_acquire)) {
                g_Status.store(
                    Status::CompositeSubmitted,
                    std::memory_order_release);
                return false;
            }

            inputs.visible = g_VisibleMask.srv;
            inputs.occluded = g_OccludedMask.srv;
            inputs.width = g_MaskWidth;
            inputs.height = g_MaskHeight;
            inputs.visibleWritten =
                (written & LayerBits(MaskLayer::Visible)) != 0;
            inputs.occludedWritten =
                (written & LayerBits(MaskLayer::Occluded)) != 0;
            g_Status.store(Status::Ready, std::memory_order_release);
            return true;
        }

        void MarkCompositeSubmitted() noexcept
        {
            if (!g_FrameBegun.load(std::memory_order_acquire) ||
                g_WrittenLayers.load(std::memory_order_acquire) == 0)
                return;
            g_CompositeSubmitted.store(true, std::memory_order_release);
            g_Status.store(
                Status::CompositeSubmitted,
                std::memory_order_release);
        }

        void MarkFailure(Status failure) noexcept
        {
            switch (failure) {
            case Status::DeviceUnavailable:
            case Status::ResourceCreationFailed:
            case Status::ShaderCompilationFailed:
            case Status::CompositeTargetUnavailable:
                g_Status.store(failure, std::memory_order_release);
                break;
            default:
                break;
            }
        }
    }
}
