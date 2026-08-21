#pragma once
#include "security/xorstr.hpp"

/*
 * shadow cheats — ADVANCED ANTI-REVERSE TRICKS v2
 *
 * Дополнительный слой поверх anti_analysis.hpp:
 *
 *  Блок A — Anti-Breakpoint (INT3 scan, SW BP detect)
 *  Блок B — Kernel Debugger Detection (NtQuerySystemInformation)
 *  Блок C — Anti-Attach (BreakOnTermination, CriticalProcess)
 *  Блок D — VEH Control Flow Obfuscation
 *  Блок E — Self-Modifying Code (decrypt/run/re-encrypt)
 *  Блок F — Import Table Corruption (post-mapping)
 *  Блок G — Anti-Dump (erase export/import dirs, section headers)
 *  Блок H — Memory Heap Scan (detect allocations from bad modules)
 *  Блок I — NtSetInformationProcess tricks (hide from kernel tools)
 *  Блок J — Exception-based Control Flow (SEH/VEH trampolines)
 *  Блок K — Code Checksums per-function (runtime verification)
 *  Блок L — Fake Thread Injection Detection
 *  Блок M — WOW64 Heaven's Gate monitoring
 *  Блок N — Hyper-V MSR timing tricks
 *  Блок O — Anti-Fuzzer (detect memory fuzzing patterns)
 *  Блок P — Process Mitigation Policies (raise ACG, CIG)
 *  Блок Q — Kernel Object Handle Checks
 *  Блок R — Anti-Kernel-Patch-Protection bypass detect
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#include <windows.h>
#include <winternl.h>
#include <intrin.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <atomic>
#include <cstring>
#include "skCrypter.h"
#include "api_hash.hpp"
#include "vmp_markers.hpp"

#pragma comment(lib, "ntdll.lib")

namespace shadow::advanced {

// ---------------------------------------------------------
//  Helpers
// ---------------------------------------------------------
static constexpr uint32_t crc32_byte(uint32_t crc, uint8_t b) {
    crc ^= b;
    for (int i = 0; i < 8; i++) crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    return crc;
}
static uint32_t crc32_buf(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFF;
    while (n--) c = crc32_byte(c, *p++);
    return ~c;
}

// ---------------------------------------------------------
//  БЛОК A — Anti-Breakpoint (Software BP = 0xCC INT3)
// ---------------------------------------------------------
namespace anti_bp {

    // A.1 Сканировать .text секцию на наличие INT3 (0xCC)
    // Нормально: 0xCC встречается крайне редко в коде (только как NOP-паддинг после RET)
    // Под отладчиком: 0xCC в начале функции = software breakpoint
    inline int scan_text_for_int3() {
        HMODULE hMod = GetModuleHandle(nullptr);
        auto dos = (IMAGE_DOS_HEADER*)hMod;
        auto nt  = (IMAGE_NT_HEADERS*)((uint8_t*)hMod + dos->e_lfanew);
        auto sec = IMAGE_FIRST_SECTION(nt);
        int  count = 0;
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
            if (strncmp((char*)sec->Name, skCrypt(xorstr_(".text")), 5) != 0) continue;
            uint8_t* base = (uint8_t*)hMod + sec->VirtualAddress;
            size_t   size = sec->Misc.VirtualSize;
            for (size_t j = 0; j < size - 1; j++) {
                if (base[j] == 0xCC) {
                    // Проверяем контекст: 0xCC после RET/RETN — нормальный паддинг
                    bool is_padding = (j > 0 && (base[j-1] == 0xC3 || base[j-1] == 0xC2));
                    if (!is_padding) count++;
                }
            }
        }
        return count; // > 3 = подозрительно
    }

    // A.2 Детект Memory Breakpoint — VirtualQuery на каждые 4096 байт .text
    // Если страница имеет PAGE_GUARD или изменены права — кто-то поставил memory BP
    inline bool scan_memory_breakpoints() {
        HMODULE hMod = GetModuleHandle(nullptr);
        auto dos = (IMAGE_DOS_HEADER*)hMod;
        auto nt  = (IMAGE_NT_HEADERS*)((uint8_t*)hMod + dos->e_lfanew);
        auto sec = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
            if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            uint8_t* base = (uint8_t*)hMod + sec->VirtualAddress;
            size_t size   = sec->Misc.VirtualSize;
            // Проверяем каждую страницу
            for (size_t off = 0; off < size; off += 4096) {
                MEMORY_BASIC_INFORMATION mbi = {};
                if (!VirtualQuery(base + off, &mbi, sizeof(mbi))) continue;
                // PAGE_EXECUTE_READ — нормально
                // PAGE_EXECUTE_READWRITE — кто-то снял защиту (для патча)
                // PAGE_GUARD — memory breakpoint
                if (mbi.Protect & PAGE_GUARD) return true;
                if (mbi.Protect == PAGE_EXECUTE_READWRITE) return true;
                if (mbi.Protect == PAGE_EXECUTE_WRITECOPY) return true;
            }
        }
        return false;
    }

    // A.3 Запись поверх INT3 безопасным образом (Self-healing)
    // Если нашли INT3 — восстанавливаем оригинальный байт из образа на диске
    // (это делает life harder для реверсера — BP сбрасываются сами)
    inline void heal_int3_breakpoints() {
        HMODULE hMod = GetModuleHandle(nullptr);
        auto dos = (IMAGE_DOS_HEADER*)hMod;
        if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return;
        auto nt  = (IMAGE_NT_HEADERS*)((uint8_t*)hMod + dos->e_lfanew);
        auto sec = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
            if (!(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
            uint8_t* base = (uint8_t*)hMod + sec->VirtualAddress;
            size_t size   = sec->Misc.VirtualSize;
            for (size_t j = 0; j < size - 1; j++) {
                if (base[j] == 0xCC) {
                    bool is_padding = (j > 0 && (base[j-1] == 0xC3 || base[j-1] == 0xC2));
                    if (!is_padding) {
                        // Восстанавливаем NOP (0x90) — не оригинал, но ломает BP
                        DWORD old;
                        VirtualProtect(base + j, 1, PAGE_EXECUTE_READWRITE, &old);
                        base[j] = 0x90; // NOP
                        VirtualProtect(base + j, 1, old, &old);
                    }
                }
            }
        }
    }

} // namespace anti_bp

// ---------------------------------------------------------
//  БЛОК B — Kernel Debugger Detection
// ---------------------------------------------------------
namespace kernel_dbg {

    // B.1 NtQuerySystemInformation: SystemKernelDebuggerInformation (0x23)
    struct SYSTEM_KERNEL_DEBUGGER_INFORMATION {
        BOOLEAN KernelDebuggerEnabled;
        BOOLEAN KernelDebuggerNotPresent;
    };

    inline bool is_kernel_debugger_present() {
        auto NtQSI = API::Resolve<NTSTATUS(NTAPI*)(ULONG,PVOID,ULONG,PULONG)>(
                     API::H_NtQuerySystemInformation);
        if (!NtQSI) return false;
        SYSTEM_KERNEL_DEBUGGER_INFORMATION info = {};
        ULONG ret = 0;
        NtQSI(0x23, &info, sizeof(info), &ret);
        return info.KernelDebuggerEnabled && !info.KernelDebuggerNotPresent;
    }

    // B.2 SharedUserData->KdDebuggerEnabled (0x7FFE02D4 на x64)
    // Читается без вызовов API — труднее перехватить
    inline bool shared_user_data_kd() {
        const uint8_t* sud = (const uint8_t*)0x7FFE0000;
        // KUSER_SHARED_DATA.KdDebuggerEnabled offset = 0x2D4
        return (sud[0x2D4] & 0x03) != 0;
    }

    // B.3 NtQuerySystemInformation: SystemCodeIntegrityInformation (0x67)
    // Если Code Integrity выключен (test signing) — вероятно dev/analysis среда
    inline bool code_integrity_disabled() {
        struct SYSTEM_CODEINTEGRITY_INFORMATION {
            ULONG Length;
            ULONG CodeIntegrityOptions;
        };
        auto NtQSI = API::Resolve<NTSTATUS(NTAPI*)(ULONG,PVOID,ULONG,PULONG)>(
                     API::H_NtQuerySystemInformation);
        if (!NtQSI) return false;
        SYSTEM_CODEINTEGRITY_INFORMATION ci = { sizeof(ci) };
        ULONG ret = 0;
        NtQSI(0x67, &ci, sizeof(ci), &ret);
        // Бит 0x08 = CODEINTEGRITY_OPTION_TESTSIGN (test signing)
        // Бит 0x10 = CODEINTEGRITY_OPTION_DEBUGMODE_REQUIRED (debug mode)
        return (ci.CodeIntegrityOptions & 0x08) || (ci.CodeIntegrityOptions & 0x10);
    }

    // B.4 Проверить NtGlobalFlag через NtQuerySystemInformation
    // (альтернатива PEB-чтению — другой путь кода)
    inline bool nt_global_flag_via_api() {
        // SystemGlobalFlag = 52 (0x34)
        ULONG global_flag = 0, ret = 0;
        auto NtQSI = API::Resolve<NTSTATUS(NTAPI*)(ULONG,PVOID,ULONG,PULONG)>(
                     API::H_NtQuerySystemInformation);
        if (!NtQSI) return false;
        NtQSI(0x34, &global_flag, sizeof(global_flag), &ret);
        return (global_flag & 0x70) != 0; // FLG_HEAP_ENABLE_TAIL_CHECK и др.
    }

} // namespace kernel_dbg

// ---------------------------------------------------------
//  БЛОК C — Anti-Attach (BreakOnTermination + CriticalProcess)
// ---------------------------------------------------------
namespace anti_attach {

    // C.1 NtSetInformationProcess: ProcessBreakOnTermination
    // При попытке завершить процесс через DebugActiveProcessStop — BSOD
    // (требует SeDebugPrivilege на стороне аттачера)
    inline bool set_break_on_termination() {
        typedef NTSTATUS(NTAPI* pfn)(HANDLE, ULONG, PVOID, ULONG);
        auto NtSIP = API::Resolve<pfn>(API::H_NtQueryInformationProcess); // wrong hash but same typedef
        // Правильно: NtSetInformationProcess
        HMODULE ntdll = (HMODULE)API::FindModuleByHash(API::H_ntdll);
        pfn fn = (pfn)GetProcAddress(ntdll, skCrypt(xorstr_("NtSetInformationProcess")));
        if (!fn) return false;
        ULONG val = 1;
        // ProcessBreakOnTermination = 29
        NTSTATUS st = fn(GetCurrentProcess(), 29, &val, sizeof(val));
        return NT_SUCCESS(st);
    }

    // C.2 Устанавливает флаг "критический процесс"
    // При крэше нашего процесса — система BSOD (код 0x000000EF)
    // Крякер не может просто убить процесс — будет BSOD
    inline bool set_critical_process() {
        HMODULE ntdll = (HMODULE)API::FindModuleByHash(API::H_ntdll);
        typedef NTSTATUS(NTAPI* pfnRtlSetProcessIsCritical)(BOOLEAN, PBOOLEAN, BOOLEAN);
        auto fn = (pfnRtlSetProcessIsCritical)GetProcAddress(ntdll,
                  skCrypt(xorstr_("RtlSetProcessIsCritical")));
        if (!fn) return false;
        BOOLEAN was_critical = FALSE;
        return NT_SUCCESS(fn(TRUE, &was_critical, FALSE));
    }

    // C.3 Обнаружить NtContinue — используется отладчиком для установки EIP/RIP
    // Мониторим через VEH: если вызов NtContinue без нашего разрешения > детект
    static std::atomic<bool> g_expect_nt_continue{ false };

    inline void set_expect_continue() { g_expect_nt_continue.store(true); }

    // C.4 ProcessHandleTable анализ
    // Каждый attached debugger открывает handle на наш процесс
    // Считаем handle'ы принадлежащие чужим процессам
    inline int count_foreign_handles_to_self() {
        auto NtQSI = API::Resolve<NTSTATUS(NTAPI*)(ULONG,PVOID,ULONG,PULONG)>(
                     API::H_NtQuerySystemInformation);
        if (!NtQSI) return 0;

        // SystemHandleInformation = 0x10
        ULONG size = 1 << 20; // 1MB initial
        std::vector<uint8_t> buf(size);
        ULONG ret = 0;
        NTSTATUS st;
        while ((st = NtQSI(0x10, buf.data(), (ULONG)buf.size(), &ret))
               == 0xC0000004L /*STATUS_INFO_LENGTH_MISMATCH*/) {
            size *= 2;
            buf.resize(size);
        }
        if (!NT_SUCCESS(st)) return 0;

        DWORD self_pid = GetCurrentProcessId();
        int count = 0;

        // SYSTEM_HANDLE_INFORMATION layout
        struct SYSTEM_HANDLE {
            ULONG  ProcessId;
            BYTE   ObjectTypeNumber;
            BYTE   Flags;
            USHORT Handle;
            PVOID  Object;
            ACCESS_MASK GrantedAccess;
        };
        struct SYSTEM_HANDLE_INFORMATION {
            ULONG Count;
            SYSTEM_HANDLE Handles[1];
        };

        auto info = (SYSTEM_HANDLE_INFORMATION*)buf.data();
        for (ULONG i = 0; i < info->Count; i++) {
            auto& h = info->Handles[i];
            // ObjectTypeNumber 7 = Process object
            if (h.ObjectTypeNumber == 7 && h.ProcessId != self_pid) {
                // Попробовать открыть процесс который держит handle и сравнить
                HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                            FALSE, h.ProcessId);
                if (hProc) {
                    // Это не точно, но указывает на возможный аттач
                    count++;
                    CloseHandle(hProc);
                }
            }
        }
        return count; // > 2 = подозрительно
    }

} // namespace anti_attach

// ---------------------------------------------------------
//  БЛОК D — VEH Control Flow Obfuscation
//  Переходы между функциями через намеренные исключения
// ---------------------------------------------------------
namespace veh_flow {

    // Под отладчиком Single-Step трассировка ломается на каждом исключении
    // Также: большинство BP-хуков не переживают цепочку VEH

    static std::atomic<void*> g_veh_target{ nullptr };
    static std::atomic<bool>  g_veh_fired{ false };

    static LONG WINAPI VEHDispatch(EXCEPTION_POINTERS* ep) {
        if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT) {
            void* target = g_veh_target.load();
            if (target) {
                ep->ContextRecord->Rip = (DWORD64)target;
                g_veh_target.store(nullptr);
                g_veh_fired.store(true);
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    static PVOID g_veh_handle = nullptr;

    inline void Init() {
        g_veh_handle = AddVectoredExceptionHandler(1, VEHDispatch);
    }

    // Вызов функции через исключение (обфусцированный call)
    template<typename Fn>
    inline void ObfuscatedCall(Fn* target) {
        g_veh_target.store((void*)target);
        g_veh_fired.store(false);
        __debugbreak(); // INT3 — уходим в VEH > редирект на target
        while (!g_veh_fired.load()) {} // ждём
    }

    // D.2 Single-Step Trap Check
    // Устанавливаем Trap Flag в EFLAGS — следующая инструкция вызовет EXCEPTION_SINGLE_STEP
    // Под отладчиком (особенно пошаговым) — этот exception увидит отладчик
    static bool g_single_step_ok = false;

    static LONG WINAPI SingleStepVEH(EXCEPTION_POINTERS* ep) {
        if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP) {
            g_single_step_ok = true;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    inline bool single_step_check() {
        PVOID h = AddVectoredExceptionHandler(1, SingleStepVEH);
        g_single_step_ok = false;

        // Установить Trap Flag через флаги контекста
        __try {
            // MSVC x64 intrinsics:
            __writeeflags(__readeflags() | 0x100);
        } __except(EXCEPTION_EXECUTE_HANDLER) {}

        RemoveVectoredExceptionHandler(h);
        // Если VEH поймал EXCEPTION_SINGLE_STEP — мы контролируем flow
        // Если отладчик перехватил — g_single_step_ok == false
        return !g_single_step_ok; // true = под отладчиком
    }

} // namespace veh_flow

// ---------------------------------------------------------
//  БЛОК E — Self-Modifying Code
//  Функции хранятся в зашифрованном виде, дешифруются прямо перед вызовом
// ---------------------------------------------------------
namespace smc {

    // XOR-шифр для демонстрации (в production — AES-256)
    static uint8_t xor_key[16] = {
        0xDE,0xAD,0xBE,0xEF,0xCA,0xFE,0xBA,0xBE,
        0x13,0x37,0x42,0x69,0x11,0x22,0x33,0x44
    };

    struct EncryptedFunc {
        uint8_t* start    = nullptr;
        size_t   size     = 0;
        bool     encrypted = false;
    };

    static void XorBuffer(uint8_t* buf, size_t size, const uint8_t* key, size_t key_len) {
        for (size_t i = 0; i < size; i++)
            buf[i] ^= key[i % key_len];
    }

    // Зашифровать функцию в памяти
    inline void EncryptFunc(EncryptedFunc& ef) {
        if (ef.encrypted || !ef.start) return;
        DWORD old;
        VirtualProtect(ef.start, ef.size, PAGE_EXECUTE_READWRITE, &old);
        XorBuffer(ef.start, ef.size, xor_key, sizeof(xor_key));
        VirtualProtect(ef.start, ef.size, PAGE_EXECUTE_READ, &old);
        ef.encrypted = true;
    }

    // Расшифровать функцию в памяти
    inline void DecryptFunc(EncryptedFunc& ef) {
        if (!ef.encrypted || !ef.start) return;
        DWORD old;
        VirtualProtect(ef.start, ef.size, PAGE_EXECUTE_READWRITE, &old);
        XorBuffer(ef.start, ef.size, xor_key, sizeof(xor_key));
        VirtualProtect(ef.start, ef.size, PAGE_EXECUTE_READ, &old);
        ef.encrypted = false;
    }

    // RAII guard: дешифрует при входе, шифрует при выходе
    struct FuncGuard {
        EncryptedFunc& ef;
        explicit FuncGuard(EncryptedFunc& f) : ef(f) { DecryptFunc(ef); }
        ~FuncGuard() { EncryptFunc(ef); }
    };

} // namespace smc

// ---------------------------------------------------------
//  БЛОК F — Import Table & Export Table Corruption
//  После того как DLL импорты разрешены — стираем таблицу
//  Дампер не сможет восстановить импорты
// ---------------------------------------------------------
namespace anti_dump {

    // F.1 Стереть Import Directory из PE headers
    inline void EraseImportDirectory() {
        HMODULE hMod = GetModuleHandle(nullptr);
        auto dos = (IMAGE_DOS_HEADER*)hMod;
        auto nt  = (IMAGE_NT_HEADERS*)((uint8_t*)hMod + dos->e_lfanew);
        DWORD old;
        VirtualProtect(&nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT],
                       sizeof(IMAGE_DATA_DIRECTORY), PAGE_READWRITE, &old);
        memset(&nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT],
               0, sizeof(IMAGE_DATA_DIRECTORY));
        VirtualProtect(&nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT],
                       sizeof(IMAGE_DATA_DIRECTORY), old, &old);
    }

    // F.2 Стереть Export Directory
    inline void EraseExportDirectory() {
        HMODULE hMod = GetModuleHandle(nullptr);
        auto dos = (IMAGE_DOS_HEADER*)hMod;
        auto nt  = (IMAGE_NT_HEADERS*)((uint8_t*)hMod + dos->e_lfanew);
        DWORD old;
        VirtualProtect(&nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT],
                       sizeof(IMAGE_DATA_DIRECTORY), PAGE_READWRITE, &old);
        memset(&nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT],
               0, sizeof(IMAGE_DATA_DIRECTORY));
        VirtualProtect(&nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT],
                       sizeof(IMAGE_DATA_DIRECTORY), old, &old);
    }

    // F.3 Стереть имена секций (Section Names)
    // Дампер не знает что где находится
    inline void EraseSectionNames() {
        HMODULE hMod = GetModuleHandle(nullptr);
        auto dos = (IMAGE_DOS_HEADER*)hMod;
        auto nt  = (IMAGE_NT_HEADERS*)((uint8_t*)hMod + dos->e_lfanew);
        DWORD old;
        VirtualProtect(nt, 4096, PAGE_READWRITE, &old);
        auto sec = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
            memset(sec->Name, 0, 8);
        }
        VirtualProtect(nt, 4096, old, &old);
    }

    // F.4 Перезаписать IAT случайными значениями после первого вызова
    // Функции вызываются через наш thunk — оригинальный IAT больше не нужен
    inline void CorruptIAT() {
        HMODULE hMod = GetModuleHandle(nullptr);
        auto dos = (IMAGE_DOS_HEADER*)hMod;
        auto nt  = (IMAGE_NT_HEADERS*)((uint8_t*)hMod + dos->e_lfanew);
        auto& imp_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!imp_dir.Size) return;

        DWORD old;
        auto desc = (IMAGE_IMPORT_DESCRIPTOR*)((uint8_t*)hMod + imp_dir.VirtualAddress);
        while (desc->Name) {
            if (desc->FirstThunk) {
                auto thunk = (IMAGE_THUNK_DATA*)((uint8_t*)hMod + desc->FirstThunk);
                // Снять защиту с IAT
                VirtualProtect(thunk, 8, PAGE_READWRITE, &old);
                // Записать мусор (первые 2 записи)
                thunk[0].u1.Function = 0xDEADBEEFCAFEBABEULL;
                thunk[1].u1.Function = 0x1337133713371337ULL;
                VirtualProtect(thunk, 8, old, &old);
            }
            desc++;
        }
    }

    // F.5 Стереть путь к модулю в LDR
    // После загрузки обнуляем FullDllName в PEB LDR
    struct _LDR_DATA_TABLE_ENTRY_FULL {
        LIST_ENTRY InLoadOrderLinks;
        LIST_ENTRY InMemoryOrderLinks;
        LIST_ENTRY InInitializationOrderLinks;
        PVOID DllBase;
        PVOID EntryPoint;
        ULONG SizeOfImage;
        UNICODE_STRING FullDllName;
        UNICODE_STRING BaseDllName;
    };

    inline void EraseLDRPath() {
        PPEB peb = (PPEB)__readgsqword(0x60);
        auto ldr = peb->Ldr;
        auto head = &ldr->InMemoryOrderModuleList;
        auto self_base = (void*)GetModuleHandle(nullptr);

        for (auto e = head->Flink; e != head; e = e->Flink) {
            auto mod = CONTAINING_RECORD(e, _LDR_DATA_TABLE_ENTRY_FULL, InMemoryOrderLinks);
            if (mod->DllBase == self_base) {
                if (mod->FullDllName.Buffer)
                    memset(mod->FullDllName.Buffer, 0, mod->FullDllName.MaximumLength);
                if (mod->BaseDllName.Buffer)
                    memset(mod->BaseDllName.Buffer, 0, mod->BaseDllName.MaximumLength);
                break;
            }
        }
    }

    // F.6 NtSetInformationProcess: скрыть адрес base image
    inline void HideImageBaseFromPEB() {
        PPEB peb = (PPEB)__readgsqword(0x60);
        // ImageBaseAddress в PEB: offset 0x10 (x64)
        // Обнулить его — Process Hacker не покажет base
        DWORD old;
        if (VirtualProtect(peb, sizeof(*peb), PAGE_READWRITE, &old)) {
            *(PVOID*)((uint8_t*)peb + 0x10) = nullptr;
            VirtualProtect(peb, sizeof(*peb), old, &old);
        }
    }

    // F.7 Мастер-функция — вызвать после инициализации
    inline void InitAntiDump() {
        EraseImportDirectory();
        EraseExportDirectory();
        EraseSectionNames();
        EraseLDRPath();
        HideImageBaseFromPEB();
        // CorruptIAT() — вызывать ТОЛЬКО если используем собственный thunk для вызовов
    }

} // namespace anti_dump

// ---------------------------------------------------------
//  БЛОК G — Thread Analysis Detection
// ---------------------------------------------------------
namespace thread_analysis {

    // G.1 Проверить есть ли потоки созданные не нами
    // Инструменты инжектируют потоки для мониторинга
    inline bool detect_foreign_threads() {
        DWORD self_pid = GetCurrentProcessId();
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSnap == INVALID_HANDLE_VALUE) return false;

        THREADENTRY32 te = { sizeof(te) };
        int our_threads = 0;
        int total_threads = 0;

        if (Thread32First(hSnap, &te)) {
            do {
                if (te.th32OwnerProcessID != self_pid) continue;
                total_threads++;

                // Попробовать получить start address потока
                HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                if (hThread) {
                    // NtQueryInformationThread: ThreadQuerySetWin32StartAddress = 9
                    PVOID start_addr = nullptr;
                    ULONG ret_len = 0;
                    auto NtQIT = API::Resolve<NTSTATUS(NTAPI*)(HANDLE,ULONG,PVOID,ULONG,PULONG)>(
                                  API::fnv1a("NtQueryInformationThread"));
                    if (NtQIT) {
                        NtQIT(hThread, 9, &start_addr, sizeof(start_addr), &ret_len);
                        // Проверить что адрес старта внутри нашего модуля
                        HMODULE hMod = GetModuleHandle(nullptr);
                        auto dos = (IMAGE_DOS_HEADER*)hMod;
                        auto nt  = (IMAGE_NT_HEADERS*)((uint8_t*)hMod + dos->e_lfanew);
                        uintptr_t mod_start = (uintptr_t)hMod;
                        uintptr_t mod_end   = mod_start + nt->OptionalHeader.SizeOfImage;
                        if ((uintptr_t)start_addr >= mod_start
                         && (uintptr_t)start_addr < mod_end) {
                            our_threads++;
                        }
                    }
                    CloseHandle(hThread);
                }
            } while (Thread32Next(hSnap, &te));
        }
        CloseHandle(hSnap);

        // Если есть потоки с неизвестными start addresses — возможно инжекция
        return (total_threads - our_threads) > 2;
    }

    // G.2 Мониторинг создания новых потоков в нашем процессе
    // Используем SetWindowsHookEx (low-level) — нет, лучше CreateToolhelp32Snapshot polling
    static std::atomic<int> g_known_thread_count{ 0 };

    inline void InitThreadBaseline() {
        DWORD self_pid = GetCurrentProcessId();
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSnap == INVALID_HANDLE_VALUE) return;
        THREADENTRY32 te = { sizeof(te) };
        int count = 0;
        if (Thread32First(hSnap, &te))
            do { if (te.th32OwnerProcessID == self_pid) count++; }
            while (Thread32Next(hSnap, &te));
        CloseHandle(hSnap);
        g_known_thread_count.store(count);
    }

    inline bool detect_new_threads() {
        DWORD self_pid = GetCurrentProcessId();
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSnap == INVALID_HANDLE_VALUE) return false;
        THREADENTRY32 te = { sizeof(te) };
        int count = 0;
        if (Thread32First(hSnap, &te))
            do { if (te.th32OwnerProcessID == self_pid) count++; }
            while (Thread32Next(hSnap, &te));
        CloseHandle(hSnap);
        int known = g_known_thread_count.load();
        if (count > known + 2) { // +2 чтобы не ложить при норм старте
            g_known_thread_count.store(count);
            return true;
        }
        return false;
    }

} // namespace thread_analysis

// ---------------------------------------------------------
//  БЛОК H — Process Mitigation Policies (ACG/CIG)
// ---------------------------------------------------------
namespace mitigation {

    // H.1 Arbitrary Code Guard (ACG)
    // После включения — нельзя создать исполняемую память извне процесса
    // Ломает инжектор и CE code cave'ы
    inline bool EnableACG() {
        PROCESS_MITIGATION_DYNAMIC_CODE_POLICY p = {};
        p.ProhibitDynamicCode = 1;
        return SetProcessMitigationPolicy(ProcessDynamicCodePolicy, &p, sizeof(p)) != FALSE;
    }

    // H.2 Code Integrity Guard (CIG)
    // Только подписанные DLL могут загружаться в процесс
    // Ломает большинство инжекторов
    inline bool EnableCIG() {
        PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY p = {};
        p.MicrosoftSignedOnly = 0; // 0 = только Microsoft, 1 = Windows Store
        p.MitigationOptIn = 1;
        return SetProcessMitigationPolicy(ProcessSignaturePolicy, &p, sizeof(p)) != FALSE;
    }

    // H.3 Extension Point Disable Policy
    // Запрещает AppInit_DLLs, глобальные хуки и browser helper objects
    inline bool DisableExtensionPoints() {
        PROCESS_MITIGATION_EXTENSION_POINT_DISABLE_POLICY p = {};
        p.DisableExtensionPoints = 1;
        return SetProcessMitigationPolicy(ProcessExtensionPointDisablePolicy, &p, sizeof(p)) != FALSE;
    }

    // H.4 Control Flow Guard усиление
    inline bool EnableStrictCFG() {
        PROCESS_MITIGATION_CONTROL_FLOW_GUARD_POLICY p = {};
        p.StrictMode = 1;
        return SetProcessMitigationPolicy(ProcessControlFlowGuardPolicy, &p, sizeof(p)) != FALSE;
    }

    // H.5 Мастер-инициализация
    inline void InitAll() {
        EnableACG();           // Ломает code injection
        EnableCIG();           // Ломает DLL injection
        DisableExtensionPoints(); // Ломает AppInit hacks
        // EnableStrictCFG(); // Включить осторожно — может сломать свой код
    }

} // namespace mitigation

// ---------------------------------------------------------
//  БЛОК I — Timing & CPU Consistency Checks
// ---------------------------------------------------------
namespace cpu_checks {

    // I.1 Количество логических ядер vs CPUID
    // VM/Sandbox иногда врёт в GetSystemInfo но не в CPUID
    inline bool cpu_count_mismatch() {
        int cpuid_info[4];
        __cpuid(cpuid_info, 1);
        int cpuid_cores = (cpuid_info[1] >> 16) & 0xFF; // EBX[23:16]

        SYSTEM_INFO si;
        GetSystemInfo(&si);
        int api_cores = (int)si.dwNumberOfProcessors;

        return cpuid_cores > 0 && abs(cpuid_cores - api_cores) > 2;
    }

    // I.2 RDTSC vs CPU frequency consistency
    // Получаем частоту CPU через реестр, сравниваем с измеренной
    inline bool cpu_freq_mismatch() {
        HKEY hKey;
        DWORD mhz = 0, size = sizeof(mhz);
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                          skCrypt(xorstr_("HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0")),
                          0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            RegQueryValueExA(hKey, skCrypt(xorstr_("~MHz")), nullptr, nullptr, (LPBYTE)&mhz, &size);
            RegCloseKey(hKey);
        }
        if (!mhz) return false;

        // Измеряем через RDTSC за 10мс
        UINT64 t1 = __rdtsc();
        Sleep(10);
        UINT64 t2 = __rdtsc();
        UINT64 measured_mhz = (t2 - t1) / 10000; // ticks/10ms = khz > /1000 = MHz... нет /10000 = MHz*10 /10 = MHz

        // Допуск: ±30%
        UINT64 low  = (UINT64)mhz * 70 / 100;
        UINT64 high = (UINT64)mhz * 130 / 100;
        return measured_mhz < low || measured_mhz > high;
    }

    // I.3 CPUID Consistency — разные листья должны быть согласованы
    inline bool cpuid_inconsistent() {
        int info0[4], info1[4], info7[4];
        __cpuid(info0, 0);
        __cpuid(info1, 1);
        __cpuid(info7, 7);

        // Max leaf из CPUID.0:EAX должен быть >= 1
        if (info0[0] < 1) return true;

        // Если гипервайзор бит есть но vendor не совпадает — кто-то спуфит
        bool hv_bit = (info1[2] >> 31) & 1;
        if (hv_bit) {
            int hv_info[4];
            __cpuid(hv_info, 0x40000000);
            // Валидный гипервайзор должен возвращать строку >= 4 символов
            if (hv_info[0] == 0) return true; // Пустой vendor при HV бите = спуф
        }
        return false;
    }

    // I.4 MXCSR register check
    // SSE control/status register — под VM иногда имеет нестандартные значения
    inline bool mxcsr_anomaly() {
        uint32_t mxcsr = _mm_getcsr();
        // Нормальный MXCSR: 0x1F80 (все маски исключений установлены, Round-to-Nearest)
        // Аномально: биты 6-12 (exception flags) = ненулевые при старте
        return (mxcsr & 0x003F) != 0 && (mxcsr & 0x003F) != 0x003F;
    }

} // namespace cpu_checks

// ---------------------------------------------------------
//  БЛОК J — Network Reachability Check
//  Sandbox/VM часто не имеют доступа к сети или имеют fake DNS
// ---------------------------------------------------------
namespace net_checks {

    // J.1 Проверить что наш домен резолвится в ожидаемый IP
    // Если DNS поддельный (sandbox redirect) — IP будет другой
    inline bool dns_poisoned(const char* expected_ip_prefix) {
        // Используем ws2_32!gethostbyname через hash
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) return false;

        auto fn = (hostent*(__stdcall*)(const char*))GetProcAddress(
                   GetModuleHandleA(skCrypt(xorstr_("ws2_32.dll"))), skCrypt(xorstr_("gethostbyname")));
        if (!fn) { WSACleanup(); return false; }

        auto he = fn(skCrypt(xorstr_("api.shadowcheats.cc")));
        if (!he || !he->h_addr_list[0]) { WSACleanup(); return false; }

        char ip[16] = {};
        snprintf(ip, sizeof(ip), xorstr_("%d.%d.%d.%d"),
                 (uint8_t)he->h_addr_list[0][0], (uint8_t)he->h_addr_list[0][1],
                 (uint8_t)he->h_addr_list[0][2], (uint8_t)he->h_addr_list[0][3]);

        WSACleanup();
        return strncmp(ip, expected_ip_prefix, strlen(expected_ip_prefix)) != 0;
    }

} // namespace net_checks

// ---------------------------------------------------------
//  БЛОК K — Anti-Fuzzing
// ---------------------------------------------------------
namespace anti_fuzz {

    // K.1 Детект AFL/libFuzzer: они устанавливают __AFL_COVERAGE_INIT
    inline bool detect_afl() {
        // AFL инжектирует в адресное пространство shared memory для coverage
        // Ищем память с характерными паттернами
        MEMORY_BASIC_INFORMATION mbi = {};
        uintptr_t addr = 0;
        while (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) {
            if (mbi.Type == MEM_MAPPED && mbi.RegionSize == 65536) {
                // AFL coverage bitmap = ровно 64KB mapped section
                return true;
            }
            addr += mbi.RegionSize;
            if (!addr) break;
        }
        return false;
    }

    // K.2 Детект Address Sanitizer (ASAN)
    // ASAN добавляет "shadow memory" — огромные регионы в 0x7FFF8000_00000000 и выше
    inline bool detect_asan() {
        // ASAN shadow region на Windows: 0x00030000_00000000 - 0x00040000_00000000
        // Проверяем несколько адресов
        const uintptr_t asan_addrs[] = {
            0x0003000000000000ULL,
            0x0007fff800000000ULL,
        };
        for (auto test_addr : asan_addrs) {
            MEMORY_BASIC_INFORMATION mbi = {};
            if (VirtualQuery((LPCVOID)test_addr, &mbi, sizeof(mbi))
                && mbi.State == MEM_COMMIT) {
                return true;
            }
        }
        return false;
    }

} // namespace anti_fuzz

// ---------------------------------------------------------
//  БЛОК L — Anti-Tamper: Multi-Layer Checksums
// ---------------------------------------------------------
namespace checksums {

    // Хешируем не всю .text, а отдельные "ключевые" функции
    // Так мы точнее знаем ГДЕ патч произошёл

    struct FuncChecksum {
        void*    fn_ptr;
        size_t   fn_size;
        uint32_t expected_crc;
        const char* name; // skCrypt
    };

    static std::vector<FuncChecksum> g_checksums;

    inline void RegisterFunc(void* fn, size_t size, const char* name) {
        uint32_t crc = crc32_buf((uint8_t*)fn, size);
        g_checksums.push_back({ fn, size, crc, name });
    }

    inline bool VerifyAll() {
        for (auto& fc : g_checksums) {
            uint32_t cur = crc32_buf((uint8_t*)fc.fn_ptr, fc.fn_size);
            if (cur != fc.expected_crc) return true; // Пропатчено
        }
        return false;
    }

    // Авто-регистрация функций через макрос
    // REGISTER_FUNC(MyFunc, 256) — зарегистрировать 256 байт MyFunc
    #define REGISTER_CHECKSUM(fn, size) \
        shadow::advanced::checksums::RegisterFunc((void*)&fn, size, #fn)

} // namespace checksums

// ---------------------------------------------------------
//  БЛОК M — Polymorphic Decoy Code
//  Генерирует фиктивный "похожий" код который реверсер будет анализировать впустую
// ---------------------------------------------------------
namespace decoy {

    // Это функции которые ВЫГЛЯДЯТ как настоящая защита
    // но на самом деле делают ничего полезного
    // IDA потратит время анализируя их

    __declspec(noinline) static int FakeDecryptRoutine(uint8_t* buf, size_t size, uint32_t key) {
        volatile uint32_t state = key ^ 0xDEADBEEF;
        for (size_t i = 0; i < size; i++) {
            state = (state << 7) | (state >> 25);
            state ^= buf[i];
            buf[i] = (uint8_t)(state & 0xFF);
        }
        return (int)(state & 0x7FFFFFFF);
    }

    __declspec(noinline) static bool FakeNetworkAuth(const char* token, uint32_t hwid) {
        volatile uint32_t h = hwid;
        volatile size_t len = 0;
        while (token[len]) { h ^= token[len++]; h = (h << 3) | (h >> 29); }
        return (h & 0xFF) == 0x42; // Всегда false (h никогда не == 0x42 от реального HWID)
    }

    __declspec(noinline) static void FakeSelfDelete() {
        // Выглядит как самоудаление файла — но ничего не делает
        volatile HANDLE h = INVALID_HANDLE_VALUE;
        volatile BOOL r = FALSE;
        (void)h; (void)r;
    }

    // Создать decoy thread который делает "что-то важное"
    static DWORD WINAPI DecoyThread(LPVOID) {
        volatile uint64_t counter = 0;
        uint8_t fake_buf[64] = {};
        while (true) {
            counter++;
            FakeDecryptRoutine(fake_buf, sizeof(fake_buf), (uint32_t)counter);
            if ((counter & 0xFFFF) == 0) {
                FakeNetworkAuth(skCrypt(xorstr_("shadow_token")), (uint32_t)GetTickCount());
            }
            Sleep(1);
        }
        return 0;
    }

    inline void SpawnDecoyThreads(int count = 3) {
        for (int i = 0; i < count; i++) {
            HANDLE h = CreateThread(nullptr, 0, DecoyThread, nullptr, 0, nullptr);
            if (h) CloseHandle(h);
        }
    }

} // namespace decoy

// ---------------------------------------------------------
//  МАСТЕР — RunAdvancedChecks()
// ---------------------------------------------------------

struct AdvancedCheckResult {
    bool int3_found         = false;
    bool memory_bp          = false;
    bool kernel_debugger    = false;
    bool kd_shared_data     = false;
    bool ci_disabled        = false;
    bool foreign_threads    = false;
    bool new_thread_inject  = false;
    bool fuzzer_detected    = false;
    bool anti_dump_needed   = false;
    bool checksum_fail      = false;
    bool cpu_inconsistent   = false;
    bool any_threat         = false;
    const char* first_hit   = nullptr;
};

inline AdvancedCheckResult RunAdvancedChecks() {
    AdvancedCheckResult r;
    auto hit = [&](bool cond, const char* name) {
        if (cond) { r.any_threat = true; if (!r.first_hit) r.first_hit = name; }
        return cond;
    };

    // VMProtect uses INT3 for virtualization and alters memory page permissions heavily.
    // r.int3_found      = hit(anti_bp::scan_text_for_int3() > 3,
    //                           skCrypt("INT3_IN_TEXT"));
    // r.memory_bp       = hit(anti_bp::scan_memory_breakpoints(),
    //                           skCrypt("MEMORY_BP"));
    r.kernel_debugger = hit(kernel_dbg::is_kernel_debugger_present(),
                              skCrypt(xorstr_("KERNEL_DBG")));
    r.kd_shared_data  = hit(kernel_dbg::shared_user_data_kd(),
                              skCrypt(xorstr_("SHARED_DATA_KD")));
    r.ci_disabled     = hit(kernel_dbg::code_integrity_disabled(),
                              skCrypt(xorstr_("CI_DISABLED")));
    r.foreign_threads = hit(thread_analysis::detect_foreign_threads(),
                              skCrypt(xorstr_("FOREIGN_THREAD")));
    r.new_thread_inject = hit(thread_analysis::detect_new_threads(),
                              skCrypt(xorstr_("THREAD_INJECT")));
    r.fuzzer_detected = hit(anti_fuzz::detect_afl() || anti_fuzz::detect_asan(),
                              skCrypt(xorstr_("FUZZER")));
    // VMProtect modifies function bytes, breaking checksums.
    // r.checksum_fail   = hit(checksums::VerifyAll(),
    //                           skCrypt("CHECKSUM_FAIL"));
    r.cpu_inconsistent = hit(cpu_checks::cpu_count_mismatch()
                           || cpu_checks::cpuid_inconsistent(),
                              skCrypt(xorstr_("CPU_INCONSISTENT")));

    // NOTE v2: self-heal INT3 убран.
    //   heal_int3_breakpoints() переписывает 0xCC > 0x90 в .text, что ломает
    //   integrity::check() (CRC был посчитан по оригинальным байтам) >
    //   сам-себя-бан по INTEGRITY_FAIL. ?? detection-only: INT3 найден = бан
    //   через ADVANCED_THREAT branch ниже по RunAdvancedChecks.

    return r;
}

// Инициализация всей системы (вызвать в Init())
inline void InitAdvanced() {
    // Запустить decoy потоки (реверсер потратит время на них)
    decoy::SpawnDecoyThreads(3);

    // Baseline потоков (до запуска security loop)
    thread_analysis::InitThreadBaseline();

    // Process Mitigations
    // mitigation::InitAll();

    // Anti-Attach (требует SeDebugPrivilege — работает не всегда)
    anti_attach::set_break_on_termination();

    // Anti-Dump после полной инициализации
    // anti_dump::InitAntiDump();

    // VEH для control flow
    // veh_flow::Init();
}

} // namespace shadow::advanced

