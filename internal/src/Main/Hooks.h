#pragma once

// =====================================================================
//  Hooks.h
//  D3D11 IDXGISwapChain::Present hook + WndProc hook.
//
//  Strategy (ReShade-style):
//    1. Scan dxgi.dll for canonical IDXGISwapChain / IDXGISwapChain1 vtables.
//    2. Vtable-patch Present (slot 8) AND Present1 (slot 22).
//       — modifies data pointers, NOT code. No CFG issues, no integrity checks.
//    3. CreateDXGIFactory export hooks for NEW factories only.
//    4. Inline detour as last-resort fallback only.
// =====================================================================

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include "imgui.h"

struct ImGui_ImplDX11_Data; // forward

namespace hooks
{
    // ----------------- state -----------------
    extern ID3D11Device*           g_Device;
    extern ID3D11DeviceContext*    g_Context;
    extern IDXGISwapChain*         g_SwapChain;
    extern void*                   g_OriginalPresent;
    extern void*                   g_OriginalPresent1;

    extern bool g_ImGuiInit;
    extern WNDPROC g_OriginalWndProc;
    extern HWND  g_Hwnd;

    typedef void (*LogFn)(const char*);
    extern LogFn g_LogFn;
#if defined(NDEBUG) && !defined(RAGEMP_DIAGNOSTICS)
    inline void HLog(const char*) {}
#else
    inline void HLog(const char* msg) { if (g_LogFn) g_LogFn(msg); }
#endif

    // ----------------- API -----------------

    // Installs Present hook.  Returns true on success.
    bool Initialize();
    // Tell InjectCore the payload is live (safe to call repeatedly).
    void NotifyInjectReady() noexcept;
    void RequestUnload();
    bool IsUnloadRequested();
    void Shutdown();

    // The Present hook function — called every frame by the game.
    extern "C" HRESULT __stdcall hkPresent(
        IDXGISwapChain* pSwap,
        UINT SyncInterval,
        UINT Flags);

    // The Present1 hook function — called by games using IDXGISwapChain1.
    extern "C" HRESULT __stdcall hkPresent1(
        IDXGISwapChain1* pSwap,
        UINT SyncInterval,
        UINT PresentFlags,
        const DXGI_PRESENT_PARAMETERS* pPresentParameters);

    // ResizeBuffers refuses to run while anything still references a back
    // buffer, and we hold a render target view on one between frames. These
    // two hooks drop it first: without them a resolution change or a
    // fullscreen switch kills the game renderer.
    extern "C" HRESULT __stdcall hkResizeBuffers(
        IDXGISwapChain* pSwap,
        UINT BufferCount,
        UINT Width,
        UINT Height,
        DXGI_FORMAT NewFormat,
        UINT SwapChainFlags);

    extern "C" HRESULT __stdcall hkSetFullscreenState(
        IDXGISwapChain* pSwap,
        BOOL Fullscreen,
        IDXGIOutput* pTarget);

    // ImGui WndProc handler.
    extern "C" LRESULT __stdcall hkWndProc(
        HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // Initializes ImGui/Win32 DX11 backends after device/swapchain swap are known.
    void InitImGui(IDXGISwapChain* swap);

    // Called from hkPresent to render the menu.
    void RenderMenu();
}

