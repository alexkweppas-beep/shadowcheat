#include "NvidiaBypass.h"
#include <d3dcompiler.h>
#include <dwmapi.h>
#include <thread>
#include <atomic>
#include <vector>
#include <cstring>
#include <imgui.h>
#include "ShadowPlayPatcher.h"

namespace hooks { void HLog(const char* msg) noexcept; }

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace NvidiaBypass {

static bool s_Enabled = false;
static int s_Available = -1;
static char s_Status[128] = "Idle";
static HWND s_GameHwnd = nullptr;
static HWND s_OvHwnd = nullptr;
static const wchar_t kWC[] = L"NvBpOvW";
static int s_LastX = -9999, s_LastY = -9999, s_LastW = 0, s_LastH = 0;

static std::thread s_PatcherThread;
static std::atomic<bool> s_PatcherStop{false};

static void PatcherLoop() {
    patcher::Patcher sp;
    while (!s_PatcherStop) {
        auto results = sp.patch_all_targets();
        if (!results.empty()) {
            char buf[256];
            wsprintfA(buf, "NvidiaBypass: Patched %zu locations in Nvidia processes.", results.size());
            hooks::HLog(buf);
        } else {
            // Uncomment to debug if it finds nothing
            // hooks::HLog("NvidiaBypass: Patcher found 0 targets (Access Denied or already patched?).");
        }
        for (int i = 0; i < 50 && !s_PatcherStop; ++i) Sleep(100);
    }
}

static const char kVSrc[] = R"HLSL(
cbuffer CB : register(b0) { float4x4 Proj; }
struct VI { float2 pos:POSITION; float2 uv:TEXCOORD0; float4 col:COLOR0; };
struct PI { float4 pos:SV_POSITION; float4 col:COLOR0; float2 uv:TEXCOORD0; };
PI main(VI i){PI o;o.pos=mul(Proj,float4(i.pos,0,1));o.col=i.col;o.uv=i.uv;return o;}
)HLSL";
static const char kPSrc[] = R"HLSL(
struct PI { float4 pos:SV_POSITION; float4 col:COLOR0; float2 uv:TEXCOORD0; };
sampler smp:register(s0); Texture2D tex:register(t0);
float4 main(PI i):SV_Target{
    float4 c = i.col * tex.Sample(smp, i.uv);
    // Premultiply alpha for DWM compositing.
    // D3D11 handles BGRA<->RGBA mapping internally, no manual channel swap needed.
    return float4(c.r * c.a, c.g * c.a, c.b * c.a, c.a);
}
)HLSL";

struct Renderer {
    ID3D11Device*             Dev   = nullptr;
    ID3D11DeviceContext*      Ctx   = nullptr;
    IDXGISwapChain*           SC    = nullptr;

    ID3D11RenderTargetView*   RTV   = nullptr;
    ID3D11VertexShader*       VS    = nullptr;
    ID3D11PixelShader*        PS    = nullptr;
    ID3D11InputLayout*        IL    = nullptr;
    ID3D11Buffer*             VB    = nullptr;
    ID3D11Buffer*             IB    = nullptr;
    ID3D11Buffer*             CB    = nullptr;
    ID3D11BlendState*         BS    = nullptr;
    ID3D11RasterizerState*    RS    = nullptr;
    ID3D11DepthStencilState*  DSS   = nullptr;
    ID3D11SamplerState*       Smp   = nullptr;
    ID3D11ShaderResourceView* Font  = nullptr;
    int VBSz = 0, IBSz = 0;

    void Release() {
#define REL(p) if(p){p->Release();p=nullptr;}
        REL(Font) REL(Smp) REL(DSS) REL(RS) REL(BS)
        REL(CB) REL(IB) REL(VB) REL(IL) REL(PS) REL(VS)
        REL(RTV) REL(SC) REL(Ctx) REL(Dev)
#undef REL
        VBSz = IBSz = 0;
    }

    bool Init(HWND hwnd, UINT w, UINT h) {
        D3D_FEATURE_LEVEL fl;
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &Dev, &fl, &Ctx))) {
            hooks::HLog("NvidiaBypass: D3D11CreateDevice failed");
            return false;
        }

        IDXGIDevice* dxgiDev = nullptr; IDXGIAdapter* ada = nullptr; IDXGIFactory* fac = nullptr;
        Dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev);
        if (dxgiDev) dxgiDev->GetAdapter(&ada);
        if (ada) ada->GetParent(__uuidof(IDXGIFactory), (void**)&fac);
        
        bool scOk = false;
        if (fac) {
            DXGI_SWAP_CHAIN_DESC sd{};
            sd.BufferCount = 1;
            sd.BufferDesc.Width = w; sd.BufferDesc.Height = h;
            // BGRA is required by DWM for alpha compositing in layered windows
            sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.OutputWindow = hwnd; sd.SampleDesc = {1,0};
            sd.Windowed = TRUE;
            sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            sd.Flags = 0;
            if (SUCCEEDED(fac->CreateSwapChain(Dev, &sd, &SC))) {
                fac->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
                scOk = true;
            } else {
                hooks::HLog("NvidiaBypass: CreateSwapChain failed");
                lstrcpyA(s_Status, "Failed: CreateSwapChain");
            }
            fac->Release();
        }
        if (ada) ada->Release();
        if (dxgiDev) dxgiDev->Release();
        if (!scOk) return false;

        ID3D11Texture2D* bb = nullptr;
        SC->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb);
        if (bb) { Dev->CreateRenderTargetView(bb, nullptr, &RTV); bb->Release(); }
        if (!RTV) return false;

        // Pre-clear to transparent black immediately so DWM never shows green on first frame
        {
            const float zero[4] = { 0, 0, 0, 0 };
            Ctx->ClearRenderTargetView(RTV, zero);
            SC->Present(0, 0);
        }

        auto compile = [&](const char* src, const char* tgt, auto** out) {
            ID3DBlob *blob=nullptr, *err=nullptr;
            D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, "main", tgt, 0, 0, &blob, &err);
            if (err) err->Release();
            return blob;
        };
        ID3DBlob* vsb = compile(kVSrc, "vs_4_0", &VS);
        ID3DBlob* psb = compile(kPSrc, "ps_4_0", &PS);
        if (vsb && psb) {
            Dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &VS);
            Dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &PS);
            D3D11_INPUT_ELEMENT_DESC ied[] = {
                {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,  0,(UINT)offsetof(ImDrawVert,pos),D3D11_INPUT_PER_VERTEX_DATA,0},
                {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,  0,(UINT)offsetof(ImDrawVert,uv), D3D11_INPUT_PER_VERTEX_DATA,0},
                {"COLOR",   0,DXGI_FORMAT_R8G8B8A8_UNORM,0,(UINT)offsetof(ImDrawVert,col),D3D11_INPUT_PER_VERTEX_DATA,0},
            };
            Dev->CreateInputLayout(ied, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &IL);
        }
        if (vsb) vsb->Release(); if (psb) psb->Release();

        D3D11_BUFFER_DESC bd{sizeof(float)*16, D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE};
        Dev->CreateBuffer(&bd, nullptr, &CB);

        // Premultiplied alpha blend for DWM compositing
        // SrcBlend=ONE because pixel shader already multiplied rgb*a
        D3D11_BLEND_DESC bld{};
        bld.RenderTarget[0].BlendEnable = TRUE;
        bld.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;           // premultiplied
        bld.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bld.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bld.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bld.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bld.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bld.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        Dev->CreateBlendState(&bld, &BS);

        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE;
        rd.ScissorEnable=TRUE; rd.DepthClipEnable=TRUE;
        Dev->CreateRasterizerState(&rd, &RS);

        D3D11_DEPTH_STENCIL_DESC dd{};
        Dev->CreateDepthStencilState(&dd, &DSS);

        D3D11_SAMPLER_DESC smpd{};
        smpd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR; smpd.AddressU=smpd.AddressV=smpd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        Dev->CreateSamplerState(&smpd, &Smp);

        unsigned char* px; int fw, fh;
        ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &fw, &fh);
        if (px && fw > 0 && fh > 0) {
            D3D11_TEXTURE2D_DESC td{};
            td.Width=fw; td.Height=fh; td.MipLevels=1; td.ArraySize=1;
            td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc={1,0};
            td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA srd{px,(UINT)(fw*4),0};
            ID3D11Texture2D* tex=nullptr;
            if (SUCCEEDED(Dev->CreateTexture2D(&td,&srd,&tex))) {
                Dev->CreateShaderResourceView(tex, nullptr, &Font);
                tex->Release();
            }
        }

        bool ready = VS && PS && IL && CB && BS && RS && DSS && Smp && Font;
        if (!ready) {
            lstrcpyA(s_Status, "Failed: Pipeline init");
        } else {
            lstrcpyA(s_Status, "Active");
        }
        return ready;
    }

    void Render(ImDrawData* data, UINT scrW, UINT scrH) {
        if (!RTV || !data || data->CmdListsCount <= 0) { SC->Present(0,0); return; }

        float L = data->DisplayPos.x, R = data->DisplayPos.x + data->DisplaySize.x;
        float T = data->DisplayPos.y, B = data->DisplayPos.y + data->DisplaySize.y;
        const float mvp[4][4] = {
            {2/(R-L),0,0,0},{0,2/(T-B),0,0},{0,0,.5f,0},{(R+L)/(L-R),(T+B)/(B-T),.5f,1}
        };
        D3D11_MAPPED_SUBRESOURCE mr{};
        Ctx->Map(CB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mr);
        memcpy(mr.pData, mvp, sizeof(mvp));
        Ctx->Unmap(CB, 0);

        int totV = data->TotalVtxCount, totI = data->TotalIdxCount;
        if (totV > VBSz) {
            if(VB) VB->Release(); VBSz = totV + 5000;
            D3D11_BUFFER_DESC bd{(UINT)(VBSz*sizeof(ImDrawVert)), D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE};
            Dev->CreateBuffer(&bd, nullptr, &VB);
        }
        if (totI > IBSz) {
            if(IB) IB->Release(); IBSz = totI + 10000;
            D3D11_BUFFER_DESC bd{(UINT)(IBSz*sizeof(ImDrawIdx)), D3D11_USAGE_DYNAMIC, D3D11_BIND_INDEX_BUFFER, D3D11_CPU_ACCESS_WRITE};
            Dev->CreateBuffer(&bd, nullptr, &IB);
        }
        if (!VB || !IB) { SC->Present(0,0); return; }

        D3D11_MAPPED_SUBRESOURCE vr{}, ir{};
        Ctx->Map(VB, 0, D3D11_MAP_WRITE_DISCARD, 0, &vr);
        Ctx->Map(IB, 0, D3D11_MAP_WRITE_DISCARD, 0, &ir);
        auto* vp = (ImDrawVert*)vr.pData; auto* ip = (ImDrawIdx*)ir.pData;
        for (int i = 0; i < data->CmdListsCount; ++i) {
            const auto* l = data->CmdLists[i];
            memcpy(vp, l->VtxBuffer.Data, l->VtxBuffer.Size * sizeof(ImDrawVert)); vp += l->VtxBuffer.Size;
            memcpy(ip, l->IdxBuffer.Data, l->IdxBuffer.Size * sizeof(ImDrawIdx)); ip += l->IdxBuffer.Size;
        }
        Ctx->Unmap(VB, 0); Ctx->Unmap(IB, 0);

        const float clearCol[4] = {};
        Ctx->ClearRenderTargetView(RTV, clearCol);
        Ctx->OMSetRenderTargets(1, &RTV, nullptr);
        D3D11_VIEWPORT vp2{0, 0, (float)scrW, (float)scrH, 0, 1};
        Ctx->RSSetViewports(1, &vp2);
        Ctx->VSSetShader(VS, nullptr, 0); Ctx->PSSetShader(PS, nullptr, 0);
        Ctx->IASetInputLayout(IL);
        UINT stride = sizeof(ImDrawVert), ofs = 0;
        Ctx->IASetVertexBuffers(0, 1, &VB, &stride, &ofs);
        Ctx->IASetIndexBuffer(IB, sizeof(ImDrawIdx)==2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, 0);
        Ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Ctx->VSSetConstantBuffers(0, 1, &CB);
        Ctx->PSSetSamplers(0, 1, &Smp);
        Ctx->PSSetShaderResources(0, 1, &Font);
        const float bf[4] = {};
        Ctx->OMSetBlendState(BS, bf, 0xFFFFFFFF);
        Ctx->OMSetDepthStencilState(DSS, 0);
        Ctx->RSSetState(RS);

        int vtx_offset = 0, idx_offset = 0;
        for (int i = 0; i < data->CmdListsCount; ++i) {
            const auto* l = data->CmdLists[i];
            for (int cmd_i = 0; cmd_i < l->CmdBuffer.Size; ++cmd_i) {
                const auto* pcmd = &l->CmdBuffer[cmd_i];
                D3D11_RECT r = { (LONG)(pcmd->ClipRect.x - data->DisplayPos.x), (LONG)(pcmd->ClipRect.y - data->DisplayPos.y),
                                 (LONG)(pcmd->ClipRect.z - data->DisplayPos.x), (LONG)(pcmd->ClipRect.w - data->DisplayPos.y) };
                Ctx->RSSetScissorRects(1, &r);
                Ctx->DrawIndexed(pcmd->ElemCount, pcmd->IdxOffset + idx_offset, pcmd->VtxOffset + vtx_offset);
            }
            idx_offset += l->IdxBuffer.Size;
            vtx_offset += l->VtxBuffer.Size;
        }
        // Present with vsync=0 and DXGI_PRESENT_DO_NOT_WAIT to avoid stalling the game thread
        SC->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
    }
};
static Renderer s_R;

bool IsAvailable() noexcept {
    if (s_Available != -1) return s_Available == 1;
    HMODULE h = GetModuleHandleW(L"user32.dll");
    s_Available = (h && GetProcAddress(h, "SetWindowDisplayAffinity")) ? 1 : 0;
    return s_Available == 1;
}

bool IsEnabled() noexcept { return s_Enabled; }
const char* StatusText() noexcept { return s_Status; }

bool IsOverlaySwapChain(IDXGISwapChain* sc) noexcept {
    return sc != nullptr && sc == s_R.SC;
}

void SetEnabled(bool enabled) noexcept {
    if (!IsAvailable() || s_Enabled == enabled) return;
    s_Enabled = enabled;
    if (enabled) {
        lstrcpyA(s_Status, "Starting...");
        s_PatcherStop = false;
        if (s_PatcherThread.joinable()) s_PatcherThread.join();
        s_PatcherThread = std::thread(PatcherLoop);
    } else {
        lstrcpyA(s_Status, "Off");
        s_PatcherStop = true;
        if (s_PatcherThread.joinable()) s_PatcherThread.join();
        s_R.Release();
        if (s_OvHwnd) { DestroyWindow(s_OvHwnd); s_OvHwnd = nullptr; }
        // Reset cached position so Tick re-initializes D3D on next enable
        s_LastX = -9999; s_LastY = -9999; s_LastW = 0; s_LastH = 0;
    }
}

void Tick(ID3D11Device* /*device*/, HWND gameHwnd) noexcept {
    s_GameHwnd = gameHwnd;
    if (!s_Enabled) return;

    if (!s_OvHwnd) {
        static bool s_ClassRegistered = false;
        if (!s_ClassRegistered) {
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = kWC;
            RegisterClassExW(&wc);
            s_ClassRegistered = true;
        }
        s_OvHwnd = CreateWindowExW(WS_EX_TOPMOST|WS_EX_TRANSPARENT|WS_EX_LAYERED|WS_EX_TOOLWINDOW,
            kWC, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (s_OvHwnd) {
            SetWindowDisplayAffinity(s_OvHwnd, WDA_EXCLUDEFROMCAPTURE);
            SetLayeredWindowAttributes(s_OvHwnd, 0, 255, LWA_ALPHA);
            // Do NOT call DwmExtendFrameIntoClientArea here yet!
            // DWM would immediately start compositing the window as green placeholder.
            // We call it only after D3D has presented the first transparent frame.
            // Window stays hidden until D3D is ready.
        }
    }

    if (s_OvHwnd) {
        MSG msg;
        while (PeekMessageW(&msg, s_OvHwnd, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        RECT cr{};
        if (GetClientRect(gameHwnd, &cr)) {
            POINT pt{0, 0}; ClientToScreen(gameHwnd, &pt);
            int w = cr.right - cr.left;
            int h = cr.bottom - cr.top;
            if (pt.x != s_LastX || pt.y != s_LastY || w != s_LastW || h != s_LastH) {
                bool needInit = (w != s_LastW || h != s_LastH);
                SetWindowPos(s_OvHwnd, HWND_TOPMOST, pt.x, pt.y, w, h, SWP_NOACTIVATE);
                s_LastX = pt.x; s_LastY = pt.y; s_LastW = w; s_LastH = h;
                if (needInit) {
                    s_R.Release();
                    bool ok = s_R.Init(s_OvHwnd, w, h);
                    if (ok && !IsWindowVisible(s_OvHwnd)) {
                        // D3D is ready, first transparent frame already presented in Init().
                        // Now enable DWM alpha compositing and show the window.
                        // Order matters: DwmExtendFrameIntoClientArea before ShowWindow
                        // so DWM never sees a non-transparent frame.
                        MARGINS m2 = {-1, -1, -1, -1};
                        DwmExtendFrameIntoClientArea(s_OvHwnd, &m2);
                        ShowWindow(s_OvHwnd, SW_SHOWNA);
                        lstrcpyA(s_Status, "Active");
                    }
                }
            }
        }
    }
}

bool RenderAndPresent(ImDrawData* data) noexcept {
    if (!s_Enabled || !s_OvHwnd || !s_R.RTV || !data || !data->Valid || data->CmdListsCount <= 0) return false;
    
    RECT cr;
    GetClientRect(s_OvHwnd, &cr);
    s_R.Render(data, cr.right - cr.left, cr.bottom - cr.top);
    return true;
}

void Shutdown() noexcept {
    SetEnabled(false);
}

} // namespace NvidiaBypass
