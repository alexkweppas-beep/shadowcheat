#define _CRT_SECURE_NO_WARNINGS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>

#pragma comment(lib, "Crypt32.lib")
#include "anti_forensics.hpp"
#include "antidebug.hpp"
#include "ban_reporter.hpp"
#include "cleanup.hpp"
#include "crash_handler.hpp"
#include "driver_ctl.hpp"
#include "hwid.hpp"
#include "loader_silent.hpp"
#include "mapper/intel_driver.hpp"
#include "mapper/kdmapper.hpp"
#include "mapper/launcher.hpp"
#include "mapper/remote_inject.hpp"
#include "network.hpp"
#include "resource.h"
#include "streamproof.hpp"
#include "syscalls.hpp"
#include "ui_imgui.hpp"
#include "vmp_markers.hpp"
#include "xorstr.hpp"
#include <cstring>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <shobjidl.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <wincrypt.h>
#include <windowsx.h>


// ImGui font — Inter Bold из alt:v проекта
#include "inter_font.hpp"
#include "shared_ipc.hpp"

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

// ─────────────────────────────────────────────────────────
// TLS Callback — anti-debug ДО WinMain
// ─────────────────────────────────────────────────────────
static void NTAPI ShadowLoaderTls(PVOID, DWORD dwReason, PVOID) {
  if (dwReason != DLL_PROCESS_ATTACH)
    return;
  if (shadow::loader_antidbg::tls_detect_ntdll_hooks())
    shadow::loader_antidbg::kill_local();
  if (shadow::loader_antidbg::is_being_debugged(false))
    shadow::loader_antidbg::kill_local();
}
#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma const_seg(".CRT$XLB")
extern "C" const PIMAGE_TLS_CALLBACK shadow_loader_tls_cb = ShadowLoaderTls;
#pragma const_seg()

// ─────────────────────────────────────────────────────────
// Глобальное состояние (идентично оригинальному main.cpp)
// ─────────────────────────────────────────────────────────
static std::string g_username = "";
static HANDLE g_hSharedMemory = NULL;

static bool CreateOrUpdateSharedAuth(const std::string &username,
                                     const std::string &token,
                                     const std::string &hwid, int days) {
  if (!g_hSharedMemory) {
    g_hSharedMemory =
        CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                           sizeof(ShadowSharedAuth), SHADOW_SHARED_MEMORY_NAME);
  }
  if (!g_hSharedMemory)
    return false;

  void *pView = MapViewOfFile(g_hSharedMemory, FILE_MAP_ALL_ACCESS, 0, 0,
                              sizeof(ShadowSharedAuth));
  if (!pView)
    return false;

  ShadowSharedAuth auth = {};
  auth.magic = SHADOW_SHARED_MAGIC;
  auth.version = 1;
  auth.timestamp = GetTickCount64();
  auth.days_remaining = days;
  auth.pid = GetCurrentProcessId();
  strncpy_s(auth.username, sizeof(auth.username), username.c_str(), _TRUNCATE);
  strncpy_s(auth.token, sizeof(auth.token), token.c_str(), _TRUNCATE);
  strncpy_s(auth.hwid, sizeof(auth.hwid), hwid.c_str(), _TRUNCATE);

  memcpy(pView, &auth, sizeof(ShadowSharedAuth));
  UnmapViewOfFile(pView);
  return true;
}

static constexpr size_t CRED_BUF = 512;
static uint8_t g_token_enc[CRED_BUF] = {};
static uint8_t g_hwid_enc[CRED_BUF] = {};
static bool g_token_valid = false;
static bool g_hwid_valid = false;

static constexpr size_t CHAL_BUF = 128;
static uint8_t g_challenge_enc[CHAL_BUF] = {};
static bool g_challenge_valid = false;

static inline bool store_credential(uint8_t *buf, size_t sz,
                                    const std::string &val, bool &flag) {
  if (val.size() >= sz)
    return false;
  SecureZeroMemory(buf, sz);
  memcpy(buf, val.data(), val.size());
  flag = !!CryptProtectMemory(buf, (DWORD)sz, CRYPTPROTECTMEMORY_SAME_PROCESS);
  return flag;
}
static inline std::string load_credential(uint8_t *buf, size_t sz) {
  uint8_t tmp[512] = {};
  if (sz > sizeof(tmp))
    return {};
  memcpy(tmp, buf, sz);
  if (!CryptUnprotectMemory(tmp, (DWORD)sz, CRYPTPROTECTMEMORY_SAME_PROCESS)) {
    SecureZeroMemory(tmp, sizeof(tmp));
    return {};
  }
  std::string r = reinterpret_cast<char *>(tmp);
  SecureZeroMemory(tmp, sizeof(tmp));
  return r;
}

static constexpr size_t SKEY_HEX_BUF = 80;
static uint8_t g_session_key_hex_enc[SKEY_HEX_BUF] = {};
static bool g_session_key_hex_protected = false;

static inline bool protect_session_key_hex() {
  return !!CryptProtectMemory(g_session_key_hex_enc, SKEY_HEX_BUF,
                              CRYPTPROTECTMEMORY_SAME_PROCESS);
}
static inline std::string unprotect_session_key_hex() {
  uint8_t tmp[SKEY_HEX_BUF] = {};
  memcpy(tmp, g_session_key_hex_enc, SKEY_HEX_BUF);
  if (!CryptUnprotectMemory(tmp, SKEY_HEX_BUF,
                            CRYPTPROTECTMEMORY_SAME_PROCESS)) {
    SecureZeroMemory(tmp, sizeof(tmp));
    return {};
  }
  std::string result = reinterpret_cast<char *>(tmp);
  SecureZeroMemory(tmp, sizeof(tmp));
  return result;
}
static inline bool store_session_key_hex(const std::string &hex) {
  if (hex.size() > SKEY_HEX_BUF - 1)
    return false;
  SecureZeroMemory(g_session_key_hex_enc, sizeof(g_session_key_hex_enc));
  memcpy(g_session_key_hex_enc, hex.data(), hex.size());
  if (!protect_session_key_hex()) {
    SecureZeroMemory(g_session_key_hex_enc, sizeof(g_session_key_hex_enc));
    return false;
  }
  g_session_key_hex_protected = true;
  return true;
}

static std::string g_product_id;
static std::string g_integrity_hash_expected;
static std::vector<uint8_t> g_pending_cheat;
static Mapper::MapResult g_mapped;

// The internal product is a DLL mapped into GTA5.exe from user mode. It needs
// no driver, so the whole driver load/handshake is skipped for it.
static bool IsInternalProduct(const std::string &pid) {
  return pid == xorstr_("internal") || pid == xorstr_("ragemp_internal") ||
         pid == xorstr_("internal_ragemp");
}

// Список активных подписок — заполняется в DoLogin из ответа сервера
static std::vector<Network::AuthSubscription> g_auth_subscriptions;

// ─── Trigger / Silent IPC ───
// Хранятся прямо в памяти лоадера, читаются тредами триггера/сайлента
#include "shared_ipc.hpp"
static TriggerSharedSettings g_trigger_settings = {};
static std::atomic<bool> g_trigger_thread_run{false};
static std::atomic<bool> g_silent_thread_run{false};
static bool g_is_trigger_product = false; // загружен ли trigger продукт

// ─ Платформы ─
enum class GtaPlatform { Unknown = 0, AltV, RageMP };
static GtaPlatform g_gta_platform = GtaPlatform::Unknown;

// ─ Оффсеты (заполняются после определения платформы) ─
static uintptr_t TRIGGER_OFFSET = 0; // заполняется в DetectPlatformAndOffsets
static uintptr_t SILENT_OFFSET = 0;

// ─ Таблица оффсетов по платформам ─
struct PlatformOffsets {
  uintptr_t trigger;
  uintptr_t silent;
};
static constexpr PlatformOffsets OFFSETS_RAGEMP = {0x202C8E0, 0x103891E};
static constexpr PlatformOffsets OFFSETS_ALTV = {0x203F8F0, 0x1044482};

// ─── Определить платформу по классу окна GTA5 ───
// grcWindow = Alt:V; любой другой = RageMP
static GtaPlatform DetectPlatformAndOffsets(DWORD game_pid) {
  struct FindCtx {
    DWORD pid;
    GtaPlatform result;
  };
  FindCtx ctx{game_pid, GtaPlatform::Unknown};

  EnumWindows(
      [](HWND hwnd, LPARAM lp) -> BOOL {
        auto *c = reinterpret_cast<FindCtx *>(lp);
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid != c->pid)
          return TRUE;
        char cls[256] = {};
        GetClassNameA(hwnd, cls, sizeof(cls));
        if (cls[0] == '\0')
          return TRUE;
        if (strcmp(cls, "grcWindow") == 0)
          c->result = GtaPlatform::AltV;
        else
          c->result = GtaPlatform::RageMP;
        return FALSE; // нашли окно — стоп
      },
      reinterpret_cast<LPARAM>(&ctx));

  // Если окно не нашли — фолбэк на RageMP
  if (ctx.result == GtaPlatform::Unknown)
    ctx.result = GtaPlatform::RageMP;

  g_gta_platform = ctx.result;
  const auto &off =
      (ctx.result == GtaPlatform::AltV) ? OFFSETS_ALTV : OFFSETS_RAGEMP;
  TRIGGER_OFFSET = off.trigger;
  SILENT_OFFSET = off.silent;

  return ctx.result;
}

// ─ Чтение/запись памяти игры из юзермода. Драйвер больше не нужен: внешний
// продукт читает GTA5 через процесс-хэндл, как и internal-инжектор. ─
static HANDLE g_TriggerGameHandle = nullptr;
static DWORD g_TriggerGamePid = 0;

static HANDLE TriggerGameHandle(int32_t game_pid) {
  if (g_TriggerGameHandle && g_TriggerGamePid == (DWORD)game_pid)
    return g_TriggerGameHandle;
  if (g_TriggerGameHandle) {
    CloseHandle(g_TriggerGameHandle);
    g_TriggerGameHandle = nullptr;
  }
  g_TriggerGameHandle =
      OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION,
                  FALSE, (DWORD)game_pid);
  g_TriggerGamePid = (DWORD)game_pid;
  return g_TriggerGameHandle;
}

template <typename T> static T TriggerReadVM(int32_t game_pid, uint64_t addr) {
  T buf{};
  HANDLE h = TriggerGameHandle(game_pid);
  if (!h)
    return buf;
  SIZE_T read = 0;
  ReadProcessMemory(h, (LPCVOID)(uintptr_t)addr, &buf, sizeof(T), &read);
  return buf;
}

template <typename T>
static void TriggerWriteVM(int32_t game_pid, uint64_t addr, T val) {
  HANDLE h = TriggerGameHandle(game_pid);
  if (!h)
    return;
  SIZE_T written = 0;
  WriteProcessMemory(h, (LPVOID)(uintptr_t)addr, &val, sizeof(T), &written);
}

// ─ Поиск PID игры по имени процесса ─
static DWORD FindProcessByName(const wchar_t *name) {
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE)
    return 0;
  PROCESSENTRY32W pe = {sizeof(pe)};
  if (Process32FirstW(snap, &pe)) {
    do {
      if (_wcsicmp(pe.szExeFile, name) == 0) {
        CloseHandle(snap);
        return pe.th32ProcessID;
      }
    } while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
  return 0;
}

// ─ Получить ImageBase процесса (NtQueryInformationProcess / PEB) ─
static uint64_t GetProcessBase(DWORD pid) {
  // Используем драйвер для получения базы, чтобы обойти античиты, блокирующие
  // ReadProcessMemory
  uint64_t base = shadow_driver_ctl::DriverGetImageBase(pid);
  if (base != 0)
    return base;

  // Фолбэк на юзермод, если драйвер не ответил (или не инициализирован для
  // чтения)
  HANDLE hProc = OpenProcess(
      PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
  if (!hProc) {
    // Пробуем без PROCESS_VM_READ, если античит блочит (но ReadProcessMemory
    // ниже скорее всего упадет)
    hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  }
  if (!hProc)
    return 0;

  // PEB address
  PROCESS_BASIC_INFORMATION pbi = {};
  typedef NTSTATUS(WINAPI * fn_NtQIP)(HANDLE, int, PVOID, ULONG, PULONG);
  fn_NtQIP NtQIP = (fn_NtQIP)GetProcAddress(GetModuleHandleA("ntdll.dll"),
                                            "NtQueryInformationProcess");
  if (!NtQIP) {
    CloseHandle(hProc);
    return 0;
  }
  NtQIP(hProc, 0, &pbi, sizeof(pbi), nullptr);

  // Read PEB.ImageBaseAddress
  ReadProcessMemory(hProc, (LPVOID)((uint64_t)pbi.PebBaseAddress + 0x10), &base,
                    sizeof(base), nullptr);
  CloseHandle(hProc);
  return base;
}

// ─── Тред триггера ───
static void TriggerThread() {
  bool is_holding = false;
  while (g_trigger_thread_run.load()) {
    if (!g_trigger_settings.trigger_enabled ||
        !g_trigger_settings.cheat_active) {
      if (is_holding) {
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
        is_holding = false;
      }
      Sleep(10);
      continue;
    }

    int32_t pid = g_trigger_settings.game_pid;
    uint64_t base = g_trigger_settings.game_base;
    int bind_vk = g_trigger_settings.trigger_bind_vk;
    int delay_ms = g_trigger_settings.trigger_delay_ms;

    if (pid == 0 || base == 0) {
      // Ищем игру
      DWORD gpid = FindProcessByName(L"GTA5.exe");
      if (!gpid)
        gpid = FindProcessByName(L"GTAVLauncher.exe");
      if (gpid) {
        uint64_t gbase = GetProcessBase(gpid);
        g_trigger_settings.game_pid = (int32_t)gpid;
        g_trigger_settings.game_base = gbase;
        shadow_driver_ctl::DriverSetTarget(gpid);
        // ─ Определяем платформу и выбираем оффсеты ─
        DetectPlatformAndOffsets(gpid);
      }
      Sleep(1000);
      continue;
    }

    bool bind_held =
        (bind_vk == 0) ? true : ((GetAsyncKeyState(bind_vk) & 0x8000) != 0);
    if (bind_held) {
      BYTE triggerState = TriggerReadVM<BYTE>(pid, base + TRIGGER_OFFSET);
      if (triggerState == 1 || triggerState == 2) {
        if (!is_holding) {
          if (delay_ms > 0)
            Sleep(delay_ms);
          mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
          is_holding = true;
        }
      } else if (is_holding) {
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
        is_holding = false;
      }
    } else if (is_holding) {
      mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
      is_holding = false;
    }
    Sleep(1);
  }
  if (is_holding)
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
}

// ─── Тред сайлент айма ───
static void SilentThread() {
  while (g_silent_thread_run.load()) {
    if (!g_trigger_settings.has_silent || !g_trigger_settings.silent_enabled ||
        !g_trigger_settings.cheat_active) {

      // Если выключили через UI, но патч ещё висит в игре - очистим его
      if (loader_silent::ctx_silent.isPatched &&
          g_trigger_settings.game_pid != 0) {
        loader_silent::SilentCleanup(g_trigger_settings.game_pid,
                                     g_trigger_settings.game_base);
      }
      Sleep(10);
      continue;
    }

    int32_t pid = g_trigger_settings.game_pid;
    uint64_t base = g_trigger_settings.game_base;
    int bind_vk = g_trigger_settings.silent_bind_vk;
    float fov = g_trigger_settings.silent_fov;
    int hitbox = g_trigger_settings.silent_hitbox;

    if (pid == 0 || base == 0) {
      Sleep(500);
      continue;
    }

    bool bind_held =
        (bind_vk == 0) ? true : ((GetAsyncKeyState(bind_vk) & 0x8000) != 0);

    // Вызываем логику сайлента
    loader_silent::SilentAimTick(pid, base, bind_held, fov, hitbox);

    Sleep(5);
  }

  if (g_trigger_settings.game_pid != 0) {
    loader_silent::SilentCleanup(g_trigger_settings.game_pid,
                                 g_trigger_settings.game_base);
  }
}

static volatile uintptr_t g_running_counter = 0;
static volatile uintptr_t g_running_seed = 0;
static constexpr uint8_t RUNNING_MAGIC = 0xA7;

static void set_cheat_running(bool running) {
  g_running_seed = (uintptr_t)__rdtsc() ^ (uintptr_t)GetTickCount64();
  if (running)
    g_running_counter = g_running_seed ^ RUNNING_MAGIC;
  else
    g_running_counter = g_running_seed ^ (~RUNNING_MAGIC & 0xFF);
}
static bool is_cheat_running() {
  volatile uintptr_t c = g_running_counter;
  volatile uintptr_t s = g_running_seed;
  return ((c ^ s) & 0xFF) == RUNNING_MAGIC;
}

static uint8_t g_session_key[32] = {};
static bool g_session_key_set = false;
static uint64_t g_dvr_session_key = 0;
static uint32_t g_loader_pid = 0;

// ─────────────────────────────────────────────────────────
// Версия лоадера — хардкодится в коде, обфусцируется xorstr_ + VMP mutation
// Изменить в бинарнике нельзя: xorstr_ шифрует на этапе компиляции,
// VMP дополнительно мутирует код вокруг.
// ─────────────────────────────────────────────────────────
static std::string GetLoaderVersion() {
  // Версия обфусцирована xorstr_ — в бинарнике как открытой строки нет
  std::string v = xorstr_("111");
  return v;
}

// ─────────────────────────────────────────────────────────
// Проверка интернета при старте (если нет — краш с ошибкой)
// ─────────────────────────────────────────────────────────
__declspec(noinline) static bool CheckInternetConnection() {
  SHADOW_PROTECT_ULTRA("CheckInternetConnection");
  // Быстрый ping /api/ping до 3 секунд
  std::string resp = Network::Ping();
  bool ok = !resp.empty() && resp.find(xorstr_("ok")) != std::string::npos;
  SHADOW_PROTECT_END();
  return ok;
}

// ─────────────────────────────────────────────────────────
// Named Pipe — лоадер отвечает читу на verify-запросы и выдаёт credentials
// ─────────────────────────────────────────────────────────
static HANDLE g_pipe_handle = INVALID_HANDLE_VALUE;
static std::atomic<bool> g_pipe_running{false};

// Лёгкое XOR-шифрование для pipe-канала (ключ = первый байт g_session_key)
// Не спасает от MITM на локалхосте, но затрудняет дамп RAM в момент передачи
static std::string PipeXorCrypt(const std::string &data, uint8_t key) {
  std::string out(data.size(), 0);
  uint8_t rolling = key;
  for (size_t i = 0; i < data.size(); i++) {
    out[i] = data[i] ^ rolling;
    rolling = (rolling * 0x41 + 0x0D) & 0xFF; // PRNG rolling key
  }
  return out;
}

static void LoaderPipeServer() {
  SHADOW_PROTECT_MUT("LoaderPipeServer");
  char pipe_name[64];
  snprintf(pipe_name, sizeof(pipe_name),
           xorstr_("\\\\.\\pipe\\ShadowLoader_%u"), GetCurrentProcessId());

  while (g_pipe_running.load()) {
    HANDLE hPipe =
        CreateNamedPipeA(pipe_name, PIPE_ACCESS_DUPLEX,
                         PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                         PIPE_UNLIMITED_INSTANCES, 4096, 4096, 5000, nullptr);
    if (hPipe == INVALID_HANDLE_VALUE) {
      Sleep(1000);
      continue;
    }

    BOOL connected = ConnectNamedPipe(hPipe, nullptr);
    if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
      CloseHandle(hPipe);
      continue;
    }

    // Читаем запрос от чита
    char buf[2048] = {};
    DWORD bytes_read = 0;
    if (ReadFile(hPipe, buf, sizeof(buf) - 1, &bytes_read, nullptr) &&
        bytes_read > 0) {
      std::string req(buf, bytes_read);

      // Формат запроса: "VERIFY:{token}:{hwid}"
      std::string resp;
      if (req.rfind(xorstr_("VERIFY:"), 0) == 0) {
        // Проверяем что лоадер всё ещё работает и токен валиден
        if (g_token_valid && g_hwid_valid)
          resp = xorstr_("OK");
        else
          resp = xorstr_("FAIL");
      } else {
        resp = xorstr_("UNKNOWN");
      }

      DWORD written = 0;
      WriteFile(hPipe, resp.data(), (DWORD)resp.size(), &written, nullptr);
    }

    FlushFileBuffers(hPipe);
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
  }
}

// ─────────────────────────────────────────────────────────
// LoadResourceToMemory / SecureDeleteFile (без изменений)
// ─────────────────────────────────────────────────────────
static std::vector<uint8_t> LoadResourceToMemory(WORD res_id) {
  HMODULE hMod = GetModuleHandle(nullptr);
  HRSRC hRes = FindResource(hMod, MAKEINTRESOURCE(res_id), RT_RCDATA);
  if (!hRes)
    return {};
  HGLOBAL hMem = LoadResource(hMod, hRes);
  if (!hMem)
    return {};
  void *data = LockResource(hMem);
  DWORD size = SizeofResource(hMod, hRes);
  if (!data || !size)
    return {};
  return std::vector<uint8_t>((uint8_t *)data, (uint8_t *)data + size);
}
static void SecureDeleteFile(const std::string &path) {
  HANDLE hFile = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile != INVALID_HANDLE_VALUE) {
    LARGE_INTEGER size;
    GetFileSizeEx(hFile, &size);
    std::vector<uint8_t> zeros(min((LONGLONG)4096, size.QuadPart), 0);
    DWORD dummy;
    LARGE_INTEGER zero_pos = {};
    SetFilePointerEx(hFile, zero_pos, nullptr, FILE_BEGIN);
    for (LONGLONG off = 0; off < size.QuadPart; off += zeros.size())
      WriteFile(hFile, zeros.data(), (DWORD)zeros.size(), &dummy, nullptr);
    SetEndOfFile(hFile);
    CloseHandle(hFile);
  }
  DeleteFileA(path.c_str());
}
static bool LoadDriverInMemory(const std::vector<uint8_t> &driver_bytes) {
  if (shadow_driver_ctl::OpenDriver()) {
    return true; // Driver is already loaded
  }

  if (!intel_driver::IsRunning()) {
    if (!NT_SUCCESS(intel_driver::Load()))
      return false;
  }

  NTSTATUS exitCode = 0;
  ULONG64 mapped_base = kdmapper::MapDriver(
      const_cast<uint8_t *>(driver_bytes.data()), 0, 0, false, true,
      kdmapper::AllocationMode::AllocatePool, false, nullptr, &exitCode);

  intel_driver::Unload();
  return mapped_base != 0;
}

// ─────────────────────────────────────────────────────────
// Phase1_Prepare / Phase2_Inject / UnloadAll — без изменений
// ─────────────────────────────────────────────────────────
static std::string ComputeTextSectionHash() {
  SHADOW_PROTECT_MUT("ComputeTextSectionHash");
  HMODULE hMod = GetModuleHandle(nullptr);
  if (!hMod)
    return xorstr_("NO_MOD");
  auto dos = (IMAGE_DOS_HEADER *)hMod;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    return xorstr_("BAD_DOS");
  auto nt = (IMAGE_NT_HEADERS *)((uint8_t *)hMod + dos->e_lfanew);
  auto sec = IMAGE_FIRST_SECTION(nt);
  for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
    if (strncmp((char *)sec->Name, ".text", 5) == 0) {
      uint8_t *base = (uint8_t *)hMod + sec->VirtualAddress;
      size_t size = sec->Misc.VirtualSize;
      uint32_t crc = 0xFFFFFFFF;
      for (size_t j = 0; j < size; j++) {
        crc ^= base[j];
        for (int k = 0; k < 8; k++)
          crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
      }
      crc = ~crc;
      char buf[16];
      snprintf(buf, sizeof(buf), xorstr_("%08X"), crc);
      return std::string(buf);
    }
  }
  return xorstr_("NO_TEXT");
}

// ─────────────────────────────────────────────────────────
// Вспомогательный класс для временной остановки DPS (обход SRUM)
// ─────────────────────────────────────────────────────────
struct DPSGuard {
  DPSGuard() {
    std::string cmd = xorstr_("cmd.exe /c net stop DPS /y");
    STARTUPINFOA si = {sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::vector<char> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(0);
    if (CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
      WaitForSingleObject(pi.hProcess, 15000);
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
    }
  }
  ~DPSGuard() {
    std::string cmd = xorstr_("cmd.exe /c net start DPS & wevtutil cl System & "
                              "wevtutil cl Application");
    STARTUPINFOA si = {sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    std::vector<char> cmd_buf(cmd.begin(), cmd.end());
    cmd_buf.push_back(0);
    if (CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
    }
  }
};

// Глобальный экземпляр, чтобы служба была остановлена на всё время работы
// лоадера
static std::unique_ptr<DPSGuard> g_dps_guard;
static int g_user_days = 0;

std::string DoLogin(const std::string &username, const std::string &password) {
  SHADOW_PROTECT_ULTRA("DoLogin");
  if (!shadow::syscall::init_syscalls())
    return xorstr_("error:Внутренняя ошибка инициализации. Пожалуйста, перезагрузите ПК и попробуйте снова.");
  HWID::Components comp = HWID::GetComponents();
  if (HWID::DetectSpoofer()) {
    shadow::ban::Init(xorstr_(""), xorstr_(""));
    shadow::ban::Trigger(shadow::ban::Reason::HWID_SPOOFER,
                         xorstr_("SPOOFER_PRE_LOGIN"));
    return xorstr_("error:Обнаружен HWID спуфер. Сбросьте HWID на сайте или используйте старое устройство.");
  }

  // Передаём версию лоадера в заголовке X-Loader-Version
  std::string loader_ver = GetLoaderVersion();

  Network::AuthResponse auth =
      Network::Login(username, password, comp.cpu, comp.disk, comp.guid,
                     comp.mac, comp.bios, comp.profile, loader_ver);
  if (!auth.ok)
    return xorstr_("error:") + (auth.error.empty()
                                    ? xorstr_("Неверный логин или пароль. Проверьте правильность введенных данных.")
                                    : auth.error);

  g_user_days = auth.days;

  // Сервер вернёт error:No active subscription если days<=0 — но на всякий
  // случай
  if (auth.days <= 0) {
    return xorstr_("error:Ваша подписка истекла или не активна. Продлите подписку на сайте.");
  }

  if (auth.session_key.size() != 64) {
    SecureZeroMemory(auth.token.data(), auth.token.size());
    SecureZeroMemory(auth.session_key.data(), auth.session_key.size());
    return xorstr_("error:Ошибка сессии. Пожалуйста, перезапустите лоадер.");
  }
  store_credential(g_token_enc, CRED_BUF, auth.token, g_token_valid);
  store_credential(g_hwid_enc, CRED_BUF, auth.hwid, g_hwid_valid);
  // Сохраняем подписки глобально для последующего использования в UI
  g_auth_subscriptions = std::move(auth.subscriptions);
  SecureZeroMemory(auth.token.data(), auth.token.size());
  SecureZeroMemory(auth.hwid.data(), auth.hwid.size());
  for (int i = 0; i < 32; i++) {
    unsigned int b;
    sscanf_s(auth.session_key.c_str() + i * 2, "%02x", &b);
    g_session_key[i] = (uint8_t)b;
  }
  g_session_key_set = true;
  memcpy(&g_dvr_session_key, g_session_key, sizeof(uint64_t));
  g_loader_pid = GetCurrentProcessId();
  if (!store_session_key_hex(auth.session_key)) {
    SecureZeroMemory(g_session_key, sizeof(g_session_key));
    SecureZeroMemory(auth.session_key.data(), auth.session_key.size());
    return xorstr_("error:Ошибка доступа к памяти. Перезапустите игру и лоадер от имени Администратора.");
  }
  SecureZeroMemory(auth.session_key.data(), auth.session_key.size());
  {
    std::string tok = load_credential(g_token_enc, CRED_BUF);
    std::string hid = load_credential(g_hwid_enc, CRED_BUF);
    g_username = username;
    CreateOrUpdateSharedAuth(username, tok, hid, auth.days);
    shadow::ban::Init(tok, hid, username);
    SecureZeroMemory(tok.data(), tok.size());
    SecureZeroMemory(hid.data(), hid.size());
  }
  shadow::ban::StartQueuePump();
  return xorstr_("ok");
}

// ────────────────────────────────────────────────────────────────
// Phase1 разбита на подфункции:
//   1A: Сеть  → MUT    (большой код, не критичный)
//   1B: Ключ  → ULTRA  (маленький, критичный — быстрая компиляция VMP)
//   1C: Драйвер → MUT  (большой код, совместимость)
//   1D: Хендшейк → ULTRA (маленький, аутентификация драйвера)
//   1E: Дешифр → ULTRA (маленький, работа с AES-ключом)
// ────────────────────────────────────────────────────────────────

// 1A: Скачаем зашифрованный чит с сервера [MUT — нетворк + большой код]
__declspec(noinline) static std::string Phase1A_DownloadCheat(
    const std::string &product_id, const HWID::Components &comp,
    std::vector<uint8_t> &out_encrypted, std::string &out_integrity,
    std::string &out_session_key) {
  SHADOW_PROTECT_MUT("Phase1A_DownloadCheat");
  std::string tok = load_credential(g_token_enc, CRED_BUF);
  std::string hid = load_credential(g_hwid_enc, CRED_BUF);
  std::string loader_ver = GetLoaderVersion();
  std::string result = Network::DownloadProduct(
      tok, hid, product_id, comp.cpu, comp.disk, comp.guid, comp.mac, comp.bios,
      comp.profile, out_encrypted, out_integrity, out_session_key,
      loader_ver); // X-Loader-Version на каждый запрос
  SecureZeroMemory(tok.data(), tok.size());
  SecureZeroMemory(hid.data(), hid.size());
  SHADOW_PROTECT_END();
  return result;
}

// 1A (альтернатива): Только инициализация сессии для trigger (без скачивания
// чита)
__declspec(noinline) static std::string
Phase1A_InitSession(const std::string &product_id,
                    std::string &out_session_key) {
  SHADOW_PROTECT_MUT("Phase1A_InitSession");
  std::string tok = load_credential(g_token_enc, CRED_BUF);
  std::string hid = load_credential(g_hwid_enc, CRED_BUF);
  std::string loader_ver = GetLoaderVersion();
  std::string result =
      Network::InitSession(tok, hid, product_id, out_session_key, loader_ver);
  SecureZeroMemory(tok.data(), tok.size());
  SecureZeroMemory(hid.data(), hid.size());
  SHADOW_PROTECT_END();
  return result;
}

// 1B: Парсим hex-ключ в g_session_key [MUT — маленький, очень критичный]
__declspec(noinline) static bool
Phase1B_ParseSessionKey(const std::string &hex_key) {
  SHADOW_PROTECT_MUT("Phase1B_ParseSessionKey");
  OBF_JUNK_BLOCK(volatile int dummy = 0;
                 for (int i = 0; i < 3; i++) dummy ^= (i * 0xDEAD););
  if (hex_key.size() != 64) {
    SHADOW_PROTECT_END();
    return false;
  }
  for (int i = 0; i < 32; i++) {
    unsigned int b = 0;
    sscanf_s(hex_key.c_str() + i * 2, "%02x", &b);
    g_session_key[i] = (uint8_t)b;
  }
  g_session_key_set = true;
  memcpy(&g_dvr_session_key, g_session_key, sizeof(uint64_t));
  store_session_key_hex(hex_key);
  SHADOW_PROTECT_END();
  return true;
}

// 1C: Скачиваем и загружаем драйвер [MUT — нетворк + LoadDriverInMemory =
// 1C: Скачиваем и загружаем драйвер [MUT]
// В самом начале проверяем: загружен ли драйвер (подключение по IOCTL
// \\.\monitorinspect). Если подключилось -> загрузка полностью скипается
// (return "ok"). Если нет -> скачиваем и маппим драйвер.
__declspec(noinline) static std::string
Phase1C_LoadDriver(const std::string &product_id) {
  SHADOW_PROTECT_MUT("Phase1C_LoadDriver");

  // 1. Проверка подключения к драйверу по IOCTL кодам
  if (shadow_driver_ctl::IsDriverLoaded()) {
    SHADOW_PROTECT_END();
    return xorstr_(
        "ok"); // Драйвер уже загружен и подключен — скипаем загрузку полностью!
  }

  // 2. Если драйвер не загружен — скачиваем с сервера
  std::vector<uint8_t> driver_bytes;
  {
    std::string tok = load_credential(g_token_enc, CRED_BUF);
    std::string hid = load_credential(g_hwid_enc, CRED_BUF);
    std::string loader_ver = GetLoaderVersion();
    std::string drv_res = Network::DownloadDriver(
        tok, hid, product_id, g_session_key, driver_bytes, loader_ver);
    SecureZeroMemory(tok.data(), tok.size());
    SecureZeroMemory(hid.data(), hid.size());
    if (drv_res != xorstr_("ok") || driver_bytes.empty()) {
      SHADOW_PROTECT_END();
      return xorstr_("error:Не удалось скачать компоненты. Проверьте интернет или отключите антивирус: ") + drv_res;
    }
  }

  // 3. Проверяем валидность PE-заголовка скачанного драйвера
  if (driver_bytes.size() < sizeof(IMAGE_DOS_HEADER)) {
    SHADOW_PROTECT_END();
    return xorstr_("error:Компонент поврежден. Отключите антивирус и перекачайте лоадер: ") +
           std::to_string(driver_bytes.size());
  }
  auto dos_hdr = (IMAGE_DOS_HEADER *)driver_bytes.data();
  if (dos_hdr->e_magic != IMAGE_DOS_SIGNATURE) {
    char b[16];
    snprintf(b, sizeof(b), "%04X", (unsigned)dos_hdr->e_magic);
    SHADOW_PROTECT_END();
    return xorstr_("error:Файл компонента поврежден. Отключите антивирус: 0x") + std::string(b) +
           xorstr_(" sz=") + std::to_string(driver_bytes.size());
  }

  // 4. Загружаем intel_driver (iqvw64e.sys) если не запущен
  if (!intel_driver::IsRunning()) {
    NTSTATUS st = intel_driver::Load();
    if (!NT_SUCCESS(st)) {
      SHADOW_PROTECT_END();
      return xorstr_("error:Загрузка заблокирована системой (Ошибка драйвера). Закройте игру, отключите Изоляцию Ядра (Core Isolation / VBS) и антивирусы. NTSTATUS=0x") + [st]() {
        char b[16];
        snprintf(b, sizeof(b), "%08X", (unsigned)st);
        return std::string(b);
      }();
    }
  }

  // 5. Маппим драйвер через kdmapper
  NTSTATUS exitCode = 0;
  ULONG64 mapped_base = kdmapper::MapDriver(
      driver_bytes.data(), 0, 0, false, true,
      kdmapper::AllocationMode::AllocatePool, false, nullptr, &exitCode);

  intel_driver::Unload();

  SecureZeroMemory(driver_bytes.data(), driver_bytes.size());
  driver_bytes.clear();
  SHADOW_PROTECT_END();

  if (mapped_base == 0) {
    return xorstr_("error:Ошибка запуска защиты. Возможен конфликт с FaceIT, Vanguard или антивирусом. Код: 0x") +
           [exitCode]() {
             char b[16];
             snprintf(b, sizeof(b), "%08X", (unsigned)exitCode);
             return std::string(b);
           }();
  }
  return xorstr_("ok");
}

// 1D: Handshake с драйвером [MUT — аутентификация от подделки]
__declspec(noinline) static bool Phase1D_DriverHandshake() {
  SHADOW_PROTECT_MUT("Phase1D_DriverHandshake");
  OBF_JUNK_BLOCK(volatile int handshake_junk = 0x1337;
                 handshake_junk += (int)GetTickCount(););
  bool ok = shadow_driver_ctl::DriverHandshake(g_dvr_session_key, g_loader_pid);
  SHADOW_PROTECT_END();
  return ok;
}

namespace shadow_cleanup {
bool EnsureDriverForCleanup() {
  if (shadow_driver_ctl::IsDriverLoaded()) {
    return true;
  }
  // Маппим драйвер (скачиваем с сервера под видом "trigger", т.к. драйвер один)
  if (Phase1C_LoadDriver(xorstr_("trigger")) != xorstr_("ok")) {
    return false;
  }
  // После загрузки обязательно делаем handshake, чтобы ядро получило наш
  // g_session_key
  return Phase1D_DriverHandshake();
}
} // namespace shadow_cleanup

// 1E: Дешифруем чит AES-GCM [MUT — работа с session_key, дешифровка ОЧЕНЬ
// тяжелая]
__declspec(noinline) static bool
Phase1E_Decrypt(std::vector<uint8_t> &encrypted) {
  SHADOW_PROTECT_MUT("Phase1E_Decrypt");
  g_pending_cheat.clear();
  bool ok = Network::DecryptPayload(encrypted, g_session_key, g_pending_cheat);
  SecureZeroMemory(encrypted.data(), encrypted.size());
  encrypted.clear();
  SecureZeroMemory(g_session_key, sizeof(g_session_key));
  g_session_key_set = false;
  SHADOW_PROTECT_END();
  return ok;
}

// ─── EOS null-AC gate ──────────────────────────────────────────────────────
// The game reads EOS_USE_ANTICHEATCLIENTNULL at startup; when it is 1, Epic
// Online Services uses the null anti-cheat client. It has to be set before
// GTA5 launches, so the gate only writes it while the game is closed.
static bool ReadMachineEnvVar(const wchar_t *name, std::wstring &out) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(
          HKEY_LOCAL_MACHINE,
          L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
          0, KEY_READ, &key) != ERROR_SUCCESS)
    return false;
  wchar_t buf[1024]{};
  DWORD size = sizeof(buf);
  DWORD type = 0;
  const LONG st = RegQueryValueExW(
      key, name, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size);
  RegCloseKey(key);
  if (st != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
    return false;
  out.assign(buf, size / sizeof(wchar_t));
  while (!out.empty() && out.back() == L'\0')
    out.pop_back();
  return true;
}

static bool WriteMachineEnvVar(const wchar_t *name, const wchar_t *value) {
  HKEY key = nullptr;
  if (RegOpenKeyExW(
          HKEY_LOCAL_MACHINE,
          L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
          0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
    return false;
  const LONG st = RegSetValueExW(
      key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(value),
      static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
  RegCloseKey(key);
  if (st != ERROR_SUCCESS)
    return false;
  // New processes inherit this only after a settings broadcast.
  SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                      reinterpret_cast<LPARAM>(L"Environment"),
                      SMTO_ABORTIFHUNG, 2000, nullptr);
  return true;
}

// Returns true when injection may proceed. Sets the EOS null-AC var when the
// game is closed; blocks when the game is open and the var is unset.
static bool EnsureEosNullAntiCheat(std::string &errOut) {
  std::wstring value;
  if (ReadMachineEnvVar(L"EOS_USE_ANTICHEATCLIENTNULL", value) &&
      value == L"1")
    return true;

  const bool gameRunning =
      FindProcessByName(L"GTA5.exe") != 0 ||
      FindProcessByName(L"ragemp_v.exe") != 0;
  if (gameRunning) {
    errOut = xorstr_(
        "error:Драйвер не запущен, закройте игру и запустите загрузку чита "
        "перед запуском игры");
    return false;
  }
  if (!WriteMachineEnvVar(L"EOS_USE_ANTICHEATCLIENTNULL", L"1")) {
    errOut = xorstr_(
        "error:Не удалось подготовить окружение. Запустите лоадер от "
        "администратора.");
    return false;
  }
  return true;
}

// Координатор Phase1 — нет своей VMP-защиты:
// VMP видит только вызовы 1A-1E, каждая защищена отдельно.
std::string Phase1_Prepare(const std::string &product_id) {
  g_product_id = product_id;

  // The EOS null-AC var must exist before the game starts, so this gate runs
  // before the download, not at inject time.
  {
    std::string gateErr;
    if (!EnsureEosNullAntiCheat(gateErr))
      return gateErr;
  }

  HWID::Components comp = HWID::GetComponents();

  // Шаг 1 [MUT]: скачать зашифрованный чит
  std::vector<uint8_t> encrypted;
  std::string integrity_hash, new_session_key;
  {
    std::string r = Phase1A_DownloadCheat(product_id, comp, encrypted,
                                          integrity_hash, new_session_key);
    if (r != xorstr_("ok"))
      return r;
  }
  g_integrity_hash_expected = integrity_hash;

  // Шаг 2 [ULTRA]: сохранить ключ шифрования
  if (!Phase1B_ParseSessionKey(new_session_key))
    return xorstr_("error:Ошибка сессии (Download). Пожалуйста, перезапустите лоадер.");
  SecureZeroMemory(new_session_key.data(), new_session_key.size());

  // Шаг 3 [MUT]: драйвер больше не грузится при подготовке. Все продукты
  // работают из юзермода: internal мапится в GTA5, external/trigger/silent
  // читают память через процесс-хэндл. Ядро осталось только для очистки следов
  // и загружается по требованию в момент очистки (EnsureDriverForCleanup).
  // Не грузить его здесь — значит не просить пользователя отключать HVCI и
  // список блокировки уязвимых драйверов.

  // Шаг 5 [ULTRA]: дешифровать чит AES-GCM
  if (!Phase1E_Decrypt(encrypted))
    return xorstr_("error:Ошибка расшифровки. Отключите антивирус и попробуйте снова.");

  return xorstr_("ok");
}

// ────────────────────────────────────────────────────────────────
// Phase2 разбита на подфункции:
//   2A: Патч кредов → ULTRA (маленький, патчит токен + ключ в DLL чита)
//   2B: Маппер  → MUT   (большой код, не критичный)
// ────────────────────────────────────────────────────────────────

// 2A: Патчим session_key + креды в чит [MUT — поиск и замена в памяти]
__declspec(noinline) static std::string Phase2A_PatchPayload() {
  SHADOW_PROTECT_MUT("Phase2A_PatchPayload");
  if (g_pending_cheat.empty()) {
    SHADOW_PROTECT_END();
    return xorstr_("error:Чит еще не загружен.");
  }
  shadow_driver_ctl::PatchCheatSessionKey(g_pending_cheat, g_dvr_session_key);
  std::string tok_cred = load_credential(g_token_enc, CRED_BUF);
  std::string hid_cred = load_credential(g_hwid_enc, CRED_BUF);
  CreateOrUpdateSharedAuth(g_username, tok_cred, hid_cred, g_user_days);
  shadow_driver_ctl::PatchCheatCredentials(g_pending_cheat, tok_cred, hid_cred);
  SecureZeroMemory(tok_cred.data(), tok_cred.size());
  SecureZeroMemory(hid_cred.data(), hid_cred.size());
  SHADOW_PROTECT_END();
  return xorstr_("ok");
}

// 2B: Маппинг чита в память [MUT — маппер = большой код, виртуализация была бы
// слишком]
__declspec(noinline) static std::string
Phase2B_MapCheat(const std::function<void(const char *, const std::string &)>
                     &report_error) {
  SHADOW_PROTECT_MUT("Phase2B_MapCheat");
  g_mapped = Mapper::MapFromMemory(g_pending_cheat);
  SecureZeroMemory(g_pending_cheat.data(), g_pending_cheat.size());
  g_pending_cheat.clear();
  if (!g_mapped.success) {
    report_error(xorstr_("MAPPER"), xorstr_("MapFromMemory failed"));
    SHADOW_PROTECT_END();
    return xorstr_("error:Не удалось внедрить чит в игру. Убедитесь, что игра полностью загружена.");
  }
  SHADOW_PROTECT_END();
  return xorstr_("ok");
}

std::string Phase2_Inject() {
  // Координатор — нет своей VMP-защиты:
  // VMP видит только вызовы 2A (ULTRA) и 2B (MUT) + хертбит (ULTRA).

  // ── Guard: не инжектить повторно пока чит уже работает ──────────────
  if (is_cheat_running())
    return xorstr_("error:Лоадер уже запущен. Проверьте панель задач.");

  auto report_error = [&](const char *phase, const std::string &msg) {
    std::string tok = load_credential(g_token_enc, CRED_BUF);
    std::string hid = load_credential(g_hwid_enc, CRED_BUF);
    std::string ver = GetLoaderVersion();
    std::thread([tok, hid, ver, phase, msg]() {
      Network::LogError(phase, msg, tok, hid, ver);
    }).detach();
    SecureZeroMemory(tok.data(), tok.size());
    SecureZeroMemory(hid.data(), hid.size());
  };

  if (shadow::loader_antidbg::is_being_debugged(true))
    shadow::loader_antidbg::kill_local();

  // Internal: мапим DLL в GTA5.exe из юзермода и выходим. Ни локального
  // маппинга в лоадер, ни драйвера, ни патча кредов под внешний чит — пейлоад
  // живёт внутри игры и сам читает cloud.session.
  if (IsInternalProduct(g_product_id)) {
    if (!RemoteInject::InjectIntoGame(g_pending_cheat)) {
      std::string err = xorstr_("error:Не удалось инжектить в GTA5: ") +
                        RemoteInject::LastError();
      report_error(xorstr_("PHASE2"), err);
      return err;
    }
    set_cheat_running(true);
    return xorstr_("ok");
  }

  // Шаг 1 [ULTRA]: патчим session_key + креды в чит
  {
    std::string r = Phase2A_PatchPayload();
    if (r != xorstr_("ok")) {
      report_error(xorstr_("PHASE2"), r);
      return r;
    }
  }

  // Шаг 2 [MUT]: маппинг чита
  {
    std::string r = Phase2B_MapCheat(report_error);
    if (r != xorstr_("ok"))
      return r;
  }

  set_cheat_running(true);

  // Хертбит-поток удален по запросу пользователя

  std::thread([report_error]() {
    bool launch_ok = Mapper::LaunchMapped(g_mapped);
    if (!launch_ok) {
      char buf[128];
      snprintf(buf, sizeof(buf), xorstr_("LaunchMapped FALSE GLE=%lu"),
               GetLastError());
      report_error(xorstr_("LAUNCH"), buf);
    }
  }).detach();

  return xorstr_("ok");
}

void UnloadAll() {
  SHADOW_PROTECT_ULTRA("UnloadAll");
  set_cheat_running(false);
  g_running_counter = 0;
  g_running_seed = 0;
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  if (g_mapped.success)
    Mapper::Unload(g_mapped);
  if (!g_pending_cheat.empty()) {
    SecureZeroMemory(g_pending_cheat.data(), g_pending_cheat.size());
    g_pending_cheat.clear();
  }
  if (g_session_key_set) {
    SecureZeroMemory(g_session_key, sizeof(g_session_key));
    g_session_key_set = false;
  }
  if (g_session_key_hex_protected) {
    SecureZeroMemory(g_session_key_hex_enc, sizeof(g_session_key_hex_enc));
    g_session_key_hex_protected = false;
  }
  shadow::syscall::free_syscall_stubs();
  shadow_driver_ctl::CloseDriver();

  // Восстанавливаем DPS при выгрузке всего
  if (g_dps_guard) {
    g_dps_guard.reset();
  }
}

// ─────────────────────────────────────────────────────────
// D3D11 globals
// ─────────────────────────────────────────────────────────
static ID3D11Device *g_pd3dDevice = nullptr;
static ID3D11DeviceContext *g_pd3dDeviceContext = nullptr;
static IDXGISwapChain *g_pSwapChain = nullptr;
static ID3D11RenderTargetView *g_mainRenderTargetView = nullptr;

static bool CreateD3D(HWND hwnd) {
  DXGI_SWAP_CHAIN_DESC sd = {};
  sd.BufferCount = 2;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  D3D_FEATURE_LEVEL fl;
  HRESULT hr = D3D11CreateDeviceAndSwapChain(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
      D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &fl,
      &g_pd3dDeviceContext);
  if (FAILED(hr))
    return false;

  ID3D11Texture2D *pBack = nullptr;
  g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBack));
  g_pd3dDevice->CreateRenderTargetView(pBack, nullptr, &g_mainRenderTargetView);
  pBack->Release();
  return true;
}
static void CleanupD3D() {
  if (g_mainRenderTargetView) {
    g_mainRenderTargetView->Release();
    g_mainRenderTargetView = nullptr;
  }
  if (g_pSwapChain) {
    g_pSwapChain->Release();
    g_pSwapChain = nullptr;
  }
  if (g_pd3dDeviceContext) {
    g_pd3dDeviceContext->Release();
    g_pd3dDeviceContext = nullptr;
  }
  if (g_pd3dDevice) {
    g_pd3dDevice->Release();
    g_pd3dDevice = nullptr;
  }
}

#define STB_IMAGE_IMPLEMENTATION
#include "images_data.hpp"
#include "stb_image.h"


#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

static ID3D11ShaderResourceView *
LoadTextureFromMemory(const unsigned char *image_data_in, int image_size,
                      int *out_width, int *out_height) {
  int image_width = 0, image_height = 0, image_channels = 0;
  unsigned char *image_data =
      stbi_load_from_memory(image_data_in, image_size, &image_width,
                            &image_height, &image_channels, 4);
  if (!image_data)
    return nullptr;

  // High quality CPU-side downsampling to fix aliasing/pixelation of 4K images
  int target_w = 824;
  int target_h =
      (int)((float)image_height * ((float)target_w / (float)image_width));
  if (image_width > target_w * 1.5f) {
    unsigned char *resized_data =
        (unsigned char *)malloc(target_w * target_h * 4);
    if (resized_data) {
      stbir_resize_uint8_linear(image_data, image_width, image_height, 0,
                                resized_data, target_w, target_h, 0,
                                STBIR_RGBA);
      stbi_image_free(image_data);
      image_data = resized_data;
      image_width = target_w;
      image_height = target_h;
    }
  }

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = image_width;
  desc.Height = image_height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  ID3D11Texture2D *pTexture = nullptr;
  D3D11_SUBRESOURCE_DATA subResource = {};
  subResource.pSysMem = image_data;
  subResource.SysMemPitch = desc.Width * 4;
  g_pd3dDevice->CreateTexture2D(&desc, &subResource, &pTexture);

  // Free using standard free if we resized, else stbi_image_free
  if (image_width == target_w && target_w != 0) {
    free(image_data);
  } else {
    stbi_image_free(image_data);
  }

  if (!pTexture)
    return nullptr;

  ID3D11ShaderResourceView *out_srv = nullptr;
  g_pd3dDevice->CreateShaderResourceView(pTexture, nullptr, &out_srv);
  pTexture->Release();

  if (out_width)
    *out_width = image_width;
  if (out_height)
    *out_height = image_height;
  return out_srv;
}

// ─────────────────────────────────────────────────────────
// Win32 Window Proc
// ─────────────────────────────────────────────────────────
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM,
                                                             LPARAM);

static LoaderUI *g_ui_ptr = nullptr;
static bool g_drag_active = false;
static POINT g_drag_start_cursor{};
static POINT g_drag_start_window{};

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
    return true;

  switch (msg) {
  case WM_LBUTTONDOWN: {
    // We let ImGui handle drags internally now via InvisibleButton in
    // ui_imgui.hpp
    break;
  }
  case WM_MOUSEMOVE:
    if (g_drag_active && (wParam & MK_LBUTTON)) {
      POINT cursor;
      GetCursorPos(&cursor);
      int dx = cursor.x - g_drag_start_cursor.x;
      int dy = cursor.y - g_drag_start_cursor.y;
      SetWindowPos(hWnd, nullptr, g_drag_start_window.x + dx,
                   g_drag_start_window.y + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
    break;
  case WM_LBUTTONUP:
    g_drag_active = false;
    ReleaseCapture();
    break;
  case WM_DESTROY:
    PostQuitMessage(0);
    break;
  }
  return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ─────────────────────────────────────────────────────────
// WinMain
// ─────────────────────────────────────────────────────────
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                      LPWSTR lpCmdLine, int nCmdShow) {
  // ── Инициализация перехвата крашей (пишет в %TEMP%) ──
  crash_handler::Install();

  SHADOW_PROTECT_MUT("LoaderMain");

  g_loader_pid = GetCurrentProcessId();

  // ── Anti-forensics: подавляем все трейсы ДО запуска UI
  //    Prefetch / USN Journal / AmCache / UserAssist / BAM / ETW
  shadow_af::Init();

  // ── Проверка интернета убрана отсюда, чтобы лоадер открывался мгновенно ──

  // ── Версия лоадера хардкодится в GetLoaderVersion() ──
  // Сессии убраны, UUID больше нет.
  std::string ver = GetLoaderVersion();
  if (ver.empty()) {
    MessageBoxW(0, L"Loader version not configured.", L"Shadow Loader",
                MB_OK | MB_ICONERROR | MB_TOPMOST);
    return 1;
  }

  // ── Начинаем Named Pipe Server в фоне ──
  g_pipe_running.store(true);
  std::thread(LoaderPipeServer).detach();

  // SetProcessDPIAware() to prevent Windows from scaling our UI
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  // Инициализируем блокировщик SRUM глобально на старте программы
  if (!g_dps_guard) {
    g_dps_guard = std::make_unique<DPSGuard>();
  }

  shadow::loader_antidbg::disable_etw();
  shadow::loader_antidbg::install_anti_attach();

  // Регистрируем класс окна
  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_CLASSDC;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = xorstr_(L"ShdwLdr"); // Имя класса обфусцировано
  RegisterClassExW(&wc);

  // Центрируем окно
  int sx = GetSystemMetrics(SM_CXSCREEN);
  int sy = GetSystemMetrics(SM_CYSCREEN);

  // Разрешение лоадера (как в оригинальном ui.html)
  int W = 860, H = 540;
  int wx = (sx - W) / 2, wy = (sy - H) / 2;

  HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, xorstr_(L"ShdwLdr"),
                              xorstr_(L"VoiceChanger64(2.02)"), WS_POPUP, wx,
                              wy, W, H, nullptr, nullptr, hInstance, nullptr);

  // Используем DWM для прозрачности фона (чтобы скругления ImGui были без
  // "лесенок")
  MARGINS margins = {-1, -1, -1, -1};
  DwmExtendFrameIntoClientArea(hwnd, &margins);

  // Отключаем скругление средствами Windows 11, так как мы рисуем свой
  // прозрачный фон
  DWMWINDOWATTRIBUTE dwmAttr = DWMWA_WINDOW_CORNER_PREFERENCE;
  DWM_WINDOW_CORNER_PREFERENCE pref = DWMWCP_DONOTROUND;
  DwmSetWindowAttribute(hwnd, dwmAttr, &pref, sizeof(pref));

  // ── NVIDIA Streamproof: предусловия при старте
  // ────────────────────────────── Только реестр (FTS DXGI mode) + патч
  // nvcontainer — без WDA на окне. WDA_EXCLUDEFROMCAPTURE применяется только по
  // кнопке в UI (fn_streamproof).
  shadow_streamproof::ApplyNvidiaFtsRegistry();
  shadow_streamproof::PatchNvContainerGetWindowAffinity();

  if (!CreateD3D(hwnd)) {
    UnregisterClassW(xorstr_(L"ShdwLdr"), hInstance);
    return 1;
  }

  ShowWindow(hwnd, SW_SHOWDEFAULT);
  UpdateWindow(hwnd);

  // ImGui init
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr; // не писать imgui.ini
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  // Стиль
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();
  style.WindowRounding = 0.f;
  style.WindowBorderSize = 0.f;
  style.FrameBorderSize = 1.f;
  style.PopupRounding = 10.f;
  style.WindowPadding = {0.f, 0.f};
  style.Colors[ImGuiCol_WindowBg] = Colors::BG;
  style.Colors[ImGuiCol_Border] = Colors::FIELD_BORDER;

  // Шрифты — Inter Bold из alt:v (inter_font.hpp содержит inter_bold[])
  ImFontConfig cfg;
  cfg.FontDataOwnedByAtlas = false;
  static ImWchar ranges[] = {0x0020, 0x00FF, 0x0400, 0x04FF,
                             0}; // Latin + Cyrillic
  ImFont *font_normal = io.Fonts->AddFontFromMemoryTTF(
      inter_bold, sizeof(inter_bold), 16.f, &cfg, ranges);
  ImFont *font_bold = io.Fonts->AddFontFromMemoryTTF(
      inter_bold, sizeof(inter_bold), 20.f, &cfg, ranges);
  ImFont *font_small = io.Fonts->AddFontFromMemoryTTF(
      inter_bold, sizeof(inter_bold), 14.f, &cfg, ranges);
  io.FontDefault = font_normal;

  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

  // UI State
  LoaderUI ui;
  ui.font_normal = font_normal;
  ui.font_bold = font_bold;
  ui.font_small = font_small;

  int tw, th;
  ui.bg_texture = (void *)LoadTextureFromMemory(
      image_bg_data, sizeof(image_bg_data), &tw, &th);
  ui.bg_size = {(float)tw, (float)th};

  ui.logo_texture = (void *)LoadTextureFromMemory(
      image_logo_data, sizeof(image_logo_data), &tw, &th);
  ui.logo_size = {(float)tw, (float)th};

  g_ui_ptr = &ui;

  // ── Привязываем бизнес-логику к UI ────────────────────
  // fn_prepare вызывается из отдельного потока чтобы не блокировать рендер
  ui.fn_prepare = [&](std::string pid, std::string user,
                      std::string pass) -> std::string {
    // Синхронизируем выбор сервера из UI перед скачиванием
    Network::g_server_region = ui.server_region;
    return Phase1_Prepare(pid);
  };
  ui.fn_inject = [&]() -> std::string { return Phase2_Inject(); };
  ui.fn_unload = [&]() { UnloadAll(); };
  bool is_hidden = false;
  ui.fn_close = [&]() {
    if (is_cheat_running()) {
      ShowWindow(hwnd, SW_HIDE);
      is_hidden = true;
    } else {
      ui.want_close = true;
    }
  };
  ui.fn_cleanup = [&]() {
    if (!shadow_cleanup::g_cleanup_running.load())
      shadow_cleanup::CleanupAsync();
  };

  static bool streamproof_enabled = false;
  ui.fn_streamproof = [hwnd]() {
    if (!streamproof_enabled) {
      // ВАЖНО: WDA нужно применять ПОСЛЕ изменения стилей,
      // иначе SetWindowLong сбрасывает SetWindowDisplayAffinity
      LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
      exStyle &= ~WS_EX_APPWINDOW;
      exStyle |= WS_EX_TOOLWINDOW;
      SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
      // WDA применяется последним — уже после изменения всех стилей
      shadow_streamproof::EnableStreamproof(hwnd);
      streamproof_enabled = true;
    } else {
      shadow_streamproof::DisableStreamproof(hwnd);
      LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
      exStyle &= ~WS_EX_TOOLWINDOW;
      exStyle |= WS_EX_APPWINDOW;
      SetWindowLong(hwnd, GWL_EXSTYLE, exStyle);
      streamproof_enabled = false;
    }
  };

  // ── Trigger: fn_enable / fn_disable ────────────────────────────────────────
  static std::thread g_trig_thread;
  static std::thread g_sil_thread;

  ui.fn_enable = [&]() {
    // fn_inject уже вызван выше (Phase2_Inject) — тут запускаем треды тригера
    // Для trigger продукта: Enable = активировать чит
    auto &prod = ui.products[ui.selected_idx];
    if (prod.id == xorstr_("trigger")) {
      g_is_trigger_product = true;
      // Синхронизируем настройки из UI
      g_trigger_settings.trigger_bind_vk = ui.trig_bind_vk;
      g_trigger_settings.trigger_delay_ms = ui.trig_delay_ms;
      g_trigger_settings.silent_bind_vk = ui.silent_bind_vk;
      g_trigger_settings.silent_fov = ui.silent_fov;
      g_trigger_settings.silent_hitbox = ui.silent_hitbox;
      g_trigger_settings.has_silent = prod.has_silent;
      g_trigger_settings.trigger_enabled = true;
      g_trigger_settings.silent_enabled = prod.has_silent;
      g_trigger_settings.cheat_active = true;
      g_trigger_settings.game_pid = 0; // тред найдёт сам
      g_trigger_settings.game_base = 0;

      if (!g_trigger_thread_run.load()) {
        g_trigger_thread_run.store(true);
        if (g_trig_thread.joinable())
          g_trig_thread.join();
        g_trig_thread = std::thread(TriggerThread);
      }
      if (prod.has_silent && !g_silent_thread_run.load()) {
        g_silent_thread_run.store(true);
        if (g_sil_thread.joinable())
          g_sil_thread.join();
        g_sil_thread = std::thread(SilentThread);
      }
    }
  };

  ui.fn_disable = [&]() {
    // Стоп тредов триггера/сайлента
    g_trigger_settings.cheat_active = false;
    g_trigger_settings.trigger_enabled = false;
    g_trigger_settings.silent_enabled = false;

    if (g_trigger_thread_run.load()) {
      g_trigger_thread_run.store(false);
      if (g_trig_thread.joinable())
        g_trig_thread.join();
    }
    if (g_silent_thread_run.load()) {
      g_silent_thread_run.store(false);
      if (g_sil_thread.joinable())
        g_sil_thread.join();
    }

    // Очищаем патчи сайлента из памяти, если игра запущена
    if (g_trigger_settings.game_pid != 0) {
      loader_silent::SilentCleanup(g_trigger_settings.game_pid,
                                   g_trigger_settings.game_base);
    }

    if (g_is_trigger_product) {
      // Trigger: драйвер остаётся загружен — можно снова сделать Enable
      g_is_trigger_product = false;
    } else {
      // External: только анмапим DLL — драйвер НЕ закрываем,
      // чтобы при повторном Enable не нужно было его маппить заново.
      set_cheat_running(false);
      g_running_counter = 0;
      g_running_seed = 0;
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      if (g_mapped.success)
        Mapper::Unload(g_mapped);
      if (!g_pending_cheat.empty()) {
        SecureZeroMemory(g_pending_cheat.data(), g_pending_cheat.size());
        g_pending_cheat.clear();
      }
    }
    // Фазу (в Ready или Launch) устанавливает ui_imgui.hpp
  };

  // ── Async-результаты prepare/inject ───────────────────
  std::string async_login_result;
  std::string async_prepare_result;
  std::string async_inject_result;
  std::mutex async_mutex;

  // Флаг для успешного modal
  bool show_success_modal = false;

  // Таймер для aurora
  auto last_frame = std::chrono::steady_clock::now();

  bool was_cheat_running = false;

  // ── Render Loop ────────────────────────────────────────
  MSG msg_win = {};
  while (!ui.want_close) {
    // Сообщения Win32
    while (PeekMessageW(&msg_win, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg_win);
      DispatchMessageW(&msg_win);
      if (msg_win.message == WM_QUIT)
        ui.want_close = true;
    }
    if (ui.want_close)
      break;

    if (is_cheat_running()) {
      was_cheat_running = true;
    } else if (was_cheat_running) {
      // Для trigger продукта не закрываем лоадер при краше игры
      if (!g_is_trigger_product) {
        ui.want_close = true;
        break;
      } else {
        // Триггер-продукт: остаёмся открытым, стопаем треды и сбрасываем
        // состояние
        g_trigger_thread_run.store(false);
        g_silent_thread_run.store(false);
        if (g_trig_thread.joinable())
          g_trig_thread.join();
        if (g_sil_thread.joinable())
          g_sil_thread.join();
        was_cheat_running = false;
        g_is_trigger_product = false;
        ui.phase = Phase::Launch;
        g_trigger_settings.cheat_active = false;
        g_trigger_settings.game_pid = 0;
        g_trigger_settings.game_base = 0;
      }
    }

    if (is_hidden) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }

    static int last_idx = -1;
    if (ui.selected_idx != last_idx) {
      last_idx = ui.selected_idx;
      ui.phase = Phase::Launch;
    }

    // Delta time
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last_frame).count();
    last_frame = now;
    ui.aurora_phase += dt;

    // Динамическое изменение размера окна
    static Screen last_screen = ui.screen;
    if (ui.screen != last_screen) {
      last_screen = ui.screen;
      if (ui.screen == Screen::Dashboard) {
        // Переход в Dashboard -> большое окно
        int sx = GetSystemMetrics(SM_CXSCREEN);
        int sy = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(hwnd, nullptr, (sx - 860) / 2, (sy - 540) / 2, 860, 540,
                     SWP_NOZORDER);
      }
    }

    // ── Обработка async результатов ──────────────────
    {
      std::lock_guard<std::mutex> lk(async_mutex);

      // Prepare завершился
      if (ui.busy_prepare && !async_prepare_result.empty()) {
        std::string res = async_prepare_result;
        async_prepare_result.clear();
        ui.busy_prepare = false;
        if (res == "ok") {
          ui.phase = Phase::Ready;
        } else {
          std::string err = res;
          if (err.rfind("error:", 0) == 0)
            err = err.substr(6);
          ui.AddToast(err, ToastType::Error);
        }
      }

      // Inject завершился
      if (ui.busy_inject && !async_inject_result.empty()) {
        std::string res = async_inject_result;
        async_inject_result.clear();
        ui.busy_inject = false;
        if (res == "ok") {
          show_success_modal = true;
          // Для trigger: переходим в Enabled, запускаем треды
          if (!ui.products.empty() &&
              ui.products[ui.selected_idx].id == xorstr_("trigger")) {
            if (ui.fn_enable)
              ui.fn_enable();
          }
          // Для всех продуктов: Enable нажата — показать Disable кнопку
          ui.phase = Phase::Enabled;
        } else {
          std::string err = res;
          if (err.rfind("error:", 0) == 0)
            err = err.substr(6);
          ui.AddToast(err, ToastType::Error);
        }
      }

      // Login завершился
      if (ui.logging_in && !async_login_result.empty()) {
        std::string res = async_login_result;
        async_login_result.clear();
        ui.logging_in = false;
        if (res == "ok") {
          ui.products.clear();

          // ─── External если есть подписка ───
          // Проверяем подписки через отдельный Network запрос /api/user/me
          // А пока используем единственную подписку days из login response
          // Получаем продукты из ответа логина (g_auth_subscriptions заполнены
          // в DoLogin)
          bool has_external = false, has_trigger = false, has_silent = false;
          int ext_days = 0, trig_days = 0;
          for (const auto &sub : g_auth_subscriptions) {
            const std::string &pid = sub.product_id; // уже lowercase
            if (pid == xorstr_("external")) {
              has_external = true;
              ext_days = sub.days_left;
            } else if (pid == xorstr_("trigger")) {
              has_trigger = true;
              trig_days = sub.days_left;
            } else if (pid == xorstr_("trigger_silent") ||
                       pid == xorstr_("silent")) {
              // trigger_silent = trigger + сайлент в одном, оба флага!
              has_trigger = true;
              has_silent = true;
              if (trig_days == 0)
                trig_days = sub.days_left;
            }
          }

          // External показываем только если явно куплен
          if (has_external) {
            Product p;
            p.id = xorstr_("external");
            p.name = xorstr_("External");
            p.status = xorstr_("Undetected");
            p.ok = true;
            p.bought = true;
            p.days = ext_days;
            p.has_silent = false;
            p.desc_ru =
                std::string(xorstr_(
                    "Внешняя модификация для GTA V. Работает полностью вне ")) +
                xorstr_(
                    "игрового процесса, не требует инжекции в память игры.");
            p.desc_en = std::string(xorstr_("External modification for GTA V. "
                                            "Runs fully outside the ")) +
                        xorstr_("game process, no memory injection required.");
            ui.products.push_back(p);
          }

          if (has_trigger) {
            // Добавляем Trigger если куплен
            Product tp;
            tp.id = xorstr_("trigger");
            tp.name = xorstr_("Trigger");
            tp.status = xorstr_("Undetected");
            tp.ok = true;
            tp.bought = true;
            tp.days = trig_days;
            tp.has_silent = has_silent;
            tp.desc_ru = xorstr_("Триггербот + Silent Aim для GTA V. Работает "
                                 "через kernel driver.");
            tp.desc_en = xorstr_(
                "Triggerbot + Silent Aim for GTA V. Works via kernel driver.");
            ui.products.push_back(tp);
          }

          if (ui.products.empty()) {
            // Фолбэк: если подписок нет, добавляем external из login
            Product p;
            p.id = xorstr_("external");
            p.name = xorstr_("External");
            p.status = xorstr_("Undetected");
            p.ok = true;
            p.bought = true;
            p.days = g_user_days;
            p.has_silent = false;
            p.desc_ru =
                std::string(xorstr_(
                    "Внешняя модификация для GTA V. Работает полностью вне ")) +
                xorstr_(
                    "игрового процесса, не требует инжекции в память игры.");
            p.desc_en = std::string(xorstr_("External modification for GTA V. "
                                            "Runs fully outside the ")) +
                        xorstr_("game process, no memory injection required.");
            ui.products.push_back(p);
          }
          ui.selected_idx = 0;
          ui.screen = Screen::Dashboard;
          ui.phase = Phase::Launch;
        } else {
          std::string err = res;
          if (err.rfind("error:", 0) == 0)
            err = err.substr(6);
          ui.AddToast(err, ToastType::Error);
        }
      }
    }

    // Запуск async prepare
    if (ui.busy_prepare && async_prepare_result.empty() &&
        ui.last_prepare_result == "__pending__") {
      ui.last_prepare_result.clear();
      std::string pid = xorstr_("external");
      if (ui.selected_idx >= 0 && ui.selected_idx < (int)ui.products.size())
        pid = ui.products[ui.selected_idx].id;

      if (pid == xorstr_("trigger")) {
        // Trigger: не скачиваем DLL — только инициализируем сессию, загружаем
        // драйвер и хэндшейк.
        std::thread([&]() {
          std::string new_session_key;
          std::string res =
              Phase1A_InitSession(xorstr_("trigger"), new_session_key);
          if (res != xorstr_("ok")) {
            std::lock_guard<std::mutex> lk(async_mutex);
            async_prepare_result = res;
            return;
          }
          if (!Phase1B_ParseSessionKey(new_session_key)) {
            std::lock_guard<std::mutex> lk(async_mutex);
            async_prepare_result = xorstr_("error:Ошибка сессии (Async). Пожалуйста, перезапустите лоадер.");
            return;
          }
          SecureZeroMemory(new_session_key.data(), new_session_key.size());

          // Шаг 1: загрузить драйвер
          res = Phase1C_LoadDriver(xorstr_("trigger"));
          if (res != xorstr_("ok")) {
            std::lock_guard<std::mutex> lk(async_mutex);
            async_prepare_result = res;
            return;
          }
          // Шаг 2: handshake с драйвером (использует g_dvr_session_key)
          if (!Phase1D_DriverHandshake()) {
            DWORD err = GetLastError();
            std::lock_guard<std::mutex> lk(async_mutex);
            async_prepare_result =
                xorstr_("error:Сбой связи с драйвером. Пожалуйста, перезагрузите ПК. Код: ") +
                std::to_string(err);
            return;
          }
          std::lock_guard<std::mutex> lk(async_mutex);
          async_prepare_result = xorstr_("ok");
        }).detach();
      } else {
        // External: полный цикл download + decrypt + map
        std::thread([&, pid]() {
          std::string res = Phase1_Prepare(pid);
          std::lock_guard<std::mutex> lk(async_mutex);
          async_prepare_result = res;
        }).detach();
      }
    }

    // Запуск async inject
    if (ui.busy_inject && async_inject_result.empty() &&
        ui.last_inject_result == "__pending__") {
      ui.last_inject_result.clear();
      std::string cur_pid;
      if (ui.selected_idx >= 0 && ui.selected_idx < (int)ui.products.size())
        cur_pid = ui.products[ui.selected_idx].id;

      // Trigger: не инжектим DLL — просто сигналим "ok", треды стартуют в
      // fn_enable
      if (cur_pid == xorstr_("trigger")) {
        std::lock_guard<std::mutex> lk(async_mutex);
        async_inject_result = xorstr_("ok");
      } else {
        std::thread([&]() {
          std::string res = Phase2_Inject();
          std::lock_guard<std::mutex> lk(async_mutex);
          async_inject_result = res;
        }).detach();
      }
    }

    // ── ImGui Frame ──────────────────────────────────
    // Синхронизируем настройки тригера из UI (каждый кадр, лёгкая операция)
    // trigger_enabled / silent_enabled НЕ синхронизируем здесь:
    // они выставляются один раз в fn_enable (true) и fn_disable (false).
    // Hold/release логика бинда живёт внутри TriggerThread/SilentThread.
    if (g_is_trigger_product && ui.phase == Phase::Enabled) {
      g_trigger_settings.trigger_bind_vk = ui.trig_bind_vk;
      g_trigger_settings.trigger_delay_ms = ui.trig_delay_ms;
      g_trigger_settings.silent_bind_vk = ui.silent_bind_vk;
      g_trigger_settings.silent_fov = ui.silent_fov;
      g_trigger_settings.silent_hitbox = ui.silent_hitbox;
    }

    // Panic hotkey check — 2-секундная задержка после установки бинда,
    // чтобы та же кнопка не закрыла лоадер сразу после выбора
    if (ui.panic_bind_vk != 0 && !ui.waiting_panic_bind) {
      double panic_elapsed = ImGui::GetTime() - ui.panic_bind_set_time;
      if (panic_elapsed > 2.0 &&
          (GetAsyncKeyState(ui.panic_bind_vk) & 0x8000)) {
        ui.want_close = true;
      }
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Fullscreen invisible window
    ImGui::SetNextWindowPos({0.f, 0.f});
    ImGui::SetNextWindowSize({(float)W, (float)H});
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoBackground);

    // ── Фоновый цвет ──
    ImVec2 wpos = ImGui::GetWindowPos();
    ImVec2 wsz = ImGui::GetWindowSize();
    ImGui::GetWindowDrawList()->AddRectFilled(wpos,
                                              {wpos.x + wsz.x, wpos.y + wsz.y},
                                              IM_COL32(10, 10, 10, 255), 40.f);

    // ── Контент по экрану ─────────────────────────────
    if (ui.screen == Screen::Login) {
      RenderLogin(ui, [&](std::string user, std::string pass) {
        ui.username_display = user;
        ui.logging_in = true;
        std::thread([&, user, pass]() {
          std::string res = DoLogin(user, pass);
          std::lock_guard<std::mutex> lk(async_mutex);
          async_login_result = res;
        }).detach();
      });
    } else {
      SysCheckAll(ui);
      RenderDashboard(ui);
    }

    // ── Success modal ─────────────────────────────────
    // Removed as requested

    // ── Toasts ────────────────────────────────────────
    RenderToasts(ui, dt, wpos, wsz);

    // ── System Compatibility Check Modal ──────────────
    RenderSysCheckModal(ui, wpos, wsz);

    ImGui::End();

    // Render (используем нулевую альфу для прозрачного фона за пределами
    // скруглений)
    const float cc[4] = {0.f, 0.f, 0.f, 0.f};
    g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView,
                                            nullptr);
    g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, cc);
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_pSwapChain->Present(1, 0);
  }

  // Cleanup
  UnloadAll();
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();
  CleanupD3D();
  DestroyWindow(hwnd);
  UnregisterClassW(L"ShadowLoader", hInstance);
  // Anti-forensics: финальная очистка всех трейсов текущего запуска
  shadow_af::Shutdown();

  return 0;
}
