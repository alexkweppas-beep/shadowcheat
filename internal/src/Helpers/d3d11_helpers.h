#pragma once

// =====================================================================
//  d3d11_helpers.h — DXGI Present hook helpers
//
//  ReShade pattern (correct for injected DLL):
//    - NEVER patch the canonical shared factory vtable at init.
//    - Hook CreateDXGIFactory1 export; patch only factories returned
//      to the game AFTER our hook is installed.
//    - Present resolve: scan dxgi.dll .rdata only (no D3D device, no heap walk).
// =====================================================================

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

namespace d3d
{
    constexpr UINT kPresentIndex  = 8;
    constexpr UINT kPresent1Index = 22;  // IDXGISwapChain1::Present1
    constexpr UINT kSetFullscreenStateIndex = 10;
    constexpr UINT kResizeBuffersIndex = 13;
    constexpr UINT kVTableSize    = 20;
    constexpr UINT kVTable1Size   = 29;  // IDXGISwapChain1 has 29 entries

    constexpr UINT kFactoryCreateSwapChainIndex         = 10;
    constexpr UINT kFactory2CreateSwapChainForHwndIndex = 15;

    using OnSwapChainFn = void(*)(IDXGISwapChain* swap);

    // Export-only factory hooks (no canonical vtable patch at init).
    bool InstallDxgiFactoryExportHooks(OnSwapChainFn onSwapChain);
    bool IsDxgiHooksInstalled();

    // ReShade primary path: hook D3D11CreateDeviceAndSwapChain export in d3d11.dll.
    // When the game creates its device+swapchain, we intercept and get the REAL
    // swapchain pointer, then patch its Present/Present1 vtable slots directly.
    bool InstallD3D11ExportHook(OnSwapChainFn onSwapChain);
    bool IsD3D11HookInstalled();
    void ShutdownHooks();

    bool GetPresentFromSwapChain(IDXGISwapChain* swap, void** outPresent);

    // Authoritative Present resolver: create a temporary D3D11 device + swapchain
    // (HARDWARE driver, fast — ~10 ms, not WARP which stalls under EAC).
    // The dummy swapchain's vtable[8] is guaranteed to be the real IDXGISwapChain::Present.
    // Used to resolve the Present function pointer for inline hooking when injected
    // AFTER the game already created its swapchain (post-launch manual map).
    IDXGISwapChain* GetDummySwapChain(void** outVTable);
    void ReleaseDummy();

    // Find both IDXGISwapChain (Present, slot 8) and IDXGISwapChain1 (Present1, slot 22)
    // canonical vtables inside dxgi.dll.
    bool GetAllDxgiPresentFunctions(
        void* outPresentArray[], int* outPresentCount,
        void* outPresent1Array[], int* outPresent1Count,
        int maxCount);

    // ReShade-style vtable slot patching — modifies data, not code.
    // Returns true if the slot was successfully patched.
    // *outOriginal receives the original function pointer from the slot.
    bool PatchSwapChainVtableSlot(void** vtable, UINT slotIndex,
                                  void* hookFn, void** outOriginal);
    bool RestoreSwapChainVtableSlot(void** vtable, UINT slotIndex,
                                    void* hookFn, void* original);

    // Install an inline detour using an unconditional 14-byte jump.
    bool InstallInlineDetour(void* target, void* hook, void** outTrampoline);
}
