#pragma once

namespace FiveRpFarmBot
{
    void Initialize() noexcept;
    void Update() noexcept;
    void RenderUI();

    bool IsFiveRpServer() noexcept;

    void QueueCommand(
        const char* command,
        const char* label = nullptr,
        bool silent = false) noexcept;
}
