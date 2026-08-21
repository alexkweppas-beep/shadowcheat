#include "Pattern.h"
#include <Psapi.h>

namespace Memory
{
    static uint8_t HexToByte(const char* hex) {
        uint8_t val = 0;
        for (int i = 0; i < 2; ++i) {
            char c = hex[i];
            val <<= 4;
            if (c >= '0' && c <= '9') val |= (c - '0');
            else if (c >= 'A' && c <= 'F') val |= (c - 'A' + 10);
            else if (c >= 'a' && c <= 'f') val |= (c - 'a' + 10);
        }
        return val;
    }

    uint8_t* FindPattern(const char* moduleName, const char* pattern)
    {
        HMODULE hModule = GetModuleHandleA(moduleName);
        if (!hModule) return nullptr;

        MODULEINFO moduleInfo;
        if (!GetModuleInformation(GetCurrentProcess(), hModule, &moduleInfo, sizeof(moduleInfo)))
            return nullptr;

        uint8_t* baseAddress = (uint8_t*)moduleInfo.lpBaseOfDll;
        size_t moduleSize = moduleInfo.SizeOfImage;

        int patternBytes[128];
        int patternLength = 0;
        const char* current = pattern;

        while (*current && patternLength < 128)
        {
            if (*current == ' ') {
                current++;
                continue;
            }
            if (*current == '?') {
                patternBytes[patternLength++] = -1;
                current++;
                if (*current == '?') current++;
            } else {
                patternBytes[patternLength++] = HexToByte(current);
                current += 2;
            }
        }

        uint8_t* address = baseAddress;
        uint8_t* endAddress = baseAddress + moduleSize;
        MEMORY_BASIC_INFORMATION mbi;

        while (address < endAddress)
        {
            if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0)
                break;

            bool readable = (mbi.State == MEM_COMMIT) &&
                            (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY)) &&
                            !(mbi.Protect & PAGE_GUARD);

            if (readable)
            {
                uint8_t* regionEnd = (uint8_t*)mbi.BaseAddress + mbi.RegionSize;
                if (regionEnd > endAddress) regionEnd = endAddress;

                if (regionEnd > (uint8_t*)mbi.BaseAddress + patternLength) {
                    for (uint8_t* p = (uint8_t*)mbi.BaseAddress; p <= regionEnd - patternLength; ++p)
                    {
                        bool found = true;
                        for (int j = 0; j < patternLength; ++j)
                        {
                            if (patternBytes[j] != -1 && p[j] != (uint8_t)patternBytes[j])
                            {
                                found = false;
                                break;
                            }
                        }
                        if (found)
                            return p;
                    }
                }
            }
            
            address = (uint8_t*)mbi.BaseAddress + mbi.RegionSize;
            if (address <= (uint8_t*)mbi.BaseAddress) break;
        }

        return nullptr;
    }
}
