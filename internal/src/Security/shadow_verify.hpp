#pragma once
#include <windows.h>
#include <cstdint>

namespace ShadowSecurity {
    // Проверяет, что IPC секция создана лоадером. Если нет - краш.
    void VerifySharedMemory();

    // Проверяет, что процесс лоадера жив (сверяет PID из Shared Memory).
    // Должно вызываться 1 раз при инжекте.
    void VerifyLoaderPidOnce();

    // Запускает фоновый поток, который проверяет подписку (через 1 мин, затем каждые 5 мин)
    void StartPeriodicSubscriptionCheck();
}
