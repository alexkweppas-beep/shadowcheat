#pragma once

/*
 * shadow cheats — Packer dispatch (was vmp_markers.hpp)
 *
 * Refaktor -*- v1.1 -*- теперь agnostic по packer'у: тот же набор
 * SHADOW_PROTECT_* макросов раскрывается в маркеры выбранного packer'а.
 *
 *   -DSHADOW_PACKER=1  → VMProtect
 *   -DSHADOW_PACKER=2  → Themida
 *   -DSHADOW_PACKER=0  (default) → NONE (no-op, debug-билд)
 *
 * Plus /DSHADOW_HAVE_VMP_SDK или /DSHADOW_HAVE_THEMIDA_SDK — реально ли
 * в include path лежит официальный SDK packer'а. Без этого — stub'ы.
 *
 * Call sites в security.cpp, ban_reporter.hpp и др. используют ТОЛЬКО
 * SHADOW_PROTECT_*. Изменение packer'а = compile flag, без правок кода.
 *
 * Для исторического backward-compat оставлены VMP_* имена (VMP_SanityCheck).
 */

#include "protection_config.hpp"

#if defined(SHADOW_PACKER_THEMIDA)
// ─── THEMIDA backend ───
#include "themida_markers.hpp"

// Themida alias для старого VMP_SanityCheck name (security.cpp зовёт).
// Реально вызывает Themida_SanityCheck → Themida runtime API.
inline void VMP_SanityCheck() { Themida_SanityCheck(); }

#elif defined(SHADOW_PACKER_VMP)
// ─── VMP backend (оригинал из старого vmp_markers.hpp, ниже) ───
#include "VMProtectSDK.h"

#define SHADOW_PROTECT_ULTRA(name)                                             \
  VMProtectBeginUltra(name);                                                   \
  do {                                                                         \
  } while (0)

#define SHADOW_PROTECT_VIRT(name)                                              \
  VMProtectBeginVirtualization(name);                                          \
  do {                                                                         \
  } while (0)

#define SHADOW_PROTECT_MUT(name)                                               \
  VMProtectBeginMutation(name);                                                \
  do {                                                                         \
  } while (0)

#define SHADOW_PROTECT_END() VMProtectEnd()

// VMP runtime checks — работают только в .vmp.exe (после post-buildконвейера)
#ifdef VMPROTECT_STUB
// Stub (VMProtectSDK.h если он лежит в репе как заглушка)
#define SHADOW_VMP_IS_PROTECTED() (false)
#define SHADOW_VMP_IS_DEBUGGER() (false)
#define SHADOW_VMP_IS_VM() (false)
#define SHADOW_VMP_IS_CRC_VALID() (true)
#else
#define SHADOW_VMP_IS_PROTECTED() (VMProtectIsProtected())
#define SHADOW_VMP_IS_DEBUGGER() (VMProtectIsDebuggerPresent(true))
#define SHADOW_VMP_IS_VM() (VMProtectIsVirtualMachinePresent())
#define SHADOW_VMP_IS_CRC_VALID() (VMProtectIsValidImageCRC())
#endif

inline void VMP_SanityCheck() {
  if (SHADOW_VMP_IS_DEBUGGER())
    *(volatile int *)nullptr = 0;
  if (SHADOW_VMP_IS_VM())
    *(volatile int *)nullptr = 0;
}

// Aliases so security.cpp compiles in either backend.
#define SHADOW_PACKER_IS_PROTECTED() SHADOW_VMP_IS_PROTECTED()
#define SHADOW_PACKER_IS_DEBUGGER() SHADOW_VMP_IS_DEBUGGER()
#define SHADOW_PACKER_IS_VM() SHADOW_VMP_IS_VM()

#else
// ─── NONE backend — dev билд, naked MSVC ───
#define SHADOW_PROTECT_ULTRA(name)                                             \
  do {                                                                         \
  } while (0)
#define SHADOW_PROTECT_VIRT(name)                                              \
  do {                                                                         \
  } while (0)
#define SHADOW_PROTECT_MUT(name)                                               \
  do {                                                                         \
  } while (0)
#define SHADOW_PROTECT_END()                                                   \
  do {                                                                         \
  } while (0)

#define SHADOW_PACKER_IS_PROTECTED() (false)
#define SHADOW_PACKER_IS_DEBUGGER() (false)
#define SHADOW_PACKER_IS_VM() (false)

inline void VMP_SanityCheck() { /* no-op in dev */ }
#endif
