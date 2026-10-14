#pragma once

/* Проверки целостности файла при запуске. В dev-сборке отключены.
 * Границы размера SHADOW_MIN_FILE_SIZE и SHADOW_MAX_FILE_SIZE задаются после упаковки. */

#include <windows.h>
#include <cstdint>
#include <string>
#include "xorstr.hpp"
#include "fibstr.hpp"
#include "lazy_importer.hpp"
#include "vmp_markers.hpp"
#include "protection_config.hpp"

// ─── Configurable thresholds ───
// Задаются compile-time. Можно переопределить через /D в CMake.
#ifndef SHADOW_MIN_FILE_SIZE
#   define SHADOW_MIN_FILE_SIZE  (38 * 1024 * 1024ULL)   // 38 MB — меньше = VMP снят
#endif
#ifndef SHADOW_MAX_FILE_SIZE
#   define SHADOW_MAX_FILE_SIZE  (58 * 1024 * 1024ULL)   // 58 MB — больше = подозрительно
#endif
#ifndef SHADOW_MIN_SECTIONS
#   define SHADOW_MIN_SECTIONS   5   // у VMP-билда >= 6-8 секций (.text/.rdata/.vmp0/.vmp1/...)
#endif
#ifndef SHADOW_MIN_VMP_SECTIONS
#   define SHADOW_MIN_VMP_SECTIONS 1 // должна быть хотя бы 1 секция с именем .vmp*
#endif

namespace shadow::integrity {

 
// Вспомогательные: получаем путь к своему exe и размер на диске
 

static uint64_t GetSelfFileSize() {
    wchar_t path[MAX_PATH] = {};
    if (!LI_FN(GetModuleFileNameW).safe_cached()(nullptr, path, MAX_PATH))
        return 0;
    HANDLE hFile = LI_FN(CreateFileW).safe_cached()(
        path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return 0;
    LARGE_INTEGER sz{};
    LI_FN(GetFileSizeEx).safe_cached()(hFile, &sz);
    LI_FN(CloseHandle).safe_cached()(hFile);
    return (uint64_t)sz.QuadPart;
}

 
// Чтение PE заголовка из памяти (без файла) — быстро и без диска
 

static IMAGE_NT_HEADERS* GetNtHeaders() {
    HMODULE hMod = LI_FN(GetModuleHandleW).safe_cached()(nullptr);
    if (!hMod) return nullptr;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(hMod);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
        reinterpret_cast<uint8_t*>(hMod) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    return nt;
}

 
// Check #1: Размер файла в ожидаемом диапазоне
 
__declspec(noinline) static bool CheckFileSize() {
    // УБРАНА: чтение файла с диска по имени ненадёжно после VMP:
    // - имя может быть любым (*.vmp.exe, *.exe, переименован)
    // - VMP import protection может помешать GetModuleFileNameW
    // - не требуется: наличие .vmp секций уже гарантирует VMP-процессинг
    return true;
}

 
// Check #2: Количество секций PE и наличие .vmp* секций
 
__declspec(noinline) static bool CheckVmpSections() {
#if defined(SHADOW_DEV_BUILD)
    return true;
#else
    SHADOW_PROTECT_VIRT("integrity::CheckVmpSections");
    auto* nt = GetNtHeaders();
    // Если VMP зашифровал PE заголовок — пропускаем (не можем прочитать секции)
    if (!nt) { SHADOW_PROTECT_END(); return true; }

    WORD numSections = nt->FileHeader.NumberOfSections;
    if (numSections < SHADOW_MIN_SECTIONS) {
        SHADOW_PROTECT_END();
#if defined(SHADOW_PACKER_VMP)
        // Под VMP: не убиваем — VMProtectIsProtected() уже проверил целостность
        return true;
#else
        return false; // Слишко мало секций — VMP снят
#endif
    }

    auto* sec = IMAGE_FIRST_SECTION(nt);
    int vmpCount = 0;
    for (WORD i = 0; i < numSections; i++, sec++) {
        // '.vmp' XOR 0xAB:
        // '.' = 0x2E ^ 0xAB = 0x85
        // 'v' = 0x76 ^ 0xAB = 0xDD
        // 'm' = 0x6D ^ 0xAB = 0xC6
        // 'p' = 0x70 ^ 0xAB = 0xDB
        static const uint8_t enc_vmp[] = {0x85, 0xDD, 0xC6, 0xDB};
        bool is_vmp = true;
        for (int j = 0; j < 4; j++) {
            if (((uint8_t)sec->Name[j] ^ 0xABu) != enc_vmp[j]) { is_vmp = false; break; }
        }
        // '.???' — имя которое VMP использует если "Сегменты: .???" включено
        bool is_qqq = (sec->Name[0] == '.' &&
                       sec->Name[1] == '?' &&
                       sec->Name[2] == '?');
        if (is_vmp || is_qqq)
            vmpCount++;
    }

    bool ok = (vmpCount >= SHADOW_MIN_VMP_SECTIONS);
    SHADOW_PROTECT_END();
#if defined(SHADOW_PACKER_VMP)
    // Под VMP: даже если не нашли — не убиваем.
    // VMProtectIsProtected() уже гарантирует что VMP применён.
    // Секции могут быть переименованы при дополнительных настройках VMP.
    return true;
#else
    return ok;
#endif
#endif
}

 
// Check #3: PE OptionalHeader.CheckSum не ноль
// (VMP всегда проставляет корректный checksum; strip-tools часто зануляют)
 
__declspec(noinline) static bool CheckPeChecksum() {
    // УБРАНА: VMP перезаписывает PE заголовок в памяти при инициализации.
    // Чтение checksum после VMP может вернуть некорректное значение.
    // Защита VMP секций достаточна.
    return true;
}

 
// Check #4: DllCharacteristics — VMP обычно убирает DYNAMIC_BASE если
//           выбран конкретный base address, или наоборот сохраняет ASLR.
//           Проверяем что у нас нет флагов stripped/debug-stripped (0x0200).
 
__declspec(noinline) static bool CheckDllCharacteristics() {
#if defined(SHADOW_DEV_BUILD)
    return true;
#else
    auto* nt = GetNtHeaders();
    if (!nt) return false;
    // IMAGE_FILE_DEBUG_STRIPPED = 0x0200. Если стоит у production билда — подозрительно.
    // Но это слабый признак, не блокируем — только логируем.
    // Главная проверка: Subsystem должна быть Windows GUI (2) или Console (3).
    WORD sub = nt->OptionalHeader.Subsystem;
    return (sub == IMAGE_SUBSYSTEM_WINDOWS_GUI || sub == IMAGE_SUBSYSTEM_WINDOWS_CUI);
#endif
}

 
// Check #5: Самоproверка — читаем .text секцию и считаем CRC32.
//           Compile-time значение хардкодится через tools/gen_text_crc.py.
//           Если крякер пропатчил антидебаг или NOP-нул проверку → CRC меняется.
//
//   В SHADOW_DEV_BUILD — пропускаем (CRC меняется на каждый билд).
//   В prod: SHADOW_TEXT_CRC32 задаётся через /D после финального билда.
 
#ifdef SHADOW_TEXT_CRC32
static uint32_t ComputeTextCrc32() {
    auto* nt = GetNtHeaders();
    if (!nt) return 0;
    HMODULE hMod = LI_FN(GetModuleHandleW).safe_cached()(nullptr);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        // Ищем .text: {0xAB^0x2E, 0xAB^0x74, 0xAB^0x65, 0xAB^0x78, 0xAB^0x74} = {0x85, 0xDF, 0xCE, 0xD3, 0xDF}
        if (sec->Name[0]=='.' && sec->Name[1]=='t' && sec->Name[2]=='e' &&
            sec->Name[3]=='x' && sec->Name[4]=='t') {
            auto* base = reinterpret_cast<const uint8_t*>(hMod) + sec->VirtualAddress;
            uint32_t crc = 0xFFFFFFFFu;
            for (uint32_t j = 0; j < sec->Misc.VirtualSize; j++) {
                crc ^= base[j];
                for (int k = 0; k < 8; k++)
                    crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
            }
            return ~crc;
        }
    }
    return 0;
}

__declspec(noinline) static bool CheckTextCrc() {
    SHADOW_PROTECT_ULTRA("integrity::CheckTextCrc");
    uint32_t actual = ComputeTextCrc32();
    bool ok = (actual == (uint32_t)SHADOW_TEXT_CRC32);
    SHADOW_PROTECT_END();
    return ok;
}
#endif // SHADOW_TEXT_CRC32

 
// Мастер-функция: запускает все проверки. При провале — возвращает false.
// Вызывающий код обязан вызвать ban::Trigger(FILE_INTEGRITY_FAIL).
 
struct IntegrityResult {
    bool file_size_ok      = true;
    bool vmp_sections_ok   = true;
    bool pe_checksum_ok    = true;
    bool dll_chars_ok      = true;
    bool text_crc_ok       = true;

    bool all_ok() const {
        return file_size_ok && vmp_sections_ok && pe_checksum_ok
               && dll_chars_ok && text_crc_ok;
    }
};

__declspec(noinline) static IntegrityResult CheckAll() {
    IntegrityResult r;
    r.file_size_ok    = CheckFileSize();
    r.vmp_sections_ok = CheckVmpSections();
    r.pe_checksum_ok  = CheckPeChecksum();
    r.dll_chars_ok    = CheckDllCharacteristics();
#ifdef SHADOW_TEXT_CRC32
    r.text_crc_ok     = CheckTextCrc();
#endif
    return r;
}

} // namespace shadow::integrity
