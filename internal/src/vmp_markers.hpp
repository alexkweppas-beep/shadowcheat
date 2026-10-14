#pragma once

#include "protection_config.hpp"

/* Маркеры VMProtect ограничены инициализацией и другими редкими вызовами.
 * Не ставить их в Present, покадровый ESP, аим и рендер меню. В dev-сборке маркеры отключены. */
#if defined(SHADOW_PACKER_VMP) && defined(SHADOW_HAVE_VMP_SDK)
#include <VMProtectSDK.h>

#define SHADOW_PROTECT_ULTRA(name) \
    VMProtectBeginMutation(name);     \
    do { } while (0)

#define SHADOW_PROTECT_VIRT(name)       \
    VMProtectBeginMutation(name); \
    do { } while (0)

#define SHADOW_PROTECT_MUT(name) \
    VMProtectBeginMutation(name); \
    do { } while (0)

#define SHADOW_PROTECT_END() \
    VMProtectEnd();          \
    do { } while (0)

#define SHADOW_DECRYPT_STRING_A(value) VMProtectDecryptStringA(value)
#define SHADOW_DECRYPT_STRING_W(value) VMProtectDecryptStringW(value)
#define SHADOW_FREE_STRING(value)      \
    do {                               \
        if (value)                     \
            VMProtectFreeString(value); \
    } while (0)
#else
#define SHADOW_PROTECT_ULTRA(name) do { (void)sizeof(name); } while (0)
#define SHADOW_PROTECT_VIRT(name)  do { (void)sizeof(name); } while (0)
#define SHADOW_PROTECT_MUT(name)   do { (void)sizeof(name); } while (0)
#define SHADOW_PROTECT_END()       do { } while (0)

#define SHADOW_DECRYPT_STRING_A(value) (value)
#define SHADOW_DECRYPT_STRING_W(value) (value)
#define SHADOW_FREE_STRING(value) do { (void)(value); } while (0)
#endif
