#pragma once

// Crash diagnostics: a log file and a record of where faults happen.
//
// Part of the developer half of this tree. The implementation lives in
// src/Dev and is compiled only when the build is configured with -DSHADOW_DEV=ON;
// without it the calls below are inline no-ops, so every call site compiles and
// costs nothing, and the user build can be published with src/Dev absent
// entirely.
//
// Separate from RAGEMP_DEV_BUILD on purpose. That flag gates a different tree's
// ped dumps, event logger and dev UI, none of which exist here, so switching it
// on compiles branches that have no implementation.
namespace Diagnostics
{
#if defined(RAGEMP_DIAGNOSTICS)

    // Opens the log, points hooks::g_LogFn at it and installs the exception
    // recorders. Safe to call more than once.
    void Initialize() noexcept;
    void Shutdown() noexcept;
    // Writes one line. Prefer hooks::HLog, which routes here.
    void Write(const char* message) noexcept;

#else

    inline void Initialize() noexcept {}
    inline void Shutdown() noexcept {}
    inline void Write(const char*) noexcept {}

#endif
}
