#pragma once

#include <cstdint>

#include "../SDK/Math.h"

// Line-of-sight straight through GTA V's own world probe manager, with no script
// native involved.
//
// Natives cannot be called by hash under RAGE MP, and this is settled rather
// than suspected. The game's registration table decodes cleanly to 6595 entries,
// but every single one of them points at one obfuscated dispatcher inside
// multiplayer.dll that works out which native it is serving from script VM
// state, and the original handlers are not present anywhere in the process. So
// the table is a dead end from the render thread by design, and the JS route it
// forced everything into is what made the visible check flicker.
//
// What is reachable is the subsystem underneath the natives.
// WorldProbe::CShapeTestManager lives in GTA5.exe, which is not obfuscated once
// unpacked, and its submit entry point takes plain vectors and returns a handle.
// That is what this calls.
namespace WorldProbe
{
    // Resolves the manager and its entry points. Safe to call repeatedly; the
    // scan runs once and the verdict is latched.
    bool Initialize() noexcept;
    bool Available() noexcept;

    // Submitting and reading are separate calls because the manager is
    // asynchronous: submit only appends the handle to a work queue that the
    // game's own scheduler task drains, so an answer cannot exist yet when
    // submit returns. Reading immediately and releasing the slot, which is what
    // the first attempt did, cancelled every test before it ran: thousands of
    // probes came back "nothing in the way" because nothing had been traced.
    //
    // Begin returns a token to poll with, or -1 when nothing could be started.
    int Begin(
        const Vector3& from,
        const Vector3& to,
        const void* ignoreEntity) noexcept;

    enum class Result : std::uint8_t
    {
        // Still queued; poll again next frame.
        Pending,
        Clear,
        Blocked,
        // Abandoned: the test outlived its deadline, or the token is stale.
        // The slot has been released either way.
        Lost
    };

    // Releases the slot on anything but Pending. hitPoint is written only for
    // Blocked.
    Result Poll(int token, Vector3* hitPoint) noexcept;

    void Cancel(int token) noexcept;
    void CancelAll() noexcept;

    struct Telemetry final
    {
        std::uint32_t submitted{};
        std::uint32_t refused{};
        std::uint32_t blocked{};
        std::uint32_t clear{};
        // Tests abandoned for outliving their deadline. A steady stream here
        // means the scheduler is not draining the queue and the whole approach
        // needs rethinking, rather than the answers merely being late.
        std::uint32_t lost{};
        std::uint64_t manager{};
    };

    const Telemetry& GetTelemetry() noexcept;
}
