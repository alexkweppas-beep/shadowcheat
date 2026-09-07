#pragma once

#include <cstdint>

// The server ships a client-side anti-cheat that watches noclip, teleports and
// vehicle state, and reports violations to the server through remote calls
// named s_ac_* / s:ac:*. The names are assembled at runtime from an obfuscated string
// table, so they cannot be listed by reading the dump: the reporter itself has
// to be observed. This wraps the runtime's outbound call, which both reveals
// the real names and gives the option of not letting the report leave.
namespace AntiCheatShield
{
    inline constexpr int kObservedNameCapacity = 8;
    inline constexpr int kNameLength = 64;

    struct Status final
    {
        bool installed{};
        // Wrapped call sites. Several mp instances coexist after a reconnect,
        // so this is normally more than one.
        int hooks{};
        std::uint32_t blocked{};
        std::uint32_t observed{};
        int nameCount{};
        char names[kObservedNameCapacity][kNameLength]{};
        char message[96]{};
    };

    // Off by default: it changes what the client tells the server, which is a
    // decision for whoever is playing, not a default.
    bool& Enabled() noexcept;

    void Initialize() noexcept;
    void Update() noexcept;
    void ForceInstall() noexcept;
    Status GetStatus() noexcept;

    // Declares an expected position jump the same way the server's own scripts
    // do before a legitimate teleport: mp.events.call('c:ac:check', ms). Safe
    // to call when the shield is disabled, and cheap enough to call per warp.
    void DeclarePositionJump(int milliseconds) noexcept;
}
