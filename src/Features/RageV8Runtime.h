#pragma once

#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace RageV8Runtime
{
    // Per-command source ceiling. The command ring is heap-backed and
    // allocated on demand, so this does not sit in the mapped image.
    inline constexpr std::size_t kMaxScriptLength =
        1024 * 1024;
    inline constexpr std::size_t kMaxResultLength = 32768;

    enum class State : std::uint32_t
    {
        Uninitialized = 0,
        Resolving,
        WaitingForApi,
        Ready,
        Error
    };

    struct StatusSnapshot final
    {
        State state{State::Uninitialized};
        bool buildMatched{};
        bool pumpQueued{};
        bool rootCaptured{};
        DWORD ownerThreadId{};
        void* isolate{};
        std::uintptr_t context{};
        void* root{};
        std::uint64_t generation{};
        std::uint32_t queuedCommands{};
        std::uint32_t queuedResults{};
        std::uint64_t completedCommands{};
        std::uint64_t droppedCommands{};
        bool cameraAimPending{};
        std::uint64_t cameraAimSubmitted{};
        std::uint64_t cameraAimExecuted{};
        std::uint32_t bootstrapCallbackCount{};
        std::uint64_t nativeTickFaults{};
        char message[160]{};
    };

    struct Result final
    {
        std::uint64_t commandId{};
        bool success{};
        char text[kMaxResultLength]{};
    };

    struct NativeContext final
    {
        void* isolate{};
        void* context{};
        void* global{};
        void* root{};
        std::uintptr_t moduleBase{};
        std::uint64_t generation{};
        DWORD ownerThreadId{};
    };

    using OwnerThreadTick =
        void(*)(const NativeContext& context) noexcept;

    bool Initialize() noexcept;
    void Update() noexcept;
    void Retry() noexcept;
    void PrepareForReconnect() noexcept;

    bool Enqueue(
        const char* script,
        std::uint64_t* commandId = nullptr) noexcept;
    bool EnqueueFireAndForget(
        const char* script) noexcept;
    bool SubmitCameraAim(
        float worldX,
        float worldY,
        float worldZ,
        float yawFactor,
        float pitchFactor) noexcept;
    void ClearCameraAim() noexcept;
    bool PollResult(Result& result) noexcept;
    // Cheap readiness probe. A Result carries a 32 KB buffer, so callers that
    // poll every frame used to zero that much stack for nothing.
    bool HasResultFor(std::uint64_t commandId) noexcept;
    bool PollResultFor(
        std::uint64_t commandId,
        Result& result) noexcept;

    void SetOwnerThreadTick(OwnerThreadTick callback) noexcept;
    bool GetProperty(
        const NativeContext& context,
        void* object,
        const char* name,
        void** value) noexcept;
    bool SetProperty(
        const NativeContext& context,
        void* object,
        const char* name,
        void* value) noexcept;
    bool FindGlobalObjectWithFunction(
        const NativeContext& context,
        const char* name,
        void** object,
        void** function) noexcept;
    bool FindGlobalObjectWithFunctionResult(
        const NativeContext& context,
        const char* name,
        const char* argument,
        void** object,
        void** function,
        bool* probeMatched) noexcept;
    bool ExposeCurrentStackFunctionHost(
        const char* name,
        const char* argument,
        const char* hostProperty,
        const char* functionProperty) noexcept;
    bool ValueToUtf8(
        void* value,
        char* output,
        std::size_t capacity) noexcept;
    std::uint16_t LocalInstanceType(void* value) noexcept;
    bool BootstrapSawCallback(std::uintptr_t callback) noexcept;

    StatusSnapshot GetStatus() noexcept;
}
