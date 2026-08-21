#pragma once

namespace QuantTools
{
    void Initialize() noexcept;
    void Update() noexcept;
    void RenderUI();
    void RenderFirmaUI();
    void RenderWidgets();

    // True while CEF dump capture/drain is active — skip heavy Present work.
    bool IsCefDumpBusy() noexcept;

    // True only after V8 probe confirms Quant RP (PM_PAUSE_HDR / quant5).
    bool IsQuantServer() noexcept;

    // Queue a command into the injected Quant Tools JS bridge.
    // silent=true: no side toast (used by Misc sync / sliders).
    void QueueCommand(
        const char* command,
        const char* label = nullptr,
        bool silent = false) noexcept;
}
