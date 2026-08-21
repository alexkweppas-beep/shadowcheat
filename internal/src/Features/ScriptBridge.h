#pragma once

#include <cstdint>

namespace ScriptBridge
{
    // One page today. The enum stays because the shell routes pages by value
    // and the bridge is the thing that decides which one is showing.
    enum class Page : std::uint8_t
    {
        Scripts = 0
    };

    bool Initialize();
    void Update();
    void RenderUI();
    void RunDirect();
    void OpenPage(Page page) noexcept;
    Page CurrentPage() noexcept;
}
