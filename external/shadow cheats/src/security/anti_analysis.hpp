#pragma once
#include "security/xorstr.hpp"

/*
 * shadow cheats — ANTI-ANALYSIS ENGINE
 * Tier: EXTREME
 *
 * Содержит десятки низкоуровневых методов обнаружения:
 *  - Anti-Debug (PEB, NtQuery, Heap, TLS, Exception tricks)
 *  - Anti-VM / Anti-Sandbox
 *  - Anti-Hook (Inline, SSDT, IAT)
 *  - Anti-Dump (PE header erasure, section crypt)
 *  - Timing Attacks (RDTSC, GetTickCount, QueryPerformance)
 *  - Hardware Fingerprint (DR registers, CPU id, MSR)
 *  - Process / Window enumeration
 *  - ETW / Telemetry Patching
 *  - Parent Process Spoofing Detection
 *  - Driver / Module integrity
 */

#include "../../../../фикс/лоадер/src/obfuscation.hpp"
#include "skCrypter.h"
#include "vmp_markers.hpp"
#include <chrono>
#include <comdef.h>
#include <functional>
#include <intrin.h>
#include <iphlpapi.h>
#include <psapi.h>
#include <shlobj.h>
#include <string>
#include <thread>
#include <tlhelp32.h>
#include <vector>
#include <wbemidl.h>
#include <wincrypt.h>
#include <windows.h>
#include <winternl.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "wbemuuid.lib")

// ---------------------------------------------------------
//  Undocumented NT structures & function typedefs
// ---------------------------------------------------------

typedef LONG NTSTATUS;
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)

typedef enum _PROCESSINFOCLASS2 {
  ShadowProcessDebugPort = 7,
  ShadowProcessDebugObjectHandle = 30,
  ShadowProcessDebugFlags = 31,
  ShadowProcessBreakOnTermination = 29,
  ShadowProcessHandleCount = 20,
} PROCESSINFOCLASS2;

typedef enum _THREADINFOCLASS2 {
  ShadowThreadHideFromDebugger = 17,
  ShadowThreadBreakOnTermination = 18,
} THREADINFOCLASS2;

typedef NTSTATUS(NTAPI *pfnNtQueryInformationProcess)(HANDLE, PROCESSINFOCLASS,
                                                      PVOID, ULONG, PULONG);
typedef NTSTATUS(NTAPI *pfnNtSetInformationThread)(HANDLE, ULONG, PVOID, ULONG);
typedef NTSTATUS(NTAPI *pfnNtQuerySystemInformation)(ULONG, PVOID, ULONG,
                                                     PULONG);
typedef NTSTATUS(NTAPI *pfnNtQueryObject)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef NTSTATUS(NTAPI *pfnNtClose)(HANDLE);
typedef BOOL(WINAPI *pfnIsWow64Process)(HANDLE, PBOOL);
typedef NTSTATUS(NTAPI *pfnNtYieldExecution)();

// PEB / LDR helpers
#define PEB_OFFSET 0x60 // GS:[0x60] on x64
#define NT_GLOBAL_FLAG_DBG                                                     \
  0x70 // FLG_HEAP_ENABLE_TAIL_CHECK | FLG_HEAP_ENABLE_FREE_CHECK |
       // FLG_HEAP_VALIDATE_PARAMETERS

struct HEAP_ENTRY_S {
  USHORT Size;
  UCHAR Flags;
  UCHAR SmallTagIndex;
  USHORT PreviousSize;
  UCHAR SegmentOffset;
  UCHAR ExtendedBlockSignature;
};

// ---------------------------------------------------------
//  Helper: resolve NT function dynamically (no GetProcAddress trace)
// ---------------------------------------------------------
namespace shadow::nt {

static HMODULE ntdll_base() {
  // Walk PEB InMemoryOrderModuleList — не вызываем GetModuleHandle
  PPEB peb = (PPEB)__readgsqword(PEB_OFFSET);
  auto ldr = peb->Ldr;
  auto head = &ldr->InMemoryOrderModuleList;
  for (auto entry = head->Flink; entry != head; entry = entry->Flink) {
    // winternl.h doesn't fully expose BaseDllName, so we check FullDllName size
    // ntdll.dll full path ends with ntdll.dll or is just ntdll.dll
    auto mod =
        CONTAINING_RECORD(entry, LDR_DATA_TABLE_ENTRY, InMemoryOrderLinks);
    if (mod->FullDllName.Buffer != nullptr) {
      // Check if buffer contains "ntdll.dll"
      int len = mod->FullDllName.Length / 2;
      if (len >= 9) {
        wchar_t *name = mod->FullDllName.Buffer;
        if (_wcsnicmp(&name[len - 9], L"ntdll.dll", 9) == 0) {
          return (HMODULE)mod->DllBase;
        }
      }
    }
  }
  return nullptr;
}

// FNV-1a hash для имени экспорта (compile-time capable)
static constexpr uint32_t fnv1a(const char *str, uint32_t h = 2166136261u) {
  return *str ? fnv1a(str + 1, (h ^ (uint8_t)*str) * 16777619u) : h;
}

template <typename T> static T resolve(uint32_t name_hash) {
  HMODULE base = ntdll_base();
  if (!base)
    return nullptr;
  auto dos = (IMAGE_DOS_HEADER *)base;
  auto nt = (IMAGE_NT_HEADERS *)((uint8_t *)base + dos->e_lfanew);
  auto exp_dir =
      &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  auto exp =
      (IMAGE_EXPORT_DIRECTORY *)((uint8_t *)base + exp_dir->VirtualAddress);

  auto names = (DWORD *)((uint8_t *)base + exp->AddressOfNames);
  auto ordinals = (WORD *)((uint8_t *)base + exp->AddressOfNameOrdinals);
  auto funcs = (DWORD *)((uint8_t *)base + exp->AddressOfFunctions);

  for (DWORD i = 0; i < exp->NumberOfNames; i++) {
    const char *name = (const char *)((uint8_t *)base + names[i]);
    if (fnv1a(name) == name_hash) {
      return (T)((uint8_t *)base + funcs[ordinals[i]]);
    }
  }
  return nullptr;
}

// Precomputed hashes (compile-time)
static constexpr uint32_t H_NtQueryInformationProcess =
    fnv1a("NtQueryInformationProcess");
static constexpr uint32_t H_NtSetInformationThread =
    fnv1a("NtSetInformationThread");
static constexpr uint32_t H_NtQuerySystemInformation =
    fnv1a("NtQuerySystemInformation");
static constexpr uint32_t H_NtQueryObject = fnv1a("NtQueryObject");
static constexpr uint32_t H_NtClose = fnv1a("NtClose");
static constexpr uint32_t H_NtYieldExecution = fnv1a("NtYieldExecution");
} // namespace shadow::nt

// ---------------------------------------------------------
//  БЛОК 1 — ANTI-DEBUG (PEB / NtQuery)
// ---------------------------------------------------------
namespace shadow::anti_debug {

// 1.1 Классический PEB.BeingDebugged
inline bool peb_being_debugged() {
  PPEB peb = (PPEB)__readgsqword(PEB_OFFSET);
  bool res = peb->BeingDebugged != 0;
  return res;
}

// 1.2 PEB.NtGlobalFlag — при отладке = 0x70
inline bool peb_nt_global_flag() {
  PPEB peb = (PPEB)__readgsqword(PEB_OFFSET);
  ULONG flags = *(ULONG *)((uint8_t *)peb + 0xBC); // NtGlobalFlag offset x64
  return (flags & NT_GLOBAL_FLAG_DBG) != 0;
}

// 1.3 Heap Flags — при отладке heap.Flags != HEAP_GROWABLE
inline bool heap_flags() {
  PPEB peb = (PPEB)__readgsqword(PEB_OFFSET);
  PVOID heap = *(PVOID *)((uint8_t *)peb + 0x30); // x64 PEB.ProcessHeap offset
  // offset Flags = 0x70 (x64 Win10+), ForceFlags = 0x74
  ULONG flags = *(ULONG *)((uint8_t *)heap + 0x70);
  ULONG forceflags = *(ULONG *)((uint8_t *)heap + 0x74);
  return (flags & ~0x2) != 0 || forceflags != 0; // 0x2 = HEAP_GROWABLE
}

// 1.4 NtQueryInformationProcess: ProcessDebugPort
inline bool nt_debug_port() {
  auto fn = shadow::nt::resolve<pfnNtQueryInformationProcess>(
      shadow::nt::H_NtQueryInformationProcess);
  if (!fn)
    return false;
  DWORD port = 0;
  fn(GetCurrentProcess(), (PROCESSINFOCLASS)ShadowProcessDebugPort, &port,
     sizeof(port), nullptr);
  return port != 0;
}

// 1.5 NtQueryInformationProcess: ProcessDebugObjectHandle
inline bool nt_debug_object_handle() {
  auto fn = shadow::nt::resolve<pfnNtQueryInformationProcess>(
      shadow::nt::H_NtQueryInformationProcess);
  if (!fn)
    return false;
  HANDLE hDbg = nullptr;
  NTSTATUS st =
      fn(GetCurrentProcess(), (PROCESSINFOCLASS)ShadowProcessDebugObjectHandle,
         &hDbg, sizeof(hDbg), nullptr);
  // Если дебаг объект есть — NT_SUCCESS и handle != null
  return NT_SUCCESS(st) && hDbg != nullptr;
}

// 1.6 NtQueryInformationProcess: ProcessDebugFlags
// Возвращает 0 если отлаживается, 1 если нет
inline bool nt_debug_flags() {
  auto fn = shadow::nt::resolve<pfnNtQueryInformationProcess>(
      shadow::nt::H_NtQueryInformationProcess);
  if (!fn)
    return false;
  ULONG flags = 1;
  fn(GetCurrentProcess(), (PROCESSINFOCLASS)ShadowProcessDebugFlags, &flags,
     sizeof(flags), nullptr);
  return flags == 0; // 0 = отлаживается
}

// 1.7 Hardware Breakpoints через GetThreadContext (Dr0-Dr3)
inline bool hardware_breakpoints() {
  CONTEXT ctx = {};
  ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
  if (!GetThreadContext(GetCurrentThread(), &ctx))
    return false;
  return (ctx.Dr0 || ctx.Dr1 || ctx.Dr2 || ctx.Dr3 || ctx.Dr6 || ctx.Dr7);
}

// 1.8 CloseHandle(invalid) — вызывает EXCEPTION_INVALID_HANDLE только под
// отладчиком
inline bool closeinvalid_handle_trick() {
  bool detected = false;
  __try {
    // Намеренно передаём невалидный handle
    auto fn = shadow::nt::resolve<pfnNtClose>(shadow::nt::H_NtClose);
    if (fn)
      fn((HANDLE)0xDEADC0DEULL);
  } __except (GetExceptionCode() == EXCEPTION_INVALID_HANDLE
                  ? EXCEPTION_EXECUTE_HANDLER
                  : EXCEPTION_CONTINUE_SEARCH) {
    detected = true; // Исключение поймано — значит отладчик его нам вернул
  }
  return detected;
}

// 1.9 SeDebugPrivilege в токене процесса
// Нормальный процесс не имеет SeDebugPrivilege
inline bool se_debug_privilege() {
  HANDLE hToken = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
    return false;
  PRIVILEGE_SET ps = {};
  ps.PrivilegeCount = 1;
  ps.Control = 0;
  ps.Privilege[0].Luid.LowPart = 20; // SE_DEBUG_PRIVILEGE
  ps.Privilege[0].Luid.HighPart = 0;
  ps.Privilege[0].Attributes = 0;
  BOOL bResult = FALSE;
  PrivilegeCheck(hToken, &ps, &bResult);
  CloseHandle(hToken);
  return bResult == TRUE;
}

// 1.10 OutputDebugString trick:
// Под отладчиком SetLastError после OutputDebugStringA сохраняет ошибку иначе
inline bool output_debug_string_trick() {
  SetLastError(0xC0FFEE);
  OutputDebugStringA(skCrypt(xorstr_("shadow_check")));
  return GetLastError() == 0; // Под отладчиком — 0, без него — 0xC0FFEE
}

// 1.11 UnhandledExceptionFilter — под отладчиком никогда не вызывается
static bool g_uef_called = false;
static LONG WINAPI uef_callback(EXCEPTION_POINTERS *) {
  g_uef_called = true;
  return EXCEPTION_EXECUTE_HANDLER;
}
inline bool unhandled_exception_filter_trick() {
  SetUnhandledExceptionFilter(uef_callback);
  g_uef_called = false;
  __try {
    RaiseException(0xC0000FFF, 0, 0, nullptr);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  return !g_uef_called; // Если не вызвался — отладчик перехватил
}

// 1.12 TLS Callback anti-debug (регистрируется в .tls секции)
// Вызывается ДО точки входа — большинство отладчиков ставят BP на main, не на
// TLS (объявляется в security.cpp, сюда только хелпер)
inline void hide_all_threads() {
  auto fn = shadow::nt::resolve<pfnNtSetInformationThread>(
      shadow::nt::H_NtSetInformationThread);
  if (!fn)
    return;
  fn(GetCurrentThread(), ShadowThreadHideFromDebugger, nullptr, 0);
}

// 1.13 Проверка через CONTEXT.EFlags — Trap Flag (TF)
// При пошаговой отладке (F7/F8) бит TF выставлен в EFLAGS
inline bool trap_flag_check() {
  bool tf_set = false;
  __try {
    tf_set = (__readeflags() & 0x100) != 0;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    tf_set = true;
  }
  return tf_set;
}

// 1.14 INT 2D (Kernel debug check)
// INT 2D используется Windows для kernel debugging. Поведение отличается под
// debugger.
inline bool int2d_check() {
  bool detected = false;
  __try {
    // MSVC doesn't support int2d inline, use DebugBreak (int3) as a fallback
    // anti-debug check
    __debugbreak();
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    detected = true;
  }
  return detected;
}

// 1.15 Проверка ProcessHandleCount — под отладчиком больше открытых handles
inline bool suspicious_handle_count() {
  auto fn = shadow::nt::resolve<pfnNtQueryInformationProcess>(
      shadow::nt::H_NtQueryInformationProcess);
  if (!fn)
    return false;
  ULONG count = 0;
  fn(GetCurrentProcess(), (PROCESSINFOCLASS)20 /*ProcessHandleCount*/, &count,
     sizeof(count), nullptr);
  // Нормальный процесс имеет ~30-80 handles. Отладчик добавляет несколько.
  // Эвристика: > 150 = подозрительно
  return count > 150;
}

} // namespace shadow::anti_debug

// ---------------------------------------------------------
//  БЛОК 2 — TIMING ATTACKS
// ---------------------------------------------------------
namespace shadow::timing {

// 2.1 RDTSC — инструкция читает Time Stamp Counter
// Под отладчиком или в VM — разница между двумя вызовами аномально велика
inline bool rdtsc_delta() {
  UINT64 t1 = __rdtsc();
  // Несколько пустых volatile операций чтобы компилятор не убрал
  volatile uint32_t sink = 0;
  for (int i = 0; i < 1000; i++)
    sink += i * 3;
  UINT64 t2 = __rdtsc();
  UINT64 delta = t2 - t1;
  // Нормально: < 500'000 тактов. Под отладчиком/VM: > 2'000'000
  return delta > 2'000'000ULL;
}

// 2.2 RDTSCP — учитывает Core ID, точнее чем RDTSC
inline bool rdtscp_delta() {
  unsigned int aux1, aux2;
  UINT64 t1 = __rdtscp(&aux1);
  volatile uint32_t x = 0;
  for (int i = 0; i < 500; i++)
    x ^= i;
  UINT64 t2 = __rdtscp(&aux2);
  return (t2 - t1) > 1'500'000ULL;
}

// 2.3 QueryPerformanceCounter timing
inline bool qpc_timing() {
  LARGE_INTEGER t1, t2, freq;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&t1);
  volatile uint32_t sink = 0;
  for (int i = 0; i < 10000; i++)
    sink += i;
  QueryPerformanceCounter(&t2);
  // Ожидаемое время: < 1 ms. Под отладчиком — значительно дольше
  double ms = (double)(t2.QuadPart - t1.QuadPart) * 1000.0 / freq.QuadPart;
  return ms > 20.0;
}

// 2.4 GetTickCount64 delta
inline bool gettickcount_delta() {
  UINT64 t1 = GetTickCount64();
  Sleep(1); // 1ms sleep
  UINT64 t2 = GetTickCount64();
  // Должно быть ровно ~1ms. Под некоторыми sandbox: 0 (time не идёт!)
  return (t2 - t1) == 0;
}

// 2.5 CPUID timing — в VM CPUID выполняется медленнее
inline bool cpuid_timing() {
  UINT64 t1 = __rdtsc();
  int info[4];
  for (int i = 0; i < 10; i++)
    __cpuid(info, 0);
  UINT64 t2 = __rdtsc();
  // В реальной системе: < 10'000. В VM: > 50'000
  return (t2 - t1) > 50'000ULL;
}

// 2.6 Сравнение двух методов измерения времени
// Спуфинг одного метода не поможет если второй не споофлен
inline bool dual_timer_mismatch() {
  UINT64 rdtsc_start = __rdtsc();
  ULONGLONG tick_start = GetTickCount64();

  Sleep(100);

  UINT64 rdtsc_end = __rdtsc();
  ULONGLONG tick_end = GetTickCount64();

  UINT64 rdtsc_ms = (rdtsc_end - rdtsc_start) / 3'000'000ULL; // ~3GHz CPU
  UINT64 tick_ms = tick_end - tick_start;

  // Расхождение > 50ms = кто-то манипулирует временем
  UINT64 diff = rdtsc_ms > tick_ms ? rdtsc_ms - tick_ms : tick_ms - rdtsc_ms;
  return diff > 50;
}

} // namespace shadow::timing

// ---------------------------------------------------------
//  БЛОК 3 — ANTI-HOOK (Inline Hooks, IAT Hooks, SSDT)
// ---------------------------------------------------------
namespace shadow::anti_hook {

struct FuncSignature {
  const char *module_name; // skCrypt'd в использовании
  const char *func_name;   // skCrypt'd в использовании
  uint8_t expected[8];     // первые 8 байт функции на чистой системе
};

// Ожидаемые сигнатуры syscall stubs в ntdll.dll (Windows 10/11 x64)
// Формат: 4C 8B D1 B8 XX 00 00 00 0F 05 C3
//         MOV R10, RCX; MOV EAX, syscall_num; SYSCALL; RET
// Если вместо этого видим E9 (JMP) или FF 25 (JMP [rip+...]) — хук!
static const FuncSignature g_syscall_sigs[] = {
    // { module, func, { expected bytes } }
    // NtReadVirtualMemory   — syscall ~0x3F
    {nullptr, nullptr, {0x4C, 0x8B, 0xD1, 0xB8, 0x3F, 0x00, 0x00, 0x00}},
    // NtWriteVirtualMemory  — syscall ~0x3A
    {nullptr, nullptr, {0x4C, 0x8B, 0xD1, 0xB8, 0x3A, 0x00, 0x00, 0x00}},
    // NtOpenProcess         — syscall ~0x26
    {nullptr, nullptr, {0x4C, 0x8B, 0xD1, 0xB8, 0x26, 0x00, 0x00, 0x00}},
    // NtProtectVirtualMemory— syscall ~0x50
    {nullptr, nullptr, {0x4C, 0x8B, 0xD1, 0xB8, 0x50, 0x00, 0x00, 0x00}},
};

// 3.1 Проверить первые байты функции на наличие JMP-хука
inline bool is_function_hooked(uint8_t *fn_ptr) {
  if (!fn_ptr)
    return false;
  // JMP rel32 (E9), JMP SHORT (EB), JMP [rip+X] (FF 25), CALL (E8)
  return fn_ptr[0] == 0xE9 || fn_ptr[0] == 0xEB || fn_ptr[0] == 0xE8 ||
         (fn_ptr[0] == 0xFF && fn_ptr[1] == 0x25) ||
         (fn_ptr[0] == 0xFF && fn_ptr[1] == 0x15);
}

// 3.2 Сравнить байты функции с ntdll.dll на диске (не в памяти!)
// Это обнаруживает хуки которые умеют обходить проверку in-memory bytes
inline bool function_matches_disk(HMODULE hMod, const char *func_name) {
  // 1. Найти путь к модулю
  char path[MAX_PATH];
  if (!GetModuleFileNameA(hMod, path, MAX_PATH))
    return true; // не смогли — пропускаем

  // 2. Открыть файл с диска
  HANDLE hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE)
    return true;

  // 3. Замапить файл в память (read-only, это не то же что LoadLibrary)
  HANDLE hMap =
      CreateFileMappingA(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
  CloseHandle(hFile);
  if (!hMap)
    return true;

  PVOID disk_base = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
  CloseHandle(hMap);
  if (!disk_base)
    return true;

  // 4. Найти функцию в disk image через Export Directory
  // (аналогично ResolveAPI, но в disk image)
  bool mismatch = false;

  // 5. Получить адрес функции в памяти через обычный GetProcAddress
  uint8_t *mem_fn = (uint8_t *)GetProcAddress(hMod, func_name);
  if (mem_fn) {
    // Найти RVA функции в disk export table
    auto dos = (IMAGE_DOS_HEADER *)disk_base;
    auto nt = (IMAGE_NT_HEADERS *)((uint8_t *)disk_base + dos->e_lfanew);
    DWORD rva = (DWORD)(mem_fn - (uint8_t *)hMod);
    uint8_t *disk_fn = (uint8_t *)disk_base + rva;

    // Сравнить первые 16 байт
    mismatch = (memcmp(mem_fn, disk_fn, 16) != 0);
  }

  UnmapViewOfFile(disk_base);
  return mismatch;
}

// 3.3 Проверить IAT чита на хуки (если кто-то подменил импорты)
inline bool check_iat_integrity() {
  HMODULE hSelf = GetModuleHandle(nullptr);
  auto dos = (IMAGE_DOS_HEADER *)hSelf;
  auto nt = (IMAGE_NT_HEADERS *)((uint8_t *)hSelf + dos->e_lfanew);
  auto imp_dir =
      &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

  if (!imp_dir->Size)
    return false;

  auto desc =
      (IMAGE_IMPORT_DESCRIPTOR *)((uint8_t *)hSelf + imp_dir->VirtualAddress);
  while (desc->Name) {
    char *mod_name = (char *)((uint8_t *)hSelf + desc->Name);
    HMODULE hMod = GetModuleHandleA(mod_name);
    if (!hMod) {
      desc++;
      continue;
    }

    auto thunk = (IMAGE_THUNK_DATA *)((uint8_t *)hSelf + desc->FirstThunk);
    auto orig =
        (IMAGE_THUNK_DATA *)((uint8_t *)hSelf + desc->OriginalFirstThunk);

    while (thunk->u1.Function) {
      uint8_t *fn = (uint8_t *)thunk->u1.Function;
      if (is_function_hooked(fn))
        return true;
      thunk++;
      if (orig)
        orig++;
    }
    desc++;
  }
  return false;
}

// 3.4 ETW (Event Tracing for Windows) Patch Detection
// Некоторые инструменты патчат EtwEventWrite в ntdll чтобы логировать наши
// вызовы
inline bool etw_patched() {
  HMODULE hNtdll = (HMODULE)shadow::nt::ntdll_base();
  if (!hNtdll)
    return false;
  uint8_t *fn = (uint8_t *)GetProcAddress(hNtdll, skCrypt(xorstr_("EtwEventWrite")));
  if (!fn)
    return false;
  // Чистый пролог EtwEventWrite начинается с: 48 83 EC (SUB RSP, ...)
  // Если видим JMP — кто-то патчнул ETW для трассировки
  return is_function_hooked(fn);
}

// 3.5 AMSI Bypass Detection
// Некоторые инструменты патчат AmsiScanBuffer чтобы обойти защиту
// Нас интересует если AMSI пропатчен — значит рядом антивирус или tool
inline bool amsi_patched() {
  HMODULE hAmsi = GetModuleHandleA(skCrypt(xorstr_("amsi.dll")));
  if (!hAmsi)
    return false; // AMSI не загружен — нормально для нашего процесса
  uint8_t *fn = (uint8_t *)GetProcAddress(hAmsi, skCrypt(xorstr_("AmsiScanBuffer")));
  if (!fn)
    return false;
  return is_function_hooked(fn);
}

// 3.6 Проверить NtdllUnhook — сравнить ntdll в памяти с ntdll на диске
inline bool ntdll_unhooked_check() {
  HMODULE hNtdll = (HMODULE)shadow::nt::ntdll_base();
  if (!hNtdll)
    return false;

  const char *critical_funcs[] = {
      skCrypt(xorstr_("NtReadVirtualMemory")),
      skCrypt(xorstr_("NtWriteVirtualMemory")),
      skCrypt(xorstr_("NtOpenProcess")),
      skCrypt(xorstr_("NtProtectVirtualMemory")),
      skCrypt(xorstr_("NtQueryInformationProcess")),
      skCrypt(xorstr_("NtSetInformationThread")),
      skCrypt(xorstr_("LdrLoadDll")),
  };

  for (auto &fname : critical_funcs) {
    if (function_matches_disk(hNtdll, fname))
      return true;
  }
  return false;
}

} // namespace shadow::anti_hook

// ---------------------------------------------------------
//  БЛОК 4 — ANTI-VM / ANTI-SANDBOX (расширенный)
// ---------------------------------------------------------
namespace shadow::anti_vm {

// 4.1 CPUID: Hypervisor Present bit (ECX[31] в leaf 1)
inline bool cpuid_hypervisor_bit() {
  int info[4];
  __cpuid(info, 1);
  return (info[2] >> 31) & 1;
}

// 4.2 CPUID: Hypervisor vendor string (leaf 0x40000000)
inline bool cpuid_hv_vendor() {
  int info[4];
  __cpuid(info, 0x40000000);
  char vendor[13] = {};
  memcpy(vendor, &info[1], 4);
  memcpy(vendor + 4, &info[2], 4);
  memcpy(vendor + 8, &info[3], 4);
  // Известные гипервайзоры
  return strstr(vendor, skCrypt(xorstr_("VMware"))) != nullptr ||
         strstr(vendor, skCrypt(xorstr_("VBox"))) != nullptr ||
         strstr(vendor, skCrypt(xorstr_("KVM"))) != nullptr ||
         strstr(vendor, skCrypt(xorstr_("Xen"))) != nullptr ||
         strstr(vendor, skCrypt(xorstr_("QEMU"))) != nullptr ||
         strstr(vendor, skCrypt(xorstr_("bhyve"))) != nullptr ||
         strstr(vendor, skCrypt(xorstr_("Parallels"))) != nullptr;
}

// 4.3 Проверка VM-специфичных устройств
inline bool vm_devices() {
  const char *paths[] = {
      skCrypt(xorstr_("\\\\.\\vmci")),        // VMware VMCI
      skCrypt(xorstr_("\\\\.\\VBoxGuest")),   // VirtualBox Guest
      skCrypt(xorstr_("\\\\.\\HGFS")),        // VMware Host-Guest FS
      skCrypt(xorstr_("\\\\.\\vmhgfs")),      // VMware HGFS (old)
      skCrypt(xorstr_("\\\\.\\VBoxTrayIPC")), // VirtualBox Tray IPC
  };
  for (auto &p : paths) {
    HANDLE h = CreateFileA(p, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
      CloseHandle(h);
      return true;
    }
  }
  return false;
}

// 4.4 Реестровые ключи VM-гостей
inline bool vm_registry() {
  const char *keys[] = {
      skCrypt(xorstr_("SOFTWARE\\VMware, Inc.\\VMware Tools")),
      skCrypt(xorstr_("SOFTWARE\\Oracle\\VirtualBox Guest Additions")),
      skCrypt(xorstr_("SYSTEM\\CurrentControlSet\\Services\\vmmouse")),
      skCrypt(xorstr_("SYSTEM\\CurrentControlSet\\Services\\vmhgfs")),
      skCrypt(xorstr_("SYSTEM\\CurrentControlSet\\Services\\VBoxSF")),
      skCrypt(xorstr_("SYSTEM\\CurrentControlSet\\Services\\vboxguest")),
      skCrypt(xorstr_("HARDWARE\\ACPI\\DSDT\\VBOX__")),
      skCrypt(xorstr_("HARDWARE\\ACPI\\FADT\\VBOX__")),
  };
  for (auto &key : keys) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &hKey) ==
        ERROR_SUCCESS) {
      RegCloseKey(hKey);
      return true;
    }
  }
  return false;
}

// 4.5 Проверка VM-специфичных процессов
inline bool vm_processes() {
  const char *procs[] = {
      skCrypt(xorstr_("vmtoolsd.exe")),   skCrypt(xorstr_("vmwaretray.exe")),
      skCrypt(xorstr_("vmwareuser.exe")), skCrypt(xorstr_("VBoxService.exe")),
      skCrypt(xorstr_("VBoxTray.exe")),   skCrypt(xorstr_("vmsrvc.exe")),
      skCrypt(xorstr_("vmusrvc.exe")),    skCrypt(xorstr_("xenservice.exe")),
      skCrypt(xorstr_("qemu-ga.exe")),    skCrypt(xorstr_("prl_tools.exe")),
  };
  HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnap == INVALID_HANDLE_VALUE)
    return false;
  PROCESSENTRY32W pe = {sizeof(pe)};
  bool found = false;
  if (Process32FirstW(hSnap, &pe)) {
    do {
      char exeName[MAX_PATH];
      size_t converted;
      wcstombs_s(&converted, exeName, sizeof(exeName), pe.szExeFile, _TRUNCATE);
      for (auto &name : procs) {
        if (_stricmp(exeName, name) == 0) {
          found = true;
          break;
        }
      }
    } while (!found && Process32NextW(hSnap, &pe));
  }
  CloseHandle(hSnap);
  return found;
}

// 4.6 MAC адрес — VM-адаптеры имеют известные OUI-префиксы
inline bool vm_mac_address() {
  // Известные VM MAC OUI (первые 3 байта)
  const uint8_t vm_ouis[][3] = {
      {0x00, 0x05, 0x69}, // VMware
      {0x00, 0x0C, 0x29}, // VMware
      {0x00, 0x50, 0x56}, // VMware
      {0x00, 0x1C, 0x14}, // VMware
      {0x08, 0x00, 0x27}, // VirtualBox
      {0x00, 0x21, 0xF6}, // VirtualBox (старый)
      {0x52, 0x54, 0x00}, // QEMU/KVM
      {0x00, 0x16, 0x3E}, // Xen
      {0x00, 0x15, 0x5D}, // Hyper-V
  };
  IP_ADAPTER_INFO info_buf[16];
  DWORD buf_size = sizeof(info_buf);
  if (GetAdaptersInfo(info_buf, &buf_size) != ERROR_SUCCESS)
    return false;
  for (auto *ai = info_buf; ai; ai = ai->Next) {
    if (ai->AddressLength < 3)
      continue;
    for (auto &oui : vm_ouis) {
      if (memcmp(ai->Address, oui, 3) == 0)
        return true;
    }
  }
  return false;
}

// 4.7 Подозрительно малое количество файлов в System32
// Sandbox системы часто имеют минимальный набор файлов
inline bool sparse_system32() {
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(skCrypt(xorstr_("C:\\Windows\\System32\\*.dll")), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return true; // не нашли — подозрительно
  int count = 0;
  while (FindNextFileA(h, &fd) && count < 200)
    count++;
  FindClose(h);
  return count < 100; // слишком мало DLL в System32
}

// 4.8 Малый uptime — sandbox/VM запускаются и гасятся быстро
inline bool low_uptime() {
  return GetTickCount64() < (5ULL * 60 * 1000); // < 5 минут
}

// 4.9 Малый объём ОЗУ
inline bool low_ram() {
  MEMORYSTATUSEX ms = {sizeof(ms)};
  GlobalMemoryStatusEx(&ms);
  return ms.ullTotalPhys < (4ULL * 1024 * 1024 * 1024); // < 4 GB
}

// 4.10 Мало ядер CPU
inline bool low_cpu_cores() {
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwNumberOfProcessors < 2;
}

// 4.11 Проверка имени компьютера — sandbox часто имеют шаблонные имена
inline bool suspicious_hostname() {
  char name[MAX_COMPUTERNAME_LENGTH + 1] = {};
  DWORD sz = sizeof(name);
  GetComputerNameA(name, &sz);
  const char *bad[] = {
      skCrypt(xorstr_("SANDBOX")), skCrypt(xorstr_("VIRUS")),    skCrypt(xorstr_("MALWARE")),
      skCrypt(xorstr_("CUCKOO")),  skCrypt(xorstr_("ANALYSIS")), skCrypt(xorstr_("DESKTOP-")),
      skCrypt(xorstr_("WIN7")),    skCrypt(xorstr_("JOHN-PC")),  skCrypt(xorstr_("TEST")),
  };
  for (auto &b : bad) {
    if (strstr(name, b))
      return true;
  }
  return false;
}

// 4.12 Проверка имени пользователя
inline bool suspicious_username() {
  char name[256] = {};
  DWORD sz = sizeof(name);
  GetUserNameA(name, &sz);
  const char *bad[] = {
      skCrypt(xorstr_("sandbox")), skCrypt(xorstr_("virus")), skCrypt(xorstr_("malware")),
      skCrypt(xorstr_("admin")),   skCrypt(xorstr_("user")),  skCrypt(xorstr_("analyst")),
      skCrypt(xorstr_("cuckoo")),  skCrypt(xorstr_("test")),
  };
  for (auto &b : bad) {
    if (_stricmp(name, b) == 0)
      return true;
  }
  return false;
}

// 4.13 Проверка размера жёсткого диска (sandbox часто < 60 GB)
inline bool small_disk() {
  ULARGE_INTEGER free_bytes, total_bytes, total_free;
  if (!GetDiskFreeSpaceExA(xorstr_("C:\\"), &free_bytes, &total_bytes, &total_free))
    return false;
  return total_bytes.QuadPart < (60ULL * 1024 * 1024 * 1024); // < 60 GB
}

// 4.14 MSR (Model Specific Register) — попытка чтения только в Ring0
// В User-mode вызовет исключение. В VM или под hypervisor — иное поведение.
inline bool rdmsr_check() {
  bool detected = false;
  __try {
    UINT64 val = __readmsr(0x1A0); // IA32_MISC_ENABLE
    // MSR 0x1A0 бит 18 = Enhanced Intel SpeedStep. VM часто не поддерживает.
    (void)val;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    // В нормальной системе тоже вызовет exception из user-mode
    // Но timing этого exception отличается в VM
    detected = false; // не используем как основной детект
  }
  return detected;
}

} // namespace shadow::anti_vm

// ---------------------------------------------------------
//  БЛОК 5 — PROCESS / WINDOW ENUMERATION
// ---------------------------------------------------------
namespace shadow::process_scan {

// 5.1 Список процессов реверс-инструментов (максимальный)
static const char *g_bad_processes[] = {
    // Debuggers
    skCrypt(xorstr_("x64dbg.exe")),
    skCrypt(xorstr_("x32dbg.exe")),
    skCrypt(xorstr_("ollydbg.exe")),
    skCrypt(xorstr_("windbg.exe")),
    skCrypt(xorstr_("ntsd.exe")),
    skCrypt(xorstr_("cdb.exe")),
    skCrypt(xorstr_("kd.exe")),
    skCrypt(xorstr_("gdb.exe")),
    // Disassemblers / RE tools
    skCrypt(xorstr_("ida64.exe")),
    skCrypt(xorstr_("ida.exe")),
    skCrypt(xorstr_("idaq64.exe")),
    skCrypt(xorstr_("idaq.exe")),
    skCrypt(xorstr_("idaw.exe")),
    skCrypt(xorstr_("idaw64.exe")),
    skCrypt(xorstr_("ghidra.exe")),
    skCrypt(xorstr_("ghidraRun.exe")),
    skCrypt(xorstr_("radare2.exe")),
    skCrypt(xorstr_("cutter.exe")),
    skCrypt(xorstr_("binary_ninja.exe")),
    skCrypt(xorstr_("binja.exe")),
    skCrypt(xorstr_("dnspy.exe")),
    skCrypt(xorstr_("dotpeek.exe")),
    skCrypt(xorstr_("de4dot.exe")),
    skCrypt(xorstr_("ilspy.exe")),
    // Memory tools
    skCrypt(xorstr_("cheatengine-x86_64.exe")),
    skCrypt(xorstr_("cheatengine-i386.exe")),
    skCrypt(xorstr_("processhacker.exe")),
    skCrypt(xorstr_("procexp64.exe")),
    skCrypt(xorstr_("procexp.exe")),
    skCrypt(xorstr_("procmon64.exe")),
    skCrypt(xorstr_("procmon.exe")),
    skCrypt(xorstr_("reclass64.exe")),
    skCrypt(xorstr_("reclass.exe")),
    skCrypt(xorstr_("reclassnext.exe")),
    // Network analysis
    skCrypt(xorstr_("wireshark.exe")),
    skCrypt(xorstr_("rawcap.exe")),
    skCrypt(xorstr_("fiddler.exe")),
    skCrypt(xorstr_("charles.exe")),
    skCrypt(xorstr_("burpsuite.exe")),
    skCrypt(xorstr_("mitmproxy.exe")),
    // Sandbox / AV tools
    skCrypt(xorstr_("cuckoo.exe")),
    skCrypt(xorstr_("sandboxie.exe")),
    skCrypt(xorstr_("sbiectrl.exe")),
    skCrypt(xorstr_("protection_id.exe")),
    skCrypt(xorstr_("peid.exe")),
    skCrypt(xorstr_("exeinfope.exe")),
    skCrypt(xorstr_("pestudio.exe")),
    skCrypt(xorstr_("lordpe.exe")),
    skCrypt(xorstr_("StudPE.exe")),
    skCrypt(xorstr_("CFF_Explorer.exe")),
    skCrypt(xorstr_("imhex.exe")),
    skCrypt(xorstr_("HxD.exe")),
    skCrypt(xorstr_("010editor.exe")),
    // Misc
    skCrypt(xorstr_("apimonitor-x64.exe")),
    skCrypt(xorstr_("apimonitor-x86.exe")),
    skCrypt(xorstr_("scyllaride.exe")),
    skCrypt(xorstr_("ScyllaHide.exe")),
    skCrypt(xorstr_("hollows_hunter.exe")),
    skCrypt(xorstr_("pe-sieve64.exe")),
    skCrypt(xorstr_("pe-sieve32.exe")),
};

inline bool find_bad_process() {
  HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnap == INVALID_HANDLE_VALUE)
    return false;
  PROCESSENTRY32W pe = {sizeof(pe)};
  bool found = false;
  if (Process32FirstW(hSnap, &pe)) {
    do {
      char exeName[MAX_PATH];
      size_t converted;
      wcstombs_s(&converted, exeName, sizeof(exeName), pe.szExeFile, _TRUNCATE);
      for (auto &name : g_bad_processes) {
        if (_stricmp(exeName, name) == 0) {
          found = true;
          break;
        }
      }
    } while (!found && Process32NextW(hSnap, &pe));
  }
  CloseHandle(hSnap);
  return found;
}

// 5.2 Проверка заголовков окон (window title)
static const char *g_bad_window_titles[] = {
    skCrypt(xorstr_("x64dbg")),
    skCrypt(xorstr_("x32dbg")),
    skCrypt(xorstr_("OllyDbg")),
    skCrypt(xorstr_("WinDbg")),
    skCrypt(xorstr_("IDA")),
    skCrypt(xorstr_("Ghidra")),
    skCrypt(xorstr_("Cheat Engine")),
    skCrypt(xorstr_("Process Hacker")),
    skCrypt(xorstr_("Process Monitor")),
    skCrypt(xorstr_("Process Explorer")),
    skCrypt(xorstr_("Wireshark")),
    skCrypt(xorstr_("Fiddler")),
    skCrypt(xorstr_("ReClass")),
    skCrypt(xorstr_("ScyllaHide")),
    skCrypt(xorstr_("Binary Ninja")),
    skCrypt(xorstr_("Cutter")),
    skCrypt(xorstr_("Immunity Debugger")),
    skCrypt(xorstr_("PE-sieve")),
};

inline bool find_bad_window() {
  for (auto &title : g_bad_window_titles) {
    if (FindWindowA(nullptr, title))
      return true;
  }
  return false;
}

// 5.3 Проверка window class names (труднее скрыть чем title)
static const char *g_bad_window_classes[] = {
    skCrypt(xorstr_("OLLYDBG")),          skCrypt(xorstr_("WinDbgFrameClass")),
    skCrypt(xorstr_("x64dbgMainWindow")), skCrypt(xorstr_("Qt5QWindowIcon")),
    skCrypt(xorstr_("GDKWINDOW")),        skCrypt(xorstr_("Afx:00400000")),
};

inline bool find_bad_window_class() {
  for (auto &cls : g_bad_window_classes) {
    if (FindWindowA(cls, nullptr))
      return true;
  }
  return false;
}

// 5.4 Проверить загруженные модули текущего процесса
// Некоторые инструменты инжектируют DLL в наш процесс
static const char *g_bad_modules[] = {
    skCrypt(xorstr_("sbiedll.dll")),    // Sandboxie
    skCrypt(xorstr_("dbghelp.dll")),    // Debug helper (может быть инжектирован)
    skCrypt(xorstr_("pstorec.dll")),    // Старый Windows debug DLL
    skCrypt(xorstr_("vmcheck.dll")),    // VM check
    skCrypt(xorstr_("wpespy.dll")),     // WPE Pro (packet editor)
    skCrypt(xorstr_("api_log.dll")),    // API Monitor
    skCrypt(xorstr_("dir_watch.dll")),  // Directory watcher
    skCrypt(xorstr_("logapi.dll")),     // Logger
    skCrypt(xorstr_("scyllahide.dll")), // ScyllaHide plugin
};

inline bool find_bad_module() {
  HANDLE hSnap =
      CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
  if (hSnap == INVALID_HANDLE_VALUE)
    return false;
  MODULEENTRY32W me = {sizeof(me)};
  bool found = false;
  if (Module32FirstW(hSnap, &me)) {
    do {
      char modName[MAX_PATH];
      size_t converted;
      wcstombs_s(&converted, modName, sizeof(modName), me.szModule, _TRUNCATE);
      for (auto &name : g_bad_modules) {
        if (_stricmp(modName, name) == 0) {
          found = true;
          break;
        }
      }
    } while (!found && Module32NextW(hSnap, &me));
  }
  CloseHandle(hSnap);
  return found;
}

} // namespace shadow::process_scan

// ---------------------------------------------------------
//  БЛОК 6 — PARENT PROCESS CHECK
// ---------------------------------------------------------
namespace shadow::parent_check {

// 6.1 Проверить родительский процесс
// Лоадер должен запускать чит — родитель должен быть нашим лоадером
// Если родитель = x64dbg, IDA, explorer.exe и т.д. — подозрительно
inline DWORD get_parent_pid() {
  auto fn = shadow::nt::resolve<pfnNtQueryInformationProcess>(
      shadow::nt::H_NtQueryInformationProcess);
  if (!fn)
    return 0;
  PROCESS_BASIC_INFORMATION pbi = {};
  fn(GetCurrentProcess(), ProcessBasicInformation, &pbi, sizeof(pbi), nullptr);
  return (DWORD)(UINT_PTR)pbi.Reserved3; // InheritedFromUniqueProcessId
}

inline std::string get_process_name(DWORD pid) {
  HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnap == INVALID_HANDLE_VALUE)
    return {};
  PROCESSENTRY32W pe = {sizeof(pe)};
  std::string result;
  if (Process32FirstW(hSnap, &pe)) {
    do {
      if (pe.th32ProcessID == pid) {
        char exeName[MAX_PATH];
        size_t converted;
        wcstombs_s(&converted, exeName, sizeof(exeName), pe.szExeFile,
                   _TRUNCATE);
        result = exeName;
        break;
      }
    } while (Process32NextW(hSnap, &pe));
  }
  CloseHandle(hSnap);
  return result;
}

inline bool bad_parent_process() {
  DWORD ppid = get_parent_pid();
  std::string parent_name = get_process_name(ppid);
  if (parent_name.empty())
    return false;

  // Разрешённые родительские процессы
  const char *allowed[] = {
      skCrypt(xorstr_("shadow_loader.exe")), // наш лоадер
      skCrypt(xorstr_("explorer.exe")),      // запуск через проводник (допустимо)
  };
  for (auto &a : allowed) {
    if (_stricmp(parent_name.c_str(), a) == 0)
      return false;
  }

  // Известные плохие родители
  const char *bad[] = {
      skCrypt(xorstr_("x64dbg.exe")),        skCrypt(xorstr_("ida64.exe")),
      skCrypt(xorstr_("ollydbg.exe")),       skCrypt(xorstr_("windbg.exe")),
      skCrypt(xorstr_("processhacker.exe")), skCrypt(xorstr_("cheatengine-x86_64.exe")),
  };
  for (auto &b : bad) {
    if (_stricmp(parent_name.c_str(), b) == 0)
      return true;
  }

  return false;
}

} // namespace shadow::parent_check

// ---------------------------------------------------------
//  БЛОК 7 — MEMORY ENVIRONMENT CHECKS
// ---------------------------------------------------------
namespace shadow::memory_checks {

// 7.1 Проверить что .text секция не имеет PAGE_EXECUTE_READWRITE
// Инжектированный код или SMC часто требует RWX — это подозрительно
// Но осторожно — наш собственный SMC тоже временно ставит RWX
inline bool suspicious_rwx_regions() {
  MEMORY_BASIC_INFORMATION mbi = {};
  uintptr_t addr = 0;
  int rwx_count = 0;
  while (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi))) {
    if (mbi.Type == MEM_PRIVATE && mbi.Protect == PAGE_EXECUTE_READWRITE &&
        mbi.RegionSize > 4096) {
      rwx_count++;
    }
    addr += mbi.RegionSize;
    if (addr == 0)
      break;
  }
  return rwx_count > 5; // много RWX регионов = подозрительно
}

// 7.2 Проверить stack cookies / canaries
// Если стек повреждён — сработает при return
// Включается компилятором через /GS, здесь — дополнительная ручная проверка
inline bool check_stack_canary(uint64_t expected_canary) {
  // Использовать __security_cookie из CRT
  // extern uintptr_t __security_cookie;
  // return __security_cookie != expected_canary;
  return false; // placeholder
}

// 7.3 Проверить PAGE_GUARD на важных страницах нашей памяти
// Можно поставить guard на критичные структуры данных — любое чтение/запись
// вызовет STATUS_GUARD_PAGE_VIOLATION которое мы перехватываем
inline void set_guard_page(void *ptr, size_t size) {
  DWORD old;
  VirtualProtect(ptr, size, PAGE_READWRITE | PAGE_GUARD, &old);
}

// 7.4 Проверка на наличие известных отладочных heap
// При _HEAP_DEBUG в CRT heap patterns иные
inline bool debug_heap_pattern() {
  void *mem = malloc(16);
  if (!mem)
    return false;
  // Дебаговый heap заполняет блок паттерном 0xCD (allocated)
  bool result = ((uint8_t *)mem)[0] == 0xCD;
  free(mem);
  return result;
}

} // namespace shadow::memory_checks

// ---------------------------------------------------------
//  БЛОК 8 — INTEGRITY CHECKS (anti-patch)
// ---------------------------------------------------------
namespace shadow::integrity {

static uintptr_t g_text_base = 0;
static size_t g_text_size = 0;
static uint32_t g_text_crc = 0;

// CRC32 (простая реализация без таблиц — меньше артефактов в .data)
static uint32_t crc32(const uint8_t *data, size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
  }
  return ~crc;
}

// Инициализация — вызвать до ErasePEHeaders!
inline void init() {
  HMODULE hMod = GetModuleHandle(nullptr);
  auto dos = (IMAGE_DOS_HEADER *)hMod;
  auto nt = (IMAGE_NT_HEADERS *)((uint8_t *)hMod + dos->e_lfanew);
  auto sec = IMAGE_FIRST_SECTION(nt);

  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
    if (strncmp((char *)sec->Name, skCrypt(xorstr_(".text")), 8) == 0) {
      g_text_base = (uintptr_t)hMod + sec->VirtualAddress;
      g_text_size = sec->Misc.VirtualSize;
      break;
    }
  }

  if (g_text_base && g_text_size) {
    g_text_crc = crc32((uint8_t *)g_text_base, g_text_size);
  }
}

inline bool check() {
  // OBF: оборачиваем return в MBA — крякер обычно ищет инструкцию `cmp ecx,
  // eax; jne`
  //   сразу после `crc32(*,g_text_size)` и патчит её в `xor eax,eax`. Hex-rays
  //   покажет не `cur != g_text_crc`, а `(cur^mask)+adj != g_text_crc` — нужно
  //   искать и патчить несколько мест сразу.
  uint32_t cur;
  if (!g_text_base || !g_text_size) {
    // OBF: тупой dead-path с junk вместо прямого return false
    OBF_JUNK_BLOCK(volatile uint32_t _obf_skip =
                       (uint32_t)__rdtsc() ^ 0xBAADF00D;
                   (void)_obf_skip;);
    return false;
  } else {
    cur = crc32((uint8_t *)g_text_base, g_text_size);
  }
  // MBA-obfuscated "cur != g_text_crc"
  //   эквивалент: ((cur - crc) | (crc - cur)) >> 31 == 0 iff cur == crc.
  //   знаком не совпадает > не ноль > return true.
  volatile uint32_t v_cur = cur;
  volatile uint32_t v_crc = g_text_crc;
  uint32_t diff =
      (uint32_t)((int)v_cur - (int)v_crc) | (uint32_t)((int)v_crc - (int)v_cur);
  // sign == 1 iff diff != 0 iff cur != crc.
  uint32_t sign = (diff >> 31) & 1;
  // Дополним MBA-equals: sign == 1 > patched (return true).
  return shadow::obf::mba_equals((int)sign, 1);
}

// 8.2 Checksum критичных функций (секция внутри секции)
static uint32_t g_security_fn_crc = 0;

inline void init_fn_crc(void *fn_start, size_t fn_size) {
  g_security_fn_crc = crc32((uint8_t *)fn_start, fn_size);
}

inline bool check_fn_crc(void *fn_start, size_t fn_size) {
  return crc32((uint8_t *)fn_start, fn_size) != g_security_fn_crc;
}

} // namespace shadow::integrity

// ---------------------------------------------------------
//  БЛОК 9 — NETWORK / ENVIRONMENT CHECKS
// ---------------------------------------------------------
namespace shadow::env_checks {

// 9.1 Проверить что запущены нужные системные процессы
// На реальной машине всегда есть: svchost, lsass, winlogon, dwm, etc.
inline bool missing_system_processes() {
  const char *required[] = {
      skCrypt(xorstr_("svchost.exe")),
      skCrypt(xorstr_("lsass.exe")),
      skCrypt(xorstr_("winlogon.exe")),
      skCrypt(xorstr_("csrss.exe")),
  };
  HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnap == INVALID_HANDLE_VALUE)
    return true;

  for (auto &req : required) {
    bool found = false;
    PROCESSENTRY32W pe = {sizeof(pe)};
    if (Process32FirstW(hSnap, &pe)) {
      do {
        char exeName[MAX_PATH];
        size_t converted;
        wcstombs_s(&converted, exeName, sizeof(exeName), pe.szExeFile,
                   _TRUNCATE);
        if (_stricmp(exeName, req) == 0) {
          found = true;
          break;
        }
      } while (Process32NextW(hSnap, &pe));
    }
    if (!found) {
      CloseHandle(hSnap);
      return true;
    }
  }
  CloseHandle(hSnap);
  return false;
}

// 9.2 Проверить количество запущенных процессов
// Sandbox: < 20 процессов. Реальная система: > 40
inline bool too_few_processes() {
  HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (hSnap == INVALID_HANDLE_VALUE)
    return false;
  int count = 0;
  PROCESSENTRY32 pe = {sizeof(pe)};
  if (Process32First(hSnap, &pe))
    do {
      count++;
    } while (Process32Next(hSnap, &pe));
  CloseHandle(hSnap);
  return count < 30;
}

// 9.3 Проверить наличие монитора/экрана
// Sandbox часто запускается без дисплея
inline bool no_display() { return GetSystemMetrics(SM_CMONITORS) == 0; }

// 9.4 Проверить разрешение экрана
// Sandbox часто имеет минимальное разрешение 800x600
inline bool suspicious_resolution() {
  int w = GetSystemMetrics(SM_CXSCREEN);
  int h = GetSystemMetrics(SM_CYSCREEN);
  return (w < 1024 || h < 768);
}

// 9.5 Проверить последнее время ввода (mouse/keyboard)
// В sandbox никто не работает — LASTINPUTINFO.dwTime очень старое
inline bool no_user_input() {
  LASTINPUTINFO lii = {sizeof(lii)};
  GetLastInputInfo(&lii);
  DWORD idle_ms = GetTickCount() - lii.dwTime;
  // Более 10 минут без ввода = возможно sandbox
  return idle_ms > (10 * 60 * 1000);
}

// 9.6 Проверить количество недавних документов
inline bool no_recent_files() {
  WIN32_FIND_DATAA fd;
  char recent_path[MAX_PATH];
  SHGetFolderPathA(nullptr, CSIDL_RECENT, nullptr, 0, recent_path);
  strcat_s(recent_path, xorstr_("\\*"));
  HANDLE h = FindFirstFileA(recent_path, &fd);
  if (h == INVALID_HANDLE_VALUE)
    return true;
  int count = 0;
  while (FindNextFileA(h, &fd) && count < 5)
    count++;
  FindClose(h);
  return count < 3;
}

} // namespace shadow::env_checks

// ---------------------------------------------------------
//  БЛОК 10 — MASTER CHECK & REACTION
// ---------------------------------------------------------
namespace shadow::security {

enum class ThreatLevel : uint8_t {
  NONE = 0,
  LOW = 1,      // одиночный подозрительный признак
  MEDIUM = 2,   // несколько признаков
  HIGH = 3,     // явный debugger/hook
  CRITICAL = 4, // аппаратный breakpoint / integrity fail
};

struct CheckResult {
  ThreatLevel level;
  const char *description;
  uint8_t ban_reason; // BanReason enum value
};

// Запустить все проверки и вернуть максимальный уровень угрозы
inline CheckResult RunAllChecks() {
  CheckResult worst = {ThreatLevel::NONE, nullptr, 0};
  auto update = [&](ThreatLevel lvl, const char *desc, uint8_t reason) {
    if (lvl > worst.level)
      worst = {lvl, desc, reason};
  };

  // -- CRITICAL: аппаратные/ядерные --
  /*
  if (anti_debug::hardware_breakpoints())
    update(ThreatLevel::CRITICAL, skCrypt("HW_BREAKPOINT"), 0x02);
  if (integrity::check())
    update(ThreatLevel::CRITICAL, skCrypt("TEXT_INTEGRITY_FAIL"), 0x07);
  if (anti_hook::ntdll_unhooked_check())
    update(ThreatLevel::HIGH, skCrypt("NTDLL_HOOK_DETECTED"), 0x08);
  if (anti_hook::check_iat_integrity())
    update(ThreatLevel::HIGH, skCrypt("IAT_HOOK_DETECTED"), 0x08);

  // -- HIGH: прямые debugger детекты --
  if (anti_debug::peb_being_debugged())
    update(ThreatLevel::HIGH, skCrypt("PEB_BEING_DEBUGGED"), 0x01);
  if (anti_debug::nt_debug_port())
    update(ThreatLevel::HIGH, skCrypt("NT_DEBUG_PORT"), 0x01);
  if (anti_debug::nt_debug_object_handle())
    update(ThreatLevel::HIGH, skCrypt("NT_DEBUG_OBJECT"), 0x01);
  if (anti_debug::nt_debug_flags())
    update(ThreatLevel::HIGH, skCrypt("NT_DEBUG_FLAGS"), 0x01);
  if (anti_debug::peb_nt_global_flag())
    update(ThreatLevel::HIGH, skCrypt("PEB_GLOBAL_FLAG"), 0x01);
  if (anti_debug::heap_flags())
    update(ThreatLevel::HIGH, skCrypt("HEAP_FLAGS"), 0x01);
  if (anti_debug::closeinvalid_handle_trick())
    update(ThreatLevel::HIGH, skCrypt("CLOSE_HANDLE_TRICK"), 0x01);
  if (anti_debug::se_debug_privilege())
    update(ThreatLevel::HIGH, skCrypt("SE_DEBUG_PRIV"), 0x01);

  // -- HIGH: Timing --
  if (timing::rdtsc_delta())
    update(ThreatLevel::HIGH, skCrypt("RDTSC_TIMING"), 0x03);
  if (timing::qpc_timing())
    update(ThreatLevel::MEDIUM, skCrypt("QPC_TIMING"), 0x03);
  if (timing::dual_timer_mismatch())
    update(ThreatLevel::HIGH, skCrypt("DUAL_TIMER_MISMATCH"), 0x03);
  */

  // -- MEDIUM: плохие процессы / окна --
  if (process_scan::find_bad_process())
    update(ThreatLevel::HIGH, skCrypt(xorstr_("BAD_PROCESS")), 0x05);
  if (process_scan::find_bad_window())
    update(ThreatLevel::HIGH, skCrypt(xorstr_("BAD_WINDOW")), 0x05);
  if (process_scan::find_bad_window_class())
    update(ThreatLevel::MEDIUM, skCrypt(xorstr_("BAD_WINDOW_CLASS")), 0x05);
  if (process_scan::find_bad_module())
    update(ThreatLevel::HIGH, skCrypt(xorstr_("BAD_MODULE_INJECTED")), 0x05);

  // -- MEDIUM: parent process --
  /*
  if (parent_check::bad_parent_process())
    update(ThreatLevel::MEDIUM, skCrypt("BAD_PARENT"), 0x01);
  */

  // -- MEDIUM: VM --
  /*
  if (anti_vm::cpuid_hypervisor_bit() && anti_vm::cpuid_hv_vendor())
    update(ThreatLevel::HIGH, skCrypt("HYPERVISOR_DETECTED"), 0x04);
  if (anti_vm::vm_devices())
    update(ThreatLevel::HIGH, skCrypt("VM_DEVICE"), 0x04);
  if (anti_vm::vm_registry())
    update(ThreatLevel::HIGH, skCrypt("VM_REGISTRY"), 0x04);
  if (anti_vm::vm_processes())
    update(ThreatLevel::HIGH, skCrypt("VM_PROCESS"), 0x04);
  if (anti_vm::vm_mac_address())
    update(ThreatLevel::MEDIUM, skCrypt("VM_MAC"), 0x04);

  // -- LOW: environment эвристики --
  int env_score = 0;
  if (anti_vm::low_ram())
    env_score++;
  if (anti_vm::low_cpu_cores())
    env_score++;
  if (anti_vm::sparse_system32())
    env_score++;
  if (anti_vm::low_uptime())
    env_score++;
  if (anti_vm::small_disk())
    env_score++;
  if (env_checks::no_display())
    env_score++;
  if (env_checks::too_few_processes())
    env_score++;
  if (env_score >= 3)
    update(ThreatLevel::MEDIUM, skCrypt("ENV_HEURISTICS"), 0x04);
  */

  // -- Hooks --
  /*
  if (anti_hook::etw_patched())
    update(ThreatLevel::MEDIUM, skCrypt("ETW_PATCHED"), 0x08);
  */

  // -- Memory --
  /*
  if (memory_checks::suspicious_rwx_regions())
    update(ThreatLevel::MEDIUM, skCrypt("SUSPICIOUS_RWX"), 0x08);
  */

  return worst;
}

} // namespace shadow::security


