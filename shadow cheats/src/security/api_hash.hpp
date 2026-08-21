#pragma once
#include "security/xorstr.hpp"

/*
 * shadow cheats — API Hash / Dynamic IAT
 *
 * Все вызовы WinAPI идут через этот модуль — нет прямых символов в IAT.
 * PEB-walker + FNV1a хеш имён. Работает без GetProcAddress.
 *
 * Использование:
 *   auto NtQuery = API::Resolve<pfnNtQueryInformationProcess>(API::H_NtQueryInformationProcess);
 *   или просто: API::NtQIP(...) для заранее объявленных функций
 */

#include <windows.h>
#include <winternl.h>
#include <cstdint>
#include "skCrypter.h"

#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)
#endif

namespace API {

    // ─── FNV-1a (constexpr для compile-time хешей) ───
    static constexpr uint32_t fnv1a(const char* s, uint32_t h = 2166136261u) noexcept {
        return *s ? fnv1a(s + 1, (h ^ uint8_t(*s)) * 16777619u) : h;
    }
    static constexpr uint32_t fnv1aw(const wchar_t* s, uint32_t h = 2166136261u) noexcept {
        return *s ? fnv1aw(s + 1, (h ^ (uint32_t(*s) & 0xFF)) * 16777619u) : h;
    }

    // ─── Precomputed хеши (все строки не попадают в бинарь) ───
    static constexpr uint32_t H_ntdll    = fnv1aw(L"ntdll.dll");
    static constexpr uint32_t H_kernel32 = fnv1aw(L"kernel32.dll");
    static constexpr uint32_t H_kernelbase = fnv1aw(L"kernelbase.dll");

    static constexpr uint32_t H_NtQueryInformationProcess = fnv1a("NtQueryInformationProcess");
    static constexpr uint32_t H_NtSetInformationThread    = fnv1a("NtSetInformationThread");
    static constexpr uint32_t H_NtQuerySystemInformation  = fnv1a("NtQuerySystemInformation");
    static constexpr uint32_t H_NtQueryObject             = fnv1a("NtQueryObject");
    static constexpr uint32_t H_NtClose                   = fnv1a("NtClose");
    static constexpr uint32_t H_NtAllocateVirtualMemory   = fnv1a("NtAllocateVirtualMemory");
    static constexpr uint32_t H_NtFreeVirtualMemory       = fnv1a("NtFreeVirtualMemory");
    static constexpr uint32_t H_NtProtectVirtualMemory    = fnv1a("NtProtectVirtualMemory");
    static constexpr uint32_t H_NtReadVirtualMemory       = fnv1a("NtReadVirtualMemory");
    static constexpr uint32_t H_NtWriteVirtualMemory      = fnv1a("NtWriteVirtualMemory");
    static constexpr uint32_t H_NtOpenProcess             = fnv1a("NtOpenProcess");
    static constexpr uint32_t H_NtCreateThreadEx          = fnv1a("NtCreateThreadEx");
    static constexpr uint32_t H_LdrLoadDll                = fnv1a("LdrLoadDll");
    static constexpr uint32_t H_RtlGetVersion             = fnv1a("RtlGetVersion");
    static constexpr uint32_t H_EtwEventWrite             = fnv1a("EtwEventWrite");
    static constexpr uint32_t H_NtYieldExecution          = fnv1a("NtYieldExecution");

    // Kernel32
    static constexpr uint32_t H_VirtualAlloc              = fnv1a("VirtualAlloc");
    static constexpr uint32_t H_VirtualFree               = fnv1a("VirtualFree");
    static constexpr uint32_t H_VirtualProtect            = fnv1a("VirtualProtect");
    static constexpr uint32_t H_CreateThread              = fnv1a("CreateThread");
    static constexpr uint32_t H_LoadLibraryA              = fnv1a("LoadLibraryA");
    static constexpr uint32_t H_GetProcAddress            = fnv1a("GetProcAddress");
    static constexpr uint32_t H_CreateToolhelp32Snapshot  = fnv1a("CreateToolhelp32Snapshot");

    // ─── PEB ModuleList Walker ───
    struct ModuleCache {
        void* ntdll     = nullptr;
        void* kernel32  = nullptr;
        void* kernelbase = nullptr;
        bool  init = false;
    };
    static ModuleCache g_mod_cache;

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

    static void* FindModuleByHash(uint32_t name_hash) noexcept {
        PPEB peb = (PPEB)__readgsqword(0x60);
        auto ldr = peb->Ldr;
        auto head = &ldr->InMemoryOrderModuleList;
        for (auto e = head->Flink; e != head; e = e->Flink) {
            auto mod = CONTAINING_RECORD(e, _LDR_DATA_TABLE_ENTRY_FULL, InMemoryOrderLinks);
            if (!mod->BaseDllName.Buffer || !mod->BaseDllName.Length) continue;

            // Lowercase hash без wcslwr (нет зависимости от CRT)
            uint32_t h = 2166136261u;
            for (int i = 0; i < mod->BaseDllName.Length / 2; i++) {
                wchar_t c = mod->BaseDllName.Buffer[i];
                if (c >= L'A' && c <= L'Z') c += 32;
                h = (h ^ (uint32_t(c) & 0xFF)) * 16777619u;
            }
            if (h == name_hash) return mod->DllBase;
        }
        return nullptr;
    }

    static void EnsureCache() noexcept {
        if (g_mod_cache.init) return;
        g_mod_cache.ntdll      = FindModuleByHash(H_ntdll);
        g_mod_cache.kernel32   = FindModuleByHash(H_kernel32);
        g_mod_cache.kernelbase = FindModuleByHash(H_kernelbase);
        g_mod_cache.init       = true;
    }

    // ─── Export Resolver ───
    static void* FindExportByHash(void* module_base, uint32_t name_hash) noexcept {
        if (!module_base) return nullptr;
        auto dos = (IMAGE_DOS_HEADER*)module_base;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
        auto nt  = (IMAGE_NT_HEADERS*)((uint8_t*)module_base + dos->e_lfanew);
        auto& ed = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!ed.Size) return nullptr;
        auto exp     = (IMAGE_EXPORT_DIRECTORY*)((uint8_t*)module_base + ed.VirtualAddress);
        auto names   = (DWORD*)((uint8_t*)module_base + exp->AddressOfNames);
        auto ords    = (WORD*) ((uint8_t*)module_base + exp->AddressOfNameOrdinals);
        auto funcs   = (DWORD*)((uint8_t*)module_base + exp->AddressOfFunctions);
        for (DWORD i = 0; i < exp->NumberOfNames; i++) {
            const char* name = (const char*)((uint8_t*)module_base + names[i]);
            uint32_t h = 2166136261u;
            for (const char* p = name; *p; p++) h = (h ^ uint8_t(*p)) * 16777619u;
            if (h == name_hash)
                return (uint8_t*)module_base + funcs[ords[i]];
        }
        return nullptr;
    }

    // ─── Универсальный Resolve ───
    template<typename T = void*>
    static T Resolve(uint32_t func_hash, uint32_t mod_hash = H_ntdll) noexcept {
        EnsureCache();
        void* mod = nullptr;
        if      (mod_hash == H_ntdll)      mod = g_mod_cache.ntdll;
        else if (mod_hash == H_kernel32)   mod = g_mod_cache.kernel32;
        else if (mod_hash == H_kernelbase) mod = g_mod_cache.kernelbase;
        else                               mod = FindModuleByHash(mod_hash);
        return (T)FindExportByHash(mod, func_hash);
    }

    // ─── Типы функций ───
    typedef NTSTATUS(NTAPI* pfnNtQueryInformationProcess)(HANDLE, uint32_t, PVOID, ULONG, PULONG);
    typedef NTSTATUS(NTAPI* pfnNtSetInformationThread)(HANDLE, ULONG, PVOID, ULONG);
    typedef NTSTATUS(NTAPI* pfnNtQuerySystemInformation)(ULONG, PVOID, ULONG, PULONG);
    typedef NTSTATUS(NTAPI* pfnNtQueryObject)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    typedef NTSTATUS(NTAPI* pfnNtClose)(HANDLE);
    typedef NTSTATUS(NTAPI* pfnNtAllocateVirtualMemory)(HANDLE, PVOID*, ULONG_PTR, PSIZE_T, ULONG, ULONG);
    typedef NTSTATUS(NTAPI* pfnNtFreeVirtualMemory)(HANDLE, PVOID*, PSIZE_T, ULONG);
    typedef NTSTATUS(NTAPI* pfnNtProtectVirtualMemory)(HANDLE, PVOID*, PSIZE_T, ULONG, PULONG);
    typedef NTSTATUS(NTAPI* pfnNtCreateThreadEx)(PHANDLE, ACCESS_MASK, PVOID, HANDLE, PVOID, PVOID, ULONG, SIZE_T, SIZE_T, SIZE_T, PVOID);
    typedef NTSTATUS(NTAPI* pfnLdrLoadDll)(PWSTR, PULONG, PUNICODE_STRING, PHANDLE);

    // ─── Кешированные функции (resolve once, call many times) ───
    namespace cached {
        static pfnNtQueryInformationProcess NtQIP = nullptr;
        static pfnNtSetInformationThread    NtSIT = nullptr;
        static pfnNtClose                   NtCls = nullptr;

        static void Init() {
            if (NtQIP) return;
            NtQIP = Resolve<pfnNtQueryInformationProcess>(H_NtQueryInformationProcess);
            NtSIT = Resolve<pfnNtSetInformationThread>(H_NtSetInformationThread);
            NtCls = Resolve<pfnNtClose>(H_NtClose);
        }
    }

    // ─── Прямые вызовы (удобные обёртки) ───
    inline NTSTATUS NtQueryInformationProcess(HANDLE h, uint32_t cls, PVOID buf, ULONG sz, PULONG ret) {
        cached::Init();
        return cached::NtQIP ? cached::NtQIP(h, cls, buf, sz, ret) : STATUS_UNSUCCESSFUL;
    }

    inline void HideThreadFromDebugger(HANDLE hThread = nullptr) {
        // cached::Init();
        // if (!cached::NtSIT) return;
        // if (!hThread) hThread = GetCurrentThread();
        // cached::NtSIT(hThread, 17 /*ThreadHideFromDebugger*/, nullptr, 0);
    }

    // ─── Syscall Direct (обходит ntdll хуки полностью) ───
    // Реализует прямой syscall без прохода через ntdll.dll
    // Работает только на Windows 10+ x64 (syscall numbers стабильны между builds)

    #pragma pack(push, 1)
    struct SyscallStub {
        uint8_t mov_r10_rcx[3] = {0x4C, 0x8B, 0xD1}; // MOV R10, RCX
        uint8_t mov_eax[1]     = {0xB8};               // MOV EAX, imm32
        uint32_t syscall_num   = 0;
        uint8_t syscall_ret[3] = {0x0F, 0x05, 0xC3};  // SYSCALL; RET
    };
    #pragma pack(pop)

    static_assert(sizeof(SyscallStub) == 11, "SyscallStub size mismatch");

    // Получить syscall number из ntdll
    static uint32_t GetSyscallNum(uint32_t func_hash) noexcept {
        EnsureCache();
        uint8_t* fn = (uint8_t*)FindExportByHash(g_mod_cache.ntdll, func_hash);
        if (!fn) return 0;
        // Проверяем пролог: 4C 8B D1 B8 [sysnum] 0F 05 C3
        if (fn[0] == 0x4C && fn[1] == 0x8B && fn[2] == 0xD1 && fn[3] == 0xB8)
            return *(uint32_t*)(fn + 4);
        // Если пролог не совпадает — возможно хук. Ищем syscall в теле функции.
        for (int i = 0; i < 32; i++) {
            if (fn[i] == 0xB8 && fn[i+5] == 0x0F && fn[i+6] == 0x05)
                return *(uint32_t*)(fn + i + 1);
        }
        return 0;
    }

    // Создать исполняемый syscall stub в RWX-памяти
    static SyscallStub* MakeSyscallStub(uint32_t func_hash) {
        uint32_t num = GetSyscallNum(func_hash);
        if (!num) return nullptr;
        auto stub = (SyscallStub*)VirtualAlloc(nullptr, sizeof(SyscallStub),
                                                MEM_RESERVE | MEM_COMMIT,
                                                PAGE_EXECUTE_READWRITE);
        if (!stub) return nullptr;
        *stub = SyscallStub{};
        stub->syscall_num = num;
        return stub;
    }

    // Кешированные direct syscall stubs
    namespace direct {
        static SyscallStub* NtReadVM  = nullptr;
        static SyscallStub* NtWriteVM = nullptr;
        static SyscallStub* NtProtVM  = nullptr;

        static void Init() {
            if (NtReadVM) return;
            NtReadVM  = MakeSyscallStub(H_NtReadVirtualMemory);
            NtWriteVM = MakeSyscallStub(H_NtWriteVirtualMemory);
            NtProtVM  = MakeSyscallStub(H_NtProtectVirtualMemory);
        }
    }

} // namespace API
