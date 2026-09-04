#pragma once

#include <cstdint>

// Server-wide admin enumeration for 5RP.
//
// The nametag snapshot only carries streamed players because ESP needs valid
// coordinates. The RAGE MP player pool, however, is not limited to stream
// range (stream-limited iteration is the separate forEachInStreamRange API),
// and shared entity variables are broadcast to every client regardless of
// distance. Walking the whole pool therefore finds every admin online, not
// just the nearby ones.
//
// The walk costs one or two getVariable calls per player across ~1900 players,
// so it only runs while the admin list is on and at a slow cadence.
namespace FiveRpAdminScan
{
    inline constexpr int kCapacity = 128;

    struct Entry final
    {
        std::uint16_t remoteId{};
        int characterId{};
        bool streamed{};
        char name[64]{};
        char factionTag[32]{};
    };

    struct Status final
    {
        int found{};
        int scanned{};
        // Players whose variable read threw, and players where it returned
        // nothing. A pass that finds nobody is only meaningful next to these.
        int errors{};
        int noData{};
        std::uint64_t lastScanAt{};
        char keyName[48]{};
    };

    void Update(bool active) noexcept;
    void Reset() noexcept;
    int CopyList(Entry* output, int capacity) noexcept;
    int Count() noexcept;
    Status GetStatus() noexcept;
}
