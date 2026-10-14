#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

 
// NvidiaBypass — External transparent overlay for Streamproof
//
// Uses a completely separate ID3D11Device + IDXGISwapChain in a background
// thread. The background device has ZERO relation to the game's DXGI factory,
// so there is no lock contention with hkPresent at all.
// The overlay window is owned by the same background thread, so DXGI's
// internal SendMessage calls during CreateSwapChain are handled immediately.
 
namespace NvidiaBypass {

bool IsAvailable() noexcept;
bool IsEnabled()   noexcept;

// SetEnabled is called from the render thread (ImGui toggle).
// Font atlas pixels are captured here before the background thread starts.
void SetEnabled(bool enabled) noexcept;
const char* StatusText() noexcept;

bool IsOverlaySwapChain(IDXGISwapChain* sc) noexcept;

// Called from hkPresent (render thread) — updates overlay window position.
// No D3D resource creation here.
void Tick(ID3D11Device* device, HWND gameHwnd) noexcept;

// Called from hkPresent after ImGui::Render().
// Serialises ImDrawData into a shared buffer for the background renderer.
// Returns true → caller should skip drawing ImGui to the game backbuffer.

} // forward-decl ImDrawData outside the namespace
struct ImDrawData;
namespace NvidiaBypass {

bool RenderAndPresent(ImDrawData* data) noexcept;

void Shutdown() noexcept;

} // namespace NvidiaBypass
