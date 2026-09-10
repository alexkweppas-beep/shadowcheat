#pragma once

#include "protection_config.hpp"

/*
 * VMProtect is deliberately limited to cold, high-value paths. Never wrap the
 * Present hook, ESP/aim per-frame passes, menu Render, or anything else that
 * runs at display rate: mutation and virtualization there make frame time and
 * UI input hitch. Protecting the whole image also makes the manually mapped
 * payload much harder to initialise reliably.
 *
 * Development builds keep these as no-ops. Release builds configured through
 * CMake define SHADOW_HAVE_VMP_SDK and are rejected if VMProtect_Con does not
 * produce the protected output.
 */
#if defined(SHADOW_PACKER_VMP) && defined(SHADOW_HAVE_VMP_SDK)
#include <VMProtectSDK.h>

#define SHADOW_PROTECT_ULTRA(name) \
    VMProtectBeginUltra(name);     \
    do { } while (0)

#define SHADOW_PROTECT_VIRT(name)       \
    VMProtectBeginVirtualization(name); \
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
