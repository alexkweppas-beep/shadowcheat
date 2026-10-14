#pragma once

#include "../imgui/imgui.h"

 
// Stream-sensitive overlay layer.
//
// ESP / glow / menu draw lists go through Overlay::GetDrawList().
// ObsBypass draws ImGui AFTER OBS Game Capture copies the backbuffer so the
// stream stays clean while the local monitor still shows overlays.
 
namespace Overlay {

inline bool& StreamProofEnabled() noexcept
{
    static bool enabled = false;
    return enabled;
}

inline bool IsStreamProofEnabled() noexcept
{
    return StreamProofEnabled();
}

inline void SetStreamProofEnabled(bool enabled) noexcept
{
    StreamProofEnabled() = enabled;
}

inline bool IsCaptureProtected() noexcept
{
    return IsStreamProofEnabled();
}

inline ImDrawList* GetDrawList() noexcept
{
    return ImGui::GetBackgroundDrawList();
}

} // namespace Overlay
