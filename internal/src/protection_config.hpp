#pragma once

/* SHADOW_PACKER выбирает VMProtect, Themida или сборку без упаковщика.
 * Наличие SDK задаётся отдельно через SHADOW_HAVE_*_SDK.
 * SHADOW_DEV_BUILD допускает тестовые значения TLS pinning и не предназначен для доставки пользователям. */

#ifndef SHADOW_PROTECTION_CONFIG_HPP
#define SHADOW_PROTECTION_CONFIG_HPP

#ifndef SHADOW_PACKER
#define SHADOW_PACKER 0
#endif

// ─── Defaults ───
#if !defined(SHADOW_PACKER_VMP) && !defined(SHADOW_PACKER_THEMIDA) && !defined(SHADOW_PACKER_NONE)
    #define SHADOW_PACKER_NONE 1
#endif

// ─── Sync: user-facing /DSHADOW_PACKER=NAME → internal define ───
#if defined(SHADOW_PACKER)
    #if SHADOW_PACKER == 0
        #undef  SHADOW_PACKER_NONE
        #define SHADOW_PACKER_NONE 1
    #elif SHADOW_PACKER == 1 /*VMP*/
        #undef  SHADOW_PACKER_NONE
        #define SHADOW_PACKER_VMP 1
    #elif SHADOW_PACKER == 2 /*THEMIDA*/
        #undef  SHADOW_PACKER_NONE
        #define SHADOW_PACKER_THEMIDA 1
    #endif
#endif

// ─── SDK presence probes (user defines when real SDK is in include path) ───
//   Серъёзный билд: /DSHADOW_HAVE_VMP_SDK=1 (если в include path есть настоящий
//   VMProtectSDK.h), или /DSHADOW_HAVE_THEMIDA_SDK=1 (если есть ThemidaSDK.h).
//   Без этого SHADOW_PROTECT_* деградирует в no-op. Защищённая Release-сборка
//   задаёт оба define из CMake и затем обязательно проходит VMProtect_Con.

// ─── Helper: маркер "протекция активна" ───
//   Используется sanity-check'ом в security.cpp; в stub-режиме = false.
#if defined(SHADOW_PACKER_VMP) && defined(SHADOW_HAVE_VMP_SDK)
    #define SHADOW_PACKED 1
#elif defined(SHADOW_PACKER_THEMIDA) && defined(SHADOW_HAVE_THEMIDA_SDK)
    #define SHADOW_PACKED 1
#else
    #define SHADOW_PACKED 0
#endif

// ─── Helper: показать что именно подключено (для diagnostics в Build Output) ───
#if SHADOW_PACKED
    #if defined(SHADOW_PACKER_VMP)
        #pragma message("SHADOW: packer=VMProtect (real SDK)")
    #elif defined(SHADOW_PACKER_THEMIDA)
        #pragma message("SHADOW: packer=Themida (real SDK)")
    #endif
#else
    #if defined(SHADOW_PACKER_VMP)
        #pragma message("SHADOW: packer=VMProtect (STUB — /DSHADOW_HAVE_VMP_SDK missing)")
    #elif defined(SHADOW_PACKER_THEMIDA)
        #pragma message("SHADOW: packer=Themida (STUB — /DSHADOW_HAVE_THEMIDA_SDK missing)")
    #else
        #pragma message("SHADOW: packer=NONE (dev build, naked MSVC)")
    #endif
#endif

#endif // SHADOW_PROTECTION_CONFIG_HPP
