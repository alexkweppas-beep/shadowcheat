#pragma once
#include "xorstr.hpp"
#include "driver_ctl.hpp"
#include "anti_forensics.hpp"

/*
 *  cleanup.hpp — Kernel-first зачистка следов.
 *
 *  Архитектура: очистка идёт через ядро (PdFwKrnl → kdmapper → cleanup-драйвер,
 *  SHADOW_CODE_CLEANUP). Ядро (Ring-0) выполняет:
 *    1. BAM  (Background Activity Moderator) — реестр
 *    2. USN Journal                          — ZwFsControlFile
 *    3. System Informer / Process Hacker     — ключи реестра
 *    4. DNS cache                            — NtSetSystemInformation(153)
 *    5. Event Logs                           — System/Application/Security .evtx
 *    6. Prefetch записи лоадера
 *
 *  Юзермод делает минимум и только то, чего текущий бинарь драйвера не
 *  покрывает (AmCache, SRUM, UserAssist, MUICache, RecentDocs, ShellBags,
 *  Explorer MRU, JumpLists, Network List, crash dumps, соседние копии лоадера,
 *  собственный .pf) — это RunUsermodeTail(). Чтобы убрать и его, расширение
 *  должно жить в драйвере (его исходник — серверный проект «фикс», сюда не
 *  входит).
 *
 *  Порядок важен:
 *    1. RunKernelCleanup()  — ядро сносит свою часть;
 *    2. RunUsermodeTail()   — юзермод добивает непокрытое (SRUM — пока DPS ещё
 *                             остановлен гардом);
 *    3. shadow_af::Shutdown() — откатывает превентивные изменения (службы,
 *                             политики реестра), чтобы они не остались следа
 *                             «мы что-то меняли»;
 *    4. SpawnSelfDelete() + ExitProcess — exe удаляется после нашей смерти.
 *
 *  ВАЖНО: CLEANUP_FLAG_SELF_DELETE намеренно НЕ передаётся в IOCTL.
 *  Если его отправить — ядро вызовет ZwTerminateProcess прямо во время
 *  обработки IRP, убивая лоадер до завершения остальной очистки.
 *
 *  Threading: запускается в detached thread из UI (CleanupAsync) или
 *  автоматически после инжекта internal (AutoCleanupAfterInjectAsync).
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

// ─── Самоудаление exe через скрытый cmd.exe ────────────────────────────────
// ping 127.0.0.1 -n 3 = ~2 секунды паузы пока мы завершаемся.
static void SpawnSelfDelete() {
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

// ─── Проход 1: ядерная зачистка (PdFwKrnl → cleanup-драйвер) ───────────────
// Грузим драйвер по требованию, шлем IOCTL, возвращаем true если ядро
// отработало. При неудаче возвращаем false — вызывающий решает про фолбэк.
static bool RunKernelCleanup() {
    if (!shadow_driver_ctl::OpenDriver()) {
        if (!EnsureDriverForCleanup())
            return false;
    }

    // Флаг CLEANUP_FLAG_SELF_DELETE намеренно исключён: если его передать,
    // ядро вызовет ZwTerminateProcess(loader_pid) прямо внутри обработчика IRP,
    // и лоадер умрёт до того как IOCTL вернёт управление — очистка прервётся.
    driver::CleanupRequest req{};
    req.session_key = shadow_driver_ctl::g_driver_session_key;
    req.request_id  = shadow_driver_ctl::next_request_id();
    req.flags       = driver::CLEANUP_FLAG_ALL & ~driver::CLEANUP_FLAG_SELF_DELETE;

    DWORD bytes = 0;
    const BOOL ok = DeviceIoControl(
        shadow_driver_ctl::g_driver_handle,
        SHADOW_CODE_CLEANUP,
        &req, sizeof(req),
        nullptr, 0,
        &bytes, nullptr);

    SecureZeroMemory(&req, sizeof(req));
    return ok != FALSE;
}

// ─── Проход 2: юзермод-хвост для непокрытого драйвером ─────────────────────
// Драйвер не трогает: AmCache, SRUM, UserAssist, MUICache, RecentDocs,
// ShellBags, Explorer MRU, JumpLists, Network List, crash dumps, соседние
// копии лоадера, собственный .pf. Пересечения с ядром (USN/DNS) безвредны.
static void RunUsermodeTail() {
    shadow_af::ActiveWipe();
}

// ─── Основная точка входа (общая для Clean-кнопки и авто-пути) ─────────────
static void PerformCleanup() {
    if (g_cleanup_running.exchange(true)) return;
    struct Guard { ~Guard() { shadow_cleanup::g_cleanup_running.store(false); } } guard;

    g_cleanup_done.store(false);
    g_cleanup_progress.store(0.05f);

    // 1. Ядро: BAM / USN / SYSINFO / DNS / EventLog / Prefetch.
    const bool kernel_ok = RunKernelCleanup();
    g_cleanup_progress.store(0.55f);

    // 2. Юзермод-хвост: то, чего драйвер не покрывает. Если ядро не
    //    загрузилось — это единственный проход, и он несёт всё сам.
    RunUsermodeTail();
    g_cleanup_progress.store(0.80f);

    // 3. Откат превентивных изменений, чтобы не осталось следа вмешательства.
    shadow_af::Shutdown();

    // 4. Самоудаление + выход.
    SpawnSelfDelete();

    g_cleanup_progress.store(1.0f);
    g_cleanup_done.store(true);

    ExitProcess(0);
}

// ─── Асинхронная обёртка для UI-bridge (кнопка Clean) ──────────────────────
static void CleanupAsync() {
    std::thread([] {
        PerformCleanup();
    }).detach();
}

// ─── Авто-зачистка после инжекта internal-продукта ─────────────────────────
// Internal живёт в GTA5.exe — лоадер после инжекта не нужен: закрываемся и
// чистим себя + следы. External мапится в процесс лоадера, trigger крутит
// треды в нём — там авто-выход не применяется (гейт в main.cpp).
// Небольшая задержка, чтобы успешный инжект успел отрисоваться в UI.
static void AutoCleanupAfterInjectAsync() {
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        PerformCleanup();
    }).detach();
}

} // namespace shadow_cleanup
