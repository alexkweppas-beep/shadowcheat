#include "WorldProbe.h"

#include <cstdio>
#include <cstring>

#include <Windows.h>

#include "../Main/Hooks.h"
#include "../vmp_markers.hpp"

namespace hooks
{
    void HLog(const char* message);
}

namespace WorldProbe
{
    namespace
    {
#if defined(RAGEMP_DIAGNOSTICS)
        // Anything past this is not slow, it is blocked: a shape test submit is
        // a pointer handoff into a pool and should never be measurable.
        constexpr double kStallMilliseconds = 40.0;

        std::int64_t StallClock() noexcept
        {
            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            return now.QuadPart;
        }

        void ReportStall(std::int64_t start, const char* what) noexcept
        {
            static LARGE_INTEGER frequency{};
            if (!frequency.QuadPart)
                QueryPerformanceFrequency(&frequency);
            if (!frequency.QuadPart)
                return;

            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            const double milliseconds =
                static_cast<double>(now.QuadPart - start) * 1000.0 /
                static_cast<double>(frequency.QuadPart);
            if (milliseconds < kStallMilliseconds)
                return;

            char line[128]{};
            _snprintf_s(
                line,
                sizeof(line),
                _TRUNCATE,
                "WorldProbe: %s BLOCKED for %.0f ms",
                what,
                milliseconds);
            hooks::HLog(line);
        }
#else
        // The timer itself costs two counter reads per probe, and the user
        // build has nowhere to report to, so it compiles away entirely.
        constexpr std::int64_t StallClock() noexcept { return 0; }
        inline void ReportStall(std::int64_t, const char*) noexcept {}
#endif

        // WorldProbe::CShapeTestManager::SubmitProbe. Twelve arguments, four in
        // registers and eight on the stack; the two vectors are loaded with
        // movaps, so both must be 16-byte aligned or the call faults.
        using SubmitFn = int (*)(
            void* manager,
            void* results,
            int resultCount,
            const float* start,
            const float* end,
            std::uint32_t options,
            std::uint32_t includeFlags,
            std::uint32_t argEight,
            std::uint32_t argNine,
            const void* const* excludeSource,
            std::uint32_t excludeMode,
            std::uint32_t argTwelve);

        // Returns a slot to the free list, keyed on the handle the submit
        // returned. Without this the pool of ninety drains and every later
        // probe is refused.
        using ReleaseFn = void (*)(void* manager, int handle);

        // Recovered from CShapeTestManager::Init and checked against live data:
        // the free list seeds to ninety descending indices with the count word
        // at +0x23EC, and the slot array ends exactly where the generation
        // words begin, which is what pins the stride.
        constexpr std::uint32_t kSlotsAt = 0x10;
        constexpr std::uint32_t kSlotStride = 0x60;
        constexpr int kSlotCount = 90;

        // Offsets inside one result block, read off a real hit: the position
        // matched the ray, the normal pointed up out of the ground, and the
        // fraction agreed with where along the segment the hit landed.
        constexpr std::uint32_t kResultSize = 0x40;
        constexpr std::uint32_t kResultPosition = 0x00;
        constexpr std::uint32_t kResultHit = 0x30;

        // Slot state is a bitfield, not a flag. Read off the manager's own
        // result getter, which tests bit 1 before anything else and only then
        // goes looking for the handle in the pending queue:
        //   bit 0  submitted
        //   bit 1  finished, results are readable
        //   bit 3  handle was not in the queue, so nothing will ever run
        //
        // Waiting for the whole word to reach zero, which is what the first
        // version did, waits for release instead of completion, and release is
        // what the caller is supposed to do after reading. Every probe timed
        // out on a condition that could not occur.
        constexpr std::uint32_t kSlotState = 0x4C;
        constexpr std::uint32_t kStateFinished = 1u << 1;
        constexpr std::uint32_t kStateAbandoned = 1u << 3;

        // How much of the game's pool we are willing to hold at once.
        //
        // This was 64 of the 90 slots, which is most of the subsystem. The pool
        // is not ours: the game runs its own line-of-sight, weapon traces,
        // camera collision and physics queries through it, and when a slot
        // cannot be had those callers do not all cope. The symptom was a null
        // dereference deep inside GTA5.exe with no frame of ours anywhere on
        // the stack, minutes after the fact, which is exactly what starving a
        // shared pool looks like: we break something for someone else and are
        // long gone by the time it falls over.
        //
        // Twelve is enough to keep visibility answering. Probes come back
        // within a frame or two, so a dozen in flight at sixty frames a second
        // is several hundred answers a second, well past what the ESP consumes,
        // and it leaves the game seven eighths of its own pool.
        constexpr int kInFlight = 12;
        alignas(16) std::uint8_t g_results[kInFlight][kResultSize]{};

        static_assert(
            kInFlight * 5 <= kSlotCount,
            "our share of the shape test pool must stay under a fifth: the "
            "game needs slots for its own traces and does not always survive "
            "being refused one");

        // Long enough that a busy frame does not throw away good work, short
        // enough that a wedged queue cannot leak the whole pool of ninety.
        constexpr std::uint64_t kDeadlineMs = 400;

        struct InFlight final
        {
            bool used{};
            int handle{};
            std::uint8_t generation{};
            std::uint64_t submittedAt{};
        };

        InFlight g_inFlight[kInFlight]{};

        // Argument values copied from probes the game itself submits, rather
        // than guessed from the documented native flags: the native's arguments
        // pass through a script command layer before reaching here, so the
        // mapping is not one to one and a working set observed in place is
        // worth more than a plausible one invented.
        //
        constexpr std::uint32_t kOptions = 7;

        // Wide on purpose, because the mask is not the lever that tells a target
        // apart from its cover.
        //
        // Two calibration rounds settled that. Narrow masks made the probe hit
        // nothing at all: 1|16 came back clear on every one of eight thousand
        // tests, as did 0x1, 0x2 and 0x10 individually. Wide masks hit something
        // every time. And subtracting bits from a wide mask changed nothing
        // measurable: without 0x1000, without 0x100, without the whole 0xFF00
        // group, even with only half the word left, the hit rate stayed at
        // essentially 100%. There is no bit here meaning "ignore what I am
        // aiming at", because the ray ends on the target's own bone and the
        // target is a perfectly real hit.
        //
        // Depth is what separates the two: a hit at the end of the ray is the
        // target itself, a hit short of it is cover. The caller already makes
        // that comparison, so all this mask has to do is avoid missing real
        // geometry.
        constexpr std::uint32_t kIncludeFlags = 0xFFFFFFFFu;
        constexpr std::uint32_t kArgEight = 0x0E1134C2;
        constexpr std::uint32_t kArgNine = 0x00800000;
        constexpr std::uint32_t kExcludeSingleEntity = 1;
        constexpr std::uint32_t kArgTwelve = 0;

        void** g_managerSlot = nullptr;
        SubmitFn g_submit = nullptr;
        ReleaseFn g_release = nullptr;
        bool g_tried = false;
        bool g_ready = false;
        Telemetry g_telemetry{};

        void Log(const char* message) noexcept
        {
            hooks::HLog(message);
        }

        bool MatchAt(
            const std::uint8_t* at,
            const std::uint8_t* bytes,
            const bool* wild,
            std::size_t length) noexcept
        {
            for (std::size_t index = 0; index < length; ++index)
            {
                if (!wild[index] && at[index] != bytes[index])
                    return false;
            }
            return true;
        }

        // Only committed, executable pages are searched. A flat scan over the
        // module is not an option here: the image is 156 MB because the packer
        // reserves a 128 MB hole that the real code is unpacked into, and much
        // of the span is not backed at all.
        const std::uint8_t* Scan(
            const std::uint8_t* bytes,
            const bool* wild,
            std::size_t length) noexcept
        {
            const auto module =
                reinterpret_cast<std::uintptr_t>(
                    GetModuleHandleA(nullptr));
            if (!module)
                return nullptr;

            MEMORY_BASIC_INFORMATION info{};
            std::uintptr_t cursor = module;
            while (VirtualQuery(
                       reinterpret_cast<void*>(cursor),
                       &info,
                       sizeof(info)) == sizeof(info))
            {
                const auto base =
                    reinterpret_cast<std::uintptr_t>(
                        info.BaseAddress);
                if (base < module)
                {
                    cursor = base + info.RegionSize;
                    continue;
                }
                const bool usable =
                    info.State == MEM_COMMIT &&
                    (info.Protect &
                     (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                      PAGE_EXECUTE_READWRITE |
                      PAGE_EXECUTE_WRITECOPY)) != 0 &&
                    (info.Protect & PAGE_GUARD) == 0;
                if (usable && info.RegionSize >= length)
                {
                    const auto* start =
                        reinterpret_cast<const std::uint8_t*>(base);
                    const std::size_t span =
                        info.RegionSize - length;
                    for (std::size_t offset = 0;
                         offset <= span;
                         ++offset)
                    {
                        if (MatchAt(
                                start + offset, bytes, wild, length))
                            return start + offset;
                    }
                }
                const std::uintptr_t next =
                    base + info.RegionSize;
                if (next <= cursor)
                    break;
                cursor = next;
            }
            return nullptr;
        }

        template <std::size_t N>
        const std::uint8_t* Find(
            const char (&pattern)[N]) noexcept
        {
            std::uint8_t bytes[N]{};
            bool wild[N]{};
            std::size_t length = 0;
            for (std::size_t index = 0; index + 1 < N;)
            {
                const char value = pattern[index];
                if (value == ' ')
                {
                    ++index;
                    continue;
                }
                if (value == '?')
                {
                    wild[length++] = true;
                    ++index;
                    if (index + 1 < N && pattern[index] == '?')
                        ++index;
                    continue;
                }
                const auto digit = [](char character) noexcept {
                    if (character >= '0' && character <= '9')
                        return character - '0';
                    if (character >= 'a' && character <= 'f')
                        return character - 'a' + 10;
                    return character - 'A' + 10;
                };
                bytes[length++] = static_cast<std::uint8_t>(
                    (digit(pattern[index]) << 4) |
                    digit(pattern[index + 1]));
                index += 2;
            }
            return Scan(bytes, wild, length);
        }

        std::uintptr_t RipTarget(
            const std::uint8_t* at,
            std::size_t dispOffset,
            std::size_t instructionLength) noexcept
        {
            std::int32_t displacement = 0;
            std::memcpy(
                &displacement, at + dispOffset, sizeof(displacement));
            return reinterpret_cast<std::uintptr_t>(at) +
                instructionLength + displacement;
        }
    }

    bool Initialize() noexcept
    {
        if (g_tried)
            return g_ready;
        g_tried = true;
        SHADOW_PROTECT_ULTRA("WorldProbe.Initialize");

        // The manager is constructed once and parked in a global. Anchored on
        // the construction site rather than on the global's address, which is
        // build specific: allocate, construct, store, initialise.
        //   mov r8d, 0x4000 / mov rcx, rax / mov [rip+X], rax / call Init
        const std::uint8_t* site = Find(
            "41 B8 00 40 00 00 48 8B C8 48 89 05 ? ? ? ? E8");
        if (site)
            g_managerSlot = reinterpret_cast<void**>(
                RipTarget(site, 12, 16));

        // SubmitProbe's prologue, ending on the lock it takes at +0x7820. Long
        // on purpose: a shorter stem matches ordinary frame setup all over the
        // module, and resolving the wrong function here means calling into
        // arbitrary code with twelve arguments.
        g_submit = reinterpret_cast<SubmitFn>(
            const_cast<std::uint8_t*>(Find(
                "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 "
                "48 89 78 20 41 54 41 56 41 57 48 83 EC 20 "
                "48 8D A9 20 78 00 00 48 8B D9")));

        // Release: takes the handle in edx, uses its low half as the slot index,
        // then walks the free list at +0x23E8 to put it back. The scan it does
        // first is also what makes a double release harmless.
        g_release = reinterpret_cast<ReleaseFn>(
            const_cast<std::uint8_t*>(Find(
                "4C 8B C9 44 0F B7 C2 B9 59 00 00 00")));

        g_ready = g_managerSlot && g_submit && g_release;
        SHADOW_PROTECT_END();

        char line[224]{};
        _snprintf_s(
            line,
            sizeof(line),
            _TRUNCATE,
            "WorldProbe: managerSlot=%016llX submit=%016llX "
            "release=%016llX ready=%d",
            static_cast<unsigned long long>(
                reinterpret_cast<std::uintptr_t>(g_managerSlot)),
            static_cast<unsigned long long>(
                reinterpret_cast<std::uintptr_t>(g_submit)),
            static_cast<unsigned long long>(
                reinterpret_cast<std::uintptr_t>(g_release)),
            g_ready ? 1 : 0);
        Log(line);
        return g_ready;
    }

    bool Available() noexcept
    {
        return g_ready;
    }

    namespace
    {
        void* CurrentManager() noexcept
        {
            if (!g_ready && !Initialize())
                return nullptr;
            void* manager = nullptr;
            __try
            {
                manager = *g_managerSlot;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return nullptr;
            }
            if (manager)
                g_telemetry.manager =
                    reinterpret_cast<std::uint64_t>(manager);
            return manager;
        }

        // Tokens carry a generation so a stale one from a previous session, or
        // from an entry that has already been recycled, cannot be mistaken for
        // a live probe and read someone else's result.
        int MakeToken(int index, std::uint8_t generation) noexcept
        {
            return (static_cast<int>(generation) << 8) | index;
        }

        InFlight* Resolve(int token) noexcept
        {
            if (token < 0)
                return nullptr;
            const int index = token & 0xFF;
            if (index >= kInFlight)
                return nullptr;
            InFlight& entry = g_inFlight[index];
            if (!entry.used ||
                entry.generation !=
                    static_cast<std::uint8_t>(
                        (token >> 8) & 0xFF))
                return nullptr;
            return &entry;
        }

        void ReleaseHandle(void* manager, int handle) noexcept
        {
            if (!manager || !g_release)
                return;
            const std::int64_t releaseStart = StallClock();
            __try
            {
                g_release(manager, handle);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                // Survivable: the manager sweeps slots that stay pending past
                // its own timeout, so the pool recovers without us.
                Log("WorldProbe: release faulted");
            }
            ReportStall(releaseStart, "release");
        }
    }

    int Begin(
        const Vector3& from,
        const Vector3& to,
        const void* ignoreEntity) noexcept
    {
        void* manager = CurrentManager();
        if (!manager)
            return -1;

        int index = -1;
        for (int probe = 0; probe < kInFlight; ++probe)
        {
            if (!g_inFlight[probe].used)
            {
                index = probe;
                break;
            }
        }
        if (index < 0)
        {
            ++g_telemetry.refused;
            return -1;
        }

        alignas(16) float start[4]{from.x, from.y, from.z, 1.0f};
        alignas(16) float end[4]{to.x, to.y, to.z, 1.0f};
        std::uint8_t* results = g_results[index];
        std::memset(results, 0, kResultSize);
        const void* exclude = ignoreEntity;

        // The game's shape test manager is shared with the game's own threads.
        // Calling it from the render thread means taking whatever lock it uses,
        // and if the holder is itself waiting on this thread the frame stops
        // dead. That is the shape of the stalls being chased, so the call is
        // timed and anything long enough to be noticed is named.
        const std::int64_t submitStart = StallClock();

        int handle = -1;
        __try
        {
            handle = g_submit(
                manager,
                results,
                1,
                start,
                end,
                kOptions,
                kIncludeFlags,
                kArgEight,
                kArgNine,
                &exclude,
                kExcludeSingleEntity,
                kArgTwelve);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_ready = false;
            Log("WorldProbe: submit faulted, disabling");
            return -1;
        }

        ReportStall(submitStart, "submit");

        if (handle == -1)
        {
            ++g_telemetry.refused;
            return -1;
        }

        InFlight& entry = g_inFlight[index];
        entry.used = true;
        entry.handle = handle;
        entry.generation =
            static_cast<std::uint8_t>(entry.generation + 1);
        entry.submittedAt = GetTickCount64();
        ++g_telemetry.submitted;
        return MakeToken(index, entry.generation);
    }

    Result Poll(int token, Vector3* hitPoint) noexcept
    {
        InFlight* entry = Resolve(token);
        if (!entry)
            return Result::Lost;

        void* manager = CurrentManager();
        if (!manager)
        {
            entry->used = false;
            ++g_telemetry.lost;
            return Result::Lost;
        }

        const auto slot =
            static_cast<std::uint32_t>(entry->handle) & 0xFFFFu;
        if (slot >= static_cast<std::uint32_t>(kSlotCount))
        {
            entry->used = false;
            ++g_telemetry.lost;
            return Result::Lost;
        }

        std::uint32_t state = 0;
        __try
        {
            state = *reinterpret_cast<const volatile std::uint32_t*>(
                static_cast<std::uint8_t*>(manager) +
                kSlotsAt + slot * kSlotStride + kSlotState);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            entry->used = false;
            ++g_telemetry.lost;
            return Result::Lost;
        }

        if (!(state & kStateFinished))
        {
            const bool abandoned = (state & kStateAbandoned) != 0;
            if (!abandoned &&
                GetTickCount64() - entry->submittedAt <= kDeadlineMs)
                return Result::Pending;
            ReleaseHandle(manager, entry->handle);
            entry->used = false;
            ++g_telemetry.lost;
            return Result::Lost;
        }

        const std::uint8_t* results =
            g_results[token & 0xFF];
        std::uint32_t hit = 0;
        std::memcpy(&hit, results + kResultHit, sizeof(hit));
        if (hit && hitPoint)
        {
            float point[3]{};
            std::memcpy(
                point, results + kResultPosition, sizeof(point));
            *hitPoint = Vector3(point[0], point[1], point[2]);
        }

        ReleaseHandle(manager, entry->handle);
        entry->used = false;
        if (hit)
        {
            ++g_telemetry.blocked;
            return Result::Blocked;
        }
        ++g_telemetry.clear;
        return Result::Clear;
    }

    void Cancel(int token) noexcept
    {
        InFlight* entry = Resolve(token);
        if (!entry)
            return;
        ReleaseHandle(CurrentManager(), entry->handle);
        entry->used = false;
    }

    void CancelAll() noexcept
    {
        void* manager = CurrentManager();
        for (auto& entry : g_inFlight)
        {
            if (!entry.used)
                continue;
            ReleaseHandle(manager, entry.handle);
            entry.used = false;
        }
    }

    int Free() noexcept
    {
        int free = 0;
        for (const auto& entry : g_inFlight)
        {
            if (!entry.used)
                ++free;
        }
        return free;
    }

    const Telemetry& GetTelemetry() noexcept
    {
        return g_telemetry;
    }

}
