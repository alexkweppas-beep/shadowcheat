#include "security/xorstr.hpp"
#include "driver_io.hpp"
#include <TlHelp32.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>

namespace driver
{
    HANDLE    driver_handle       = nullptr;
    INT32     process_id          = 0;

    // ─── Watchdog state ───
    static std::atomic<bool> s_watchdog_running{ false };
    static std::thread       s_watchdog_thread;

    // session_key — loader патчит inline перед inject, заменяя sentinel на реальный ключ.
    // Без патча driver VerifySession отклонит любой запрос с неправильным ключом.
    //
    // ВАЖНО: volatile + #pragma optimize off гарантируют что MSVC сохранит
    // sentinel 0xFEEDFACECAFEBABE как literal в секции .data (raw bytes).
    // Без этого компилятор может инициализировать переменную через CRT startup
    // код (т.е. в .data будет 0x0, а не sentinel) — и лоадер не найдёт паттерн.
#pragma optimize("", off)
    volatile uint64_t g_cheat_session_key = 0xFEEDFACECAFEBABEULL;
#pragma optimize("", on)
    uint32_t  g_cheat_request_id  = 0;

    static uint32_t next_request_id() {
        return ++g_cheat_request_id;
    }

    // ─────────────────────────────────────────────────────────
    //  find_driver — открыть хэндл устройства драйвера
    // ─────────────────────────────────────────────────────────
    bool find_driver()
    {
        char dev_path[32] = {};
        shadow_format_device_path(dev_path, sizeof(dev_path));

        driver_handle = CreateFileA(
            dev_path,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, 0, nullptr);

        if (!driver_handle || driver_handle == INVALID_HANDLE_VALUE)
        {
            std::cout << xorstr_("[driver] Failed to open device ") << dev_path
                      << xorstr_(". Is the driver loaded?") << std::endl;
            return false;
        }
        return true;
    }

    // ─────────────────────────────────────────────────────────
    //  find_process — найти PID по имени процесса
    // ─────────────────────────────────────────────────────────
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
                    process_id = pt.th32ProcessID;
                    return process_id;
                }
            } while (Process32Next(hSnap, &pt));
        }
        CloseHandle(hSnap);
        return 0;
    }

    // ─────────────────────────────────────────────────────────
    //  find_image — получить image base через driver
    // ─────────────────────────────────────────────────────────
    uintptr_t find_image()
    {
        if (!driver_handle || driver_handle == INVALID_HANDLE_VALUE) return 0;

        uintptr_t image_address = 0;
        ImageRequest req{};
        req.security_code = SHADOW_SECURITY_CODE;
        req.process_id    = process_id;
        req.address       = reinterpret_cast<uint64_t*>(&image_address);

        DeviceIoControl(driver_handle, SHADOW_CODE_IMAGE,
                        &req, sizeof(req), nullptr, 0, nullptr, nullptr);
        return image_address;
    }

    // ─────────────────────────────────────────────────────────
    //  physical_op — внутренняя функция read/write physical
    // ─────────────────────────────────────────────────────────
    static void physical_op(PVOID address, PVOID buffer, DWORD size, int32_t mode)
    {
        if (!driver_handle || driver_handle == INVALID_HANDLE_VALUE) return;

        PhysicalRequest req{};
        req.security_code  = SHADOW_SECURITY_CODE;
        req.process_id     = process_id;
        req.physical_mode  = mode;
        req._pad           = 0;
        req.address        = reinterpret_cast<uint64_t>(address);
        req.buffer         = reinterpret_cast<uint64_t>(buffer);
        req.size           = size;

        DeviceIoControl(driver_handle, SHADOW_CODE_PHYSICAL,
                        &req, sizeof(req), nullptr, 0, nullptr, nullptr);
    }

    void read_physical_memory (PVOID a, PVOID b, DWORD s) { physical_op(a, b, s, SHADOW_PHYSICAL_READ);  }
    void write_physical_memory(PVOID a, PVOID b, DWORD s) { physical_op(a, b, s, SHADOW_PHYSICAL_WRITE); }

    // ─────────────────────────────────────────────────────────
    //  do_handshake — установить session_key, активировать защиту
    // ─────────────────────────────────────────────────────────
    bool do_handshake(uint64_t session_key, uint32_t loader_pid)
    {
        if (!driver_handle || driver_handle == INVALID_HANDLE_VALUE) return false;

        HandshakeRequest req{};
        req.session_key = session_key;
        req.loader_pid  = loader_pid;
        req.build_seed  = 0;

        // Сохраняем ключ локально
        g_cheat_session_key = (volatile uint64_t)session_key;
        g_cheat_request_id  = 0;

        DWORD bytes = 0;
        return DeviceIoControl(driver_handle, SHADOW_CODE_HANDSHAKE,
                               &req, sizeof(req), nullptr, 0, &bytes, nullptr) == TRUE;
    }

    // ─────────────────────────────────────────────────────────
    //  heartbeat — проверка что драйвер жив
    // ─────────────────────────────────────────────────────────
    bool heartbeat()
    {
        if (!driver_handle || driver_handle == INVALID_HANDLE_VALUE) return false;

        HeartbeatRequest req{};
        req.session_key = g_cheat_session_key;
        req.request_id  = next_request_id();
        req._pad        = 0;

        DWORD bytes = 0;
        return DeviceIoControl(driver_handle, SHADOW_CODE_HEARTBEAT,
                               &req, sizeof(req), nullptr, 0, &bytes, nullptr) == TRUE;
    }

    // ─────────────────────────────────────────────────────────
    //  set_target_pid — сообщить драйверу PID игры (game process)
    // ─────────────────────────────────────────────────────────
    bool set_target_pid(uint32_t target_pid)
    {
        if (!driver_handle || driver_handle == INVALID_HANDLE_VALUE) return false;

        SetTargetRequest req{};
        req.session_key = g_cheat_session_key;
        req.request_id  = next_request_id();
        req.target_pid  = target_pid;

        DWORD bytes = 0;
        return DeviceIoControl(driver_handle, SHADOW_CODE_SET_TARGET,
                               &req, sizeof(req), nullptr, 0, &bytes, nullptr) == TRUE;
    }

    // ─────────────────────────────────────────────────────────
    //  protect_self — поставить текущий процесс под защиту ObCallback
    // ─────────────────────────────────────────────────────────
    bool protect_self()
    {
        if (!driver_handle || driver_handle == INVALID_HANDLE_VALUE) return false;

        ProtectProcessRequest req{};
        req.session_key   = g_cheat_session_key;
        req.request_id    = next_request_id();
        req.target_pid    = GetCurrentProcessId();
        req.protect_flags = 0x3;  // 0x1 (no dump) | 0x2 (no open)
        req._pad          = 0;

        DWORD bytes = 0;
        return DeviceIoControl(driver_handle, SHADOW_CODE_PROTECT_PROCESS,
                               &req, sizeof(req), nullptr, 0, &bytes, nullptr) == TRUE;
    }

    // ─────────────────────────────────────────────────────────
    //  get_access_log — читает кто пытался подключиться к нашим процессам
    // ─────────────────────────────────────────────────────────
    bool get_access_log(AccessLogDumpRequest& out)
    {
        if (!driver_handle || driver_handle == INVALID_HANDLE_VALUE) return false;

        out.session_key = g_cheat_session_key;
        out.request_id  = next_request_id();
        out.count       = 0;

        DWORD bytes = 0;
        return DeviceIoControl(driver_handle, SHADOW_CODE_ACCESS_LOG,
                               &out, sizeof(out), nullptr, 0, &bytes, nullptr) == TRUE;
    }

    // ─────────────────────────────────────────────────────────
    //  trim_working_set — скрыть процесс из памяти в диспетчере задач
    //  target_pid = 0 → лоадер (по умолчанию)
    // ─────────────────────────────────────────────────────────
    bool trim_working_set(uint32_t target_pid)
    {
        if (!driver_handle || driver_handle == INVALID_HANDLE_VALUE) return false;

        TrimWsRequest req{};
        req.session_key = g_cheat_session_key;
        req.request_id  = next_request_id();
        req.target_pid  = target_pid;

        DWORD bytes = 0;
        return DeviceIoControl(driver_handle, SHADOW_CODE_TRIM_WS,
                               &req, sizeof(req), nullptr, 0, &bytes, nullptr) == TRUE;
    }

    // ─────────────────────────────────────────────────────────
    //  wipe_pe_header — затереть PE-заголовок маппинга через ядро
    // ─────────────────────────────────────────────────────────
    bool wipe_pe_header(uint32_t target_pid, uint64_t base_address, uint32_t header_size)
    {
        if (!driver_handle || driver_handle == INVALID_HANDLE_VALUE) return false;

        WipeHeaderRequest req{};
        req.session_key  = g_cheat_session_key;
        req.request_id   = next_request_id();
        req.target_pid   = target_pid;
        req.base_address = base_address;
        req.header_size  = header_size;
        req._pad         = 0;

        DWORD bytes = 0;
        return DeviceIoControl(driver_handle, SHADOW_CODE_WIPE_HEADER,
                               &req, sizeof(req), nullptr, 0, &bytes, nullptr) == TRUE;
    }

    // ─────────────────────────────────────────────────────────
    //  self_destruct — немедленное самоуничтожение
    //  1. Зануляем session_key (драйвер перестанет принимать пакеты)
    //  2. Закрываем handle устройства
    //  3. TerminateProcess — без flush буферов, без деструкторов
    // ─────────────────────────────────────────────────────────
    void self_destruct()
    {
        SHADOW_PROTECT_MUT("self_destruct");
        g_cheat_session_key = (volatile uint64_t)0;
        g_cheat_request_id  = 0;

        if (driver_handle && driver_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(driver_handle);
            driver_handle = nullptr;
        }

        TerminateProcess(GetCurrentProcess(), 0);
        SHADOW_PROTECT_END();
    }

    // ─────────────────────────────────────────────────────────
    //  watchdog_loop — тело фонового потока
    // ─────────────────────────────────────────────────────────
    static void watchdog_loop(uint32_t poll_ms, std::function<void()> on_detect)
    {
        // Маска опасных прав — триггер на самоуничтожение
        constexpr uint32_t DANGEROUS_ACCESS =
            0x0010 |  // PROCESS_VM_READ
            0x0020 |  // PROCESS_VM_WRITE
            0x0008 |  // PROCESS_VM_OPERATION
            0x0002;   // PROCESS_CREATE_THREAD

        while (s_watchdog_running.load(std::memory_order_relaxed))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));

            if (!s_watchdog_running.load(std::memory_order_relaxed))
                break;

            // Запрашиваем лог подключений у драйвера
            AccessLogDumpRequest log{};
            if (!get_access_log(log) || log.count == 0)
                continue;

            bool danger = false;
            for (uint32_t i = 0; i < log.count && i < ACCESS_LOG_SIZE; ++i)
            {
                const AccessEvent& ev = log.events[i];

                // System (PID 4) — Windows kernel, игнорируем
                if (ev.requester_pid == 4)
                    continue;

                if (ev.desired_access & DANGEROUS_ACCESS)
                {
                    danger = true;
                    break;
                }
            }

            if (danger)
            {
                s_watchdog_running.store(false, std::memory_order_relaxed);

                // Пользовательский callback: вайп данных, зачистка памяти и т.п.
                if (on_detect)
                {
                    try { on_detect(); } catch (...) {}
                }

                // Самоуничтожение
                self_destruct();
                return;
            }
        }
    }

    // ─────────────────────────────────────────────────────────
    //  start_protection_watchdog — запуск фонового потока
    // ─────────────────────────────────────────────────────────
    void start_protection_watchdog(uint32_t poll_ms,
                                   std::function<void()> on_detect)
    {
        if (s_watchdog_running.load())
            return;  // уже запущен

        s_watchdog_running.store(true, std::memory_order_relaxed);
        s_watchdog_thread = std::thread(watchdog_loop, poll_ms, on_detect);
        s_watchdog_thread.detach();  // живёт до self_destruct()
    }

    // ─────────────────────────────────────────────────────────
    //  stop_protection_watchdog — штатная остановка
    // ─────────────────────────────────────────────────────────
    void stop_protection_watchdog()
    {
        s_watchdog_running.store(false, std::memory_order_relaxed);
        // Поток detach'нут — завершится сам на следующей итерации sleep
    }
}


