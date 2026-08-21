#pragma once

#include <cstdint>

namespace RuntimeProfiler
{
    enum class Scope : std::uint8_t
    {
        Frame = 0,
        ScriptBridge,
        Names,
        EventLogger,
        Quant,
        Ukraine,
        World,
        Misc,
        PlayerCache,
        FramePedContext,
        Aimbot,
        Esp,
        Menu,
        PlayersOverlay,
        ModelPreview,
        ImGuiRender,
        V8OwnerTick,
        // ESP sub-scopes. These are accumulated per frame and submitted as one
        // sample, so a percentile still means "share of a frame" rather than
        // "cost of one label".
        EspProject,
        EspBoxes,
        EspText,
        EspSkeleton,
        SnapshotCopy,
        // Whole-frame wall clock between two Present calls, and the time the
        // game's own Present spends. Our CPU scopes above only cover what we
        // do; if those are small while the frame is long, the cost is GPU or
        // outside Present, and these two say which.
        FrameInterval,
        PresentCall,
        // Scripts we queue run inside the game's own V8 pump, on the thread
        // that drives its frame. That cost never showed up in any scope.
        V8Execute,
        Count
    };

    struct Stats final
    {
        double p50Us{};
        double p95Us{};
        double p99Us{};
        double maxUs{};
        double averageUs{};
        std::uint64_t calls{};
        std::uint32_t samples{};
    };

    struct WorkCounters final
    {
        std::uint32_t activePeds{};
        std::uint32_t drawnPeds{};
        std::uint32_t boneRefreshes{};
        std::uint32_t boneCacheHits{};
        std::uint32_t projections{};
        std::uint32_t visibilitySamples{};
        std::uint64_t visibilitySamplesTotal{};
        std::uint32_t labels{};
        std::uint32_t glyphPasses{};
        std::uint32_t skeletonsDrawn{};
        std::uint32_t skeletonsExpected{};
    };

    bool Enabled() noexcept;
    std::int64_t Begin() noexcept;
    void End(Scope scope, std::int64_t started) noexcept;
    // Sub-scope timing: sums inside the frame, one sample per FlushFrame.
    void Accumulate(Scope scope, std::int64_t started) noexcept;
    void FlushFrame() noexcept;
    Stats GetStats(Scope scope) noexcept;
    const char* Name(Scope scope) noexcept;
    void SetFrameWork(
        std::uint32_t activePeds,
        std::uint32_t boneRefreshes,
        std::uint32_t boneCacheHits,
        std::uint32_t projections) noexcept;
    void SetDrawnPeds(
        std::uint32_t drawnPeds) noexcept;
    void SetDrawWork(
        std::uint32_t labels,
        std::uint32_t glyphPasses) noexcept;
    void SetSkeletonWork(
        std::uint32_t drawn,
        std::uint32_t expected) noexcept;
    void RecordVisibilitySamples(
        std::uint32_t samples) noexcept;
    WorkCounters GetWorkCounters() noexcept;
    void TickReport() noexcept;
    void Reset() noexcept;
}

#if defined(RAGEMP_DIAGNOSTICS)
#define RUNTIME_PROFILE_BEGIN(variable) \
    const auto variable = RuntimeProfiler::Begin()
#define RUNTIME_PROFILE_END(name, variable) \
    RuntimeProfiler::End(RuntimeProfiler::Scope::name, variable)
#define RUNTIME_PROFILE_ACCUM(name, variable) \
    RuntimeProfiler::Accumulate(RuntimeProfiler::Scope::name, variable)
#else
#define RUNTIME_PROFILE_BEGIN(variable) ((void)0)
#define RUNTIME_PROFILE_END(name, variable) ((void)0)
#define RUNTIME_PROFILE_ACCUM(name, variable) ((void)0)
#endif
