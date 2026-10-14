#pragma once

/*
 * shadow cheats — Manual PE Mapper
 * Загружает PE (EXE/DLL) из байтового массива памяти.
 * Не использует LoadLibrary, не оставляет файл на диске.
 * Поддерживает: релокации, импорты через PEB, TLS коллбэки.
 */

#include <windows.h>
#include <winternl.h> // for PPEB
#include <vector>
#include <string>
#include <cstring>
#include <thread>
#include "skCrypter.h"
#include "../obfuscation.hpp"
#include "../antidebug.hpp"
#include "../vmp_markers.hpp"

namespace Mapper {

    // ─── FNV-1a хеш для резолва экспортов без GetProcAddress ───
    static constexpr uint32_t fnv1a(const char* s, uint32_t h = 2166136261u) {
        return *s ? fnv1a(s + 1, (h ^ (uint8_t)*s) * 16777619u) : h;
    }

    // ─── Резолв экспорта модуля по хешу имени (не GetProcAddress!) ───
    static void* ResolveExportByHash(void* module_base, uint32_t name_hash) {
        auto dos = (IMAGE_DOS_HEADER*)module_base;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
        auto nt = (IMAGE_NT_HEADERS*)((uint8_t*)module_base + dos->e_lfanew);
        auto& exp_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!exp_dir.Size) return nullptr;
        auto exp = (IMAGE_EXPORT_DIRECTORY*)((uint8_t*)module_base + exp_dir.VirtualAddress);
        auto names    = (DWORD*)((uint8_t*)module_base + exp->AddressOfNames);
        auto ordinals = (WORD*) ((uint8_t*)module_base + exp->AddressOfNameOrdinals);
        auto funcs    = (DWORD*)((uint8_t*)module_base + exp->AddressOfFunctions);
        for (DWORD i = 0; i < exp->NumberOfNames; i++) {
            const char* name = (const char*)((uint8_t*)module_base + names[i]);
            if (fnv1a(name) == name_hash)
                return (uint8_t*)module_base + funcs[ordinals[i]];
        }
        return nullptr;
    }

    // Определяем свою структуру LDR, так как в winternl.h имена скрыты (Reserved4 и т.д.)
    typedef struct _LDR_DATA_TABLE_ENTRY_CUSTOM {
        LIST_ENTRY InLoadOrderLinks;
        LIST_ENTRY InMemoryOrderLinks;
        LIST_ENTRY InInitializationOrderLinks;
        PVOID DllBase;
        PVOID EntryPoint;
        ULONG SizeOfImage;
        UNICODE_STRING FullDllName;
        UNICODE_STRING BaseDllName;
    } LDR_DATA_TABLE_ENTRY_CUSTOM, *PLDR_DATA_TABLE_ENTRY_CUSTOM;

    // ─── Найти модуль в PEB InMemoryOrderModuleList (без GetModuleHandle) ───
    static void* FindModuleInPEB(const wchar_t* name_lower) {
        PPEB peb = (PPEB)__readgsqword(0x60);
        auto ldr = peb->Ldr;
        auto head = &ldr->InMemoryOrderModuleList;
        for (auto e = head->Flink; e != head; e = e->Flink) {
            auto mod = CONTAINING_RECORD(e, LDR_DATA_TABLE_ENTRY_CUSTOM, InMemoryOrderLinks);
            if (!mod->BaseDllName.Buffer) continue;
            // Сравниваем без wcscmp (потенциальный хук)
            wchar_t buf[64] = {};
            int len = mod->BaseDllName.Length / 2;
            if (len >= 64) continue;
            for (int i = 0; i < len; i++)
                buf[i] = (mod->BaseDllName.Buffer[i] >= L'A' && mod->BaseDllName.Buffer[i] <= L'Z')
                          ? mod->BaseDllName.Buffer[i] + 32
                          : mod->BaseDllName.Buffer[i];
            bool match = true;
            for (int i = 0; i < len && name_lower[i]; i++)
                if (buf[i] != name_lower[i]) { match = false; break; }
            if (match) return mod->DllBase;
        }
        return nullptr;
    }

    // ─── Структура результата маппинга ───
    struct MapResult {
        bool    success  = false;
        void*   base     = nullptr;  // Mapped base address
        size_t  size     = 0;
        void*   entry    = nullptr;  // Entry point
        DWORD   hdr_size = 0;        // SizeOfHeaders — для Anti-Dump после старта
    };

    static void map_log(const char* /*msg*/) {
        // Logging disabled — shadow_log.txt not written to disk
    }

    // ─── Основная функция маппинга ───
    __declspec(noinline) MapResult MapFromMemory(const std::vector<uint8_t>& raw_bytes) {

        map_log("Step 0: Start");
        MapResult result;

        // OBF: signature check через MBA. Прямой `src_dos->e_magic == IMAGE_DOS_SIGNATURE`
        //   легко распознать по константе 0x5A4D; patch в `mov eax, 1; jmp ok` обходит.
        //   Обернём в MBA-equals — hex-rays выдаст `(x ^ K) + Adj == T` форму.
        if (raw_bytes.size() < sizeof(IMAGE_DOS_HEADER)) return result;
        auto src_dos = (IMAGE_DOS_HEADER*)raw_bytes.data();
        if (!shadow::obf::mba_equals((uintptr_t)src_dos->e_magic,
                                      (uintptr_t)IMAGE_DOS_SIGNATURE))
            return result;
        auto src_nt = (IMAGE_NT_HEADERS*)(raw_bytes.data() + src_dos->e_lfanew);
        if (!shadow::obf::mba_equals((uintptr_t)src_nt->Signature,
                                      (uintptr_t)IMAGE_NT_SIGNATURE))
            return result;

        // 1. Выделить память под образ
        map_log("Step 1: Allocating memory");
        size_t image_size = src_nt->OptionalHeader.SizeOfImage;
        uint8_t* mapped = (uint8_t*)VirtualAlloc(
            nullptr, image_size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (!mapped) return result;

        // 2. Скопировать заголовки
        map_log("Step 2: Copying headers");
        memcpy(mapped, raw_bytes.data(), src_nt->OptionalHeader.SizeOfHeaders);

        // 3. Скопировать секции
        map_log("Step 3: Copying sections");
        auto sec = IMAGE_FIRST_SECTION(src_nt);
        for (WORD i = 0; i < src_nt->FileHeader.NumberOfSections; i++, sec++) {
            if (!sec->SizeOfRawData) continue;
            uint8_t* dst = mapped + sec->VirtualAddress;
            const uint8_t* src = raw_bytes.data() + sec->PointerToRawData;
            memcpy(dst, src, sec->SizeOfRawData);
        }

        // 4. Применить релокации (Base Relocations)
        map_log("Step 4: Relocations");
        uintptr_t preferred = src_nt->OptionalHeader.ImageBase;
        uintptr_t actual    = (uintptr_t)mapped;
        intptr_t  delta     = (intptr_t)(actual - preferred);

        if (delta != 0) {
            auto& reloc_dir = src_nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
            if (reloc_dir.Size) {
                auto reloc = (IMAGE_BASE_RELOCATION*)(mapped + reloc_dir.VirtualAddress);
                while (reloc->VirtualAddress) {
                    DWORD count = (reloc->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
                    auto* entries = (WORD*)((uint8_t*)reloc + sizeof(IMAGE_BASE_RELOCATION));
                    for (DWORD i = 0; i < count; i++) {
                        int type   = entries[i] >> 12;
                        int offset = entries[i] & 0xFFF;
                        if (type == IMAGE_REL_BASED_DIR64) {
                            auto* patch = (int64_t*)(mapped + reloc->VirtualAddress + offset);
                            *patch += delta;
                        } else if (type == IMAGE_REL_BASED_HIGHLOW) {
                            auto* patch = (int32_t*)(mapped + reloc->VirtualAddress + offset);
                            *patch += (int32_t)delta;
                        }
                    }
                    reloc = (IMAGE_BASE_RELOCATION*)((uint8_t*)reloc + reloc->SizeOfBlock);
                }
            }
        }

        // 5. Резолвить импорты через PEB (не LoadLibrary)
        map_log("Step 5: Imports");
        auto& imp_dir = src_nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (imp_dir.Size) {
            auto desc = (IMAGE_IMPORT_DESCRIPTOR*)(mapped + imp_dir.VirtualAddress);
            while (desc->Name) {
                char* mod_name = (char*)(mapped + desc->Name);

                // Найти модуль в PEB
                wchar_t mod_lower[128] = {};
                for (int i = 0; mod_name[i] && i < 127; i++)
                    mod_lower[i] = (mod_name[i] >= 'A' && mod_name[i] <= 'Z')
                                   ? mod_name[i] + 32 : mod_name[i];

                void* hMod = FindModuleInPEB(mod_lower);
                if (!hMod) {
                    // Модуль не загружен — загрузить через LoadLibrary (допустимо для не-критичных)
                    wchar_t wname[128] = {};
                    MultiByteToWideChar(CP_ACP, 0, mod_name, -1, wname, 128);
                    hMod = LoadLibraryW(wname);
                }

                if (hMod) {
                    auto thunk     = (IMAGE_THUNK_DATA*)(mapped + desc->FirstThunk);
                    auto orig_thunk= (IMAGE_THUNK_DATA*)(mapped + desc->OriginalFirstThunk);
                    while (thunk->u1.AddressOfData) {
                        void* fn_addr = nullptr;
                        if (orig_thunk && (orig_thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG64)) {
                            // Import by ordinal
                            WORD ordinal = (WORD)(orig_thunk->u1.Ordinal & 0xFFFF);
                            auto dos2 = (IMAGE_DOS_HEADER*)hMod;
                            auto nt2  = (IMAGE_NT_HEADERS*)((uint8_t*)hMod + dos2->e_lfanew);
                            auto exp  = (IMAGE_EXPORT_DIRECTORY*)((uint8_t*)hMod
                                         + nt2->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);
                            if (ordinal >= exp->Base && ordinal < exp->Base + exp->NumberOfFunctions) {
                                auto funcs = (DWORD*)((uint8_t*)hMod + exp->AddressOfFunctions);
                                fn_addr = (uint8_t*)hMod + funcs[ordinal - exp->Base];
                            }
                            if (!fn_addr) {
                                char err[256];
                                sprintf_s(err, "IMPORT FAIL ordinal: %s!#%u", mod_name, (unsigned)ordinal);
                                map_log(err);
                                thunk++;
                                if (orig_thunk) orig_thunk++;
                                continue; // Don't write NULL into IAT — same fix as name branch
                            }
                        } else {
                            // Import by name — резолвим по хешу
                            PIMAGE_IMPORT_BY_NAME ibn = nullptr;
                            if (orig_thunk) {
                                ibn = (PIMAGE_IMPORT_BY_NAME)(mapped + orig_thunk->u1.AddressOfData);
                            } else {
                                ibn = (PIMAGE_IMPORT_BY_NAME)(mapped + thunk->u1.AddressOfData);
                            }
                            
                            uint32_t hash = fnv1a(ibn->Name);
                            fn_addr = ResolveExportByHash(hMod, hash);
                            if (!fn_addr) {
                                // Fallback на GetProcAddress (не критично для known DLL)
                                fn_addr = GetProcAddress((HMODULE)hMod, ibn->Name);
                            }
                            if (!fn_addr) {
                                // логируем неразрешённый импорт вместо записи null в IAT
                                // Запись 0 в IAT → вызов по адресу 0 → WRITE at 0x0 (именно этот краш)
                                char err[256];
                                sprintf_s(err, "IMPORT FAIL name: %s!%s (hash=0x%08X)", mod_name, ibn->Name, hash);
                                map_log(err);
                                thunk++;
                                if (orig_thunk) orig_thunk++;
                                continue; // Оставляем оригинальный thunk — не пишем null
                            }
                        }
                        thunk->u1.Function = (ULONGLONG)fn_addr;
                        thunk++;
                        if (orig_thunk) orig_thunk++;
                    }
                }
                desc++;
            }
        }

        // 5.5 Register Exception Handlers (x64)
        map_log("Step 5.5: Exception Table");
        auto& exc_dir = src_nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if (exc_dir.Size) {
            auto pRtlAddFunctionTable = (BOOLEAN(WINAPI*)(PRUNTIME_FUNCTION, DWORD, DWORD64))
                GetProcAddress(GetModuleHandleA("kernel32.dll"), "RtlAddFunctionTable");
            if (pRtlAddFunctionTable) {
                pRtlAddFunctionTable((PRUNTIME_FUNCTION)(mapped + exc_dir.VirtualAddress),
                                     exc_dir.Size / sizeof(RUNTIME_FUNCTION),
                                     (DWORD64)mapped);
                map_log("Step 5.5: RtlAddFunctionTable called");
            }
        }

        // 6. Выставить правильные разрешения по секциям
        map_log("Step 6: Permissions");
        sec = IMAGE_FIRST_SECTION((IMAGE_NT_HEADERS*)(mapped + src_dos->e_lfanew));
        auto mapped_nt = (IMAGE_NT_HEADERS*)(mapped + src_dos->e_lfanew);
        sec = IMAGE_FIRST_SECTION(mapped_nt);
        for (WORD i = 0; i < mapped_nt->FileHeader.NumberOfSections; i++, sec++) {
            if (!sec->VirtualAddress || !sec->Misc.VirtualSize) continue;
            DWORD old = 0;
            DWORD prot = PAGE_READONLY;
            bool exec  = (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
            bool write = (sec->Characteristics & IMAGE_SCN_MEM_WRITE)   != 0;
            bool read  = (sec->Characteristics & IMAGE_SCN_MEM_READ)    != 0;
            if (exec && write) prot = PAGE_EXECUTE_READWRITE;
            else if (exec && read) prot = PAGE_EXECUTE_READ;
            else if (exec) prot = PAGE_EXECUTE;
            else if (write) prot = PAGE_READWRITE;
            else if (read) prot = PAGE_READONLY;
            VirtualProtect(mapped + sec->VirtualAddress, sec->Misc.VirtualSize, prot, &old);
        }

        // 7. Вызвать TLS коллбэки (если есть)
        map_log("Step 7: TLS Callbacks");
        auto& tls_dir = src_nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
        if (tls_dir.Size) {
            auto tls = (IMAGE_TLS_DIRECTORY*)(mapped + tls_dir.VirtualAddress);
            
            // tls читается из mapped (уже релоцированного образа).
            // После шага 4 (релокации) поля TLS directory содержат ACTUAL VA,
            // а НЕ preferred VA. Применять delta снова — ошибка (даёт FFFFFFFE...)
            // Просто проверяем что адрес в пределах mapped.
            uintptr_t cb_va = (uintptr_t)tls->AddressOfCallBacks;
            // не применяем delta — просто bounds check
            if (!cb_va || cb_va < (uintptr_t)mapped || cb_va >= (uintptr_t)mapped + image_size) {
                map_log("Step 7.1: AddressOfCallBacks out of mapped bounds — skip TLS callbacks");
                cb_va = 0;
            }

            // Инициализируем TLS Index — тоже уже actual VA после релокаций
            uintptr_t index_va = (uintptr_t)tls->AddressOfIndex;
            // не применяем delta — просто bounds check
            if (index_va && index_va >= (uintptr_t)mapped && index_va < (uintptr_t)mapped + image_size) {
                *(DWORD*)index_va = 0;
                map_log("Step 7.1: TlsIndex fixed");
            } else if (index_va) {
                map_log("Step 7.1: TlsIndex VA out of mapped bounds — skipped");
            }
            
            auto callbacks = cb_va ? (PIMAGE_TLS_CALLBACK*)cb_va : nullptr;
            map_log("Step 7.1: Found TLS directory");
            
            if (callbacks) {
                map_log("Step 7.2: Callbacks array is not null");
                int cb_idx = 0;
                while (cb_idx < 10 && *callbacks != nullptr) {
                    uintptr_t cb_func_va = (uintptr_t)*callbacks;
                    
                    // TLS callback addresses are absolute VAs based on the preferred image base.
                    // We need to convert them to actual mapped addresses:
                    //   RVA = cb_func_va - preferred_base
                    //   actual_addr = mapped_base + RVA
                    uintptr_t cb_rva = cb_func_va - preferred;
                    uintptr_t cb_actual = actual + cb_rva;
                    
                    auto func = (PIMAGE_TLS_CALLBACK)cb_actual;
                    
                    char cb_msg[256];
                    sprintf_s(cb_msg, "Step 7.3: Executing callback %d at %p (RVA=0x%llX)", cb_idx, (void*)func, (unsigned long long)cb_rva);
                    map_log(cb_msg);
                    
                    uintptr_t map_start = (uintptr_t)mapped;
                    uintptr_t map_end = map_start + image_size;
                    
                    if (cb_actual >= map_start && cb_actual < map_end) {
                        __try {
                            func(mapped, DLL_PROCESS_ATTACH, nullptr);
                            sprintf_s(cb_msg, "Step 7.4: Callback %d finished", cb_idx);
                            map_log(cb_msg);
                        } __except (EXCEPTION_EXECUTE_HANDLER) {
                            sprintf_s(cb_msg, "Step 7.4: Callback %d EXCEPTION: 0x%08X", cb_idx, GetExceptionCode());
                            map_log(cb_msg);
                        }
                    } else {
                        sprintf_s(cb_msg, "Step 7.4: Callback %d OUT OF BOUNDS (addr=%p map=%p..%p) - skipped!", cb_idx, (void*)cb_actual, (void*)map_start, (void*)map_end);
                        map_log(cb_msg);
                    }
                    callbacks++;
                    cb_idx++;
                }
            }
        }

        result.success   = mapped != nullptr;
        result.base      = mapped;
        result.size      = image_size;
        result.entry     = mapped + mapped_nt->OptionalHeader.AddressOfEntryPoint;
        result.hdr_size  = mapped_nt->OptionalHeader.SizeOfHeaders;
        map_log("Step 8: Done");
        return result;
    }

    // ─── Вспомогательная функция для SEH защиты DllMain ───
    static DWORD SafeCallEntry(void* entry_ptr, void* base) {
        typedef BOOL(WINAPI* Entry_t)(HINSTANCE, DWORD, LPVOID);
        auto entry = (Entry_t)entry_ptr;
        DWORD exc_code = 0;
        __try {
            entry((HINSTANCE)base, DLL_PROCESS_ATTACH, nullptr);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            exc_code = GetExceptionCode();
        }
        return exc_code;
    }

    // ─── Запуск смаппированного образа ───
    // Порядок: вызываем Entry (DllMain/WinMain), затем ждём ~500ms чтобы
    // VMP/CRT завершили инициализацию, и только потом стираем PE-заголовки.
    // Это позволяет VMP нормально распаковаться (заголовок цел), а после
    // старта чита заголовок уничтожается — Anti-Dump снова работает.
    __declspec(noinline) bool LaunchMapped(const MapResult& mr) {
        SHADOW_PROTECT_MUT("LaunchMapped");
        if (!mr.success || !mr.entry) return false;

        // Вызываем DllMain через SEH-обертку. MSVC запрещает __try в функциях,
        // где есть объекты с деструкторами (std::thread ниже), поэтому вынесли.
        DWORD entry_exc = SafeCallEntry(mr.entry, mr.base);
        if (entry_exc) {
            char exc_msg[64];
            sprintf_s(exc_msg, "LAUNCHER: entry() EXCEPTION 0x%08X at DLL_PROCESS_ATTACH", entry_exc);
            map_log(exc_msg);
            // DllMain крашнулся — маппирование провалено, чит не запустится
            SHADOW_PROTECT_END();
            return false;
        }

        SHADOW_PROTECT_END();
        return true;
    }

    // ─── Безопасная выгрузка — стираем байты из памяти ───
    __declspec(noinline) void Unload(MapResult& mr) {
        if (mr.base && mr.size) {
            SecureZeroMemory(mr.base, mr.size);
            VirtualFree(mr.base, 0, MEM_RELEASE);
            mr.base    = nullptr;
            mr.size    = 0;
        }
        mr.success = false; // сбрасываем флаг чтобы можно было инжектить снова
        mr.entry   = nullptr;
    }

} // namespace Mapper
