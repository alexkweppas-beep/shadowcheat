#include <Windows.h>
#include <psapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include "Hooks.h"
#include "Diagnostics.h"
#include "../Helpers/d3d11_helpers.h"
#include "../imgui/imgui.h"
#include "../imgui/imgui_impl_dx11.h"
#include "../imgui/imgui_impl_win32.h"
#include "../Menu/Menu.h"
#include "../Menu/MenuState.h"
#include "../Menu/MenuTexture.h"
// Legacy menu is no longer used — see Menu\Menu.h instead.
// Stale font headers are still referenced by other codepaths; keep them in scope.
#include "../Menu/font_roboto.h"
#include "../Menu/font_awesome.h"
#include "../Menu/IconsFontAwesome6.h"
#include "../SDK/Offsets.h"
#include "../SDK/Game.h"
#include "../SDK/RageBuildFlavor.h"
#include "../Features/Visuals.h"
#include "../Features/Aimbot.h"
#include "../Features/SilentAim.h"
#include "../Features/RuntimeProfiler.h"
#include "../Features/AntiCheatShield.h"
#include "../Features/QuantTools.h"
#include "../Features/UkraineTools.h"
#include "../Features/FiveRpFarmBot.h"
#include "../Features/MiscFeatures.h"
#include "../Features/ScriptBridge.h"
#include "../Features/CloudStorage.h"
#include "../Features/RageV8Runtime.h"
#include "../Features/FeatureBinds.h"
#include "../Features/EventLogger.h"
#include "../Features/ObsBypass.h"
#include "../Features/Overlay.h"
#include "../Features/WorldOverlay.h"
#if defined(RAGEMP_DIAGNOSTICS)
#include "../Dev/VehicleListProbe.h"
#endif
#include "../Features/PedPreview.h"
#include "../Features/ModelPreview.h"
#include "../Helpers/inject_ready.h"

// Defined in dllmain.cpp, which owns the single-instance claim.
void ReleaseSoleInstanceClaim() noexcept;

// ----------------------------------------------------------------
// External linkage  (see declarations in Hooks.h)
// ----------------------------------------------------------------
namespace hooks {
    ID3D11Device*         g_Device         = nullptr;
    ID3D11DeviceContext*  g_Context        = nullptr;
    IDXGISwapChain*       g_SwapChain      = nullptr;
    void*                 g_OriginalPresent  = nullptr;
    void*                 g_OriginalPresent1 = nullptr;
    void*                 g_OriginalResizeBuffers = nullptr;
    void*                 g_OriginalSetFullscreenState = nullptr;

    bool    g_ImGuiInit   = false;
    bool    g_InjectReadySignaled = false;
    WNDPROC g_OriginalWndProc = nullptr;
    HWND    g_Hwnd        = nullptr;
    bool    g_AllocatorsSet = false;
    bool    g_AllocProbeLogged = false;

    // menu visibility toggle — start closed so Enter reaches game chat/CEF
    bool    g_MenuOpen    = false;
    bool    g_PrevMenuKey = false;  // for menu toggle key edge detection
    bool    g_PrevUnloadKey = false;
    bool    g_FirstFrameLogged = false;
    DWORD   g_FrameCount       = 0;
    DWORD   g_LastError        = 0;
    bool    g_RestoreGameMouseCapture = false;
    volatile LONG g_UnloadRequested = FALSE;
    volatile LONG g_ShutdownStarted = FALSE;
    volatile LONG g_ImGuiRecoverRequested = FALSE;
    UINT    g_LastBackBufferWidth = 0;
    UINT    g_LastBackBufferHeight = 0;
    ID3D11RenderTargetView* g_BackBufferRtv = nullptr;
    IDXGISwapChain* g_BackBufferRtvSwap = nullptr;
    bool    g_InlineHookInstalled = false;
    bool    g_VtableHookInstalled = false;
#if defined(RAGEMP_DIAGNOSTICS)
    // Which part of our frame was running last. A device that dies takes the
    // whole renderer with it and leaves no stack to read, so the breadcrumb is
    // the only thing that says which of our blocks it died under.
    const char* g_PresentStage = nullptr;
#  define PRESENT_STAGE(name) (g_PresentStage = (name))
#else
#  define PRESENT_STAGE(name) ((void)0)
#endif
    void*   g_PresentFunc         = nullptr;
    // After Present/device faults: skip overlay+V8 for a short window and
    // suppress F4 edge so modal dialog focus regain cannot force-open the menu.
    ULONGLONG g_RecoverCooldownUntil = 0;
    ULONGLONG g_F4SuppressUntil = 0;

    // The gear panel owns this key. F4 is the fallback so an empty bind can
    // never leave the menu unreachable.
    static int MenuToggleKey() noexcept
    {
        return Menu::s_MenuToggleKey > 0 ? Menu::s_MenuToggleKey : VK_F4;
    }

    LogFn   g_LogFn       = nullptr;
}

// ----------------------------------------------------------------
// Forward declarations
// ----------------------------------------------------------------
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND, UINT, WPARAM, LPARAM);

#pragma comment(lib, "psapi.lib")

// CheatLog — called by CHEAT_LOG macro inside imgui.cpp for debug tracing.
extern "C" void CheatLog(const char* msg)
{
    hooks::HLog(msg);
}

// SEH filter helper: logs the exception code + faulting address, then
// tells the __except block to swallow the fault (EXCEPTION_EXECUTE_HANDLER).
// Used to wrap game-memory reads so a stale pointer skips the frame instead
// of killing the process.
static int SafeFilter(EXCEPTION_POINTERS* ep, const char* where)
{
    DWORD code = 0;
    void* addr = nullptr;
    if (ep && ep->ExceptionRecord) {
        code = ep->ExceptionRecord->ExceptionCode;
        addr = ep->ExceptionRecord->ExceptionAddress;
    }
    char buf[256];
    wsprintfA(
        buf,
        "[SEH] %s caught code=0x%08X addr=%p phase=%s",
        where,
        code,
        addr,
        Menu::RenderPhase());
    hooks::HLog(buf);
    return EXCEPTION_EXECUTE_HANDLER;
}

namespace hooks
{
    static void ReleaseMenuInputCapture(bool unloading = false);

    static DXGI_FORMAT TypedRtvFormat(DXGI_FORMAT format) noexcept
    {
        switch (format) {
        case DXGI_FORMAT_R8G8B8A8_TYPELESS:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_TYPELESS:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS:
            return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default:
            return format;
        }
    }

    static void RequestImGuiRecover(const char* reason) noexcept
    {
        const ULONGLONG now = GetTickCount64();
        g_RecoverCooldownUntil = now + 1500ull;
        g_F4SuppressUntil = now + 1800ull; // cooldown + ~300ms
        // Preserve open state; only reset the toggle edge so dialog Enter/focus
        // cannot spuriously toggle the menu open.
        g_PrevMenuKey = (GetAsyncKeyState(MenuToggleKey()) & 0x8000) != 0;
        if (InterlockedExchange(&g_ImGuiRecoverRequested, 1) == 0)
        {
            if (reason)
                HLog(reason);
            char buf[96];
            wsprintfA(
                buf,
                "ImGui recover: menuOpen=%d cooldown=1500ms",
                g_MenuOpen ? 1 : 0);
            HLog(buf);
        }
    }

    // Clean counterpart to RequestImGuiRecover: the device objects have to be
    // rebuilt, but nothing faulted, so there is no reason to blank the overlay
    // for a second and a half afterwards.
    static void RequestDeviceRebuild(const char* reason) noexcept
    {
        if (InterlockedExchange(&g_ImGuiRecoverRequested, 1) == 0 && reason)
            HLog(reason);
    }

    static bool InRecoverCooldown() noexcept
    {
        return GetTickCount64() < g_RecoverCooldownUntil;
    }

    static void ReleaseBackBufferRtv() noexcept
    {
        if (g_BackBufferRtv) {
            g_BackBufferRtv->Release();
            g_BackBufferRtv = nullptr;
        }
        g_BackBufferRtvSwap = nullptr;
    }

    // Everything of ours that is tied to the current back buffer size. Called
    // before the swap chain reallocates, because a surviving reference makes
    // ResizeBuffers fail and the game has no answer for that.
    static void ReleaseSwapChainDependentResources() noexcept
    {
        WorldOverlay::InvalidateDeviceObjects();
        ReleaseBackBufferRtv();
        g_LastBackBufferWidth = 0;
        g_LastBackBufferHeight = 0;
    }

    static bool EnsureBackBufferRtv(IDXGISwapChain* swap) noexcept
    {
        if (!swap || !g_Device)
            return false;

        DXGI_SWAP_CHAIN_DESC desc{};
        const bool haveDesc = SUCCEEDED(swap->GetDesc(&desc));
        const bool sizeMismatch =
            haveDesc &&
            g_BackBufferRtv &&
            (desc.BufferDesc.Width != g_LastBackBufferWidth ||
             desc.BufferDesc.Height != g_LastBackBufferHeight);

        if (g_BackBufferRtv && g_BackBufferRtvSwap == swap && !sizeMismatch)
            return true;

        WorldOverlay::InvalidateDeviceObjects();
        ReleaseBackBufferRtv();

        ID3D11Texture2D* bb = nullptr;
        if (FAILED(swap->GetBuffer(
                0,
                __uuidof(ID3D11Texture2D),
                reinterpret_cast<void**>(&bb))) ||
            !bb)
            return false;

        D3D11_TEXTURE2D_DESC td{};
        bb->GetDesc(&td);

        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = TypedRtvFormat(td.Format);
        rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;

        ID3D11RenderTargetView* rtv = nullptr;
        HRESULT hr = g_Device->CreateRenderTargetView(bb, &rtvDesc, &rtv);
        if (FAILED(hr) || !rtv)
            hr = g_Device->CreateRenderTargetView(bb, nullptr, &rtv);
        bb->Release();
        if (FAILED(hr) || !rtv)
            return false;

        g_BackBufferRtv = rtv;
        g_BackBufferRtvSwap = swap;
        return true;
    }

    // ImGui does not bind an RTV itself. Drawing with a null/foreign target
    // during CEF chat / Present races is what trips RAGE's
    // "game renderer has crashed" dialog.
    static void DrawImGuiToBackbuffer(IDXGISwapChain* swap) noexcept
    {
        ImDrawData* data = ImGui::GetDrawData();
        if (!data || data->CmdListsCount <= 0 || !g_Context)
            return;
        if (!EnsureBackBufferRtv(swap)) {
            HLog("hkPresent: backbuffer RTV missing, skip overlay draw");
            RequestImGuiRecover("ImGui recover: backbuffer RTV failed");
            return;
        }

        ID3D11RenderTargetView* oldRtv = nullptr;
        ID3D11DepthStencilView* oldDsv = nullptr;
        g_Context->OMGetRenderTargets(1, &oldRtv, &oldDsv);
        g_Context->OMSetRenderTargets(1, &g_BackBufferRtv, nullptr);
        // Bypass off: still run post-compose hook before ImGui (OBS will see it).
        WorldOverlay::ComposeAfterCapture(g_Device, g_Context, swap);
        ImGui_ImplDX11_RenderDrawData(data);
        g_Context->OMSetRenderTargets(1, &oldRtv, oldDsv);
        if (oldRtv)
            oldRtv->Release();
        if (oldDsv)
            oldDsv->Release();
    }

    static bool EnsureImGuiDevice(IDXGISwapChain* swap) noexcept
    {
        if (!g_ImGuiInit || !swap || !ImGui::GetCurrentContext())
            return false;

        ID3D11Device* device = nullptr;
        if (FAILED(swap->GetDevice(
                __uuidof(ID3D11Device),
                reinterpret_cast<void**>(&device))) ||
            !device)
        {
            RequestImGuiRecover("ImGui recover: GetDevice failed");
            return false;
        }

        ID3D11DeviceContext* ctx = nullptr;
        device->GetImmediateContext(&ctx);
        if (!ctx) {
            device->Release();
            RequestImGuiRecover("ImGui recover: GetImmediateContext failed");
            return false;
        }

        DXGI_SWAP_CHAIN_DESC desc{};
        const bool haveDesc = SUCCEEDED(swap->GetDesc(&desc));
        const bool sizeChanged =
            haveDesc &&
            g_LastBackBufferWidth != 0 &&
            (desc.BufferDesc.Width != g_LastBackBufferWidth ||
             desc.BufferDesc.Height != g_LastBackBufferHeight);
        const bool deviceChanged =
            g_Device != device || g_Context != ctx;
        const bool recover =
            InterlockedExchange(&g_ImGuiRecoverRequested, 0) != 0 ||
            deviceChanged ||
            sizeChanged;

        if (haveDesc) {
            g_LastBackBufferWidth = desc.BufferDesc.Width;
            g_LastBackBufferHeight = desc.BufferDesc.Height;
            if (desc.OutputWindow)
                g_Hwnd = desc.OutputWindow;
        }

        if (!recover) {
            device->Release();
            ctx->Release();
            return true;
        }

        HLog(deviceChanged
            ? "ImGui recover: rebinding D3D device"
            : (sizeChanged
                ? "ImGui recover: swapchain size changed"
                : "ImGui recover: rebuilding device objects"));

        WorldOverlay::InvalidateDeviceObjects();
        ReleaseBackBufferRtv();

        if (g_Device)
            g_Device->Release();
        if (g_Context)
            g_Context->Release();
        g_Device = device;
        g_Context = ctx;
        g_SwapChain = swap;

        if (!ImGui_ImplDX11_RebindDevice(g_Device, g_Context)) {
            HLog("ImGui recover: RebindDevice FAILED");
            RequestImGuiRecover("ImGui recover: retry next frame");
            return false;
        }
        HLog("ImGui recover: OK");
        return true;
    }

    static bool PresentNeedsRecover(HRESULT hr) noexcept
    {
        return hr == DXGI_ERROR_DEVICE_REMOVED ||
            hr == DXGI_ERROR_DEVICE_RESET ||
            hr == DXGI_ERROR_INVALID_CALL ||
            hr == DXGI_ERROR_ACCESS_LOST;
    }

    // Scan a memory region for a pointer equal to `target`. Returns the
    // address of the first occurrence, or nullptr if not found.
    static void* ScanRegion(PVOID base, SIZE_T size, PVOID target)
    {
        __try {
            PVOID* p = (PVOID*)base;
            SIZE_T count = size / sizeof(PVOID);
            for (SIZE_T i = 0; i < count; ++i)
                if (p[i] == target)
                    return &p[i];
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return nullptr;
    }

    // Walk all committed, readable memory regions looking for a pointer
    // == vtableAddr (the dummy swapchain's vtable). When found, this is
    // the game's IDXGISwapChain object slot.
    static IDXGISwapChain* FindGameSwapChain(void* vtableAddr)
    {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        MEMORY_BASIC_INFORMATION mbi{};

        BYTE* addr = (BYTE*)si.lpMinimumApplicationAddress;
        BYTE* end  = (BYTE*)si.lpMaximumApplicationAddress;

        while (addr < end) {
            if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0)
                break;

            // Only scan committed, readable, non-guarded memory.
            bool readable =
                (mbi.State == MEM_COMMIT) &&
                (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE |
                                PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                                PAGE_WRITECOPY)) &&
                !(mbi.Protect & PAGE_GUARD);

            if (readable && mbi.RegionSize > 0 && mbi.RegionSize < 0x40000000) {
                // Scan in chunks of ~4 MB to keep __try scopes small.
                BYTE* p   = (BYTE*)mbi.BaseAddress;
                SIZE_T sz = mbi.RegionSize;
                while (sz > 0) {
                    SIZE_T chunk = sz < (4 * 1024 * 1024) ? sz : (4 * 1024 * 1024);
                    void* found = ScanRegion(p, chunk, vtableAddr);
                    if (found)
                        return (IDXGISwapChain*)found;
                    p  += chunk;
                    sz -= chunk;
                }
            }

            // Advance to next region.
            addr = (BYTE*)mbi.BaseAddress + mbi.RegionSize;
            if (addr <= (BYTE*)mbi.BaseAddress)
                break; // guard against wrap-around
        }
        return nullptr;
    }

    static bool IsReadableProtect(DWORD protect)
    {
        if (protect & PAGE_GUARD) return false;
        if (protect & PAGE_NOACCESS) return false;
        return (protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                           PAGE_EXECUTE_WRITECOPY)) != 0;
    }

    static bool IsInsideModule(void* ptr, const MODULEINFO& mi)
    {
        BYTE* p = (BYTE*)ptr;
        BYTE* base = (BYTE*)mi.lpBaseOfDll;
        return p >= base && p < base + mi.SizeOfImage;
    }

    static constexpr size_t kPresentHookSize = 14;



    // IDXGISwapChain has exactly 18 virtual methods (8 IDXGIObject/SubObject + 10 IDXGISwapChain).
    // We require ALL 18 to point inside dxgi.dll for a vtable to qualify.
    static constexpr int kSwapChainVTableMin = 18;

    static bool PresentInsideDxgi(void* present);

    // Output buffer of live vtables actually referenced by some object in the process.
    // If `referencePresent` != nullptr, only vtables whose slot[kPresentIndex] equals it qualify.
    static int FindLiveDxgiSwapChainVTables(void*** outVTables, int maxCount,
                                             void* referencePresent = nullptr)
    {
        HMODULE hDxgi = GetModuleHandleW(L"dxgi.dll");
        if (!hDxgi) {
            HLog("Init: dxgi.dll not loaded");
            return 0;
        }

        MODULEINFO dxgiInfo{};
        if (!GetModuleInformation(GetCurrentProcess(), hDxgi, &dxgiInfo, sizeof(dxgiInfo))) {
            HLog("Init: GetModuleInformation(dxgi) failed");
            return 0;
        }

        // Pass 1: scan dxgi.dll for candidate vtables (>= 18 in-module pointers).
        void** candidates[512]{};
        int candidateCount = 0;

        BYTE* base = (BYTE*)dxgiInfo.lpBaseOfDll;
        BYTE* end = base + dxgiInfo.SizeOfImage;
        MEMORY_BASIC_INFORMATION mbi{};

        for (BYTE* addr = base; addr < end && candidateCount < 512; ) {
            if (!VirtualQuery(addr, &mbi, sizeof(mbi)))
                break;

            BYTE* region = (BYTE*)mbi.BaseAddress;
            BYTE* regionEnd = region + mbi.RegionSize;
            if (regionEnd > end) regionEnd = end;

            if (mbi.State == MEM_COMMIT && IsReadableProtect(mbi.Protect)) {
                const size_t step = sizeof(void*);
                const size_t need = step * kSwapChainVTableMin;
                for (BYTE* p = region; p + need <= regionEnd; p += step) {
                    __try {
                        void** vt = (void**)p;
                        bool allInside = true;
                        for (int i = 0; i < kSwapChainVTableMin; ++i) {
                            if (!IsInsideModule(vt[i], dxgiInfo)) {
                                allInside = false;
                                break;
                            }
                        }
                        if (!allInside) {
                            // skip
                        } else if (referencePresent &&
                                   vt[d3d::kPresentIndex] != referencePresent) {
                            // slot[8] does not match the known IDXGISwapChain::Present
                            // → this is some other DXGI vtable, not a swapchain
                        } else {
                            candidates[candidateCount++] = vt;
                            if (candidateCount >= 512) break;
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                    }
                }
            }

            addr = regionEnd;
            if (addr <= (BYTE*)mbi.BaseAddress)
                break;
        }

        if (candidateCount == 0) {
            HLog("Init: no DXGI vtable candidates found");
            return 0;
        }

        // Pass 2: count how many live COM objects reference each candidate vtable.
        int refCounts[512]{};

        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        BYTE* scan = (BYTE*)si.lpMinimumApplicationAddress;
        BYTE* scanEnd = (BYTE*)si.lpMaximumApplicationAddress;

        while (scan < scanEnd) {
            if (!VirtualQuery(scan, &mbi, sizeof(mbi)))
                break;

            bool readable = (mbi.State == MEM_COMMIT) && IsReadableProtect(mbi.Protect);
            bool notDxgi = ((BYTE*)mbi.BaseAddress < base || (BYTE*)mbi.BaseAddress >= end);

            if (readable && notDxgi && mbi.RegionSize > sizeof(void*) && mbi.RegionSize < 0x40000000) {
                BYTE* p = (BYTE*)mbi.BaseAddress;
                BYTE* rEnd = p + mbi.RegionSize;
                while (p + sizeof(void*) <= rEnd) {
                    __try {
                        void** possibleVt = *(void***)p;
                        for (int i = 0; i < candidateCount; ++i) {
                            if (possibleVt == candidates[i])
                                ++refCounts[i];
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                    }
                    p += sizeof(void*);
                }
            }

            scan = (BYTE*)mbi.BaseAddress + mbi.RegionSize;
            if (scan <= (BYTE*)mbi.BaseAddress)
                break;
        }

        int bestIdx = -1;
        int bestRefs = 0;
        for (int i = 0; i < candidateCount; ++i) {
            if (refCounts[i] <= 0)
                continue;

            void* present = nullptr;
            __try {
                present = candidates[i][d3d::kPresentIndex];
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }

            if (!PresentInsideDxgi(present))
                continue;

            if (referencePresent && present != referencePresent)
                continue;

            if (refCounts[i] > bestRefs) {
                bestRefs = refCounts[i];
                bestIdx = i;
            }
        }

        if (bestIdx < 0) {
            HLog("Init: no live DXGI swapchain object found");
            return 0;
        }

        int outCount = 0;
        if (outVTables && maxCount > 0)
            outVTables[outCount++] = candidates[bestIdx];

        char buf[128];
        wsprintfA(buf, "Init: %d candidate vtables, picked idx=%d refs=%d vt=%p Present=%p",
                  candidateCount, bestIdx, bestRefs,
                  candidates[bestIdx], candidates[bestIdx][d3d::kPresentIndex]);
        HLog(buf);
        return outCount;
    }

    static bool ResolveSwapChainVtableLive(void*** outVtableAddr, void** outPresent,
                                           void* referencePresent = nullptr)
    {
        if (!outVtableAddr || !outPresent)
            return false;

        void** liveVtables[1]{};
        const int count = FindLiveDxgiSwapChainVTables(liveVtables, 1, referencePresent);
        if (count <= 0)
            return false;

        *outVtableAddr = liveVtables[0];
        __try {
            *outPresent = liveVtables[0][d3d::kPresentIndex];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }

        HLog("Init: live swapchain vtable resolved (ref-count pick)");
        return PresentInsideDxgi(*outPresent);
    }

    // Legacy wrapper — only for diagnostics; never use without referencePresent.
    static bool ResolveSwapChainVtable(void*** outVtableAddr, void** outPresent,
                                       void* referencePresent)
    {
        return ResolveSwapChainVtableLive(outVtableAddr, outPresent, referencePresent);
    }

    static void WhitelistHkPresentCfg()
    {
        HLog("Init: whitelist hkPresent via CFG");

        typedef struct _CFG_CALL_TARGET_INFO_MY {
            ULONG_PTR Offset;
            ULONG_PTR Flags;
        } CFG_CALL_TARGET_INFO_MY;

        typedef BOOL (WINAPI *PFN_SetValidCallTargets)(
            HANDLE, PVOID, SIZE_T, ULONG,
            CFG_CALL_TARGET_INFO_MY*);

        PFN_SetValidCallTargets pSetValid = nullptr;
        HMODULE hKB = GetModuleHandleW(L"kernelbase.dll");
        if (hKB)
            pSetValid = (PFN_SetValidCallTargets)GetProcAddress(
                hKB, "SetProcessValidCallTargets");
        if (!pSetValid) {
            HMODULE hK32b = GetModuleHandleW(L"kernel32.dll");
            if (hK32b)
                pSetValid = (PFN_SetValidCallTargets)GetProcAddress(
                    hK32b, "SetProcessValidCallTargets");
        }

        if (!pSetValid) {
            HLog("Init: SetProcessValidCallTargets not found");
            return;
        }

        ULONG_PTR pageBase = (ULONG_PTR)hkPresent & ~((ULONG_PTR)0xFFF);
        ULONG     CFG_VALID = 0x00000001;

        CFG_CALL_TARGET_INFO_MY info{};
        info.Offset = (ULONG_PTR)hkPresent - pageBase;
        info.Flags  = CFG_VALID;

        char buf[128];
        SetLastError(0);
        BOOL cfgOk = pSetValid(
            GetCurrentProcess(),
            (PVOID)pageBase,
            0x1000,
            1,
            &info);
        DWORD err = GetLastError();
        if (cfgOk) {
            HLog("Init: CFG whitelist OK");
        } else {
            wsprintfA(buf, "Init: CFG whitelist FAILED err=%lu (harmless if no CFG)", err);
            HLog(buf);
        }
    }

    // ------------------------------------------------------------
    //  ImGui init (lazy — needs device/swapchain backbuffers)
    // ------------------------------------------------------------
    // --------------------------------------------------------------------
    //  Private-heap allocator funcs for ImGui. Defined in dllmain.cpp.
    //  Declared at namespace scope so the linker can find them.
    // --------------------------------------------------------------------
    void* ImHeapAllocFn(size_t sz, void* user_data);
    void  ImHeapFreeFn(void* ptr, void* user_data);

    int s_initAttempts = 0;

    void NotifyInjectReady() noexcept
    {
        if (g_InjectReadySignaled)
            return;
        HANDLE ready = OpenEventW(
            EVENT_MODIFY_STATE,
            FALSE,
            kInjectReadyEventName);
        if (!ready)
            return;
        SetEvent(ready);
        CloseHandle(ready);
        g_InjectReadySignaled = true;
        HLog("inject-ready signaled");
    }

    void InitImGui(IDXGISwapChain* swap)
    {
        if (g_ImGuiInit) return;

        // Give up after 3 failed attempts to prevent infinite crash-loop.
        if (s_initAttempts >= 3) return;
        ++s_initAttempts;

        // ImGui allocations are routed to the private g_Heap via
        // SetAllocatorFunctions below, isolating them from GetProcessHeap().
        HLog("InitImGui: using private g_Heap allocator");

        HLog("InitImGui: step 1 - GetDevice");

        // Pull device + context out of the swapchain.
        ID3D11Device* dev = nullptr;
        ID3D11DeviceContext* ctx = nullptr;
        if (FAILED(swap->GetDevice(__uuidof(ID3D11Device), (void**)&dev)))
            return;
        dev->GetImmediateContext(&ctx);
        g_Device    = dev;
        g_Context   = ctx;
        g_SwapChain = swap;

        HLog("InitImGui: step 2 - GetDesc for HWND");
        // Resolve HWND by inspecting swapchain description.
        DXGI_SWAP_CHAIN_DESC sd{};
        if (SUCCEEDED(swap->GetDesc(&sd))) {
            g_Hwnd = sd.OutputWindow;
            g_LastBackBufferWidth = sd.BufferDesc.Width;
            g_LastBackBufferHeight = sd.BufferDesc.Height;
        }
        // The game may already own mouse capture. The stock ImGui Win32
        // backend cannot distinguish that from capture acquired by itself
        // and releases it after a menu click.
        g_RestoreGameMouseCapture =
            g_Hwnd && GetCapture() == g_Hwnd;

        HLog("InitImGui: step 3 - IMGUI_CHECKVERSION");
        IMGUI_CHECKVERSION();

        // Route ImGui allocations through our private g_Heap (defined in
        // dllmain.cpp). This isolates ImGui from GetProcessHeap() /
        // heap-lock contention with the game — the main cause of the
        // multi-second freeze during font atlas construction.
        HLog("InitImGui: step 3.1 - SetAllocatorFunctions");
        ImGui::SetAllocatorFunctions(ImHeapAllocFn, ImHeapFreeFn, nullptr);

        HLog("InitImGui: step 3.5 - CreateContext");
        ImGuiContext* createdCtx = ImGui::CreateContext();
        if (!createdCtx) {
            HLog("InitImGui: CreateContext FAILED");
            return;
        }
        HLog("InitImGui: step 3.6 - context created");
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr; // DISABLE imgui.ini disk I/O (prevents deadlocks!)
        io.LogFilename = nullptr; // DISABLE imgui_log.txt disk I/O
        HLog("InitImGui: step 3.9 - IO configured");

        // Initialize the framework: applies style + queues fonts.
        HLog("InitImGui: step 3.A - InitializeFonts BEGIN");
        Menu::InitializeFonts(io);
        HLog("InitImGui: step 3.B - InitializeFonts DONE");

        // Initialize SDK Offsets via Pattern Scanning (Moved here to avoid injection thread lag)
        HLog("InitImGui: step 3.C - Offsets::Initialize BEGIN");
        Offsets::Initialize();
        HLog("InitImGui: step 3.D - Offsets::Initialize DONE");

        HLog("InitImGui: step 3.E - ScriptBridge::Initialize BEGIN");
        ScriptBridge::Initialize();
        HLog("InitImGui: step 3.F - ScriptBridge::Initialize DONE");
        AntiCheatShield::Initialize();
#if RAGEMP_EVENT_LOGGER
        EventLogger::Initialize();
#endif
        QuantTools::Initialize();
        UkraineTools::Initialize();
        FiveRpFarmBot::Initialize();
        MiscFeatures::Initialize();
        SilentAim::Initialize();

        HLog("InitImGui: step 4 - DX11/Win32 backend init");
        // Initialize Win32 + DX11 backends.
        HLog("InitImGui: step 4.1 - Win32_Init BEGIN");
        ImGui_ImplWin32_Init(g_Hwnd);
        HLog("InitImGui: step 4.2 - Win32_Init DONE");
        HLog("InitImGui: step 4.3 - DX11_Init BEGIN");
        ImGui_ImplDX11_Init(g_Device, g_Context);
        HLog("InitImGui: step 4.4 - DX11_Init DONE");
        ImGui_ImplDX11_CreateDeviceObjects();
        HLog("InitImGui: step 4.5 - DX11_CreateDeviceObjects DONE");

        // Only init D3D preview resources here. Loading the embedded GLB
        // during Present/InitImGui crashed GTA (null HeapHandle + smashed
        // stack in RtlAllocateHeap). Mesh load is deferred to first preview.
        bool previewReady = false;
        __try {
            previewReady = ModelPreview::Init(g_Device, g_Context);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            previewReady = false;
            HLog("InitImGui: ModelPreview init SEH FAULT (prevented crash)");
        }
        HLog(previewReady
            ? "InitImGui: ModelPreview init OK (mesh deferred)"
            : "InitImGui: ModelPreview init FAILED");
        // Don't block loader completion on cloud icon download.
        NotifyInjectReady();
        MenuTexture::Initialize(dev);

        // Install WndProc hook so ImGui receives input.
        if (g_Hwnd) {
            g_OriginalWndProc = (WNDPROC)SetWindowLongPtrW(
                g_Hwnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);
            HLog(g_OriginalWndProc
                ? "InitImGui: WndProc hooked"
                : "InitImGui: WndProc hook FAILED");
        }
        g_ImGuiInit = true;
        HLog("InitImGui: DONE");
        NotifyInjectReady();
    }

    // ------------------------------------------------------------
    //  Helpers
    // ------------------------------------------------------------
    static bool PresentInsideDxgi(void* present)
    {
        if (!present) return false;
        HMODULE hDxgi = GetModuleHandleW(L"dxgi.dll");
        if (!hDxgi) return false;

        MODULEINFO mi{};
        if (!GetModuleInformation(GetCurrentProcess(), hDxgi, &mi, sizeof(mi)))
            return false;

        BYTE* p = (BYTE*)present;
        return (p >= (BYTE*)mi.lpBaseOfDll) &&
               (p <  (BYTE*)mi.lpBaseOfDll + mi.SizeOfImage);
    }



    // ------------------------------------------------------------
    //  OnDxgiSwapChainCreated — ReShade pattern: when the game
    //  creates its swapchain (via D3D11CreateDeviceAndSwapChain or
    //  IDXGIFactory::CreateSwapChain), we patch THAT swapchain's
    //  vtable Present (slot 8) and Present1 (slot 22) slots directly.
    //  This guarantees we hook the REAL Present the game calls.
    // ------------------------------------------------------------
    // Both patch sites need these, and a resize that is not intercepted is
    // fatal, so failures are logged rather than passed over.
    static void PatchResizeSlots(void** vt, const char* where)
    {
        if (!vt)
            return;
        char buf[128];
        void* origResize = nullptr;
        if (d3d::PatchSwapChainVtableSlot(
                vt,
                d3d::kResizeBuffersIndex,
                reinterpret_cast<void*>(hkResizeBuffers),
                &origResize)) {
            if (!g_OriginalResizeBuffers)
                g_OriginalResizeBuffers = origResize;
            wsprintfA(buf, "Init: ResizeBuffers patched (%s)", where);
            HLog(buf);
        } else if (!g_OriginalResizeBuffers) {
            wsprintfA(buf, "Init: ResizeBuffers patch FAILED (%s)", where);
            HLog(buf);
        }

        void* origFullscreen = nullptr;
        if (d3d::PatchSwapChainVtableSlot(
                vt,
                d3d::kSetFullscreenStateIndex,
                reinterpret_cast<void*>(hkSetFullscreenState),
                &origFullscreen)) {
            if (!g_OriginalSetFullscreenState)
                g_OriginalSetFullscreenState = origFullscreen;
            wsprintfA(buf, "Init: SetFullscreenState patched (%s)", where);
            HLog(buf);
        } else if (!g_OriginalSetFullscreenState) {
            wsprintfA(
                buf,
                "Init: SetFullscreenState patch FAILED (%s)",
                where);
            HLog(buf);
        }
    }

    static void OnDxgiSwapChainCreated(IDXGISwapChain* swap)
    {
        if (!swap)
            return;
        if (g_VtableHookInstalled)
            return;

        void** vt = nullptr;
        __try {
            vt = *reinterpret_cast<void***>(swap);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            HLog("Init: OnSwapChain — vtable read SEH");
            return;
        }
        if (!vt)
            return;

        char buf[160];
        wsprintfA(buf, "Init: OnSwapChain swap=%p vt=%p Present[8]=%p",
                  swap, vt, vt[d3d::kPresentIndex]);
        HLog(buf);

        void* origPresent = nullptr;
        if (d3d::PatchSwapChainVtableSlot(
                vt, d3d::kPresentIndex,
                reinterpret_cast<void*>(hkPresent), &origPresent)) {
            if (!g_OriginalPresent) g_OriginalPresent = origPresent;
            g_VtableHookInstalled = true;
            HLog("Init: Present vtable patched on real swapchain");
        }

        // Present1 (slot 22) — only exists on IDXGISwapChain1+.
        if (vt[d3d::kPresent1Index] &&
            PresentInsideDxgi(vt[d3d::kPresent1Index])) {
            void* origPresent1 = nullptr;
            if (d3d::PatchSwapChainVtableSlot(
                    vt, d3d::kPresent1Index,
                    reinterpret_cast<void*>(hkPresent1), &origPresent1)) {
                if (!g_OriginalPresent1) g_OriginalPresent1 = origPresent1;
                g_VtableHookInstalled = true;
                HLog("Init: Present1 vtable patched on real swapchain");
            }
        }

        PatchResizeSlots(vt, "real swapchain");
    }

    // ------------------------------------------------------------
    //  TryVtablePatchFromDummy — create a throwaway HARDWARE device
    //  + swapchain. The dummy's vtable pointer IS the canonical shared
    //  IDXGISwapChain vtable in dxgi.dll .rdata — the game's swapchain
    //  uses the SAME vtable. So patching the slot here redirects every
    //  swapchain's Present, and the ORIGINAL pointer we read from the
    //  slot is called directly (no trampoline, no stolen bytes, no
    //  RIP-relative relocation bugs that froze the game in v8).
    //  This is exactly how ReShade's hook_manager patches vtables.
    // ------------------------------------------------------------
    static bool TryVtablePatchFromDummy()
    {
        HLog("Init: dummy HARDWARE device resolve (vtable patch)");

        void* dummyVt[d3d::kVTable1Size]{};
        IDXGISwapChain* dummy = d3d::GetDummySwapChain(dummyVt);
        if (!dummy) {
            HLog("Init: dummy device FAILED");
            return false;
        }

        // The vtable ADDRESS (in dxgi.dll .rdata) — this is shared by all
        // IDXGISwapChain instances, including the game's.
        void** canonicalVt = *reinterpret_cast<void***>(dummy);

        void* presentFn  = dummyVt[d3d::kPresentIndex];
        void* present1Fn = dummyVt[d3d::kPresent1Index];

        char buf[160];
        wsprintfA(buf, "Init: dummy resolved vt=%p Present[8]=%p Present1[22]=%p",
                  canonicalVt, presentFn, present1Fn);
        HLog(buf);

        bool hooked = false;

        // Always take the Present slot. If a previous inject / overlay already
        // owns it, chain through that pointer so OUR hkPresent is outermost.
        if (presentFn) {
            if (!PresentInsideDxgi(presentFn))
                HLog("Init: Present slot already hooked — re-chaining to us");
            void* origPresent = nullptr;
            if (d3d::PatchSwapChainVtableSlot(
                    canonicalVt, d3d::kPresentIndex,
                    reinterpret_cast<void*>(hkPresent), &origPresent)) {
                if (!g_OriginalPresent) g_OriginalPresent = origPresent;
                g_VtableHookInstalled = true;
                HLog("Init: Present vtable slot patched (canonical)");
                hooked = true;
            } else {
                HLog("Init: Present vtable patch FAILED");
            }
        }

        if (present1Fn && present1Fn != presentFn) {
            if (!PresentInsideDxgi(present1Fn))
                HLog("Init: Present1 slot already hooked — re-chaining to us");
            void* origPresent1 = nullptr;
            if (d3d::PatchSwapChainVtableSlot(
                    canonicalVt, d3d::kPresent1Index,
                    reinterpret_cast<void*>(hkPresent1), &origPresent1)) {
                if (!g_OriginalPresent1) g_OriginalPresent1 = origPresent1;
                g_VtableHookInstalled = true;
                HLog("Init: Present1 vtable slot patched (canonical)");
                hooked = true;
            } else {
                HLog("Init: Present1 vtable patch FAILED");
            }
        }

        PatchResizeSlots(canonicalVt, "canonical");

        d3d::ReleaseDummy();
        return hooked;
    }

    // ------------------------------------------------------------
    //  Initialize — v1 pattern (proven to work):
    //    1. Create dummy HARDWARE device FIRST and inline-detour the
    //       REAL Present/Present1 resolved from its vtable.
    //    2. Install D3D11 + DXGI export hooks AFTER (so the dummy's
    //       own D3D11CreateDeviceAndSwapChain call isn't intercepted
    //       by our own hook, which caused a stall in v6).
    // ------------------------------------------------------------
    bool Initialize()
    {
        if (g_VtableHookInstalled || g_InlineHookInstalled ||
            g_OriginalPresent || g_OriginalPresent1)
            return true;

        HLog("Init: dummy-first vtable patch (ReShade pattern)");

        // Step 1: Create a dummy HARDWARE device and patch the canonical
        // IDXGISwapChain vtable (shared in dxgi.dll .rdata) Present/Present1
        // slots. The original pointers are stored and called directly — no
        // trampoline, no stolen bytes. MUST happen before export hooks.
        if (!g_VtableHookInstalled) {
            __try {
                TryVtablePatchFromDummy();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                HLog("Init: CRASH in dummy vtable patch");
            }
        }

        // ReShade-style fallback: if dummy creation is unavailable this
        // early, hook creation exports and wait for the REAL game swapchain.
        // Do this after the dummy attempt so our own dummy creation cannot
        // recurse through the creation hooks.
        if (!g_VtableHookInstalled && !d3d::IsDxgiHooksInstalled()) {
            if (d3d::InstallDxgiFactoryExportHooks(&OnDxgiSwapChainCreated)) {
                HLog("Init: D3D11/DXGI creation hooks installed; waiting for real swapchain");
                g_InlineHookInstalled = true;
            } else {
                HLog("Init: D3D11/DXGI creation hook install FAILED");
            }
        }

        // Creation hooks count as successful bootstrap installation even
        // before the real swapchain exists. The callback will set
        // g_VtableHookInstalled and original Present pointers later.
        const bool hooked = g_VtableHookInstalled ||
                            d3d::IsDxgiHooksInstalled() ||
                            g_InlineHookInstalled;
        if (!hooked)
            HLog("Init: Present not hooked yet");

        return hooked;
    }

    void RequestUnload()
    {
        InterlockedExchange(&g_UnloadRequested, TRUE);
    }

    bool IsUnloadRequested()
    {
        return InterlockedCompareExchange(
            &g_UnloadRequested, FALSE, FALSE) != FALSE;
    }

    void Shutdown()
    {
        if (InterlockedCompareExchange(
                &g_ShutdownStarted, TRUE, FALSE) != FALSE)
            return;

        HLog("Shutdown: begin");
        g_MenuOpen = false;

#if RAGEMP_EVENT_LOGGER
        EventLogger::Shutdown();
#endif
        ObsBypass::Shutdown();
        MiscFeatures::Shutdown();
        WorldOverlay::Shutdown();
        PedPreview::Shutdown();
        ModelPreview::Shutdown();
        SilentAim::Shutdown();
        CloudStorage::Shutdown();

        if (g_ImGuiInit && ImGui::GetCurrentContext())
            ReleaseMenuInputCapture(true);

        if (g_SwapChain) {
            __try {
                void** vt = *reinterpret_cast<void***>(g_SwapChain);
                d3d::RestoreSwapChainVtableSlot(
                    vt, d3d::kPresentIndex,
                    reinterpret_cast<void*>(hkPresent),
                    g_OriginalPresent);
                d3d::RestoreSwapChainVtableSlot(
                    vt, d3d::kPresent1Index,
                    reinterpret_cast<void*>(hkPresent1),
                    g_OriginalPresent1);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                HLog("Shutdown: swapchain restore faulted");
            }
        }

        d3d::ShutdownHooks();

        bool wndProcDetached = true;
        if (g_Hwnd && g_OriginalWndProc) {
            const LONG_PTR current =
                GetWindowLongPtrW(g_Hwnd, GWLP_WNDPROC);
            if (current == reinterpret_cast<LONG_PTR>(hkWndProc)) {
                SetWindowLongPtrW(
                    g_Hwnd,
                    GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(g_OriginalWndProc));
                wndProcDetached =
                    GetWindowLongPtrW(g_Hwnd, GWLP_WNDPROC) ==
                    reinterpret_cast<LONG_PTR>(g_OriginalWndProc);
                if (!wndProcDetached)
                    HLog("Shutdown: WndProc restore FAILED; keeping pass-through");
            } else if (current !=
                       reinterpret_cast<LONG_PTR>(g_OriginalWndProc)) {
                // Another overlay may have subclassed the window after us.
                // Its previous-proc pointer can still lead to hkWndProc, so
                // keep our forwarding target alive instead of turning every
                // subsequent keyboard/mouse message into a null call.
                wndProcDetached = false;
                HLog("Shutdown: WndProc is chained; keeping pass-through");
            }
        }

        if (g_ImGuiInit && ImGui::GetCurrentContext()) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }

        ReleaseBackBufferRtv();

        // ImGui hides the OS cursor while drawing its software cursor.
        // There will be no later overlay frame to restore it after unload,
        // so hand a valid cursor and focus back to the game explicitly.
        if (g_Hwnd) {
            if (GetForegroundWindow() == g_Hwnd)
                SetFocus(g_Hwnd);
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        }

        if (g_Context) {
            g_Context->Release();
            g_Context = nullptr;
        }
        if (g_Device) {
            g_Device->Release();
            g_Device = nullptr;
        }

        g_SwapChain = nullptr;
        if (wndProcDetached) {
            g_Hwnd = nullptr;
            g_OriginalWndProc = nullptr;
        }
        g_ImGuiInit = false;
        g_VtableHookInstalled = false;
        g_InlineHookInstalled = false;
        HLog("Shutdown: hooks removed; manual-map image left dormant");
        ReleaseSoleInstanceClaim();
        // Last, so everything above is still recorded. No-op in a build
        // without RAGEMP_DIAGNOSTICS.
        Diagnostics::Shutdown();
    }

    // ------------------------------------------------------------
    // Capture ViewProjection matrix from D3D11 vertex shader constant buffers.
    // Called every frame from hkPresent before rendering ESP.
    // The game uploads the camera VP matrix as a cbuffer bound to VS slot 0.
    // We read it back here so WorldToScreen works without a code pattern.
    // ------------------------------------------------------------
    static void CaptureViewProjection()
    {
        if (!g_Context) return;

        ID3D11Buffer* vsCB[1] = { nullptr };
        UINT strides[1] = { 0 };
        g_Context->VSGetConstantBuffers(0, 1, vsCB);
        if (!vsCB[0]) {
            // Some builds bind VP to slot 1 or 2; try up to 4.
            for (UINT i = 1; i < 4; ++i) {
                g_Context->VSGetConstantBuffers(i, 1, vsCB);
                if (vsCB[0]) break;
            }
        }
        if (!vsCB[0]) return;

        D3D11_BUFFER_DESC desc{};
        vsCB[0]->GetDesc(&desc);
        // Only proceed on a dynamic/staging-acessible cbuffer.
        // We use CopyResource to a staging buffer for safe CPU read.
        if ((desc.CPUAccessFlags & D3D11_CPU_ACCESS_READ) == 0) {
            // Need staging copy
            if (!g_Device) { vsCB[0]->Release(); return; }
            ID3D11Buffer* staging = nullptr;
            D3D11_BUFFER_DESC sdesc = desc;
            sdesc.Usage = D3D11_USAGE_STAGING;
            sdesc.BindFlags = 0;
            sdesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            sdesc.MiscFlags = 0;
            if (FAILED(g_Device->CreateBuffer(&sdesc, nullptr, &staging))) {
                vsCB[0]->Release();
                return;
            }
            g_Context->CopyResource(staging, vsCB[0]);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(g_Context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped))) {
                SIZE_T copy = desc.ByteWidth < sizeof(Game::g_ViewProjection)
                                ? desc.ByteWidth : sizeof(Game::g_ViewProjection);
                memcpy(Game::g_ViewProjection, mapped.pData, copy);
                Game::g_ViewProjectionValid = true;
                g_Context->Unmap(staging, 0);
            }
            staging->Release();
        } else {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(g_Context->Map(vsCB[0], 0, D3D11_MAP_READ_WRITE, 0, &mapped))) {
                SIZE_T copy = desc.ByteWidth < sizeof(Game::g_ViewProjection)
                                ? desc.ByteWidth : sizeof(Game::g_ViewProjection);
                memcpy(Game::g_ViewProjection, mapped.pData, copy);
                Game::g_ViewProjectionValid = true;
                g_Context->Unmap(vsCB[0], 0);
            }
        }
        vsCB[0]->Release();
    }

    // ------------------------------------------------------------
    void RenderMenu() {}

    static void ReleaseMenuInputCapture(bool unloading)
    {
        ImGuiIO& io = ImGui::GetIO();

        // Clear the Win32 backend's button mask before clearing ImGuiIO.
        if (g_Hwnd) {
            ImGui_ImplWin32_WndProcHandler(g_Hwnd, WM_LBUTTONUP, 0, 0);
            ImGui_ImplWin32_WndProcHandler(g_Hwnd, WM_RBUTTONUP, 0, 0);
            ImGui_ImplWin32_WndProcHandler(g_Hwnd, WM_MBUTTONUP, 0, 0);
            ImGui_ImplWin32_WndProcHandler(
                g_Hwnd, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON1), 0);
            ImGui_ImplWin32_WndProcHandler(
                g_Hwnd, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), 0);
        }
        // Hand input ownership back to the game explicitly.  ImGui captures
        // the HWND while a button is held.  On borderless/multi-monitor
        // setups the game may not receive the matching button-up/cursor
        // message and can leave its mouse state latched after the menu closes.
        if (g_Hwnd && g_OriginalWndProc) {
            POINT cursor{};
            if (!GetCursorPos(&cursor) ||
                !ScreenToClient(g_Hwnd, &cursor)) {
                cursor.x = 0;
                cursor.y = 0;
            }
            const LPARAM mousePos =
                MAKELPARAM(
                    static_cast<SHORT>(cursor.x),
                    static_cast<SHORT>(cursor.y));

            CallWindowProcW(
                g_OriginalWndProc, g_Hwnd,
                WM_LBUTTONUP, 0, mousePos);
            CallWindowProcW(
                g_OriginalWndProc, g_Hwnd,
                WM_RBUTTONUP, 0, mousePos);
            CallWindowProcW(
                g_OriginalWndProc, g_Hwnd,
                WM_MBUTTONUP, 0, mousePos);
            CallWindowProcW(
                g_OriginalWndProc, g_Hwnd,
                WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON1), mousePos);
            CallWindowProcW(
                g_OriginalWndProc, g_Hwnd,
                WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON2), mousePos);
            CallWindowProcW(
                g_OriginalWndProc, g_Hwnd,
                WM_MOUSEMOVE, 0, mousePos);

            if (GetForegroundWindow() == g_Hwnd)
                SetFocus(g_Hwnd);
        }

        io.MouseDrawCursor = false;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.ClearEventsQueue();
        io.ClearInputKeys();
        io.ClearInputMouse();
        ImGui::SetNextFrameWantCaptureKeyboard(false);
        ImGui::SetNextFrameWantCaptureMouse(false);
        SetCursor(unloading
            ? LoadCursorW(nullptr, IDC_ARROW)
            : nullptr);

        if (g_Hwnd && g_RestoreGameMouseCapture) {
            // Restore the game's pre-menu capture only after both ImGui and
            // the original WndProc have processed their button-up events.
            // Re-centering keeps the first post-menu click on the game
            // monitor even if the cursor crossed onto another display.
            RECT client{};
            if (GetClientRect(g_Hwnd, &client)) {
                POINT center{
                    (client.left + client.right) / 2,
                    (client.top + client.bottom) / 2
                };
                if (ClientToScreen(g_Hwnd, &center))
                    SetCursorPos(center.x, center.y);
            }
            SetCapture(g_Hwnd);
        } else if (GetCapture() == g_Hwnd) {
            ReleaseCapture();
        }
        g_RestoreGameMouseCapture = false;
    }

    // ------------------------------------------------------------
    //  Shared frame-rendering logic — called from both hkPresent
    //  and hkPresent1 to avoid code duplication.
    // ------------------------------------------------------------
    static void RenderFrame(IDXGISwapChain* pSwap, const char* caller)
    {
        const std::int64_t profileFrame =
            RuntimeProfiler::Begin();
        if (!g_FirstFrameLogged) {
            char msg[64];
            wsprintfA(msg, "%s: ENTERED!", caller);
            HLog(msg);
        }

        ++g_FrameCount;

        if (!g_ImGuiInit) {
            InitImGui(pSwap);
        }

        // Retry until InjectCore's event exists (elevation / race safe).
        NotifyInjectReady();

        if (!g_ImGuiInit)
            return;

        if (!EnsureImGuiDevice(pSwap))
            return;

        // After Present/device fault: do not NewFrame/draw/V8 on a stale
        // device — avoids menu ghost on black screen and CEF races.
        if (InRecoverCooldown()) {
            g_PrevMenuKey = (GetAsyncKeyState(MenuToggleKey()) & 0x8000) != 0;
            ObsBypass::Tick();
            return;
        }

        const bool cefDumpBusy =
            QuantTools::IsCefDumpBusy() || UkraineTools::IsCefDumpBusy();

        // Grab pause-menu ped into private RT and scrub BB (OBS-safe).
        // Skip during CEF dump — Present must stay light (renderer AV).
        if (!cefDumpBusy) {
            __try {
                PedPreview::CaptureAndSanitizeBackbuffer(
                    g_Device, g_Context, pSwap);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }

        if (g_Hwnd) {
            RECT client{};
            if (GetClientRect(g_Hwnd, &client)) {
                const LONG width = client.right - client.left;
                const LONG height = client.bottom - client.top;
                // Minimized or alt-tabbed away. There is nothing to draw into
                // and nothing is broken, so the frame is simply skipped. This
                // used to request a full recovery every frame, which rebuilt
                // the device objects on each one and kept pushing the
                // post-fault cooldown forward, so the overlay stayed dark for
                // a long time after coming back.
                if (width < 64 || height < 64)
                    return;
            }
        }

        const bool logFrame = g_FrameCount <= 8;
        PRESENT_STAGE("NewFrame");

        // --- NewFrame block: ImGui backend + frame start.
        bool newFrameOk = false;
        __try {
            if (logFrame) HLog("hkPresent: ImGui_ImplDX11_NewFrame");
            ImGui_ImplDX11_NewFrame();
            if (logFrame) HLog("hkPresent: ImGui_ImplWin32_NewFrame");
            ImGui_ImplWin32_NewFrame();
            if (logFrame) HLog("hkPresent: ImGui::NewFrame");
            ImGui::NewFrame();
            newFrameOk = true;
        } __except (SafeFilter(GetExceptionInformation(), "hkPresent:NewFrame")) {
            HLog("hkPresent: NewFrame block FAULTED, skipping frame");
            RequestImGuiRecover("ImGui recover: NewFrame FAULTED");
        }

        if (newFrameOk) {
            std::int64_t profileStart = 0;
            // Set while a keybind row is capturing, so the key being assigned
            // does not also fire the action it is being assigned to.
            const bool bindCapture = FeatureBinds::IsCaptureActive();
            FeatureBinds::Tick(
                ImGui::GetIO().WantTextInput ||
                ImGui::GetIO().WantCaptureKeyboard);
            FeatureBinds::SetCaptureActive(false);

            bool curMenuKey = !bindCapture &&
                (GetAsyncKeyState(MenuToggleKey()) & 0x8000) != 0;
            const bool f4Ok = GetTickCount64() >= g_F4SuppressUntil;
            if (f4Ok && curMenuKey && !g_PrevMenuKey) {
                g_MenuOpen = !g_MenuOpen;
                if (g_MenuOpen) {
                    g_RestoreGameMouseCapture =
                        g_Hwnd && GetCapture() == g_Hwnd;
                    ImGui::GetIO().ConfigFlags &=
                        ~ImGuiConfigFlags_NoMouseCursorChange;
                } else {
                    ReleaseMenuInputCapture();
                }
            }
            g_PrevMenuKey = curMenuKey;
            ImGui::GetIO().MouseDrawCursor = g_MenuOpen;

            // Unload is unbound by default; when bound it acts at once, which
            // is the point of a panic key.
            const bool curUnloadKey = Menu::s_UnloadKey > 0 &&
                !bindCapture &&
                !ImGui::GetIO().WantTextInput &&
                (GetAsyncKeyState(Menu::s_UnloadKey) & 0x8000) != 0;
            if (curUnloadKey && !g_PrevUnloadKey)
                RequestUnload();
            g_PrevUnloadKey = curUnloadKey;

#if RAGEMP_DEV_BUILD
            // F8 cycles the synthetic crowd used for ESP measurements. A live
            // server with 100 players is not something we can summon on demand.
            {
                static bool s_PrevCrowdKey = false;
                const bool crowdKey =
                    (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
                if (crowdKey && !s_PrevCrowdKey) {
                    int& crowd = Game::SyntheticCrowdCount();
                    crowd = crowd == 0 ? 50
                        : (crowd == 50 ? 100
                            : (crowd == 100 ? 200 : 0));
                    // The percentile ring holds ~68 s, far longer than one
                    // crowd size is held, so without a reset every phase
                    // reads as a blend of the previous ones.
                    RuntimeProfiler::Reset();
                    char buf[64];
                    wsprintfA(
                        buf,
                        "[SyntheticCrowd] size=%d",
                        crowd);
                    HLog(buf);
                }
                s_PrevCrowdKey = crowdKey;
            }
#endif

            profileStart = RuntimeProfiler::Begin();
            __try {
            PRESENT_STAGE("ScriptBridge");
                ScriptBridge::Update();
            } __except (SafeFilter(GetExceptionInformation(), "hkPresent:ScriptBridge")) {
                HLog("hkPresent: ScriptBridge::Update FAULTED");
            }
            RuntimeProfiler::End(
                RuntimeProfiler::Scope::ScriptBridge,
                profileStart);

            __try {
                AntiCheatShield::Update();
            } __except (SafeFilter(GetExceptionInformation(), "hkPresent:AntiCheatShield")) {
                HLog("hkPresent: AntiCheatShield::Update FAULTED");
            }

            profileStart = RuntimeProfiler::Begin();
            __try {
                QuantTools::Update();
            } __except (SafeFilter(GetExceptionInformation(), "hkPresent:QuantTools")) {
                HLog("hkPresent: QuantTools::Update FAULTED");
            }
            RuntimeProfiler::End(
                RuntimeProfiler::Scope::Quant,
                profileStart);

            profileStart = RuntimeProfiler::Begin();
            __try {
                UkraineTools::Update();
            } __except (SafeFilter(GetExceptionInformation(), "hkPresent:UkraineTools")) {
                HLog("hkPresent: UkraineTools::Update FAULTED");
            }
            RuntimeProfiler::End(
                RuntimeProfiler::Scope::Ukraine,
                profileStart);

            __try {
                FiveRpFarmBot::Update();
            } __except (SafeFilter(GetExceptionInformation(), "hkPresent:FiveRpFarm")) {
                HLog("hkPresent: FiveRpFarmBot::Update FAULTED");
            }

            ObsBypass::Tick();

            if (!cefDumpBusy) {
#if RAGEMP_EVENT_LOGGER
                // Guarded like everything else here because it instruments the
                // game's V8 and a fault in it must not take the frame with it.
                __try {
                    EventLogger::Update();
                } __except (SafeFilter(GetExceptionInformation(), "hkPresent:EventLogger")) {
                    HLog("hkPresent: EventLogger::Update FAULTED");
                }
#endif

                profileStart = RuntimeProfiler::Begin();
                __try {
                PRESENT_STAGE("MiscFeatures");
                    MiscFeatures::Update();
                } __except (SafeFilter(GetExceptionInformation(), "hkPresent:MiscFeatures")) {
                    HLog("hkPresent: MiscFeatures::Update FAULTED");
                }
                RuntimeProfiler::End(
                    RuntimeProfiler::Scope::Misc,
                    profileStart);

                const auto& miscSettings =
                    MiscFeatures::GetSettings();
                const bool playerCacheDemand =
                    Features::g_EspEnabled ||
                    Features::g_AimbotEnabled ||
                    miscSettings.showPlayers ||
                    miscSettings.showAdmins ||
                    miscSettings.showAdminList ||
                    Menu::NeedsPlayerCache();
                if (playerCacheDemand)
                {
                    profileStart = RuntimeProfiler::Begin();
                    __try {
                        Game::RefreshPlayerCache();
                    } __except (SafeFilter(GetExceptionInformation(), "hkPresent:NameResolver")) {
                    }
                    RuntimeProfiler::End(
                        RuntimeProfiler::Scope::PlayerCache,
                        profileStart);
                }

                if (Features::g_EspEnabled ||
                    (Features::g_AimbotEnabled &&
                     !g_MenuOpen))
                {
                    const auto& activeAim =
                        Menu::GetActiveAimProfile();
                    const bool aimNeedsBones =
                        Features::g_AimbotEnabled &&
                        !g_MenuOpen &&
                        (activeAim.vecEnabled ||
                         activeAim.silentEnabled ||
                         activeAim.triggerEnabled);
                    std::uint32_t
                        espBoneProfileMask = 0;
                    bool visibilityDemand = false;
                    for (int profile = 0;
                         profile < static_cast<int>(
                             Menu::EspProfileId::Count);
                         ++profile)
                    {
                        if (!Menu::s_EspProfiles[profile].
                                enabled)
                            continue;
                        if (Menu::s_EspProfiles[profile].
                                skeleton)
                        {
                            espBoneProfileMask |=
                                1u << profile;
                        }
                        if (Menu::s_EspProfiles[profile].
                                visibleCheck)
                            visibilityDemand = true;
                    }
                    // Wallshot ignores the visible check outright, so silent aim
                    // in that mode is not a reason to spend physics queries.
                    if (aimNeedsBones &&
                        ((activeAim.vecEnabled &&
                          activeAim.vecVisible) ||
                         (activeAim.silentEnabled &&
                          activeAim.silentVisible &&
                          !activeAim.silentWallshot) ||
                         (activeAim.triggerEnabled &&
                          activeAim.triggerVisible)))
                        visibilityDemand = true;
                    profileStart =
                        RuntimeProfiler::Begin();
                    __try {
                        PRESENT_STAGE("FramePeds");
                        Game::BuildFramePedContext(
                            g_FrameCount,
                            aimNeedsBones,
                            espBoneProfileMask,
                            visibilityDemand,
                            Menu::s_AdminLevelThreshold);
                        const auto& frame =
                            Game::GetFramePedContext();
                        RuntimeProfiler::SetFrameWork(
                            static_cast<std::uint32_t>(
                                frame.count),
                            frame.boneRefreshes,
                            frame.boneCacheHits,
                            frame.projections);
                    } __except (SafeFilter(
                        GetExceptionInformation(),
                        "hkPresent:FramePedContext")) {
                    }
                    RuntimeProfiler::End(
                        RuntimeProfiler::Scope::
                            FramePedContext,
                        profileStart);
                }

                // Aimbot — sp¬ vector (CPlayerAngles) + silent (bullet_instance).
                if (Features::g_AimbotEnabled && !g_MenuOpen) {
                    profileStart = RuntimeProfiler::Begin();
                    __try {
                        PRESENT_STAGE("Aimbot");
                        Features::RunAimbot();
                    } __except (SafeFilter(GetExceptionInformation(), "hkPresent:Aimbot")) {
                        HLog("hkPresent: Aimbot FAULTED");
                        Features::g_AimbotEnabled = false;
                    }
                    RuntimeProfiler::End(
                        RuntimeProfiler::Scope::Aimbot,
                        profileStart);
                }

                // ESP — game-memory reads, SEH-guarded
                if (Features::g_EspEnabled) {
                    profileStart = RuntimeProfiler::Begin();
                    __try {
                        if (logFrame) HLog("hkPresent: RenderESP BEGIN");
                        PRESENT_STAGE("ESP");
#if defined(RAGEMP_DIAGNOSTICS)
                        VehicleListProbe::Update();
#endif
                        Features::RenderESP(Overlay::GetDrawList());
                        if (logFrame) HLog("hkPresent: RenderESP DONE");
                    } __except (SafeFilter(GetExceptionInformation(), "hkPresent:ESP")) {
                        HLog("hkPresent: ESP FAULTED");
                        Features::g_EspEnabled = false;
                    }
                    RuntimeProfiler::End(
                        RuntimeProfiler::Scope::Esp,
                        profileStart);
                }
            }

            // Render Watermark (always visible)
            __try {
                Menu::Watermark();
            } __except (SafeFilter(GetExceptionInformation(), "hkPresent:Watermark")) {
            }

            // Render menu (handles open/close fade animation).
            profileStart = RuntimeProfiler::Begin();
            __try {
                if (logFrame) HLog("hkPresent: Menu::Render BEGIN");
                PRESENT_STAGE("Menu");
                Menu::Render(g_MenuOpen);
                if (logFrame) HLog("hkPresent: Menu::Render DONE");
            } __except (SafeFilter(GetExceptionInformation(), "hkPresent:Menu")) {
                HLog("hkPresent: Menu FAULTED");
            }
            RuntimeProfiler::End(
                RuntimeProfiler::Scope::Menu,
                profileStart);

            profileStart = RuntimeProfiler::Begin();
            __try {
                PRESENT_STAGE("PlayersOverlay");
                MiscFeatures::RenderPlayersOverlay();
            } __except (SafeFilter(GetExceptionInformation(), "hkPresent:PlayersOverlay")) {
            }
            RuntimeProfiler::End(
                RuntimeProfiler::Scope::PlayersOverlay,
                profileStart);
        } // end newFrameOk

        const std::int64_t profileRender =
            RuntimeProfiler::Begin();
        __try {
            if (logFrame) HLog("hkPresent: ImGui::EndFrame");
            PRESENT_STAGE("ImGuiRender");
            ImGui::EndFrame();
            if (logFrame) HLog("hkPresent: ImGui::Render");
            ImGui::Render();
            // Defer only when OBS capture is nested inside Present and will
            // draw onto the backbuffer RTV after copying. Otherwise draw now
            // so the local screen never goes blank.
            if (!ObsBypass::TryDeferOverlayDraw()) {
                if (logFrame) HLog("hkPresent: DrawImGuiToBackbuffer");
                DrawImGuiToBackbuffer(pSwap);
            } else if (logFrame) {
                HLog("hkPresent: overlay draw deferred (OBS bypass)");
            }
            if (logFrame) HLog("hkPresent: frame render DONE");
        } __except (SafeFilter(GetExceptionInformation(), "hkPresent:EndFrame/Render")) {
            HLog("hkPresent: EndFrame/Render FAULTED");
            RequestImGuiRecover("ImGui recover: EndFrame/Render FAULTED");
        }
        RuntimeProfiler::End(
            RuntimeProfiler::Scope::ImGuiRender,
            profileRender);

        if (g_FrameCount <= 8) {
            char buf[64];
            wsprintfA(buf, "%s: frame %lu render complete", caller, g_FrameCount);
            HLog(buf);
        }

        if (!g_FirstFrameLogged) {
            g_FirstFrameLogged = true;
        }
        RuntimeProfiler::End(
            RuntimeProfiler::Scope::Frame,
            profileFrame);
        // Sub-scope accumulators become one sample per frame here.
        RuntimeProfiler::FlushFrame();
        RuntimeProfiler::TickReport();
    }

#if defined(RAGEMP_DIAGNOSTICS)
    // RAGE puts up its "game renderer has crashed" box off the device's own
    // state, not off what our Present returns, so suppressing the error code
    // there hides the symptom and not the cause. This reads the reason
    // straight from the device and names it the first time it appears.
    static void ReportDeviceHealth() noexcept
    {
        if (!g_Device)
            return;
        static HRESULT s_Reported = S_OK;
        const HRESULT reason = g_Device->GetDeviceRemovedReason();
        if (reason == S_OK || reason == s_Reported)
            return;
        s_Reported = reason;

        const char* name = "unknown";
        switch (reason) {
        case DXGI_ERROR_DEVICE_HUNG:   name = "DEVICE_HUNG"; break;
        case DXGI_ERROR_DEVICE_REMOVED: name = "DEVICE_REMOVED"; break;
        case DXGI_ERROR_DEVICE_RESET:  name = "DEVICE_RESET"; break;
        case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
            name = "DRIVER_INTERNAL_ERROR"; break;
        case DXGI_ERROR_INVALID_CALL:  name = "INVALID_CALL"; break;
        default: break;
        }
        char buf[160];
        wsprintfA(
            buf,
            "hkPresent: DEVICE LOST %s (0x%08X) at stage %s",
            name,
            static_cast<unsigned>(reason),
            g_PresentStage ? g_PresentStage : "none");
        HLog(buf);
    }
#endif

    // Wall clock between two Present calls, i.e. the real frame time. Our
    // own scopes only cover CPU work we do; comparing this against them says
    // whether a lost frame is ours at all.
    static void MeasureFrameInterval() noexcept
    {
        static std::int64_t s_LastEntry = 0;
        const std::int64_t now = RuntimeProfiler::Begin();
        if (s_LastEntry)
        {
            RuntimeProfiler::End(
                RuntimeProfiler::Scope::FrameInterval,
                s_LastEntry);
        }
        s_LastEntry = now;
    }

    // ------------------------------------------------------------
    // hkPresent — IDXGISwapChain::Present hook (vtable slot 8).
    // ------------------------------------------------------------
    extern "C" HRESULT __stdcall hkPresent(
        IDXGISwapChain* pSwap, UINT SyncInterval, UINT Flags)
    {
        using PFN_Present = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
        auto orig = (PFN_Present)g_OriginalPresent;

        if (!orig) {
            if (!g_VtableHookInstalled && !g_InlineHookInstalled)
                Initialize();
            orig = (PFN_Present)g_OriginalPresent;
        }
        if (!orig) {
            if (!g_FirstFrameLogged) HLog("hkPresent: orig is NULL");
            return S_OK;
        }

        MeasureFrameInterval();
#if defined(RAGEMP_DIAGNOSTICS)
        ReportDeviceHealth();
#endif
        ObsBypass::BeginPresent();
        RenderFrame(pSwap, "hkPresent");
        if (IsUnloadRequested())
            Shutdown();

        HRESULT presentHr = S_OK;
        const std::int64_t presentStart = RuntimeProfiler::Begin();
        __try {
            if (g_FrameCount <= 8)
                HLog("hkPresent: calling orig Present");
            presentHr = orig(pSwap, SyncInterval, Flags);
            if (g_FrameCount <= 8)
                HLog("hkPresent: orig Present returned");
        } __except (SafeFilter(GetExceptionInformation(), "hkPresent:origPresent")) {
            HLog("hkPresent: orig Present FAULTED");
            RequestImGuiRecover("ImGui recover: Present FAULTED");
            presentHr = S_OK;
        }
        RuntimeProfiler::End(
            RuntimeProfiler::Scope::PresentCall,
            presentStart);
        if (PresentNeedsRecover(presentHr)) {
            char buf[96];
            wsprintfA(buf, "ImGui recover: Present hr=0x%08X", presentHr);
            RequestImGuiRecover(buf);
            // Don't feed DEVICE_REMOVED back into RAGE's fatal dialog loop.
            presentHr = S_OK;
        }
        // Never draw after Present — that never reaches the display.
        ObsBypass::DiscardDeferredOverlayDraw();
        ObsBypass::EndPresent();
        return presentHr;
    }

    // ------------------------------------------------------------
    // hkPresent1 — IDXGISwapChain1::Present1 hook (vtable slot 22).
    // This is the call path for modern games (GTA V / RageMP).
    // ------------------------------------------------------------
    extern "C" HRESULT __stdcall hkPresent1(
        IDXGISwapChain1* pSwap, UINT SyncInterval, UINT PresentFlags,
        const DXGI_PRESENT_PARAMETERS* pPresentParameters)
    {
        using PFN_Present1 = HRESULT(__stdcall*)(
            IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
        auto orig1 = (PFN_Present1)g_OriginalPresent1;

        if (!orig1) {
            if (!g_FirstFrameLogged) HLog("hkPresent1: orig1 is NULL");
            return S_OK;
        }

        // Present1's first arg is IDXGISwapChain1 which inherits from
        // IDXGISwapChain — safe to cast for InitImGui.
        ObsBypass::BeginPresent();
        RenderFrame(static_cast<IDXGISwapChain*>(pSwap), "hkPresent1");
        if (IsUnloadRequested())
            Shutdown();

        HRESULT presentHr = S_OK;
        __try {
            if (g_FrameCount <= 8)
                HLog("hkPresent1: calling orig Present1");
            presentHr = orig1(pSwap, SyncInterval, PresentFlags, pPresentParameters);
            if (g_FrameCount <= 8)
                HLog("hkPresent1: orig Present1 returned");
        } __except (SafeFilter(GetExceptionInformation(), "hkPresent1:origPresent1")) {
            HLog("hkPresent1: orig Present1 FAULTED");
            RequestImGuiRecover("ImGui recover: Present1 FAULTED");
            presentHr = S_OK;
        }
        if (PresentNeedsRecover(presentHr)) {
            char buf[96];
            wsprintfA(buf, "ImGui recover: Present1 hr=0x%08X", presentHr);
            RequestImGuiRecover(buf);
            presentHr = S_OK;
        }
        ObsBypass::DiscardDeferredOverlayDraw();
        ObsBypass::EndPresent();
        return presentHr;
    }

    // ------------------------------------------------------------
    // hkResizeBuffers — IDXGISwapChain::ResizeBuffers hook (slot 13).
    // DXGI refuses the call while any reference to a back buffer is alive,
    // and we keep a render target view on buffer 0 between frames. Dropping
    // it here is the whole point: the previous build failed the game's
    // resize and took the renderer down with it.
    // ------------------------------------------------------------
    extern "C" HRESULT __stdcall hkResizeBuffers(
        IDXGISwapChain* pSwap,
        UINT BufferCount,
        UINT Width,
        UINT Height,
        DXGI_FORMAT NewFormat,
        UINT SwapChainFlags)
    {
        using PFN_ResizeBuffers = HRESULT(__stdcall*)(
            IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
        auto orig =
            reinterpret_cast<PFN_ResizeBuffers>(g_OriginalResizeBuffers);
        if (!orig)
            return DXGI_ERROR_INVALID_CALL;

        char buf[128];
        wsprintfA(
            buf,
            "hkResizeBuffers: %ux%u count=%u fmt=%d",
            Width,
            Height,
            BufferCount,
            static_cast<int>(NewFormat));
        HLog(buf);

        __try {
            ReleaseSwapChainDependentResources();
        } __except (SafeFilter(
            GetExceptionInformation(),
            "hkResizeBuffers:release")) {
        }

        const HRESULT hr = orig(
            pSwap,
            BufferCount,
            Width,
            Height,
            NewFormat,
            SwapChainFlags);

        if (FAILED(hr)) {
            wsprintfA(buf, "hkResizeBuffers: orig hr=0x%08X", hr);
            HLog(buf);
            RequestImGuiRecover("ImGui recover: resize failed");
            return hr;
        }
        // The next frame rebuilds the view and the overlay targets against
        // the new buffers. A successful resize is not a fault, so the overlay
        // comes back on that frame instead of after a cooldown.
        RequestDeviceRebuild("ImGui rebuild: swapchain buffers resized");
        return hr;
    }

    // ------------------------------------------------------------
    // hkSetFullscreenState — slot 10. A fullscreen transition resizes the
    // buffers internally, so the same references have to go first.
    // ------------------------------------------------------------
    extern "C" HRESULT __stdcall hkSetFullscreenState(
        IDXGISwapChain* pSwap,
        BOOL Fullscreen,
        IDXGIOutput* pTarget)
    {
        using PFN_SetFullscreenState = HRESULT(__stdcall*)(
            IDXGISwapChain*, BOOL, IDXGIOutput*);
        auto orig = reinterpret_cast<PFN_SetFullscreenState>(
            g_OriginalSetFullscreenState);
        if (!orig)
            return DXGI_ERROR_INVALID_CALL;

        char buf[96];
        wsprintfA(
            buf,
            "hkSetFullscreenState: fullscreen=%d",
            Fullscreen ? 1 : 0);
        HLog(buf);

        __try {
            ReleaseSwapChainDependentResources();
        } __except (SafeFilter(
            GetExceptionInformation(),
            "hkSetFullscreenState:release")) {
        }

        const HRESULT hr = orig(pSwap, Fullscreen, pTarget);
        if (FAILED(hr)) {
            wsprintfA(buf, "hkSetFullscreenState: orig hr=0x%08X", hr);
            HLog(buf);
            RequestImGuiRecover("ImGui recover: fullscreen switch failed");
            return hr;
        }
        RequestDeviceRebuild("ImGui rebuild: fullscreen state changed");
        return hr;
    }

    // ------------------------------------------------------------
    // hkWndProc — input hook for ImGui.
    // ------------------------------------------------------------
    static void SubmitRawMouseInputToImGui(LPARAM lParam)
    {
        RAWINPUT input{};
        UINT size = sizeof(input);
        const UINT copied = GetRawInputData(
            reinterpret_cast<HRAWINPUT>(lParam),
            RID_INPUT,
            &input,
            &size,
            sizeof(RAWINPUTHEADER));
        if (copied == static_cast<UINT>(-1) ||
            input.header.dwType != RIM_TYPEMOUSE) {
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
        const USHORT flags = input.data.mouse.usButtonFlags;
        if (flags & RI_MOUSE_LEFT_BUTTON_DOWN)
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        if (flags & RI_MOUSE_LEFT_BUTTON_UP)
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        if (flags & RI_MOUSE_RIGHT_BUTTON_DOWN)
            io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
        if (flags & RI_MOUSE_RIGHT_BUTTON_UP)
            io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
        if (flags & RI_MOUSE_MIDDLE_BUTTON_DOWN)
            io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
        if (flags & RI_MOUSE_MIDDLE_BUTTON_UP)
            io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
        if (flags & RI_MOUSE_BUTTON_4_DOWN)
            io.AddMouseButtonEvent(3, true);
        if (flags & RI_MOUSE_BUTTON_4_UP)
            io.AddMouseButtonEvent(3, false);
        if (flags & RI_MOUSE_BUTTON_5_DOWN)
            io.AddMouseButtonEvent(4, true);
        if (flags & RI_MOUSE_BUTTON_5_UP)
            io.AddMouseButtonEvent(4, false);
        if (flags & RI_MOUSE_WHEEL) {
            const SHORT wheel =
                static_cast<SHORT>(input.data.mouse.usButtonData);
            io.AddMouseWheelEvent(
                0.f,
                static_cast<float>(wheel) /
                    static_cast<float>(WHEEL_DELTA));
        }
        if (flags & RI_MOUSE_HWHEEL) {
            const SHORT wheel =
                static_cast<SHORT>(input.data.mouse.usButtonData);
            io.AddMouseWheelEvent(
                -static_cast<float>(wheel) /
                    static_cast<float>(WHEEL_DELTA),
                0.f);
        }
    }

    extern "C" LRESULT __stdcall hkWndProc(
        HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (g_ImGuiInit) {
            ImGuiIO& io = ImGui::GetIO();

            // The toggle key is owned by the overlay (GetAsyncKeyState drives
            // it), so the game must not see it. While a keybind row captures,
            // the press has to reach ImGui to be recorded.
            if ((msg == WM_KEYDOWN || msg == WM_KEYUP ||
                 msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) &&
                wParam == static_cast<WPARAM>(MenuToggleKey()) &&
                !FeatureBinds::IsCaptureActive()) {
                return 0;
            }

            // The close animation may keep drawing, but it must not keep
            // intercepting game input after the menu is logically closed.
            io.MouseDrawCursor = g_MenuOpen;

            if (g_MenuOpen) {
                // Pass input to ImGui
                ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);

                switch (msg) {
                case WM_INPUT:
                    SubmitRawMouseInputToImGui(lParam);
                    // WM_INPUT with RIM_INPUT must reach DefWindowProc for
                    // mandatory raw-input cleanup.
                    return DefWindowProcW(hWnd, msg, wParam, lParam);

                case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK:
                case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK:
                case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK:
                case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK:
                case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
                case WM_MOUSEMOVE:
                case WM_KEYDOWN: case WM_KEYUP:
                case WM_SYSKEYDOWN: case WM_SYSKEYUP:
                case WM_CHAR:
                case WM_LBUTTONUP: case WM_RBUTTONUP:
                case WM_MBUTTONUP: case WM_XBUTTONUP:
                    return 1;
                }
            }
        }
        return CallWindowProcW(g_OriginalWndProc, hWnd, msg, wParam, lParam);
    }

} // namespace hooks
