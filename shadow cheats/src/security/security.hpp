#pragma once

#include <windows.h>
#include <string>

namespace shadow::security {

    // Инициализация защиты (вызвать первым в main)
    void Init(const std::string& token, const std::string& hwid);

    // Стирание PE заголовков из памяти
    void ErasePEHeaders(void* module_base);

    // Скрыть поток от отладчика
    void HideThread(HANDLE hThread);

    // Главный security loop (запускать в std::thread)
    void SecurityLoop(const std::string& token, const std::string& hwid);

    // Honeypot приманки (определения в security.cpp)
    bool FakeLicenseCheck();
    bool FakeAuthBypass();
}
