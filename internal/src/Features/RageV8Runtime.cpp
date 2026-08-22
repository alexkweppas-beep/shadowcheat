#define WIN32_LEAN_AND_MEAN
#include "RageV8Runtime.h"
#include "RuntimeProfiler.h"

#ifdef RESOURCE_DUMPER_SAFE_JS
#include "../../ResourceDumperStandalone/StandaloneLog.h"
#else
#include "../Main/Hooks.h"
#endif
#include "../SDK/RageMpBuildProfile.h"
#include "../SDK/RageBuildFlavor.h"
#include "../SDK/V8Internal87.h"

#include <TlHelp32.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <intrin.h>

namespace
{
    // multiplayer.dll rev 396fe4b (PE ts 0x6A6F9E5F) — live-remapped 2026-08-06.
    constexpr std::uint32_t kPlatformSingletonRva = 0x01559CE8U;
    constexpr std::uint32_t kIsolateTlsKeyRva = 0x0155A1E8U;
    constexpr std::uint32_t kHandleScopeCtorRva = 0x006CCC80U;
    constexpr std::uint32_t kHandleScopeDtorRva = 0x005714C0U;
    constexpr std::uint32_t kStringNewFromUtf8Rva = 0x00554B80U;
    constexpr std::uint32_t kValueToUtf8Rva = 0x00548600U;
    constexpr std::uint32_t kUtf8CleanupRva = 0x00548E30U;
    constexpr std::uint32_t kObjectGetByKeyRva = 0x0054DA20U;
    constexpr std::uint32_t kObjectSetByKeyRva = 0x005577A0U;
    constexpr std::uint32_t kFunctionCallRva = 0x005496F0U;
    constexpr std::uint32_t kFunctionTemplateNewRva = 0x00553AF0U;
    constexpr std::uint32_t kGlobalizeReferenceRva = 0x00550A10U;
    constexpr std::uint32_t kApiBootstrapRva = 0x00335100U;
    constexpr std::uint32_t kApiBootstrapReturnRva = 0x00340087U;
    constexpr int kV8StringNormal = 0;

    struct RuntimeFingerprint final
    {
        std::uint32_t rva;
        std::uint8_t bytes[16];
    };

    constexpr RuntimeFingerprint kRuntimeFingerprints[] = {
        {kHandleScopeCtorRva,
         {0x48, 0x89, 0x11, 0x48, 0x8B, 0x82, 0x28, 0xA7,
          0x00, 0x00, 0x48, 0x89, 0x41, 0x08, 0x48, 0x8B}},
        {kHandleScopeDtorRva,
         {0x48, 0x8B, 0xC1, 0x48, 0x8B, 0x09, 0x48, 0x85,
          0xC9, 0x74, 0x2A, 0x48, 0x8B, 0x50, 0x10, 0x48}},
        {kStringNewFromUtf8Rva,
         {0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x57, 0x41,
          0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC}},
        {kValueToUtf8Rva,
         {0x48, 0x8B, 0xC4, 0x55, 0x53, 0x56, 0x57, 0x41,
          0x57, 0x48, 0x8D, 0x68, 0xA1, 0x48, 0x81, 0xEC}},
        {kUtf8CleanupRva,
         {0x48, 0x8B, 0x09, 0xE9, 0x34, 0xB9, 0xFB, 0xFF,
          0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC}},
        {kObjectGetByKeyRva,
         {0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x53, 0x56,
          0x57, 0x41, 0x54, 0x41, 0x56, 0x48, 0x8D, 0x6C}},
        {kObjectSetByKeyRva,
         {0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x48,
          0x8D, 0x6C, 0x24, 0xD1, 0x48, 0x81, 0xEC, 0xC0}},
        {kFunctionCallRva,
         {0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x53, 0x56,
          0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x48}},
        {kFunctionTemplateNewRva,
         {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x68, 0x08, 0x48,
          0x89, 0x70, 0x10, 0x48, 0x89, 0x78, 0x18, 0x4C}},
        {kGlobalizeReferenceRva,
         {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74,
          0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x8B}},
        {kApiBootstrapRva,
         {0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57,
          0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57}},
        {kApiBootstrapReturnRva,
         {0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
          0xCC, 0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54}},
    };

    constexpr std::uint32_t kCommandCapacity = 8;
    constexpr std::uint32_t kResultCapacity = 16;
    constexpr std::uint32_t kBootstrapCallbackCapacity = 256;
    constexpr std::size_t kApiStringsBefore = 0x4A08;
    constexpr std::size_t kApiStringsSnapshotSize = 0x5000;

    using HandleScopeCtor =
        void*(__fastcall*)(void* scope, void* isolate);
    using HandleScopeDtor = void(__fastcall*)(void* scope);
    using StringNewFromUtf8 = void(__fastcall*)(
        void* out,
        void* isolate,
        const char* data,
        int type,
        int length);
    struct Utf8Buffer final
    {
        char* data{};
        std::int32_t length{};
        std::int32_t reserved{};
    };
    using ValueToUtf8 = void*(__fastcall*)(
        Utf8Buffer* output,
        void* isolate,
        void* value);
    using Utf8Cleanup =
        void(__fastcall*)(Utf8Buffer* output);
    using ObjectGetByKey = void(__fastcall*)(
        void* object,
        void* out,
        void* context,
        void* key);
    using ObjectSetByKey = void(__fastcall*)(
        void* object,
        void* out,
        void* context,
        void* key,
        void* value);
    using FunctionCall = void(__fastcall*)(
        void* function,
        void* out,
        void* context,
        void* receiver,
        int argc,
        void** argv);
    using GlobalizeReference =
        void*(__fastcall*)(void* isolate, void* localHandle);
    using GetForegroundTaskRunner = void(__fastcall*)(
        void* platform,
        void* outSharedPointer,
        void* isolate);
    using PostTask =
        void(__fastcall*)(void* runner, void* uniqueTaskPointer);
    using PostDelayedTask = void(__fastcall*)(
        void* runner,
        void* uniqueTaskPointer,
        double delaySeconds);

    struct V8Api final
    {
        std::uintptr_t base{};
        void* isolate{};
        void* platform{};
        DWORD ownerThreadId{};
        HandleScopeCtor handleScopeCtor{};
        HandleScopeDtor handleScopeDtor{};
        StringNewFromUtf8 newFromUtf8{};
        ValueToUtf8 valueToUtf8{};
        Utf8Cleanup cleanupUtf8{};
        ObjectGetByKey objectGetByKey{};
        ObjectSetByKey objectSetByKey{};
        FunctionCall functionCall{};
        GlobalizeReference globalizeReference{};
    };

    // User-script command. The source buffer is owned by the ring slot and
    // allocated on first use: at a 1 MiB ceiling the old inline array put
    // 8 MiB of dead weight in the mapped image.
    struct Command final
    {
        std::uint64_t id{};
        std::uint64_t generation{};
        char* script{};
    };

    // Camera aim scripts are generated here and stay small, so they keep an
    // inline buffer and never touch the user-script ring.
    constexpr std::size_t kCameraScriptCapacity = 2048;

    struct CameraCommand final
    {
        std::uint64_t generation{};
        char script[kCameraScriptCapacity]{};
    };

    struct RuntimeTask;
    using TaskDestructor =
        void(__fastcall*)(RuntimeTask* task, unsigned int flags);
    using TaskRun = void(__fastcall*)(RuntimeTask* task);

    struct TaskVTable final
    {
        TaskDestructor destructor{};
        TaskRun run{};
    };

    struct RuntimeTask final
    {
        TaskVTable* vtable{};
        unsigned int slot{};
    };

    volatile LONG g_State =
        static_cast<LONG>(RageV8Runtime::State::Uninitialized);
    volatile LONG g_InitializeStarted = 0;
    volatile LONG g_BuildMatched = 0;
    volatile LONG g_PumpQueued = 0;
    volatile LONG g_RootCaptured = 0;
    volatile LONG g_CaptureStage = 0;
    volatile LONG g_ReconnectRequested = 0;
    volatile LONG g_WaitingForContextChange = 0;
    volatile LONG g_V8Paused = 0;
    volatile LONG64 g_RootGeneration = 0;
    volatile LONG64 g_BoundContext = 0;
    volatile LONG64 g_ReconnectPreviousContext = 0;
    volatile LONG64 g_NextCommandId = 0;
    volatile LONG64 g_ExecutedCommands = 0;
    volatile LONG64 g_DroppedCommands = 0;
    volatile LONG64 g_NativeTickFaults = 0;
    volatile LONG g_TraceCommandCount = 0;
    ULONGLONG g_LastInitializeAttempt = 0;
    ULONGLONG g_LastCaptureArmAttempt = 0;
    ULONGLONG g_ReadySince = 0;

    volatile LONG g_StatusLock = 0;
    char g_StatusMessage[160] = "Waiting for multiplayer.dll";

    volatile LONG g_CommandLock = 0;
    Command g_Commands[kCommandCapacity]{};
    std::uint32_t g_CommandRead = 0;
    std::uint32_t g_CommandWrite = 0;
    std::uint32_t g_CommandCount = 0;
    volatile LONG g_CameraCommandLock = 0;
    volatile LONG g_CameraCommandPending = 0;
    CameraCommand g_CameraCommand{};
    volatile LONG64 g_CameraAimSubmitted = 0;
    volatile LONG64 g_CameraAimExecuted = 0;
    volatile LONG g_CameraAimRequested = 0;

    volatile LONG g_ResultLock = 0;
    RageV8Runtime::Result g_Results[kResultCapacity]{};
    std::uint32_t g_ResultRead = 0;
    std::uint32_t g_ResultWrite = 0;
    std::uint32_t g_ResultCount = 0;

    V8Api g_Api{};
    void* volatile g_OwnerThreadTick = nullptr;
    void* g_TaskRunner = nullptr;
    void* g_TaskRunnerControl = nullptr;
    RuntimeTask g_Tasks[2]{};
    volatile LONG g_TaskInUse[2]{};
    TaskVTable g_TaskVTable{};

    std::uintptr_t g_CaptureEntry = 0;
    std::uintptr_t g_CaptureReturn = 0;
    std::uintptr_t g_FunctionTemplateNew = 0;
    std::uintptr_t g_PreBootstrapDr3 = 0;
    DWORD64 g_PreBootstrapDr3Control = 0;
    std::uintptr_t
        g_BootstrapCallbacks[kBootstrapCallbackCapacity]{};
    std::uintptr_t
        g_BootstrapCallbackCallers[
            kBootstrapCallbackCapacity]{};
    std::uintptr_t
        g_BootstrapCallbackData[
            kBootstrapCallbackCapacity]{};
    volatile LONG g_BootstrapCallbackCount = 0;
    std::uintptr_t g_ApiStringsRuntimeBase = 0;
    std::uint8_t
        g_ApiStringsSnapshot[
            kApiStringsSnapshotSize]{};
    std::uintptr_t g_CapturedRootLocal = 0;
    void* g_RootPersistent = nullptr;
    void* g_VehHandle = nullptr;
    std::uintptr_t g_GlobalHandleBlock = 0;
    std::uintptr_t g_GlobalHandleScanContext = 0;
    std::uint32_t g_GlobalHandleBlocksScanned = 0;
    bool g_GlobalHandleScanComplete = false;
    ULONGLONG g_LastGlobalHandleScan = 0;
    ULONGLONG g_RootScanNotBefore = 0;

    struct FiveRpPlayersProbe final
    {
        std::uint64_t generation{};
        std::uintptr_t block{};
        std::uintptr_t previousBlock{};
        std::uint32_t blockIndex{};
        int nodeIndex{};
        std::uintptr_t seenContexts[512]{};
        int seenContextCount{};
        std::uintptr_t seenReceivers[1024]{};
        int seenReceiverCount{};
        ULONGLONG retryAt{};
        bool playersExposed{};
        bool apiExposed{};
        bool dataKeysExposed{};
        bool exposed{};
    };

    FiveRpPlayersProbe g_FiveRpPlayersProbe{};

    char* g_WrappedSource = nullptr;
    Command g_ExecutingCommand{};

    char* AllocateScriptBuffer() noexcept
    {
        auto* buffer = static_cast<char*>(
            HeapAlloc(
                GetProcessHeap(),
                0,
                RageV8Runtime::kMaxScriptLength));
        if (buffer)
            buffer[0] = 0;
        return buffer;
    }

    // Keeps the slot's buffer, drops only the payload: the reset paths must
    // not leak the allocation they are clearing.
    void ResetCommand(Command& command) noexcept
    {
        command.id = 0;
        command.generation = 0;
        if (command.script)
            command.script[0] = 0;
    }

    double ElapsedMilliseconds(std::int64_t started) noexcept
    {
        if (!started)
            return 0.0;
        LARGE_INTEGER frequency{};
        LARGE_INTEGER now{};
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&now);
        if (frequency.QuadPart <= 0)
            return 0.0;
        return static_cast<double>(now.QuadPart - started) *
            1000.0 /
            static_cast<double>(frequency.QuadPart);
    }

    void Lock(volatile LONG& lock) noexcept
    {
        while (InterlockedCompareExchange(&lock, 1, 0) != 0)
            Sleep(0);
    }

    void Unlock(volatile LONG& lock) noexcept
    {
        InterlockedExchange(&lock, 0);
    }

    void CopyText(char* output, std::size_t capacity, const char* text) noexcept
    {
        if (!output || !capacity)
            return;
        if (!text)
        {
            output[0] = 0;
            return;
        }
        lstrcpynA(output, text, static_cast<int>(capacity));
    }

    void AppendText(char* output, std::size_t capacity, const char* text) noexcept
    {
        if (!output || !capacity || !text)
            return;
        const int used = lstrlenA(output);
        if (used >= static_cast<int>(capacity) - 1)
            return;
        lstrcpynA(
            output + used,
            text,
            static_cast<int>(capacity) - used);
    }

    bool StartsWith(const char* text, const char* prefix) noexcept
    {
        if (!text || !prefix)
            return false;
        while (*prefix)
        {
            if (*text++ != *prefix++)
                return false;
        }
        return true;
    }

    void SetState(
        RageV8Runtime::State state,
        const char* message) noexcept
    {
        const LONG previousState = InterlockedExchange(
            &g_State,
            static_cast<LONG>(state));
        if (state == RageV8Runtime::State::Ready &&
            previousState != static_cast<LONG>(
                RageV8Runtime::State::Ready))
            g_ReadySince = GetTickCount64();
        if (state != RageV8Runtime::State::Ready)
            g_ReadySince = 0;
        Lock(g_StatusLock);
        const bool changed =
            previousState != static_cast<LONG>(state) ||
            lstrcmpA(
                g_StatusMessage,
                message ? message : "") != 0;
        CopyText(g_StatusMessage, sizeof(g_StatusMessage), message);
        Unlock(g_StatusLock);
        if (changed && message)
        {
#if RAGEMP_DEV_BUILD
            char line[224]{};
            CopyText(line, sizeof(line), "RageV8Runtime: ");
            AppendText(line, sizeof(line), message);
#ifdef RESOURCE_DUMPER_SAFE_JS
            StandaloneLog::Write(line);
#else
            if (hooks::g_LogFn)
                hooks::HLog(line);
#endif
#endif
        }
    }

    bool IsReadable(const void* address, std::size_t size) noexcept
    {
        if (!address || !size)
            return false;
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT ||
            (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;
        const auto begin =
            reinterpret_cast<std::uintptr_t>(address);
        const auto end =
            reinterpret_cast<std::uintptr_t>(memory.BaseAddress) +
            memory.RegionSize;
        return begin <= end && size <= end - begin;
    }

    bool IsExecutableAddress(std::uintptr_t address) noexcept
    {
        if (!address)
            return false;
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(
                reinterpret_cast<const void*>(address),
                &memory,
                sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT ||
            (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;
        const DWORD protection =
            memory.Protect & 0xFFU;
        return protection == PAGE_EXECUTE ||
            protection == PAGE_EXECUTE_READ ||
            protection == PAGE_EXECUTE_READWRITE ||
            protection == PAGE_EXECUTE_WRITECOPY;
    }

    std::uintptr_t ReadPointer(std::uintptr_t address) noexcept
    {
        if (!IsReadable(
                reinterpret_cast<const void*>(address),
                sizeof(std::uintptr_t)))
            return 0;
        return *reinterpret_cast<const std::uintptr_t*>(address);
    }

    std::uint32_t ReadU32(std::uintptr_t address) noexcept
    {
        if (!IsReadable(
                reinterpret_cast<const void*>(address),
                sizeof(std::uint32_t)))
            return 0;
        return *reinterpret_cast<const std::uint32_t*>(address);
    }

    std::uint8_t ReadU8(std::uintptr_t address) noexcept
    {
        if (!IsReadable(
                reinterpret_cast<const void*>(address),
                sizeof(std::uint8_t)))
            return 0;
        return *reinterpret_cast<const std::uint8_t*>(address);
    }

    bool MatchesRuntimeFingerprints(std::uintptr_t base) noexcept
    {
        for (const auto& fingerprint : kRuntimeFingerprints)
        {
            const auto* actual =
                reinterpret_cast<const std::uint8_t*>(
                    base + fingerprint.rva);
            if (!IsReadable(actual, sizeof(fingerprint.bytes)))
                return false;
            for (std::size_t index = 0;
                 index < sizeof(fingerprint.bytes);
                 ++index)
            {
                if (actual[index] != fingerprint.bytes[index])
                    return false;
            }
        }
        return true;
    }

    std::uint16_t TaggedType(std::uintptr_t tagged) noexcept
    {
        if ((tagged & 1ULL) == 0)
            return 0;
        const auto map = ReadPointer(tagged - 1);
        if ((map & 1ULL) == 0 ||
            !IsReadable(
                reinterpret_cast<const void*>(map + 0xB),
                sizeof(std::uint16_t)))
            return 0;
        return *reinterpret_cast<const std::uint16_t*>(map + 0xB);
    }

    void RecordBootstrapCallback(
        std::uintptr_t callback,
        std::uintptr_t data,
        std::uintptr_t caller) noexcept
    {
        if (!IsExecutableAddress(callback))
            return;
        const LONG count = InterlockedCompareExchange(
            &g_BootstrapCallbackCount,
            0,
            0);
        for (LONG index = 0; index < count; ++index)
        {
            if (g_BootstrapCallbacks[index] == callback)
                return;
        }
        if (count >= static_cast<LONG>(
                kBootstrapCallbackCapacity))
            return;
        g_BootstrapCallbacks[count] = callback;
        g_BootstrapCallbackData[count] = data;
        g_BootstrapCallbackCallers[count] = caller;
        InterlockedExchange(
            &g_BootstrapCallbackCount,
            count + 1);
    }

    struct ClientIdLocal final
    {
        HANDLE process{};
        HANDLE thread{};
    };

    struct ThreadBasicInformationLocal final
    {
        LONG exitStatus{};
        void* tebBaseAddress{};
        ClientIdLocal clientId{};
        ULONG_PTR affinityMask{};
        LONG priority{};
        LONG basePriority{};
    };

    using NtQueryInformationThreadFn =
        LONG(NTAPI*)(HANDLE, ULONG, void*, ULONG, ULONG*);

    void* ReadTlsValue(void* teb, DWORD key) noexcept
    {
        if (!teb || key == TLS_OUT_OF_INDEXES)
            return nullptr;
        auto* bytes = static_cast<std::uint8_t*>(teb);
        if (key < 64)
        {
            const auto address =
                reinterpret_cast<std::uintptr_t>(
                    bytes + 0x1480 + key * sizeof(void*));
            return reinterpret_cast<void*>(ReadPointer(address));
        }
        const DWORD index = key - 64;
        if (index >= 1024)
            return nullptr;
        const auto expansion = ReadPointer(
            reinterpret_cast<std::uintptr_t>(bytes + 0x1780));
        if (!expansion)
            return nullptr;
        return reinterpret_cast<void*>(
            ReadPointer(expansion + index * sizeof(void*)));
    }

    bool IsValidIsolate(void* candidate) noexcept
    {
        if (!candidate)
            return false;
        const auto address =
            reinterpret_cast<std::uintptr_t>(candidate);
        const auto globalHandles =
            ReadPointer(address + 0xA768);
        if (!globalHandles)
            return false;
        return ReadPointer(globalHandles + 0x08) != 0;
    }

    void* FindIsolate(
        std::uintptr_t moduleBase,
        DWORD& ownerThreadId) noexcept
    {
        ownerThreadId = 0;
        const DWORD key = ReadU32(moduleBase + kIsolateTlsKeyRva);
        if (key == TLS_OUT_OF_INDEXES)
            return nullptr;

        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        const auto queryThread = ntdll
            ? reinterpret_cast<NtQueryInformationThreadFn>(
                GetProcAddress(ntdll, "NtQueryInformationThread"))
            : nullptr;
        if (!queryThread)
            return nullptr;

        HANDLE snapshot =
            CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return nullptr;

        void* found = nullptr;
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        if (Thread32First(snapshot, &entry))
        {
            do
            {
                if (entry.th32OwnerProcessID != GetCurrentProcessId())
                    continue;
                HANDLE thread = OpenThread(
                    THREAD_QUERY_INFORMATION,
                    FALSE,
                    entry.th32ThreadID);
                if (!thread)
                {
                    thread = OpenThread(
                        THREAD_QUERY_LIMITED_INFORMATION,
                        FALSE,
                        entry.th32ThreadID);
                }
                if (!thread)
                    continue;

                ThreadBasicInformationLocal information{};
                const LONG status = queryThread(
                    thread,
                    0,
                    &information,
                    sizeof(information),
                    nullptr);
                CloseHandle(thread);
                if (status < 0 || !information.tebBaseAddress)
                    continue;

                void* isolate = ReadTlsValue(
                    information.tebBaseAddress,
                    key);
                if (!IsValidIsolate(isolate))
                {
                    for (DWORD slot = 0; slot < 64; ++slot)
                    {
                        isolate = ReadTlsValue(
                            information.tebBaseAddress,
                            slot);
                        if (IsValidIsolate(isolate))
                            break;
                    }
                }
                if (IsValidIsolate(isolate))
                {
                    found = isolate;
                    ownerThreadId = entry.th32ThreadID;
                    break;
                }
            } while (Thread32Next(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return found;
    }

    bool ResolveApi() noexcept
    {
        HMODULE module =
            GetModuleHandleW(RageMpBuildProfile::kModuleName);
        if (!module)
            return false;
        if (!RageMpBuildProfile::MatchesLoadedModule(module))
        {
            SetState(
                RageV8Runtime::State::Error,
                "Unsupported multiplayer.dll build");
            return false;
        }

        g_Api = {};
        g_TaskRunner = nullptr;
        g_TaskRunnerControl = nullptr;
        InterlockedExchange(&g_TaskInUse[0], 0);
        InterlockedExchange(&g_TaskInUse[1], 0);
        InterlockedExchange(&g_PumpQueued, 0);
        g_Api.base = reinterpret_cast<std::uintptr_t>(module);
        if (!MatchesRuntimeFingerprints(g_Api.base))
        {
            SetState(
                RageV8Runtime::State::Error,
                "multiplayer.dll V8 opcode profile mismatch");
            return false;
        }
        g_Api.platform = *reinterpret_cast<void**>(
            g_Api.base + kPlatformSingletonRva);
        g_Api.isolate =
            FindIsolate(g_Api.base, g_Api.ownerThreadId);
        g_Api.handleScopeCtor =
            reinterpret_cast<HandleScopeCtor>(
                g_Api.base + kHandleScopeCtorRva);
        g_Api.handleScopeDtor =
            reinterpret_cast<HandleScopeDtor>(
                g_Api.base + kHandleScopeDtorRva);
        g_Api.newFromUtf8 =
            reinterpret_cast<StringNewFromUtf8>(
                g_Api.base + kStringNewFromUtf8Rva);
        g_Api.valueToUtf8 =
            reinterpret_cast<ValueToUtf8>(
                g_Api.base + kValueToUtf8Rva);
        g_Api.cleanupUtf8 =
            reinterpret_cast<Utf8Cleanup>(
                g_Api.base + kUtf8CleanupRva);
        g_Api.objectGetByKey =
            reinterpret_cast<ObjectGetByKey>(
                g_Api.base + kObjectGetByKeyRva);
        g_Api.objectSetByKey =
            reinterpret_cast<ObjectSetByKey>(
                g_Api.base + kObjectSetByKeyRva);
        g_Api.functionCall =
            reinterpret_cast<FunctionCall>(
                g_Api.base + kFunctionCallRva);
        g_Api.globalizeReference =
            reinterpret_cast<GlobalizeReference>(
                g_Api.base + kGlobalizeReferenceRva);

        const bool valid =
            g_Api.platform &&
            g_Api.isolate &&
            g_Api.ownerThreadId &&
            g_Api.handleScopeCtor &&
            g_Api.handleScopeDtor &&
            g_Api.newFromUtf8 &&
            g_Api.valueToUtf8 &&
            g_Api.cleanupUtf8 &&
            g_Api.objectGetByKey &&
            g_Api.objectSetByKey &&
            g_Api.functionCall &&
            g_Api.globalizeReference;
        if (!valid)
        {
            InterlockedExchange(&g_InitializeStarted, 0);
            SetState(
                RageV8Runtime::State::Uninitialized,
                "multiplayer.dll loaded; waiting for V8 owner thread");
            return false;
        }
        InterlockedExchange(&g_BuildMatched, 1);
        return true;
    }

    LONG CALLBACK RootCaptureHandler(EXCEPTION_POINTERS* pointers)
    {
        if (!pointers ||
            !pointers->ExceptionRecord ||
            !pointers->ContextRecord ||
            pointers->ExceptionRecord->ExceptionCode !=
                EXCEPTION_SINGLE_STEP)
            return EXCEPTION_CONTINUE_SEARCH;

        CONTEXT* context = pointers->ContextRecord;
        const LONG stage =
            InterlockedCompareExchange(&g_CaptureStage, 0, 0);
        if (stage == 1 &&
            context->Rip == g_FunctionTemplateNew)
        {
            // Win64 MaybeLocal return ABI:
            // RCX=out, RDX=isolate, R8=FunctionCallback.
            const std::uintptr_t caller = ReadPointer(
                static_cast<std::uintptr_t>(
                    context->Rsp));
            RecordBootstrapCallback(
                static_cast<std::uintptr_t>(context->R8),
                static_cast<std::uintptr_t>(context->R9),
                caller);
            context->Dr6 = 0;
            context->EFlags |= 0x10000;
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        if (context->Rip == g_CaptureEntry &&
            InterlockedCompareExchange(
                &g_RootCaptured,
                0,
                0) != 0)
        {
            context->Dr0 = 0;
            context->Dr6 = 0;
            context->Dr7 &=
                ~static_cast<DWORD64>(0x000F0003ULL);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (stage == 0 && context->Rip == g_CaptureEntry)
        {
            g_CapturedRootLocal = context->Rcx;
            g_ApiStringsRuntimeBase =
                static_cast<std::uintptr_t>(
                    context->Rdx);
            const auto snapshotStart =
                g_ApiStringsRuntimeBase >
                    kApiStringsBefore
                    ? g_ApiStringsRuntimeBase -
                        kApiStringsBefore
                    : 0;
            if (snapshotStart &&
                IsReadable(
                    reinterpret_cast<const void*>(
                        snapshotStart),
                    sizeof(g_ApiStringsSnapshot)))
            {
                __try {
                    CopyMemory(
                        g_ApiStringsSnapshot,
                        reinterpret_cast<const void*>(
                            snapshotStart),
                        sizeof(g_ApiStringsSnapshot));
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    ZeroMemory(
                        g_ApiStringsSnapshot,
                        sizeof(g_ApiStringsSnapshot));
                }
            }
            g_PreBootstrapDr3 =
                static_cast<std::uintptr_t>(context->Dr3);
            g_PreBootstrapDr3Control =
                context->Dr7 &
                static_cast<DWORD64>(0xF00000C0ULL);
            context->Dr0 = g_CaptureReturn;
            context->Dr3 = g_FunctionTemplateNew;
            context->Dr6 = 0;
            context->Dr7 &= ~static_cast<DWORD64>(0x000F0003ULL);
            context->Dr7 &=
                ~static_cast<DWORD64>(0xF00000C0ULL);
            context->Dr7 |= 0x41;
            InterlockedExchange(&g_CaptureStage, 1);
            return EXCEPTION_CONTINUE_EXECUTION;
        }

        if (stage == 1 && context->Rip == g_CaptureReturn)
        {
            const auto rootTagged =
                ReadPointer(g_CapturedRootLocal);
            if ((rootTagged & 1ULL) != 0)
            {
                std::uintptr_t local = rootTagged;
                g_RootPersistent =
                    g_Api.globalizeReference(
                        g_Api.isolate,
                        &local);
            }
            context->Dr0 = 0;
            context->Dr3 = g_PreBootstrapDr3;
            context->Dr6 = 0;
            context->Dr7 &=
                ~static_cast<DWORD64>(0x000F0003ULL);
            context->Dr7 &=
                ~static_cast<DWORD64>(0xF00000C0ULL);
            context->Dr7 |= g_PreBootstrapDr3Control;
            InterlockedExchange(&g_CaptureStage, 2);
            if (g_RootPersistent)
                InterlockedExchange(&g_RootCaptured, 1);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    using RtlAddVectoredExceptionHandlerFn =
        void*(NTAPI*)(ULONG, PVECTORED_EXCEPTION_HANDLER);
    using SuspendThreadFn = DWORD(WINAPI*)(HANDLE);
    using ResumeThreadFn = DWORD(WINAPI*)(HANDLE);
    using GetThreadContextFn = BOOL(WINAPI*)(HANDLE, CONTEXT*);
    using SetThreadContextFn =
        BOOL(WINAPI*)(HANDLE, const CONTEXT*);

    bool InstallRootCapture() noexcept
    {
        if (!g_Api.base || !g_Api.ownerThreadId ||
            g_Api.ownerThreadId == GetCurrentThreadId())
            return false;

        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        const auto addVeh = ntdll
            ? reinterpret_cast<RtlAddVectoredExceptionHandlerFn>(
                GetProcAddress(
                    ntdll,
                    "RtlAddVectoredExceptionHandler"))
            : nullptr;
        const auto suspendThread = kernel32
            ? reinterpret_cast<SuspendThreadFn>(
                GetProcAddress(kernel32, "SuspendThread"))
            : nullptr;
        const auto resumeThread = kernel32
            ? reinterpret_cast<ResumeThreadFn>(
                GetProcAddress(kernel32, "ResumeThread"))
            : nullptr;
        const auto getThreadContext = kernel32
            ? reinterpret_cast<GetThreadContextFn>(
                GetProcAddress(kernel32, "GetThreadContext"))
            : nullptr;
        const auto setThreadContext = kernel32
            ? reinterpret_cast<SetThreadContextFn>(
                GetProcAddress(kernel32, "SetThreadContext"))
            : nullptr;
        if (!addVeh || !suspendThread || !resumeThread ||
            !getThreadContext || !setThreadContext)
            return false;

        if (!g_VehHandle)
            g_VehHandle = addVeh(1, RootCaptureHandler);
        if (!g_VehHandle)
            return false;

        g_CaptureEntry = g_Api.base + kApiBootstrapRva;
        g_CaptureReturn = g_Api.base + kApiBootstrapReturnRva;
        g_FunctionTemplateNew =
            g_Api.base + kFunctionTemplateNewRva;
        g_CapturedRootLocal = 0;
        g_PreBootstrapDr3 = 0;
        g_PreBootstrapDr3Control = 0;
        for (std::uint32_t index = 0;
             index < kBootstrapCallbackCapacity;
             ++index)
        {
            g_BootstrapCallbacks[index] = 0;
            g_BootstrapCallbackCallers[index] = 0;
            g_BootstrapCallbackData[index] = 0;
        }
        g_ApiStringsRuntimeBase = 0;
        ZeroMemory(
            g_ApiStringsSnapshot,
            sizeof(g_ApiStringsSnapshot));
        InterlockedExchange(&g_BootstrapCallbackCount, 0);
        InterlockedExchange(&g_CaptureStage, 0);

        HANDLE thread = OpenThread(
            THREAD_SUSPEND_RESUME |
            THREAD_GET_CONTEXT |
            THREAD_SET_CONTEXT |
            THREAD_QUERY_INFORMATION,
            FALSE,
            g_Api.ownerThreadId);
        if (!thread)
            return false;

        bool installed = false;
        if (suspendThread(thread) != static_cast<DWORD>(-1))
        {
            CONTEXT context{};
            context.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (getThreadContext(thread, &context))
            {
                context.Dr0 = g_CaptureEntry;
                context.Dr6 = 0;
                context.Dr7 &=
                    ~static_cast<DWORD64>(0x000F0003ULL);
                context.Dr7 |= 1;
                installed =
                    setThreadContext(thread, &context) != FALSE;
            }
            resumeThread(thread);
        }
        CloseHandle(thread);
        if (installed)
            g_LastCaptureArmAttempt = GetTickCount64();
        return installed;
    }

    bool PopCommand(Command& command) noexcept
    {
        Lock(g_CommandLock);
        if (!g_CommandCount)
        {
            Unlock(g_CommandLock);
            return false;
        }
        Command& slot = g_Commands[g_CommandRead];
        command.id = slot.id;
        command.generation = slot.generation;
        // Buffers are swapped rather than copied: the slot keeps owning one,
        // and a pop stays O(1) instead of moving up to a megabyte.
        char* const returned = command.script;
        command.script = slot.script;
        slot.script = returned;
        slot.id = 0;
        slot.generation = 0;
        if (slot.script)
            slot.script[0] = 0;
        g_CommandRead =
            (g_CommandRead + 1) % kCommandCapacity;
        --g_CommandCount;
        Unlock(g_CommandLock);
        return command.script != nullptr;
    }

    bool PopCameraCommand(CameraCommand& command) noexcept
    {
        if (InterlockedCompareExchange(
                &g_CameraCommandPending, 0, 0) == 0)
            return false;
        Lock(g_CameraCommandLock);
        if (InterlockedExchange(
                &g_CameraCommandPending, 0) == 0)
        {
            Unlock(g_CameraCommandLock);
            return false;
        }
        command = g_CameraCommand;
        Unlock(g_CameraCommandLock);
        return true;
    }

    void PushResult(
        std::uint64_t commandId,
        bool success,
        const char* text) noexcept
    {
        Lock(g_ResultLock);
        if (g_ResultCount == kResultCapacity)
        {
            g_ResultRead =
                (g_ResultRead + 1) % kResultCapacity;
            --g_ResultCount;
        }
        auto& result = g_Results[g_ResultWrite];
        result = {};
        result.commandId = commandId;
        result.success = success;
        CopyText(result.text, sizeof(result.text), text);
        g_ResultWrite =
            (g_ResultWrite + 1) % kResultCapacity;
        ++g_ResultCount;
        Unlock(g_ResultLock);
    }

    std::uintptr_t CurrentContextValue() noexcept
    {
        if (!g_Api.isolate)
            return 0;
        return ReadPointer(
            reinterpret_cast<std::uintptr_t>(
                g_Api.isolate) + 0x3348);
    }

    std::uint64_t CurrentGeneration() noexcept
    {
        return static_cast<std::uint64_t>(
            InterlockedCompareExchange64(
                &g_RootGeneration,
                0,
                0));
    }

    void ResetRootScan() noexcept
    {
        g_GlobalHandleBlock = 0;
        g_GlobalHandleScanContext = 0;
        g_GlobalHandleBlocksScanned = 0;
        g_GlobalHandleScanComplete = false;
        g_LastGlobalHandleScan = 0;
    }

    void BindCurrentRoot(
        std::uintptr_t contextValue) noexcept
    {
        if (!contextValue || !g_RootPersistent)
            return;
        InterlockedExchange64(
            &g_BoundContext,
            static_cast<LONG64>(contextValue));
        if (CurrentGeneration() == 0)
        {
            InterlockedCompareExchange64(
                &g_RootGeneration,
                1,
                0);
        }
        g_GlobalHandleScanContext = contextValue;
    }

    void InvalidateRoot(
        const char* message) noexcept
    {
        {
            // The fatal sequence starts with a generation change, which only
            // happens here, and the fault lands on a thread that is not the
            // pump's. Naming the caller and its thread is what ties the two
            // together instead of guessing at the link.
            char line[224]{};
            _snprintf(
                line,
                sizeof(line) - 1,
                "V8pump: InvalidateRoot thread=%lu owner=%lu reason=%s",
                GetCurrentThreadId(),
                g_Api.ownerThreadId,
                message ? message : "?");
            hooks::HLog(line);
        }
        SetState(
            RageV8Runtime::State::WaitingForApi,
            message);
        g_RootScanNotBefore = GetTickCount64() + 750;
        InterlockedExchange(&g_RootCaptured, 0);
        InterlockedExchange(&g_CaptureStage, 0);
        InterlockedExchange64(&g_BoundContext, 0);
        InterlockedIncrement64(&g_RootGeneration);
        g_RootPersistent = nullptr;
        g_CapturedRootLocal = 0;
        ResetRootScan();

        Lock(g_CommandLock);
        const auto dropped = g_CommandCount;
        g_CommandRead = 0;
        g_CommandWrite = 0;
        g_CommandCount = 0;
        for (auto& command : g_Commands)
            ResetCommand(command);
        Unlock(g_CommandLock);
        if (dropped)
        {
            InterlockedExchangeAdd64(
                &g_DroppedCommands,
                static_cast<LONG64>(dropped));
        }

        Lock(g_ResultLock);
        g_ResultRead = 0;
        g_ResultWrite = 0;
        g_ResultCount = 0;
        for (auto& result : g_Results)
            result = {};
        Unlock(g_ResultLock);

        Lock(g_CameraCommandLock);
        g_CameraCommand = {};
        InterlockedExchange(&g_CameraCommandPending, 0);
        InterlockedExchange(&g_CameraAimRequested, 0);
        Unlock(g_CameraCommandLock);
        ResetCommand(g_ExecutingCommand);
    }

    bool OnV8OwnerThread() noexcept
    {
        return g_Api.ownerThreadId != 0 &&
            g_Api.ownerThreadId == GetCurrentThreadId() &&
            g_Api.isolate != nullptr;
    }

    bool HandleHasProperty(
        void* handle,
        void* context,
        const char* name) noexcept
    {
        if (!OnV8OwnerThread() ||
            !g_Api.newFromUtf8 ||
            !g_Api.objectGetByKey)
            return false;
        void* key = nullptr;
        void* value = nullptr;
        g_Api.newFromUtf8(
            &key,
            g_Api.isolate,
            name,
            kV8StringNormal,
            -1);
        if (!key)
            return false;
        g_Api.objectGetByKey(
            handle,
            &value,
            context,
            key);
        if (!value)
            return false;
        const auto tagged =
            ReadPointer(reinterpret_cast<std::uintptr_t>(value));
        return TaggedType(tagged) >= 0xB8;
    }

    bool ValidateRoot() noexcept
    {
        if (!OnV8OwnerThread() || !g_RootPersistent)
            return false;
        const auto tagged = ReadPointer(
            reinterpret_cast<std::uintptr_t>(
                g_RootPersistent));
        if (TaggedType(tagged) < 0xB8)
            return false;

        std::uint8_t scope[0x30]{};
        g_Api.handleScopeCtor(scope, g_Api.isolate);
        std::uintptr_t contextValue = ReadPointer(
            reinterpret_cast<std::uintptr_t>(
                g_Api.isolate) + 0x3348);
        void* context = &contextValue;
        const bool valid =
            contextValue &&
            HandleHasProperty(
                g_RootPersistent,
                context,
                "_players") &&
            HandleHasProperty(
                g_RootPersistent,
                context,
                "_vehicles") &&
            HandleHasProperty(
                g_RootPersistent,
                context,
                "events") &&
            HandleHasProperty(
                g_RootPersistent,
                context,
                "game");
        g_Api.handleScopeDtor(scope);
        return valid;
    }

    bool DiscoverExistingRoot() noexcept
    {
        if (!OnV8OwnerThread())
            return false;
        if (!g_Api.isolate)
            return false;

        std::uintptr_t contextValue =
            CurrentContextValue();
        if (!contextValue)
            return false;
        if (InterlockedCompareExchange(
                &g_WaitingForContextChange,
                0,
                0) != 0)
        {
            const auto previousContext =
                static_cast<std::uintptr_t>(
                    InterlockedCompareExchange64(
                        &g_ReconnectPreviousContext,
                        0,
                        0));
            if (!previousContext ||
                contextValue == previousContext)
                return false;
            InterlockedExchange(
                &g_WaitingForContextChange,
                0);
            g_RootScanNotBefore =
                GetTickCount64() + 750;
            ResetRootScan();
            SetState(
                RageV8Runtime::State::WaitingForApi,
                "New V8 context observed; stabilizing RAGE API");
        }
        if (GetTickCount64() < g_RootScanNotBefore)
            return false;
        if (g_GlobalHandleScanContext != contextValue)
        {
            g_GlobalHandleBlock = 0;
            g_GlobalHandleScanContext = contextValue;
            g_GlobalHandleBlocksScanned = 0;
            g_GlobalHandleScanComplete = false;
            g_LastGlobalHandleScan = 0;
        }

        if (!g_GlobalHandleBlock)
        {
            const ULONGLONG now = GetTickCount64();
#ifdef RESOURCE_DUMPER_SAFE_JS
            constexpr ULONGLONG retryInterval = 100;
#else
            constexpr ULONGLONG retryInterval = 250;
#endif
            if (g_GlobalHandleScanComplete &&
                now - g_LastGlobalHandleScan < retryInterval)
                return false;
            g_LastGlobalHandleScan = now;
            g_GlobalHandleScanComplete = false;

            const auto globalHandles = ReadPointer(
                reinterpret_cast<std::uintptr_t>(
                    g_Api.isolate) + 0xA768);
            const auto regularSpace =
                ReadPointer(globalHandles + 0x08);
            g_GlobalHandleBlock =
                ReadPointer(regularSpace + 0x08);
            g_GlobalHandleBlocksScanned = 0;
            if (!g_GlobalHandleBlock)
            {
                g_GlobalHandleScanComplete = true;
                SetState(
                    RageV8Runtime::State::WaitingForApi,
                    "V8 global handles not ready; retrying scan");
                return false;
            }
            SetState(
                RageV8Runtime::State::WaitingForApi,
                "Scanning existing V8 persistent handles");
        }

        std::uint8_t scope[0x30]{};
        g_Api.handleScopeCtor(scope, g_Api.isolate);
        void* context = &contextValue;

#ifdef RESOURCE_DUMPER_SAFE_JS
        constexpr int scanBlockBudget = 2;
#else
        constexpr int scanBlockBudget = 8;
#endif
        for (int blockBudget = 0;
            blockBudget < scanBlockBudget && g_GlobalHandleBlock;
            ++blockBudget)
        {
            const auto block = g_GlobalHandleBlock;
            for (std::uint32_t index = 0;
                index < 256;
                ++index)
            {
                const auto node =
                    block + index * 0x20;
                const auto flags = ReadU8(node + 0x0B);
                if ((flags & 0x07) != 1)
                    continue;
                const auto tagged = ReadPointer(node);
                if (TaggedType(tagged) != 0x421)
                    continue;

                void* handle =
                    reinterpret_cast<void*>(node);
                if (!HandleHasProperty(
                        handle,
                        context,
                        "_players") ||
                    !HandleHasProperty(
                        handle,
                        context,
                        "_vehicles") ||
                    !HandleHasProperty(
                        handle,
                        context,
                        "events") ||
                    !HandleHasProperty(
                        handle,
                        context,
                        "game"))
                    continue;

                g_RootPersistent = handle;
                BindCurrentRoot(contextValue);
                InterlockedExchange(&g_RootCaptured, 1);
                g_Api.handleScopeDtor(scope);
                SetState(
                    RageV8Runtime::State::Ready,
                    "Existing RAGE API root discovered; bridge ready");
                return true;
            }

            g_GlobalHandleBlock =
                ReadPointer(block + 0x2000);
            ++g_GlobalHandleBlocksScanned;
            if (g_GlobalHandleBlocksScanned > 4096)
            {
                g_GlobalHandleBlock = 0;
                break;
            }
        }
        g_Api.handleScopeDtor(scope);

        if (!g_GlobalHandleBlock)
        {
            g_GlobalHandleBlocksScanned = 0;
            g_GlobalHandleScanComplete = true;
            g_LastGlobalHandleScan = GetTickCount64();
            SetState(
                RageV8Runtime::State::WaitingForApi,
                "RAGE API root not present; retrying scan");
        }
        return false;
    }

    bool PrepareWrappedSource(const char* script) noexcept
    {
        if (!g_WrappedSource)
            g_WrappedSource = AllocateScriptBuffer();
        if (!g_WrappedSource)
            return false;
        g_WrappedSource[0] = 0;
        AppendText(
            g_WrappedSource,
            RageV8Runtime::kMaxScriptLength,
            "(function(mp){try{\n");
        AppendText(
            g_WrappedSource,
            RageV8Runtime::kMaxScriptLength,
            script);
        AppendText(
            g_WrappedSource,
            RageV8Runtime::kMaxScriptLength,
            "\n}catch(e){return 'BridgeError: '+"
            "(e&&e.stack?e.stack:e);}})");
        return lstrlenA(g_WrappedSource) <
            static_cast<int>(
                RageV8Runtime::kMaxScriptLength) - 1;
    }

    void FormatResult(
        void* result,
        void* context,
        char* output,
        std::size_t capacity) noexcept
    {
        if (!result)
        {
            CopyText(output, capacity, "<empty result>");
            return;
        }
        const auto tagged = ReadPointer(
            reinterpret_cast<std::uintptr_t>(result));
        if ((tagged & 1ULL) == 0)
        {
            const auto value =
                static_cast<std::int64_t>(tagged) >> 32;
            _snprintf(
                output,
                capacity - 1,
                "%lld",
                static_cast<long long>(value));
            output[capacity - 1] = 0;
            return;
        }

        const auto type = TaggedType(tagged);
        if (type == 0x42 &&
            IsReadable(
                reinterpret_cast<const void*>(
                    tagged - 1 + 0x08),
                sizeof(double)))
        {
            const double value =
                *reinterpret_cast<const double*>(
                    tagged - 1 + 0x08);
            _snprintf(output, capacity - 1, "%.15g", value);
            output[capacity - 1] = 0;
            return;
        }

        UNREFERENCED_PARAMETER(context);
        Utf8Buffer converted{};
        g_Api.valueToUtf8(
            &converted,
            g_Api.isolate,
            result);
        if (!converted.data || converted.length < 0)
        {
            _snprintf(
                output,
                capacity - 1,
                "<value type=0x%X>",
                static_cast<unsigned>(type));
            output[capacity - 1] = 0;
            return;
        }
        std::size_t length =
            static_cast<std::size_t>(converted.length);
        if (length >= capacity)
            length = capacity - 1;
        CopyMemory(output, converted.data, length);
        output[length] = 0;
        g_Api.cleanupUtf8(&converted);
    }

    // Which of our subsystems queued a script. Used to name the one that
    // blocks the game's frame: the pump runs on the thread that drives it, so
    // a slow script is a stutter.
    const char* ClassifyScript(const char* script) noexcept
    {
        if (!script)
            return "none";
        if (std::strstr(script, "__butterflyVis"))
            return "player-snapshot";
        if (std::strstr(script, "__butterflyWorld"))
            return "world-snapshot";
        if (std::strstr(script, "__butterflyAimState"))
            return "camera-aim";
        if (std::strstr(script, "__eventLogger"))
            return "event-logger";
        if (std::strstr(script, "__quantTools"))
            return "quant-tools";
        if (std::strstr(script, "__ukraineTools"))
            return "ukraine-tools";
        if (std::strstr(script, "__farm"))
            return "farm-bot";
        if (std::strstr(script, "adminScan") ||
            std::strstr(script, "__butterflyAdmin"))
            return "admin-scan";
        return "other";
    }

    void ExecuteCommand(const Command& command) noexcept
    {
        RageV8Runtime::Result result{};
        result.commandId = command.id;
        if (!command.script)
        {
            if (command.id)
            {
                PushResult(
                    command.id,
                    false,
                    "Canceled: no script buffer available");
            }
            return;
        }
#if RAGEMP_DEV_BUILD && !defined(RESOURCE_DUMPER_SAFE_JS)
        const bool traceCommand =
            InterlockedIncrement(&g_TraceCommandCount) <= 16;
        const char* traceKind =
            std::strstr(command.script, "__butterflyVis")
                ? "player-snapshot"
            : (std::strstr(
                    command.script,
                    "__eventLoggerUnhook")
                ? "logger-install"
            : (std::strstr(
                    command.script,
                    "__eventLoggerDrain")
                ? "logger-drain" : "other"));
        auto traceStage =
            [&](const char* stage) noexcept {
                if (!traceCommand)
                    return;
                char message[192]{};
                _snprintf(
                    message,
                    sizeof(message) - 1,
                    "V8CMD: id=%llu gen=%llu kind=%s len=%d stage=%s",
                    static_cast<unsigned long long>(command.id),
                    static_cast<unsigned long long>(
                        command.generation),
                    traceKind,
                    lstrlenA(command.script),
                    stage);
                hooks::HLog(message);
            };
        traceStage("begin");
#endif

        if (!OnV8OwnerThread())
        {
            if (command.id)
            {
                PushResult(
                    command.id,
                    false,
                    "Canceled: V8 API called off owner thread");
            }
            return;
        }

        if (static_cast<RageV8Runtime::State>(
                InterlockedCompareExchange(
                    &g_State,
                    0,
                    0)) != RageV8Runtime::State::Ready ||
            InterlockedCompareExchange(
                &g_RootCaptured,
                0,
                0) == 0)
        {
            if (command.id)
            {
                PushResult(
                    command.id,
                    false,
                    "Canceled: V8 bridge not ready");
            }
            return;
        }

        const auto boundContext =
            static_cast<std::uintptr_t>(
                InterlockedCompareExchange64(
                    &g_BoundContext,
                    0,
                    0));
        const auto liveContext = CurrentContextValue();
        if (!liveContext ||
            (boundContext && liveContext != boundContext))
        {
            if (command.id)
            {
                PushResult(
                    command.id,
                    false,
                    "Canceled: V8 context changed");
            }
            return;
        }

        if (!PrepareWrappedSource(command.script))
        {
            if (command.id)
            {
                PushResult(
                    command.id,
                    false,
                    "Script is too large after wrapper");
            }
            return;
        }
#if RAGEMP_DEV_BUILD && !defined(RESOURCE_DUMPER_SAFE_JS)
        traceStage("wrapped");
#endif

        std::uint8_t scope[0x30]{};
        g_Api.handleScopeCtor(scope, g_Api.isolate);
#if RAGEMP_DEV_BUILD && !defined(RESOURCE_DUMPER_SAFE_JS)
        traceStage("scope");
#endif
        std::uintptr_t contextValue = liveContext;
        std::uintptr_t globalValue = contextValue
            ? ReadPointer((contextValue - 1) + 0x20)
            : 0;
        void* context = &contextValue;
        void* global = &globalValue;

        void* evalKey = nullptr;
        void* evalFunction = nullptr;
        void* sourceString = nullptr;
        void* wrapperFunction = nullptr;
        void* finalResult = nullptr;
        bool success = false;

        if (contextValue && globalValue)
        {
            g_Api.newFromUtf8(
                &evalKey,
                g_Api.isolate,
                "eval",
                kV8StringNormal,
                -1);
        }
        if (evalKey)
        {
            g_Api.objectGetByKey(
                global,
                &evalFunction,
                context,
                evalKey);
        }
        if (evalFunction)
        {
            g_Api.newFromUtf8(
                &sourceString,
                g_Api.isolate,
                g_WrappedSource,
                kV8StringNormal,
                -1);
        }
        if (sourceString)
        {
#if RAGEMP_DEV_BUILD && !defined(RESOURCE_DUMPER_SAFE_JS)
            traceStage("eval-call");
#endif
            void* arguments[1]{sourceString};
            g_Api.functionCall(
                evalFunction,
                &wrapperFunction,
                context,
                global,
                1,
                arguments);
        }
        if (wrapperFunction)
        {
#if RAGEMP_DEV_BUILD && !defined(RESOURCE_DUMPER_SAFE_JS)
            traceStage("wrapper-call");
#endif
#ifdef RESOURCE_DUMPER_SAFE_JS
            if (g_RootPersistent)
            {
                void* arguments[1]{g_RootPersistent};
                g_Api.functionCall(
                    wrapperFunction,
                    &finalResult,
                    context,
                    global,
                    1,
                    arguments);
            }
            else
            {
                g_Api.functionCall(
                    wrapperFunction,
                    &finalResult,
                    context,
                    global,
                    0,
                    nullptr);
            }
#else
            void* arguments[1]{g_RootPersistent};
            g_Api.functionCall(
                wrapperFunction,
                &finalResult,
                context,
                global,
                1,
                arguments);
#endif
        }

        if (finalResult)
        {
            FormatResult(
                finalResult,
                context,
                result.text,
                sizeof(result.text));
            success = !StartsWith(
                result.text,
                "BridgeError:");
        }
        else
        {
            CopyText(
                result.text,
                sizeof(result.text),
                "JavaScript compile or call returned empty");
        }
        g_Api.handleScopeDtor(scope);
#if RAGEMP_DEV_BUILD && !defined(RESOURCE_DUMPER_SAFE_JS)
        traceStage(success ? "done-ok" : "done-empty");
#endif

        if (command.id)
            PushResult(command.id, success, result.text);
        else if (success)
            InterlockedIncrement64(&g_CameraAimExecuted);
        InterlockedIncrement64(&g_ExecutedCommands);
    }

    bool HasFiveRpPlayersMethods(
        const RageV8Runtime::NativeContext& context,
        void* receiver) noexcept
    {
        if (!receiver)
            return false;
        constexpr const char* required[]{
            "getName",
            "setName",
            "getServerFactionId",
            "getServerFactionType"
        };
        for (const char* name : required)
        {
            void* function = nullptr;
            if (!RageV8Runtime::GetProperty(
                    context,
                    receiver,
                    name,
                    &function) ||
                RageV8Runtime::LocalInstanceType(function) !=
                    V8Internal87::kJsFunctionType)
                return false;
        }
        return true;
    }

    bool TryExposeFiveRpPlayersCandidate(
        const RageV8Runtime::NativeContext& context,
        std::uintptr_t receiverValue) noexcept
    {
        const auto receiverType = TaggedType(receiverValue);
        if (receiverType < 0x420 || receiverType > 0x447)
            return false;

        for (int index = 0;
             index < g_FiveRpPlayersProbe.seenReceiverCount;
             ++index)
        {
            if (g_FiveRpPlayersProbe.seenReceivers[index] ==
                receiverValue)
                return false;
        }
        if (g_FiveRpPlayersProbe.seenReceiverCount <
            static_cast<int>(_countof(
                g_FiveRpPlayersProbe.seenReceivers)))
        {
            g_FiveRpPlayersProbe.seenReceivers[
                g_FiveRpPlayersProbe.seenReceiverCount++] =
                    receiverValue;
        }

        std::uintptr_t apiValue = 0;
        std::uintptr_t playersValue = receiverValue;
        void* players = &playersValue;
        if (!HasFiveRpPlayersMethods(context, players))
        {
            void* nested = nullptr;
            if (!RageV8Runtime::GetProperty(
                    context,
                    players,
                    "players",
                    &nested) ||
                !HasFiveRpPlayersMethods(context, nested))
                return false;
            playersValue = ReadPointer(
                reinterpret_cast<std::uintptr_t>(nested));
            if (!playersValue)
                return false;
            players = &playersValue;
            apiValue = receiverValue;
        }

        const auto exposeOn =
            [&](void* target) noexcept
            {
                if (!target)
                    return false;
                if (!RageV8Runtime::SetProperty(
                        context,
                        target,
                        "__butterflyFiveRpPlayers",
                        players))
                    return false;
                void* readBack = nullptr;
                return RageV8Runtime::GetProperty(
                           context,
                           target,
                           "__butterflyFiveRpPlayers",
                           &readBack) &&
                    HasFiveRpPlayersMethods(
                        context,
                        readBack);
            };
        const bool rootPlayers = exposeOn(context.root);
        const bool globalPlayers =
            !context.global ||
            context.global == context.root ||
            exposeOn(context.global);
        if (!rootPlayers || !globalPlayers)
            return false;
        g_FiveRpPlayersProbe.playersExposed = true;
#if RAGEMP_DEV_BUILD
        if (hooks::g_LogFn)
            hooks::HLog(
                "RageV8Runtime: exposed validated 5RP players");
#endif

        // apiValue is only set when the candidate was the api wrapper and
        // players was nested inside it. When the scan lands on api.players
        // directly there is no way back up to the parent, so treat the api
        // alias as a bonus: the JS resolvers only ever need api.players.
        if (apiValue)
        {
            void* api = &apiValue;
            const auto exposeApiOn =
                [&](void* target) noexcept
                {
                    if (!target ||
                        !RageV8Runtime::SetProperty(
                            context,
                            target,
                            "__butterflyFiveRpApi",
                            api))
                        return false;
                    void* readBack = nullptr;
                    void* nestedPlayers = nullptr;
                    return RageV8Runtime::GetProperty(
                               context,
                               target,
                               "__butterflyFiveRpApi",
                               &readBack) &&
                        RageV8Runtime::GetProperty(
                            context,
                            readBack,
                            "players",
                            &nestedPlayers) &&
                        HasFiveRpPlayersMethods(
                            context,
                            nestedPlayers);
                };
            const bool rootApi = exposeApiOn(context.root);
            const bool globalApi =
                !context.global ||
                context.global == context.root ||
                exposeApiOn(context.global);
            g_FiveRpPlayersProbe.apiExposed =
                rootApi && globalApi;
#if RAGEMP_DEV_BUILD
            if (g_FiveRpPlayersProbe.apiExposed &&
                hooks::g_LogFn)
                hooks::HLog(
                    "RageV8Runtime: exposed validated 5RP api alias");
#endif
        }
        g_FiveRpPlayersProbe.exposed =
            g_FiveRpPlayersProbe.playersExposed &&
            g_FiveRpPlayersProbe.dataKeysExposed;
        return true;
    }

    // 5RP publishes the names of its shared variables in mp.serverDataKeys,
    // and mp.serverDataKeys.isAdmin is the only way to know which variable
    // marks an admin. The catch is that several mp instances coexist after a
    // reconnect and only the live one carries a filled-in table, so an empty
    // isAdmin means this candidate is a stale duplicate. Unlike the player
    // pool, which is native and therefore shared, this table is a plain JS
    // property and cannot be reached from the instance our scripts run in.
    bool TryExposeFiveRpDataKeysCandidate(
        const RageV8Runtime::NativeContext& context,
        std::uintptr_t receiverValue) noexcept
    {
        if (g_FiveRpPlayersProbe.dataKeysExposed)
            return false;
        const auto receiverType = TaggedType(receiverValue);
        if (receiverType < 0x420 || receiverType > 0x447)
            return false;

        std::uintptr_t receiverSlot = receiverValue;
        void* receiver = &receiverSlot;
        void* keys = nullptr;
        if (!RageV8Runtime::GetProperty(
                context,
                receiver,
                "serverDataKeys",
                &keys) ||
            !keys)
            return false;
        void* isAdmin = nullptr;
        if (!RageV8Runtime::GetProperty(
                context,
                keys,
                "isAdmin",
                &isAdmin) ||
            !isAdmin)
            return false;
        char keyText[64]{};
        if (!RageV8Runtime::ValueToUtf8(
                isAdmin,
                keyText,
                sizeof(keyText)) ||
            !keyText[0])
            return false;

        const auto exposeOn =
            [&](void* target) noexcept
            {
                if (!target)
                    return false;
                if (!RageV8Runtime::SetProperty(
                        context,
                        target,
                        "__butterflyFiveRpDataKeys",
                        keys))
                    return false;
                void* readBack = nullptr;
                void* value = nullptr;
                char check[64]{};
                return RageV8Runtime::GetProperty(
                           context,
                           target,
                           "__butterflyFiveRpDataKeys",
                           &readBack) &&
                    RageV8Runtime::GetProperty(
                        context,
                        readBack,
                        "isAdmin",
                        &value) &&
                    RageV8Runtime::ValueToUtf8(
                        value,
                        check,
                        sizeof(check)) &&
                    check[0] != 0;
            };
        const bool rootKeys = exposeOn(context.root);
        const bool globalKeys =
            !context.global ||
            context.global == context.root ||
            exposeOn(context.global);
        if (!rootKeys || !globalKeys)
            return false;
        g_FiveRpPlayersProbe.dataKeysExposed = true;
#if RAGEMP_DEV_BUILD
        if (hooks::g_LogFn)
            hooks::HLog(
                "RageV8Runtime: exposed validated 5RP data keys");
#endif
        g_FiveRpPlayersProbe.exposed =
            g_FiveRpPlayersProbe.playersExposed &&
            g_FiveRpPlayersProbe.dataKeysExposed;
        return true;
    }

    void ResetFiveRpPlayersProbe(
        const RageV8Runtime::NativeContext& context) noexcept
    {
        g_FiveRpPlayersProbe = {};
        g_FiveRpPlayersProbe.generation = context.generation;
        const auto globalHandles = ReadPointer(
            reinterpret_cast<std::uintptr_t>(
                context.isolate) + 0xA768);
        const auto regular = globalHandles
            ? ReadPointer(globalHandles + 0x08) : 0;
        g_FiveRpPlayersProbe.block = regular
            ? ReadPointer(regular + 0x08) : 0;
    }

    void ProbeFiveRpPlayers(
        const RageV8Runtime::NativeContext& context) noexcept
    {
        const ULONGLONG now = GetTickCount64();
        if (g_FiveRpPlayersProbe.generation !=
            context.generation)
            ResetFiveRpPlayersProbe(context);
        if (g_FiveRpPlayersProbe.exposed ||
            now < g_FiveRpPlayersProbe.retryAt)
            return;
        if (!g_FiveRpPlayersProbe.block)
        {
            g_FiveRpPlayersProbe.retryAt = now + 5000;
            ResetFiveRpPlayersProbe(context);
            g_FiveRpPlayersProbe.retryAt = now + 5000;
            return;
        }

        // 8 nodes per tick made the first exposure take minutes on a full
        // heap, and until it lands no name resolver works (the overlay falls
        // back to placeholders). The scan is read-only and stops at the first
        // exposure, so a large budget only shortens the pre-exposure phase:
        // one whole block per tick pins the api within the first seconds.
        constexpr int kNodeBudget = 256;
        int processed = 0;
        while (g_FiveRpPlayersProbe.block &&
               g_FiveRpPlayersProbe.block !=
                   g_FiveRpPlayersProbe.previousBlock &&
               g_FiveRpPlayersProbe.blockIndex < 256 &&
               processed < kNodeBudget &&
               !g_FiveRpPlayersProbe.exposed)
        {
            while (g_FiveRpPlayersProbe.nodeIndex < 256 &&
                   processed < kNodeBudget &&
                   !g_FiveRpPlayersProbe.exposed)
            {
                const auto node =
                    g_FiveRpPlayersProbe.block +
                    g_FiveRpPlayersProbe.nodeIndex * 0x20;
                ++g_FiveRpPlayersProbe.nodeIndex;
                ++processed;
                if ((ReadU8(node + 0x0B) & 0x07) != 1)
                    continue;
                const auto functionTagged = ReadPointer(node);
                if (TaggedType(functionTagged) !=
                    V8Internal87::kJsFunctionType)
                    continue;

                std::uintptr_t contextTagged = ReadPointer(
                    V8Internal87::Untag(functionTagged) + 0x20);
                for (int depth = 0;
                     depth < 8 && contextTagged &&
                     !g_FiveRpPlayersProbe.exposed;
                     ++depth)
                {
                    const auto contextType =
                        TaggedType(contextTagged);
                    if (contextType < 136 ||
                        contextType > 145)
                        break;
                    bool seen = false;
                    for (int index = 0;
                         index <
                             g_FiveRpPlayersProbe.seenContextCount;
                         ++index)
                    {
                        if (g_FiveRpPlayersProbe.seenContexts[
                                index] == contextTagged)
                        {
                            seen = true;
                            break;
                        }
                    }
                    if (seen)
                        break;
                    if (g_FiveRpPlayersProbe.seenContextCount <
                        static_cast<int>(_countof(
                            g_FiveRpPlayersProbe.seenContexts)))
                    {
                        g_FiveRpPlayersProbe.seenContexts[
                            g_FiveRpPlayersProbe.seenContextCount++] =
                                contextTagged;
                    }

                    const auto contextAddress =
                        V8Internal87::Untag(contextTagged);
                    const auto lengthTagged =
                        ReadPointer(contextAddress + 0x08);
                    if (V8Internal87::IsHeapObject(lengthTagged))
                        break;
                    int length = static_cast<int>(
                        static_cast<std::int64_t>(
                            lengthTagged) >> 32);
                    if (length <= 0 || length > 4096)
                        break;
                    if (length > 96)
                        length = 96;
                    for (int slot = 0;
                         slot < length &&
                         !g_FiveRpPlayersProbe.exposed;
                         ++slot)
                    {
                        const auto receiverValue = ReadPointer(
                            contextAddress + 0x10 +
                            slot * sizeof(std::uintptr_t));
                        TryExposeFiveRpPlayersCandidate(
                            context,
                            receiverValue);
                        TryExposeFiveRpDataKeysCandidate(
                            context,
                            receiverValue);
                    }
                    const auto previous =
                        ReadPointer(contextAddress + 0x18);
                    contextTagged = previous != contextTagged
                        ? previous : 0;
                }
            }
            if (g_FiveRpPlayersProbe.nodeIndex >= 256)
            {
                const auto current =
                    g_FiveRpPlayersProbe.block;
                const auto next =
                    ReadPointer(current + 0x2000);
                g_FiveRpPlayersProbe.previousBlock = current;
                g_FiveRpPlayersProbe.block = next;
                g_FiveRpPlayersProbe.nodeIndex = 0;
                ++g_FiveRpPlayersProbe.blockIndex;
            }
        }
        if (!g_FiveRpPlayersProbe.exposed &&
            (!g_FiveRpPlayersProbe.block ||
             g_FiveRpPlayersProbe.block ==
                 g_FiveRpPlayersProbe.previousBlock ||
             g_FiveRpPlayersProbe.blockIndex >= 256))
        {
            ResetFiveRpPlayersProbe(context);
            g_FiveRpPlayersProbe.retryAt = now + 5000;
        }
    }

    void ExecuteOwnerThreadTick() noexcept
    {
        auto callback = reinterpret_cast<
            RageV8Runtime::OwnerThreadTick>(
                InterlockedCompareExchangePointer(
                    &g_OwnerThreadTick,
                    nullptr,
                    nullptr));
        // The probe must run even with no tick callback registered: the
        // callback is only installed by the dev-build event logger, while
        // the 5RP players alias is needed by every name resolver.
        if (!OnV8OwnerThread() || !g_RootPersistent)
            return;
        // Nothing to do once the alias is published and no callback wants
        // the tick, so skip the per-pump handle scope entirely.
        if (!callback &&
            g_FiveRpPlayersProbe.exposed &&
            g_FiveRpPlayersProbe.generation == CurrentGeneration())
            return;

        const auto contextValue = CurrentContextValue();
        if (!contextValue)
            return;
        std::uintptr_t contextSlot = contextValue;
        std::uintptr_t globalSlot = ReadPointer(
            (contextValue - 1) + 0x20);
        std::uint8_t scope[0x30]{};
        g_Api.handleScopeCtor(scope, g_Api.isolate);
        RageV8Runtime::NativeContext native{};
        native.isolate = g_Api.isolate;
        native.context = &contextSlot;
        native.global = globalSlot
            ? &globalSlot : nullptr;
        native.root = g_RootPersistent;
        native.moduleBase = g_Api.base;
        native.generation = CurrentGeneration();
        native.ownerThreadId = g_Api.ownerThreadId;
        // Kept in its own guard so a probe fault cannot tear down the
        // event logger callback, and a callback fault cannot stall the probe.
        __try {
            ProbeFiveRpPlayers(native);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            InterlockedIncrement64(&g_NativeTickFaults);
            g_FiveRpPlayersProbe.retryAt =
                GetTickCount64() + 5000;
        }
        __try {
            if (callback)
                callback(native);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            InterlockedIncrement64(&g_NativeTickFaults);
            if (callback)
            {
                InterlockedExchangePointer(
                    &g_OwnerThreadTick,
                    nullptr);
            }
        }
        g_Api.handleScopeDtor(scope);
    }

    bool QueueNextTask(RuntimeTask* current) noexcept;

    void __fastcall DeleteTask(
        RuntimeTask* task,
        unsigned int) noexcept
    {
        if (task && task->slot < 2 &&
            task == &g_Tasks[task->slot])
        {
            InterlockedExchange(
                &g_TaskInUse[task->slot],
                0);
        }
    }

    void __fastcall RunTask(RuntimeTask* task) noexcept
    {
        auto state = static_cast<RageV8Runtime::State>(
            InterlockedCompareExchange(&g_State, 0, 0));


        // Soft-pause only blocks work. Never InvalidateRoot / touch V8
        // on the F1 edge — RAGE tears the context down concurrently and
        // that race is what lands in ntdll+0x33FFA.
        if (InterlockedExchange(
                &g_ReconnectRequested,
                0) != 0)
        {
            state = RageV8Runtime::State::WaitingForApi;
        }

        if (state == RageV8Runtime::State::Ready &&
            InterlockedCompareExchange(
                &g_RootCaptured,
                0,
                0) != 0 &&
            InterlockedCompareExchange(
                &g_V8Paused,
                0,
                0) == 0)
        {
            const auto boundContext =
                static_cast<std::uintptr_t>(
                    InterlockedCompareExchange64(
                        &g_BoundContext,
                        0,
                        0));
            const auto currentContext =
                CurrentContextValue();
            if (boundContext &&
                currentContext != boundContext)
            {
                InvalidateRoot(
                    "V8 context changed; rebinding RAGE API");
                InterlockedExchange(
                    &g_WaitingForContextChange,
                    1);
                InterlockedExchange(&g_V8Paused, 0);
                state = RageV8Runtime::State::WaitingForApi;
            }
        }

        if (InterlockedCompareExchange(
                &g_WaitingForContextChange,
                0,
                0) != 0)
        {
            const auto previousContext =
                static_cast<std::uintptr_t>(
                    InterlockedCompareExchange64(
                        &g_ReconnectPreviousContext,
                        0,
                        0));
            const auto currentContext =
                CurrentContextValue();
            if (previousContext &&
                currentContext &&
                currentContext != previousContext)
            {
                InterlockedExchange(
                    &g_WaitingForContextChange,
                    0);
                InterlockedExchange(&g_V8Paused, 0);
                g_RootScanNotBefore =
                    GetTickCount64() + 1500;
                ResetRootScan();
                SetState(
                    RageV8Runtime::State::WaitingForApi,
                    "New V8 context observed; stabilizing RAGE API");
                state = RageV8Runtime::State::WaitingForApi;
            }
            else
            {
                // Still on the dying/old context — idle the pump.
                if (!QueueNextTask(task))
                {
                    InterlockedExchange(&g_PumpQueued, 0);
                    SetState(
                        RageV8Runtime::State::Error,
                        "V8 task pump stopped");
                }
                return;
            }
        }

        if (state == RageV8Runtime::State::WaitingForApi &&
            InterlockedCompareExchange(
                &g_RootCaptured,
                0,
                0) == 0 &&
            InterlockedCompareExchange(
                &g_WaitingForContextChange,
                0,
                0) == 0)
        {
            DiscoverExistingRoot();
        }

        if (InterlockedCompareExchange(
                &g_RootCaptured,
                0,
                0) != 0 &&
            static_cast<RageV8Runtime::State>(
                InterlockedCompareExchange(
                    &g_State,
                    0,
                    0)) != RageV8Runtime::State::Ready)
        {
            if (ValidateRoot())
            {
                BindCurrentRoot(CurrentContextValue());
                InterlockedExchange(&g_V8Paused, 0);
                SetState(
                    RageV8Runtime::State::Ready,
                    "RAGE API captured; bridge ready");
            }
            else
            {
                g_RootPersistent = nullptr;
                InterlockedExchange(&g_RootCaptured, 0);
                InterlockedExchange64(&g_BoundContext, 0);
                ResetRootScan();
                SetState(
                    RageV8Runtime::State::WaitingForApi,
                    "Captured API root failed validation");
            }
        }

        const auto executionState =
            static_cast<RageV8Runtime::State>(
                InterlockedCompareExchange(
                    &g_State,
                    0,
                    0));
        bool mayExecute =
            executionState == RageV8Runtime::State::Ready &&
            InterlockedCompareExchange(
                &g_RootCaptured,
                0,
                0) != 0 &&
            InterlockedCompareExchange(
                &g_V8Paused,
                0,
                0) == 0 &&
            InterlockedCompareExchange(
                &g_WaitingForContextChange,
                0,
                0) == 0;
#ifdef RESOURCE_DUMPER_SAFE_JS
        mayExecute =
            mayExecute ||
            executionState ==
                RageV8Runtime::State::WaitingForApi;
#endif
        if (mayExecute)
        {
            const auto generation = CurrentGeneration();
            const auto ownerProfile =
                RuntimeProfiler::Begin();
            ExecuteOwnerThreadTick();
            RuntimeProfiler::End(
                RuntimeProfiler::Scope::V8OwnerTick,
                ownerProfile);
            CameraCommand cameraCommand{};
            if (PopCameraCommand(cameraCommand))
            {
                if (cameraCommand.generation == generation)
                {
                    Command wrapped{};
                    wrapped.generation = cameraCommand.generation;
                    wrapped.script = cameraCommand.script;
                    __try {
                        ExecuteCommand(wrapped);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                        SetState(
                            RageV8Runtime::State::WaitingForApi,
                            "V8 execute faulted; waiting for reconnect");
                        InterlockedExchange(&g_V8Paused, 1);
                        InterlockedExchange(
                            &g_WaitingForContextChange,
                            1);
                        InterlockedExchange(
                            &g_RootCaptured,
                            0);
                        g_RootPersistent = nullptr;
                    }
                }
            }
            if (PopCommand(g_ExecutingCommand))
            {
                const auto executeProfile =
                    RuntimeProfiler::Begin();
                if (g_ExecutingCommand.generation == generation)
                {
                    __try {
                        ExecuteCommand(g_ExecutingCommand);
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                        if (g_ExecutingCommand.id)
                        {
                            PushResult(
                                g_ExecutingCommand.id,
                                false,
                                "Canceled: V8 execute faulted");
                        }
                        SetState(
                            RageV8Runtime::State::WaitingForApi,
                            "V8 execute faulted; waiting for reconnect");
                        InterlockedExchange(&g_V8Paused, 1);
                        InterlockedExchange(
                            &g_WaitingForContextChange,
                            1);
                        InterlockedExchange(
                            &g_RootCaptured,
                            0);
                        g_RootPersistent = nullptr;
                    }
                }
                else
                {
                    PushResult(
                        g_ExecutingCommand.id,
                        false,
                        "Canceled: V8 context changed");
                }
                RuntimeProfiler::End(
                    RuntimeProfiler::Scope::V8Execute,
                    executeProfile);
#if RAGEMP_DEV_BUILD
                // Anything this long is a visible hitch, so name the script
                // that caused it instead of leaving a percentile behind.
                const double elapsedMs =
                    ElapsedMilliseconds(executeProfile);
                if (elapsedMs >= 4.0)
                {
                    char line[192]{};
                    _snprintf(
                        line,
                        sizeof(line) - 1,
                        "V8SLOW: kind=%s len=%d ms=%.1f",
                        ClassifyScript(g_ExecutingCommand.script),
                        g_ExecutingCommand.script
                            ? lstrlenA(g_ExecutingCommand.script)
                            : 0,
                        elapsedMs);
                    hooks::HLog(line);
                }
#endif
            }
        }

        if (!QueueNextTask(task))
        {
            InterlockedExchange(&g_PumpQueued, 0);
            SetState(
                RageV8Runtime::State::Error,
                "V8 task pump stopped");
        }
    }

    bool QueueNextTask(RuntimeTask* current) noexcept
    {
        if (!current || !g_TaskRunner)
            return false;
        const unsigned int slot = current->slot ^ 1U;
        if (InterlockedCompareExchange(
                &g_TaskInUse[slot],
                1,
                0) != 0)
            return false;

        RuntimeTask* next = &g_Tasks[slot];
        *next = {};
        next->vtable = &g_TaskVTable;
        next->slot = slot;
        RuntimeTask* pointer = next;
        auto** vtable =
            *reinterpret_cast<void***>(g_TaskRunner);
        const auto postDelayed =
            reinterpret_cast<PostDelayedTask>(vtable[2]);
        postDelayed(g_TaskRunner, &pointer, 0.05);
        if (pointer)
        {
            InterlockedExchange(&g_TaskInUse[slot], 0);
            return false;
        }
        return true;
    }

    bool StartTaskPump() noexcept
    {
        void* sharedRunner[2]{};
        auto** platformVTable =
            *reinterpret_cast<void***>(g_Api.platform);
        const auto getRunner =
            reinterpret_cast<GetForegroundTaskRunner>(
                platformVTable[5]);
        getRunner(
            g_Api.platform,
            sharedRunner,
            g_Api.isolate);
        g_TaskRunner = sharedRunner[0];
        g_TaskRunnerControl = sharedRunner[1];
        {
            char line[160]{};
            _snprintf(
                line,
                sizeof(line) - 1,
                "V8pump: runner=%p platform=%p isolate=%p owner=%lu "
                "caller=%lu",
                g_TaskRunner,
                g_Api.platform,
                g_Api.isolate,
                g_Api.ownerThreadId,
                GetCurrentThreadId());
            hooks::HLog(line);
        }
        if (!g_TaskRunner)
            return false;

        g_TaskVTable = {DeleteTask, RunTask};
        if (InterlockedCompareExchange(
                &g_TaskInUse[0],
                1,
                0) != 0)
            return false;
        RuntimeTask* task = &g_Tasks[0];
        *task = {};
        task->vtable = &g_TaskVTable;
        task->slot = 0;
        RuntimeTask* pointer = task;
        auto** runnerVTable =
            *reinterpret_cast<void***>(g_TaskRunner);
        const auto postNonNestable =
            reinterpret_cast<PostTask>(runnerVTable[1]);
        postNonNestable(g_TaskRunner, &pointer);
        if (pointer)
        {
            InterlockedExchange(&g_TaskInUse[0], 0);
            return false;
        }
        InterlockedExchange(&g_PumpQueued, 1);
        return true;
    }
}

namespace RageV8Runtime
{
    bool Initialize() noexcept
    {
        if (InterlockedCompareExchange(
                &g_InitializeStarted,
                1,
                0) != 0)
            return InterlockedCompareExchange(
                &g_PumpQueued,
                0,
                0) != 0;

        SetState(State::Resolving, "Resolving RAGE V8 runtime");
        g_LastInitializeAttempt = GetTickCount64();
        HMODULE module =
            GetModuleHandleW(RageMpBuildProfile::kModuleName);
        if (!module)
        {
            InterlockedExchange(&g_InitializeStarted, 0);
            SetState(
                State::Uninitialized,
                "Waiting for multiplayer.dll");
            return false;
        }
        if (!ResolveApi())
            return false;
        // Observe-only bootstrap capture. It is best-effort because the
        // current context may already exist; reconnect will hit it again.
        InstallRootCapture();
        if (!StartTaskPump())
        {
            InterlockedExchange(&g_InitializeStarted, 0);
            SetState(
                State::Uninitialized,
                "V8 owner found; waiting for foreground task runner");
            return false;
        }
        g_RootPersistent = nullptr;
        InterlockedExchange(&g_RootCaptured, 0);
        InterlockedExchange(&g_CaptureStage, 0);
        InterlockedExchange(&g_ReconnectRequested, 0);
        InterlockedExchange(&g_WaitingForContextChange, 0);
        InterlockedExchange(&g_V8Paused, 0);
        InterlockedExchange64(&g_BoundContext, 0);
        InterlockedExchange64(
            &g_ReconnectPreviousContext,
            0);
        g_RootScanNotBefore = 0;
        ResetRootScan();
        SetState(
            State::WaitingForApi,
            "Scanning existing V8 persistent handles");
        return true;
    }

    void PrepareForReconnect() noexcept
    {
        if (static_cast<State>(
                InterlockedCompareExchange(
                    &g_State,
                    0,
                    0)) != State::Ready ||
            InterlockedCompareExchange(
                &g_RootCaptured,
                0,
                0) == 0)
            return;

        constexpr ULONGLONG kReadyGraceMs = 8000;
        if (!g_ReadySince ||
            GetTickCount64() - g_ReadySince < kReadyGraceMs)
            return;

        auto context =
            static_cast<std::uintptr_t>(
                InterlockedCompareExchange64(
                    &g_BoundContext,
                    0,
                    0));
        if (!context)
            context = CurrentContextValue();
        if (!context)
            return;

        // Soft-pause only: drop our enqueue/execute side and wait for the
        // isolate context to actually change. Do NOT call InvalidateRoot /
        // V8 APIs here — that races RAGE teardown and faults in ntdll.
        InterlockedExchange64(
            &g_ReconnectPreviousContext,
            static_cast<LONG64>(context));
        InterlockedExchange(&g_V8Paused, 1);
        InterlockedExchange(&g_WaitingForContextChange, 1);
        InterlockedExchange(&g_RootCaptured, 0);
        g_RootPersistent = nullptr;
        InterlockedExchange64(&g_BoundContext, 0);
        InterlockedIncrement64(&g_RootGeneration);

        Lock(g_CommandLock);
        const auto dropped = g_CommandCount;
        g_CommandRead = 0;
        g_CommandWrite = 0;
        g_CommandCount = 0;
        for (auto& command : g_Commands)
            ResetCommand(command);
        Unlock(g_CommandLock);
        if (dropped)
        {
            InterlockedExchangeAdd64(
                &g_DroppedCommands,
                static_cast<LONG64>(dropped));
        }
        Lock(g_CameraCommandLock);
        g_CameraCommand = {};
        InterlockedExchange(&g_CameraCommandPending, 0);
        InterlockedExchange(&g_CameraAimRequested, 0);
        Unlock(g_CameraCommandLock);

        SetState(
            State::WaitingForApi,
            "Reconnect soft-pause; waiting for new V8 context");
        InterlockedExchange(&g_ReconnectRequested, 1);
    }

    void Update() noexcept
    {
        const State state = static_cast<State>(
            InterlockedCompareExchange(&g_State, 0, 0));
        const ULONGLONG now = GetTickCount64();
        if (state == State::Uninitialized)
        {
#ifdef RESOURCE_DUMPER_SAFE_JS
            constexpr ULONGLONG retryInterval = 100;
#elif defined(RESOURCE_DUMPER_STANDALONE)
            constexpr ULONGLONG retryInterval = 25;
#else
            constexpr ULONGLONG retryInterval = 1000;
#endif
            if (now - g_LastInitializeAttempt >= retryInterval)
                Initialize();
        }
        else if (state == State::WaitingForApi &&
            InterlockedCompareExchange(
                &g_RootCaptured,
                0,
                0) == 0 &&
            InterlockedCompareExchange(
                &g_WaitingForContextChange,
                0,
                0) == 0 &&
            now - g_LastCaptureArmAttempt >= 2000)
        {
            InstallRootCapture();
        }
    }

    void Retry() noexcept
    {
        const State state = static_cast<State>(
            InterlockedCompareExchange(&g_State, 0, 0));
        if (state == State::WaitingForApi && g_Api.base)
        {
            ResetRootScan();
            SetState(
                State::WaitingForApi,
                "RAGE API rescan requested");
            return;
        }
        if (state == State::Error &&
            InterlockedCompareExchange(&g_PumpQueued, 0, 0) == 0)
        {
            InterlockedExchange(&g_InitializeStarted, 0);
            InterlockedExchange(
                &g_State,
                static_cast<LONG>(State::Uninitialized));
            Initialize();
        }
    }

    bool Enqueue(
        const char* script,
        std::uint64_t* commandId) noexcept
    {
        if (!script || !script[0])
            return false;
        const int length = lstrlenA(script);
        if (length <= 0 ||
            length >= static_cast<int>(kMaxScriptLength) - 96)
            return false;
#ifndef RESOURCE_DUMPER_SAFE_JS
        if (static_cast<State>(
                InterlockedCompareExchange(
                    &g_State,
                    0,
                    0)) != State::Ready ||
            InterlockedCompareExchange(
                &g_RootCaptured,
                0,
                0) == 0)
            return false;
#endif

        const auto id = static_cast<std::uint64_t>(
            InterlockedIncrement64(&g_NextCommandId));
        Lock(g_CommandLock);
#ifndef RESOURCE_DUMPER_SAFE_JS
        if (static_cast<State>(
                InterlockedCompareExchange(
                    &g_State,
                    0,
                    0)) != State::Ready ||
            InterlockedCompareExchange(
                &g_RootCaptured,
                0,
                0) == 0)
        {
            Unlock(g_CommandLock);
            return false;
        }
#endif
        if (g_CommandCount == kCommandCapacity)
        {
            Unlock(g_CommandLock);
            InterlockedIncrement64(&g_DroppedCommands);
            return false;
        }
        Command& command = g_Commands[g_CommandWrite];
        if (!command.script)
            command.script = AllocateScriptBuffer();
        if (!command.script)
        {
            Unlock(g_CommandLock);
            InterlockedIncrement64(&g_DroppedCommands);
            return false;
        }
        command.id = id;
        command.generation = CurrentGeneration();
        CopyText(
            command.script,
            RageV8Runtime::kMaxScriptLength,
            script);
        g_CommandWrite =
            (g_CommandWrite + 1) % kCommandCapacity;
        ++g_CommandCount;
        Unlock(g_CommandLock);

        if (commandId)
            *commandId = id;
        return true;
    }

    bool EnqueueFireAndForget(
        const char* script) noexcept
    {
        if (!script || !script[0])
            return false;
        const int length = lstrlenA(script);
        if (length <= 0 ||
            length >= static_cast<int>(
                kMaxScriptLength) - 96)
            return false;
        if (static_cast<State>(
                InterlockedCompareExchange(
                    &g_State,
                    0,
                    0)) != State::Ready ||
            InterlockedCompareExchange(
                &g_RootCaptured,
                0,
                0) == 0)
            return false;

        Lock(g_CommandLock);
        if (static_cast<State>(
                InterlockedCompareExchange(
                    &g_State,
                    0,
                    0)) != State::Ready ||
            InterlockedCompareExchange(
                &g_RootCaptured,
                0,
                0) == 0 ||
            g_CommandCount == kCommandCapacity)
        {
            Unlock(g_CommandLock);
            InterlockedIncrement64(&g_DroppedCommands);
            return false;
        }
        Command& command = g_Commands[g_CommandWrite];
        if (!command.script)
            command.script = AllocateScriptBuffer();
        if (!command.script)
        {
            Unlock(g_CommandLock);
            InterlockedIncrement64(&g_DroppedCommands);
            return false;
        }
        command.id = 0;
        command.generation = CurrentGeneration();
        CopyText(
            command.script,
            RageV8Runtime::kMaxScriptLength,
            script);
        g_CommandWrite =
            (g_CommandWrite + 1) % kCommandCapacity;
        ++g_CommandCount;
        Unlock(g_CommandLock);
        return true;
    }

    bool SubmitCameraAim(
        float worldX,
        float worldY,
        float worldZ,
        float yawFactor,
        float pitchFactor) noexcept
    {
        if (!std::isfinite(worldX) ||
            !std::isfinite(worldY) ||
            !std::isfinite(worldZ) ||
            !std::isfinite(yawFactor) ||
            !std::isfinite(pitchFactor))
            return false;
        if (static_cast<State>(
                InterlockedCompareExchange(
                    &g_State, 0, 0)) != State::Ready ||
            InterlockedCompareExchange(
                &g_RootCaptured,
                0,
                0) == 0)
            return false;
        if (yawFactor < 0.01f) yawFactor = 0.01f;
        if (yawFactor > 1.0f) yawFactor = 1.0f;
        if (pitchFactor < 0.01f) pitchFactor = 0.01f;
        if (pitchFactor > 1.0f) pitchFactor = 1.0f;

        CameraCommand next{};
        next.generation = CurrentGeneration();
        const int written = sprintf_s(
            next.script,
            sizeof(next.script),
            "return (function(){const g=globalThis;"
            "let s=g.__butterflyAimState;"
            "if(!s){s=g.__butterflyAimState={active:false};"
            "mp.events.add('render',function(){"
            "const a=g.__butterflyAimState;"
            "if(!a||!a.active)return;"
            "if(Date.now()>a.expires){a.active=false;return;}"
            "const c=mp&&mp.game&&mp.game.cam;if(!c)return;"
            "const o=c.getGameplayCamCoord(),r=c.getGameplayCamRot(2);"
            "const dx=a.x-o.x,dy=a.y-o.y,dz=a.z-o.z;"
            "const flat=Math.sqrt(dx*dx+dy*dy);if(!(flat>0.001))return;"
            "const yaw=Math.atan2(-dx,dy)*57.295779513;"
            "const pitch=Math.atan2(dz,flat)*57.295779513;"
            "let yd=yaw-r.z;while(yd>180)yd-=360;"
            "while(yd<-180)yd+=360;const pd=pitch-r.x;"
            "const ys=Math.max(-12,Math.min(12,yd*a.yf));"
            "const ps=Math.max(-8,Math.min(8,pd*a.pf));"
            "c.setGameplayCamRelativeHeading("
            "c.getGameplayCamRelativeHeading()+ys);"
            "const setter=c.setGameplayRelativePitch||"
            "c.setGameplayCamRelativePitch;"
            "if(setter)setter.call(c,Math.max(-89,Math.min(89,"
            "c.getGameplayRelativePitch()+ps)),1.0);"
            "});}"
            "s.x=%.7g;s.y=%.7g;s.z=%.7g;"
            "s.yf=%.7g;s.pf=%.7g;"
            "s.expires=Date.now()+160;s.active=true;"
            "return true;})()",
            worldX, worldY, worldZ,
            yawFactor, pitchFactor);
        if (written <= 0 ||
            written >= static_cast<int>(sizeof(next.script)))
            return false;

        Lock(g_CameraCommandLock);
        g_CameraCommand = next;
        InterlockedExchange(&g_CameraCommandPending, 1);
        InterlockedExchange(&g_CameraAimRequested, 1);
        Unlock(g_CameraCommandLock);
        InterlockedIncrement64(&g_CameraAimSubmitted);
        return true;
    }

    void ClearCameraAim() noexcept
    {
        if (InterlockedExchange(
                &g_CameraAimRequested, 0) == 0 &&
            InterlockedCompareExchange(
                &g_CameraCommandPending, 0, 0) == 0)
            return;
        if (static_cast<State>(
                InterlockedCompareExchange(
                    &g_State,
                    0,
                    0)) != State::Ready ||
            InterlockedCompareExchange(
                &g_RootCaptured,
                0,
                0) == 0)
        {
            Lock(g_CameraCommandLock);
            g_CameraCommand = {};
            InterlockedExchange(&g_CameraCommandPending, 0);
            Unlock(g_CameraCommandLock);
            return;
        }
        CameraCommand clear{};
        clear.generation = CurrentGeneration();
        CopyText(
            clear.script,
            sizeof(clear.script),
            "return (function(){const s=globalThis."
            "__butterflyAimState;if(s)s.active=false;"
            "return true;})()");
        Lock(g_CameraCommandLock);
        g_CameraCommand = clear;
        InterlockedExchange(&g_CameraCommandPending, 1);
        Unlock(g_CameraCommandLock);
    }

    void SetOwnerThreadTick(OwnerThreadTick callback) noexcept
    {
        InterlockedExchangePointer(
            &g_OwnerThreadTick,
            reinterpret_cast<void*>(callback));
    }

    bool GetProperty(
        const NativeContext& context,
        void* object,
        const char* name,
        void** value) noexcept
    {
        if (value)
            *value = nullptr;
        if (!value || !object || !name || !name[0] ||
            !OnV8OwnerThread() ||
            context.ownerThreadId != GetCurrentThreadId() ||
            context.isolate != g_Api.isolate ||
            !context.context ||
            !g_Api.newFromUtf8 ||
            !g_Api.objectGetByKey)
            return false;
        void* key = nullptr;
        g_Api.newFromUtf8(
            &key,
            g_Api.isolate,
            name,
            kV8StringNormal,
            -1);
        if (!key)
            return false;
        g_Api.objectGetByKey(
            object,
            value,
            context.context,
            key);
        return *value != nullptr;
    }

    bool SetProperty(
        const NativeContext& context,
        void* object,
        const char* name,
        void* value) noexcept
    {
        if (!object || !name || !name[0] || !value ||
            !OnV8OwnerThread() ||
            context.ownerThreadId != GetCurrentThreadId() ||
            context.isolate != g_Api.isolate ||
            !context.context ||
            !g_Api.newFromUtf8 ||
            !g_Api.objectSetByKey)
            return false;
        void* key = nullptr;
        g_Api.newFromUtf8(
            &key,
            g_Api.isolate,
            name,
            kV8StringNormal,
            -1);
        if (!key)
            return false;
        std::uint16_t result = 0;
        g_Api.objectSetByKey(
            object,
            &result,
            context.context,
            key,
            value);
        return result != 0;
    }

    bool FindGlobalObjectWithFunction(
        const NativeContext& context,
        const char* name,
        void** object,
        void** function) noexcept
    {
        if (object)
            *object = nullptr;
        if (function)
            *function = nullptr;
        if (!object || !function || !name || !name[0] ||
            !OnV8OwnerThread() ||
            context.isolate != g_Api.isolate)
            return false;

        const auto globalHandles = ReadPointer(
            reinterpret_cast<std::uintptr_t>(
                g_Api.isolate) + 0xA768);
        const auto regular = globalHandles
            ? ReadPointer(globalHandles + 0x08) : 0;
        std::uintptr_t block = regular
            ? ReadPointer(regular + 0x08) : 0;
        std::uintptr_t previous = 0;
        for (std::uint32_t blockIndex = 0;
             block && block != previous &&
             blockIndex < 256;
             ++blockIndex)
        {
            for (int index = 0; index < 256; ++index)
            {
                const auto node =
                    block + index * 0x20;
                if ((ReadU8(node + 0x0B) & 0x07) != 1)
                    continue;
                const auto tagged = ReadPointer(node);
                const auto candidateType = TaggedType(tagged);
                // API hosts are not necessarily JS_OBJECT_TYPE (0x421).
                // RAGE also stores methods on JS_API_OBJECT / proxy /
                // special receiver instances from the same receiver range.
                if (candidateType < 0x420 ||
                    candidateType > 0x447)
                    continue;
                void* candidate =
                    reinterpret_cast<void*>(node);
                void* value = nullptr;
                if (!GetProperty(
                        context,
                        candidate,
                        name,
                        &value) ||
                    LocalInstanceType(value) != 0x444)
                    continue;
                *object = candidate;
                *function = value;
                return true;
            }
            previous = block;
            block = ReadPointer(block + 0x2000);
        }
        return false;
    }

    bool FindGlobalObjectWithFunctionResult(
        const NativeContext& context,
        const char* name,
        const char* argument,
        void** object,
        void** function,
        bool* probeMatched) noexcept
    {
        if (object)
            *object = nullptr;
        if (function)
            *function = nullptr;
        if (probeMatched)
            *probeMatched = false;
        if (!object || !function || !probeMatched ||
            !name || !name[0] ||
            !argument || !argument[0] ||
            !OnV8OwnerThread() ||
            context.isolate != g_Api.isolate ||
            !g_Api.newFromUtf8 ||
            !g_Api.functionCall)
            return false;

        void* probeArgument = nullptr;
        g_Api.newFromUtf8(
            &probeArgument,
            g_Api.isolate,
            argument,
            kV8StringNormal,
            -1);
        if (!probeArgument)
            return false;

        void* fallbackObject = nullptr;
        void* fallbackFunction = nullptr;
        const auto globalHandles = ReadPointer(
            reinterpret_cast<std::uintptr_t>(
                g_Api.isolate) + 0xA768);
        const auto regular = globalHandles
            ? ReadPointer(globalHandles + 0x08) : 0;
        std::uintptr_t block = regular
            ? ReadPointer(regular + 0x08) : 0;
        std::uintptr_t previous = 0;
        for (std::uint32_t blockIndex = 0;
             block && block != previous &&
             blockIndex < 256;
             ++blockIndex)
        {
            for (int index = 0; index < 256; ++index)
            {
                const auto node =
                    block + index * 0x20;
                if ((ReadU8(node + 0x0B) & 0x07) != 1)
                    continue;
                const auto tagged = ReadPointer(node);
                const auto candidateType = TaggedType(tagged);
                if (candidateType < 0x420 ||
                    candidateType > 0x447)
                    continue;
                void* candidate =
                    reinterpret_cast<void*>(node);
                void* value = nullptr;
                if (!GetProperty(
                        context,
                        candidate,
                        name,
                        &value) ||
                    LocalInstanceType(value) != 0x444)
                    continue;
                if (!fallbackObject)
                {
                    fallbackObject = candidate;
                    fallbackFunction = value;
                }

                void* callResult = nullptr;
                void* arguments[1]{probeArgument};
                g_Api.functionCall(
                    value,
                    &callResult,
                    context.context,
                    candidate,
                    1,
                    arguments);
                char text[192]{};
                if (callResult &&
                    ValueToUtf8(
                        callResult,
                        text,
                        sizeof(text)) &&
                    text[0] &&
                    lstrcmpA(text, "undefined") != 0 &&
                    lstrcmpA(text, "null") != 0 &&
                    lstrcmpA(text, argument) != 0)
                {
                    *object = candidate;
                    *function = value;
                    *probeMatched = true;
                    return true;
                }
            }
            previous = block;
            block = ReadPointer(block + 0x2000);
        }

        // Compiled resource modules keep the real 5RP helper in lexical
        // contexts rather than on a directly-global receiver. Walk
        // persistent JSFunctions, then their Context slots, and probe
        // receiver objects found there.
        if (lstrcmpA(name, "clientEvCrypt") == 0)
        {
            std::uintptr_t seenContexts[512]{};
            int seenContextCount = 0;
            auto scopeLocalIndex =
                [&](std::uintptr_t scopeTagged) noexcept
                {
                    if (TaggedType(scopeTagged) != 129)
                        return -1;
                    const auto scopeAddress =
                        V8Internal87::Untag(scopeTagged);
                    const auto lengthTagged =
                        ReadPointer(scopeAddress + 0x08);
                    if (V8Internal87::IsHeapObject(
                            lengthTagged))
                        return -1;
                    int length = static_cast<int>(
                        static_cast<std::int64_t>(
                            lengthTagged) >> 32);
                    if (length < 3 || length > 4096)
                        return -1;
                    const auto localCountTagged =
                        ReadPointer(scopeAddress + 0x20);
                    if (V8Internal87::IsHeapObject(
                            localCountTagged))
                        return -1;
                    int localCount = static_cast<int>(
                        static_cast<std::int64_t>(
                            localCountTagged) >> 32);
                    if (localCount <= 0 ||
                        localCount > 512 ||
                        localCount + 3 > length)
                        return -1;
                    for (int local = 0;
                         local < localCount;
                         ++local)
                    {
                        const auto item = ReadPointer(
                            scopeAddress + 0x28 +
                            local *
                                sizeof(std::uintptr_t));
                        if (TaggedType(item) >=
                            V8Internal87::kFirstNonStringType)
                            continue;
                        std::uintptr_t handleValue = item;
                        char text[96]{};
                        if (ValueToUtf8(
                                &handleValue,
                                text,
                                sizeof(text)) &&
                            lstrcmpA(text, name) == 0)
                            return local;
                    }
                    return -1;
                };

            block = regular
                ? ReadPointer(regular + 0x08) : 0;
            previous = 0;
            for (std::uint32_t blockIndex = 0;
                 block && block != previous &&
                 blockIndex < 256;
                 ++blockIndex)
            {
                for (int index = 0; index < 256; ++index)
                {
                    const auto node =
                        block + index * 0x20;
                    if ((ReadU8(node + 0x0B) & 0x07) != 1)
                        continue;
                    const auto functionTagged =
                        ReadPointer(node);
                    if (TaggedType(functionTagged) !=
                        V8Internal87::kJsFunctionType)
                        continue;
                    std::uintptr_t contextTagged =
                        ReadPointer(
                            V8Internal87::Untag(
                                functionTagged) +
                            0x20);
                    for (int depth = 0;
                         depth < 8 && contextTagged;
                         ++depth)
                    {
                        const auto contextType =
                            TaggedType(contextTagged);
                        if (contextType < 136 ||
                            contextType > 145)
                            break;
                        bool contextSeen = false;
                        for (int seen = 0;
                             seen < seenContextCount;
                             ++seen)
                        {
                            if (seenContexts[seen] ==
                                contextTagged)
                            {
                                contextSeen = true;
                                break;
                            }
                        }
                        if (contextSeen)
                            break;
                        if (seenContextCount <
                            static_cast<int>(
                                _countof(seenContexts)))
                        {
                            seenContexts[
                                seenContextCount++] =
                                    contextTagged;
                        }
                        const auto contextAddress =
                            V8Internal87::Untag(
                                contextTagged);
                        const auto lengthTagged =
                            ReadPointer(
                                contextAddress + 0x08);
                        if (V8Internal87::IsHeapObject(
                                lengthTagged))
                            break;
                        int length = static_cast<int>(
                            static_cast<std::int64_t>(
                                lengthTagged) >> 32);
                        if (length <= 0 || length > 4096)
                            break;
                        if (length > 128)
                            length = 128;
                        const auto scopeTagged =
                            ReadPointer(
                                contextAddress + 0x10);
                        const auto previousContext =
                            ReadPointer(
                                contextAddress + 0x18);
                        const int localIndex =
                            scopeLocalIndex(scopeTagged);
                        if (localIndex >= 0)
                        {
                            for (int headerLength = 2;
                                 headerLength <= 3;
                                 ++headerLength)
                            {
                                std::uintptr_t
                                    candidateValue =
                                        ReadPointer(
                                            contextAddress +
                                            0x10 +
                                            (headerLength +
                                             localIndex) *
                                                sizeof(
                                                    std::uintptr_t));
                                if (TaggedType(
                                        candidateValue) !=
                                    V8Internal87::
                                        kJsFunctionType)
                                    continue;
                                void* candidate =
                                    &candidateValue;
                                void* callResult = nullptr;
                                void* arguments[1]{
                                    probeArgument
                                };
                                void* receiver =
                                    context.global
                                        ? context.global
                                        : context.root;
                                g_Api.functionCall(
                                    candidate,
                                    &callResult,
                                    context.context,
                                    receiver,
                                    1,
                                    arguments);
                                char text[192]{};
                                if (!callResult ||
                                    !ValueToUtf8(
                                        callResult,
                                        text,
                                        sizeof(text)) ||
                                    !text[0] ||
                                    lstrcmpA(
                                        text,
                                        "undefined") == 0 ||
                                    lstrcmpA(
                                        text,
                                        "null") == 0 ||
                                    lstrcmpA(
                                        text,
                                        argument) == 0)
                                    continue;
                                void* persistent =
                                    g_Api.globalizeReference(
                                        g_Api.isolate,
                                        &candidateValue);
                                if (!persistent)
                                    continue;
                                *object = receiver;
                                *function = persistent;
                                *probeMatched = true;
                                return true;
                            }
                        }
                        contextTagged =
                            previousContext != contextTagged
                                ? previousContext : 0;
                    }
                }
                previous = block;
                block = ReadPointer(block + 0x2000);
            }
        }
        if (false)
        {
        std::uintptr_t seenContexts[512]{};
        int seenContextCount = 0;
        std::uintptr_t seenReceivers[1024]{};
        int seenReceiverCount = 0;
        block = regular ? ReadPointer(regular + 0x08) : 0;
        previous = 0;
        for (std::uint32_t blockIndex = 0;
             block && block != previous &&
             blockIndex < 256;
             ++blockIndex)
        {
            for (int index = 0; index < 256; ++index)
            {
                const auto node = block + index * 0x20;
                if ((ReadU8(node + 0x0B) & 0x07) != 1)
                    continue;
                const auto functionTagged = ReadPointer(node);
                if (TaggedType(functionTagged) != 0x444)
                    continue;
                for (std::uintptr_t contextOffset = 0x20;
                     contextOffset <= 0x40;
                     contextOffset += 0x08)
                {
                    std::uintptr_t contextTagged = ReadPointer(
                        V8Internal87::Untag(functionTagged) +
                        contextOffset);
                    const auto initialContextType =
                        TaggedType(contextTagged);
                    if (initialContextType < 136 ||
                        initialContextType > 145)
                        continue;
                    for (int depth = 0;
                         depth < 8 && contextTagged;
                         ++depth)
                    {
                        bool contextSeen = false;
                        for (int seen = 0;
                             seen < seenContextCount;
                             ++seen)
                        {
                            if (seenContexts[seen] == contextTagged)
                            {
                                contextSeen = true;
                                break;
                            }
                        }
                        if (contextSeen)
                            break;
                        if (seenContextCount <
                            static_cast<int>(
                                _countof(seenContexts)))
                            seenContexts[seenContextCount++] =
                                contextTagged;
                        const auto contextAddress =
                            V8Internal87::Untag(contextTagged);
                        const auto lengthTagged =
                            ReadPointer(contextAddress + 0x08);
                        if (V8Internal87::IsHeapObject(
                                lengthTagged))
                            break;
                        int length = static_cast<int>(
                            static_cast<std::int64_t>(
                                lengthTagged) >> 32);
                        if (length <= 0 || length > 4096)
                            break;
                        if (length > 96)
                            length = 96;
                        std::uintptr_t previousContext = 0;
                        for (int slot = 0; slot < length; ++slot)
                        {
                            std::uintptr_t receiverTagged =
                                ReadPointer(
                                    contextAddress + 0x10 +
                                    slot * sizeof(std::uintptr_t));
                            const auto receiverType =
                                TaggedType(receiverTagged);
                            if (receiverType >= 136 &&
                                receiverType <= 145 &&
                                receiverTagged != contextTagged &&
                                !previousContext)
                            {
                                previousContext = receiverTagged;
                            }
                            if (receiverType < 0x420 ||
                                receiverType > 0x447)
                                continue;
                            bool receiverSeen = false;
                            for (int seen = 0;
                                 seen < seenReceiverCount;
                                 ++seen)
                            {
                                if (seenReceivers[seen] ==
                                    receiverTagged)
                                {
                                    receiverSeen = true;
                                    break;
                                }
                            }
                            if (receiverSeen)
                                continue;
                            if (seenReceiverCount <
                                static_cast<int>(
                                    _countof(seenReceivers)))
                            {
                                seenReceivers[
                                    seenReceiverCount++] =
                                        receiverTagged;
                            }
                            std::uintptr_t receiverHandleValue =
                                receiverTagged;
                            void* receiverHandle =
                                &receiverHandleValue;
                            void* value = nullptr;
                            if (!GetProperty(
                                    context,
                                    receiverHandle,
                                    name,
                                    &value) ||
                                LocalInstanceType(value) != 0x444)
                                continue;
                            void* callResult = nullptr;
                            void* arguments[1]{probeArgument};
                            g_Api.functionCall(
                                value,
                                &callResult,
                                context.context,
                                receiverHandle,
                                1,
                                arguments);
                            char text[192]{};
                            if (!callResult ||
                                !ValueToUtf8(
                                    callResult,
                                    text,
                                    sizeof(text)) ||
                                !text[0] ||
                                lstrcmpA(text, "undefined") == 0 ||
                                lstrcmpA(text, "null") == 0 ||
                                lstrcmpA(text, argument) == 0)
                                continue;
                            void* persistent =
                                g_Api.globalizeReference(
                                    g_Api.isolate,
                                    &receiverHandleValue);
                            if (!persistent)
                                continue;
                            *object = persistent;
                            *function = value;
                            *probeMatched = true;
                            return true;
                        }
                        contextTagged = previousContext;
                    }
                }
            }
            previous = block;
            block = ReadPointer(block + 0x2000);
        }
        }
        *object = fallbackObject;
        *function = fallbackFunction;
        return fallbackObject != nullptr;
    }

    bool ExposeCurrentStackFunctionHost(
        const char* name,
        const char* argument,
        const char* hostProperty,
        const char* functionProperty) noexcept
    {
        if (!name || !name[0] ||
            !argument || !argument[0] ||
            !hostProperty || !hostProperty[0] ||
            !functionProperty || !functionProperty[0] ||
            !OnV8OwnerThread() ||
            !g_Api.isolate ||
            !g_Api.handleScopeCtor ||
            !g_Api.handleScopeDtor ||
            !g_Api.newFromUtf8 ||
            !g_Api.functionCall ||
            !g_RootPersistent)
            return false;

        const auto contextValue = CurrentContextValue();
        if (!contextValue)
            return false;
        std::uintptr_t contextSlot = contextValue;
        std::uintptr_t globalSlot = ReadPointer(
            V8Internal87::Untag(contextValue) + 0x20);
        if (!globalSlot)
            return false;

        std::uint8_t scope[0x30]{};
        g_Api.handleScopeCtor(scope, g_Api.isolate);
        NativeContext native{};
        native.isolate = g_Api.isolate;
        native.context = &contextSlot;
        native.global = &globalSlot;
        native.root = g_RootPersistent;
        native.moduleBase = g_Api.base;
        native.generation = CurrentGeneration();
        native.ownerThreadId = g_Api.ownerThreadId;

        void* probeArgument = nullptr;
        g_Api.newFromUtf8(
            &probeArgument,
            g_Api.isolate,
            argument,
            kV8StringNormal,
            -1);

        std::uintptr_t contexts[128]{};
        int contextCount = 0;
        std::uintptr_t receivers[256]{};
        int receiverCount = 0;
        const auto addContext =
            [&](std::uintptr_t tagged) noexcept
            {
                if (TaggedType(tagged) < 136 ||
                    TaggedType(tagged) > 145)
                    return;
                for (int index = 0;
                     index < contextCount;
                     ++index)
                {
                    if (contexts[index] == tagged)
                        return;
                }
                if (contextCount <
                    static_cast<int>(_countof(contexts)))
                    contexts[contextCount++] = tagged;
            };

        const auto start = reinterpret_cast<std::uintptr_t>(
            _AddressOfReturnAddress());
        ULONG_PTR stackLow = 0;
        ULONG_PTR stackHigh = 0;
        GetCurrentThreadStackLimits(&stackLow, &stackHigh);
        const auto stackBase =
            static_cast<std::uintptr_t>(stackHigh);
        const auto limit =
            start && stackBase > start &&
                start + 0x10000 < stackBase
                ? start + 0x10000 : stackBase;
        for (auto at = start;
             at && at + sizeof(std::uintptr_t) <= limit;
             at += sizeof(std::uintptr_t))
        {
            const std::uintptr_t raw =
                ReadPointer(at);
            if (!raw)
                continue;
            std::uintptr_t candidates[2]{raw, 0};
            if (raw >= 0x10000 &&
                raw <= 0x00007FFFFFFFFFFFULL)
                candidates[1] = ReadPointer(raw);
            for (const auto tagged : candidates)
            {
                const auto type = TaggedType(tagged);
                if (type >= 136 && type <= 145)
                {
                    addContext(tagged);
                }
                else if (type ==
                    V8Internal87::kJsFunctionType)
                {
                    addContext(ReadPointer(
                        V8Internal87::Untag(tagged) +
                        0x20));
                }
            }
        }

        bool exposed = false;
        for (int contextIndex = 0;
             contextIndex < contextCount && !exposed;
             ++contextIndex)
        {
            std::uintptr_t current =
                contexts[contextIndex];
            for (int depth = 0;
                 depth < 8 && current && !exposed;
                 ++depth)
            {
                const auto type = TaggedType(current);
                if (type < 136 || type > 145)
                    break;
                const auto address =
                    V8Internal87::Untag(current);
                const auto lengthTagged =
                    ReadPointer(address + 0x08);
                if (V8Internal87::IsHeapObject(
                        lengthTagged))
                    break;
                int length = static_cast<int>(
                    static_cast<std::int64_t>(
                        lengthTagged) >> 32);
                if (length <= 0 || length > 4096)
                    break;
                if (length > 128)
                    length = 128;
                for (int slot = 0;
                     slot < length && !exposed;
                     ++slot)
                {
                    std::uintptr_t receiverValue =
                        ReadPointer(
                            address + 0x10 +
                            slot *
                                sizeof(std::uintptr_t));
                    const auto receiverType =
                        TaggedType(receiverValue);
                    if (receiverType < 0x420 ||
                        receiverType > 0x447)
                        continue;
                    bool seen = false;
                    for (int receiverIndex = 0;
                         receiverIndex < receiverCount;
                         ++receiverIndex)
                    {
                        if (receivers[receiverIndex] ==
                            receiverValue)
                        {
                            seen = true;
                            break;
                        }
                    }
                    if (seen)
                        continue;
                    if (receiverCount <
                        static_cast<int>(
                            _countof(receivers)))
                        receivers[receiverCount++] =
                            receiverValue;

                    void* receiver = &receiverValue;
                    void* function = nullptr;
                    if (!GetProperty(
                            native,
                            receiver,
                            name,
                            &function) ||
                        LocalInstanceType(function) !=
                            V8Internal87::kJsFunctionType)
                        continue;
                    void* callResult = nullptr;
                    void* arguments[1]{
                        probeArgument
                    };
                    g_Api.functionCall(
                        function,
                        &callResult,
                        native.context,
                        receiver,
                        1,
                        arguments);
                    char text[192]{};
                    if (!callResult ||
                        !ValueToUtf8(
                            callResult,
                            text,
                            sizeof(text)) ||
                        !text[0] ||
                        lstrcmpA(text, "undefined") == 0 ||
                        lstrcmpA(text, "null") == 0 ||
                        lstrcmpA(text, argument) == 0)
                        continue;
                    exposed =
                        SetProperty(
                            native,
                            native.global,
                            hostProperty,
                            receiver) &&
                        SetProperty(
                            native,
                            native.global,
                            functionProperty,
                            function);
                }
                const auto previous =
                    ReadPointer(address + 0x18);
                current = previous != current
                    ? previous : 0;
            }
        }
        g_Api.handleScopeDtor(scope);
#if RAGEMP_DEV_BUILD
        static volatile LONG stackProbeLogs = 0;
        const LONG probeLog =
            InterlockedIncrement(&stackProbeLogs);
        if (exposed || probeLog <= 12)
        {
            char line[192]{};
            sprintf_s(
                line,
                "RageV8Runtime: callback-stack host probe "
                "contexts=%d receivers=%d result=%s",
                contextCount,
                receiverCount,
                exposed ? "matched" : "miss");
            if (hooks::g_LogFn)
                hooks::HLog(line);
        }
#endif
        return exposed;
    }

    bool ValueToUtf8(
        void* value,
        char* output,
        std::size_t capacity) noexcept
    {
        if (!output || !capacity)
            return false;
        output[0] = 0;
        if (!value || !OnV8OwnerThread() ||
            !g_Api.valueToUtf8 ||
            !g_Api.cleanupUtf8)
            return false;
        Utf8Buffer converted{};
        g_Api.valueToUtf8(
            &converted,
            g_Api.isolate,
            value);
        if (!converted.data || converted.length < 0)
            return false;
        std::size_t length =
            static_cast<std::size_t>(converted.length);
        if (length >= capacity)
            length = capacity - 1;
        CopyMemory(output, converted.data, length);
        output[length] = 0;
        g_Api.cleanupUtf8(&converted);
        return true;
    }

    std::uint16_t LocalInstanceType(void* value) noexcept
    {
        if (!value)
            return 0;
        const auto tagged = ReadPointer(
            reinterpret_cast<std::uintptr_t>(value));
        return TaggedType(tagged);
    }

    bool BootstrapSawCallback(
        std::uintptr_t callback) noexcept
    {
        if (!callback)
            return false;
        const LONG count = InterlockedCompareExchange(
            &g_BootstrapCallbackCount,
            0,
            0);
        for (LONG index = 0; index < count; ++index)
        {
            if (g_BootstrapCallbacks[index] == callback)
                return true;
        }
        return false;
    }

    bool PollResult(Result& result) noexcept
    {
        Lock(g_ResultLock);
        if (!g_ResultCount)
        {
            Unlock(g_ResultLock);
            return false;
        }
        result = g_Results[g_ResultRead];
        g_ResultRead =
            (g_ResultRead + 1) % kResultCapacity;
        --g_ResultCount;
        Unlock(g_ResultLock);
        return true;
    }

    bool HasResultFor(std::uint64_t commandId) noexcept
    {
        if (!commandId)
            return false;
        Lock(g_ResultLock);
        bool found = false;
        for (std::uint32_t offset = 0;
            offset < g_ResultCount;
            ++offset)
        {
            const std::uint32_t index =
                (g_ResultRead + offset) % kResultCapacity;
            if (g_Results[index].commandId == commandId)
            {
                found = true;
                break;
            }
        }
        Unlock(g_ResultLock);
        return found;
    }

    bool PollResultFor(
        std::uint64_t commandId,
        Result& result) noexcept
    {
        if (!commandId)
            return false;
        Lock(g_ResultLock);
        int found = -1;
        for (std::uint32_t offset = 0;
            offset < g_ResultCount;
            ++offset)
        {
            const std::uint32_t index =
                (g_ResultRead + offset) % kResultCapacity;
            if (g_Results[index].commandId == commandId)
            {
                found = static_cast<int>(offset);
                result = g_Results[index];
                break;
            }
        }
        if (found < 0)
        {
            Unlock(g_ResultLock);
            return false;
        }

        for (std::uint32_t offset =
                static_cast<std::uint32_t>(found);
            offset + 1 < g_ResultCount;
            ++offset)
        {
            const std::uint32_t destination =
                (g_ResultRead + offset) % kResultCapacity;
            const std::uint32_t source =
                (g_ResultRead + offset + 1) % kResultCapacity;
            g_Results[destination] = g_Results[source];
        }
        g_ResultWrite =
            (g_ResultWrite + kResultCapacity - 1) %
            kResultCapacity;
        --g_ResultCount;
        Unlock(g_ResultLock);
        return true;
    }

    StatusSnapshot GetStatus() noexcept
    {
        StatusSnapshot snapshot{};
        snapshot.state = static_cast<State>(
            InterlockedCompareExchange(&g_State, 0, 0));
        snapshot.buildMatched =
            InterlockedCompareExchange(&g_BuildMatched, 0, 0) != 0;
        snapshot.pumpQueued =
            InterlockedCompareExchange(&g_PumpQueued, 0, 0) != 0;
        snapshot.rootCaptured =
            InterlockedCompareExchange(&g_RootCaptured, 0, 0) != 0;
        snapshot.ownerThreadId = g_Api.ownerThreadId;
        snapshot.isolate = g_Api.isolate;
        snapshot.context =
            static_cast<std::uintptr_t>(
                InterlockedCompareExchange64(
                    &g_BoundContext,
                    0,
                    0));
        snapshot.root = g_RootPersistent;
        snapshot.generation = CurrentGeneration();
        snapshot.completedCommands =
            static_cast<std::uint64_t>(
                InterlockedCompareExchange64(
                    &g_ExecutedCommands,
                    0,
                    0));
        snapshot.droppedCommands =
            static_cast<std::uint64_t>(
                InterlockedCompareExchange64(
                    &g_DroppedCommands,
                    0,
                    0));
        snapshot.cameraAimPending =
            InterlockedCompareExchange(
                &g_CameraCommandPending, 0, 0) != 0;
        snapshot.cameraAimSubmitted =
            static_cast<std::uint64_t>(
                InterlockedCompareExchange64(
                    &g_CameraAimSubmitted, 0, 0));
        snapshot.cameraAimExecuted =
            static_cast<std::uint64_t>(
                InterlockedCompareExchange64(
                    &g_CameraAimExecuted, 0, 0));
        snapshot.bootstrapCallbackCount =
            static_cast<std::uint32_t>(
                InterlockedCompareExchange(
                    &g_BootstrapCallbackCount,
                    0,
                    0));
        snapshot.nativeTickFaults =
            static_cast<std::uint64_t>(
                InterlockedCompareExchange64(
                    &g_NativeTickFaults,
                    0,
                    0));

        Lock(g_CommandLock);
        snapshot.queuedCommands = g_CommandCount;
        Unlock(g_CommandLock);
        Lock(g_ResultLock);
        snapshot.queuedResults = g_ResultCount;
        Unlock(g_ResultLock);
        Lock(g_StatusLock);
        CopyText(
            snapshot.message,
            sizeof(snapshot.message),
            g_StatusMessage);
        Unlock(g_StatusLock);
        return snapshot;
    }
}
