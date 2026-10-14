#pragma once

/* Защита памяти процесса. Порядок установки проверок задан в Install(). */

#include <windows.h>
#include <Psapi.h>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <thread>
#include "xorstr.hpp"
#include "fibstr.hpp"
#include "lazy_importer.hpp"
#include "syscalls.hpp"
#include "vmp_markers.hpp"
#include "obfuscation.hpp"

#pragma comment(lib, "psapi.lib")

namespace shadow::anti_dump {

static std::atomic<bool> g_installed{false};
static std::atomic<bool> g_critical_process{false};

 
// 1. PE Header Erase
//    Обнуляем первые SizeOfHeaders байт нашего образа в памяти.
//    После этого дампер видит 0x00 по базовому адресу.
 
__declspec(noinline) static void EraseOwnPeHeader() {
    HMODULE hMod = LI_FN(GetModuleHandleW).safe_cached()(nullptr);
    if (!hMod) return;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(hMod);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
        reinterpret_cast<uint8_t*>(hMod) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    DWORD soh = nt->OptionalHeader.SizeOfHeaders;

    // WriteProcessMemory к себе — ядро пишет без смены флагов страницы (нет RWX).
    SIZE_T written = 0;
    WriteProcessMemory(GetCurrentProcess(), hMod, nullptr, 0, nullptr);
    // Реальное обнуление: нам нужно сделать страницу writable на момент записи
    DWORD old = 0;
    if (LI_FN(VirtualProtect).safe_cached()(hMod, soh, PAGE_READWRITE, &old)) {
        SecureZeroMemory(hMod, soh);
        LI_FN(VirtualProtect).safe_cached()(hMod, soh, PAGE_READONLY, &old);
    }
}

 
// 2. Section Name Scramble
//    Затираем имена секций в памяти случайными байтами.
//    Вызывать ПОСЛЕ EraseOwnPeHeader() — nt header уже обнулён,
//    но у нас есть копия адреса из IMAGE_FIRST_SECTION до обнуления.
//    Реально — это делается ДО EraseOwnPeHeader через отдельный проход.
 
__declspec(noinline) static void ScrambleSectionNames() {
    // Читаем из файла на диске адреса секций, потом в памяти их скремблируем.
    // Используем snapshot заголовка до EraseOwnPeHeader.
    // Поэтому Scramble вызывается ПЕРВЫМ.
    HMODULE hMod = LI_FN(GetModuleHandleW).safe_cached()(nullptr);
    if (!hMod) return;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(hMod);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
        reinterpret_cast<uint8_t*>(hMod) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;

    auto* sec = IMAGE_FIRST_SECTION(nt);
    WORD  n   = nt->FileHeader.NumberOfSections;

    // Делаем страницу заголовков writable
    DWORD old = 0;
    DWORD soh = nt->OptionalHeader.SizeOfHeaders;
    if (!LI_FN(VirtualProtect).safe_cached()(hMod, soh, PAGE_READWRITE, &old))
        return;

    // Перебиваем имена секций случайными байтами (не нулями — нули сделает EraseOwnPeHeader)
    for (WORD i = 0; i < n; i++) {
        uint64_t noise = (uint64_t)__rdtsc() ^ (uint64_t)(i + 1) * 0xDEADC0DE13370000ull;
        memcpy(sec[i].Name, &noise, min((size_t)8, sizeof(sec[i].Name)));
    }

    LI_FN(VirtualProtect).safe_cached()(hMod, soh, old, &old);
}

 
// 3. Working Set Manipulation — выгружаем страницы из RAM в pagefile
 
__declspec(noinline) static void FlushWorkingSet() {
    // EmptyWorkingSet — форсирует выгрузку всех страниц процесса в pagefile.
    // Дампер при чтении RAM-а получает PAGE_FAULT и нули/ошибки.
    LI_FN(EmptyWorkingSet).safe_cached()(GetCurrentProcess());
    // Повторяем периодически через SetProcessWorkingSetSize
    LI_FN(SetProcessWorkingSetSize).safe_cached()(GetCurrentProcess(),
        (SIZE_T)-1, (SIZE_T)-1);
}

 
// 4. Guard Pages на начале .text
//    При попытке дампера читать нашу .text — EXCEPTION_GUARD_PAGE.
//    Обработчик (наш SEH) убивает процесс.
//    ОСТОРОЖНО: наш код тоже не может читать эти страницы без снятия guard!
//    Поэтому ставим guard только на ПЕРВЫЕ 4KB (до реального кода нет).
 
static void* g_guard_page_addr = nullptr;
static SIZE_T g_guard_page_size = 0;

static LONG WINAPI GuardPageHandler(EXCEPTION_POINTERS* ep) {
    if (ep && ep->ExceptionRecord &&
        ep->ExceptionRecord->ExceptionCode == STATUS_GUARD_PAGE_VIOLATION) {
        // Дампер наткнулся на наш guard page — убиваем процесс немедленно
        // Без ban report (нечего ждать — дамп уже начался)
        // Используем TerminateProcess — guard page handler не должен делать complex calls
        TerminateProcess(GetCurrentProcess(), 0xDEAD1337);
        volatile int* p = nullptr; *p = 0; // fallback AV
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

__declspec(noinline) static void InstallGuardPages() {
    HMODULE hMod = LI_FN(GetModuleHandleW).safe_cached()(nullptr);
    if (!hMod) return;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(hMod);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return; // уже стёрт — не ставим
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
        reinterpret_cast<uint8_t*>(hMod) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;

    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (strncmp((char*)sec->Name, ".text", 5) == 0) {
            // Ставим guard на первые 4KB .text — там нет нашего кода выполнения
            // (Функции расположены после первых 4KB пролога)
            void* addr = reinterpret_cast<uint8_t*>(hMod) + sec->VirtualAddress;
            SIZE_T guard_sz = 4096; // одна страница
            DWORD old = 0;
            if (LI_FN(VirtualProtect).safe_cached()(addr, guard_sz,
                    PAGE_EXECUTE_READ | PAGE_GUARD, &old)) {
                g_guard_page_addr = addr;
                g_guard_page_size = guard_sz;
            }
            break;
        }
    }

    // Регистрируем VEH обработчик
    LI_FN(AddVectoredExceptionHandler).safe_cached()(1, GuardPageHandler);
}

 
// 5. ProcessBreakOnTermination (Critical Process)
//    ТОЛЬКО при детекте дампера/отладчика.
//    При внешнем TerminateProcess → BSOD.
//    Direct syscall, не Win32 (нет в IAT).
 
__declspec(noinline) static void MakeCriticalProcess() {
    if (g_critical_process.exchange(true)) return; // уже

    // NtSetInformationProcess(ProcessBreakOnTermination=29, &1, sizeof(ULONG))
    void* ntdll = shadow::syscall::get_ntdll_base();
    if (!ntdll) return;

    // Найти NtSetInformationProcess по FNV хэшу
    using NtSetInfoProc_t = NTSTATUS(NTAPI*)(HANDLE, UINT, PVOID, ULONG);
    auto fn = (NtSetInfoProc_t)shadow::syscall::get_export_by_hash(
        ntdll, shadow::syscall::fnv1a_ct("NtSetInformationProcess"));
    if (!fn) return;

    ULONG one = 1;
    fn(GetCurrentProcess(), 29 /*ProcessBreakOnTermination*/, &one, sizeof(one));
}

 
// 6. Anti-PE-Sieve / Hollows Hunter: затираем Export Directory
//    PE-sieve ищет "тайные" процессы по экспорт-таблице.
//    Затираем её адрес в DataDirectory — инструменты не найдут.
 
__declspec(noinline) static void EraseExportDirectory() {
    HMODULE hMod = LI_FN(GetModuleHandleW).safe_cached()(nullptr);
    if (!hMod) return;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(hMod);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return; // уже стёрт
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
        reinterpret_cast<uint8_t*>(hMod) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;

    DWORD soh = nt->OptionalHeader.SizeOfHeaders;
    DWORD old = 0;
    if (LI_FN(VirtualProtect).safe_cached()(hMod, soh, PAGE_READWRITE, &old)) {
        // Зануляем экспорт и импорт DataDirectory
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT] = {0, 0};
        // Не трогаем IAT/Import — нужны для работы (если не lazy-import)
        LI_FN(VirtualProtect).safe_cached()(hMod, soh, old, &old);
    }
}

 
// 7. Anti-Scylla IAT Scramble
//    Scylla читает IAT для реконструкции импортов.
//    Перемешиваем несколько записей IAT мусорными значениями.
//    (Реальные вызовы идут через LI_FN — IAT им не нужна)
 
__declspec(noinline) static void ScrambleIAT() {
    HMODULE hMod = LI_FN(GetModuleHandleW).safe_cached()(nullptr);
    if (!hMod) return;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(hMod);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
        reinterpret_cast<uint8_t*>(hMod) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;

    auto& iatDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT];
    if (!iatDir.VirtualAddress || !iatDir.Size) return;

    auto* iat = reinterpret_cast<uintptr_t*>(
        reinterpret_cast<uint8_t*>(hMod) + iatDir.VirtualAddress);
    SIZE_T iatSize = iatDir.Size;

    DWORD old = 0;
    if (!LI_FN(VirtualProtect).safe_cached()(iat, iatSize, PAGE_READWRITE, &old))
        return;

    // Перебиваем КАЖДЫЙ 4-й указатель мусором (оставляем часть рабочих для стабильности)
    size_t count = iatSize / sizeof(uintptr_t);
    for (size_t i = 0; i < count; i += 4) {
        // Проверяем: если по этому адресу есть реальная функция — не трогаем
        // (грубая эвристика: у нас LI_FN, реальных IAT вызовов мало)
        // Просто XOR-им старший байт — дампер восстанавливает неверный адрес
        if (iat[i]) {
            iat[i] ^= 0xFF00000000000000ull;
        }
    }

    LI_FN(VirtualProtect).safe_cached()(iat, iatSize, old, &old);
}

 
// 8. Thread Context Poisoning — фоновый поток засоряет Debug Registers
//    всех потоков в процессе случайными значениями.
//    x64dbg и WinDbg используют Dr0-Dr7 для аппаратных BP.
//    Поиск OEP через thread context ломается.
 
static std::atomic<bool> g_poison_running{false};

static void PoisonThreadContextsLoop() {
    DWORD pid = GetCurrentProcessId();
    while (g_poison_running.load()) {
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te = { sizeof(te) };
            if (Thread32First(hSnap, &te)) {
                do {
                    if (te.th32OwnerProcessID != pid) continue;
                    if (te.th32ThreadID == GetCurrentThreadId()) continue;
                    HANDLE hT = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT,
                                           FALSE, te.th32ThreadID);
                    if (!hT) continue;
                    if (SuspendThread(hT) != (DWORD)-1) {
                        CONTEXT ctx = {};
                        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
                        if (GetThreadContext(hT, &ctx)) {
                            // Засыпаем мусором Dr0-Dr3 (аппаратные BP)
                            ctx.Dr0 = (DWORD64)__rdtsc();
                            ctx.Dr1 = (DWORD64)(__rdtsc() ^ 0xDEAD1337CAFEBABEull);
                            ctx.Dr2 = (DWORD64)(__rdtsc() * 0x9E3779B97F4A7C15ull);
                            ctx.Dr3 = (DWORD64)(__rdtsc() ^ 0xA5A5A5A55A5A5A5Aull);
                            ctx.Dr7 = 0; // Отключаем условия срабатывания
                            SetThreadContext(hT, &ctx);
                        }
                        ResumeThread(hT);
                    }
                    CloseHandle(hT);
                } while (Thread32Next(hSnap, &te));
            }
            CloseHandle(hSnap);
        }
        Sleep(500); // каждые 500мс
    }
}

__declspec(noinline) static void StartContextPoisoner() {
    if (g_poison_running.exchange(true)) return;
    std::thread(PoisonThreadContextsLoop).detach();
}

 
// 9. Anti-Hollows-Hunter: затираем PEB.ImageBaseAddress
//    hollows_hunter и PE-sieve используют PEB для нахождения модуля.
//    Заменяем на мусорный адрес.
 
__declspec(noinline) static void PoisonPebImageBase() {
    auto* peb = reinterpret_cast<uint8_t*>(__readgsqword(0x60));
    // PEB+0x10 = ImageBaseAddress (x64)
    auto* imgBase = reinterpret_cast<uintptr_t*>(peb + 0x10);
    // Оставляем реальный адрес — нам самим нужен GetModuleHandle(null)!
    // Вместо этого ставим decoy в PEB.SessionId и NtGlobalFlag
    // NtGlobalFlag (PEB+0xBC): обнуляем — это сбивает heap-flag детект античитов
    auto* ntGlobalFlag = reinterpret_cast<ULONG*>(peb + 0xBC);
    *ntGlobalFlag = 0;
    // PEB.NtGlobalFlag через прямой доступ  
    // (без WinAPI — нет в IAT, не виден антивирусам)
}

 
// 10. Module List Unlink — убираем свой модуль из PEB.InMemoryOrderModuleList
//     (stealth: Process Hacker, API Monitor не видят лоадер в списке модулей)
//     ОСТОРОЖНО: некоторые WinAPI функции используют LDR — вызывать ПОСЛЕ
//     инициализации всех зависимостей.
 
__declspec(noinline) static void UnlinkFromPebModuleList() {
    // PEB->Ldr->InMemoryOrderModuleList
    auto* peb      = reinterpret_cast<uint8_t*>(__readgsqword(0x60));
    auto* ldr      = *reinterpret_cast<uint8_t**>(peb + 0x18);
    auto* memHead  = reinterpret_cast<LIST_ENTRY*>(ldr + 0x20); // InMemoryOrderModuleList
    auto* initHead = reinterpret_cast<LIST_ENTRY*>(ldr + 0x30); // InInitializationOrderModuleList
    auto* loadHead = reinterpret_cast<LIST_ENTRY*>(ldr + 0x10); // InLoadOrderModuleList

    // Первый элемент = наш exe (index 0)
    // Структура: LIST_ENTRY InOrder, LIST_ENTRY InMemory, LIST_ENTRY InInit, PVOID Base...
    // Оффсеты: InLoad=0, InMem=0x10, InInit=0x20
    struct LDR_ENTRY {
        LIST_ENTRY InLoad;
        LIST_ENTRY InMem;
        LIST_ENTRY InInit;
        PVOID Base;
    };

    auto* entry = CONTAINING_RECORD(loadHead->Flink, LDR_ENTRY, InLoad);
    if (!entry->Base) return; // не наш

    // Unlink из всех трёх списков
    auto unlink = [](LIST_ENTRY* e) {
        e->Blink->Flink = e->Flink;
        e->Flink->Blink = e->Blink;
        e->Flink = e->Blink = e; // безопасная петля
    };

    unlink(&entry->InLoad);
    unlink(&entry->InMem);
    unlink(&entry->InInit);
}

 
// ГЛАВНАЯ ФУНКЦИЯ: Install() — устанавливает все техники анти-дампа
// Вызывать в WinMain сразу после инициализации syscall stubs.
 
__declspec(noinline) static void Install() {
    if (g_installed.exchange(true)) return; // только один раз

    SHADOW_PROTECT_VIRT("anti_dump::Install");

#if !defined(SHADOW_PACKER_VMP)
     
    // EraseOwnPeHeader, ScrambleSectionNames, ScrambleIAT,
    // PoisonPebImageBase ОТКЛЮЧЕНЫ при VMP-билде.
    //
    // VMP runtime читает PE заголовок, IAT и PEB на протяжении ВСЕГО
    // времени выполнения процесса (при каждом вызове виртуализированной
    // функции). Уничтожение этих структур = немедленный краш VMP.
    //
    // VMP "Защита памяти: Да" уже обеспечивает защиту от дампа лучше
    // нашей ручной затирки — дублировать не нужно.
     

    // 1. Scramble section names (до erase header — нужен NT header)
    ScrambleSectionNames();

    // 2. Erase PE header (после scramble)
    EraseOwnPeHeader();

    // 4. Guard pages
    // InstallGuardPages(); // Осторожно: может мешать VMP runtime.

    // 6. Poison PEB
    PoisonPebImageBase();

    // 7. IAT Scramble (НЕБЕЗОПАСНО при VMP — VMP использует IAT)
    ScrambleIAT();
#endif // !SHADOW_PACKER_VMP

    // 5. Выгружаем страницы из RAM (безопасно при VMP)
    FlushWorkingSet();

    // 8. Фоновый poisoner контекстов потоков (безопасно при VMP)
    StartContextPoisoner();

    // 9. Unlink from PEB module list — отключено (ломает GetModuleHandle)
    // UnlinkFromPebModuleList();

    SHADOW_PROTECT_END();
}

 
// Вызывать ТОЛЬКО при детекте дампера/отладчика.
 
__declspec(noinline) static void EscalateOnDumperDetect() {
    // Flush working set ещё раз
    FlushWorkingSet();

    // Стать critical process → внешний kill = BSOD
    MakeCriticalProcess();
}

 
// Остановить фоновые потоки (при штатном выходе)
 
static void Shutdown() {
    g_poison_running.store(false);
}

} // namespace shadow::anti_dump
