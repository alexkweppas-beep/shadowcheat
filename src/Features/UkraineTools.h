#pragma once

namespace UkraineTools
{
    void Initialize() noexcept;
    void Update() noexcept;
    void RenderUI();
    void OpenUiTab(int tab) noexcept;
    void RenderWidgets();

    // True only after V8 probe confirms Ukraine GTA.
    bool IsUkraineServer() noexcept;

    // True while CEF dump capture/drain is active — skip heavy Present work.
    bool IsCefDumpBusy() noexcept;

    void QueueCommand(
        const char* command,
        const char* label = nullptr,
        bool silent = false) noexcept;
}
