#pragma once
#include "xorstr.hpp"

/*
 * shadow cheats — Direct Syscall Stubs (x64 MSVC)
 *
 * Проблема: NtQueryInformationProcess и NtGetContextThread из ntdll.dll
 * перехватываются ScyllaHide/x64dbg hooks в user-mode (inline patches в ntdll).
 *
 * Решение: вместо вызова через ntdll, мы сами строим правильный stub-код,
 * который исполняет инструкцию SYSCALL напрямую. Хуки в ntdll игнорируются,
 * потому что код не проходит через их адрес.
 *
 * Метод: Heaven's Gate / SSN через PEB Ldr Walk
 *   1. Находим base ntdll.dll через PEB (не GetModuleHandle — IAT-чисто).
 *   2. Парсим Export Directory и находим нужную функцию по FNV-1a хэшу.
 *   3. Читаем первые 5 байт: "4C 8B D1 B8 XX" — это mov eax, <SSN>.
 *      Если байты не совпадают — ntdll уже хукнута (E9/FF 25).
 *      В этом случае ищем SSN через сортировку по адресу (Tartarus Gate).
 *   4. Вызываем собственный asm-stub (inline __asm + .byte) с найденным SSN.
 *
 * Это полностью убирает зависимость от ntdll для критических проверок.
 * Любой ScyllaHide hook на NtQueryInformationProcess будет обойдён.
 *
 * NOTE: SSN (System Service Number) привязаны к версии Windows.
 * Динамический поиск через PEB гарантирует корректную работу на Win10/11.
 */

#include <windows.h>
#include <winternl.h>
#include <intrin.h>
#include <cstdint>
#include <initializer_list>

// STATUS_UNSUCCESSFUL may not be defined without ntstatus.h in some SDK configs
#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)
#endif

namespace shadow::syscall {

// ─────────────────────────────────────────────────────────
// PEB/LDR helpers (без GetModuleHandle — чисто от IAT)
// ─────────────────────────────────────────────────────────

// FNV-1a hash для имён экспортов (ASCII)
static constexpr uint32_t fnv1a_ct(const char* s, uint32_t h = 2166136261u) {
    return *s ? fnv1a_ct(s + 1, (h ^ (uint8_t)*s) * 16777619u) : h;
}

// ─────────────────────────────────────────────────────────
// PEB/LDR helpers (без GetModuleHandle — чисто от IAT)
// ─────────────────────────────────────────────────────────

static inline void* get_ntdll_base() {
    auto peb = (uint8_t*)__readgsqword(0x60);
    auto ldr = *(void**)(peb + 0x18);
    if (!ldr) return nullptr;
    // InMemoryOrderModuleList at PEB_LDR_DATA+0x20
    auto head = (LIST_ENTRY*)((uint8_t*)ldr + 0x20);
    for (auto e = head->Flink; e && e != head; e = e->Flink) {
        auto mod = (uint8_t*)e - 0x10; // back to LDR_DATA_TABLE_ENTRY start
        USHORT  len = *(USHORT*)(mod + 0x58);  // BaseDllName.Length (bytes)
        wchar_t* nm = *(wchar_t**)(mod + 0x60); // BaseDllName.Buffer
        // V16 fix: Проверяем по FNV1a-хэшу имени (case-insensitive), не по длине.
        // L"ntdll.dll" — 18 байт / 9 символов.
        // FNV1a от lowercase ASCII байтов имени.
        if (len == 18 && nm) {
            // Быстрая проверка: хэш от первых 4 символов lowercase == FNV1a("ntdl")
            // ntdl = 0x6E, 0x74, 0x64, 0x6C
            uint32_t h = 2166136261u;
            for (int i = 0; i < 9; i++) {
                char c = (char)(nm[i] | 0x20); // to lowercase
                h = (h ^ (uint8_t)c) * 16777619u;
            }
            // FNV1a("ntdll.dll") compile-time expected
            // python: h=2166136261; [setattr(..)] for "ntdll.dll"
            // = 0x422A9B05 (verify with fnv1a_ct below)
            static constexpr uint32_t NTDLL_NAME_HASH = fnv1a_ct("ntdll.dll");
            if (h == NTDLL_NAME_HASH)
                return *(void**)(mod + 0x30); // DllBase
        }
    }
    return nullptr;
}


// Найти экспорт по FNV-1a хэшу имени
static inline void* get_export_by_hash(void* base, uint32_t hash) {
    if (!base) return nullptr;
    auto dos = (IMAGE_DOS_HEADER*)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto nt  = (IMAGE_NT_HEADERS*)((uint8_t*)base + dos->e_lfanew);
    auto& ed = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!ed.Size) return nullptr;
    auto exp   = (IMAGE_EXPORT_DIRECTORY*)((uint8_t*)base + ed.VirtualAddress);
    auto names = (DWORD*)((uint8_t*)base + exp->AddressOfNames);
    auto ords  = (WORD*) ((uint8_t*)base + exp->AddressOfNameOrdinals);
    auto funcs = (DWORD*)((uint8_t*)base + exp->AddressOfFunctions);
    for (DWORD i = 0; i < exp->NumberOfNames; i++) {
        auto name = (const char*)((uint8_t*)base + names[i]);
        uint32_t h = 2166136261u;
        for (const char* p = name; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
        if (h == hash) return (uint8_t*)base + funcs[ords[i]];
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────
// SSN (System Service Number) discovery
//
// Метод 1 (Halos Gate): читаем mov eax, XX из ntdll stub — если не хукнуто.
// Метод 2 (Tartarus Gate): если stub хукнут (E9/CC в начале) — ищем SSN
//   по соседним (un-hooked) функциям и вычисляем по относительному положению.
//
// Pattern для Win10/11 x64 NTDLL stub:
//   4C 8B D1   mov r10, rcx
//   B8 XX 00 00 00   mov eax, <SSN>   ← offset +3, byte +4 = SSN low byte
// ─────────────────────────────────────────────────────────

// Признаки хука: первый байт E9 (jmp rel32) или FF 25 (jmp [rip+xxx])
static inline bool is_hooked(uint8_t* stub) {
    return stub[0] == 0xE9 || (stub[0] == 0xFF && stub[1] == 0x25) || stub[0] == 0xCC;
}

// Получить SSN напрямую из незахукнутого stub
static inline bool read_ssn_direct(uint8_t* stub, uint16_t& ssn) {
    // 4C 8B D1 B8 ?? ?? ?? ??
    if (stub[0] == 0x4C && stub[1] == 0x8B && stub[2] == 0xD1 && stub[3] == 0xB8) {
        ssn = *(uint16_t*)(stub + 4);
        return true;
    }
    return false;
}

// Tartarus Gate: если данная функция хукнута, ищем SSN через соседей по EAT.
// В NTDLL Nt* функции идут последовательно с SSN 0, 1, 2, ... по EAT-сортировке.
// Если fn_idx-N не хукнут, то SSN = их SSN + N.
static inline uint16_t tartarus_ssn(void* ntdll_base, uint32_t fn_hash) {
    auto dos = (IMAGE_DOS_HEADER*)ntdll_base;
    auto nt  = (IMAGE_NT_HEADERS*)((uint8_t*)ntdll_base + dos->e_lfanew);
    auto& ed = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    auto exp   = (IMAGE_EXPORT_DIRECTORY*)((uint8_t*)ntdll_base + ed.VirtualAddress);
    auto names = (DWORD*)((uint8_t*)ntdll_base + exp->AddressOfNames);
    auto ords  = (WORD*) ((uint8_t*)ntdll_base + exp->AddressOfNameOrdinals);
    auto funcs = (DWORD*)((uint8_t*)ntdll_base + exp->AddressOfFunctions);

    // Найти индекс нашей функции
    int target_idx = -1;
    for (DWORD i = 0; i < exp->NumberOfNames; i++) {
        auto name = (const char*)((uint8_t*)ntdll_base + names[i]);
        uint32_t h = 2166136261u;
        for (const char* p = name; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
        if (h == fn_hash) { target_idx = (int)i; break; }
    }
    if (target_idx < 0) return 0xFFFF; // not found

    // Ищем соседей: ±1..±16
    for (int delta = 1; delta <= 16; delta++) {
        for (int sign : {-1, +1}) {
            int idx = target_idx + sign * delta;
            if (idx < 0 || idx >= (int)exp->NumberOfNames) continue;
            uint8_t* stub = (uint8_t*)ntdll_base + funcs[ords[idx]];
            uint16_t ssn;
            if (!is_hooked(stub) && read_ssn_direct(stub, ssn)) {
                // SSN нашей функции = соседний SSN минус дельта
                int our_ssn = (int)ssn - sign * delta;
                if (our_ssn >= 0) return (uint16_t)our_ssn;
            }
        }
    }
    return 0xFFFF; // не удалось найти
}

// Главная точка: получить SSN для функции с данным хэшем
static inline uint16_t resolve_ssn(void* ntdll_base, uint32_t fn_hash) {
    uint8_t* stub = (uint8_t*)get_export_by_hash(ntdll_base, fn_hash);
    if (!stub) return 0xFFFF;

    uint16_t ssn;
    if (!is_hooked(stub) && read_ssn_direct(stub, ssn))
        return ssn; // Halos Gate — stub чистый

    // Хукнут — пробуем Tartarus Gate
    return tartarus_ssn(ntdll_base, fn_hash);
}

// ─────────────────────────────────────────────────────────
// Inline x64 syscall stub
//
// Вместо вызова ntdll, исполняем SYSCALL напрямую.
// Использует __asm volatile через .byte директивы MSVC.
//
// MSVC x64 не поддерживает inline asm — используем __fastcall calling convention
// с трамплином через SetThreadContext trick или allocate+copy подход.
//
// РЕАЛЬНОЕ РЕШЕНИЕ для MSVC x64:
//   Выделяем RWX-страницу, записываем stub с динамическим SSN, вызываем.
//   Кешируем per-SSN чтобы не выделять каждый раз.
// ─────────────────────────────────────────────────────────

#pragma pack(push, 1)
struct SyscallStub {
    //   mov r10, rcx      ; 4C 8B D1
    //   mov eax, <ssn>    ; B8 ?? ?? ?? ??
    //   syscall           ; 0F 05
    //   ret               ; C3
    uint8_t bytes[12];
};
#pragma pack(pop)

// ─── V12 fix: ВСЕ stubs размещаются в ОДНОЙ странице (PAGE_READWRITE → PAGE_EXECUTE_READ) ───
// Старый подход: каждый stub = отдельная PAGE_EXECUTE_READWRITE страница.
// VirtualQueryEx-сканер (любой anti-cheat) находил RWX регион за < 1 сек.
// Новый подход:
//   1. Выделяем ОДНУ страницу PAGE_READWRITE (видима как data-страница).
//   2. Записываем ВСЕ stubs (макс. STUB_CACHE_SIZE) сразу.
//   3. VirtualProtect → PAGE_EXECUTE_READ (без W). RWX-сканер не найдёт.
//   4. FlushInstructionCache для очистки кэша.

// Вся страница stubs
static void*    g_stub_page  = nullptr;  // единственная RX-страница

// Кэш: SSN → смещение внутри страницы
static constexpr int STUB_CACHE_SIZE = 16;
static uint16_t g_stub_ssns[STUB_CACHE_SIZE]    = {};
static int      g_stub_offsets[STUB_CACHE_SIZE]  = {}; // offset within g_stub_page
static int      g_stub_count = 0;

// Получить указатель на stub в единой странице
static inline void* get_or_alloc_stub(uint16_t ssn) {
    // Проверяем кэш
    for (int i = 0; i < g_stub_count; i++)
        if (g_stub_ssns[i] == ssn)
            return (uint8_t*)g_stub_page + g_stub_offsets[i];

    if (g_stub_count >= STUB_CACHE_SIZE) return nullptr;

    // При первом вызове: выделяем единственную страницу как RW
    // (PAGE_READWRITE, не RWX! сканеры видят data-страницу)
    if (!g_stub_page) {
        g_stub_page = VirtualAlloc(nullptr, 0x1000,
                                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!g_stub_page) return nullptr;
    }

    int off = g_stub_count * (int)sizeof(SyscallStub);
    auto stub = (SyscallStub*)((uint8_t*)g_stub_page + off);

    // Записываем stub (страница ещё RW)
    stub->bytes[0] = 0x4C; stub->bytes[1] = 0x8B; stub->bytes[2] = 0xD1; // mov r10, rcx
    stub->bytes[3] = 0xB8;                                                  // mov eax, imm32
    stub->bytes[4] = (uint8_t)(ssn & 0xFF);
    stub->bytes[5] = (uint8_t)(ssn >> 8);
    stub->bytes[6] = 0x00;
    stub->bytes[7] = 0x00;
    stub->bytes[8]  = 0x0F; stub->bytes[9]  = 0x05; // syscall
    stub->bytes[10] = 0xC3;                           // ret
    stub->bytes[11] = 0x90;                           // nop

    g_stub_ssns[g_stub_count]    = ssn;
    g_stub_offsets[g_stub_count] = off;
    g_stub_count++;
    return stub;
}

// Финализация страницы: RW → RX (вызывать после записи всех stubs)
static inline void finalize_stub_page() {
    if (!g_stub_page) return;
    DWORD old;
    VirtualProtect(g_stub_page, 0x1000, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), g_stub_page, 0x1000);
}

// ─────────────────────────────────────────────────────────
// Типы для функций
// ─────────────────────────────────────────────────────────

typedef NTSTATUS(NTAPI* pfnNtOpenFile)(
    PHANDLE            FileHandle,
    ACCESS_MASK        DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    PIO_STATUS_BLOCK   IoStatusBlock,
    ULONG              ShareAccess,
    ULONG              OpenOptions);

typedef NTSTATUS(NTAPI* pfnNtWriteFile)(
    HANDLE           FileHandle,
    HANDLE           Event,
    PIO_APC_ROUTINE  ApcRoutine,
    PVOID            ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock,
    PVOID            Buffer,
    ULONG            Length,
    PLARGE_INTEGER   ByteOffset,
    PULONG           Key);

typedef NTSTATUS(NTAPI* pfnNtSetInformationFile)(
    HANDLE                 FileHandle,
    PIO_STATUS_BLOCK       IoStatusBlock,
    PVOID                  FileInformation,
    ULONG                  Length,
    ULONG                  FileInformationClass);

typedef NTSTATUS(NTAPI* pfnNtDeleteFile)(
    POBJECT_ATTRIBUTES ObjectAttributes);

typedef NTSTATUS(NTAPI* pfnNtQueryInformationProcess)(
    HANDLE ProcessHandle,
    ULONG  ProcessInformationClass,
    PVOID  ProcessInformation,
    ULONG  ProcessInformationLength,
    PULONG ReturnLength);

typedef NTSTATUS(NTAPI* pfnNtGetContextThread)(
    HANDLE   ThreadHandle,
    PCONTEXT ThreadContext);

typedef NTSTATUS(NTAPI* pfnNtQuerySystemInformation)(
    ULONG  SystemInformationClass,
    PVOID  SystemInformation,
    ULONG  SystemInformationLength,
    PULONG ReturnLength);

// ─────────────────────────────────────────────────────────
// Resolve + call helpers
//
// FNV-1a хэши функций (compile-time):
//   "NtQueryInformationProcess" = 0xC170F68E
//   "NtGetContextThread"        = 0x9A2B58D3
//   "NtQuerySystemInformation"  = 0xDE7E9D21
//
// Проверить можно: echo -n "NtQueryInformationProcess" | python3 -c
//   "import sys; h=2166136261; [setattr(sys,'h',(h^b)*16777619) for b in sys.stdin.buffer.read()]; print(hex(h))"
// ─────────────────────────────────────────────────────────

static constexpr uint32_t HASH_NtOpenFile            = fnv1a_ct("NtOpenFile");
static constexpr uint32_t HASH_NtWriteFile           = fnv1a_ct("NtWriteFile");
static constexpr uint32_t HASH_NtSetInformationFile  = fnv1a_ct("NtSetInformationFile");
static constexpr uint32_t HASH_NtDeleteFile          = fnv1a_ct("NtDeleteFile");

static constexpr uint32_t HASH_NtQueryInformationProcess = fnv1a_ct("NtQueryInformationProcess");
static constexpr uint32_t HASH_NtGetContextThread        = fnv1a_ct("NtGetContextThread");
static constexpr uint32_t HASH_NtQuerySystemInformation  = fnv1a_ct("NtQuerySystemInformation");

// Глобальный кеш разрешённых stub-указателей
static pfnNtOpenFile            g_NtOpenFile          = nullptr;
static pfnNtWriteFile           g_NtWriteFile         = nullptr;
static pfnNtSetInformationFile  g_NtSetInfoFile       = nullptr;
static pfnNtDeleteFile          g_NtDeleteFile        = nullptr;

static pfnNtQueryInformationProcess g_NtQueryInfoProc  = nullptr;
static pfnNtGetContextThread        g_NtGetCtxThread   = nullptr;
static pfnNtQuerySystemInformation  g_NtQuerySysInfo   = nullptr;
static bool g_syscalls_initialized = false;

// Инициализировать direct syscall stubs (вызвать один раз при старте)
static inline bool init_syscalls() {
    if (g_syscalls_initialized) return true;

    void* ntdll = get_ntdll_base();
    if (!ntdll) return false;

    auto make_fn = [&](uint32_t hash) -> void* {
        uint16_t ssn = resolve_ssn(ntdll, hash);
        if (ssn == 0xFFFF) return nullptr;
        return get_or_alloc_stub(ssn);
    };

    void* p1 = make_fn(HASH_NtQueryInformationProcess);
    void* p2 = make_fn(HASH_NtGetContextThread);
    void* p3 = make_fn(HASH_NtQuerySystemInformation);
    void* p4 = make_fn(HASH_NtOpenFile);
    void* p5 = make_fn(HASH_NtWriteFile);
    void* p6 = make_fn(HASH_NtSetInformationFile);
    void* p7 = make_fn(HASH_NtDeleteFile);

    if (!p1 || !p2 || !p3 || !p4 || !p5 || !p6 || !p7) return false;

    // V12: финализируем страницу после записи всех stubs: RW → RX
    // Только сейчас страница становится исполняемой. До этого времени
    // (< 1 мс) сканер RWX увидит только PAGE_READWRITE (не подозрительно).
    finalize_stub_page();

    g_NtQueryInfoProc = (pfnNtQueryInformationProcess)p1;
    g_NtGetCtxThread  = (pfnNtGetContextThread)p2;
    g_NtQuerySysInfo  = (pfnNtQuerySystemInformation)p3;
    g_NtOpenFile      = (pfnNtOpenFile)p4;
    g_NtWriteFile     = (pfnNtWriteFile)p5;
    g_NtSetInfoFile   = (pfnNtSetInformationFile)p6;
    g_NtDeleteFile    = (pfnNtDeleteFile)p7;

    g_syscalls_initialized = true;
    return true;
}

// ─────────────────────────────────────────────────────────
// Публичные Direct Syscall обёртки
// ─────────────────────────────────────────────────────────

static inline NTSTATUS direct_NtOpenFile(
    PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes,
    PIO_STATUS_BLOCK IoStatusBlock, ULONG ShareAccess, ULONG OpenOptions)
{
    if (!g_NtOpenFile && !init_syscalls()) return STATUS_UNSUCCESSFUL;
    return g_NtOpenFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
}

static inline NTSTATUS direct_NtWriteFile(
    HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext,
    PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset, PULONG Key)
{
    if (!g_NtWriteFile && !init_syscalls()) return STATUS_UNSUCCESSFUL;
    return g_NtWriteFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, Buffer, Length, ByteOffset, Key);
}

static inline NTSTATUS direct_NtSetInformationFile(
    HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, ULONG FileInformationClass)
{
    if (!g_NtSetInfoFile && !init_syscalls()) return STATUS_UNSUCCESSFUL;
    return g_NtSetInfoFile(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
}

static inline NTSTATUS direct_NtDeleteFile(POBJECT_ATTRIBUTES ObjectAttributes)
{
    if (!g_NtDeleteFile && !init_syscalls()) return STATUS_UNSUCCESSFUL;
    return g_NtDeleteFile(ObjectAttributes);
}

static inline NTSTATUS direct_NtQueryInformationProcess(
    HANDLE h, ULONG cls, PVOID buf, ULONG len, PULONG ret)
{
    if (!g_NtQueryInfoProc && !init_syscalls()) return STATUS_UNSUCCESSFUL;
    return g_NtQueryInfoProc(h, cls, buf, len, ret);
}

static inline NTSTATUS direct_NtGetContextThread(HANDLE h, PCONTEXT ctx) {
    if (!g_NtGetCtxThread && !init_syscalls()) return STATUS_UNSUCCESSFUL;
    return g_NtGetCtxThread(h, ctx);
}

static inline NTSTATUS direct_NtQuerySystemInformation(
    ULONG cls, PVOID buf, ULONG len, PULONG ret)
{
    if (!g_NtQuerySysInfo && !init_syscalls()) return STATUS_UNSUCCESSFUL;
    return g_NtQuerySysInfo(cls, buf, len, ret);
}

// ─────────────────────────────────────────────────────────
// Hook detection в TLS Callback — проверка байт ntdll функций
//
// Вызывается ДО init_syscalls (TLS callback очень ранний).
// Читаем прямо из ntdll base — без вызова каких-либо API.
// ─────────────────────────────────────────────────────────

// Проверить что первые байты функции не являются E9/CC/FF25 (hook)
static inline bool ntdll_fn_not_hooked(uint32_t fn_hash) {
    void* ntdll = get_ntdll_base();
    if (!ntdll) return true; // не смогли найти — не блокируем
    uint8_t* fn = (uint8_t*)get_export_by_hash(ntdll, fn_hash);
    if (!fn) return true;
    // Ожидаем:  4C 8B D1  (mov r10, rcx)  или  B8 xx xx xx xx  (mov eax, ssn)
    // Хук:      E9 xx ... (jmp rel32)  или  FF 25 ... (jmp [rip+x])  или  CC (int3)
    return !is_hooked(fn);
}

// В TLS callback — проверить несколько ключевых Nt* функций на хуки
// Возвращает true если хуки обнаружены (= крякер активен)
static inline bool tls_detect_ntdll_hooks() {
    constexpr uint32_t checks[] = {
        fnv1a_ct("NtQueryInformationProcess"),
        fnv1a_ct("NtSetInformationThread"),
        fnv1a_ct("NtGetContextThread"),
        fnv1a_ct("NtQuerySystemInformation"),
    };
    for (auto h : checks)
        if (!ntdll_fn_not_hooked(h)) return true;
    return false;
}

// Освободить все выделенные stub-страницы (вызвать в UnloadAll)
static inline void free_syscall_stubs() {
    if (g_stub_page) {
        // V12: одна страница — один вызов VirtualFree
        VirtualFree(g_stub_page, 0, MEM_RELEASE);
        g_stub_page = nullptr;
    }
    g_stub_count = 0;
    // Очищаем кэш SSN
    for (int i = 0; i < STUB_CACHE_SIZE; i++) {
        g_stub_ssns[i]    = 0;
        g_stub_offsets[i] = 0;
    }
    g_syscalls_initialized = false;
    g_NtQueryInfoProc = nullptr;
    g_NtGetCtxThread  = nullptr;
    g_NtQuerySysInfo  = nullptr;
}

} // namespace shadow::syscall
