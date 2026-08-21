#pragma once
#include <windows.h>
#include <cstdint>

#pragma pack(push, 1)
struct ShadowSharedAuth {
    uint32_t magic;            // 0x53484457 ("SHDW")
    uint32_t version;          // 1
    uint64_t timestamp;        // GetTickCount64()
    char     username[64];     // Login username
    char     token[512];       // JWT Token / Session Token
    char     hwid[128];        // User HWID
    int32_t  days_remaining;   // Subscription days
    uint32_t pid;              // Loader PID
};
#pragma pack(pop)

#define SHADOW_SHARED_MEMORY_NAME "Local\\SHADOW_IPC_AUTH_SECTION"
#define SHADOW_SHARED_MAGIC       0x53484457

inline bool ReadSharedAuth(ShadowSharedAuth& out) {
    HANDLE hMap = OpenFileMappingA(FILE_MAP_READ, FALSE, SHADOW_SHARED_MEMORY_NAME);
    if (!hMap) return false;
    void* pView = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, sizeof(ShadowSharedAuth));
    if (!pView) {
        CloseHandle(hMap);
        return false;
    }
    memcpy(&out, pView, sizeof(ShadowSharedAuth));
    UnmapViewOfFile(pView);
    CloseHandle(hMap);

    if (out.magic != SHADOW_SHARED_MAGIC) return false;
    if (out.username[0] == '\0' && out.token[0] == '\0' && out.hwid[0] == '\0') return false;
    return true;
}
