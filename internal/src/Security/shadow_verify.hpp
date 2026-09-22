#pragma once
#include <windows.h>
#include <cstdint>

struct ShadowLaunchAuth;

namespace ShadowSecurity {
    bool VerifyLaunchAuth(const ShadowLaunchAuth* auth) noexcept;

    // Запускает фоновый поток, который проверяет подписку (через 1 мин, затем каждые 5 мин)
    void StartPeriodicSubscriptionCheck();
}
