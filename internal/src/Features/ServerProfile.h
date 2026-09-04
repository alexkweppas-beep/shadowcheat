#pragma once

#include <cstddef>
#include <cstdint>

// Which server we are on, and what that server lets us do.
//
// Three modules used to answer this question separately, each with its own
// fingerprint script and its own private enum, and all three asked the script
// runtime every second forever. They agreed on nothing and nobody else could
// ask: the roleplay identity resolver, the health scale and the anti-cheat
// announcement were all written for GTA5RP and ran everywhere.
//
// One probe answers for all of them now, and the answer is a capability table
// rather than a name, so a call site asks what it may do instead of guessing
// from a server it recognises.
namespace ServerProfile
{
    enum class Kind : int
    {
        // No answer yet. Nothing server-specific may be attempted here, and the
        // probe keeps asking.
        Unknown = 0,
        FiveRp,
        Quant,
        Ukraine,
        // A server we know nothing about: only the stock RAGE API is used.
        Generic,
        Count
    };

    // Where a player's roleplay name, faction and admin flag come from. Each
    // server publishes them under its own variable names, and a lookup aimed at
    // the wrong set returns nothing rather than failing loudly.
    enum class Identity : int
    {
        None = 0,
        FiveRp,
        Quant,
        Ukraine
    };

    // How a coming position jump is declared. The server's own scripts do this
    // before every teleport they make, and the check on the other side expects
    // the jump inside the window it opens; a warp that skips it looks exactly
    // like what they hunt.
    enum class Announce : int
    {
        // Nothing to tell: no such channel on this server.
        None = 0,
        // GTA5RP: mp.events.call('c:ac:check', ms)
        AcCheck,
        // Quant and Ukraine: mp.events.call('ac:refreshtp')
        RefreshTp
    };

    struct Settings final
    {
        // 0 keeps the probe's answer. Anything else pins a Kind by hand, which
        // is the only way to test one server's paths while sitting on another.
        int force{};
    };

    struct Status final
    {
        Kind kind{Kind::Unknown};
        bool probed{};
        bool forced{};
        int attempts{};
        char detail[64]{};
    };

    Settings& Access() noexcept;
    void Update() noexcept;
    void Reset() noexcept;
    Status GetStatus() noexcept;

    Kind Current() noexcept;
    const char* Name() noexcept;
    const char* NameOf(Kind kind) noexcept;

    inline bool IsFiveRp() noexcept { return Current() == Kind::FiveRp; }
    inline bool IsQuant() noexcept { return Current() == Kind::Quant; }
    inline bool IsUkraine() noexcept { return Current() == Kind::Ukraine; }
    // Any of the three roleplay servers whose own code we know.
    bool IsKnownRp() noexcept;

    Identity IdentitySource() noexcept;
    Announce AnnounceChannel() noexcept;

    // Families exist on GTA5RP alone; the other two have no equivalent.
    bool HasFamilies() noexcept;
    // The ground-item bridge auto loot rides on, and the trunk inventory RPC.
    bool HasItemBridge() noexcept;
    bool HasTrunkInventory() noexcept;
    bool HasGreenzone() noexcept;
    // The server's own "I am not idle" call. Without it anti-AFK has to move
    // the game itself.
    bool HasAfkBridge() noexcept;

    // Roleplay servers map their own 0..100 bar onto the game's 100..200, so a
    // hundred is an empty bar rather than half a one.
    bool RoleplayHealth() noexcept;
    // Health at or below this means the player is down.
    int DownThreshold() noexcept;
    // How far a single position jump may travel. Quant's check trips on 650 m
    // within two and a half seconds of the last legitimate teleport, so a long
    // warp is walked in steps instead of one hop.
    float MaxWarpStep() noexcept;

    // The body of the g.__bfAcPoke(ms) helper the movement hooks call. Kept
    // here rather than inline in each hook so the hooks stay free of any one
    // server's event names.
    const char* AnnounceJs() noexcept;

    bool Export(
        char* output,
        std::size_t capacity,
        std::size_t& used) noexcept;
    void Import(const char* content) noexcept;
}
