#pragma once

#include <Windows.h>
#include <cstdint>

// The payload is a manually mapped image, so the CRT never runs its startup
// code: localtime_s faults inside its lazy timezone/heap initialisation and the
// SEH guard around the frame swallows it, silently killing the rest of the UI.
// Win32 performs the same conversion without touching the CRT heap.
namespace TimeUtil
{
    // Seconds since the Unix epoch to local wall-clock time.
    inline bool LocalTimeFromUnix(
        std::int64_t unixSeconds,
        SYSTEMTIME& out) noexcept
    {
        out = SYSTEMTIME{};
        if (unixSeconds <= 0)
            return false;
        // FILETIME counts 100 ns ticks since 1601-01-01.
        const std::int64_t ticks =
            (unixSeconds + 11644473600LL) * 10000000LL;
        if (ticks <= 0)
            return false;
        FILETIME utc{};
        utc.dwLowDateTime = static_cast<DWORD>(ticks & 0xFFFFFFFFLL);
        utc.dwHighDateTime =
            static_cast<DWORD>((ticks >> 32) & 0xFFFFFFFFLL);
        FILETIME local{};
        if (!FileTimeToLocalFileTime(&utc, &local))
            return false;
        return FileTimeToSystemTime(&local, &out) != FALSE;
    }
}
