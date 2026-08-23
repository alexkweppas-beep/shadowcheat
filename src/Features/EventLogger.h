#pragma once

// Records the server's script events and shows them, so that what a server
// actually sends can be read rather than guessed at.
//
// Part of the developer half of this tree: the implementation lives in src/Dev
// and is compiled only with -DSHADOW_DEV=ON. Without it these are inline no-ops,
// so the call sites in Hooks and ScriptBridge compile and cost nothing, and the
// user build links with src/Dev absent entirely.
namespace EventLogger
{
#if defined(RAGEMP_DIAGNOSTICS)

    void Initialize() noexcept;
    void Shutdown() noexcept;
    void Update() noexcept;
    void RenderUI();

#else

    inline void Initialize() noexcept {}
    inline void Shutdown() noexcept {}
    inline void Update() noexcept {}
    inline void RenderUI() {}

#endif
}
