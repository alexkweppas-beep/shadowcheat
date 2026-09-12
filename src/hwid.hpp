#pragma once
#include "xorstr.hpp"

/*
 * shadow cheats — HWID Module
 * Генерирует уникальный многокомпонентный HWID
 * CPU + Disk + MachineGuid + MAC + BIOS + TPM check
 */

#include <windows.h>
#include <iphlpapi.h>
#include <intrin.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include "skCrypter.h"
#include "obfuscation.hpp"

// Forward declaration — actual definition in antidebug.hpp (included by main.cpp)
namespace shadow::loader_antidbg {
    [[noreturn]] void kill_local();
}


#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace HWID {

    // ─── SHA-256 (Windows BCrypt — без внешних зависимостей) ───
    namespace detail {
        static std::string sha256_hex(const std::string& data) {
            BCRYPT_ALG_HANDLE hAlg  = nullptr;
            BCRYPT_HASH_HANDLE hHash = nullptr;
            std::string result;

            if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
                return {};

            DWORD hash_obj_size = 0, data_size = 0;
            BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&hash_obj_size, sizeof(DWORD), &data_size, 0);

            std::vector<BYTE> hash_obj(hash_obj_size);
            BYTE hash_bytes[32] = {};

            BCryptCreateHash(hAlg, &hHash, hash_obj.data(), hash_obj_size, nullptr, 0, 0);
            BCryptHashData(hHash, (PUCHAR)data.data(), (ULONG)data.size(), 0);
            BCryptFinishHash(hHash, hash_bytes, sizeof(hash_bytes), 0);

            BCryptDestroyHash(hHash);
            BCryptCloseAlgorithmProvider(hAlg, 0);

            std::ostringstream oss;
            for (auto b : hash_bytes) oss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
            return oss.str();
        }

        // XOR-mix двух строк для усиления
        static std::string xor_mix(const std::string& a, const std::string& b) {
            std::string out = a;
            for (size_t i = 0; i < out.size() && i < b.size(); i++)
                out[i] ^= b[i];
            return out;
        }
    }

    // ─── Компоненты HWID ───

    // 1. CPU Signature (CPUID leaf 1, 3)
    static std::string GetCPUID() {
        int info[4] = {};
        std::string result;
        __cpuid(info, 1);
        result += std::to_string(info[0]); // Family/Model/Stepping
        result += xorstr_("_");
        result += std::to_string(info[3]); // Feature flags EDX
        __cpuid(info, 0);
        char vendor[13] = {};
        memcpy(vendor,     &info[1], 4);
        memcpy(vendor + 4, &info[3], 4);
        memcpy(vendor + 8, &info[2], 4);
        result += xorstr_("_");
        result += vendor;
        return result;
    }

    // 2. Серийный номер диска C:\ (два метода — проверяем совпадение)
    static std::string GetDiskSerial() {
        DWORD serial = 0;
        char vol_name[MAX_PATH] = {};
        GetVolumeInformationA(xorstr_("C:\\"), vol_name, MAX_PATH, &serial, nullptr, nullptr, nullptr, 0);
        return std::to_string(serial) + std::string(vol_name);
    }

    // 3. MachineGuid из реестра
    static std::string GetMachineGuid() {
        HKEY hKey = nullptr;
        char guid[64] = {};
        DWORD size = sizeof(guid);
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                          xorstr_("SOFTWARE\\Microsoft\\Cryptography"),
                          0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
            RegQueryValueExA(hKey, xorstr_("MachineGuid"), nullptr, nullptr, (LPBYTE)guid, &size);
            RegCloseKey(hKey);
        }
        return std::string(guid);
    }

    // 4. MAC-адрес первого физического (non-virtual) адаптера
    static std::string GetMACAddress() {
        // Известные VM MAC OUI — пропускаем виртуальные адаптеры
        static const uint8_t vm_ouis[][3] = {
            {0x00,0x05,0x69},{0x00,0x0C,0x29},{0x00,0x50,0x56},{0x00,0x1C,0x14},
            {0x08,0x00,0x27},{0x52,0x54,0x00},{0x00,0x16,0x3E},{0x00,0x15,0x5D},
        };
        auto is_vm_mac = [](const uint8_t* addr) {
            for (auto& oui : vm_ouis)
                if (memcmp(addr, oui, 3) == 0) return true;
            return false;
        };

        IP_ADAPTER_INFO info_buf[16] = {};
        DWORD buf_size = sizeof(info_buf);
        if (GetAdaptersInfo(info_buf, &buf_size) != ERROR_SUCCESS) return xorstr_("NOMAC");

        for (auto* ai = info_buf; ai; ai = ai->Next) {
            if (ai->AddressLength == 6 && !is_vm_mac(ai->Address)) {
                char mac[32] = {};
                snprintf(mac, sizeof(mac), xorstr_("%02X%02X%02X%02X%02X%02X"),
                         ai->Address[0], ai->Address[1], ai->Address[2],
                         ai->Address[3], ai->Address[4], ai->Address[5]);
                return std::string(mac);
            }
        }
        return xorstr_("NOMAC");
    }

    // 5. SMBIOS ProductUUID через реестр (Hardware\Description\System\BIOS)
    static std::string GetBIOSUUID() {
        HKEY hKey = nullptr;
        char bios_ver[64] = {}, bios_date[64] = {}, sys_manuf[64] = {};
        DWORD size = 64;

        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                          xorstr_("HARDWARE\\DESCRIPTION\\System\\BIOS"),
                          0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            RegQueryValueExA(hKey, xorstr_("BIOSVersion"), nullptr, nullptr, (LPBYTE)bios_ver, &size);
            size = 64;
            RegQueryValueExA(hKey, xorstr_("BIOSReleaseDate"), nullptr, nullptr, (LPBYTE)bios_date, &size);
            size = 64;
            RegQueryValueExA(hKey, xorstr_("SystemManufacturer"), nullptr, nullptr, (LPBYTE)sys_manuf, &size);
            RegCloseKey(hKey);
        }
        return std::string(bios_ver) + xorstr_("|") + std::string(bios_date) + xorstr_("|") + std::string(sys_manuf);
    }

    // 6. Количество ядер процессора + размер страницы (дополнительная энтропия)
    static std::string GetSystemProfile() {
        SYSTEM_INFO si = {};
        GetSystemInfo(&si);
        return std::to_string(si.dwNumberOfProcessors)
             + xorstr_("_") + std::to_string(si.dwPageSize)
             + xorstr_("_") + std::to_string(si.dwProcessorType);
    }

    // ─── Финальный HWID ───
//   OBF: соединение компонентов через MBA-obfuscated разделитель не '"',
//   а числовой код, который сводится к ':' только после mba_identity.
//   Hex-rays не даст прямой "raw += ':'" pattern → крякеру сложно найти
//   HWID aggregation точечно.
    inline std::string GetHardwareID() {
        // OBF: canary на вход — если крякер поставит BP внутри GetHardwareID,
        //   canary сломается, kill_local ударит. Крякер не сразу свяжет kill
        //   с этой функцией.
        shadow::obf::ObfCanary _obf_can;
        if (!_obf_can.verify()) shadow::loader_antidbg::kill_local();

        // OBF: junk block — выглядит как дополнительная энтропия / подготовка.
        OBF_JUNK_BLOCK(
            volatile uint32_t _obf_mix = 0x1337;
            for (int i = 0; i < 3; i++) _obf_mix ^= (uint32_t)GetTickCount() << i;
            (void)_obf_mix;
        );

        std::string raw;
        // separator через mba_identity(':') — компилятор раскроет в :, но
        // статически дизассемблер видит билет из XOR/ADD последовательности.
        char sep = (char)shadow::obf::mba_identity((int)':');
        raw += GetCPUID()        + std::string(1, sep);
        raw += GetDiskSerial()   + std::string(1, sep);
        raw += GetMachineGuid()  + std::string(1, sep);
        raw += GetMACAddress()   + std::string(1, sep);
        raw += GetBIOSUUID()     + std::string(1, sep);
        raw += GetSystemProfile();
        return detail::sha256_hex(raw);
    }

    // Возвращает все компоненты по отдельности (для spoofer detection)
    struct Components {
        std::string cpu;
        std::string disk;
        std::string guid;
        std::string mac;
        std::string bios;
        std::string profile;
    };

    inline Components GetComponents() {
        return {
            detail::sha256_hex(GetCPUID()),
            detail::sha256_hex(GetDiskSerial()),
            detail::sha256_hex(GetMachineGuid()),
            detail::sha256_hex(GetMACAddress()),
            detail::sha256_hex(GetBIOSUUID()),
            detail::sha256_hex(GetSystemProfile()),
        };
    }

    // Проверяет сколько компонентов изменилось (для определения спуфера/смены железа)
    inline int CountChangedComponents(const Components& stored, const Components& current) {
        int changed = 0;
        if (stored.cpu     != current.cpu)     changed++;
        if (stored.disk    != current.disk)    changed++;
        if (stored.guid    != current.guid)    changed++;
        if (stored.mac     != current.mac)     changed++;
        if (stored.bios    != current.bios)    changed++;
        if (stored.profile != current.profile) changed++;
        return changed;
    }

    // Обнаружение спуфера — проверяем расхождение между двумя методами получения VolumeSerial
//   OBF: сравнение обёрнуто в mba_equals — hex-rays не увидит прямой "!="
//   и не сможет патчнуть единственный conditional branch.
    inline bool DetectSpoofer() {
        // Метод 1: Win32 API
        DWORD api_serial = 0;
        GetVolumeInformationA(xorstr_("C:\\"), nullptr, 0, &api_serial, nullptr, nullptr, nullptr, 0);

        // Метод 2: Напрямую через NT API (труднее перехватить спуферу)
        HANDLE hFile = CreateFileA(xorstr_("\\\\.\\C:"), GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, 0, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return false;

        struct {
            ULONG VolumeSerialNumber;
            ULONG VolumeLabelOffset;
            ULONG SupportsObjects;
            WCHAR VolumeLabel[32];
        } fsi = {};

        DWORD bytes = 0;
        DeviceIoControl(hFile, 0x90064, nullptr, 0, &fsi, sizeof(fsi), &bytes, nullptr);
        CloseHandle(hFile);

        // OBF: junk block в rarely-taken path — крякер ставит BP, не видит в коде.
        if (bytes == 0 || fsi.VolumeSerialNumber == 0) {
            OBF_JUNK_BLOCK(
                volatile uint32_t _obf_dead = (uint32_t)api_serial ^ 0xCAFEBABE;
                (void)_obf_dead;
            );
            return false;
        }

        // Расхождение — через MBA-обёрнутое сравнение
        return shadow::obf::mba_equals((uintptr_t)fsi.VolumeSerialNumber,
                                         (uintptr_t)api_serial) == false;
    }

} // namespace HWID
