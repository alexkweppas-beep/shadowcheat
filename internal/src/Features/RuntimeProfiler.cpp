#include "RuntimeProfiler.h"

#include <Windows.h>
#include <algorithm>
#include <cstdio>

namespace hooks
{
    void HLog(const char* message);
}

namespace
{
#if defined(RAGEMP_DIAGNOSTICS)
    constexpr int kScopeCount =
        static_cast<int>(RuntimeProfiler::Scope::Count);
    // 4096 samples cover a fixed ~60 second window at 60 FPS.
    constexpr int kSampleCapacity = 4096;

    struct ScopeState final
    {
        volatile LONG lock{};
        double samples[kSampleCapacity]{};
        std::uint32_t write{};
        std::uint32_t count{};
        std::uint64_t calls{};
        double sum{};
        double maximum{};
    };

    ScopeState g_States[kScopeCount]{};
    // Sub-scopes fire hundreds of times per frame; summing the ticks and
    // submitting one sample per frame keeps the ring window at ~60 s and keeps
    // percentiles meaningful ("share of a frame", not "cost of one label").
    volatile LONG64 g_Pending[kScopeCount]{};
    volatile LONG g_Initialized = 0;
    LARGE_INTEGER g_Frequency{};
    ULONGLONG g_LastReport = 0;
    ULONGLONG g_LastMinuteReport = 0;
    volatile LONG g_ActivePeds = 0;
    volatile LONG g_DrawnPeds = 0;
    volatile LONG g_Labels = 0;
    volatile LONG g_GlyphPasses = 0;
    volatile LONG g_SkeletonsDrawn = 0;
    volatile LONG g_SkeletonsExpected = 0;
    volatile LONG g_BoneRefreshes = 0;
    volatile LONG g_BoneCacheHits = 0;
    volatile LONG g_Projections = 0;
    volatile LONG g_VisibilitySamples = 0;
    volatile LONG64 g_VisibilitySamplesTotal = 0;

    void EnsureInitialized() noexcept
    {
        if (InterlockedCompareExchange(
                &g_Initialized, 1, 0) == 0)
        {
            QueryPerformanceFrequency(&g_Frequency);
            if (g_Frequency.QuadPart <= 0)
                g_Frequency.QuadPart = 1;
        }
    }

    void Lock(volatile LONG& lock) noexcept
    {
        while (InterlockedCompareExchange(
                   &lock, 1, 0) != 0)
            YieldProcessor();
    }

    void Unlock(volatile LONG& lock) noexcept
    {
        InterlockedExchange(&lock, 0);
    }

    int ScopeIndex(RuntimeProfiler::Scope scope) noexcept
    {
        const int index = static_cast<int>(scope);
        return index >= 0 && index < kScopeCount
            ? index : -1;
    }

    void RecordSample(int index, double microseconds) noexcept
    {
        auto& state = g_States[index];
        Lock(state.lock);
        state.samples[state.write] = microseconds;
        state.write =
            (state.write + 1) % kSampleCapacity;
        if (state.count < kSampleCapacity)
            ++state.count;
        ++state.calls;
        state.sum += microseconds;
        if (microseconds > state.maximum)
            state.maximum = microseconds;
        Unlock(state.lock);
    }
#endif
}

namespace RuntimeProfiler
{
    bool Enabled() noexcept
    {
#if defined(RAGEMP_DIAGNOSTICS)
        return true;
#else
        return false;
#endif
    }

    std::int64_t Begin() noexcept
    {
#if defined(RAGEMP_DIAGNOSTICS)
        EnsureInitialized();
        LARGE_INTEGER value{};
        QueryPerformanceCounter(&value);
        return value.QuadPart;
#else
        return 0;
#endif
    }

    void End(
        Scope scope,
        std::int64_t started) noexcept
    {
#if defined(RAGEMP_DIAGNOSTICS)
        if (!started)
            return;
        const int index = ScopeIndex(scope);
        if (index < 0)
            return;
        EnsureInitialized();
        LARGE_INTEGER ended{};
        QueryPerformanceCounter(&ended);
        const double microseconds =
            static_cast<double>(
                ended.QuadPart - started) *
            1000000.0 /
            static_cast<double>(
                g_Frequency.QuadPart);
        if (microseconds < 0.0)
            return;
        RecordSample(index, microseconds);
#else
        (void)scope;
        (void)started;
#endif
    }

    void Accumulate(
        Scope scope,
        std::int64_t started) noexcept
    {
#if defined(RAGEMP_DIAGNOSTICS)
        if (!started)
            return;
        const int index = ScopeIndex(scope);
        if (index < 0)
            return;
        EnsureInitialized();
        LARGE_INTEGER ended{};
        QueryPerformanceCounter(&ended);
        const std::int64_t ticks = ended.QuadPart - started;
        if (ticks <= 0)
            return;
        InterlockedExchangeAdd64(
            &g_Pending[index],
            static_cast<LONG64>(ticks));
#else
        (void)scope;
        (void)started;
#endif
    }

    void FlushFrame() noexcept
    {
#if defined(RAGEMP_DIAGNOSTICS)
        EnsureInitialized();
        const double scale = 1000000.0 /
            static_cast<double>(g_Frequency.QuadPart);
        for (int index = 0; index < kScopeCount; ++index)
        {
            const LONG64 ticks = InterlockedExchange64(
                &g_Pending[index],
                0);
            if (ticks <= 0)
                continue;
            RecordSample(
                index,
                static_cast<double>(ticks) * scale);
        }
#endif
    }

    Stats GetStats(Scope scope) noexcept
    {
        Stats result{};
#if defined(RAGEMP_DIAGNOSTICS)
        const int index = ScopeIndex(scope);
        if (index < 0)
            return result;
        auto& state = g_States[index];
        double values[kSampleCapacity]{};
        Lock(state.lock);
        const std::uint32_t count = state.count;
        for (std::uint32_t sample = 0;
             sample < count;
             ++sample)
            values[sample] = state.samples[sample];
        result.calls = state.calls;
        result.samples = count;
        result.averageUs = state.calls
            ? state.sum /
                static_cast<double>(state.calls)
            : 0.0;
        result.maxUs = state.maximum;
        Unlock(state.lock);

        std::sort(values, values + count);
        if (count)
        {
            result.p50Us = values[
                (count - 1) / 2];
            result.p95Us = values[
                (count - 1) * 95 / 100];
            result.p99Us = values[
                (count - 1) * 99 / 100];
        }
#else
        (void)scope;
#endif
        return result;
    }

    const char* Name(Scope scope) noexcept
    {
        switch (scope)
        {
        case Scope::Frame: return "Frame";
        case Scope::ScriptBridge: return "ScriptBridge";
        case Scope::Names: return "Names";
        case Scope::EventLogger: return "EventLogger";
        case Scope::Quant: return "Quant";
        case Scope::Ukraine: return "Ukraine";
        case Scope::World: return "World";
        case Scope::Misc: return "Misc";
        case Scope::PlayerCache: return "PlayerCache";
        case Scope::FramePedContext: return "FramePeds";
        case Scope::Aimbot: return "Aimbot";
        case Scope::Esp: return "ESP";
        case Scope::Menu: return "Menu";
        case Scope::PlayersOverlay: return "PlayersOverlay";
        case Scope::ModelPreview: return "ModelPreview";
        case Scope::ImGuiRender: return "ImGuiRender";
        case Scope::V8OwnerTick: return "V8OwnerTick";
        case Scope::EspProject: return "EspProject";
        case Scope::EspBoxes: return "EspBoxes";
        case Scope::EspText: return "EspText";
        case Scope::EspSkeleton: return "EspSkel";
        case Scope::SnapshotCopy: return "SnapCopy";
        case Scope::FrameInterval: return "FrameTotal";
        case Scope::PresentCall: return "PresentCall";
        case Scope::V8Execute: return "V8Exec";
        default: return "Unknown";
        }
    }

    void SetFrameWork(
        std::uint32_t activePeds,
        std::uint32_t boneRefreshes,
        std::uint32_t boneCacheHits,
        std::uint32_t projections) noexcept
    {
#if defined(RAGEMP_DIAGNOSTICS)
        InterlockedExchange(
            &g_ActivePeds,
            static_cast<LONG>(activePeds));
        InterlockedExchange(
            &g_BoneRefreshes,
            static_cast<LONG>(boneRefreshes));
        InterlockedExchange(
            &g_BoneCacheHits,
            static_cast<LONG>(boneCacheHits));
        InterlockedExchange(
            &g_Projections,
            static_cast<LONG>(projections));
#else
        (void)activePeds;
        (void)boneRefreshes;
        (void)boneCacheHits;
        (void)projections;
#endif
    }

    void SetDrawnPeds(
        std::uint32_t drawnPeds) noexcept
    {
#if defined(RAGEMP_DIAGNOSTICS)
        InterlockedExchange(
            &g_DrawnPeds,
            static_cast<LONG>(drawnPeds));
#else
        (void)drawnPeds;
#endif
    }

    void SetDrawWork(
        std::uint32_t labels,
        std::uint32_t glyphPasses) noexcept
    {
#if defined(RAGEMP_DIAGNOSTICS)
        InterlockedExchange(
            &g_Labels,
            static_cast<LONG>(labels));
        InterlockedExchange(
            &g_GlyphPasses,
            static_cast<LONG>(glyphPasses));
#else
        (void)labels;
        (void)glyphPasses;
#endif
    }

    void SetSkeletonWork(
        std::uint32_t drawn,
        std::uint32_t expected) noexcept
    {
#if defined(RAGEMP_DIAGNOSTICS)
        InterlockedExchange(
            &g_SkeletonsDrawn,
            static_cast<LONG>(drawn));
        InterlockedExchange(
            &g_SkeletonsExpected,
            static_cast<LONG>(expected));
#else
        (void)drawn;
        (void)expected;
#endif
    }

    void RecordVisibilitySamples(
        std::uint32_t samples) noexcept
    {
#if defined(RAGEMP_DIAGNOSTICS)
        InterlockedExchange(
            &g_VisibilitySamples,
            static_cast<LONG>(samples));
        InterlockedExchangeAdd64(
            &g_VisibilitySamplesTotal,
            static_cast<LONG64>(samples));
#else
        (void)samples;
#endif
    }

    WorkCounters GetWorkCounters() noexcept
    {
        WorkCounters counters{};
#if defined(RAGEMP_DIAGNOSTICS)
        counters.activePeds =
            static_cast<std::uint32_t>(
                InterlockedCompareExchange(
                    &g_ActivePeds, 0, 0));
        counters.drawnPeds =
            static_cast<std::uint32_t>(
                InterlockedCompareExchange(
                    &g_DrawnPeds, 0, 0));
        counters.boneRefreshes =
            static_cast<std::uint32_t>(
                InterlockedCompareExchange(
                    &g_BoneRefreshes, 0, 0));
        counters.boneCacheHits =
            static_cast<std::uint32_t>(
                InterlockedCompareExchange(
                    &g_BoneCacheHits, 0, 0));
        counters.projections =
            static_cast<std::uint32_t>(
                InterlockedCompareExchange(
                    &g_Projections, 0, 0));
        counters.visibilitySamples =
            static_cast<std::uint32_t>(
                InterlockedCompareExchange(
                    &g_VisibilitySamples, 0, 0));
        counters.visibilitySamplesTotal =
            static_cast<std::uint64_t>(
                InterlockedCompareExchange64(
                    &g_VisibilitySamplesTotal,
                    0,
                    0));
        counters.labels =
            static_cast<std::uint32_t>(
                InterlockedCompareExchange(
                    &g_Labels, 0, 0));
        counters.glyphPasses =
            static_cast<std::uint32_t>(
                InterlockedCompareExchange(
                    &g_GlyphPasses, 0, 0));
        counters.skeletonsDrawn =
            static_cast<std::uint32_t>(
                InterlockedCompareExchange(
                    &g_SkeletonsDrawn, 0, 0));
        counters.skeletonsExpected =
            static_cast<std::uint32_t>(
                InterlockedCompareExchange(
                    &g_SkeletonsExpected, 0, 0));
#endif
        return counters;
    }

    void TickReport() noexcept
    {
#if defined(RAGEMP_DIAGNOSTICS)
        const ULONGLONG now = GetTickCount64();
        if (now - g_LastReport < 5000)
            return;
        g_LastReport = now;
        bool minuteReport = false;
        if (!g_LastMinuteReport)
            g_LastMinuteReport = now;
        else if (now - g_LastMinuteReport >= 60000)
        {
            minuteReport = true;
            g_LastMinuteReport = now;
        }
        const Scope scopes[] = {
            Scope::FrameInterval,
            Scope::PresentCall,
            Scope::Frame,
            Scope::Names,
            Scope::PlayerCache,
            Scope::FramePedContext,
            Scope::Aimbot,
            Scope::Esp,
            Scope::Menu,
            Scope::ImGuiRender,
            Scope::V8OwnerTick,
            Scope::V8Execute,
            Scope::EspProject,
            Scope::EspBoxes,
            Scope::EspText,
            Scope::EspSkeleton,
            Scope::SnapshotCopy
        };
        char line[1536]{};
        int write = sprintf_s(
            line,
            minuteReport
                ? "[RuntimeProfiler60]"
                : "[RuntimeProfiler]");
        // p50/p99/max: a stutter lives in the tail, so the median alone hides
        // exactly what we are hunting.
        for (const Scope scope : scopes)
        {
            if (write <= 0 ||
                write >= static_cast<int>(
                    sizeof(line)) - 128)
                break;
            const Stats stats = GetStats(scope);
            if (!stats.samples)
                continue;
            write += sprintf_s(
                line + write,
                sizeof(line) - write,
                " %s=%.0f/%.0f/%.0fus",
                Name(scope),
                stats.p50Us,
                stats.p99Us,
                stats.maxUs);
        }
        const WorkCounters counters =
            GetWorkCounters();
        if (write > 0 &&
            write < static_cast<int>(
                sizeof(line)) - 220)
        {
            sprintf_s(
                line + write,
                sizeof(line) - write,
                " work[peds=%u draw=%u bone=%u cache=%u "
                "w2s=%u vis=%u/%llu lbl=%u glyph=%u "
                "skel=%u/%u]",
                counters.activePeds,
                counters.drawnPeds,
                counters.boneRefreshes,
                counters.boneCacheHits,
                counters.projections,
                counters.visibilitySamples,
                static_cast<unsigned long long>(
                    counters.visibilitySamplesTotal),
                counters.labels,
                counters.glyphPasses,
                counters.skeletonsDrawn,
                counters.skeletonsExpected);
        }
        hooks::HLog(line);
#endif
    }

    void Reset() noexcept
    {
#if defined(RAGEMP_DIAGNOSTICS)
        for (auto& state : g_States)
        {
            Lock(state.lock);
            state.write = 0;
            state.count = 0;
            state.calls = 0;
            state.sum = 0.0;
            state.maximum = 0.0;
            for (double& sample : state.samples)
                sample = 0.0;
            Unlock(state.lock);
        }
        for (auto& pending : g_Pending)
            InterlockedExchange64(&pending, 0);
        g_LastReport = 0;
        g_LastMinuteReport = 0;
        InterlockedExchange(&g_ActivePeds, 0);
        InterlockedExchange(&g_DrawnPeds, 0);
        InterlockedExchange(&g_Labels, 0);
        InterlockedExchange(&g_GlyphPasses, 0);
        InterlockedExchange(&g_SkeletonsDrawn, 0);
        InterlockedExchange(&g_SkeletonsExpected, 0);
        InterlockedExchange(&g_BoneRefreshes, 0);
        InterlockedExchange(&g_BoneCacheHits, 0);
        InterlockedExchange(&g_Projections, 0);
        InterlockedExchange(&g_VisibilitySamples, 0);
        InterlockedExchange64(
            &g_VisibilitySamplesTotal, 0);
#endif
    }
}
