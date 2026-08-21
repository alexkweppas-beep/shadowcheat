#include "d3d11_helpers.h"

#include <psapi.h>
#include <dxgi1_2.h>
#include <MinHook.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "psapi.lib")

namespace d3d
{
    static ID3D11Device*        g_dummyDevice  = nullptr;
    static ID3D11DeviceContext* g_dummyContext = nullptr;
    static IDXGISwapChain*      g_dummySwap    = nullptr;

    static bool IsReadableProtect(DWORD protect)
    {
        if (protect & PAGE_GUARD || protect & PAGE_NOACCESS)
            return false;
        return (protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                           PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) != 0;
    }

    static bool PtrInsideModule(const void* ptr, const MODULEINFO& mi)
    {
        const BYTE* p = static_cast<const BYTE*>(ptr);
        const BYTE* base = static_cast<const BYTE*>(mi.lpBaseOfDll);
        return p >= base && p < base + mi.SizeOfImage;
    }

    // No log sink in this build. The call sites stay because they mark the
    // places where a hook can fail, but nothing leaves the process.
    static void HelperLog(const char* msg)
    {
        (void)msg;
    }

    static OnSwapChainFn g_onSwapChain    = nullptr;
    static bool          g_hooksInstalled = false;

    static thread_local bool g_inDxgiRuntime = false;

    static void** g_patchedFactoryVtables[16]{};
    static int    g_patchedFactoryCount = 0;

    typedef HRESULT (STDMETHODCALLTYPE* PFN_CreateSwapChain)(
        IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
    typedef HRESULT (STDMETHODCALLTYPE* PFN_CreateSwapChainForHwnd)(
        IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
    typedef HRESULT (WINAPI* PFN_CreateDXGIFactory1)(REFIID, void**);
    typedef HRESULT (WINAPI* PFN_CreateDXGIFactory2)(UINT, REFIID, void**);

    static PFN_CreateSwapChain         g_origCreateSwapChain        = nullptr;
    static PFN_CreateSwapChainForHwnd  g_origCreateSwapChainForHwnd = nullptr;
    static PFN_CreateDXGIFactory1      g_trampCreateDXGIFactory1     = nullptr;
    static PFN_CreateDXGIFactory2      g_trampCreateDXGIFactory2     = nullptr;
    static bool                        g_minHookInitialized           = false;

    // ReShade primary path: hook D3D11CreateDeviceAndSwapChain export.
    // When the game creates its device+swapchain, we intercept, let the
    // original create it, then read the REAL swapchain's Present/Present1
    // vtable slots and notify the callback — exactly like ReShade's d3d11.cpp.
    typedef HRESULT (WINAPI* PFN_D3D11CreateDeviceAndSwapChain)(
        IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
        const D3D_FEATURE_LEVEL*, UINT, UINT,
        const DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**,
        ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
    static PFN_D3D11CreateDeviceAndSwapChain g_trampD3D11CreateDeviceAndSwapChain = nullptr;

    static void NotifySwapChain(IDXGISwapChain* swap);

    static bool InstallMinHook(void* target, void* detour, void** original)
    {
        if (!target || !detour || !original)
            return false;

        if (!g_minHookInitialized) {
            const MH_STATUS initStatus = MH_Initialize();
            if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED) {
                HelperLog("MinHook: MH_Initialize failed");
                return false;
            }
            g_minHookInitialized = true;
        }

        const MH_STATUS createStatus = MH_CreateHook(target, detour, original);
        if (createStatus != MH_OK && createStatus != MH_ERROR_ALREADY_CREATED) {
            HelperLog("MinHook: MH_CreateHook failed");
            return false;
        }

        const MH_STATUS enableStatus = MH_EnableHook(target);
        if (enableStatus != MH_OK && enableStatus != MH_ERROR_ENABLED) {
            HelperLog("MinHook: MH_EnableHook failed");
            return false;
        }

        return true;
    }

    static HRESULT WINAPI hkD3D11CreateDeviceAndSwapChain(
        IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
        UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
        UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
        IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice,
        D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext)
    {
        if (!g_trampD3D11CreateDeviceAndSwapChain)
            return E_FAIL;

        // ReShade: skip hooking internal DXGI device creation (recursive calls).
        if (g_inDxgiRuntime)
            return g_trampD3D11CreateDeviceAndSwapChain(
                pAdapter, DriverType, Software, Flags, pFeatureLevels,
                FeatureLevels, SDKVersion, pSwapChainDesc, ppSwapChain,
                ppDevice, pFeatureLevel, ppImmediateContext);

        g_inDxgiRuntime = true;
        const HRESULT hr = g_trampD3D11CreateDeviceAndSwapChain(
            pAdapter, DriverType, Software, Flags, pFeatureLevels,
            FeatureLevels, SDKVersion, pSwapChainDesc, ppSwapChain,
            ppDevice, pFeatureLevel, ppImmediateContext);
        g_inDxgiRuntime = false;

        if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain && pSwapChainDesc) {
            char buf[128];
            wsprintfA(buf, "D3D11: CreateDeviceAndSwapChain OK swap=%p", *ppSwapChain);
            HelperLog(buf);
            NotifySwapChain(*ppSwapChain);
        }
        return hr;
    }

    static bool WasFactoryVtablePatched(void** vt)
    {
        for (int i = 0; i < g_patchedFactoryCount; ++i)
            if (g_patchedFactoryVtables[i] == vt)
                return true;
        return false;
    }

    static void RememberFactoryVtable(void** vt)
    {
        if (!vt || WasFactoryVtablePatched(vt))
            return;
        if (g_patchedFactoryCount >= 16)
            return;
        g_patchedFactoryVtables[g_patchedFactoryCount++] = vt;
    }

    static bool PatchVtableSlot(void** slot, void* hook)
    {
        if (!slot || !hook)
            return false;

        __try {
            if (*slot == hook)
                return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }

        DWORD oldProt = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProt))
            return false;
        *slot = hook;
        VirtualProtect(slot, sizeof(void*), oldProt, &oldProt);
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
        return true;
    }

    static void NotifySwapChain(IDXGISwapChain* swap)
    {
        if (!swap || !g_onSwapChain)
            return;
        __try {
            g_onSwapChain(swap);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            HelperLog("DXGI: onSwapChain callback SEH");
        }
    }

    static HRESULT STDMETHODCALLTYPE hkCreateSwapChain(
        IDXGIFactory* factory, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc,
        IDXGISwapChain** ppSwapChain)
    {
        if (!g_origCreateSwapChain)
            return E_FAIL;
        if (g_inDxgiRuntime)
            return g_origCreateSwapChain(factory, device, desc, ppSwapChain);

        g_inDxgiRuntime = true;
        const HRESULT hr = g_origCreateSwapChain(factory, device, desc, ppSwapChain);
        g_inDxgiRuntime = false;

        if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain)
            NotifySwapChain(*ppSwapChain);
        return hr;
    }

    static HRESULT STDMETHODCALLTYPE hkCreateSwapChainForHwnd(
        IDXGIFactory2* factory, IUnknown* device, HWND hwnd,
        const DXGI_SWAP_CHAIN_DESC1* desc,
        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc,
        IDXGIOutput* restrictToOutput, IDXGISwapChain1** ppSwapChain)
    {
        if (!g_origCreateSwapChainForHwnd)
            return E_FAIL;
        if (g_inDxgiRuntime)
            return g_origCreateSwapChainForHwnd(
                factory, device, hwnd, desc, fullscreenDesc,
                restrictToOutput, ppSwapChain);

        g_inDxgiRuntime = true;
        const HRESULT hr = g_origCreateSwapChainForHwnd(
            factory, device, hwnd, desc, fullscreenDesc,
            restrictToOutput, ppSwapChain);
        g_inDxgiRuntime = false;

        if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain)
            NotifySwapChain(static_cast<IDXGISwapChain*>(*ppSwapChain));
        return hr;
    }

    // ReShade: hook vtable of the factory instance returned by CreateDXGIFactory*.
    // Do NOT call this on a throw-away factory at init — that patches the global
    // vtable and breaks the game's existing DXGI objects (black screen).
    static void PatchFactoryInstance(IDXGIFactory* factory)
    {
        if (!factory)
            return;

        void** vt = nullptr;
        __try {
            vt = *reinterpret_cast<void***>(factory);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return;
        }
        if (!vt)
            return;

        const bool vtAlready = WasFactoryVtablePatched(vt);

        if (!vtAlready) {
            __try {
                if (!g_origCreateSwapChain)
                    g_origCreateSwapChain = reinterpret_cast<PFN_CreateSwapChain>(
                        vt[kFactoryCreateSwapChainIndex]);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return;
            }

            if (g_origCreateSwapChain &&
                PatchVtableSlot(
                    &vt[kFactoryCreateSwapChainIndex],
                    reinterpret_cast<void*>(&hkCreateSwapChain))) {
                RememberFactoryVtable(vt);
            }
        }

        IDXGIFactory2* factory2 = nullptr;
        if (FAILED(factory->QueryInterface(
                __uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory2))))
            return;

        void** vt2 = nullptr;
        __try {
            vt2 = *reinterpret_cast<void***>(factory2);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            factory2->Release();
            return;
        }

        if (vt2 && !WasFactoryVtablePatched(vt2)) {
            __try {
                if (!g_origCreateSwapChainForHwnd)
                    g_origCreateSwapChainForHwnd =
                        reinterpret_cast<PFN_CreateSwapChainForHwnd>(
                            vt2[kFactory2CreateSwapChainForHwndIndex]);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                factory2->Release();
                return;
            }

            if (g_origCreateSwapChainForHwnd &&
                PatchVtableSlot(
                    &vt2[kFactory2CreateSwapChainForHwndIndex],
                    reinterpret_cast<void*>(&hkCreateSwapChainForHwnd))) {
                RememberFactoryVtable(vt2);
                char buf[96];
                wsprintfA(buf, "DXGI: patched factory2 vt=%p (slot 15)", vt2);
                HelperLog(buf);
            }
        }

        factory2->Release();

        if (!vtAlready) {
            char buf[96];
            wsprintfA(buf, "DXGI: patched factory vt=%p (slot 10)", vt);
            HelperLog(buf);
        }
    }

    // To prevent splitting an instruction, we use a simple length guesser.
    // We need at least 14 bytes for the absolute JMP.
    static size_t GetDetourLength(BYTE* p) {
        size_t len = 0;
        while (len < 14) {
            BYTE b = p[len];
            if (b == 0x48 || b == 0x4C || b == 0x49) {
                BYTE op = p[len+1];
                if (op == 0x89) { // mov [rsp+xx], reg
                    if ((p[len+2] >> 6) == 1) len += 5;
                    else if ((p[len+2] >> 6) == 2) len += 8;
                    else len += 4;
                } else if (op == 0x8B) { // mov reg, [rsp+xx]
                     if ((p[len+2] >> 6) == 1) len += 5;
                     else len += 4;
                } else if (op == 0x83) { // sub rsp, xx
                    len += 4;
                } else {
                    len += 3;
                }
            } else if (b == 0x40 || b == 0x41) {
                BYTE op = p[len+1];
                if (op >= 0x50 && op <= 0x57) len += 2;
                else len += 3;
            } else if (b >= 0x50 && b <= 0x57) {
                len += 1;
            } else if (b == 0x0F) {
                len += 3;
            } else if (b == 0xE9) {
                len += 5;
            } else {
                len += 2;
            }
        }
        return len;
    }

    bool InstallInlineDetour(void* target, void* hook, void** outTrampoline)
    {
        if (!target || !hook || !outTrampoline)
            return false;

        BYTE* p = static_cast<BYTE*>(target);
        size_t detourSize = 15; // default safe fallback

        __try {
            if (p[0] == 0xFF && p[1] == 0x25) {
                const UINT64 existing = *reinterpret_cast<UINT64*>(p + 6);
                if (existing == static_cast<UINT64>(reinterpret_cast<ULONG_PTR>(hook)))
                    return *outTrampoline != nullptr;
            }
            detourSize = GetDetourLength(p);
            if (detourSize < 14 || detourSize > 32) detourSize = 15; // sanity bounds
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }

        BYTE stolen[32]{};
        __try {
            for (size_t i = 0; i < detourSize; ++i)
                stolen[i] = p[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }

        BYTE* tramp = static_cast<BYTE*>(VirtualAlloc(
            nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!tramp)
            return false;

        for (size_t i = 0; i < detourSize; ++i)
            tramp[i] = stolen[i];

        const size_t off = detourSize;
        tramp[off + 0]  = 0x48;
        tramp[off + 1]  = 0xB8;
        *reinterpret_cast<UINT64*>(tramp + off + 2) =
            static_cast<UINT64>(reinterpret_cast<ULONG_PTR>(target) + detourSize);
        tramp[off + 10] = 0xFF;
        tramp[off + 11] = 0xE0;

        DWORD trampProt = 0;
        if (!VirtualProtect(tramp, 0x1000, PAGE_EXECUTE_READ, &trampProt)) {
            VirtualFree(tramp, 0, MEM_RELEASE);
            return false;
        }
        FlushInstructionCache(GetCurrentProcess(), tramp, off + 12);

        DWORD oldProt = 0;
        if (!VirtualProtect(target, detourSize, PAGE_EXECUTE_READWRITE, &oldProt)) {
            VirtualFree(tramp, 0, MEM_RELEASE);
            return false;
        }

        bool writeSuccess = false;
        __try {
            BYTE* hookBytes = static_cast<BYTE*>(target);
            hookBytes[0] = 0xFF;
            hookBytes[1] = 0x25;
            *reinterpret_cast<UINT32*>(hookBytes + 2) = 0;
            *reinterpret_cast<UINT64*>(hookBytes + 6) =
                static_cast<UINT64>(reinterpret_cast<ULONG_PTR>(hook));
            // Fill remainder with NOPs
            for (size_t i = 14; i < detourSize; ++i)
                hookBytes[i] = 0x90;
            writeSuccess = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            HelperLog("DXGI: CRASH writing hook bytes (anticheat protection?)");
        }

        VirtualProtect(target, detourSize, oldProt, &oldProt);
        
        if (!writeSuccess) {
            VirtualFree(tramp, 0, MEM_RELEASE);
            return false;
        }
        FlushInstructionCache(GetCurrentProcess(), target, detourSize);

        *outTrampoline = tramp;
        return true;
    }

    static HRESULT WINAPI hkCreateDXGIFactory1(REFIID riid, void** ppFactory)
    {
        if (!g_trampCreateDXGIFactory1)
            return E_FAIL;

        const HRESULT hr = g_trampCreateDXGIFactory1(riid, ppFactory);
        if (SUCCEEDED(hr) && ppFactory && *ppFactory && !g_inDxgiRuntime)
            PatchFactoryInstance(static_cast<IDXGIFactory*>(*ppFactory));
        return hr;
    }

    static HRESULT WINAPI hkCreateDXGIFactory2(UINT flags, REFIID riid, void** ppFactory)
    {
        if (!g_trampCreateDXGIFactory2)
            return hkCreateDXGIFactory1(riid, ppFactory);

        const HRESULT hr = g_trampCreateDXGIFactory2(flags, riid, ppFactory);
        if (SUCCEEDED(hr) && ppFactory && *ppFactory && !g_inDxgiRuntime)
            PatchFactoryInstance(static_cast<IDXGIFactory*>(*ppFactory));
        return hr;
    }

    bool GetAllDxgiPresentFunctions(
        void* outPresentArray[], int* outPresentCount,
        void* outPresent1Array[], int* outPresent1Count,
        int maxCount)
    {
        if (!outPresentArray || !outPresentCount || !outPresent1Array || !outPresent1Count) return false;
        
        *outPresentCount = 0;
        *outPresent1Count = 0;

        HMODULE hDxgi = GetModuleHandleW(L"dxgi.dll");
        if (!hDxgi) return false;
        MODULEINFO mi{};
        GetModuleInformation(GetCurrentProcess(), hDxgi, &mi, sizeof(mi));

        BYTE* base = (BYTE*)mi.lpBaseOfDll;
        BYTE* end = base + mi.SizeOfImage;
        MEMORY_BASIC_INFORMATION mbi{};

        for (BYTE* addr = base; addr < end; ) {
            if (!VirtualQuery(addr, &mbi, sizeof(mbi))) break;
            if (mbi.State == MEM_COMMIT && IsReadableProtect(mbi.Protect)) {
                for (BYTE* p = (BYTE*)mbi.BaseAddress; p + 18*8 <= (BYTE*)mbi.BaseAddress + mbi.RegionSize; p += 8) {
                    __try {
                        void** vt = (void**)p;
                        bool valid = true;
                        for (int i = 0; i < 18; ++i) {
                            if (!PtrInsideModule(vt[i], mi)) { valid = false; break; }
                        }
                        if (valid) {
                            void* present = vt[kPresentIndex];
                            if (PtrInsideModule(present, mi)) {
                                bool exists = false;
                                for (int i = 0; i < *outPresentCount; ++i) {
                                    if (outPresentArray[i] == present) { exists = true; break; }
                                }
                                if (!exists && *outPresentCount < maxCount) {
                                    outPresentArray[(*outPresentCount)++] = present;
                                }
                            }
                            
                            bool valid23 = true;
                            if (p + 23*8 <= (BYTE*)mbi.BaseAddress + mbi.RegionSize) {
                                for (int i = 18; i < 23; ++i) {
                                    if (!PtrInsideModule(vt[i], mi)) { valid23 = false; break; }
                                }
                            } else valid23 = false;

                            if (valid23) {
                                void* present1 = vt[kPresent1Index];
                                if (PtrInsideModule(present1, mi) && present1 != present) {
                                    bool exists = false;
                                    for (int i = 0; i < *outPresent1Count; ++i) {
                                        if (outPresent1Array[i] == present1) { exists = true; break; }
                                    }
                                    if (!exists && *outPresent1Count < maxCount) {
                                        outPresent1Array[(*outPresent1Count)++] = present1;
                                    }
                                }
                            }
                        }
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                }
            }
            addr = (BYTE*)mbi.BaseAddress + mbi.RegionSize;
        }

        char buf[128];
        wsprintfA(buf, "DXGI: found %d Present, %d Present1 functions in dxgi.dll", *outPresentCount, *outPresent1Count);
        HelperLog(buf);

        return (*outPresentCount > 0 || *outPresent1Count > 0);
    }

    // Authoritative IDXGISwapChain::Present resolver.
    // Creates a real device+swapchain via D3D11CreateDeviceAndSwapChain and
    // reads vtable[8] (Present) and vtable[22] (Present1). Unlike dxgi.dll
    // scanning, this cannot match the wrong interface — the object we create
    // IS an IDXGISwapChain, so its vtable is the real one.
    //
    // HARDWARE driver is used (not WARP): WARP triggered multi-second stalls
    // under EAC because the software rasterizer initialised inside DllMain.
    // HARDWARE just opens the existing GPU driver handle — fast and matches
    // what the game itself uses, so vtable[8] resolves to the same function.
    IDXGISwapChain* GetDummySwapChain(void** outVTable)
    {
        if (g_dummySwap) {
            void** ppv = *reinterpret_cast<void***>(g_dummySwap);
            if (outVTable) {
                for (UINT i = 0; i < kVTable1Size; ++i)
                    outVTable[i] = ppv[i];
            }
            return g_dummySwap;
        }

        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount       = 1;
        sd.BufferDesc.Width  = 1;
        sd.BufferDesc.Height = 1;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator   = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow      = GetDesktopWindow();
        sd.SampleDesc.Count  = 1;
        sd.Windowed          = TRUE;
        sd.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

        const D3D_FEATURE_LEVEL flTry[] = {
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };
        D3D_FEATURE_LEVEL actualFl{};

        // Try HARDWARE first (fast, matches game's driver).
        HelperLog("GetDummySwapChain: HARDWARE device");
        HRESULT hr = E_FAIL;
        __try {
            hr = D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                flTry, _countof(flTry), D3D11_SDK_VERSION,
                &sd, &g_dummySwap, &g_dummyDevice, &actualFl, &g_dummyContext);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            HelperLog("GetDummySwapChain: HARDWARE SEH");
        }

        // Fallback to WARP only if HARDWARE failed.
        if (FAILED(hr) || !g_dummySwap) {
            HelperLog("GetDummySwapChain: HARDWARE failed, trying WARP");
            sd.OutputWindow = nullptr;  // WARP doesn't need a real HWND
            __try {
                hr = D3D11CreateDeviceAndSwapChain(
                    nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                    flTry, _countof(flTry), D3D11_SDK_VERSION,
                    &sd, &g_dummySwap, &g_dummyDevice, &actualFl, &g_dummyContext);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                HelperLog("GetDummySwapChain: WARP SEH");
                return nullptr;
            }
        }

        if (FAILED(hr) || !g_dummySwap) {
            HelperLog("GetDummySwapChain: FAILED");
            return nullptr;
        }
        HelperLog("GetDummySwapChain: OK");

        void** ppv = *reinterpret_cast<void***>(g_dummySwap);
        if (outVTable) {
            for (UINT i = 0; i < kVTable1Size; ++i)
                outVTable[i] = ppv[i];
        }
        return g_dummySwap;
    }

    void ReleaseDummy()
    {
        if (g_dummySwap)    { g_dummySwap->Release();    g_dummySwap    = nullptr; }
        if (g_dummyContext) { g_dummyContext->Release(); g_dummyContext = nullptr; }
        if (g_dummyDevice)  { g_dummyDevice->Release();  g_dummyDevice  = nullptr; }
    }

    bool GetPresentFromSwapChain(IDXGISwapChain* swap, void** outPresent)
    {
        if (!swap || !outPresent)
            return false;
        __try {
            void** vt = *reinterpret_cast<void***>(swap);
            if (!vt)
                return false;
            *outPresent = vt[kPresentIndex];
            return *outPresent != nullptr;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool InstallDxgiFactoryExportHooks(OnSwapChainFn onSwapChain)
    {
        if (g_hooksInstalled)
            return true;

        g_onSwapChain = onSwapChain;

        bool anyHooked = false;

        // --- ReShade primary path: hook D3D11CreateDeviceAndSwapChain export ---
        // This is how ReShade intercepts swapchain creation (d3d11/d3d11.cpp).
        // GTA/RAGEMP creates its device+swapchain via this export, bypassing
        // IDXGIFactory::CreateSwapChain entirely.
        HMODULE hD3D11 = GetModuleHandleW(L"d3d11.dll");
        if (hD3D11) {
            void* exp = reinterpret_cast<void*>(
                GetProcAddress(hD3D11, "D3D11CreateDeviceAndSwapChain"));
            if (exp && InstallMinHook(
                    exp,
                    reinterpret_cast<void*>(&hkD3D11CreateDeviceAndSwapChain),
                    reinterpret_cast<void**>(&g_trampD3D11CreateDeviceAndSwapChain))) {
                HelperLog("D3D11: hooked export D3D11CreateDeviceAndSwapChain");
                anyHooked = true;
            } else {
                HelperLog("D3D11: D3D11CreateDeviceAndSwapChain hook FAILED");
            }
        }

        HMODULE hDxgi = GetModuleHandleW(L"dxgi.dll");
        if (!hDxgi) {
            HelperLog("DXGI: dxgi.dll missing");
        } else {
            HelperLog("DXGI: installing export hooks");

            const bool hooked1 = [&]() -> bool {
                void* exportFn = reinterpret_cast<void*>(
                    GetProcAddress(hDxgi, "CreateDXGIFactory1"));
                if (!exportFn)
                    return false;
                if (!InstallMinHook(
                        exportFn,
                        reinterpret_cast<void*>(&hkCreateDXGIFactory1),
                        reinterpret_cast<void**>(&g_trampCreateDXGIFactory1)))
                    return false;
                HelperLog("DXGI: hooked export CreateDXGIFactory1");
                return true;
            }();

            const bool hooked2 = [&]() -> bool {
                void* exportFn = reinterpret_cast<void*>(
                    GetProcAddress(hDxgi, "CreateDXGIFactory2"));
                if (!exportFn)
                    return false;
                if (!InstallMinHook(
                        exportFn,
                        reinterpret_cast<void*>(&hkCreateDXGIFactory2),
                        reinterpret_cast<void**>(&g_trampCreateDXGIFactory2)))
                    return false;
                HelperLog("DXGI: hooked export CreateDXGIFactory2");
                return true;
            }();

            if (hooked1 || hooked2)
                anyHooked = true;
        }

        if (!anyHooked) {
            HelperLog("DXGI/D3D11: export hook install FAILED");
            return false;
        }

        g_hooksInstalled = true;
        return true;
    }

    bool IsDxgiHooksInstalled()
    {
        return g_hooksInstalled;
    }

    void ShutdownHooks()
    {
        g_onSwapChain = nullptr;

        if (g_minHookInitialized) {
            MH_DisableHook(MH_ALL_HOOKS);
            MH_RemoveHook(MH_ALL_HOOKS);
            MH_Uninitialize();
            g_minHookInitialized = false;
        }

        for (int i = 0; i < g_patchedFactoryCount; ++i) {
            void** vt = g_patchedFactoryVtables[i];
            if (!vt)
                continue;
            RestoreSwapChainVtableSlot(
                vt,
                kFactoryCreateSwapChainIndex,
                reinterpret_cast<void*>(&hkCreateSwapChain),
                reinterpret_cast<void*>(g_origCreateSwapChain));
            RestoreSwapChainVtableSlot(
                vt,
                kFactory2CreateSwapChainForHwndIndex,
                reinterpret_cast<void*>(&hkCreateSwapChainForHwnd),
                reinterpret_cast<void*>(g_origCreateSwapChainForHwnd));
        }

        g_patchedFactoryCount = 0;
        g_origCreateSwapChain = nullptr;
        g_origCreateSwapChainForHwnd = nullptr;
        g_trampCreateDXGIFactory1 = nullptr;
        g_trampCreateDXGIFactory2 = nullptr;
        g_trampD3D11CreateDeviceAndSwapChain = nullptr;
        g_hooksInstalled = false;
        ReleaseDummy();
    }

    // ----------------------------------------------------------------
    // ReShade-style vtable slot patching — modifies data, not code.
    // Safer than inline detours: no CFG issues, no code integrity checks.
    // ----------------------------------------------------------------
    bool PatchSwapChainVtableSlot(void** vtable, UINT slotIndex,
                                  void* hookFn, void** outOriginal)
    {
        if (!vtable || !hookFn || !outOriginal)
            return false;

        void** slot = &vtable[slotIndex];

        // Read current value.
        __try {
            *outOriginal = *slot;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            HelperLog("DXGI: vtable slot read SEH");
            return false;
        }

        // Already hooked?
        if (*outOriginal == hookFn) {
            HelperLog("DXGI: vtable slot already patched");
            return true;
        }

        // VirtualProtect → WriteProcessMemory → verify → restore.
        // A direct store into dxgi.dll's shared image-backed .rdata can still
        // raise STATUS_ACCESS_VIOLATION on some driver/anti-cheat stacks even
        // after VirtualProtect reports success. NtWriteVirtualMemory handles
        // the image COW transition and reports failure instead of terminating
        // GTA from an unhandled write fault.
        DWORD oldProt = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProt)) {
            HelperLog("DXGI: VirtualProtect vtable slot FAILED");
            return false;
        }
        SIZE_T written = 0;
        const BOOL writeOk = WriteProcessMemory(
            GetCurrentProcess(),
            slot,
            &hookFn,
            sizeof(hookFn),
            &written);
        VirtualProtect(slot, sizeof(void*), oldProt, &oldProt);
        if (!writeOk || written != sizeof(hookFn)) {
            HelperLog("DXGI: WriteProcessMemory vtable slot FAILED");
            return false;
        }

        void* installed = nullptr;
        SIZE_T read = 0;
        if (!ReadProcessMemory(
                GetCurrentProcess(),
                slot,
                &installed,
                sizeof(installed),
                &read) ||
            read != sizeof(installed) ||
            installed != hookFn) {
            HelperLog("DXGI: vtable slot verification FAILED");
            return false;
        }
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));

        char buf[128];
        wsprintfA(buf, "DXGI: patched vtable slot[%u] %p -> %p",
                  slotIndex, *outOriginal, hookFn);
        HelperLog(buf);
        return true;
    }

    bool RestoreSwapChainVtableSlot(void** vtable, UINT slotIndex,
                                    void* hookFn, void* original)
    {
        if (!vtable || !hookFn || !original)
            return false;

        void** slot = &vtable[slotIndex];
        __try {
            if (*slot != hookFn)
                return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }

        DWORD oldProt = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProt))
            return false;
        SIZE_T written = 0;
        const BOOL writeOk = WriteProcessMemory(
            GetCurrentProcess(),
            slot,
            &original,
            sizeof(original),
            &written);
        VirtualProtect(slot, sizeof(void*), oldProt, &oldProt);
        if (!writeOk || written != sizeof(original))
            return false;
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
        return true;
    }

    // End of d3d11_helpers
}

