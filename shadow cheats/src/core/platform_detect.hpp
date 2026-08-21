#pragma once
#include "security/xorstr.hpp"
#include <Windows.h>
#include <string>

// Поддерживаемые платформы
enum class Platform
{
    Unknown = 0,
    AltV,    // класс окна: "grcWindow"
    RageMP,  // класс окна: любой другой (например случайные цифры)
};

namespace platform
{
    inline Platform g_platform = Platform::Unknown;

    struct FindResult
    {
        Platform platform = Platform::Unknown;
        HWND     hwnd     = nullptr;
        DWORD    target_pid = 0;
    };

    inline BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
    {
        FindResult* result = reinterpret_cast<FindResult*>(lParam);

        // Проверяем что окно принадлежит нашему процессу GTA5
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != result->target_pid)
            return TRUE; // не наш процесс, продолжаем

        char class_name[256] = {};
        GetClassNameA(hwnd, class_name, sizeof(class_name));

        if (class_name[0] == '\0')
            return TRUE; // пустой класс — пропускаем

        if (std::string(class_name) == xorstr_("grcWindow"))
        {
            // Alt:V
            result->platform = Platform::AltV;
            result->hwnd     = hwnd;
            return FALSE; // нашли — стоп
        }
        else
        {
            // Любое другое окно GTA5 процесса = RageMP
            result->platform = Platform::RageMP;
            result->hwnd     = hwnd;
            return FALSE; // нашли — стоп
        }
    }

    // Определяет платформу по окну процесса GTA5.
    // pid — ID процесса GTA5.exe (из driver::process_id).
    // Возвращает найденный HWND (или nullptr).
    inline HWND detect(Platform& out_platform, DWORD pid = 0)
    {
        FindResult result{};
        result.target_pid = pid;

        // Если pid не передан — ищем по всем окнам (старый режим)
        if (pid == 0)
        {
            // fallback: просто ищем grcWindow среди всех окон
            EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
                FindResult* r = reinterpret_cast<FindResult*>(lp);
                char cls[256] = {};
                GetClassNameA(hwnd, cls, sizeof(cls));
                if (std::string(cls) == xorstr_("grcWindow")) {
                    r->platform = Platform::AltV;
                    r->hwnd = hwnd;
                    return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&result));
        }
        else
        {
            EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&result));
        }

        out_platform = result.platform;
        g_platform   = result.platform;
        return result.hwnd;
    }

    inline const char* to_string(Platform p)
    {
        switch (p)
        {
        case Platform::AltV:   return xorstr_("Alt:V");
        case Platform::RageMP: return xorstr_("RageMP");
        default:               return xorstr_("Unknown");
        }
    }
}
