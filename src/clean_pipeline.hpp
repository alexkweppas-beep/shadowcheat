#pragma once
/*
 *  clean_pipeline.hpp — driver-first пайплайн зачистки (переписан с нуля).
 *
 *  Архитектура:
 *    1. Юзермод заполняет CleanRequest (инфо о лоадере + точные пути файлов
 *       для force-delete) в своей памяти.
 *    2. Драйвер ShadowClean маппится через amd_driver (PdFwKrnl, KDU #44) и
 *      вызывается один раз с param1=&req: USN journal, event-логи .evtx,
 *      реестр (HKLM+HKCU), force-delete файлов — всё в Ring-0, невидимо для
 *      юзермод-наблюдателей. Пул освобождается сразу (free=true) — ни девайса,
 *      ни резидента.
 *    3. Юзермод-хвост — только то, что ядром не зачистить: SRUM (ESE),
 *       AmCache (RegLoadKey). Превенция (Init/Shutdown) — отдельно.
 *    4. Self-delete (retry-loop) + выход через главный цикл (без ExitProcess
 *       с рабочего треда).
 *
 *  Фолбэк: если драйвер недоступен (HVCI и т.п.) — юзермод-примитивы покрывают
 *  реестр/USN/файлы сами (хуже по stealth, но следы не остаются).
 */

#include <windows.h>
#include <sddl.h>
#include <atomic>
#include <thread>
#include <cstring>
#include <vector>
#include <string>
#include "xorstr.hpp"
#include "anti_forensics.hpp"
#include "../driver/clean_request.h"

namespace shadow_pipeline {

enum class Mode { AutoAfterInject, ManualDeep };

// Прогресс для UI.
static std::atomic<bool>  g_running { false };
static std::atomic<float> g_progress{ 0.0f  };
static std::atomic<bool>  g_done    { false  };

// Координация выхода: пайплайн не зовёт ExitProcess сам — выставляет
// g_exit_requested, а процесс завершает главный цикл (WinMain return).
static std::atomic<bool> g_exit_requested{ false };

// Драйверный тир: скачать ShadowClean + смапить с param1=&req + free=true.
// Реализация в main.cpp. Возвращает false, если драйвер недоступен (HVCI и
// т.п.) — тогда юзермод-фолбэк покрывает реестр/USN/файлы сам.
extern bool RunCleanupDriverOnce(CleanRequest* req);

// ─── Сборка CleanRequest ───────────────────────────────────────────────────

static void GetCurrentUserSidW(wchar_t* out, size_t cap) {
    out[0] = 0;
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return;
    DWORD sz = 0;
    GetTokenInformation(tok, TokenUser, nullptr, 0, &sz);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && sz) {
        std::vector<uint8_t> buf(sz);
        if (GetTokenInformation(tok, TokenUser, buf.data(), sz, &sz)) {
            TOKEN_USER* tu = reinterpret_cast<TOKEN_USER*>(buf.data());
            wchar_t* sidStr = nullptr;
            if (ConvertSidToStringSidW(tu->User.Sid, &sidStr)) {
                wcsncpy_s(out, cap, sidStr, _TRUNCATE);
                LocalFree(sidStr);
            }
        }
    }
    CloseHandle(tok);
}

static void AddFileToRequest(CleanRequest& req, const std::wstring& path) {
    if (req.file_count >= SC_MAX_FILES) return;
    // Не добавляем запущенный образ — его сносит юзермод self-delete.
    if (req.loader_path[0] &&
        _wcsicmp(path.c_str(), req.loader_path) == 0)
        return;
    wcsncpy_s(req.file_paths[req.file_count], SC_PATH_LEN, path.c_str(), _TRUNCATE);
    ++req.file_count;
}

// Перечислить файлы по маске в каталоге в req.file_paths (без удаления).
static void EnumerateIntoRequest(CleanRequest& req, const std::wstring& dir,
                                 const std::wstring& pattern) {
    std::wstring search = dir + L"\\" + pattern;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        AddFileToRequest(req, dir + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void FillCleanRequest(CleanRequest& req) {
    req = {};
    req.magic = SHADOW_CLEAN_MAGIC;
    req.flags = SC_DO_ALL;
    req.loader_pid = GetCurrentProcessId();

    GetCurrentUserSidW(req.user_sid, RTL_NUMBER_OF(req.user_sid));

    // Тома: системный + том лоадера.
    wchar_t win[MAX_PATH]{};
    if (GetWindowsDirectoryW(win, MAX_PATH) && win[0]) {
        req.system_volume[0] = win[0];
        req.system_volume[1] = L':';
        req.system_volume[2] = 0;
    }
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    if (self[0]) {
        req.loader_volume[0] = self[0];
        req.loader_volume[1] = L':';
        req.loader_volume[2] = 0;
    }

    // Путь/каталог/имя лоадера.
    wcsncpy_s(req.loader_path, self, _TRUNCATE);
    std::wstring sp(self);
    size_t sl = sp.find_last_of(L'\\');
    std::wstring dir = (sl != std::wstring::npos) ? sp.substr(0, sl) : sp;
    std::wstring name = (sl != std::wstring::npos) ? sp.substr(sl + 1) : sp;
    wcsncpy_s(req.loader_dir, dir.c_str(), _TRUNCATE);
    std::wstring base = name;
    size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos) base = base.substr(0, dot);
    wcsncpy_s(req.loader_base, base.c_str(), _TRUNCATE);

    // %USERPROFILE% для sibling-копий.
    wchar_t up[MAX_PATH]{};
    DWORD uplen = GetEnvironmentVariableW(L"USERPROFILE", up, MAX_PATH);
    if (uplen && uplen < MAX_PATH)
        wcsncpy_s(req.user_profile, up, _TRUNCATE);

    wcsncpy_s(req.dns_domain, L"shadowcheats.cc", _TRUNCATE);

    // Перечисляем файлы для kernel force-delete (точные пути, без удаления).
    // 1. Свой .pf в Prefetch.
    {
        wchar_t windir[MAX_PATH]{};
        if (GetWindowsDirectoryW(windir, MAX_PATH) && windir[0]) {
            std::wstring pf = std::wstring(windir) + L"\\Prefetch";
            EnumerateIntoRequest(req, pf, base + L"*.pf");
        }
    }
    // 2. Crash dumps: %LOCALAPPDATA%\CrashDumps\<base>*.dmp
    {
        wchar_t local[MAX_PATH]{};
        DWORD l = GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
        if (l && l < MAX_PATH)
            EnumerateIntoRequest(req, std::wstring(local) + L"\\CrashDumps",
                                 base + L"*.dmp");
    }
    // 3. Sibling-копии: Downloads/Desktop/каталог лоадера — <base>*.exe.
    if (req.user_profile[0]) {
        EnumerateIntoRequest(req, std::wstring(req.user_profile) + L"\\Downloads",
                             base + L"*.exe");
        EnumerateIntoRequest(req, std::wstring(req.user_profile) + L"\\Desktop",
                             base + L"*.exe");
    }
    if (req.loader_dir[0])
        EnumerateIntoRequest(req, req.loader_dir, base + L"*.exe");

    // 4. LNK-файлы в Recent (<base>*.lnk).
    {
        wchar_t appdata[MAX_PATH]{};
        DWORD l = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
        if (l && l < MAX_PATH)
            EnumerateIntoRequest(req,
                                 std::wstring(appdata) + L"\\Microsoft\\Windows\\Recent",
                                 base + L"*.lnk");
    }

    // 5. Crash dumps: %LOCALAPPDATA%\CrashDumps\<base>*.dmp — уже выше;
    //    WER ReportQueue/ReportArchive: файлы внутри папок, названных под нас.
    {
        wchar_t progdata[MAX_PATH]{};
        DWORD l = GetEnvironmentVariableW(L"ProgramData", progdata, MAX_PATH);
        if (l && l < MAX_PATH) {
            std::wstring wer = std::wstring(progdata) + L"\\Microsoft\\Windows\\WER";
            const wchar_t* sub[] = { L"\\ReportArchive", L"\\ReportQueue" };
            for (int s = 0; s < 2; ++s) {
                std::wstring dir = wer + sub[s];
                // Папки отчётов названы по имени exe — находим их и берём файлы.
                WIN32_FIND_DATAW fd;
                HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
                if (h == INVALID_HANDLE_VALUE) continue;
                do {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                    if (fd.cFileName[0] == L'.') continue;
                    // Имя папки содержит имя нашего exe?
                    std::wstring dn = fd.cFileName;
                    std::wstring baseL = base;
                    for (auto& c : dn) c = towlower(c);
                    for (auto& c : baseL) c = towlower(c);
                    if (dn.find(baseL) == std::wstring::npos) continue;
                    EnumerateIntoRequest(req, dir + L"\\" + fd.cFileName, L"*");
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        }
    }

    // 6. JumpLists: AutomaticDestinations/CustomDestinations — .ms названы хэшем,
    //    матчим по содержимому (первые 4К содержат путь к нашему exe).
    {
        wchar_t appdata[MAX_PATH]{};
        DWORD l = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
        if (l && l < MAX_PATH) {
            const wchar_t* jl[] = { L"\\Microsoft\\Windows\\Recent\\AutomaticDestinations",
                                    L"\\Microsoft\\Windows\\Recent\\CustomDestinations" };
            std::string baseA;
            for (const wchar_t* p = base.c_str(); *p; ++p) baseA.push_back((char)*p);
            for (auto& c : baseA) c = (char)tolower((unsigned char)c);
            for (int d = 0; d < 2; ++d) {
                std::wstring dir = std::wstring(appdata) + jl[d];
                WIN32_FIND_DATAW fd;
                HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
                if (h == INVALID_HANDLE_VALUE) continue;
                do {
                    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                    std::wstring full = dir + L"\\" + fd.cFileName;
                    HANDLE f = CreateFileW(full.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (f == INVALID_HANDLE_VALUE) continue;
                    char buf[4096]; DWORD rd = 0;
                    ReadFile(f, buf, sizeof(buf) - 1, &rd, nullptr);
                    CloseHandle(f);
                    bool match = false;
                    for (DWORD i = 0; i + baseA.size() <= rd && !match; ++i) {
                        size_t j = 0;
                        for (; j < baseA.size(); ++j) {
                            char a = buf[i + j];
                            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                            if (a != baseA[j]) break;
                        }
                        if (j == baseA.size()) match = true;
                    }
                    if (match) AddFileToRequest(req, full);
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        }
    }
}

// ─── Самоудаление exe: bounded retry-loop вместо ping-гонки ────────────────
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

// ─── Юзермод-фолбэк (драйвер недоступен — HVCI и т.п.) ─────────────────────
// Покрывает то, что драйвер делал бы, но из юзермода. Хуже по stealth, но
// следы не остаются. Вызывается только при фейле драйверного тира.
static void RunUsermodeFallback(const CleanRequest& req) {
    (void)req;
    shadow_af::WipeBamEntry();
    shadow_af::WipeUserAssist();
    shadow_af::WipeMuiCacheEntry();
    shadow_af::WipeRecentDocsRegistry();
    shadow_af::WipeAppCompatFlags();
    shadow_af::WipeShellBagsSelective();
    shadow_af::WipeExplorerMRUKeys();
    shadow_af::WipeNetworkListEntries();
    shadow_af::WipeLnkEntries();
    shadow_af::WipeJumpLists();
    shadow_af::WipeDnsCache();
    shadow_af::DeleteUsnJournal();
    shadow_af::VerifyUsnJournalDeleted();
    shadow_af::WipeOwnPrefetch();
    shadow_af::WipeSiblingLoaderCopies();
    shadow_af::WipeCrashDumps();
}

// ─── Точка входа. Никогда не вызывает ExitProcess. ─────────────────────────
static void Run(Mode mode) {
    if (g_running.exchange(true)) return;
    struct Guard { ~Guard() { shadow_pipeline::g_running.store(false); } } guard;

    g_done.store(false);
    g_progress.store(0.05f);

    // 1. Заполнить CleanRequest (инфо + точные пути файлов).
    CleanRequest req{};
    FillCleanRequest(req);
    g_progress.store(0.15f);

    // 2. Драйверный тир: ShadowClean в Ring-0 (USN/event-логи/реестр/файлы).
    bool driverOk = RunCleanupDriverOnce(&req);
    g_progress.store(0.65f);

    // 2b. Фолбэк на HVCI/фейл драйвера: юзермод покрывает сам.
    if (!driverOk)
        RunUsermodeFallback(req);

    // 3. Юзермод-хвост: то, что ядром не зачистить или что точечнее из юзермода.
    //    SRUM — только при подтверждённо остановленном DPS; AmCache — RegLoadKey;
    //    DNS — точечный flush наших доменов; WER report-ДИРЫ (драйвер удаляет
    //    только файлы, пустые папки отчётов снимаем тут).
    g_progress.store(0.72f);
    if (!shadow_af::IsServiceRunning(xorstr_("DPS")))
        shadow_af::WipeSrumRecords();
    g_progress.store(0.78f);
    shadow_af::WipeAmCacheEntry();
    g_progress.store(0.82f);
    shadow_af::WipeDnsCache();
    g_progress.store(0.85f);
    shadow_af::WipeCrashDumps();

    // 4. Самоудаление — последним, до сигнала выхода.
    g_progress.store(0.95f);
    SpawnSelfDelete();

    g_progress.store(1.0f);
    g_done.store(true);

    // Просим главный цикл завершить WinMain штатно (там restore превенции).
    g_exit_requested.store(true);
}

// Асинхронная обёртка для кнопки Clean (прогресс виден в UI).
static void RunAsync(Mode mode) {
    std::thread([mode] { Run(mode); }).detach();
}

} // namespace shadow_pipeline
