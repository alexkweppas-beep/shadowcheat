#pragma once
#include <windows.h>
#include <iphlpapi.h>
#include <intrin.h>
#include <wincrypt.h>
#include <bcrypt.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")

namespace HWID_DLL {
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

    static std::string GetCPUID() {
        int info[4] = {};
        std::string result;
        __cpuid(info, 1);
        result += std::to_string(info[0]); // Family/Model/Stepping
        result += "_";
        result += std::to_string(info[3]); // Feature flags EDX
        __cpuid(info, 0);
        char vendor[13] = {};
        memcpy(vendor,     &info[1], 4);
        memcpy(vendor + 4, &info[3], 4);
        memcpy(vendor + 8, &info[2], 4);
        result += "_";
        result += vendor;
        return result;
    }

    static std::string GetDiskSerial() {
        DWORD serial = 0;
        char vol_name[MAX_PATH] = {};
        GetVolumeInformationA("C:\\", vol_name, MAX_PATH, &serial, nullptr, nullptr, nullptr, 0);
        return std::to_string(serial) + std::string(vol_name);
    }

    static std::string GetMachineGuid() {
        HKEY hKey = nullptr;
        char guid[64] = {};
        DWORD size = sizeof(guid);
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
            RegQueryValueExA(hKey, "MachineGuid", nullptr, nullptr, (LPBYTE)guid, &size);
            RegCloseKey(hKey);
        }
        return std::string(guid);
    }

    static std::string GetMACAddress() {
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
        if (GetAdaptersInfo(info_buf, &buf_size) != ERROR_SUCCESS) return "NOMAC";

        for (auto* ai = info_buf; ai; ai = ai->Next) {
            if (ai->AddressLength == 6 && !is_vm_mac(ai->Address)) {
                char mac[32] = {};
                snprintf(mac, sizeof(mac), "%02X%02X%02X%02X%02X%02X",
                         ai->Address[0], ai->Address[1], ai->Address[2],
                         ai->Address[3], ai->Address[4], ai->Address[5]);
                return std::string(mac);
            }
        }
        return "NOMAC";
    }

    static std::string GetBIOSUUID() {
        HKEY hKey = nullptr;
        char bios_ver[64] = {}, bios_date[64] = {}, sys_manuf[64] = {};
        DWORD size = 64;

        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            RegQueryValueExA(hKey, "BIOSVersion", nullptr, nullptr, (LPBYTE)bios_ver, &size);
            size = 64;
            RegQueryValueExA(hKey, "BIOSReleaseDate", nullptr, nullptr, (LPBYTE)bios_date, &size);
            size = 64;
            RegQueryValueExA(hKey, "SystemManufacturer", nullptr, nullptr, (LPBYTE)sys_manuf, &size);
            RegCloseKey(hKey);
        }
        return std::string(bios_ver) + "|" + std::string(bios_date) + "|" + std::string(sys_manuf);
    }

    static std::string GetSystemProfile() {
        SYSTEM_INFO si = {};
        GetSystemInfo(&si);
        return std::to_string(si.dwNumberOfProcessors)
             + "_" + std::to_string(si.dwPageSize)
             + "_" + std::to_string(si.dwProcessorType);
    }

    inline std::string GenerateLocalHWID() {
        std::string raw;
        raw += GetCPUID()        + ":";
        raw += GetDiskSerial()   + ":";
        raw += GetMachineGuid()  + ":";
        raw += GetMACAddress()   + ":";
        raw += GetBIOSUUID()     + ":";
        raw += GetSystemProfile();
        return sha256_hex(raw);
    }
}
