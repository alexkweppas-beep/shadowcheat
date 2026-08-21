#pragma once
// streamproof.hpp — NVIDIA ShadowPlay / GeForce Experience / OBS Streamproof
//
// 1. FTS Registry Keys — форсируют NVIDIA ShadowPlay в режим DXGI Desktop Duplication.
//    В этом режиме окна с WDA_EXCLUDEFROMCAPTURE становятся ПОЛНОСТЬЮ невидимыми (прозрачными).
//
// 2. Service/Process Restart — перезапускает процессы NVIDIA (nvcontainer.exe, NVIDIA Share.exe и др.),
//    чтобы они перечитали реестр и запустились в DXGI-режиме.
//
// 3. WDA_EXCLUDEFROMCAPTURE (0x11) — скрывает окно из DWM захвата (DXGI, OBS, Discord, Xbox DVR).
//
// 4. Патч GetWindowDisplayAffinity — обходит проверку защищённых окон в процессах NVIDIA.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <winsvc.h>
#include <cwchar>
#include <thread>
#include <chrono>
#include <atomic>
#include "xorstr.hpp"

namespace shadow_streamproof {

// ─── 0. Включение SeDebugPrivilege ──────────────────────────────────────────
static inline bool EnableDebugPrivilege() {
    HANDLE hToken = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;

    TOKEN_PRIVILEGES tp = {};
    LUID luid = {};
    if (::LookupPrivilegeValueA(nullptr, xorstr_("SeDebugPrivilege"), &luid)) {
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        ::AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    }
    ::CloseHandle(hToken);
    return true;
}

// ─── 1. WDA_EXCLUDEFROMCAPTURE ───────────────────────────────────────────────
static inline bool HideWindowFromCapture(HWND hwnd) {
    if (!hwnd || !::IsWindow(hwnd)) return false;
    // WDA_EXCLUDEFROMCAPTURE = 0x00000011 (Windows 10 2004+ / Windows 11)
    BOOL ok = ::SetWindowDisplayAffinity(hwnd, 0x00000011u);
    if (!ok) {
        ok = ::SetWindowDisplayAffinity(hwnd, 0x00000001u); // fallback WDA_MONITOR
    }
    return ok != FALSE;
}

static inline void ShowWindowToCapture(HWND hwnd) {
    if (hwnd && ::IsWindow(hwnd)) {
        ::SetWindowDisplayAffinity(hwnd, 0x00000000u); // WDA_NONE
    }
}

// ─── 2. Полная запись FTS-ключей во все разделы реестра NVIDIA ───────────────
// The value is written to 15 locations and was never restored, so it outlived
// the loader. Back up each location's prior state and restore it on exit.
struct FtsBackup {
    bool existed = false;
    DWORD prior = 0;
};
static FtsBackup g_ftsBackup[15];
static bool g_ftsBackedUp = false;

static inline bool ApplyNvidiaFtsRegistry() {
    DWORD val = 0x24; // 36 decimal — DXGI Desktop Duplication mode
    bool any_success = false;
    int backupIndex = 0;

    const char* paths[] = {
        xorstr_("SOFTWARE\\NVIDIA Corporation\\Global\\NvApp\\ShadowPlay\\FTS"),
        xorstr_("SOFTWARE\\NVIDIA Corporation\\Global\\ShadowPlay\\FTS"),
        xorstr_("SOFTWARE\\NVIDIA Corporation\\Global\\ShadowPlay"),
        xorstr_("SOFTWARE\\NVIDIA Corporation\\Global\\NvApp\\FTS"),
        xorstr_("SOFTWARE\\NVIDIA Corporation\\Global\\FTS"),
    };

    const HKEY hives[3] = {
        HKEY_LOCAL_MACHINE, HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER
    };
    const DWORD hiveAccess[3] = {
        KEY_WOW64_64KEY, KEY_WOW64_32KEY, 0
    };

    for (int hive = 0; hive < 3; ++hive) {
        for (const auto* path : paths) {
            if (backupIndex >= 15) break;
            FtsBackup& backup = g_ftsBackup[backupIndex++];

            // Read the prior value before overwriting so exit can restore it.
            HKEY hRead = nullptr;
            if (::RegOpenKeyExA(
                    hives[hive], path, 0, KEY_READ | hiveAccess[hive],
                    &hRead) == ERROR_SUCCESS) {
                DWORD prior = 0;
                DWORD sz = sizeof(prior);
                DWORD type = 0;
                if (::RegQueryValueExA(
                        hRead,
                        xorstr_("{497B8458-4244-4EE6-BFEA-F3D28A294F21}"),
                        nullptr, &type, reinterpret_cast<BYTE*>(&prior),
                        &sz) == ERROR_SUCCESS && type == REG_DWORD) {
                    backup.existed = true;
                    backup.prior = prior;
                }
                ::RegCloseKey(hRead);
            }

            HKEY hKey = nullptr;
            LONG res = ::RegCreateKeyExA(
                hives[hive], path, 0, nullptr, REG_OPTION_NON_VOLATILE,
                KEY_SET_VALUE | KEY_CREATE_SUB_KEY | hiveAccess[hive],
                nullptr, &hKey, nullptr);
            if (res == ERROR_SUCCESS) {
                ::RegSetValueExA(hKey,
                    xorstr_("{497B8458-4244-4EE6-BFEA-F3D28A294F21}"),
                    0, REG_DWORD, reinterpret_cast<const BYTE*>(&val),
                    sizeof(val));
                ::RegCloseKey(hKey);
                any_success = true;
            }
        }
    }
    g_ftsBackedUp = true;

    return any_success;
}

// Restore every FTS location to what it held before ApplyNvidiaFtsRegistry.
static inline void RestoreNvidiaFtsRegistry() {
    if (!g_ftsBackedUp) return;
    const char* paths[] = {
        xorstr_("SOFTWARE\\NVIDIA Corporation\\Global\\NvApp\\ShadowPlay\\FTS"),
        xorstr_("SOFTWARE\\NVIDIA Corporation\\Global\\ShadowPlay\\FTS"),
        xorstr_("SOFTWARE\\NVIDIA Corporation\\Global\\ShadowPlay"),
        xorstr_("SOFTWARE\\NVIDIA Corporation\\Global\\NvApp\\FTS"),
        xorstr_("SOFTWARE\\NVIDIA Corporation\\Global\\FTS"),
    };
    const HKEY hives[3] = {
        HKEY_LOCAL_MACHINE, HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER
    };
    const DWORD hiveAccess[3] = {
        KEY_WOW64_64KEY, KEY_WOW64_32KEY, 0
    };
    int backupIndex = 0;
    for (int hive = 0; hive < 3; ++hive) {
        for (const auto* path : paths) {
            if (backupIndex >= 15) break;
            const FtsBackup& backup = g_ftsBackup[backupIndex++];
            HKEY hKey = nullptr;
            if (::RegOpenKeyExA(
                    hives[hive], path, 0, KEY_SET_VALUE | hiveAccess[hive],
                    &hKey) != ERROR_SUCCESS)
                continue;
            if (backup.existed) {
                ::RegSetValueExA(hKey,
                    xorstr_("{497B8458-4244-4EE6-BFEA-F3D28A294F21}"),
                    0, REG_DWORD,
                    reinterpret_cast<const BYTE*>(&backup.prior),
                    sizeof(DWORD));
            } else {
                // It was not there before us, so remove the value we added.
                ::RegDeleteValueA(hKey,
                    xorstr_("{497B8458-4244-4EE6-BFEA-F3D28A294F21}"));
            }
            ::RegCloseKey(hKey);
        }
    }
    g_ftsBackedUp = false;
}

// ─── 3. Перезапуск процессов захвата NVIDIA ─────────────────────────────────
static inline void KillNvidiaProcesses() {
    EnableDebugPrivilege();

    const wchar_t* targetExes[] = {
        L"nvcontainer.exe",
        L"NVIDIA Share.exe",
        L"NVIDIA App.exe",
        L"NvCamera64.exe",
        L"nvsphelper64.exe",
        L"nvspcaps64.exe"
    };

    HANDLE hSnap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (::Process32FirstW(hSnap, &pe)) {
        do {
            for (const auto* exe : targetExes) {
                if (_wcsicmp(pe.szExeFile, exe) == 0) {
                    HANDLE hProc = ::OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProc) {
                        ::TerminateProcess(hProc, 0);
                        ::CloseHandle(hProc);
                    }
                    break;
                }
            }
        } while (::Process32NextW(hSnap, &pe));
    }
    ::CloseHandle(hSnap);
}

// ─── 4. Патч GetWindowDisplayAffinity во всех процессах NVIDIA ───────────────
static inline bool PatchAllNvidiaProcesses() {
    EnableDebugPrivilege();

    HMODULE hU32 = ::GetModuleHandleA(xorstr_("user32.dll"));
    if (!hU32) return false;

    auto* pTarget = reinterpret_cast<BYTE*>(
        ::GetProcAddress(hU32, xorstr_("GetWindowDisplayAffinity")));
    if (!pTarget) return false;

    // Патч (14 байт):
    //   xor eax, eax             ; 31 C0
    //   test rdx, rdx            ; 48 85 D2
    //   jz skip                  ; 74 06
    //   mov dword ptr [rdx], 0   ; C7 02 00 00 00 00
    // skip:
    //   ret                      ; C3
    const BYTE patch[] = {
        0x31, 0xC0,
        0x48, 0x85, 0xD2,
        0x74, 0x06,
        0xC7, 0x02, 0x00, 0x00, 0x00, 0x00,
        0xC3
    };

    const wchar_t* targetExes[] = {
        L"nvcontainer.exe",
        L"NVIDIA Share.exe",
        L"NVIDIA App.exe",
        L"NvCamera64.exe",
        L"nvsphelper64.exe",
        L"nvspcaps64.exe"
    };

    HANDLE hSnap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    bool ok = false;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (::Process32FirstW(hSnap, &pe)) {
        do {
            bool isTarget = false;
            for (const auto* exe : targetExes) {
                if (_wcsicmp(pe.szExeFile, exe) == 0) {
                    isTarget = true;
                    break;
                }
            }
            if (!isTarget) continue;

            HANDLE hProc = ::OpenProcess(
                PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION,
                FALSE, pe.th32ProcessID);
            if (!hProc) continue;

            DWORD oldProt = 0;
            if (::VirtualProtectEx(hProc, pTarget, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProt)) {
                SIZE_T written = 0;
                if (::WriteProcessMemory(hProc, pTarget, patch, sizeof(patch), &written) && written == sizeof(patch)) {
                    ok = true;
                }
                ::VirtualProtectEx(hProc, pTarget, sizeof(patch), oldProt, &oldProt);
            }
            ::CloseHandle(hProc);
        } while (::Process32NextW(hSnap, &pe));
    }
    ::CloseHandle(hSnap);
    return ok;
}

// ─── 5. Включение Streamproof ────────────────────────────────────────────────
static inline void EnableStreamproof(HWND hwnd) {
    // 1. Применяем WDA_EXCLUDEFROMCAPTURE немедленно
    HideWindowFromCapture(hwnd);

    // 2. Записываем FTS-ключи во все ветки реестра
    ApplyNvidiaFtsRegistry();

    // 3. Патчим все уже работающие процессы NVIDIA
    PatchAllNvidiaProcesses();

    // 4. В фоне перезапускаем процессы NVIDIA чтобы они гарантированно загрузили FTS-ключ
    std::thread([hwnd]() {
        KillNvidiaProcesses();
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));

        // Повторно применяем WDA и патчим перезапущенные процессы
        if (hwnd && ::IsWindow(hwnd))
            HideWindowFromCapture(hwnd);
        PatchAllNvidiaProcesses();
    }).detach();

    // 5. Фоновый вотчер: поддерживает WDA и патчит новые процессы каждые 2 сек
    static std::atomic<bool> s_watcher{false};
    if (!s_watcher.exchange(true)) {
        std::thread([hwnd]() {
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                PatchAllNvidiaProcesses();
                if (hwnd && ::IsWindow(hwnd)) {
                    DWORD aff = 0;
                    if (::GetWindowDisplayAffinity(hwnd, &aff) && aff == 0) {
                        HideWindowFromCapture(hwnd);
                    }
                }
            }
        }).detach();
    }
}

// ─── 6. Выключение Streamproof ───────────────────────────────────────────────
static inline void DisableStreamproof(HWND hwnd) {
    if (hwnd) ShowWindowToCapture(hwnd);
}

// Совместимость
static inline void ApplyAll(HWND hwnd = nullptr) {
    ApplyNvidiaFtsRegistry();
    if (hwnd) HideWindowFromCapture(hwnd);
    PatchAllNvidiaProcesses();
}

static inline bool PatchNvContainerGetWindowAffinity() {
    return PatchAllNvidiaProcesses();
}

} // namespace shadow_streamproof
