#include "security/xorstr.hpp"
#include "globals.hpp"

#include "../core/entity_list.hpp"
#include "../driver/driver_io.hpp"
#include "../features/aimbot/aimbot.hpp"
#include "../features/aimbot/trigger.hpp"
#include "../features/misc/anti_afk.hpp"
#include "../features/misc/player.hpp"
#include "../features/misc/tp_waypoint.hpp"
#include "../memory/memory.hpp"
#include "../render/overlay.hpp"
#include "../utils/console.hpp"
#include "../utils/logger.hpp"
#include "../security/security.hpp"
#include "../security/vmp_markers.hpp"
#include "../security/shared_ipc.hpp"
#include "../config/config.hpp"
#include <chrono>
#include <thread>

HINSTANCE g_cheat_base = nullptr;

static void WriteCrashLog(const char* msg)
{
    // Logging removed
}

// Fallback для крашей через TerminateProcess / __fastfail которые обходят VEH
static LONG WINAPI ShadowUnhandledFilter(PEXCEPTION_POINTERS pExc)
{
    // Logging removed
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI ShadowVectoredExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo)
{
    if (pExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT ||
        pExceptionInfo->ExceptionRecord->ExceptionCode == 0x406D1388)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Logging removed

    ExitProcess(1);
    return EXCEPTION_EXECUTE_HANDLER;
}

// C2712 fix: __try нельзя использовать в функции с C++ объектами (деструкторами).
// Передаём уже готовые std::string* снаружи — здесь никаких C++ объектов нет.
static DWORD SecurityInitSafe(std::string* tok, std::string* hwd)
{
    __try {
        shadow::security::Init(*tok, *hwd);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}



// Глобальная функция логгирования удалена (оставлена пустой для обратной совместимости, если где-то забыли убрать вызов)
// Но мы уже везде удалили, просто чтобы избежать ошибок линковки на случай чего.
void shadow_log(const char* msg) {
}

DWORD WINAPI CheatThread(LPVOID lpParam) {
  SHADOW_PROTECT_MUT(xorstr_("CheatThread"));
  SetProcessDPIAware();

  shadow_log("[1] CheatThread started");
  slog::info(xorstr_("shadow cheats starting..."));

  ShadowSharedAuth auth = {};
  if (!ReadSharedAuth(auth)) {
      shadow_log("[!] Unauthorized execution: Shared Memory IPC not found. Terminating.");
      ExitProcess(0);
      SHADOW_PROTECT_END();
      return -1;
  }

  std::string token = auth.token;
  std::string hwid = auth.hwid;
  std::string username = auth.username;

  // ── Диагностика аутентификации из Shared Memory ──
  {
    char dbg_auth[160];
    snprintf(dbg_auth, sizeof(dbg_auth), "[IPC] auth OK: user=%s hwid=%.16s... days=%d",
             username.c_str(), hwid.c_str(), auth.days_remaining);
    shadow_log(dbg_auth);
  }
  shadow_log("[2] Before security::Init");
  {
    DWORD sec_exc = SecurityInitSafe(&token, &hwid);
    if (sec_exc != 0) {
      char exc_buf[80];
      snprintf(exc_buf, sizeof(exc_buf), "[!!] security::Init SEH exception: 0x%08X", sec_exc);
      shadow_log(exc_buf);
      WriteCrashLog(exc_buf);
      WriteCrashLog(" (crash in security::Init)\n");
      SHADOW_PROTECT_END();
      return -1;
    }
  }
  shadow_log("[3] After security::Init");

  // Инициализируем систему конфигов с переданными учетными данными
  config_system::set_creds(token, hwid, username);
  config_system::init();

  std::thread([token, hwid]() { shadow::security::SecurityLoop(token, hwid); }).detach();

  shadow_log("[4] Before find_driver");
  if (!driver::find_driver()) {
    shadow_log("[!] find_driver FAILED - driver not found");
    SHADOW_PROTECT_END();
    return -1;
  }
  shadow_log("[5] Driver found OK");

  if (driver::protect_self()) {
    shadow_log("[6] protect_self OK");
  } else {
    shadow_log("[6] protect_self FAILED (non-critical)");
  }

  shadow_log("[7] Before memory::init - waiting for GTA5.exe");
  if (!memory::init()) {
    shadow_log("[!] memory::init FAILED");
    SHADOW_PROTECT_END();
    return -1;
  }
  shadow_log("[8] memory::init OK");

  if (driver::protect_self()) {
    slog::info(xorstr_("anti-dump protection activated."));
  } else {
    slog::error(xorstr_("failed to activate anti-dump protection."));
  }

  slog::info(xorstr_("memory subsystem ready."));

  g.Run = true;
  shadow_log("[9] g.Run = true, starting feature threads");

  std::thread([] { features::UpdateEntityListLoop(); }).detach();
  shadow_log("[9.1] UpdateEntityListLoop launched");
  std::thread([] { features::aimbot_thread(); }).detach();
  shadow_log("[9.2] aimbot_thread launched");
  std::thread([] { features::TriggerLoop(); }).detach();
  shadow_log("[9.3] TriggerLoop launched");
  std::thread([] { features::AntiAfkLoop(); }).detach();
  shadow_log("[9.4] AntiAfkLoop launched");
  std::thread([] { features::PlayerLoop(); }).detach();
  shadow_log("[9.5] PlayerLoop launched");

  std::thread([] {
    while (g.Run) {
      // Ждём пока текущий процесс игры жив
      HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, driver::process_id);
      if (hProcess) {
        WaitForSingleObject(hProcess, INFINITE);
        CloseHandle(hProcess);
      } else {
        // Не смогли открыть хендл — просто проверяем периодически
        while (g.Run) {
          if (driver::find_process(L"GTA5.exe") != driver::process_id)
            break;
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
      }

      if (!g.Run) break;

      // Игра закрылась/крашнула — ждём пока она снова запустится
      slog::info(xorstr_("Game process ended. Waiting for restart..."));

      // Signal threads: game is gone, don't try to read its memory
      driver::process_id = 0;
      g.ImageBase = 0;
      // Zero the ptr (not the pointer itself) so entity_list/ESP skip gracefully
      {
        std::lock_guard<std::mutex> lock(g.EntityListMutex);
        if (g.localplayer) g.localplayer->ptr = 0;
      }

      DWORD new_pid = 0;
      while (g.Run) {
        new_pid = driver::find_process(L"GTA5.exe");
        if (new_pid != 0) break;
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }

      if (!g.Run) break;

      // Игра снова запустилась — переподключаемся
      driver::process_id = new_pid;
      uintptr_t new_base = driver::find_image();
      if (new_base == 0) {
        // Не смогли получить базу — игра ещё не готова, ждём
        for (int i = 0; i < 30 && g.Run; i++) {
          std::this_thread::sleep_for(std::chrono::seconds(1));
          new_base = driver::find_image();
          if (new_base != 0) break;
        }
      }

      if (new_base != 0) {
        g.ImageBase = new_base;
        slog::info(xorstr_("Game restarted. Reconnected successfully."));
      }
      // Если не смогли получить базу — продолжаем работать, нитки сами восстановятся
    }
  }).detach();

  // TP to waypoint checker
  std::thread([] {
    while (g.Run) {
      static bool suicide_pressed = false;
      if (cfg::suicide_key != 0 && (GetAsyncKeyState(cfg::suicide_key) & 0x8000)) {
        if (!suicide_pressed) {
          if (g.localplayer && g.localplayer->IsValid()) {
            write<float>(g.localplayer->ptr + offsets::ped::Health, 0.0f);
          }
          suicide_pressed = true;
          if (cfg::suicide_key == 1) cfg::suicide_key = 0;
        }
      } else {
        suicide_pressed = false;
      }

      static bool tp_pressed = false;
      if (cfg::tp_waypoint_key != 0 && (GetAsyncKeyState(cfg::tp_waypoint_key) & 0x8000)) {
        if (!tp_pressed) {
          features::TeleportToWaypoint();
          tp_pressed = true;
          if (cfg::tp_waypoint_key == 1) cfg::tp_waypoint_key = 0;
        }
      } else {
        tp_pressed = false;
      }

      static bool unload_pressed = false;
      if (cfg::unload_cheat_key != 0 && (GetAsyncKeyState(cfg::unload_cheat_key) & 0x8000)) {
        if (!unload_pressed) {
          PostQuitMessage(0); // Signals the message loop in overlay to quit
          unload_pressed = true;
          if (cfg::unload_cheat_key == 1) cfg::unload_cheat_key = 0;
        }
      } else {
        unload_pressed = false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }).detach();

  shadow_log("[10] Entering overlay::Render()");
  overlay::Render();
  shadow_log("[11] overlay::Render() returned");

  g.Run = false;
  overlay::Shutdown();
  SHADOW_PROTECT_END();
  return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    SHADOW_PROTECT_MUT("DllMain");
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        AddVectoredExceptionHandler(1, ShadowVectoredExceptionHandler);
        SetUnhandledExceptionFilter(ShadowUnhandledFilter); // fallback для __fastfail и TerminateProcess
        
        g_cheat_base = hModule;
        DisableThreadLibraryCalls(hModule);
        
        HANDLE hThread = CreateThread(nullptr, 0, CheatThread, nullptr, 0, nullptr);
        if (hThread) {
            CloseHandle(hThread);
        }
    }
    SHADOW_PROTECT_END();
    return TRUE;
}


