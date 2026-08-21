#pragma once
#include "xorstr.hpp"
#include "driver_ctl.hpp"
#include "anti_forensics.hpp"

/*
 *  cleanup.hpp — Очистка следов через kernel driver (SHADOW_CODE_CLEANUP).
 *
 *  Ядро (Ring-0) выполняет:
 *    1. BAM  (Background Activity Moderator) — реестр
 *    2. USN Journal                          — ZwFsControlFile
 *    3. System Informer / Process Hacker     — ключи реестра
 *    4. DNS cache                            — NtSetSystemInformation(153)
 *    5. Event Logs                           — System/Application/Security .evtx
 *    6. Prefetch записи лоадера
 *
 *  Self-delete: после IOCTL запускается скрытый cmd.exe, который
 *  удаляет файл лоадера, затем вызывается ExitProcess(0).
 *
 *  ВАЖНО: CLEANUP_FLAG_SELF_DELETE намеренно НЕ передаётся в IOCTL.
 *  Если его отправить — ядро вызовет ZwTerminateProcess прямо во время
 *  обработки IRP, убивая лоадер до завершения остальной очистки.
 *
 *  Threading: запускается в detached thread из UI (CleanupAsync).
 */

#include <windows.h>
#include <atomic>
#include <thread>
#include <cstring>

namespace shadow_cleanup {
extern bool EnsureDriverForCleanup();

static std::atomic<bool>  g_cleanup_running { false };
static std::atomic<float> g_cleanup_progress{ 0.0f  };
static std::atomic<bool>  g_cleanup_done    { false  };

// ─── Основная точка входа ──────────────────────────────────────────────────
static void PerformCleanup() {
    if (g_cleanup_running.exchange(true)) return;
    struct Guard { ~Guard() { shadow_cleanup::g_cleanup_running.store(false); } } guard;

    g_cleanup_done.store(false);
    g_cleanup_progress.store(0.05f);

    // ─── Проход 1: юзермод-зачистка (не требует драйвера) ─────────────────
    // Реестр (BAM/UserAssist/MUICache/RecentDocs/ShellBags/MRU/JumpLists),
    // AmCache, DNS, USN journal (FSCTL), SRUM, crash dumps, собственный .pf.
    shadow_af::ActiveWipe();

    g_cleanup_progress.store(0.20f);

    // Убеждаемся что драйвер открыт (handshake уже должен был быть).
    // Если нет — пробуем скачать/смапить и выполнить handshake сами.
    if (!shadow_driver_ctl::OpenDriver()) {
        if (!EnsureDriverForCleanup()) {
            g_cleanup_progress.store(1.0f);
            g_cleanup_done.store(true);
            return;
        }
    }

    g_cleanup_progress.store(0.35f);

    // ─── IOCTL очистки (BAM, USN, DNS, EventLog, Prefetch) ─────────────────
    // Флаг CLEANUP_FLAG_SELF_DELETE намеренно исключён: если его передать,
    // ядро вызовет ZwTerminateProcess(loader_pid) прямо внутри обработчика IRP,
    // и лоадер умрёт до того как IOCTL вернёт управление — очистка прервётся.
    driver::CleanupRequest req{};
    req.session_key = shadow_driver_ctl::g_driver_session_key;
    req.request_id  = shadow_driver_ctl::next_request_id();
    req.flags       = driver::CLEANUP_FLAG_ALL & ~driver::CLEANUP_FLAG_SELF_DELETE;

    g_cleanup_progress.store(0.50f);

    DWORD bytes = 0;
    DeviceIoControl(
        shadow_driver_ctl::g_driver_handle,
        SHADOW_CODE_CLEANUP,
        &req, sizeof(req),
        nullptr, 0,
        &bytes, nullptr);

    SecureZeroMemory(&req, sizeof(req));

    g_cleanup_progress.store(0.85f);

    // ─── Самоудаление через user-mode ──────────────────────────────────────
    // Запускаем скрытый cmd.exe который удалит файл лоадера после нашего выхода.
    // ping 127.0.0.1 -n 3 = ~2 секунды паузы пока мы завершаемся.
    {
        wchar_t my_path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, my_path, MAX_PATH);

        wchar_t cmd_line[MAX_PATH + 80] = {};
        swprintf(cmd_line, ARRAYSIZE(cmd_line),
            L"cmd.exe /c ping 127.0.0.1 -n 3 >nul & del /f /q \"%s\"",
            my_path);

        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(nullptr, cmd_line,
                           nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW | DETACHED_PROCESS,
                           nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }

    g_cleanup_progress.store(1.0f);
    g_cleanup_done.store(true);

    // Выходим — через ~2 сек cmd.exe удалит наш .exe файл.
    ExitProcess(0);
}

// ─── Асинхронная обёртка для UI-bridge ────────────────────────────────────
static void CleanupAsync() {
    std::thread([] {
        PerformCleanup();
    }).detach();
}

} // namespace shadow_cleanup
