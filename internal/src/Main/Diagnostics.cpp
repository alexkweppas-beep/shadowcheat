#include "Diagnostics.h"

#if defined(RAGEMP_DIAGNOSTICS)

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Hooks.h"
#include "../Features/NativeInvoke.hpp"

extern HMODULE g_ThisModule;
extern uint32_t g_ThisModuleSize;

namespace Diagnostics
{
    namespace
    {
        // Written through on every line rather than buffered: the whole point is
        // to still have the last line after the process dies, and a buffered
        // write is exactly what gets lost.
        HANDLE g_File = INVALID_HANDLE_VALUE;
        CRITICAL_SECTION g_Lock{};
        bool g_Ready = false;
        void* g_Veh = nullptr;
        LPTOP_LEVEL_EXCEPTION_FILTER g_PreviousFilter = nullptr;

        // The payload swallows access violations by design on every speculative
        // ped and viewport read, so a vectored handler sees a constant stream of
        // first-chance faults that are not bugs. Distinct addresses are recorded
        // once each and the total is capped, which keeps the fatal one visible
        // instead of buried.
        constexpr int kMaxSeen = 96;
        void* g_Seen[kMaxSeen]{};
        int g_SeenCount = 0;

        // Captured here rather than read from g_ThisModuleSize on demand:
        // diagnostics come up before DllMain fills that in, and the header this
        // reads is wiped later in DllMain anyway.
        uintptr_t g_SelfBase = 0;
        uint32_t g_SelfSize = 0;

        void CaptureSelfRange() noexcept
        {
            g_SelfBase = reinterpret_cast<uintptr_t>(g_ThisModule);
            g_SelfSize = g_ThisModuleSize;
            if (!g_SelfBase || g_SelfSize)
                return;
            __try
            {
                const auto* base =
                    reinterpret_cast<const BYTE*>(g_SelfBase);
                const auto* dos =
                    reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
                if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
                    dos->e_lfanew <= 0)
                    return;
                const auto* nt =
                    reinterpret_cast<const IMAGE_NT_HEADERS64*>(
                        base + dos->e_lfanew);
                if (nt->Signature == IMAGE_NT_SIGNATURE)
                    g_SelfSize = nt->OptionalHeader.SizeOfImage;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                g_SelfSize = 0;
            }
        }

        void WriteRaw(const char* text) noexcept
        {
            if (g_File == INVALID_HANDLE_VALUE || !text)
                return;
            DWORD written = 0;
            WriteFile(
                g_File,
                text,
                static_cast<DWORD>(strlen(text)),
                &written,
                nullptr);
        }

        void WriteLine(const char* text) noexcept
        {
            if (!g_Ready)
                return;
            EnterCriticalSection(&g_Lock);
            char stamp[32]{};
            _snprintf_s(
                stamp,
                sizeof(stamp),
                _TRUNCATE,
                "[%8lu] ",
                GetTickCount());
            WriteRaw(stamp);
            WriteRaw(text);
            WriteRaw("\r\n");
            FlushFileBuffers(g_File);
            LeaveCriticalSection(&g_Lock);
        }

        // Turns a raw address into something that can be looked up in a linker
        // map. Our own image is checked first because it is manually mapped and
        // therefore invisible to GetModuleHandleEx, which is also why the
        // Windows error report calls it "unknown".
        void DescribeAddress(
            const void* address,
            char* out,
            size_t outSize) noexcept
        {
            const auto value =
                reinterpret_cast<uintptr_t>(address);

            if (g_SelfBase && g_SelfSize &&
                value >= g_SelfBase &&
                value < g_SelfBase + g_SelfSize)
            {
                _snprintf_s(
                    out,
                    outSize,
                    _TRUNCATE,
                    "shadow_internal+0x%llX",
                    static_cast<unsigned long long>(value - g_SelfBase));
                return;
            }

            HMODULE module = nullptr;
            if (GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(address),
                    &module) &&
                module)
            {
                wchar_t wide[MAX_PATH]{};
                char name[MAX_PATH]{};
                if (GetModuleFileNameW(module, wide, MAX_PATH))
                {
                    const wchar_t* leaf = wcsrchr(wide, L'\\');
                    WideCharToMultiByte(
                        CP_ACP,
                        0,
                        leaf ? leaf + 1 : wide,
                        -1,
                        name,
                        sizeof(name),
                        nullptr,
                        nullptr);
                }
                _snprintf_s(
                    out,
                    outSize,
                    _TRUNCATE,
                    "%s+0x%llX",
                    name[0] ? name : "?",
                    static_cast<unsigned long long>(
                        value - reinterpret_cast<uintptr_t>(module)));
                return;
            }

            _snprintf_s(
                out,
                outSize,
                _TRUNCATE,
                "unmapped 0x%llX",
                static_cast<unsigned long long>(value));
        }

        bool IsFatalCode(DWORD code) noexcept
        {
            switch (code)
            {
            case EXCEPTION_ACCESS_VIOLATION:
            case EXCEPTION_ILLEGAL_INSTRUCTION:
            case EXCEPTION_STACK_OVERFLOW:
            case EXCEPTION_INT_DIVIDE_BY_ZERO:
            case EXCEPTION_PRIV_INSTRUCTION:
            case 0xC0000409:  // __fastfail / security check failure
            case 0xC0000374:  // heap corruption
                return true;
            default:
                return false;
            }
        }

        // Static rather than automatic, and the lock is entered around them: a
        // stack overflow is one of the faults being reported, so the recorder
        // must not itself put a kilobyte of buffers on the failing stack.
        char g_Where[MAX_PATH + 64]{};
        char g_Line[512]{};
        void* g_Frames[24]{};

        void ReportBacktrace() noexcept
        {
            const USHORT captured = RtlCaptureStackBackTrace(
                2,
                static_cast<DWORD>(
                    sizeof(g_Frames) / sizeof(g_Frames[0])),
                g_Frames,
                nullptr);
            for (USHORT index = 0; index < captured; ++index)
            {
                DescribeAddress(
                    g_Frames[index], g_Where, sizeof(g_Where));
                _snprintf_s(
                    g_Line,
                    sizeof(g_Line),
                    _TRUNCATE,
                    "    frame %2u  %s",
                    index,
                    g_Where);
                WriteLine(g_Line);
            }
            if (!captured)
                WriteLine("    (no unwindable frames)");
        }

        // RtlCaptureStackBackTrace returns nothing here: the walk needs unwind
        // info for every frame and stops dead at the first one without it,
        // which on this stack is immediate. Reading the stack directly and
        // reporting the words that land inside a module is cruder, it lists
        // stale return addresses too, but it does name the callers, which is
        // the whole point.
        void ReportStackScan(const CONTEXT* context) noexcept
        {
            if (!context)
                return;
            const auto stack = static_cast<uintptr_t>(context->Rsp);
            if (!stack)
                return;

            int reported = 0;
            __try
            {
                for (int index = 0;
                     index < 768 && reported < 14;
                     ++index)
                {
                    const auto candidate =
                        *reinterpret_cast<const uintptr_t*>(
                            stack + static_cast<uintptr_t>(index) * 8);
                    // Anything below the first user page or above the user
                    // address space cannot be a return address.
                    if (candidate < 0x10000 ||
                        candidate > 0x7FFFFFFFFFFFull)
                        continue;
                    DescribeAddress(
                        reinterpret_cast<const void*>(candidate),
                        g_Where,
                        sizeof(g_Where));
                    // Only words that resolve to a module: everything else is
                    // data that happens to look like a pointer.
                    if (g_Where[0] == 'u' &&
                        g_Where[1] == 'n' &&
                        g_Where[2] == 'm')
                        continue;
                    _snprintf_s(
                        g_Line,
                        sizeof(g_Line),
                        _TRUNCATE,
                        "    stack +0x%03X  %s",
                        index * 8,
                        g_Where);
                    WriteLine(g_Line);
                    ++reported;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                WriteLine("    stack scan faulted");
            }
        }

        void Report(
            const char* prefix,
            const EXCEPTION_RECORD& record,
            const CONTEXT* context) noexcept
        {
            // Recursive by design, so the nested WriteLine calls below are fine
            // and the buffers above stay private for the whole report.
            EnterCriticalSection(&g_Lock);

            // Reported with the smallest possible footprint: there is by
            // definition almost no stack left, and a backtrace walk here is
            // what turns a recoverable report into a second fault.
            if (record.ExceptionCode == EXCEPTION_STACK_OVERFLOW)
            {
                WriteLine("FATAL stack overflow");
                LeaveCriticalSection(&g_Lock);
                return;
            }

            DescribeAddress(
                record.ExceptionAddress, g_Where, sizeof(g_Where));

            char detail[128]{};
            if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
                record.NumberParameters >= 2)
            {
                const char* verb =
                    record.ExceptionInformation[0] == 0 ? "read" :
                    (record.ExceptionInformation[0] == 1 ? "write"
                                                         : "execute");
                _snprintf_s(
                    detail,
                    sizeof(detail),
                    _TRUNCATE,
                    " %s of 0x%llX",
                    verb,
                    static_cast<unsigned long long>(
                        record.ExceptionInformation[1]));
            }

            _snprintf_s(
                g_Line,
                sizeof(g_Line),
                _TRUNCATE,
                "%s code=0x%08lX at %s (thread %lu)%s",
                prefix,
                record.ExceptionCode,
                g_Where,
                GetCurrentThreadId(),
                detail);
            WriteLine(g_Line);
            ReportBacktrace();
            ReportStackScan(context);

            LeaveCriticalSection(&g_Lock);
        }

        LONG CALLBACK OnVectoredException(
            EXCEPTION_POINTERS* info) noexcept
        {
            if (!info || !info->ExceptionRecord ||
                !IsFatalCode(info->ExceptionRecord->ExceptionCode))
                return EXCEPTION_CONTINUE_SEARCH;

            void* const at = info->ExceptionRecord->ExceptionAddress;
            for (int index = 0; index < g_SeenCount; ++index)
            {
                if (g_Seen[index] == at)
                    return EXCEPTION_CONTINUE_SEARCH;
            }
            if (g_SeenCount < kMaxSeen)
            {
                g_Seen[g_SeenCount++] = at;
                Report(
                    "first-chance",
                    *info->ExceptionRecord,
                    info->ContextRecord);
                if (g_SeenCount == kMaxSeen)
                {
                    WriteLine(
                        "first-chance: cap reached, further distinct "
                        "addresses are not recorded");
                }
            }
            return EXCEPTION_CONTINUE_SEARCH;
        }

        // Reached only when nothing handled it, which is the crash the user
        // sees. Recorded unconditionally, dedup does not apply.
        LONG WINAPI OnUnhandledException(
            EXCEPTION_POINTERS* info) noexcept
        {
            if (info && info->ExceptionRecord)
                Report(
                    "FATAL unhandled",
                    *info->ExceptionRecord,
                    info->ContextRecord);
            else
                WriteLine("FATAL unhandled exception, no record");
            return g_PreviousFilter
                ? g_PreviousFilter(info)
                : EXCEPTION_CONTINUE_SEARCH;
        }
    }

    void Initialize() noexcept
    {
        if (g_Ready)
            return;

        InitializeCriticalSection(&g_Lock);
        g_File = CreateFileW(
            L"C:\\RAGEMP\\cheat_log.txt",
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
            nullptr);
        if (g_File == INVALID_HANDLE_VALUE)
        {
            DeleteCriticalSection(&g_Lock);
            return;
        }
        g_Ready = true;

        CaptureSelfRange();
        hooks::g_LogFn = &Write;
        // Native resolution runs lazily on first use, long after this, and used
        // to fail without a trace.
        NativeInvoke::g_diag = &Write;

        g_Veh = AddVectoredExceptionHandler(1, &OnVectoredException);
        g_PreviousFilter =
            SetUnhandledExceptionFilter(&OnUnhandledException);

        char line[192]{};
        _snprintf_s(
            line,
            sizeof(line),
            _TRUNCATE,
            "diagnostics up: image at 0x%llX size 0x%X, pid %lu",
            static_cast<unsigned long long>(g_SelfBase),
            g_SelfSize,
            GetCurrentProcessId());
        WriteLine(line);
    }

    void Shutdown() noexcept
    {
        if (!g_Ready)
            return;
        WriteLine("diagnostics down");
        if (g_Veh)
        {
            RemoveVectoredExceptionHandler(g_Veh);
            g_Veh = nullptr;
        }
        SetUnhandledExceptionFilter(g_PreviousFilter);
        g_PreviousFilter = nullptr;
        hooks::g_LogFn = nullptr;
        g_Ready = false;
        CloseHandle(g_File);
        g_File = INVALID_HANDLE_VALUE;
        DeleteCriticalSection(&g_Lock);
    }

    void Write(const char* message) noexcept
    {
        WriteLine(message);
    }
}

#else

namespace Diagnostics
{
    void Initialize() noexcept {}
    void Shutdown() noexcept {}
    void Write(const char*) noexcept {}
}

#endif
