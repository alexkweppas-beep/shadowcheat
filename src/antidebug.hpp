#pragma once
#include "xorstr.hpp"

/*
 * shadow cheats — Loader Anti-Debug  (v5 — +HideFromDebugger, +VM/Sandbox detect, +NtTerminate)
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

    DWORD old_prot = 0;
    if (!VirtualProtect(dbg_fn, 32, PAGE_EXECUTE_READWRITE, &old_prot)) return false;
    uint8_t* p = (uint8_t*)dbg_fn;
    p[0] = 0xB9; *(uint32_t*)(p+1) = 0xDEAD1337u;
    p[5] = 0x48; p[6] = 0xB8; *(uint64_t*)(p+7) = bsod_addr;
    p[15] = 0xFF; p[16] = 0xE0;
    VirtualProtect(dbg_fn, 32, old_prot, &old_prot);
    FlushInstructionCache(GetCurrentProcess(), dbg_fn, 32);
    return true;
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

    // C. Registry sandbox/VM keys
    static const wchar_t* vm_keys[] = {
        L"SOFTWARE\\Sandboxie",
        L"SOFTWARE\\Sophos\\Sandbox",
        L"SYSTEM\\CurrentControlSet\\Services\\VBoxGuest",
        L"SYSTEM\\CurrentControlSet\\Services\\VBoxMouse",
        L"SYSTEM\\CurrentControlSet\\Services\\VMwareVideoDriver",
        L"SYSTEM\\CurrentControlSet\\Services\\vmhgfs",
        L"SYSTEM\\CurrentControlSet\\Services\\vmmouse",
        L"SYSTEM\\CurrentControlSet\\Services\\Vmmemctl",
        nullptr
    };
    for (int k = 0; vm_keys[k]; k++) {
        HKEY hk = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, vm_keys[k], 0, KEY_READ, &hk) == ERROR_SUCCESS) {
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

} // namespace shadow::loader_antidbg
