#include "ObsBypass.h"
#include "Overlay.h"
#include "WorldOverlay.h"
#include "../Main/Hooks.h"
#include "../Memory/Pattern.h"
#include "../vmp_markers.hpp"
#include <MinHook.h>
#include "../imgui/imgui.h"
#include "../imgui/imgui_impl_dx11.h"

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <Psapi.h>
#include <atomic>

namespace ObsBypass {
namespace {

std::atomic<bool> g_Enabled{false};
std::atomic<bool> g_Hooked{false};
std::atomic<bool> g_DrawPending{false};
std::atomic<bool> g_InsidePresent{false};
std::atomic<bool> g_SeenNestedCapture{false};
// After the first nested OBS capture, stay in stream-proof mode while
// enabled — do not require fragile per-frame IsCaptureActive gates.
std::atomic<bool> g_StickyStreamProof{false};
std::atomic<bool> g_LocalDrawOk{false};
std::atomic<bool> g_DeferThisPresent{false};
std::atomic<bool> g_ServedThisPresent{false};
std::atomic<ULONGLONG> g_LastCaptureMs{0};
std::atomic<ULONG> g_CaptureCalls{0};
std::atomic<ULONG> g_DrawCalls{0};
std::atomic<int> g_PresentDepth{0};
// Frames presented since OBS last copied the backbuffer. Deferring the overlay
// means the only thing that paints it is the capture callback, so once capture
// stops the screen has nothing on it. Counting frames instead of waiting out a
// wall-clock grace keeps that gap down to a few frames.
std::atomic<ULONG> g_FramesSinceCapture{0};
// Game Capture calls this once per Present while a source is live. Three is
// slack for a dropped frame without letting the overlay stay invisible.
constexpr ULONG kMaxIdleFramesBeforeLocalDraw = 3;

using CaptureFn2 = void (*)(void* swap, void* backbuffer);

CaptureFn2 g_OrigCapture2 = nullptr;
void* g_HookTarget = nullptr;

enum class FailReason : int {
    None = 0,
    MarkerMissing,
    XrefMissing,
    BoundsUnknown,
    MinHookFailed,
    PointersMissing
};

FailReason g_FailReason{FailReason::None};
char g_StatusDetail[192]{"obs idle"};
int g_ModuleSize = 0;
int g_LastMhOk = 0;
int g_LastMhWanted = 0;
int g_LastPtrCount = 0;
char g_LastStrat[40]{"none"};
uint8_t* g_FoundFns[4]{};
int g_FoundCount = 0;
HMODULE g_LastObsMod = nullptr;

constexpr int kMaxCaptureHooks = 4;
struct CaptureHook final {
    uint8_t* target{};
    CaptureFn2 orig{};
    void* stub{};
    bool minhook{};
};
CaptureHook g_CaptureHooks[kMaxCaptureHooks]{};
int g_CaptureHookCount = 0;

// OBS stores data.capture as a raw function pointer. Patching those pointers
// is more reliable than a prologue hook when several functions share a prologue.
constexpr int kMaxPtrPatches = 8;
void** g_PatchedSlots[kMaxPtrPatches]{};
void* g_PatchedOrigFn[kMaxPtrPatches]{};
void* g_PatchedHookFn[kMaxPtrPatches]{};
int g_PatchedCount = 0;

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

// Paint ImGui onto the game backbuffer AFTER OBS has copied a clean frame.
// Do not require g_DrawPending — capture often races ahead of TryDefer on the
// first frames; gating on pending left the stream-proof path permanently idle.
bool DrawOverlayAfterCapture(void* swapPtr, void* backbufferPtr) noexcept
{
    if (!ImGui::GetCurrentContext())
        return false;

    ImDrawData* data = ImGui::GetDrawData();
    if (!data || data->CmdListsCount <= 0)
        return false;

    // ImGui DX11 backend is bound to the Present-thread device. Prefer that
    // so RenderDrawData and CreateRTV share one device/context.
    ID3D11Device* device = hooks::g_Device;
    ID3D11DeviceContext* ctx = hooks::g_Context;
    bool ownDevice = false;
    bool ownCtx = false;

    ID3D11Texture2D* bb = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11RenderTargetView* oldRtv = nullptr;
    ID3D11DepthStencilView* oldDsv = nullptr;
    bool drew = false;
    int failStep = 0;

    if (!device || !ctx) {
        IDXGISwapChain* swap = static_cast<IDXGISwapChain*>(swapPtr);
        if (!swap)
            return false;
        if (FAILED(swap->GetDevice(__uuidof(ID3D11Device),
                                   reinterpret_cast<void**>(&device))) ||
            !device)
            return false;
        ownDevice = true;
        device->GetImmediateContext(&ctx);
        if (!ctx) {
            device->Release();
            return false;
        }
        ownCtx = true;
    }

    __try {
        ctx->OMGetRenderTargets(1, &oldRtv, &oldDsv);

        if (backbufferPtr) {
            reinterpret_cast<IUnknown*>(backbufferPtr)
                ->QueryInterface(__uuidof(ID3D11Texture2D),
                                 reinterpret_cast<void**>(&bb));
        }
        if (!bb && swapPtr) {
            static_cast<IDXGISwapChain*>(swapPtr)->GetBuffer(
                0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&bb));
        }
        if (!bb) {
            failStep = 1;
            __leave;
        }

        D3D11_TEXTURE2D_DESC td{};
        bb->GetDesc(&td);

        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
        rtvDesc.Format = TypedRtvFormat(td.Format);
        if (td.SampleDesc.Count > 1) {
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMS;
        } else {
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            rtvDesc.Texture2D.MipSlice = 0;
        }

        HRESULT hr = device->CreateRenderTargetView(bb, &rtvDesc, &rtv);
        if (FAILED(hr) || !rtv)
            hr = device->CreateRenderTargetView(bb, nullptr, &rtv);
        if (FAILED(hr) || !rtv) {
            failStep = 2;
            __leave;
        }

        ctx->OMSetRenderTargets(1, &rtv, nullptr);
        ImGui_ImplDX11_RenderDrawData(data);
        drew = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        drew = false;
        failStep = 3;
    }

    if (ctx) {
        __try {
            ctx->OMSetRenderTargets(1, &oldRtv, oldDsv);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    if (oldRtv)
        oldRtv->Release();
    if (oldDsv)
        oldDsv->Release();
    if (rtv)
        rtv->Release();
    if (bb)
        bb->Release();
    if (ownCtx && ctx)
        ctx->Release();
    if (ownDevice && device)
        device->Release();

    if (drew) {
        g_DrawPending.store(false);
        g_LocalDrawOk.store(true);
        g_ServedThisPresent.store(true);
        g_DrawCalls.fetch_add(1);
        return true;
    }

    static ULONG s_FailLogs = 0;
    if (s_FailLogs < 6) {
        ++s_FailLogs;
        char buf[96];
        wsprintfA(buf, "ObsBypass: post-capture draw fail step=%d", failStep);
        hooks::HLog(buf);
    }
    return false;
}

void AfterOrigCapture(void* swap, void* backbuffer) noexcept
{
    const ULONG n = g_CaptureCalls.load();
    const bool nested = g_InsidePresent.load();
    const bool deferred = g_DeferThisPresent.load();

    // Ensure the GPU copy is ordered before we dirty the backbuffer.
    // Without this, async shtex copies can race and OBS records the overlay.
    if (hooks::g_Context) {
        __try {
            hooks::g_Context->Flush();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    if (nested)
        g_SeenNestedCapture.store(true);
    // Any live capture while bypass is on arms sticky stream-proof.
    if (g_Enabled.load())
        g_StickyStreamProof.store(true);

    // Paint menu/ESP for the local display only — after the copy.
    bool drew = false;
    if (g_Enabled.load()) {
        WorldOverlay::ComposeAfterCapture(
            hooks::g_Device,
            hooks::g_Context,
            static_cast<IDXGISwapChain*>(swap));
        drew = DrawOverlayAfterCapture(swap, backbuffer);
    }

    if (n <= 16 || (n % 120ull) == 0) {
        char buf[128];
        wsprintfA(
            buf,
            "ObsBypass: capture#%lu nested=%d deferred=%d drew=%d sticky=%d",
            n,
            nested ? 1 : 0,
            deferred ? 1 : 0,
            drew ? 1 : 0,
            g_StickyStreamProof.load() ? 1 : 0);
        hooks::HLog(buf);
    }

    // Pre-Present paint leaked into OBS — scream once so logs catch it.
    if (!deferred && nested && g_Enabled.load()) {
        static ULONG s_LeakLogs = 0;
        if (s_LeakLogs < 8) {
            ++s_LeakLogs;
            hooks::HLog(
                "ObsBypass: LEAK — overlay was drawn before capture "
                "(defer failed)");
        }
    }
}

void RunCaptureHook(void* swap, void* backbuffer, CaptureFn2 orig) noexcept
{
    g_LastCaptureMs.store(GetTickCount64());
    g_FramesSinceCapture.store(0);
    g_CaptureCalls.fetch_add(1);
    if (orig)
        orig(swap, backbuffer);
    AfterOrigCapture(swap, backbuffer);
}

void __cdecl HkCapture2(void* swap, void* backbuffer)
{
    CaptureFn2 orig = g_OrigCapture2;
    if (g_CaptureHookCount > 0 && g_CaptureHooks[0].orig)
        orig = g_CaptureHooks[0].orig;
    RunCaptureHook(swap, backbuffer, orig);
}

void __cdecl HkCaptureB(void* swap, void* backbuffer)
{
    CaptureFn2 orig = g_CaptureHookCount > 1 ? g_CaptureHooks[1].orig : g_OrigCapture2;
    RunCaptureHook(swap, backbuffer, orig);
}

void __cdecl HkCaptureC(void* swap, void* backbuffer)
{
    CaptureFn2 orig = g_CaptureHookCount > 2 ? g_CaptureHooks[2].orig : g_OrigCapture2;
    RunCaptureHook(swap, backbuffer, orig);
}

void __cdecl HkCaptureD(void* swap, void* backbuffer)
{
    CaptureFn2 orig = g_CaptureHookCount > 3 ? g_CaptureHooks[3].orig : g_OrigCapture2;
    RunCaptureHook(swap, backbuffer, orig);
}

void* StubForIndex(int index) noexcept
{
    if (index == 1)
        return reinterpret_cast<void*>(&HkCaptureB);
    if (index == 2)
        return reinterpret_cast<void*>(&HkCaptureC);
    if (index == 3)
        return reinterpret_cast<void*>(&HkCaptureD);
    return reinterpret_cast<void*>(&HkCapture2);
}

const char* FailStatusText(FailReason reason) noexcept
{
    if (reason == FailReason::MarkerMissing)
        return "OBS found, marker missing";
    if (reason == FailReason::XrefMissing)
        return "OBS found, xref missing";
    if (reason == FailReason::BoundsUnknown)
        return "OBS found, function bounds unknown";
    if (reason == FailReason::MinHookFailed)
        return "OBS found, MinHook failed";
    if (reason == FailReason::PointersMissing)
        return "OBS found, capture pointers missing";
    return "OBS found, hook failed";
}

void RebuildStatusDetail() noexcept
{
    char rvas[80]{};
    int used = 0;
    for (int i = 0; i < g_FoundCount && used < 70; ++i) {
        const unsigned long rva = g_LastObsMod && g_FoundFns[i]
            ? static_cast<unsigned long>(
                  g_FoundFns[i] - reinterpret_cast<uint8_t*>(g_LastObsMod))
            : 0;
        const int n = wsprintfA(rvas + used, used ? ",0x%lX" : "0x%lX", rva);
        if (n > 0)
            used += n;
    }
    wsprintfA(
        g_StatusDetail,
        "obs size=0x%X rva=%s strat=%s mh=%d/%d ptrs=%d reason=%d",
        g_ModuleSize,
        rvas[0] ? rvas : "-",
        g_LastStrat,
        g_LastMhOk,
        g_LastMhWanted,
        g_LastPtrCount,
        static_cast<int>(g_FailReason));
}

void RememberFail(FailReason reason) noexcept
{
    g_FailReason = reason;
    RebuildStatusDetail();
    hooks::HLog(g_StatusDetail);
}

uint8_t* FindModuleString(HMODULE mod, const char* text) noexcept
{
    if (!mod || !text)
        return nullptr;
    MODULEINFO info{};
    if (!GetModuleInformation(GetCurrentProcess(), mod, &info, sizeof(info)))
        return nullptr;
    const size_t len = strlen(text);
    auto* base = static_cast<uint8_t*>(info.lpBaseOfDll);
    const size_t size = info.SizeOfImage;
    if (len == 0 || size < len)
        return nullptr;
    for (size_t i = 0; i + len <= size; ++i) {
        if (memcmp(base + i, text, len) == 0)
            return base + i;
    }
    return nullptr;
}

bool ProtectIsExecutable(DWORD protect) noexcept
{
    if (protect & PAGE_GUARD)
        return false;
    return (protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                       PAGE_EXECUTE_READWRITE |
                       PAGE_EXECUTE_WRITECOPY)) != 0;
}

// Finds the RIP-relative lea that references `target`.
//
// One VirtualQuery per memory region, not per byte. The previous loop probed
// every address inside the module's code, which is hundreds of thousands of
// syscalls run from the render thread — it showed up as a multi-second freeze
// on the first frame after OBS appeared.
uint8_t* FindRipRelativeXref(
    uint8_t* base,
    uint8_t* end,
    uint8_t* target) noexcept
{
    uint8_t* cursor = base;
    while (cursor < end)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(cursor, &mbi, sizeof(mbi)))
            break;

        auto* regionBase = static_cast<uint8_t*>(mbi.BaseAddress);
        uint8_t* regionEnd = regionBase + mbi.RegionSize;
        if (regionEnd > end)
            regionEnd = end;
        if (regionEnd <= cursor)
            break;

        if (mbi.State == MEM_COMMIT &&
            ProtectIsExecutable(mbi.Protect))
        {
            uint8_t* scan = cursor > regionBase ? cursor : regionBase;
            for (; scan + 7 < regionEnd; ++scan)
            {
                // lea r64,[rip+disp32] or mov r64,[rip+disp32]
                if ((scan[0] != 0x48 && scan[0] != 0x4C) ||
                    (scan[1] != 0x8D && scan[1] != 0x8B) ||
                    (scan[2] & 0xC7) != 0x05)
                    continue;
                const int32_t rel =
                    *reinterpret_cast<int32_t*>(scan + 3);
                if (scan + 7 + rel == target)
                    return scan;
            }
        }

        cursor = regionEnd;
    }
    return nullptr;
}

uint8_t* FindByPrologue(uint8_t* base, uint8_t* ref) noexcept
{
    for (int back = 0; back < 0x400; ++back) {
        uint8_t* cursor = ref - back;
        if (cursor <= base)
            break;
        if (cursor[0] == 0xCC && cursor[1] != 0xCC)
            return cursor + 1;
        const bool prologue =
            (cursor[0] == 0x48 && cursor[1] == 0x89 && cursor[2] == 0x5C) ||
            (cursor[0] == 0x40 && cursor[1] == 0x53) ||
            (cursor[0] == 0x48 && cursor[1] == 0x83 && cursor[2] == 0xEC);
        if (prologue && cursor > base && cursor[-1] == 0xCC)
            return cursor;
    }
    return nullptr;
}

uint8_t* FindByPdata(uint8_t* base, uint8_t* ref) noexcept
{
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return nullptr;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return nullptr;
    const IMAGE_DATA_DIRECTORY dir =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    if (!dir.VirtualAddress || !dir.Size)
        return nullptr;
    auto* rows = reinterpret_cast<RUNTIME_FUNCTION*>(base + dir.VirtualAddress);
    const int count = static_cast<int>(dir.Size / sizeof(RUNTIME_FUNCTION));
    const DWORD rva = static_cast<DWORD>(ref - base);
    for (int i = 0; i < count; ++i) {
        if (rva >= rows[i].BeginAddress && rva < rows[i].EndAddress)
            return base + rows[i].BeginAddress;
    }
    return nullptr;
}

bool RememberFunction(uint8_t* fn, const char* strat) noexcept
{
    if (!fn)
        return false;
    for (int i = 0; i < g_FoundCount; ++i) {
        if (g_FoundFns[i] == fn)
            return true;
    }
    if (g_FoundCount >= 4)
        return true;
    g_FoundFns[g_FoundCount++] = fn;
    lstrcpynA(g_LastStrat, strat, sizeof(g_LastStrat));
    return true;
}

uint8_t* ResolveOneMarker(uint8_t* base, uint8_t* end, uint8_t* str) noexcept
{
    uint8_t* ref = FindRipRelativeXref(base, end, str);
    if (!ref)
        return nullptr;
    uint8_t* fn = FindByPdata(base, ref);
    if (fn && RememberFunction(fn, "pdata"))
        return fn;
    fn = FindByPrologue(base, ref);
    if (fn && RememberFunction(fn, "prologue"))
        return fn;
    return nullptr;
}

int ResolveCaptureFns(HMODULE mod) noexcept
{
    g_FoundCount = 0;
    lstrcpynA(g_LastStrat, "none", sizeof(g_LastStrat));
    if (!mod)
        return 0;
    MODULEINFO info{};
    if (!GetModuleInformation(GetCurrentProcess(), mod, &info, sizeof(info)))
        return 0;
    auto* base = static_cast<uint8_t*>(info.lpBaseOfDll);
    uint8_t* end = base + info.SizeOfImage;
    g_ModuleSize = static_cast<int>(info.SizeOfImage);
    g_LastObsMod = mod;
    static const char* kMarkers[] = {
        "d3d11_shtex_capture: failed to get backbuffer",
        "d3d11_shmem_capture: failed to get backbuffer",
        "d3d11_shtex_capture",
        "d3d11_shmem_capture"
    };
    bool sawMarker = false;
    bool sawXref = false;
    for (const char* text : kMarkers) {
        uint8_t* str = FindModuleString(mod, text);
        if (!str)
            continue;
        sawMarker = true;
        uint8_t* fn = ResolveOneMarker(base, end, str);
        if (FindRipRelativeXref(base, end, str))
            sawXref = true;
        (void)fn;
    }
    if (g_FoundCount > 0)
        return g_FoundCount;
    if (!sawMarker)
        RememberFail(FailReason::MarkerMissing);
    else if (!sawXref)
        RememberFail(FailReason::XrefMissing);
    else
        RememberFail(FailReason::BoundsUnknown);
    return 0;
}

bool PatchCapturePointers(HMODULE mod, void* captureFn, void* hookFn) noexcept
{
    if (!mod || !captureFn || !hookFn)
        return false;

    MODULEINFO info{};
    if (!GetModuleInformation(GetCurrentProcess(), mod, &info, sizeof(info)))
        return false;

    auto* base = static_cast<uint8_t*>(info.lpBaseOfDll);
    uint8_t* end = base + info.SizeOfImage;
    const auto want = reinterpret_cast<uintptr_t>(captureFn);
    int patched = 0;

    for (uint8_t* p = base; p + sizeof(void*) <= end; p += sizeof(void*)) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(p, &mbi, sizeof(mbi)))
            break;
        const bool writable =
            (mbi.State == MEM_COMMIT) &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY |
                            PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) &&
            !(mbi.Protect & PAGE_GUARD);
        uint8_t* regionEnd =
            static_cast<uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
        if (regionEnd > end)
            regionEnd = end;

        if (!writable) {
            p = regionEnd - sizeof(void*);
            continue;
        }

        for (; p + sizeof(void*) <= regionEnd; p += sizeof(void*)) {
            auto* slot = reinterpret_cast<void**>(p);
            if (reinterpret_cast<uintptr_t>(*slot) != want)
                continue;

            bool known = false;
            for (int i = 0; i < g_PatchedCount; ++i) {
                if (g_PatchedSlots[i] == slot) {
                    known = true;
                    break;
                }
            }
            if (!known && g_PatchedCount < kMaxPtrPatches) {
                g_PatchedSlots[g_PatchedCount] = slot;
                g_PatchedOrigFn[g_PatchedCount] = captureFn;
                g_PatchedHookFn[g_PatchedCount] = hookFn;
                ++g_PatchedCount;
            }

            DWORD oldProtect = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect))
                continue;
            // Keep MinHook trampoline if we already have it — never store the
            // raw hooked address or HkCapture2 will recurse into itself.
            if (!g_OrigCapture2)
                g_OrigCapture2 = reinterpret_cast<CaptureFn2>(*slot);
            *slot = hookFn;
            VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);
            ++patched;
        }
        p = regionEnd - sizeof(void*);
    }

    if (patched > 0) {
        char buf[80];
        wsprintfA(buf, "ObsBypass: patched %d capture pointer(s)", patched);
        hooks::HLog(buf);
    }
    return patched > 0 || g_PatchedCount > 0;
}

void RefreshCapturePointers(HMODULE mod) noexcept
{
    if (!mod || !g_Hooked.load())
        return;
    bool reverted = false;
    for (int i = 0; i < g_PatchedCount; ++i) {
        void** slot = g_PatchedSlots[i];
        if (!slot || !g_PatchedOrigFn[i] || !g_PatchedHookFn[i])
            continue;
        __try {
            if (*slot == g_PatchedOrigFn[i]) {
                DWORD oldProtect = 0;
                if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
                    *slot = g_PatchedHookFn[i];
                    VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);
                    reverted = true;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    static ULONGLONG s_LastScan = 0;
    const ULONGLONG now = GetTickCount64();
    if ((!g_PatchedCount || reverted) && now - s_LastScan > 1000ull) {
        s_LastScan = now;
        for (int i = 0; i < g_CaptureHookCount; ++i) {
            PatchCapturePointers(
                mod,
                g_CaptureHooks[i].target,
                g_CaptureHooks[i].stub);
        }
    }
}

bool InstallOneMinHook(CaptureHook& hook) noexcept
{
    if (!hook.target || hook.minhook)
        return hook.minhook;
    static bool mhReady = false;
    if (!mhReady) {
        const MH_STATUS st = MH_Initialize();
        if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED)
            return false;
        mhReady = true;
    }
    CaptureFn2 trampoline = nullptr;
    if (MH_CreateHook(hook.target, hook.stub, reinterpret_cast<void**>(&trampoline)) != MH_OK)
        return false;
    if (MH_EnableHook(hook.target) != MH_OK) {
        MH_RemoveHook(hook.target);
        return false;
    }
    hook.orig = trampoline;
    hook.minhook = true;
    if (!g_OrigCapture2)
        g_OrigCapture2 = trampoline;
    if (!g_HookTarget)
        g_HookTarget = hook.target;
    return true;
}

uint8_t* g_ResolvedCapture = nullptr;

bool TryInstall() noexcept
{
    SHADOW_PROTECT_VIRT("Obs.Install");
    HMODULE obs = GetModuleHandleW(L"graphics-hook64.dll");
    if (!obs)
        obs = GetModuleHandleW(L"graphics-hook.dll");
    if (!obs) {
        SHADOW_PROTECT_END();
        return false;
    }
    if (g_FoundCount == 0 && ResolveCaptureFns(obs) == 0) {
        SHADOW_PROTECT_END();
        return false;
    }
    g_ResolvedCapture = g_FoundFns[0];
    g_LastMhWanted = g_FoundCount;
    g_LastMhOk = 0;
    bool ok = false;
    g_CaptureHookCount = 0;
    for (int i = 0; i < g_FoundCount && i < kMaxCaptureHooks; ++i) {
        CaptureHook& hook = g_CaptureHooks[g_CaptureHookCount];
        hook.target = g_FoundFns[i];
        hook.stub = StubForIndex(i);
        hook.orig = nullptr;
        hook.minhook = false;
        if (InstallOneMinHook(hook)) {
            ++g_LastMhOk;
            ok = true;
        }
        if (PatchCapturePointers(obs, hook.target, hook.stub))
            ok = true;
        ++g_CaptureHookCount;
    }
    g_LastPtrCount = g_PatchedCount;
    if (!ok) {
        for (int i = 0; i < g_CaptureHookCount; ++i) {
            if (g_CaptureHooks[i].minhook && g_CaptureHooks[i].target) {
                MH_DisableHook(g_CaptureHooks[i].target);
                MH_RemoveHook(g_CaptureHooks[i].target);
            }
            g_CaptureHooks[i] = {};
        }
        g_CaptureHookCount = 0;
        RememberFail(g_LastMhOk == 0 && g_FoundCount > 0
            ? FailReason::MinHookFailed
            : FailReason::PointersMissing);
        SHADOW_PROTECT_END();
        return false;
    }
    g_FailReason = FailReason::None;
    g_Hooked.store(true);
    if (!g_OrigCapture2)
        g_OrigCapture2 = reinterpret_cast<CaptureFn2>(g_ResolvedCapture);
    RebuildStatusDetail();
    hooks::HLog(g_StatusDetail);
    SHADOW_PROTECT_END();
    return true;
}

} // namespace

bool IsEnabled() noexcept
{
    return g_Enabled.load();
}

void SetEnabled(bool enabled) noexcept
{
    g_Enabled.store(enabled);
    Overlay::SetStreamProofEnabled(enabled);
    if (!enabled) {
        g_DrawPending.store(false);
        g_LocalDrawOk.store(false);
        g_StickyStreamProof.store(false);
        g_SeenNestedCapture.store(false);
        return;
    }
    Tick();
}

bool IsHooked() noexcept
{
    return g_Hooked.load();
}

bool IsCaptureActive() noexcept
{
    const ULONGLONG last = g_LastCaptureMs.load();
    if (!last)
        return false;
    return (GetTickCount64() - last) < 1000ull;
}

const char* StatusText() noexcept
{
    if (!g_Enabled.load())
        return "Off";
    if (!GetModuleHandleW(L"graphics-hook64.dll") &&
        !GetModuleHandleW(L"graphics-hook.dll"))
        return "Waiting for OBS Game Capture";
    if (!g_Hooked.load())
        return FailStatusText(g_FailReason);
    if (g_StickyStreamProof.load() && g_LocalDrawOk.load())
        return "Active — you see ESP, OBS Game Capture does not";
    if (IsCaptureActive())
        return "Capturing (arming stream-proof...)";
    return "Hooked — start Game Capture on GTA5";
}

const char* StatusDetail() noexcept
{
    return g_StatusDetail;
}

void BeginPresent() noexcept
{
    const int depth = g_PresentDepth.fetch_add(1) + 1;
    g_InsidePresent.store(true);
    // Only the outermost Present owns defer flags (Present+Present1 re-enter).
    if (depth == 1) {
        g_DeferThisPresent.store(false);
        g_ServedThisPresent.store(false);
        if (g_FramesSinceCapture.load() < 0xFFFFu)
            g_FramesSinceCapture.fetch_add(1);
    }
}

void EndPresent() noexcept
{
    const int depth = g_PresentDepth.fetch_sub(1) - 1;
    if (depth <= 0) {
        g_PresentDepth.store(0);
        g_InsidePresent.store(false);
    }
}

bool TryDeferOverlayDraw() noexcept
{
    if (!g_Enabled.load() || !g_Hooked.load())
        return false;

    // Arm sticky mode from any prior nested capture.
    if (g_SeenNestedCapture.load())
        g_StickyStreamProof.store(true);

    const bool sticky = g_StickyStreamProof.load();
    if (!sticky) {
        // First frames before OBS has nested once: paint locally so the
        // monitor is not blank. One dirty OBS frame is acceptable.
        return false;
    }

    // Capture stopped (source hidden, scene switched, OBS closed). Deferring
    // now would leave the overlay unpainted, because the capture callback is
    // the only thing that draws it while sticky. A wall-clock grace of five
    // seconds meant five seconds of blank ESP and menu; a few frames is enough
    // to tell "between captures" from "capture is gone".
    if (g_FramesSinceCapture.load() > kMaxIdleFramesBeforeLocalDraw) {
        g_StickyStreamProof.store(false);
        g_SeenNestedCapture.store(false);
        g_DrawPending.store(false);
        g_DeferThisPresent.store(false);
        return false;
    }

    // NEVER paint ImGui onto the backbuffer before Present while sticky.
    // Post-capture draw restores the local overlay. Missing a local frame
    // is better than leaking ESP into the recording.
    g_DrawPending.store(true);
    g_DeferThisPresent.store(true);
    return true;
}

void DiscardDeferredOverlayDraw() noexcept
{
    // Do NOT force pre-Present draws on miss — that is exactly how overlays
    // leaked into Game Capture (ForceLocalFrames).
    g_DeferThisPresent.store(false);
    g_DrawPending.store(false);
}

void Tick() noexcept
{
    if (!g_Enabled.load())
        return;

    HMODULE obs = GetModuleHandleW(L"graphics-hook64.dll");
    if (!obs)
        obs = GetModuleHandleW(L"graphics-hook.dll");
    if (!obs)
        return;

    if (!g_Hooked.load())
        TryInstall();
    else
        RefreshCapturePointers(obs);
}

void Shutdown() noexcept
{
    HMODULE obs = GetModuleHandleW(L"graphics-hook64.dll");
    if (!obs)
        obs = GetModuleHandleW(L"graphics-hook.dll");

    // The slots were written through VirtualProtect, so putting the original
    // pointer back needs the same treatment. Without it the restore faults on a
    // read-only page and OBS keeps calling HkCapture2 in an unloaded image,
    // which crashes the game some frames after unload rather than at unload.
    for (int i = 0; i < g_PatchedCount; ++i) {
        void** slot = g_PatchedSlots[i];
        if (slot && g_PatchedOrigFn[i]) {
            __try {
                if (*slot == g_PatchedHookFn[i]) {
                    DWORD oldProtect = 0;
                    if (VirtualProtect(
                            slot,
                            sizeof(void*),
                            PAGE_READWRITE,
                            &oldProtect)) {
                        *slot = g_PatchedOrigFn[i];
                        VirtualProtect(
                            slot,
                            sizeof(void*),
                            oldProtect,
                            &oldProtect);
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
        g_PatchedSlots[i] = nullptr;
        g_PatchedOrigFn[i] = nullptr;
        g_PatchedHookFn[i] = nullptr;
    }
    g_PatchedCount = 0;

    for (int i = 0; i < g_CaptureHookCount; ++i) {
        if (g_CaptureHooks[i].minhook && g_CaptureHooks[i].target) {
            MH_DisableHook(g_CaptureHooks[i].target);
            MH_RemoveHook(g_CaptureHooks[i].target);
        }
        g_CaptureHooks[i] = {};
    }
    g_CaptureHookCount = 0;
    g_FoundCount = 0;
    g_HookTarget = nullptr;
    g_OrigCapture2 = nullptr;
    g_ResolvedCapture = nullptr;
    g_Hooked.store(false);
    g_DrawPending.store(false);
    g_InsidePresent.store(false);
    g_SeenNestedCapture.store(false);
    g_StickyStreamProof.store(false);
    g_LocalDrawOk.store(false);
    g_DeferThisPresent.store(false);
    g_ServedThisPresent.store(false);
    g_CaptureCalls.store(0);
    g_DrawCalls.store(0);
    g_PresentDepth.store(0);
    (void)obs;
}

} // namespace ObsBypass
