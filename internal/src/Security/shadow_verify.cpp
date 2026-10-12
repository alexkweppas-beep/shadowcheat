#include "shadow_verify.hpp"
#include "ida_cloak.hpp"
#include "../xorstr.hpp"
#include "../vmp_markers.hpp"
#include "../Features/CloudStorage.h"
#include <thread>
#include <chrono>

#pragma pack(push, 1)
struct ShadowSharedAuth {
  uint32_t magic;
  uint32_t version;
  uint64_t timestamp;
  char username[64];
  char token[512];
  char hwid[128];
  int32_t days_remaining;
  uint32_t pid;
};
#pragma pack(pop)

#define SHADOW_SHARED_MAGIC 0x53484457u

// ─────────────────────────────────────────────────────────────────────────────
// Real _impl functions — exported with C linkage so ida_cloak.asm can link
// them as EXTERN symbols. The public API (shadow_verify.hpp) routes through
// the MASM thunks, so IDA < 9.5 never decompiles these bodies.
// ─────────────────────────────────────────────────────────────────────────────
extern "C" {

void ShadowSecurity_VerifySharedMemory_impl() {
    SHADOW_PROTECT_VIRT("VerifySharedMemory");
    bool valid = false;

    HANDLE hMap = OpenFileMappingA(FILE_MAP_READ, FALSE, xorstr_("Local\\SHADOW_IPC_AUTH_SECTION"));
    if (hMap) {
        auto* auth = static_cast<ShadowSharedAuth*>(
            MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, sizeof(ShadowSharedAuth)));
        if (auth) {
            if (auth->magic == SHADOW_SHARED_MAGIC && auth->version == 1)
                valid = true;
            UnmapViewOfFile(auth);
        }
        CloseHandle(hMap);
    }

    if (!valid) {
        // Умышленный краш — shared memory отсутствует или сломана
        volatile int* p = nullptr;
        *p = 0xDEADC0DE;
    }
    SHADOW_PROTECT_END();
}

void ShadowSecurity_VerifyLoaderPidOnce_impl() {
    SHADOW_PROTECT_VIRT("VerifyLoaderPidOnce");

    uint32_t loaderPid = 0;
    HANDLE hMap = OpenFileMappingA(FILE_MAP_READ, FALSE, xorstr_("Local\\SHADOW_IPC_AUTH_SECTION"));
    if (hMap) {
        auto* auth = static_cast<ShadowSharedAuth*>(
            MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, sizeof(ShadowSharedAuth)));
        if (auth && auth->magic == SHADOW_SHARED_MAGIC)
            loaderPid = auth->pid;
        if (auth) UnmapViewOfFile(auth);
        CloseHandle(hMap);
    }

    if (loaderPid == 0) {
        // Shared memory невалидна или PID не записан -> краш
        volatile int* p = reinterpret_cast<volatile int*>(static_cast<uintptr_t>(0x10));
        *p = 1;
    } else {
        // Проверяем, что процесс лоадера жив
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, loaderPid);
        if (!hProc) {
            // Процесс уже убит -> краш
            volatile int* p = reinterpret_cast<volatile int*>(static_cast<uintptr_t>(0x20));
            *p = 1;
        } else {
            DWORD exitCode = 0;
            if (!GetExitCodeProcess(hProc, &exitCode) || exitCode != STILL_ACTIVE) {
                volatile int* p = reinterpret_cast<volatile int*>(static_cast<uintptr_t>(0x30));
                *p = 1;
            }
            CloseHandle(hProc);
        }
    }
    SHADOW_PROTECT_END();
}

void ShadowSecurity_StartPeriodicSubscriptionCheck_impl() {
    std::thread([]() {
        SHADOW_PROTECT_VIRT("ServerSubscriptionThread");

        // Первая проверка — сразу при запуске
        while (true) {
            if (!CloudStorage::VerifySubscription()) {
                // Сервер явно отклонил токен (HTTP 401/403)
                volatile int* p = reinterpret_cast<volatile int*>(static_cast<uintptr_t>(0x40));
                *p = 1;
            }
            // Следующая проверка — через 2 минуты
            std::this_thread::sleep_for(std::chrono::minutes(2));
        }

        SHADOW_PROTECT_END();
    }).detach();
}

} // extern "C"

// ─────────────────────────────────────────────────────────────────────────────
// Public API (namespace ShadowSecurity) — routes through the MASM thunks.
// ida_cloak.asm provides the actual symbols; we just declare them here so
// that callers in dllmain.cpp and Hooks.cpp link correctly.
// ─────────────────────────────────────────────────────────────────────────────
extern "C" {
    void ShadowSecurity_VerifySharedMemory();
    void ShadowSecurity_VerifyLoaderPidOnce();
    void ShadowSecurity_StartPeriodicSubscriptionCheck();
}

namespace ShadowSecurity {
    void VerifySharedMemory()              { ShadowSecurity_VerifySharedMemory(); }
    void VerifyLoaderPidOnce()             { ShadowSecurity_VerifyLoaderPidOnce(); }
    void StartPeriodicSubscriptionCheck()  { ShadowSecurity_StartPeriodicSubscriptionCheck(); }
}
