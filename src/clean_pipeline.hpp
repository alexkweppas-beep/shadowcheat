#pragma once
/*
 *  clean_pipeline.hpp — единый упорядоченный пайплайн зачистки.
 *
 *  Заменяет cleanup.hpp. Надёжность прежде всего:
 *    1. Один поток на прогон, шаги упорядочены и идемпотентны.
 *    2. Никакого ExitProcess с рабочего треда: пайплайн выставляет
 *       g_exit_requested, а процесс завершает главный цикл (WinMain return).
 *       Зачистка никогда не обрывается посередине кадром UI.
 *    3. USN journal удаляется ПОСЛЕДНИМ среди файловых операций и
 *       верифицируется перечитыванием.
 *    4. Self-delete без гонки по таймингу: дочерний cmd крутит del в
 *       bounded retry-loop до тех пор, пока файл не исчезнет.
 *    5. Ядро просим только о том, что юзермод не может: event-логи (.evtx)
 *       и ключи System Informer/Process Hacker. BAM/USN/DNS/Prefetch покрыты
 *       юзермодом и у ядра больше не запрашиваются.
 *
 *  Режимы:
 *    AutoAfterInject — после успешного инжекта internal (лоадер не нужен):
 *                      юзермод-проход + самоудаление, без ядра.
 *    ManualDeep      — кнопка Clean: юзермод-проход + ядерный тир.
 */

#include <windows.h>
#include <atomic>
#include <thread>
#include <cstring>
#include "xorstr.hpp"
#include "driver_ctl.hpp"
#include "anti_forensics.hpp"

namespace shadow_pipeline {

enum class Mode { AutoAfterInject, ManualDeep };

// Прогресс для UI.
static std::atomic<bool>  g_running { false };
static std::atomic<float> g_progress{ 0.0f  };
static std::atomic<bool>  g_done    { false  };

// Координация выхода: пайплайн никогда не зовёт ExitProcess сам. Он выставляет
// g_exit_requested, а главный цикл завершает WinMain штатно.
static std::atomic<bool> g_exit_requested{ false };

// Ядерный тир поднимается по требованию (скачивание+мап через PdFwKrnl) —
// реализация в main.cpp.
extern bool EnsureDriverForCleanup();

// ─── Самоудаление exe: bounded retry-loop вместо ping-гонки ────────────────
// Ребёнок крутит del до 50 раз с ~1с паузой между попытками и выходит, как
// только файл исчез — не зависит от того, как быстро умирает родитель.
static bool SpawnSelfDelete() {
    wchar_t my_path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, my_path, MAX_PATH);

    wchar_t cmd_line[MAX_PATH * 2 + 160] = {};
    swprintf(cmd_line, ARRAYSIZE(cmd_line),
        L"cmd.exe /c \"for /l %%%%i in (1,1,50) do (ping 127.0.0.1 -n 2 >nul & del /f /q \"%s\" & if not exist \"%s\" exit)\"",
        my_path, my_path);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmd_line,
                        nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | DETACHED_PROCESS,
                        nullptr, nullptr, &si, &pi))
        return false;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

// ─── Упорядоченные юзермод-шаги (примитивы живут в anti_forensics.hpp) ─────
static void RunUsermodeSteps() {
    // 1. Реестр (AeLookupSvc уже остановлен превенцией).
    g_progress.store(0.10f);
    shadow_af::WipeAmCacheEntry();
    shadow_af::WipeUserAssist();
    shadow_af::WipeMuiCacheEntry();
    shadow_af::WipeBamEntry();
    shadow_af::WipeLnkEntries();
    shadow_af::WipeRecentDocsRegistry();
    shadow_af::WipeAppCompatFlags();
    shadow_af::WipeShellBagsSelective();
    shadow_af::WipeExplorerMRUKeys();
    shadow_af::WipeJumpLists();
    shadow_af::WipeNetworkListEntries();

    // 2. DNS.
    g_progress.store(0.30f);
    shadow_af::WipeDnsCache();

    // 3. SRUM — только если DPS подтверждённо остановлен (превенция держит
    //    его стопнутым всю сессию; проверка — страховка от частичного старта).
    g_progress.store(0.45f);
    if (!shadow_af::IsServiceRunning(xorstr_("DPS")))
        shadow_af::WipeSrumRecords();

    // 4. Crash dumps / WER archives.
    g_progress.store(0.55f);
    shadow_af::WipeCrashDumps();

    // 5. Файлы: свой .pf + соседние копии лоадера.
    g_progress.store(0.65f);
    shadow_af::WipeOwnPrefetch();
    shadow_af::WipeSiblingLoaderCopies();

    // 6. USN journal — ПОСЛЕДНИМ среди файловых операций, с верификацией.
    //    После удаления журнала последующие файловые операции не журналируются.
    g_progress.store(0.80f);
    shadow_af::DeleteUsnJournal();
    shadow_af::VerifyUsnJournalDeleted();
}

// ─── Ядерный тир (только ManualDeep): event-логи + System Informer ключи ───
static void RunKernelDeepStep() {
    if (!shadow_driver_ctl::OpenDriver()) {
        if (!EnsureDriverForCleanup())
            return; // ядро недоступно — юзермод уже покрыл всё остальное
    }

    driver::CleanupRequest req{};
    req.session_key = shadow_driver_ctl::g_driver_session_key;
    req.request_id  = shadow_driver_ctl::next_request_id();
    // Только то, что юзермод не может. BAM/USN/DNS/Prefetch покрыты юзермодом
    // и у ядра больше не запрашиваются; SELF_DELETE никогда не шлём в IOCTL.
    req.flags = driver::CLEANUP_FLAG_EVENT_LOG | driver::CLEANUP_FLAG_SYSINFO;

    DWORD bytes = 0;
    DeviceIoControl(
        shadow_driver_ctl::g_driver_handle,
        SHADOW_CODE_CLEANUP,
        &req, sizeof(req),
        nullptr, 0,
        &bytes, nullptr);
    SecureZeroMemory(&req, sizeof(req));
}

// ─── Точка входа. Никогда не вызывает ExitProcess. ─────────────────────────
static void Run(Mode mode) {
    if (g_running.exchange(true)) return;
    struct Guard { ~Guard() { shadow_pipeline::g_running.store(false); } } guard;

    g_done.store(false);
    g_progress.store(0.05f);

    RunUsermodeSteps();

    if (mode == Mode::ManualDeep) {
        g_progress.store(0.88f);
        RunKernelDeepStep();
    }

    // Самоудаление — последним, до сигнала выхода.
    g_progress.store(0.95f);
    SpawnSelfDelete();

    g_progress.store(1.0f);
    g_done.store(true);

    // Просим главный цикл завершить WinMain штатно (там же restore превенции).
    g_exit_requested.store(true);
}

// Асинхронная обёртка для кнопки Clean (прогресс виден в UI).
static void RunAsync(Mode mode) {
    std::thread([mode] { Run(mode); }).detach();
}

} // namespace shadow_pipeline
