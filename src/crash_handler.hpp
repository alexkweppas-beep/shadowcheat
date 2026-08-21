#pragma once
/*
 * crash_handler.hpp — перехват необработанных исключений лоадера
 *
 * Logging DISABLED — shadow_crash_*.log not written to disk.
 * Install() still exists so call sites compile correctly.
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace crash_handler {

// ─── Человекочитаемое описание кода исключения ────────────────────────────
static const char *ExceptionName(DWORD code) {
  switch (code) {
  case EXCEPTION_ACCESS_VIOLATION:
    return "ACCESS_VIOLATION";
  case EXCEPTION_ILLEGAL_INSTRUCTION:
    return "ILLEGAL_INSTRUCTION";
  case EXCEPTION_STACK_OVERFLOW:
    return "STACK_OVERFLOW";
  case EXCEPTION_INT_DIVIDE_BY_ZERO:
    return "INT_DIVIDE_BY_ZERO";
  case EXCEPTION_PRIV_INSTRUCTION:
    return "PRIV_INSTRUCTION";
  case EXCEPTION_IN_PAGE_ERROR:
    return "IN_PAGE_ERROR";
  case EXCEPTION_BREAKPOINT:
    return "BREAKPOINT";
  case EXCEPTION_SINGLE_STEP:
    return "SINGLE_STEP";
  case 0xC0000374:
    return "HEAP_CORRUPTION";
  case 0xE06D7363:
    return "C++ EXCEPTION";
  default:
    return "UNKNOWN";
  }
}

// ─── Главный обработчик исключений (silent — no file written) ─────────────
static LONG WINAPI ShadowCrashHandler(EXCEPTION_POINTERS *ep) {
  if (!ep || !ep->ExceptionRecord)
    return EXCEPTION_CONTINUE_SEARCH;
  DWORD code = ep->ExceptionRecord->ExceptionCode;
  // Пропускаем breakpoint/step
  if (code == EXCEPTION_BREAKPOINT || code == EXCEPTION_SINGLE_STEP)
    return EXCEPTION_CONTINUE_SEARCH;
  
  // File logging removed

  return EXCEPTION_EXECUTE_HANDLER;
}

// ─── Установить обработчик ────────────────────────────────────────────────
static void Install() { SetUnhandledExceptionFilter(ShadowCrashHandler); }

} // namespace crash_handler
