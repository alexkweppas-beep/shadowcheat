#pragma once
#include "security/xorstr.hpp"

/*
 * shadow cheats — Honeypot System
 *
 * Несколько видов приманок:
 *  1. Fake license check functions — крякер патчит их → детект
 *  2. Fake "bypass" exports в DLL (если инжектируем себя как DLL)
 *  3. Named Pipe honeypot — кто-то пытается подключиться к нашему pipe
 *  4. Anti-dump страница — обнуляется при дампе
 *  5. Canary bytes в критичных структурах данных
 */

#include <windows.h>
#include <atomic>
#include <thread>
#include <chrono>
#include <functional>
#include "skCrypter.h"

namespace shadow::honeypot {

    // ─── Callbacks ───
    // Исправление: используем голый указатель на функцию вместо std::function,
    // чтобы избежать Heap Corruption при manual mapping / пропущенных TLS коллбеках
    using DetectCallback = void(*)(const char* trigger);
    static DetectCallback g_on_detect = nullptr;

    inline void SetCallback(DetectCallback cb) { g_on_detect = cb; }
    inline void Fire(const char* trigger) {
        if (g_on_detect) g_on_detect(trigger);
    }

    // ─────────────────────────────────────────────────────────
    //  1. FAKE LICENSE FUNCTIONS
    //  Крякер ищет в IDA "license/auth/bypass" → патчит первые байты
    // ─────────────────────────────────────────────────────────

    // Эти функции выглядят как проверка лицензии, но на самом деле — приманки
    // Крякер найдёт их по xref от сравнения результата с true/false

    __declspec(noinline) __declspec(dllexport)
    bool CheckLicense() {
        // Выглядит как настоящая проверка — volatile чтобы компилятор не убрал
        volatile int a = 1;
        volatile int b = 1;
        return (a + b) == 2;
    }

    __declspec(noinline) __declspec(dllexport)
    bool IsSubscriptionValid() {
        volatile DWORD tick = GetTickCount();
        return tick > 0;
    }

    __declspec(noinline) __declspec(dllexport)
    bool VerifyToken(const char* token) {
        volatile int len = 0;
        if (!token) return false;
        while (token[len]) len++;
        return len > 0;
    }

    __declspec(noinline) __declspec(dllexport)
    bool AuthBypass() {
        // Самая очевидная приманка — крякер патчит чтобы возвращала true
        volatile bool result = false;
        return result;
    }

    // Детект: проверить первые байты всех honeypot функций
    // Нормально: 40/48 55 (PUSH RBP или похожий пролог x64)
    // Пропатчено: B0 01 C3 (MOV AL,1; RET) или 31 C0 C3 (XOR EAX,EAX; RET)
    static bool IsFnPatched(void* fn) {
        if (!fn) return false;
        uint8_t* b = (uint8_t*)fn;
        // MOV AL, 0/1 + RET
        if (b[0] == 0xB0 && (b[1] == 0 || b[1] == 1) && b[2] == 0xC3) return true;
        // XOR EAX,EAX + RET
        if (b[0] == 0x31 && b[1] == 0xC0 && b[2] == 0xC3) return true;
        // MOV EAX, 0/1 + RET
        if (b[0] == 0xB8 && b[4] == 0 && b[5] == 0xC3) return true;
        // MOV EAX, 1 + RET
        if (b[0] == 0xB8 && b[1] == 1 && b[2] == 0 && b[3] == 0 && b[4] == 0 && b[5] == 0xC3) return true;
        // Прямой JMP (крякер поставил JMP на свою функцию)
        if (b[0] == 0xE9 || b[0] == 0xEB) return true;
        return false;
    }

    inline bool CheckFunctions() {
        return IsFnPatched(&CheckLicense)
            || IsFnPatched(&IsSubscriptionValid)
            || IsFnPatched(&VerifyToken)
            || IsFnPatched(&AuthBypass);
    }

    // ─────────────────────────────────────────────────────────
    //  2. MEMORY CANARIES
    //  Страницы-приманки в важных местах памяти
    // ─────────────────────────────────────────────────────────

    struct CanaryPage {
        uint8_t* page     = nullptr;
        uint64_t expected = 0;
        size_t   offset   = 0; // Смещение canary в странице
    };

    static CanaryPage g_canary;
    static constexpr uint64_t CANARY_VALUE = 0xDEADBEEFCAFEBABEULL;

    inline void InitCanary() {
        // Выделить страницу и записать canary в неожиданное место
        g_canary.page = (uint8_t*)VirtualAlloc(nullptr, 4096,
                                                 MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!g_canary.page) return;
        g_canary.offset   = 0x100 + (GetTickCount() & 0xFF); // Рандомный offset
        g_canary.expected = CANARY_VALUE ^ (uint64_t)(uintptr_t)g_canary.page;
        *(uint64_t*)(g_canary.page + g_canary.offset) = g_canary.expected;
    }

    inline bool CheckCanary() {
        if (!g_canary.page) return false;
        uint64_t current = *(uint64_t*)(g_canary.page + g_canary.offset);
        return current != g_canary.expected; // true = canary убит
    }

    // ─────────────────────────────────────────────────────────
    //  3. ANTI-DUMP PAGE
    //  Страница заполнена данными и защищена PAGE_GUARD
    //  При дампе процесса — дампер пытается прочитать её → исключение → детект
    // ─────────────────────────────────────────────────────────

    static uint8_t* g_dump_page  = nullptr;
    static bool     g_dump_hit   = false;

    static LONG WINAPI DumpPageVEH(EXCEPTION_POINTERS* info) {
        if (!g_dump_page) return EXCEPTION_CONTINUE_SEARCH;
        uintptr_t fault_addr = (uintptr_t)info->ExceptionRecord->ExceptionAddress;
        uintptr_t page_addr  = (uintptr_t)g_dump_page;
        if (info->ExceptionRecord->ExceptionCode == STATUS_GUARD_PAGE_VIOLATION
            && fault_addr >= page_addr && fault_addr < page_addr + 4096) {
            // Наша guard page была задета
            g_dump_hit = true;
            // Восстановить guard (она снимается после первого hit)
            DWORD old;
            VirtualProtect(g_dump_page, 4096, PAGE_READWRITE | PAGE_GUARD, &old);
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }

    inline void InitDumpDetector() {
        g_dump_page = (uint8_t*)VirtualAlloc(nullptr, 4096,
                                               MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!g_dump_page) return;
        // Заполнить мусором чтобы не выглядело подозрительно
        for (int i = 0; i < 4096; i++) g_dump_page[i] = (uint8_t)(i ^ 0xA5);
        // Поставить PAGE_GUARD
        DWORD old;
        VirtualProtect(g_dump_page, 4096, PAGE_READWRITE | PAGE_GUARD, &old);
        // Зарегистрировать VEH чтобы перехватывать guard violations
        AddVectoredExceptionHandler(1, DumpPageVEH);
    }

    inline bool CheckDumpHit() {
        if (g_dump_hit) {
            g_dump_hit = false; // сбросить для следующей проверки
            return true;
        }
        return false;
    }

    // ─────────────────────────────────────────────────────────
    //  4. NAMED PIPE HONEYPOT
    //  Cheat Engine и другие инструменты могут искать inter-process channels
    //  Создаём pipe с "интересным" именем и ждём подключения
    // ─────────────────────────────────────────────────────────

    static bool g_pipe_hit   = false;
    static HANDLE g_pipe     = INVALID_HANDLE_VALUE;

    static DWORD WINAPI PipeHoneypotThread(LPVOID) {
        // Создать pipe с именем которое выглядит как "обычная" коммуникация
        g_pipe = CreateNamedPipeA(
            skCrypt(xorstr_("\\\\.\\pipe\\shadow_license_ipc")), // "интересное" имя
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 0, 0, 500, nullptr);
        if (g_pipe == INVALID_HANDLE_VALUE) return 0;

        // Ждём подключения с таймаутом 500мс, потом цикл
        while (true) {
            if (ConnectNamedPipe(g_pipe, nullptr)
                || GetLastError() == ERROR_PIPE_CONNECTED) {
                g_pipe_hit = true;
                DisconnectNamedPipe(g_pipe);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return 0;
    }

    inline void InitPipeHoneypot() {
        HANDLE hThread = CreateThread(nullptr, 0, PipeHoneypotThread, nullptr, 0, nullptr);
        if (hThread) CloseHandle(hThread);
    }

    inline bool CheckPipeHit() {
        if (g_pipe_hit) { g_pipe_hit = false; return true; }
        return false;
    }

    // ─────────────────────────────────────────────────────────
    //  5. FAKE EXPORTS (для DLL-варианта чита)
    //  Если чит грузится как DLL — экспортируем "вкусные" функции
    //  Любой кто попробует их вызвать — попадает в ловушку
    // ─────────────────────────────────────────────────────────

    __declspec(noinline) __declspec(dllexport)
    void* GetInternalPointer() {
        // Крякер ищет этот экспорт чтобы найти внутренние структуры
        Fire(skCrypt(xorstr_("FAKE_EXPORT_GETINTERNALPOINTER")));
        return nullptr;
    }

    __declspec(noinline) __declspec(dllexport)
    int UnlockAllFeatures(int key) {
        (void)key;
        Fire(skCrypt(xorstr_("FAKE_EXPORT_UNLOCKALL")));
        return -1;
    }

    // ─────────────────────────────────────────────────────────
    //  MASTER INIT + CHECK
    // ─────────────────────────────────────────────────────────

    inline void InitAll() {
        InitCanary();
        // InitDumpDetector();
        InitPipeHoneypot();
    }

    struct HoneypotResult {
        bool any_triggered = false;
        bool fn_patched    = false;
        bool canary_killed = false;
        bool dump_hit      = false;
        bool pipe_hit      = false;
        const char* first_trigger = nullptr;
    };

    inline HoneypotResult CheckAll() {
        HoneypotResult r;
        if ((r.fn_patched  = CheckFunctions()))  { r.any_triggered = true; r.first_trigger = xorstr_("FN_PATCHED"); }
        if ((r.canary_killed = CheckCanary()))   { r.any_triggered = true; if (!r.first_trigger) r.first_trigger = xorstr_("CANARY_KILLED"); }
        if ((r.dump_hit    = CheckDumpHit()))    { r.any_triggered = true; if (!r.first_trigger) r.first_trigger = xorstr_("DUMP_PAGE_HIT"); }
        if ((r.pipe_hit    = CheckPipeHit()))    { r.any_triggered = true; if (!r.first_trigger) r.first_trigger = xorstr_("PIPE_CONNECT"); }
        return r;
    }

} // namespace shadow::honeypot
