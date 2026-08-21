#pragma once
#include "../security/vmp_markers.hpp"
#include "ioctl_codes.hpp"
#include <atomic>
#include <functional>

namespace driver
{
    extern HANDLE   driver_handle;
    extern INT32    process_id;

    // Базовые операции
    bool      find_driver();
    INT32     find_process(LPCTSTR process_name);
    uintptr_t find_image();

    void read_physical_memory (PVOID address, PVOID buffer, DWORD size);
    void write_physical_memory(PVOID address, PVOID buffer, DWORD size);

    // Защита и управление
    bool do_handshake(uint64_t session_key, uint32_t loader_pid);
    bool heartbeat();
    bool set_target_pid(uint32_t target_pid);
    bool protect_self();

    // Скан подключений (кто пытается открыть хэндл к нашим процессам)
    bool get_access_log(AccessLogDumpRequest& out);

    // Скрытие процесса / затирание следов
    bool trim_working_set(uint32_t target_pid = 0);
    bool wipe_pe_header(uint32_t target_pid, uint64_t base_address, uint32_t header_size);

    // ─────────────────────────────────────────────────────────
    //  Protection Watchdog
    //  Фоновый поток: поллит access_log каждые poll_ms мс.
    //  При детекте опасного подключения — вызывает on_detect,
    //  затем self_destruct() (вайп памяти + выход).
    //
    //  Опасные права (триггер):
    //    PROCESS_VM_READ, PROCESS_VM_WRITE, PROCESS_VM_OPERATION,
    //    PROCESS_CREATE_THREAD
    //
    //  Использование:
    //    driver::start_protection_watchdog(2000,
    //        []{ /* опционально: вайп своих данных */ });
    // ─────────────────────────────────────────────────────────
    void start_protection_watchdog(uint32_t poll_ms = 2000,
                                   std::function<void()> on_detect = nullptr);
    void stop_protection_watchdog();

    // Немедленное самоуничтожение (вайп key + handle + ExitProcess)
    void self_destruct();
}
