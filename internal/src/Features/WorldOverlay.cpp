#include "WorldOverlay.h"

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <d3d11.h>
#include <d3dcompiler.h> // removed
#include <dxgi.h>
// #pragma comment(lib, "d3dcompiler.lib")

namespace WorldOverlay
{
    namespace
    {
        constexpr UINT kClassInstanceCapacity = 256;
        constexpr UINT kRenderTargetCount =
            D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
        constexpr UINT kViewportCount =
            D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        constexpr UINT kUavCapacity =
            D3D11_PS_CS_UAV_REGISTER_COUNT;

        ID3D11Device* g_CompositeDevice = nullptr;
        ID3D11VertexShader* g_CompositeVs = nullptr;
        ID3D11PixelShader* g_CompositePs = nullptr;
        ID3D11Buffer* g_CompositeConstants = nullptr;
        ID3D11SamplerState* g_CompositeSampler = nullptr;
        ID3D11BlendState* g_CompositeBlend = nullptr;
        ID3D11DepthStencilState* g_CompositeDepth = nullptr;
        ID3D11RasterizerState* g_CompositeRasterizer = nullptr;

        template <typename T>
        void SafeRelease(T*& object) noexcept
        {
            if (object) {
                object->Release();
                object = nullptr;
            }
        }

        void ReleaseCompositeResources() noexcept
        {
            SafeRelease(g_CompositeRasterizer);
            SafeRelease(g_CompositeDepth);
            SafeRelease(g_CompositeBlend);
            SafeRelease(g_CompositeSampler);
            SafeRelease(g_CompositeConstants);
            SafeRelease(g_CompositePs);
            SafeRelease(g_CompositeVs);
            SafeRelease(g_CompositeDevice);
        }

        constexpr const char* kCompositeShader = R"(
Texture2D<float4> VisibleMask : register(t0);
Texture2D<float4> OccludedMask : register(t1);
SamplerState MaskSampler : register(s0);

cbuffer CompositeConstants : register(b0)
{
    float4 VisibleColor;
    float4 OccludedColor;
    float2 TexelSize;
    float Opacity;
    float EdgeWidth;
    float Softness;
    float FillEnabled;
    float EdgeEnabled;
    float VisibleWritten;
    float OccludedWritten;
    float3 Padding;
};

struct VsOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VsOutput VsMain(uint vertexId : SV_VertexID)
{
    VsOutput output;
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position =
        float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    output.uv = uv;
    return output;
}

float2 ReadMasks(float2 uv)
{
    float visible = VisibleMask.SampleLevel(MaskSampler, uv, 0.0).r;
    float occluded = OccludedMask.SampleLevel(MaskSampler, uv, 0.0).r;
    return saturate(float2(
        visible * VisibleWritten,
        occluded * OccludedWritten));
}

float4 PsMain(VsOutput input) : SV_TARGET
{
    float2 center = ReadMasks(input.uv);
    float2 expanded = center;

    float outerRadius = min(EdgeWidth + Softness, 8.0);
    float innerRadius = max(EdgeWidth - Softness, 0.0);

    [loop]
    for (int y = -8; y <= 8; ++y)
    {
        [loop]
        for (int x = -8; x <= 8; ++x)
        {
            float distanceFromCenter = length(float2(x, y));
            if (distanceFromCenter > outerRadius)
                continue;

            float weight = 0.0;
            if (Softness <= 0.001)
            {
                weight =
                    distanceFromCenter <= EdgeWidth ? 1.0 : 0.0;
            }
            else
            {
                weight = 1.0 - smoothstep(
                    innerRadius,
                    max(outerRadius, innerRadius + 0.001),
                    distanceFromCenter);
            }

            float2 sampleUv =
                saturate(input.uv + float2(x, y) * TexelSize);
            expanded = max(expanded, ReadMasks(sampleUv) * weight);
        }
    }

    float visibleFill = FillEnabled * center.x;
    float visibleEdge =
        EdgeEnabled * saturate(expanded.x - center.x);
    float visibleCoverage = saturate(visibleFill + visibleEdge);

    // A visible fragment wins over the through-wall layer.
    float occludedFill =
        FillEnabled * center.y * (1.0 - center.x);
    float occludedEdge =
        EdgeEnabled * saturate(expanded.y - center.y) *
        (1.0 - expanded.x);
    float occludedCoverage = saturate(occludedFill + occludedEdge);

    float visibleWeight =
        visibleCoverage * saturate(VisibleColor.a);
    float occludedWeight =
        occludedCoverage * saturate(OccludedColor.a) *
        (1.0 - visibleWeight);
    float combinedWeight = visibleWeight + occludedWeight;
    float alpha = saturate(combinedWeight * Opacity);

    float3 color = (
        VisibleColor.rgb * visibleWeight +
        OccludedColor.rgb * occludedWeight) /
        max(combinedWeight, 0.0001);
    return float4(color, alpha);
}
)";

        struct CompositeConstants
        {
            float visibleColor[4];
            float occludedColor[4];
            float texelSize[2];
            float opacity;
            float edgeWidth;
            float softness;
            float fillEnabled;
            float edgeEnabled;
            float visibleWritten;
            float occludedWritten;
            float padding[3];
        };

        static_assert(
            sizeof(CompositeConstants) % 16 == 0,
            "D3D constant buffers must be 16-byte aligned");

        bool CompileShader(
            const char* entry,
            const char* profile,
            ID3DBlob** output) noexcept
        {
            if (output) *output = nullptr;
            return false;
        }

        bool CompositeResourcesReady() noexcept
        {
            return g_CompositeDevice &&
                g_CompositeVs &&
                g_CompositePs &&
                g_CompositeConstants &&
                g_CompositeSampler &&
                g_CompositeBlend &&
                g_CompositeDepth &&
                g_CompositeRasterizer;
        }

        bool EnsureCompositeResources(ID3D11Device* device) noexcept
        {
            if (CompositeResourcesReady() && g_CompositeDevice == device)
                return true;

            ReleaseCompositeResources();
            if (!device)
                return false;

            ID3DBlob* vsBlob = nullptr;
            ID3DBlob* psBlob = nullptr;
            if (!CompileShader("VsMain", "vs_4_0", &vsBlob) ||
                !CompileShader("PsMain", "ps_4_0", &psBlob)) {
                SafeRelease(vsBlob);
                SafeRelease(psBlob);
                return false;
            }

            bool success =
                SUCCEEDED(device->CreateVertexShader(
                    vsBlob->GetBufferPointer(),
                    vsBlob->GetBufferSize(),
                    nullptr,
                    &g_CompositeVs)) &&
                g_CompositeVs &&
                SUCCEEDED(device->CreatePixelShader(
                    psBlob->GetBufferPointer(),
                    psBlob->GetBufferSize(),
                    nullptr,
                    &g_CompositePs)) &&
                g_CompositePs;
            SafeRelease(vsBlob);
            SafeRelease(psBlob);

            D3D11_BUFFER_DESC bufferDesc{};
            bufferDesc.ByteWidth = sizeof(CompositeConstants);
            bufferDesc.Usage = D3D11_USAGE_DEFAULT;
            bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            success = success &&
                SUCCEEDED(device->CreateBuffer(
                    &bufferDesc, nullptr, &g_CompositeConstants)) &&
                g_CompositeConstants;

            D3D11_SAMPLER_DESC samplerDesc{};
            samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
            samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
            samplerDesc.MinLOD = 0.0f;
            samplerDesc.MaxLOD = FLT_MAX;
            success = success &&
                SUCCEEDED(device->CreateSamplerState(
                    &samplerDesc, &g_CompositeSampler)) &&
                g_CompositeSampler;

            D3D11_BLEND_DESC blendDesc{};
            D3D11_RENDER_TARGET_BLEND_DESC& target =
                blendDesc.RenderTarget[0];
            target.BlendEnable = TRUE;
            target.SrcBlend = D3D11_BLEND_SRC_ALPHA;
            target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            target.BlendOp = D3D11_BLEND_OP_ADD;
            // Preserve the swapchain alpha channel.
            target.SrcBlendAlpha = D3D11_BLEND_ZERO;
            target.DestBlendAlpha = D3D11_BLEND_ONE;
            target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
            target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            success = success &&
                SUCCEEDED(device->CreateBlendState(
                    &blendDesc, &g_CompositeBlend)) &&
                g_CompositeBlend;

            D3D11_DEPTH_STENCIL_DESC depthDesc{};
            depthDesc.DepthEnable = FALSE;
            depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            depthDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
            depthDesc.StencilEnable = FALSE;
            success = success &&
                SUCCEEDED(device->CreateDepthStencilState(
                    &depthDesc, &g_CompositeDepth)) &&
                g_CompositeDepth;

            D3D11_RASTERIZER_DESC rasterizerDesc{};
            rasterizerDesc.FillMode = D3D11_FILL_SOLID;
            rasterizerDesc.CullMode = D3D11_CULL_NONE;
            rasterizerDesc.DepthClipEnable = TRUE;
            rasterizerDesc.ScissorEnable = FALSE;
            rasterizerDesc.MultisampleEnable = TRUE;
            success = success &&
                SUCCEEDED(device->CreateRasterizerState(
                    &rasterizerDesc, &g_CompositeRasterizer)) &&
                g_CompositeRasterizer;

            if (!success) {
                ReleaseCompositeResources();
                return false;
            }

            g_CompositeDevice = device;
            g_CompositeDevice->AddRef();
            return true;
        }

        class ScopedPipelineState
        {
        public:
            explicit ScopedPipelineState(
                ID3D11DeviceContext* context) noexcept
                : context_(context)
            {
                if (!context_)
                    return;

                context_->OMGetRenderTargets(
                    kRenderTargetCount, renderTargets_, &depthView_);
                for (UINT index = 0;
                     index < kRenderTargetCount;
                     ++index)
                {
                    if (renderTargets_[index])
                        renderTargetCount_ = index + 1;
                }
                uavStartSlot_ = renderTargetCount_;
                uavCount_ = kUavCapacity > uavStartSlot_
                    ? kUavCapacity - uavStartSlot_
                    : 0;
                if (uavCount_ > 0)
                {
                    context_->
                        OMGetRenderTargetsAndUnorderedAccessViews(
                            0,
                            nullptr,
                            nullptr,
                            uavStartSlot_,
                            uavCount_,
                            unorderedAccessViews_);
                }
                context_->OMGetBlendState(
                    &blendState_, blendFactor_, &sampleMask_);
                context_->OMGetDepthStencilState(
                    &depthState_, &stencilRef_);
                context_->RSGetState(&rasterizerState_);
                context_->RSGetViewports(
                    &viewportCount_, viewports_);
                context_->RSGetScissorRects(
                    &scissorCount_, scissorRects_);

                context_->IAGetInputLayout(&inputLayout_);
                context_->IAGetPrimitiveTopology(&topology_);

                context_->VSGetShader(
                    &vertexShader_,
                    vertexInstances_,
                    &vertexInstanceCount_);
                context_->PSGetShader(
                    &pixelShader_,
                    pixelInstances_,
                    &pixelInstanceCount_);
                context_->GSGetShader(
                    &geometryShader_,
                    geometryInstances_,
                    &geometryInstanceCount_);
                context_->HSGetShader(
                    &hullShader_,
                    hullInstances_,
                    &hullInstanceCount_);
                context_->DSGetShader(
                    &domainShader_,
                    domainInstances_,
                    &domainInstanceCount_);
                context_->CSGetShader(
                    &computeShader_,
                    computeInstances_,
                    &computeInstanceCount_);

                context_->PSGetConstantBuffers(
                    0, 1, &pixelConstants_);
                context_->PSGetShaderResources(
                    0, 2, pixelResources_);
                context_->PSGetSamplers(
                    0, 1, &pixelSampler_);
                context_->GetPredication(
                    &predicate_, &predicateValue_);
                captured_ = true;
            }

            ScopedPipelineState(const ScopedPipelineState&) = delete;
            ScopedPipelineState& operator=(
                const ScopedPipelineState&) = delete;

            ~ScopedPipelineState() noexcept
            {
                Restore();
                ReleaseReferences();
            }

        private:
            void Restore() noexcept
            {
                if (!captured_)
                    return;

                ID3D11ShaderResourceView* nullResources[2] = {};
                context_->PSSetShaderResources(
                    0, 2, nullResources);

                if (uavCount_ > 0)
                {
                    UINT initialCounts[kUavCapacity]{};
                    for (UINT index = 0;
                         index < kUavCapacity;
                         ++index)
                        initialCounts[index] = 0xffffffffu;
                    context_->
                        OMSetRenderTargetsAndUnorderedAccessViews(
                            renderTargetCount_,
                            renderTargets_,
                            depthView_,
                            uavStartSlot_,
                            uavCount_,
                            unorderedAccessViews_,
                            initialCounts);
                }
                else
                {
                    context_->OMSetRenderTargets(
                        renderTargetCount_,
                        renderTargets_,
                        depthView_);
                }
                context_->OMSetBlendState(
                    blendState_, blendFactor_, sampleMask_);
                context_->OMSetDepthStencilState(
                    depthState_, stencilRef_);
                context_->RSSetState(rasterizerState_);
                context_->RSSetViewports(
                    viewportCount_,
                    viewportCount_ ? viewports_ : nullptr);
                context_->RSSetScissorRects(
                    scissorCount_,
                    scissorCount_ ? scissorRects_ : nullptr);

                context_->IASetInputLayout(inputLayout_);
                context_->IASetPrimitiveTopology(topology_);

                context_->VSSetShader(
                    vertexShader_,
                    vertexInstances_,
                    vertexInstanceCount_);
                context_->PSSetShader(
                    pixelShader_,
                    pixelInstances_,
                    pixelInstanceCount_);
                context_->GSSetShader(
                    geometryShader_,
                    geometryInstances_,
                    geometryInstanceCount_);
                context_->HSSetShader(
                    hullShader_,
                    hullInstances_,
                    hullInstanceCount_);
                context_->DSSetShader(
                    domainShader_,
                    domainInstances_,
                    domainInstanceCount_);
                context_->CSSetShader(
                    computeShader_,
                    computeInstances_,
                    computeInstanceCount_);

                context_->PSSetConstantBuffers(
                    0, 1, &pixelConstants_);
                context_->PSSetShaderResources(
                    0, 2, pixelResources_);
                context_->PSSetSamplers(
                    0, 1, &pixelSampler_);
                context_->SetPredication(
                    predicate_, predicateValue_);
            }

            template <typename T, UINT Count>
            void ReleaseArray(T* (&objects)[Count]) noexcept
            {
                for (UINT index = 0; index < Count; ++index)
                    SafeRelease(objects[index]);
            }

            void ReleaseReferences() noexcept
            {
                ReleaseArray(renderTargets_);
                ReleaseArray(unorderedAccessViews_);
                SafeRelease(depthView_);
                SafeRelease(blendState_);
                SafeRelease(depthState_);
                SafeRelease(rasterizerState_);
                SafeRelease(inputLayout_);
                SafeRelease(vertexShader_);
                SafeRelease(pixelShader_);
                SafeRelease(geometryShader_);
                SafeRelease(hullShader_);
                SafeRelease(domainShader_);
                SafeRelease(computeShader_);
                SafeRelease(pixelConstants_);
                ReleaseArray(pixelResources_);
                SafeRelease(pixelSampler_);
                SafeRelease(predicate_);

                ReleaseArray(vertexInstances_);
                ReleaseArray(pixelInstances_);
                ReleaseArray(geometryInstances_);
                ReleaseArray(hullInstances_);
                ReleaseArray(domainInstances_);
                ReleaseArray(computeInstances_);
            }

            ID3D11DeviceContext* context_ = nullptr;
            bool captured_ = false;

            ID3D11RenderTargetView*
                renderTargets_[kRenderTargetCount]{};
            UINT renderTargetCount_ = 0;
            ID3D11DepthStencilView* depthView_ = nullptr;
            ID3D11UnorderedAccessView*
                unorderedAccessViews_[kUavCapacity]{};
            UINT uavStartSlot_ = 0;
            UINT uavCount_ = 0;
            ID3D11BlendState* blendState_ = nullptr;
            FLOAT blendFactor_[4]{};
            UINT sampleMask_ = 0xffffffffu;
            ID3D11DepthStencilState* depthState_ = nullptr;
            UINT stencilRef_ = 0;
            ID3D11RasterizerState* rasterizerState_ = nullptr;
            D3D11_VIEWPORT viewports_[kViewportCount]{};
            UINT viewportCount_ = kViewportCount;
            D3D11_RECT scissorRects_[kViewportCount]{};
            UINT scissorCount_ = kViewportCount;

            ID3D11InputLayout* inputLayout_ = nullptr;
            D3D11_PRIMITIVE_TOPOLOGY topology_ =
                D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

            ID3D11VertexShader* vertexShader_ = nullptr;
            ID3D11PixelShader* pixelShader_ = nullptr;
            ID3D11GeometryShader* geometryShader_ = nullptr;
            ID3D11HullShader* hullShader_ = nullptr;
            ID3D11DomainShader* domainShader_ = nullptr;
            ID3D11ComputeShader* computeShader_ = nullptr;
            ID3D11ClassInstance*
                vertexInstances_[kClassInstanceCapacity]{};
            ID3D11ClassInstance*
                pixelInstances_[kClassInstanceCapacity]{};
            ID3D11ClassInstance*
                geometryInstances_[kClassInstanceCapacity]{};
            ID3D11ClassInstance*
                hullInstances_[kClassInstanceCapacity]{};
            ID3D11ClassInstance*
                domainInstances_[kClassInstanceCapacity]{};
            ID3D11ClassInstance*
                computeInstances_[kClassInstanceCapacity]{};
            UINT vertexInstanceCount_ = kClassInstanceCapacity;
            UINT pixelInstanceCount_ = kClassInstanceCapacity;
            UINT geometryInstanceCount_ = kClassInstanceCapacity;
            UINT hullInstanceCount_ = kClassInstanceCapacity;
            UINT domainInstanceCount_ = kClassInstanceCapacity;
            UINT computeInstanceCount_ = kClassInstanceCapacity;

            ID3D11Buffer* pixelConstants_ = nullptr;
            ID3D11ShaderResourceView* pixelResources_[2]{};
            ID3D11SamplerState* pixelSampler_ = nullptr;
            ID3D11Predicate* predicate_ = nullptr;
            BOOL predicateValue_ = FALSE;
        };

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

        DXGI_FORMAT TypedRtvFormat(DXGI_FORMAT format) noexcept
        {
            switch (format) {
            case DXGI_FORMAT_R8G8B8A8_TYPELESS:
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            case DXGI_FORMAT_B8G8R8A8_TYPELESS:
                return DXGI_FORMAT_B8G8R8A8_UNORM;
            case DXGI_FORMAT_R16G16B16A16_TYPELESS:
                return DXGI_FORMAT_R16G16B16A16_FLOAT;
            case DXGI_FORMAT_R10G10B10A2_TYPELESS:
                return DXGI_FORMAT_R10G10B10A2_UNORM;
            default:
                return format;
            }
        }

        bool CreateBackbufferRtv(
            ID3D11Device* device,
            ID3D11Texture2D* backbuffer,
            const D3D11_TEXTURE2D_DESC& textureDesc,
            ID3D11RenderTargetView** output) noexcept
        {
            if (!output)
                return false;
            *output = nullptr;

            D3D11_RENDER_TARGET_VIEW_DESC viewDesc{};
            viewDesc.Format = TypedRtvFormat(textureDesc.Format);
            if (textureDesc.SampleDesc.Count > 1) {
                viewDesc.ViewDimension =
                    D3D11_RTV_DIMENSION_TEXTURE2DMS;
            } else {
                viewDesc.ViewDimension =
                    D3D11_RTV_DIMENSION_TEXTURE2D;
                viewDesc.Texture2D.MipSlice = 0;
            }

            HRESULT result = device->CreateRenderTargetView(
                backbuffer, &viewDesc, output);
            if (FAILED(result) || !*output) {
                SafeRelease(*output);
                result = device->CreateRenderTargetView(
                    backbuffer, nullptr, output);
            }
            return SUCCEEDED(result) && *output;
        }

        float Clamp01(float value) noexcept
        {
            return (std::max)(0.0f, (std::min)(value, 1.0f));
        }

        float ClampRadius(float value) noexcept
        {
            return (std::max)(0.0f, (std::min)(value, 8.0f));
        }

        void StoreColor(
            float (&destination)[4],
            const MeshSilhouette::Color& source) noexcept
        {
            destination[0] = Clamp01(source.r);
            destination[1] = Clamp01(source.g);
            destination[2] = Clamp01(source.b);
            destination[3] = Clamp01(source.a);
        }
    }

    void ComposeAfterCapture(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        IDXGISwapChain* swap) noexcept
    {
        // No classifier is installed here. In particular, this module never
        // installs a generic DrawIndexed detour.
        if (!device || !context || !swap ||
            context->GetType() != D3D11_DEVICE_CONTEXT_IMMEDIATE ||
            !ContextMatchesDevice(device, context)) {
            MeshSilhouette::Detail::MarkFailure(
                MeshSilhouette::Status::DeviceUnavailable);
            return;
        }

        if (FAILED(device->GetDeviceRemovedReason())) {
            InvalidateDeviceObjects();
            MeshSilhouette::Detail::MarkFailure(
                MeshSilhouette::Status::DeviceUnavailable);
            return;
        }

        ID3D11Device* swapDevice = nullptr;
        if (FAILED(swap->GetDevice(
                __uuidof(ID3D11Device),
                reinterpret_cast<void**>(&swapDevice))) ||
            swapDevice != device) {
            SafeRelease(swapDevice);
            MeshSilhouette::Detail::MarkFailure(
                MeshSilhouette::Status::DeviceUnavailable);
            return;
        }
        SafeRelease(swapDevice);

        ID3D11Texture2D* backbuffer = nullptr;
        if (FAILED(swap->GetBuffer(
                0,
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(&backbuffer))) ||
            !backbuffer) {
            MeshSilhouette::Detail::MarkFailure(
                MeshSilhouette::Status::CompositeTargetUnavailable);
            return;
        }

        D3D11_TEXTURE2D_DESC backbufferDesc{};
        backbuffer->GetDesc(&backbufferDesc);

        MeshSilhouette::Detail::CompositeInputs inputs;
        if (!MeshSilhouette::Detail::GetCompositeInputs(
                device,
                backbufferDesc.Width,
                backbufferDesc.Height,
                inputs)) {
            backbuffer->Release();
            return;
        }

        const MeshSilhouette::Settings settings =
            MeshSilhouette::GetSettings();
        if ((!settings.fill && !settings.edge) ||
            settings.opacity <= 0.0f) {
            backbuffer->Release();
            return;
        }

        if (!EnsureCompositeResources(device)) {
            backbuffer->Release();
            MeshSilhouette::Detail::MarkFailure(
                MeshSilhouette::Status::ShaderCompilationFailed);
            return;
        }

        ID3D11RenderTargetView* target = nullptr;
        if (!CreateBackbufferRtv(
                device, backbuffer, backbufferDesc, &target)) {
            backbuffer->Release();
            MeshSilhouette::Detail::MarkFailure(
                MeshSilhouette::Status::CompositeTargetUnavailable);
            return;
        }
        backbuffer->Release();

        CompositeConstants constants{};
        StoreColor(constants.visibleColor, settings.visibleColor);
        StoreColor(constants.occludedColor, settings.occludedColor);
        constants.texelSize[0] = 1.0f / inputs.width;
        constants.texelSize[1] = 1.0f / inputs.height;
        constants.opacity = Clamp01(settings.opacity);
        constants.edgeWidth = ClampRadius(settings.edgeWidth);
        constants.softness = ClampRadius(settings.softness);
        constants.fillEnabled = settings.fill ? 1.0f : 0.0f;
        constants.edgeEnabled = settings.edge ? 1.0f : 0.0f;
        constants.visibleWritten =
            inputs.visibleWritten ? 1.0f : 0.0f;
        constants.occludedWritten =
            inputs.occludedWritten ? 1.0f : 0.0f;
        context->UpdateSubresource(
            g_CompositeConstants, 0, nullptr, &constants, 0, 0);

        {
            ScopedPipelineState savedState(context);

            ID3D11ShaderResourceView* maskResources[2] = {
                inputs.visible,
                inputs.occluded
            };
            D3D11_VIEWPORT viewport{};
            viewport.Width =
                static_cast<FLOAT>(backbufferDesc.Width);
            viewport.Height =
                static_cast<FLOAT>(backbufferDesc.Height);
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;

            context->SetPredication(nullptr, FALSE);
            context->OMSetRenderTargets(1, &target, nullptr);
            context->OMSetBlendState(
                g_CompositeBlend, nullptr, 0xffffffffu);
            context->OMSetDepthStencilState(
                g_CompositeDepth, 0);
            context->RSSetState(g_CompositeRasterizer);
            context->RSSetViewports(1, &viewport);

            context->IASetInputLayout(nullptr);
            context->IASetPrimitiveTopology(
                D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context->VSSetShader(g_CompositeVs, nullptr, 0);
            context->PSSetShader(g_CompositePs, nullptr, 0);
            context->GSSetShader(nullptr, nullptr, 0);
            context->HSSetShader(nullptr, nullptr, 0);
            context->DSSetShader(nullptr, nullptr, 0);
            context->PSSetConstantBuffers(
                0, 1, &g_CompositeConstants);
            context->PSSetShaderResources(
                0, 2, maskResources);
            context->PSSetSamplers(
                0, 1, &g_CompositeSampler);
            context->Draw(3, 0);
        }

        target->Release();
        MeshSilhouette::Detail::MarkCompositeSubmitted();
    }

    ID3D11RenderTargetView* GlowMaskRtv() noexcept
    {
        return MeshSilhouette::VisibleMaskRtv();
    }

    bool BeginMaskFrame(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        std::uint32_t width,
        std::uint32_t height) noexcept
    {
        return MeshSilhouette::BeginFrame(
            device, context, width, height);
    }

    bool BeginMaskFrame(
        ID3D11Device* device,
        ID3D11DeviceContext* context,
        IDXGISwapChain* swap) noexcept
    {
        return MeshSilhouette::BeginFrame(
            device, context, swap);
    }

    ID3D11RenderTargetView* VisibleMaskRtv() noexcept
    {
        return MeshSilhouette::VisibleMaskRtv();
    }

    ID3D11RenderTargetView* OccludedMaskRtv() noexcept
    {
        return MeshSilhouette::OccludedMaskRtv();
    }

    void MarkClassifierReady(bool ready) noexcept
    {
        MeshSilhouette::MarkClassifierReady(ready);
    }

    void MarkMaskWritten(
        MeshSilhouette::MaskLayer layer) noexcept
    {
        MeshSilhouette::MarkMaskWritten(layer);
    }

    MeshSilhouette::Status MaskStatus() noexcept
    {
        return MeshSilhouette::GetStatus();
    }

    const char* MaskStatusText() noexcept
    {
        return MeshSilhouette::StatusText();
    }

    void InvalidateDeviceObjects() noexcept
    {
        ReleaseCompositeResources();
        MeshSilhouette::InvalidateDeviceObjects();
    }

    void Shutdown() noexcept
    {
        ReleaseCompositeResources();
        MeshSilhouette::Shutdown();
    }
}
