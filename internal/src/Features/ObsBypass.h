#pragma once

namespace ObsBypass {

// Toggle: when on and OBS Game Capture is hooked, overlays are drawn
// AFTER OBS copies the backbuffer — stream sees a clean frame, local
// monitor still sees ESP/menu.
bool IsEnabled() noexcept;
void SetEnabled(bool enabled) noexcept;

bool IsHooked() noexcept;
bool IsCaptureActive() noexcept;
const char* StatusText() noexcept;
const char* StatusDetail() noexcept;

// Bracket the Present call so capture knows it is nested inside us.
void BeginPresent() noexcept;
void EndPresent() noexcept;

// Call from Present after ImGui::Render().
// Returns true if draw was deferred to the OBS capture hook (stream-proof).
// While sticky stream-proof is armed, this MUST skip pre-Present paints.
bool TryDeferOverlayDraw() noexcept;

// Clear defer flags after Present. Never paints — pre-Present force-draws
// were leaking overlays into Game Capture.
void DiscardDeferredOverlayDraw() noexcept;

void Tick() noexcept;
void Shutdown() noexcept;

} // namespace ObsBypass
