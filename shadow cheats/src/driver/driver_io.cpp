#include "security/xorstr.hpp"
#include "driver_io.hpp"
#include <TlHelp32.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <vector>

#pragma comment(lib, "ntdll.lib")

 
//  Usermode memory backend (no kernel driver).
//
//  The external cheat reads GTA5 from a separate process. That used to go
//  through a vulnerable-driver physical-memory primitive, which forced users
//  to disable HVCI and the Microsoft vulnerable-driver blocklist. This backend
//  reads and writes the game's virtual memory directly with
//  ReadProcessMemory / WriteProcessMemory on a process handle, so no driver is
//  loaded and nothing has to be disabled.
//
//  Trade-off, stated plainly: a usermode handle + RPM is more visible to an
//  anti-cheat than physical access was. The product gains "no setup" in
//  exchange for a larger detection surface.
//
//  The public interface (driver::read_physical_memory / write_physical_memory /
//  find_process / find_image) is unchanged so entity_list, esp, aimbot and
//  trigger keep working.
 

namespace driver
{
    INT32     process_id          = 0;

    // Kept for the header's extern; the driver device handle is gone, the game
    // process handle below is what memory operations use now.
    HANDLE    driver_handle       = nullptr;

    // The game process handle replaces the driver device handle.
    static HANDLE g_process_handle = nullptr;

    static HANDLE game_handle()
    {
        if (g_process_handle && g_process_handle != INVALID_HANDLE_VALUE)
            return g_process_handle;
        if (!process_id)
            return nullptr;
        g_process_handle = OpenProcess(
            PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION |
                PROCESS_QUERY_INFORMATION,
            FALSE, static_cast<DWORD>(process_id));
        return g_process_handle;
    }

    // Kept for interface compatibility. With no driver there is nothing to
    // open here; the game handle is opened lazily once the pid is known.
    bool find_driver()
    {
        return true;
    }

    // ─── find_process — найти PID по имени процесса (usermode) ───
    INT32 find_process(LPCTSTR process_name)
    {
        PROCESSENTRY32 pt{};
        pt.dwSize = sizeof(PROCESSENTRY32);

        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap == INVALID_HANDLE_VALUE) return 0;

        if (Process32First(hSnap, &pt))
        {
            do {
                if (!lstrcmpi(pt.szExeFile, process_name))
                {
                    CloseHandle(hSnap);
                    // A new target invalidates any handle opened for the old one.
                    if (process_id != (INT32)pt.th32ProcessID && g_process_handle)
                    {
                        CloseHandle(g_process_handle);
                        g_process_handle = nullptr;
                    }
                    process_id = pt.th32ProcessID;
                    return process_id;
                }
            } while (Process32Next(hSnap, &pt));
        }
        CloseHandle(hSnap);
        return 0;
    }

    // winternl.h declares this but conflicts with other headers here, so the
    // layout is spelled out. PebBaseAddress is the field we read.
    struct ProcessBasicInfo {
        PVOID Reserved1;
        PVOID PebBaseAddress;
        PVOID Reserved2[2];
        ULONG_PTR UniqueProcessId;
        PVOID Reserved3;
    };

    // ─── find_image — image base через PEB (usermode) ───
    uintptr_t find_image()
    {
        HANDLE h = game_handle();
        if (!h) return 0;

        typedef NTSTATUS(WINAPI* fn_NtQIP)(HANDLE, int, PVOID, ULONG, PULONG);
        static fn_NtQIP NtQIP = reinterpret_cast<fn_NtQIP>(
            GetProcAddress(GetModuleHandleA("ntdll.dll"),
                           "NtQueryInformationProcess"));
        if (!NtQIP) return 0;

        ProcessBasicInfo pbi{};
        if (NtQIP(h, 0, &pbi, sizeof(pbi), nullptr) != 0 || !pbi.PebBaseAddress)
            return 0;

        // PEB.ImageBaseAddress is at offset 0x10 on x64.
        uintptr_t base = 0;
        SIZE_T read = 0;
        if (!ReadProcessMemory(
                h,
                reinterpret_cast<LPCVOID>(
                    reinterpret_cast<uintptr_t>(pbi.PebBaseAddress) + 0x10),
                &base, sizeof(base), &read) || read != sizeof(base))
            return 0;
        return base;
    }

    // ─── read/write — виртуальная память игры через процесс-хэндл ───
    void read_physical_memory(PVOID address, PVOID buffer, DWORD size)
    {
        HANDLE h = game_handle();
        if (!h) return;
        SIZE_T read = 0;
        ReadProcessMemory(h, address, buffer, size, &read);
    }

    void write_physical_memory(PVOID address, PVOID buffer, DWORD size)
    {
        HANDLE h = game_handle();
        if (!h) return;
        SIZE_T written = 0;
        WriteProcessMemory(h, address, buffer, size, &written);
    }

     
    //  Driver-only management features. With no driver loaded there is nothing
    //  to handshake with, so these report unavailable instead of pretending.
     
    bool do_handshake(uint64_t, uint32_t) { return false; }
    bool heartbeat() { return g_process_handle != nullptr; }
    bool set_target_pid(uint32_t) { return true; }
    bool protect_self() { return false; }
    bool get_access_log(AccessLogDumpRequest& out) { out.count = 0; return false; }
    bool trim_working_set(uint32_t) { return false; }

    // wipe_pe_header is expressible in usermode: zero the region ourselves.
    bool wipe_pe_header(uint32_t target_pid, uint64_t base_address, uint32_t header_size)
    {
        HANDLE h = nullptr;
        if (target_pid == 0 || target_pid == (uint32_t)process_id)
        {
            h = game_handle();
        }
        else
        {
            h = OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, target_pid);
        }
        if (!h) return false;
        std::vector<uint8_t> zeros(header_size, 0);
        SIZE_T written = 0;
        const bool ok = WriteProcessMemory(
            h, reinterpret_cast<PVOID>(base_address), zeros.data(),
            header_size, &written) != FALSE;
        if (h != g_process_handle)
            CloseHandle(h);
        return ok;
    }

    // ─── self_destruct — немедленное самоуничтожение (usermode) ───
    void self_destruct()
    {
        SHADOW_PROTECT_MUT("self_destruct");
        if (g_process_handle && g_process_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(g_process_handle);
            g_process_handle = nullptr;
        }
        TerminateProcess(GetCurrentProcess(), 0);
        SHADOW_PROTECT_END();
    }

    // ─── Protection watchdog — needs the driver's access log, which does not
    // exist without a driver. Report unavailable rather than fake-watch. ───
    void start_protection_watchdog(uint32_t, std::function<void()>) {}
    void stop_protection_watchdog() {}
}
