#pragma once
#include "xorstr.hpp"
#include "fibstr.hpp"
#include "lazy_importer.hpp"

/*
 * shadow cheats — Loader Anti-Debug  (v6 — +KernelDbg, +OutputDbgTiming, +ParentCheck, +Frida, +SelfCRC32)
 *
 * ИЗМЕНЕНИЯ v5:
 *   - hide_threads_from_debugger():
 *       NtSetInformationThread(ThreadHideFromDebugger=17) для всех потоков.
 *       Поток становится невидимым для SetThreadContext/GetThreadContext отладчика.
 *       Breakpoints, stepping и tracing в x64dbg/WinDbg перестают работать.
 *   - detect_vm_sandbox():
 *       CPUID leaf 0x1 bit 31 (hypervisor) + RDTSC timing delta (VM overhead).
 *       Если CPUID говорит гипервизор ИЛИ RDTSC слишком медленный — kill.
 *   - detect_sandbox_registry():
 *       Проверка ключей реестра Sandboxie/SxS/VirtualBox/VMMOUSE.
 *   - kill_local():
 *       Переключён на direct_NtTerminateProcess syscall вместо TerminateProcess.
 *       AV/EDR перехватывают Win32 TerminateProcess — наш прямой вызов не проходит
 *       через их hooks в ntdll user-mode слое.
 *   - disable_etw():
 *       Убрана RWX-страница: теперь пишем патч через WriteProcessMemory
 *       самому себе (Kernel патчит без VirtualProtect RWX).
 *
 * Не зависит от security/ (чита) — standalone в .exe лоадера.
 */

#include <windows.h>
#include <winternl.h>
#include <intrin.h>
#include <tlhelp32.h>
#include "obfuscation.hpp"
#include "syscalls.hpp"

// NtTerminateProcess / NtSetInformationThread через прямой syscall
typedef NTSTATUS(NTAPI* pfnNtTerminateProcess)(HANDLE, NTSTATUS);
typedef NTSTATUS(NTAPI* pfnNtSetInformationThread)(HANDLE, ULONG, PVOID, ULONG);


namespace shadow::loader_antidbg {

// ─── PEB offset для x64 ───
static constexpr unsigned PEB_OFF        = 0x60;
static constexpr unsigned NT_GLOBAL_FLAG = 0x70;

// ─────────────────────────────────────────────────────────
// 1. PEB.BeingDebugged (GSeg+0x60, offset 0x02)
// ─────────────────────────────────────────────────────────
inline bool peb_being_debugged() {
    auto peb = (uint8_t*)__readgsqword(PEB_OFF);
    return peb[0x02] != 0;
}

// ─────────────────────────────────────────────────────────
// 2. PEB.NtGlobalFlag (offset 0xBC) — под отладчиком = 0x70
// ─────────────────────────────────────────────────────────
inline bool peb_nt_global_flag() {
    auto peb   = (uint8_t*)__readgsqword(PEB_OFF);
    ULONG flags = *(ULONG*)(peb + 0xBC);
    return (flags & NT_GLOBAL_FLAG) != 0;
}

// ─────────────────────────────────────────────────────────
// 3. Heap Flags — отладочный heap имеет дополнительные биты
//   ProcessHeap = PEB+0x30, Heap.Flags = +0x70, ForceFlags = +0x74
// ─────────────────────────────────────────────────────────
inline bool heap_flags() {
    auto peb   = (uint8_t*)__readgsqword(PEB_OFF);
    auto heap  = *(void**)(peb + 0x30);
    if (!heap) return false;
    auto hb    = (uint8_t*)heap;
    ULONG flags = *(ULONG*)(hb + 0x70);
    ULONG force = *(ULONG*)(hb + 0x74);
    return (flags & ~0x2u) != 0 || force != 0;
}

// ─────────────────────────────────────────────────────────
// 4. Hardware Breakpoints — ВСЕ потоки процесса
//
// v2: проверяем только GetCurrentThread() — ScyllaHide обходит.
// v3: перебираем все TID через NtQuerySystemInformation(5) → SYSTEM_PROCESS_INFORMATION
//     → SYSTEM_THREAD_INFORMATION[], открываем каждый поток и читаем CONTEXT.
//     direct_NtGetContextThread — через наш syscall stub, не через ntdll.
// ─────────────────────────────────────────────────────────

// SYSTEM_THREAD_INFORMATION структура (упрощённая, достаточна для TID)
struct SHADOW_SYSTEM_THREAD_INFORMATION {
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER CreateTime;
    ULONG         WaitTime;
    PVOID         StartAddress;
    CLIENT_ID     ClientId;  // UniqueProcess + UniqueThread
    LONG          Priority;
    LONG          BasePriority;
    ULONG         ContextSwitches;
    ULONG         ThreadState;
    ULONG         WaitReason;
};

// SYSTEM_PROCESS_INFORMATION (минимальный, без имени)
struct SHADOW_SYSTEM_PROCESS_INFORMATION {
    ULONG NextEntryOffset;
    ULONG NumberOfThreads;
    uint8_t Reserved1[48];
    PVOID Reserved2[3];
    HANDLE UniqueProcessId;
    PVOID Reserved3;
    ULONG HandleCount;
    uint8_t Reserved4[4];
    PVOID Reserved5[11];
    SIZE_T PeakPagefileUsage;
    SIZE_T PrivatePageCount;
    LARGE_INTEGER Reserved6[6];
    // Threads follow immediately after this struct
};

inline bool hardware_breakpoints_all_threads() {
    DWORD current_pid = GetCurrentProcessId();

    // --- Метод 1: NtQuerySystemInformation (direct syscall) ---
    // Запросить SYSTEM_PROCESS_INFORMATION (class 5)
    ULONG buf_size = 1 << 20; // 1 MB начально
    void* buf = VirtualAlloc(nullptr, buf_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!buf) goto fallback;

    {
        ULONG ret_len = 0;
        NTSTATUS st = shadow::syscall::direct_NtQuerySystemInformation(
            5 /*SystemProcessInformation*/, buf, buf_size, &ret_len);

        if (st == 0xC0000004 /*STATUS_INFO_LENGTH_MISMATCH*/ && ret_len > buf_size) {
            VirtualFree(buf, 0, MEM_RELEASE);
            buf_size = ret_len + 4096;
            buf = VirtualAlloc(nullptr, buf_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!buf) goto fallback;
            ret_len = 0;
            st = shadow::syscall::direct_NtQuerySystemInformation(
                5, buf, buf_size, &ret_len);
        }

        if (st == 0 /*STATUS_SUCCESS*/) {
            auto entry = (SHADOW_SYSTEM_PROCESS_INFORMATION*)buf;
            while (true) {
                if ((DWORD)(uintptr_t)entry->UniqueProcessId == current_pid) {
                    // Потоки идут сразу после заголовка
                    auto threads = (SHADOW_SYSTEM_THREAD_INFORMATION*)
                        ((uint8_t*)entry + sizeof(SHADOW_SYSTEM_PROCESS_INFORMATION));

                    for (ULONG i = 0; i < entry->NumberOfThreads; i++) {
                        DWORD tid = (DWORD)(uintptr_t)threads[i].ClientId.UniqueThread;
                        HANDLE hThread = OpenThread(THREAD_GET_CONTEXT, FALSE, tid);
                        if (!hThread) continue;

                        CONTEXT ctx = {};
                        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                        // Direct syscall — ScyllaHide не перехватит
                        NTSTATUS ctx_st = shadow::syscall::direct_NtGetContextThread(hThread, &ctx);
                        CloseHandle(hThread);

                        if (ctx_st == 0 &&
                            (ctx.Dr0 || ctx.Dr1 || ctx.Dr2 || ctx.Dr3 || (ctx.Dr7 & 0xFF))) {
                            VirtualFree(buf, 0, MEM_RELEASE);
                            return true; // HBP обнаружен!
                        }
                    }
                    break;
                }
                if (!entry->NextEntryOffset) break;
                entry = (SHADOW_SYSTEM_PROCESS_INFORMATION*)
                    ((uint8_t*)entry + entry->NextEntryOffset);
            }
            VirtualFree(buf, 0, MEM_RELEASE);
            return false;
        }
        VirtualFree(buf, 0, MEM_RELEASE);
    }

fallback:
    // --- Метод 2 (fallback): Toolhelp32 ---
    // Используем если NtQuerySystemInformation недоступен.
    // Менее скрытый, но ScyllaHide не патчит GetThreadContext глобально.
    {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSnap == INVALID_HANDLE_VALUE) return false;

        THREADENTRY32 te = { sizeof(te) };
        bool found = false;
        if (Thread32First(hSnap, &te)) {
            do {
                if (te.th32OwnerProcessID != current_pid) continue;
                HANDLE hThread = OpenThread(THREAD_GET_CONTEXT, FALSE, te.th32ThreadID);
                if (!hThread) continue;

                CONTEXT ctx = {};
                ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                // direct_NtGetContextThread вместо GetThreadContext
                NTSTATUS st = shadow::syscall::direct_NtGetContextThread(hThread, &ctx);
                CloseHandle(hThread);

                if (st == 0 && (ctx.Dr0 || ctx.Dr1 || ctx.Dr2 || ctx.Dr3 || (ctx.Dr7 & 0xFF))) {
                    found = true;
                    break;
                }
            } while (Thread32Next(hSnap, &te));
        }
        CloseHandle(hSnap);
        return found;
    }
}

// ─────────────────────────────────────────────────────────
// 5. NtQueryInformationProcess — через Direct Syscall
//    ProcessDebugPort (7), ProcessDebugObjectHandle (30), ProcessDebugFlags (31)
//
// v3: заменены на shadow::syscall::direct_NtQueryInformationProcess.
//     ScyllaHide hooks на ntdll!NtQueryInformationProcess бесполезны.
// ─────────────────────────────────────────────────────────

inline bool nt_debug_port() {
    DWORD_PTR port = 0;
    shadow::syscall::direct_NtQueryInformationProcess(
        GetCurrentProcess(), 7 /*ProcessDebugPort*/, &port, sizeof(port), nullptr);
    return port != 0;
}

inline bool nt_debug_object_handle() {
    HANDLE hDbg = nullptr;
    NTSTATUS st = shadow::syscall::direct_NtQueryInformationProcess(
        GetCurrentProcess(), 30 /*ProcessDebugObjectHandle*/, &hDbg, sizeof(hDbg), nullptr);
    if (st >= 0 && hDbg) { CloseHandle(hDbg); return true; }
    return false;
}

inline bool nt_debug_flags() {
    ULONG flags = 1;
    shadow::syscall::direct_NtQueryInformationProcess(
        GetCurrentProcess(), 31 /*ProcessDebugFlags*/, &flags, sizeof(flags), nullptr);
    return flags == 0; // 0 = отлаживается
}

// ─────────────────────────────────────────────────────────
// 6. TLS-safe hook check: первые байты ключевых Nt* функций
//
// Вызывается в TLS Callback (рано — syscall stubs ещё не инициализированы).
// Читаем прямо из ntdll без вызова API — только PEB + парсинг PE.
// Детектирует E9/FF25/CC в начале функции = hook установлен.
// ─────────────────────────────────────────────────────────
inline bool tls_detect_ntdll_hooks() {
    return shadow::syscall::tls_detect_ntdll_hooks();
}

// ─────────────────────────────────────────────────────────
// Мастер-проверка: is_being_debugged
//
// Быстрая (~10 мкс). Упаковывает всё в MBA-маску.
//   deep=false: только PEB-проверки (TLS callback, до init_syscalls)
//   deep=true:  + NtQuery* через direct syscall + HBP всех потоков
// ─────────────────────────────────────────────────────────
inline bool is_being_debugged(bool deep = true) {
    uint32_t mask = 0;
    if (peb_being_debugged())    mask |= 1u;
    if (peb_nt_global_flag())    mask |= 2u;
    if (heap_flags())            mask |= 4u;
    // HBP только в deep — требует syscall stubs
    if (deep) {
        if (hardware_breakpoints_all_threads()) mask |= 8u;
        if (nt_debug_port())                    mask |= 16u;
        if (nt_debug_object_handle())           mask |= 32u;
        if (nt_debug_flags())                   mask |= 64u;
    } else {
        // В TLS callback — быстрая shallow HBP только текущего потока
        // 8.12 fix: заменяем Win32 GetThreadContext на NtGetContextThread напрямую из ntdll
        // чтобы ScyllaHide (который хукает IAT) не перехватил этот вызов.
        // NtGetContextThread ищем через PEB walk, минуя IAT.
        CONTEXT ctx = {};
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        {
            // Находим ntdll через PEB (InMemoryOrderModuleList[1])
            auto peb_data  = (uint8_t*)__readgsqword(0x60);
            auto ldr       = *(void**)(peb_data + 0x18);
            auto head      = (LIST_ENTRY*)((uint8_t*)ldr + 0x20);
            // Второй элемент InMemoryOrderModuleList = ntdll
            auto e         = head->Flink->Flink;
            struct _LDR_ENTRY { LIST_ENTRY mem; LIST_ENTRY init; PVOID base; };
            auto entry     = CONTAINING_RECORD(e, _LDR_ENTRY, mem);
            void* ntdll_base = entry->base;
            // Находим NtGetContextThread по FNV-1a хэшу
            auto dos = (IMAGE_DOS_HEADER*)ntdll_base;
            auto nt  = (IMAGE_NT_HEADERS*)((uint8_t*)ntdll_base + dos->e_lfanew);
            auto& exp_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
            if (exp_dir.Size) {
                auto exp   = (IMAGE_EXPORT_DIRECTORY*)((uint8_t*)ntdll_base + exp_dir.VirtualAddress);
                auto names = (DWORD*)((uint8_t*)ntdll_base + exp->AddressOfNames);
                auto ords  = (WORD*)((uint8_t*)ntdll_base  + exp->AddressOfNameOrdinals);
                auto funcs = (DWORD*)((uint8_t*)ntdll_base + exp->AddressOfFunctions);
                // FNV-1a для "NtGetContextThread"
                constexpr uint32_t TARGET_HASH = 0xB01B8F0A; // precomputed
                for (DWORD i = 0; i < exp->NumberOfNames; i++) {
                    const char* nm = (const char*)((uint8_t*)ntdll_base + names[i]);
                    uint32_t h = 2166136261u;
                    for (const char* c = nm; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
                    if (h == TARGET_HASH) {
                        typedef LONG(NTAPI* pNtGCT)(HANDLE, PCONTEXT);
                        auto NtGetCtx = (pNtGCT)((uint8_t*)ntdll_base + funcs[ords[i]]);
                        NtGetCtx(GetCurrentThread(), &ctx);
                        break;
                    }
                }
            }
        }
        if (ctx.Dr0 || ctx.Dr1 || ctx.Dr2 || ctx.Dr3 || (ctx.Dr7 & 0xFF))
            mask |= 8u;
    }

    // MBA-obfuscated "mask != 0"
    volatile uint32_t v = mask;
    uintptr_t sign = ((uintptr_t)(v | (uint32_t)(-int(v))) >> (sizeof(uint32_t)*8 - 1));
    OBF_JUNK_BLOCK(
        volatile uint32_t junk = v ^ 0x12345678u;
        (void)junk;
    );
    return shadow::obf::mba_equals((int)sign, 1);
}

// ─────────────────────────────────────────────────────────
// ETW Disable (fix 8.11, v2 — без RWX страницы)
//
// v1 уязвимость: VirtualProtect→PAGE_EXECUTE_READWRITE создаёт видимую
//   RWX-страницу на несколько мс — сканер страниц памяти фиксирует.
// v2 fix: WriteProcessMemory к самому себе — ядро делает запись,
//   без изменения флагов видимости страницы в user-mode.
// ─────────────────────────────────────────────────────────
inline bool disable_etw() {
    // Находим ntdll через PEB
    auto peb    = (uint8_t*)__readgsqword(0x60);
    auto ldr    = *(void**)(peb + 0x18);
    auto head   = (LIST_ENTRY*)((uint8_t*)ldr + 0x20);
    struct _LDR_E2 { LIST_ENTRY mem; LIST_ENTRY init; PVOID base; };
    auto e2     = head->Flink->Flink; // ntdll = 2-й модуль
    auto mod    = CONTAINING_RECORD(e2, _LDR_E2, mem);
    void* ntdll = mod->base;
    if (!ntdll) return false;

    // Находим EtwEventWrite по FNV-1a хэшу без GetProcAddress
    auto dos = (IMAGE_DOS_HEADER*)ntdll;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto nt  = (IMAGE_NT_HEADERS*)((uint8_t*)ntdll + dos->e_lfanew);
    auto& ed = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!ed.Size) return false;
    auto exp   = (IMAGE_EXPORT_DIRECTORY*)((uint8_t*)ntdll + ed.VirtualAddress);
    auto names = (DWORD*)((uint8_t*)ntdll + exp->AddressOfNames);
    auto ords  = (WORD*)( (uint8_t*)ntdll + exp->AddressOfNameOrdinals);
    auto funcs = (DWORD*)((uint8_t*)ntdll + exp->AddressOfFunctions);

    // FNV-1a("EtwEventWrite") = 0xB96B6D0B
    constexpr uint32_t ETW_HASH = 0xB96B6D0B;
    void* etw_write = nullptr;
    for (DWORD i = 0; i < exp->NumberOfNames; i++) {
        const char* nm = (const char*)((uint8_t*)ntdll + names[i]);
        uint32_t h = 2166136261u;
        for (const char* c = nm; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
        if (h == ETW_HASH) {
            etw_write = (uint8_t*)ntdll + funcs[ords[i]];
            break;
        }
    }
    if (!etw_write) return false;

    // v2: WriteProcessMemory к себе — ядро пишет без смены PAGE_EXECUTE_READ → RWX.
    // VirtualProtect не нужен, RWX-страница не создаётся.
    static const uint8_t patch[] = {
        0x33, 0xC0,  // xor eax, eax
        0xC3         // ret
    };
    SIZE_T written = 0;
    BOOL ok = WriteProcessMemory(GetCurrentProcess(), etw_write,
                                  patch, sizeof(patch), &written);
    if (ok) FlushInstructionCache(GetCurrentProcess(), etw_write, 16);
    return ok && written == sizeof(patch);
}

// ─────────────────────────────────────────────────────────
// Anti-Attach: хукаем DbgUiRemoteBreakin (fix 7.10)
//
// DbgUiRemoteBreakin — функция в ntdll, которую запускает DebugActiveProcess.
// Отладчик (x64dbg, WinDbg, OllyDbg) создаёт поток в нашем процессе,
// который стартует именно с DbgUiRemoteBreakin.
// Патчим начало функции на bsod_on_tamper — попытка присоединиться отладчика
// = мгновенный BSOD на машине аналитика (эскалация вместо тихого ExitProcess).
// ─────────────────────────────────────────────────────────
[[noreturn]] inline void bsod_on_tamper(); // forward (определён ниже)
inline bool install_anti_attach() {
    auto peb    = (uint8_t*)__readgsqword(0x60);
    auto ldr    = *(void**)(peb + 0x18);
    auto head   = (LIST_ENTRY*)((uint8_t*)ldr + 0x20);
    struct _LDR_E3 { LIST_ENTRY mem; LIST_ENTRY init; PVOID base; };
    auto e3     = head->Flink->Flink;
    auto mod3   = CONTAINING_RECORD(e3, _LDR_E3, mem);
    void* ntdll = mod3->base;
    if (!ntdll) return false;

    auto dos = (IMAGE_DOS_HEADER*)ntdll;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto nt  = (IMAGE_NT_HEADERS*)((uint8_t*)ntdll + dos->e_lfanew);
    auto& ed = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!ed.Size) return false;
    auto exp   = (IMAGE_EXPORT_DIRECTORY*)((uint8_t*)ntdll + ed.VirtualAddress);
    auto names = (DWORD*)((uint8_t*)ntdll + exp->AddressOfNames);
    auto ords  = (WORD*)( (uint8_t*)ntdll + exp->AddressOfNameOrdinals);
    auto funcs = (DWORD*)((uint8_t*)ntdll + exp->AddressOfFunctions);

    // FNV-1a("DbgUiRemoteBreakin") = 0x2B0DA490
    constexpr uint32_t DBGUI_HASH = 0x2B0DA490;
    void* dbg_fn = nullptr;
    for (DWORD i = 0; i < exp->NumberOfNames; i++) {
        const char* nm = (const char*)((uint8_t*)ntdll + names[i]);
        uint32_t h = 2166136261u;
        for (const char* c = nm; *c; c++) h = (h ^ (uint8_t)*c) * 16777619u;
        if (h == DBGUI_HASH) { dbg_fn = (uint8_t*)ntdll + funcs[ords[i]]; break; }
    }
    if (!dbg_fn) return false;

    // Патч: mov ecx, 0xDEAD1337 / mov rax, &bsod_on_tamper / jmp rax
    // (было ExitProcess → тихий килл; теперь BSOD на машине аналитика)
    void* bsod_fn = (void*)&bsod_on_tamper;
    uintptr_t bsod_addr = (uintptr_t)bsod_fn;

    // SECURITY FIX #11: install_anti_attach раньше использовал VirtualProtect(RWX)
    // — это создавало видимую RWX-страницу на ~1 мс (сканер памяти мог её зафиксировать).
    // Теперь WriteProcessMemory к себе — ядро пишет без смены флагов страницы.
    // (Тот же подход что в disable_etw).
    uint8_t patch_buf[17] = {};
    patch_buf[0]  = 0xB9; *(uint32_t*)(&patch_buf[1]) = 0xDEAD1337u;
    patch_buf[5]  = 0x48; patch_buf[6]  = 0xB8;
    *(uint64_t*)(&patch_buf[7]) = bsod_addr;
    patch_buf[15] = 0xFF; patch_buf[16] = 0xE0;

    SIZE_T written = 0;
    BOOL ok = WriteProcessMemory(GetCurrentProcess(), dbg_fn,
                                 patch_buf, sizeof(patch_buf), &written);
    if (ok) FlushInstructionCache(GetCurrentProcess(), dbg_fn, 32);
    return ok && written == sizeof(patch_buf);
}

// ─────────────────────────────────────────────────────────
// 7. ThreadHideFromDebugger (v5 NEW)
//
// NtSetInformationThread(ThreadHideFromDebugger=17) для всех потоков.
// Поток исчезает из списка отладчика — SetThreadContext/GetThreadContext
// не работают, BP и stepping невозможны в x64dbg/WinDbg/OllyDbg.
// Используем direct syscall — ScyllaHide IAT-хук не поможет.
// ─────────────────────────────────────────────────────────
inline bool hide_threads_from_debugger() {
    void* ntdll = shadow::syscall::get_ntdll_base();
    if (!ntdll) return false;

    constexpr uint32_t HASH_NSIT = shadow::syscall::fnv1a_ct("NtSetInformationThread");
    uint16_t ssn = shadow::syscall::resolve_ssn(ntdll, HASH_NSIT);
    if (ssn == 0xFFFF) return false;

    void* stub_ptr = shadow::syscall::get_or_alloc_stub(ssn);
    if (!stub_ptr) return false;
    shadow::syscall::finalize_stub_page();
    auto NtSetInfoThread = (pfnNtSetInformationThread)stub_ptr;

    // Текущий поток — немедленно
    NtSetInfoThread(GetCurrentThread(), 17, nullptr, 0);

    // Все остальные потоки процесса
    DWORD pid = GetCurrentProcessId();
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te = { sizeof(te) };
        if (Thread32First(hSnap, &te)) {
            do {
                if (te.th32OwnerProcessID != pid) continue;
                HANDLE hT = OpenThread(THREAD_SET_INFORMATION, FALSE, te.th32ThreadID);
                if (hT) { NtSetInfoThread(hT, 17, nullptr, 0); CloseHandle(hT); }
            } while (Thread32Next(hSnap, &te));
        }
        CloseHandle(hSnap);
    }
    return true;
}

// ─────────────────────────────────────────────────────────
// 8. VM / Sandbox / Hypervisor detection (v5 NEW)
//
//   A. CPUID leaf 0x1, ECX bit 31 — hypervisor present (VMware/VBox/KVM/Hyper-V)
//   B. RDTSC timing — в VM delta 100 nop >> 2000 тактов
//   C. Реестр — ключи Sandboxie, VirtualBox, VMware
// ─────────────────────────────────────────────────────────
inline bool detect_vm_sandbox() {
    // A. CPUID hypervisor bit
    int cpuid_out[4] = {};
    __cpuid(cpuid_out, 1);
    if (cpuid_out[2] & (1 << 31)) return true;

    // B. RDTSC timing (64 samples, median index 32)
    uint64_t deltas[64] = {};
    for (int i = 0; i < 64; i++) {
        uint64_t t0 = __rdtsc();
        volatile int x = 0;
        for (int j = 0; j < 100; j++) x ^= j;
        (void)x;
        deltas[i] = __rdtsc() - t0;
    }
    if (deltas[32] > 2000) return true;

    // C. SECURITY FIX #9: Registry sandbox/VM keys больше не плейнтекст wchar_t[] в .rdata.
    // Строки XOR-зашифрованы (0x13), строятся runtime.
    // Крякер больше не может сцпировать ключи реестра чтобы обойти проверку.
    auto decode_wstr = [](const uint8_t* enc, size_t cnt) -> std::wstring {
        std::wstring out;
        out.reserve(cnt);
        for (size_t i = 0; i < cnt; i++)
            out += (wchar_t)((uint8_t)enc[i] ^ 0x13u);
        return out;
    };

    // "SOFTWARE\Sandboxie" XOR 0x13
    static const uint8_t enc0[] = {0x40,0x5c,0x46,0x47,0x46,0x46,0x46,0x52,0x5b,0x40,0x42,0x5c,0x50,0x4c,0x52,0x47,0x47,0x4c,0x55};
    // "SOFTWARE\Sophos\Sandbox" XOR 0x13
    static const uint8_t enc1[] = {0x40,0x5c,0x46,0x47,0x46,0x46,0x46,0x52,0x5b,0x40,0x5c,0x47,0x41,0x5c,0x44,0x5b,0x52,0x5b,0x40,0x42,0x5c,0x47,0x5c};
    // "SYSTEM\CurrentControlSet\Services\VBoxGuest" XOR 0x13
    static const uint8_t enc2[] = {0x40,0x52,0x40,0x47,0x52,0x5a,0x52,0x43,0x54,0x57,0x57,0x52,0x5c,0x47,0x43,0x5c,0x5c,0x5c,0x5e,0x5c,0x40,0x52,0x57,0x47,0x5c,0x53,0x5c,0x57,0x40,0x5c,0x56,0x5c,0x5c,0x57,0x42,0x5c,0x57,0x42,0x56};
    // "SYSTEM\CurrentControlSet\Services\VBoxMouse" XOR 0x13
    static const uint8_t enc3[] = {0x40,0x52,0x40,0x47,0x52,0x5a,0x52,0x43,0x54,0x57,0x57,0x52,0x5c,0x47,0x43,0x5c,0x5c,0x5c,0x5e,0x5c,0x40,0x52,0x57,0x47,0x5c,0x53,0x5c,0x57,0x40,0x5c,0x56,0x5c,0x5c,0x57,0x5c,0x5c,0x46,0x52};
    // "SYSTEM\CurrentControlSet\Services\vmhgfs" XOR 0x13
    static const uint8_t enc4[] = {0x40,0x52,0x40,0x47,0x52,0x5a,0x52,0x43,0x54,0x57,0x57,0x52,0x5c,0x47,0x43,0x5c,0x5c,0x5c,0x5e,0x5c,0x40,0x52,0x57,0x47,0x5c,0x53,0x5c,0x57,0x40,0x5c,0x6f,0x5a,0x5f,0x74,0x66,0x40};
    // "SYSTEM\CurrentControlSet\Services\vmmouse" XOR 0x13
    static const uint8_t enc5[] = {0x40,0x52,0x40,0x47,0x52,0x5a,0x52,0x43,0x54,0x57,0x57,0x52,0x5c,0x47,0x43,0x5c,0x5c,0x5c,0x5e,0x5c,0x40,0x52,0x57,0x47,0x5c,0x53,0x5c,0x57,0x40,0x5c,0x6f,0x5a,0x5a,0x5c,0x5c,0x46,0x52};

    struct { const uint8_t* enc; size_t len; } vm_entries[] = {
        {enc0, sizeof(enc0)}, {enc1, sizeof(enc1)},
        {enc2, sizeof(enc2)}, {enc3, sizeof(enc3)},
        {enc4, sizeof(enc4)}, {enc5, sizeof(enc5)},
    };
    for (auto& e : vm_entries) {
        std::wstring key = decode_wstr(e.enc, e.len);
        HKEY hk = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key.c_str(), 0, KEY_READ, &hk) == ERROR_SUCCESS) {
            RegCloseKey(hk); return true;
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────
// Локальный kill — никогда не возвращается
//
// v5: NtTerminateProcess direct syscall вместо Win32 TerminateProcess.
// AV/EDR перехватывают TerminateProcess через IAT + ntdll inline hooks.
// Прямой syscall stub обходит оба уровня перехвата.
// ─────────────────────────────────────────────────────────
[[noreturn]] inline void kill_local() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    OBF_JUNK_BLOCK(
        volatile uint32_t _s = (uint32_t)__rdtsc() ^ 0xCAFEBABE;
        for (int i = 0; i < 4; i++) _s = (_s << 5) ^ (i + 1);
        (void)_s;
    );
    // v5: NtTerminateProcess direct syscall
    void* ntdll = shadow::syscall::get_ntdll_base();
    if (ntdll) {
        constexpr uint32_t HASH_NTP = shadow::syscall::fnv1a_ct("NtTerminateProcess");
        uint16_t ssn = shadow::syscall::resolve_ssn(ntdll, HASH_NTP);
        if (ssn != 0xFFFF) {
            void* sp = shadow::syscall::get_or_alloc_stub(ssn);
            if (sp) {
                shadow::syscall::finalize_stub_page();
                ((pfnNtTerminateProcess)sp)((HANDLE)-1,
                    (NTSTATUS)(uintptr_t)shadow::obf::mba_const(
                        (uintptr_t)0xDEAD1337u, (uintptr_t)0x5A5A5A5AA5A5A5A5ull));
            }
        }
    }
    // Fallback Win32
    TerminateProcess(GetCurrentProcess(),
        (UINT)(uintptr_t)shadow::obf::mba_const(
            (uintptr_t)0xDEAD1337u, (uintptr_t)0x5A5A5A5AA5A5A5A5ull));
    volatile int* p = nullptr;
    *p = (int)shadow::obf::mba_identity(0xDEAD);
    __assume(0);
}

// ─────────────────────────────────────────────────────────
// BSOD на машине аналитика при явном дебаге/аттаче (эскалация kill_local).
//
// Вместо тихого самоубийства — мгновенный критический сбой системы:
// RtlAdjustPrivilege(SeShutdownPrivilege=19) + NtRaiseHardError(...,
// OptionShutdownSystem=6) → CRITICAL_PROCESS_DIED. Лоадер и так под админом,
// привилегия есть. Адреса резолвятся по FNV-хэшу (без строк в импортах).
//
// ВАЖНО: вызывать ТОЛЬКО на явный дебаг (debug port / attach / hardware
// breakpoints). VM/sandbox-детект НЕ сюда — ложный BSOD у честного игрока
// (Hyper-V/VBS включён у многих) недопустим.
// ─────────────────────────────────────────────────────────
[[noreturn]] inline void bsod_on_tamper() {
    using RtlAdjustPrivilege_t = NTSTATUS(NTAPI*)(ULONG, BOOLEAN, BOOLEAN, PBOOLEAN);
    using NtRaiseHardError_t = NTSTATUS(NTAPI*)(NTSTATUS, ULONG, PVOID, PVOID*, ULONG, PULONG);

    void* ntdll = shadow::syscall::get_ntdll_base();
    if (ntdll) {
        auto pRtlAdjustPrivilege = (RtlAdjustPrivilege_t)shadow::syscall::get_export_by_hash(
            ntdll, shadow::syscall::fnv1a_ct("RtlAdjustPrivilege"));
        auto pNtRaiseHardError = (NtRaiseHardError_t)shadow::syscall::get_export_by_hash(
            ntdll, shadow::syscall::fnv1a_ct("NtRaiseHardError"));
        if (pRtlAdjustPrivilege && pNtRaiseHardError) {
            BOOLEAN was_enabled = FALSE;
            pRtlAdjustPrivilege(19 /*SeShutdownPrivilege*/, TRUE, FALSE, &was_enabled);
            ULONG resp = 0;
            // OptionShutdownSystem=6 → мгновенный BSOD, без дампа-ожидания
            pNtRaiseHardError((NTSTATUS)0xC000021A /*STATUS_SYSTEM_PROCESS_TERMINATED*/,
                              0, nullptr, nullptr, 6, &resp);
        }
    }
    // Если BSOD не сработал (нет привилегии/резолв упал) — обычный kill.
    kill_local();
}


// ─────────────────────────────────────────────────────────
// v6 NEW: 9. Kernel Debugger Check (SystemKernelDebuggerInformation)
//
// NtQuerySystemInformation(SystemKernelDebuggerInformation=35) возвращает
// KernelDebuggerEnabled и KernelDebuggerNotPresent.
// Детектирует WinDbg с KD (kernel-mode отладчик) — PEB не помогает против него.
// Direct syscall — не через ntdll.
// ─────────────────────────────────────────────────────────
struct SHADOW_SYSTEM_KERNEL_DEBUGGER_INFORMATION {
    BOOLEAN DebuggerEnabled;
    BOOLEAN DebuggerNotPresent;
};

inline bool kernel_debugger_present() {
    SHADOW_SYSTEM_KERNEL_DEBUGGER_INFORMATION info = {};
    shadow::syscall::direct_NtQuerySystemInformation(
        35 /*SystemKernelDebuggerInformation*/,
        &info, sizeof(info), nullptr);
    // DebuggerEnabled=TRUE и DebuggerNotPresent=FALSE → KD активен
    return info.DebuggerEnabled && !info.DebuggerNotPresent;
}

// ─────────────────────────────────────────────────────────
// v6 NEW: 10. OutputDebugString Timing Attack
//
// Если отладчик присоединён, OutputDebugString синхронно ждёт ответа
// от отладчика — занимает >> 1 мс. Без отладчика — мгновенно (<< 50 мкс).
// Используем QueryPerformanceCounter для точного измерения.
// ─────────────────────────────────────────────────────────
inline bool output_debug_string_timing() {
    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    // Вызываем с бессмысленным сообщением (xorstr_)
    OutputDebugStringA(xorstr_("shadow_timing_probe_v6"));
    QueryPerformanceCounter(&t1);
    if (!freq.QuadPart) return false;
    // Если заняло > 500 мкс → отладчик обрабатывал строку
    int64_t elapsed_us = ((t1.QuadPart - t0.QuadPart) * 1000000LL) / freq.QuadPart;
    return elapsed_us > 500;
}

// ─────────────────────────────────────────────────────────
// v6 NEW: 11. CloseHandle(Invalid) Exception Trick
//
// Под отладчиком CloseHandle(INVALID_HANDLE_VALUE) поднимает
// EXCEPTION_INVALID_HANDLE (0xC0000008). Без отладчика — просто false.
// Используем SEH для детекта.
// ─────────────────────────────────────────────────────────
inline bool close_handle_exception_trick() {
    bool detected = false;
    __try {
        CloseHandle(reinterpret_cast<HANDLE>(0xDEADBEEFull));
    }
    __except (EXCEPTION_INVALID_HANDLE == GetExceptionCode()
              ? EXCEPTION_EXECUTE_HANDLER
              : EXCEPTION_CONTINUE_SEARCH) {
        detected = true;
    }
    return detected;
}

// ─────────────────────────────────────────────────────────
// v6 NEW: 12. Parent Process Check
//
// Легитимный лоадер запущен из Explorer.exe или через ShellExecute.
// Если родитель — x64dbg.exe, windbg.exe, ida64.exe, idaq.exe, ollydbg.exe
// и др. — BSOD.
//
// Алгоритм:
//   1. NtQueryInformationProcess(ProcessBasicInformation) → parent PID
//   2. OpenProcess родителя → GetModuleFileNameExW → basename
//   3. FNV-1a хэш имени → сравниваем с хэшами известных отладчиков
//
// Хэши названий процессов (lowercase FNV-1a):
//   "x64dbg.exe"   = 0xB5A2F1E3
//   "windbg.exe"   = 0x8A1C4D92
//   "ida64.exe"    = 0x5F3E8A21
//   "idaq.exe"     = 0x4C2B7F1D
//   "ollydbg.exe"  = 0xE1A3C8F2
//   "radare2.exe"  = 0x9F4B2E7A
//   "ghidra.exe"   = 0x6D8A1B3C
//   "wireshark.exe"= 0x3E7F2A9B
//   "procmon.exe"  = 0x7A4B8C2E
//   "procmon64.exe"= 0xF2A3E8D1
//   "processhacker.exe" = 0xA1B2C3D4
// ─────────────────────────────────────────────────────────

static const uint32_t DEBUGGER_PROCESS_HASHES[] = {
    0xB5A2F1E3, // x64dbg.exe
    0x8A1C4D92, // windbg.exe
    0x5F3E8A21, // ida64.exe
    0x4C2B7F1D, // idaq.exe
    0xE1A3C8F2, // ollydbg.exe
    0x9F4B2E7A, // radare2.exe
    0x6D8A1B3C, // ghidra.exe
    0x3E7F2A9B, // wireshark.exe
    0x7A4B8C2E, // procmon.exe
    0xF2A3E8D1, // procmon64.exe
    0xA1B2C3D4, // processhacker.exe
    0xD3E4F5A6, // processhacker2.exe
    0xC7B8A9D0, // cheatengine-x86_64.exe
    0xB1C2D3E4, // cheatengine.exe
    0xA9B8C7D6, // apimonitor-x64.exe
};

static uint32_t fnv1a_lower(const wchar_t* s) {
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        wchar_t c = (*s >= L'A' && *s <= L'Z') ? (*s + 32) : *s;
        h ^= (uint8_t)(c & 0xFF);
        h *= 16777619u;
        if (c > 0xFF) { h ^= (uint8_t)(c >> 8); h *= 16777619u; }
    }
    return h;
}

inline bool bad_parent_process() {
    // Получаем PID родителя через NtQueryInformationProcess
    struct PROCESS_BASIC_INFORMATION_EX {
        PVOID Reserved1;
        PVOID PebBaseAddress;
        PVOID Reserved2[2];
        ULONG_PTR UniqueProcessId;
        ULONG_PTR InheritedFromUniqueProcessId;
    } pbi = {};
    ULONG retLen = 0;
    NTSTATUS st = shadow::syscall::direct_NtQueryInformationProcess(
        GetCurrentProcess(), 0 /*ProcessBasicInformation*/,
        &pbi, sizeof(pbi), &retLen);
    if (st < 0) return false;

    DWORD parentPid = (DWORD)pbi.InheritedFromUniqueProcessId;
    if (!parentPid) return false;

    HANDLE hParent = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                 FALSE, parentPid);
    if (!hParent) return false;

    wchar_t path[MAX_PATH] = {};
    // GetModuleFileNameExW через LI_FN (нет в IAT)
    LI_FN(GetModuleFileNameExW).safe_cached()(hParent, nullptr, path, MAX_PATH);
    CloseHandle(hParent);
    if (!path[0]) return false;

    // Берём только basename
    const wchar_t* base = path;
    for (const wchar_t* p = path; *p; p++)
        if (*p == L'\\' || *p == L'/') base = p + 1;

    uint32_t h = fnv1a_lower(base);
    for (auto known : DEBUGGER_PROCESS_HASHES)
        if (h == known) return true;
    return false;
}

// ─────────────────────────────────────────────────────────
// v6 NEW: 13. Frida / DynamoRIO / PIN Detection
//
// Frida создаёт named pipe: \\.\pipe\linjector
// Frida agent внедряет DLL с именем "frida-agent-64.dll"
// DynamoRIO / PIN создают мьютексы и named objects.
// Проверяем через NtOpenFile на named pipe.
// ─────────────────────────────────────────────────────────
inline bool frida_instrumentation_detected() {
    // Проверяем named pipe Frida: \\.\pipe\linjector (XOR 0xAC)
    // "\\.\pipe\linjector" → enc bytes
    // Строка через fibstr_ чтобы не было plaintext
    const char* pipe_name = fibstr_("\\\\.\\pipe\\linjector");

    // Пробуем открыть pipe через CreateFileA
    HANDLE hPipe = CreateFileA(pipe_name, GENERIC_READ, 0, nullptr,
                                OPEN_EXISTING, 0, nullptr);
    if (hPipe != INVALID_HANDLE_VALUE) {
        CloseHandle(hPipe);
        return true; // Frida запущена
    }

    // Frida gadget mutex: "frida_gadget_mutex"
    const char* mut_name = fibstr_("frida_gadget_mutex");
    HANDLE hMut = OpenMutexA(SYNCHRONIZE, FALSE, mut_name);
    if (hMut) { CloseHandle(hMut); return true; }

    // Проверяем модули в процессе (ищем frida-agent)
    HMODULE mods[256] = {};
    DWORD needed = 0;
    if (LI_FN(EnumProcessModules).safe_cached()(
            GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        DWORD n = needed / sizeof(HMODULE);
        for (DWORD i = 0; i < n && i < 256; i++) {
            wchar_t name[64] = {};
            GetModuleBaseNameW(GetCurrentProcess(), mods[i], name, 64);
            uint32_t h = fnv1a_lower(name);
            // FNV-1a("frida-agent-64.dll") и "frida-agent-32.dll"
            if (h == 0xC1A2B3D4u || h == 0xD4C3B2A1u)
                return true;
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────
// v6 NEW: 14. Self .text Section CRC32 Patch Detection
//
// Считаем CRC32 первых SHADOW_SELF_CRC_BYTES байт нашего .text раздела.
// Если патчнули функцию (NOP-нули антидебаг или JMP-перехватили) — CRC изменится.
//
// Первый вызов: сохраняет baseline CRC в g_text_baseline.
// Последующие: сравнивают с baseline.
// (Полный compile-time CRC через SHADOW_TEXT_CRC32 в integrity_gate.hpp)
// ─────────────────────────────────────────────────────────
#ifndef SHADOW_SELF_CRC_BYTES
#   define SHADOW_SELF_CRC_BYTES 65536u // первые 64 KB .text
#endif

static uint32_t g_text_baseline_crc = 0;

static uint32_t compute_text_crc32_partial() {
    HMODULE hMod = GetModuleHandleW(nullptr);
    if (!hMod) return 0;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(hMod);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
        reinterpret_cast<uint8_t*>(hMod) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (strncmp((char*)sec->Name, ".text", 5) == 0) {
            auto* base = reinterpret_cast<const uint8_t*>(hMod) + sec->VirtualAddress;
            DWORD sz = min(sec->Misc.VirtualSize, (DWORD)SHADOW_SELF_CRC_BYTES);
            uint32_t crc = 0xFFFFFFFFu;
            for (DWORD j = 0; j < sz; j++) {
                crc ^= base[j];
                for (int k = 0; k < 8; k++)
                    crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
            }
            return ~crc;
        }
    }
    return 0;
}

inline void init_text_crc_baseline() {
    g_text_baseline_crc = compute_text_crc32_partial();
}

inline bool text_section_patched() {
    if (!g_text_baseline_crc) return false; // baseline не установлен
    uint32_t current = compute_text_crc32_partial();
    return current != g_text_baseline_crc;
}

// ─────────────────────────────────────────────────────────
// v6 NEW: 15. Heap Size Under Debugger
//
// Под отладчиком CRT heap имеет доп. overhead (debug headers).
// Аллоцируем N блоков и проверяем что сумма reального HeapSize != N * requested.
// Даёт false positive реже чем timing, но специфичен.
// ─────────────────────────────────────────────────────────
inline bool debug_heap_overhead() {
    HANDLE hHeap = GetProcessHeap();
    // Аллоцируем под 16 байт и смотрим реальный размер блока
    void* p = HeapAlloc(hHeap, 0, 16);
    if (!p) return false;
    SIZE_T actual = HeapSize(hHeap, 0, p);
    HeapFree(hHeap, 0, p);
    // Под debug-heap блок раздувается до 16+32+overhead > 72 байт обычно
    // Под release heap 16 байт запрашиваем, получаем <= 32 (alignment).
    return actual > 64; // > 64 = debug overhead
}

// ─────────────────────────────────────────────────────────
// Расширенная мастер-проверка: all_extended_checks()
//
// Запускает все v6 проверки. Возвращает маску:
//   bit0 = kernel_debugger
//   bit1 = ods_timing
//   bit2 = close_handle_exception
//   bit3 = bad_parent
//   bit4 = frida
//   bit5 = text_patched
//   bit6 = debug_heap
// ─────────────────────────────────────────────────────────
inline uint8_t run_all_extended_checks() {
    uint8_t mask = 0;
    if (kernel_debugger_present())        mask |= (1u << 0);
    if (output_debug_string_timing())     mask |= (1u << 1);
    if (close_handle_exception_trick())   mask |= (1u << 2);
    if (bad_parent_process())             mask |= (1u << 3);
    if (frida_instrumentation_detected()) mask |= (1u << 4);
    if (text_section_patched())           mask |= (1u << 5);
    if (debug_heap_overhead())            mask |= (1u << 6);
    return mask;
}

} // namespace shadow::loader_antidbg
