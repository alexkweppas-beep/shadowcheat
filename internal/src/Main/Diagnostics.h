#pragma once

// Crash diagnostics, present only in a build configured with
// RAGEMP_DIAGNOSTICS (CMake: -DSHADOW_INTERNAL_DIAGNOSTICS=ON).
//
// This is deliberately separate from RAGEMP_DEV_BUILD. That flag gates the
// development tree's ped dumps, event logger and dev UI, none of which exist in
// this tree, so switching it on here compiles code paths that have no
// implementation. RAGEMP_DIAGNOSTICS is the switch Hooks.h already uses to
// un-stub hooks::HLog; all that was missing was somewhere for it to write.
//
// The shipping build defines neither, so it still touches no files on disk.
namespace Diagnostics
{
    // Opens the log, points hooks::g_LogFn at it and installs the exception
    // recorders. Safe to call more than once. A no-op without
    // RAGEMP_DIAGNOSTICS.
    void Initialize() noexcept;

    void Shutdown() noexcept;

    // Writes one line. Prefer hooks::HLog, which routes here.
    void Write(const char* message) noexcept;
}
