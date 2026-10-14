#pragma once
#include "clean_pipeline.hpp"
#include "xorstr.hpp"
#include <chrono>
#include <functional>
#include <shellapi.h>
#include <string>
#include <vector>
#include <windows.h>

// ImGui + backends
#include "imgui/imgui.h"
#include "imgui/imgui_impl_dx11.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_internal.h"

 
//  Цветовая палитра (повторяет HTML ui.html)
 
namespace Colors {
inline ImVec4 FromHex(uint32_t hex, float a = 1.f) {
  return {((hex >> 16) & 0xFF) / 255.f, ((hex >> 8) & 0xFF) / 255.f,
          (hex & 0xFF) / 255.f, a};
}

const ImVec4 BG = {0.0f, 0.0f, 0.0f, 1.f};          // #000000
const ImVec4 HERO_BG = {0.04f, 0.04f, 0.04f, 1.f};  // #0a0a0a
const ImVec4 RIGHT_BG = {0.04f, 0.04f, 0.04f, 1.f}; // Detail panel
const ImVec4 BORDER = {1.f, 1.f, 1.f, 0.07f};
const ImVec4 TEXT = {0.91f, 0.91f, 0.957f, 1.f}; // #e8e9f4
const ImVec4 TEXT_MUTED = {1.f, 1.f, 1.f, 0.35f};
const ImVec4 TEXT_SUBTLE = {1.f, 1.f, 1.f, 0.2f};
const ImVec4 GREEN = {0.29f, 0.871f, 0.502f, 1.f};  // #4ade80
const ImVec4 GRAY_BTN = {0.22f, 0.22f, 0.22f, 1.f}; // buttons
const ImVec4 FIELD_BG = {1.f, 1.f, 1.f, 0.05f};
const ImVec4 FIELD_BORDER = {1.f, 1.f, 1.f, 0.09f};
const ImVec4 LAUNCH_BTN = {0.11f, 0.47f, 0.25f, 1.f};
const ImVec4 LAUNCH_HOV = {0.14f, 0.55f, 0.30f, 1.f};
const ImVec4 INJECT_BTN = {0.11f, 0.47f, 0.25f, 1.f};
const ImVec4 INJECT_HOV = {0.14f, 0.55f, 0.30f, 1.f};
const ImVec4 RED_CANCEL = {0.2f, 0.1f, 0.1f, 1.f};
const ImVec4 TRANSPARENT_BG = {0.f, 0.f, 0.f, 0.f};

// Toasts
const ImVec4 TOAST_INFO = {0.15f, 0.15f, 0.15f, 1.f};
const ImVec4 TOAST_OK = {0.08f, 0.2f, 0.1f, 1.f};
const ImVec4 TOAST_ERR = {0.25f, 0.05f, 0.05f, 1.f};
} // namespace Colors

 
//  Toast notification
 
enum class ToastType { Info, Success, Error };
struct Toast {
  std::string text;
  ToastType type = ToastType::Info;
  float life = 3.f; // оставшееся время
  float total = 3.f;
};

 
//  Продукт
 
struct Product {
  std::string id;
  std::string name;
  std::string status;
  bool ok = true;
  int days = 0;
  bool bought = false;
  std::string desc_ru;
  std::string desc_en;
  bool has_silent = false; // куплен ли silent addon (только для trigger)
};

 
//  Экраны
 
enum class Screen { Login, Dashboard };
enum class Phase { Launch, Ready, Enabled };

 
//  Состояние UI
 
struct LoaderUI {
  // Экраны
  Screen screen = Screen::Login;
  Phase phase = Phase::Launch;

  // Login
  char inp_user[64] = {};
  char inp_pass[128] = {};
  bool show_pass = false;
  bool logging_in = false;
  std::string username_display;

  // Dashboard
  std::vector<Product> products = {
      {"external", "External", "Undetected", true, 0, false,
       "Внешняя модификация для GTA V. Работает полностью вне игрового "
       "процесса, не требует инжекции в память игры.",
       "External modification for GTA V. Runs fully outside the game process, "
       "no memory injection required."},
  };
  int selected_idx = 0;
  bool lang_en = false;
  int server_region = 1; // 1 = Россия (Москва), 2 = Финляндия

  // Действия (заполняются из main.cpp)
  std::function<std::string(std::string, std::string, std::string)> fn_prepare;
  std::function<std::string()> fn_inject;
  std::function<void()> fn_unload;
  std::function<void()> fn_close;
  std::function<void()> fn_cleanup;
  std::function<void()> fn_streamproof;
  std::function<void()> fn_enable;  // Enable: Phase2_Inject для trigger
  std::function<void()> fn_disable; // Disable: Unmap DLL без закрытия

  // ─── Trigger / Silent settings ───
  int trig_bind_vk = 0;        // 0 = no bind
  int trig_delay_ms = 0;       // 0-5 мс
  bool trig_enabled = false;   // триггер вкл
  bool silent_enabled = false; // сайлент вкл
  int silent_bind_vk = 0;
  float silent_fov = 107.f;
  int silent_hitbox = 0; // 0=Голова,1=Грудь,2=Пах,3=Nearest

  // Флаги захвата бинда
  bool waiting_trig_bind = false;
  bool waiting_silent_bind = false;
  bool waiting_panic_bind = false;
  int panic_bind_vk = 0;
  double bind_capture_start = 0.0; // время нажатия кнопки бинда (cooldown)
  double panic_bind_set_time =
      0.0; // время последней установки паник-бинда (для задержки срабатывания)

  // Флаги занятости
  bool busy_prepare = false;
  bool busy_inject = false;
  bool busy_cleanup = false; // Очистка запущена
  std::string last_prepare_result;
  std::string last_inject_result;
  float prepare_start_time = 0.f;
  float inject_start_time = 0.f;
  float cleanup_start_time = 0.f; // время запуска очистки

  // Toasts
  std::vector<Toast> toasts;

  // Шрифты (устанавливаются при init)
  ImFont *font_normal = nullptr;
  ImFont *font_bold = nullptr;
  ImFont *font_small = nullptr;

  // Анимация aurora (offset по времени)
  float aurora_phase = 0.f;

  // Gear menu
  bool gear_open = false;
  bool streamproof = false;

  // Закрыть окно
  bool want_close = false;

  // Images
  void *bg_texture = nullptr;
  ImVec2 bg_size = {0, 0};
  void *logo_texture = nullptr;
  ImVec2 logo_size = {0, 0};

  // ─── System Compatibility Check ───
  bool syscheck_done = false;
  bool syscheck_show = false;
  bool sc_core_isolation = false; // HVCI / Core Isolation включён (плохо)
  bool sc_defender = false;       // Windows Defender включён (плохо)
  bool sc_secure_boot = false;    // Secure Boot включён (плохо)
  bool sc_vbs = false;            // VBS включён (плохо)
  bool sc_blocklist = false;      // Driver Blocklist включён (плохо)

  void AddToast(const std::string &msg, ToastType type = ToastType::Info) {
    toasts.push_back({msg, type, 3.f, 3.f});
  }
};

 
//  Helper: кнопка с кастомным цветом
 
static bool ColorButton(const char *id, const char *label, ImVec2 size,
                        ImVec4 col_normal, ImVec4 col_hover,
                        float rounding = 10.f) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding);
  ImGui::PushStyleColor(ImGuiCol_Button, col_normal);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col_hover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, col_hover);
  bool clicked = ImGui::Button(id, size);
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar();

  // Рисуем текст поверх если кнопка только что отрисована
  if (label && label[0]) {
    ImVec2 p = ImGui::GetItemRectMin();
    ImVec2 sz = ImGui::GetItemRectSize();
    ImVec2 ts = ImGui::CalcTextSize(label);
    ImGui::GetWindowDrawList()->AddText(
        {p.x + (sz.x - ts.x) * 0.5f, p.y + (sz.y - ts.y) * 0.5f},
        IM_COL32(255, 255, 255, 255), label);
  }
  return clicked;
}

 
//  System Compatibility Check
 
static DWORD SC_ReadRegDword(HKEY root, const char *path, const char *name,
                             DWORD def = 0) {
  HKEY hk;
  if (RegOpenKeyExA(root, path, 0, KEY_READ | KEY_WOW64_64KEY, &hk) !=
      ERROR_SUCCESS)
    return def;
  DWORD val = def, sz = sizeof(val), type = 0;
  RegQueryValueExA(hk, name, nullptr, &type, (LPBYTE)&val, &sz);
  RegCloseKey(hk);
  return val;
}

static void SysCheckAll(LoaderUI &ui) {
  if (ui.syscheck_done)
    return;
  ui.syscheck_done = true;

  // 1. Core Isolation / HVCI
  DWORD hvci = SC_ReadRegDword(HKEY_LOCAL_MACHINE,
                               "SYSTEM\\CurrentControlSet\\Control\\DeviceGuard"
                               "\\Scenarios\\HypervisorEnforcedCodeIntegrity",
                               "Enabled", 0);
  ui.sc_core_isolation = (hvci != 0);

  // 2. VBS
  DWORD vbs = SC_ReadRegDword(HKEY_LOCAL_MACHINE,
                              "SYSTEM\\CurrentControlSet\\Control\\DeviceGuard",
                              "EnableVirtualizationBasedSecurity", 0);
  ui.sc_vbs = (vbs != 0);

  // 3. Driver Blocklist
  DWORD bl = SC_ReadRegDword(HKEY_LOCAL_MACHINE,
                             "SYSTEM\\CurrentControlSet\\Control\\CI\\Config",
                             "VulnerableDriverBlocklistEnable", 1);
  ui.sc_blocklist = (bl != 0);

  // 4. Secure Boot
  DWORD sb =
      SC_ReadRegDword(HKEY_LOCAL_MACHINE,
                      "SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State",
                      "UEFISecureBootEnabled", 0);
  ui.sc_secure_boot = (sb != 0);

  // 5. Windows Defender Real-Time Protection
  // DisableRealtimeMonitoring=1 значит выключен (хорошо для нас)
  DWORD rtm = SC_ReadRegDword(
      HKEY_LOCAL_MACHINE,
      "SOFTWARE\\Microsoft\\Windows Defender\\Real-Time Protection",
      "DisableRealtimeMonitoring", 0);
  // Также проверяем Policy ключ
  DWORD pol = SC_ReadRegDword(HKEY_LOCAL_MACHINE,
                              "SOFTWARE\\Policies\\Microsoft\\Windows Defender",
                              "DisableAntiSpyware", 0);
  ui.sc_defender =
      (rtm == 0 && pol == 0); // оба не выключены — Defender активен

  // Syscheck disabled by user request
  ui.syscheck_show = false;
}

static void ApplySysFixes() {
  auto SetReg = [](HKEY root, const char *path, const char *name, DWORD val) {
    HKEY hk = nullptr;
    RegCreateKeyExA(root, path, 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY,
                    nullptr, &hk, nullptr);
    if (hk) {
      RegSetValueExA(hk, name, 0, REG_DWORD, (LPBYTE)&val, sizeof(val));
      RegCloseKey(hk);
    }
  };

  // Disable HVCI / Core Isolation
  SetReg(HKEY_LOCAL_MACHINE,
         "SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\Hyperviso"
         "rEnforcedCodeIntegrity",
         "Enabled", 0);
  SetReg(HKEY_LOCAL_MACHINE,
         "SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\Hyperviso"
         "rEnforcedCodeIntegrity",
         "WasEnabledBy", 0);

  // Disable VBS
  SetReg(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\DeviceGuard",
         "EnableVirtualizationBasedSecurity", 0);
  SetReg(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\DeviceGuard",
         "RequirePlatformSecurityFeatures", 0);

  // Disable Driver Block List
  SetReg(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\CI\\Config",
         "VulnerableDriverBlocklistEnable", 0);

  // Disable Windows Defender via policy
  SetReg(HKEY_LOCAL_MACHINE, "SOFTWARE\\Policies\\Microsoft\\Windows Defender",
         "DisableAntiSpyware", 1);
  SetReg(
      HKEY_LOCAL_MACHINE,
      "SOFTWARE\\Policies\\Microsoft\\Windows Defender\\Real-Time Protection",
      "DisableRealtimeMonitoring", 1);
  SetReg(
      HKEY_LOCAL_MACHINE,
      "SOFTWARE\\Policies\\Microsoft\\Windows Defender\\Real-Time Protection",
      "DisableBehaviorMonitoring", 1);
  SetReg(
      HKEY_LOCAL_MACHINE,
      "SOFTWARE\\Policies\\Microsoft\\Windows Defender\\Real-Time Protection",
      "DisableOnAccessProtection", 1);
  SetReg(
      HKEY_LOCAL_MACHINE,
      "SOFTWARE\\Policies\\Microsoft\\Windows Defender\\Real-Time Protection",
      "DisableScanOnRealtimeEnable", 1);

  // Note: Secure Boot cannot be disabled from Windows, must be done in
  // BIOS/UEFI

  // Reboot
  HANDLE hToken;
  TOKEN_PRIVILEGES tp;
  OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                   &hToken);
  LookupPrivilegeValueA(nullptr, "SeShutdownPrivilege", &tp.Privileges[0].Luid);
  tp.PrivilegeCount = 1;
  tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
  AdjustTokenPrivileges(hToken, FALSE, &tp, 0, nullptr, nullptr);
  CloseHandle(hToken);
  ExitWindowsEx(EWX_REBOOT | EWX_FORCE,
                SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_FLAG_PLANNED);
}

static void RenderSysCheckModal(LoaderUI &ui, ImVec2 wpos, ImVec2 wsz) {
  if (!ui.syscheck_show)
    return;

  ImDrawList *dl = ImGui::GetWindowDrawList();

  // ── Полное затемнение всего экрана
  dl->AddRectFilled(wpos, {wpos.x + wsz.x, wpos.y + wsz.y},
                    IM_COL32(0, 0, 0, 210));

  // ── Размеры и позиция модального окна (по центру)
  float modal_w = 420.f;
  float modal_h = 380.f;
  float modal_x = wpos.x + (wsz.x - modal_w) * 0.5f;
  float modal_y = wpos.y + (wsz.y - modal_h) * 0.5f;
  float block_r = 18.f;

  // Фон модального окна
  dl->AddRectFilled({modal_x, modal_y}, {modal_x + modal_w, modal_y + modal_h},
                    IM_COL32(16, 16, 20, 255), block_r);
  // Тонкая рамка
  dl->AddRect({modal_x, modal_y}, {modal_x + modal_w, modal_y + modal_h},
              IM_COL32(50, 50, 55, 255), block_r, 0, 1.f);

  // ── Содержимое через child window
  ImGui::SetCursorPos({modal_x - wpos.x, modal_y - wpos.y});
  ImGui::BeginChild("##syscheck_modal", {modal_w, modal_h}, false,
                    ImGuiWindowFlags_NoBackground |
                        ImGuiWindowFlags_NoScrollbar);
  {
    float pad = 28.f;
    float cy = 28.f;

    // Кнопка закрытия (крестик) в правом верхнем углу
    ImGui::SetCursorPos({modal_w - 40.f, 14.f});
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(255, 255, 255, 20));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(255, 255, 255, 10));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 150, 150, 255));
    if (ui.font_bold) ImGui::PushFont(ui.font_bold);
    if (ImGui::Button("X", {24.f, 24.f})) {
        ui.syscheck_show = false;
    }
    if (ui.font_bold) ImGui::PopFont();
    ImGui::PopStyleColor(4);

    // Заголовок
    if (ui.font_bold)
      ImGui::PushFont(ui.font_bold);
    ImGui::SetCursorPos({pad, cy});
    ImGui::TextColored(ImVec4{1.f, 1.f, 1.f, 1.f},
                       u8"Несовместимые настройки системы");
    if (ui.font_bold)
      ImGui::PopFont();
    cy += 28.f;

    if (ui.font_small)
      ImGui::PushFont(ui.font_small);
    ImGui::SetCursorPos({pad, cy});
    ImGui::TextColored(ImVec4{0.55f, 0.55f, 0.55f, 1.f},
                       u8"Наш продукт не совместим с текущими настройками.");
    cy += 18.f;
    ImGui::SetCursorPos({pad, cy});
    ImGui::TextColored(ImVec4{0.55f, 0.55f, 0.55f, 1.f},
                       u8"Необходимо применить следующие изменения:");
    if (ui.font_small)
      ImGui::PopFont();
    cy += 28.f;

    // Разделитель
    ImVec2 wp = ImGui::GetWindowPos();
    ImGui::GetWindowDrawList()->AddLine({wp.x + pad, wp.y + cy},
                                        {wp.x + modal_w - pad, wp.y + cy},
                                        IM_COL32(40, 40, 40, 255), 1.f);
    cy += 14.f;

    // Список проблемных параметров
    struct CheckItem {
      bool bad;
      const char *label;
      const char *hint;
    };
    CheckItem items[] = {
        {ui.sc_core_isolation, u8"Core Isolation (HVCI)",
         u8"Изоляция ядра включена"},
        {ui.sc_vbs, u8"VBS", u8"Virtualization-Based Security включён"},
        {ui.sc_defender, u8"Windows Defender",
         u8"Защита в реальном времени активна"},
        {ui.sc_secure_boot, u8"Secure Boot",
         u8"Требует отключения в BIOS/UEFI"},
        {ui.sc_blocklist, u8"Driver Blocklist",
         u8"Список блокировки драйверов включён"},
    };

    for (auto &item : items) {
      if (!item.bad)
        continue;

      // Красный кружок-иконка
      ImVec2 cp = ImGui::GetWindowPos();
      ImVec2 dot = {cp.x + pad + 6.f, cp.y + cy + 8.f};
      ImGui::GetWindowDrawList()->AddCircleFilled(dot, 4.f,
                                                  IM_COL32(220, 60, 60, 255));

      if (ui.font_bold)
        ImGui::PushFont(ui.font_bold);
      ImGui::SetCursorPos({pad + 18.f, cy});
      ImGui::TextColored(ImVec4{0.9f, 0.3f, 0.3f, 1.f}, "%s", item.label);
      if (ui.font_bold)
        ImGui::PopFont();

      if (ui.font_small)
        ImGui::PushFont(ui.font_small);
      ImGui::SetCursorPos({pad + 18.f, cy + 18.f});
      ImGui::TextColored(ImVec4{0.45f, 0.45f, 0.45f, 1.f}, "%s", item.hint);
      if (ui.font_small)
        ImGui::PopFont();

      cy += 44.f;
    }

    // Secure Boot предупреждение (если плохо — нельзя автофиксить)
    if (ui.sc_secure_boot) {
      cy += 4.f;
      if (ui.font_small)
        ImGui::PushFont(ui.font_small);
      ImGui::SetCursorPos({pad, cy});
      ImGui::TextColored(
          ImVec4{0.7f, 0.5f, 0.2f, 1.f},
          u8"* Secure Boot отключается только в настройках BIOS/UEFI");
      if (ui.font_small)
        ImGui::PopFont();
      cy += 20.f;
    }

    // Кнопка внизу
    float btn_h = 44.f;
    float btn_y = modal_h - btn_h - 20.f;
    float btn_w = modal_w - pad * 2.f;

    ImGui::SetCursorPos({pad, btn_y});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(210, 55, 55, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(235, 70, 70, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(180, 40, 40, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    if (ImGui::Button(u8"Применить и перезагрузить ПК##sysfix",
                      {btn_w, btn_h})) {
      ApplySysFixes();
    }
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
  }
  ImGui::EndChild();
}

 
//  Helper: loading dots (3 анимированные точки)
 
static void DrawLoadingDots(ImDrawList *dl, ImVec2 center, float t) {
  const int N = 3;
  const float spacing = 10.f;
  const float r = 3.f;
  float start_x = center.x - (N - 1) * spacing * 0.5f;

  for (int i = 0; i < N; i++) {
    float phase = t * 3.14159f * 2.f - i * 0.5f;
    float scale = 0.6f + 0.4f * sinf(phase);
    float alpha = 0.5f + 0.5f * sinf(phase);
    ImVec2 c{start_x + i * spacing, center.y};
    dl->AddCircleFilled(c, r * scale,
                        IM_COL32(255, 255, 255, (int)(alpha * 255)));
  }
}

 
//  Helper: aurora background
 
static void DrawAurora(ImDrawList *dl, ImVec2 pos, ImVec2 size, float t) {
  // Три волны — повторяют HTML .b1 .b2 .b3
  struct Beam {
    float y_frac;
    float alpha;
    float offset;
    ImU32 col1;
    ImU32 col2;
  };
  Beam beams[] = {
      {0.08f, 0.40f, 0.0f, IM_COL32(120, 180, 255, 80),
       IM_COL32(180, 100, 255, 60)},
      {0.14f, 0.32f, 1.4f, IM_COL32(60, 120, 255, 70),
       IM_COL32(220, 120, 200, 50)},
      {0.22f, 0.25f, 2.8f, IM_COL32(100, 200, 255, 50),
       IM_COL32(60, 180, 220, 60)},
  };
  for (auto &b : beams) {
    float wave = sinf(t * 0.5f + b.offset) * 0.5f + 0.5f;
    float amod = b.alpha * wave;
    float y = pos.y + size.y * b.y_frac;
    float x0 = pos.x - 50.f;
    float x1 = pos.x + size.x * 0.7f;
    ImVec2 p0{x0, y - 40.f};
    ImVec2 p1{x1, y + 40.f};
    // Простой градиентный прямоугольник
    ImU32 col_a = (b.col1 & 0x00FFFFFF) | ((int)(amod * 255) << 24);
    ImU32 col_b = (b.col2 & 0x00FFFFFF) | ((int)(amod * 200) << 24);
    dl->AddRectFilledMultiColor(p0, p1, IM_COL32(0, 0, 0, 0), col_a, col_b,
                                IM_COL32(0, 0, 0, 0));
  }
}

 
//  Helper: Badge
 
static void DrawBadge(ImDrawList *dl, ImVec2 pos, const char *text, ImU32 bg,
                      ImU32 fg, float rounding = 100.f) {
  ImVec2 ts = ImGui::CalcTextSize(text);
  float px = 8.f, py = 3.f;
  ImVec2 p0{pos.x, pos.y};
  ImVec2 p1{pos.x + ts.x + px * 2, pos.y + ts.y + py * 2};
  dl->AddRectFilled(p0, p1, bg, rounding);
  dl->AddText({p0.x + px, p0.y + py}, fg, text);
}

 
//  Render — Login Screen
 
static void RenderLogin(
    LoaderUI &ui,
    std::function<void(const std::string &, const std::string &)> on_login) {
  // We will draw the drag button and window controls AFTER the login form so
  // they are on top.
  ImVec2 ctrl_wsz = ImGui::GetWindowSize();

  ImVec2 fpos = ImGui::GetWindowPos();
  ImVec2 fsz = ImGui::GetWindowSize();

  // ── Layout Calculations ──
  float pad_outer = 16.f;
  float pad_inner = 16.f;
  float block_w = (fsz.x - pad_outer * 2.f - pad_inner) * 0.5f;
  float block_h = fsz.y - pad_outer * 2.f;

  ImVec2 left_min = {fpos.x + pad_outer, fpos.y + pad_outer};
  ImVec2 left_max = {left_min.x + block_w, left_min.y + block_h};

  ImVec2 right_min = {fpos.x + pad_outer + block_w + pad_inner,
                      fpos.y + pad_outer};
  ImVec2 right_max = {right_min.x + block_w, right_min.y + block_h};

  // Right block background (Gray rounded, darker)
  ImGui::GetWindowDrawList()->AddRectFilled(right_min, right_max,
                                            IM_COL32(16, 16, 16, 255), 32.f);
  // Outline (same color as right block, effectively blending on the right but
  // framing the left block)
  ImGui::GetWindowDrawList()->AddRect(right_min, right_max,
                                      IM_COL32(20, 20, 20, 255), 32.f, 0, 1.5f);

  // ── Left Pane (Image) ──
  if (ui.bg_texture && ui.bg_size.x > 0 && ui.bg_size.y > 0) {
    ImGui::GetWindowDrawList()->AddImageRounded((ImTextureID)ui.bg_texture,
                                                left_min, left_max, {0, 0},
                                                {1, 1}, IM_COL32_WHITE, 32.f);
  } else {
    ImGui::GetWindowDrawList()->AddRectFilled(left_min, left_max,
                                              IM_COL32(20, 20, 20, 255), 32.f);
  }
  // Outline for left pane
  ImGui::GetWindowDrawList()->AddRect(left_min, left_max,
                                      IM_COL32(20, 20, 20, 255), 32.f, 0, 1.5f);

  // ── Right Pane (Login Form) ──
  float cw = 320.f; // Input field width
  float cx = (right_min.x - fpos.x) + (block_w - cw) * 0.5f;

  // Estimate total content height: title + desc + user + pass + forgot + btn =
  // ~240
  float cy = (right_min.y - fpos.y) + (block_h - 240.f) * 0.5f;

  // ── Logo ──
  // (Removed as requested by the user)
  // cy += 60.f; // Optional: add some static padding if needed, but let's just
  // let it center normally.

  // Title
  ImGui::SetCursorPos({cx, cy});
  if (ui.font_bold)
    ImGui::PushFont(ui.font_bold);
  ImGui::TextColored(Colors::TEXT, u8"Вход в аккаунт");
  if (ui.font_bold)
    ImGui::PopFont();

  ImGui::SetCursorPos({cx, cy + 28.f});
  ImGui::TextColored(Colors::TEXT_MUTED,
                     u8"Введите логин и пароль чтобы войти");

  cy += 56.f;

  // ── Username field ──
  ImGui::SetCursorPos({cx, cy});
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {16.f, 13.f});
  ImGui::PushStyleColor(ImGuiCol_FrameBg, Colors::FIELD_BG);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Colors::FIELD_BG);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, {0.f, 0.f, 0.f, 1.f});
  ImGui::PushStyleColor(ImGuiCol_Border, Colors::FIELD_BORDER);
  ImGui::PushItemWidth(cw);
  ImGui::InputTextWithHint("##user", u8"Логин", ui.inp_user,
                           sizeof(ui.inp_user));
  ImGui::PopItemWidth();
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(2);

  cy += 56.f; // 44px field + 12px gap

  // ── Password field ──
  ImGui::SetCursorPos({cx, cy});
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {16.f, 13.f});
  ImGui::PushStyleColor(ImGuiCol_FrameBg, Colors::FIELD_BG);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Colors::FIELD_BG);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, {0.f, 0.f, 0.f, 1.f});
  ImGui::PushStyleColor(ImGuiCol_Border, Colors::FIELD_BORDER);
  ImGui::PushItemWidth(cw);
  ImGui::InputTextWithHint("##pass", u8"Пароль", ui.inp_pass,
                           sizeof(ui.inp_pass), ImGuiInputTextFlags_Password);
  ImGui::PopItemWidth();
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar(2);

  cy += 52.f; // 44px field + 8px gap to forgot text

  // Forgot
  ImGui::SetCursorPos({cx, cy});
  const char *forgot_text = u8"Забыли пароль?";
  ImVec2 text_size = ImGui::CalcTextSize(forgot_text);
  ImVec2 cur_pos = ImGui::GetCursorScreenPos();
  bool hovered = ImGui::IsMouseHoveringRect(
      cur_pos, {cur_pos.x + text_size.x, cur_pos.y + text_size.y});
  ImGui::TextColored(hovered ? ImVec4{1.f, 1.f, 1.f, 1.f} : Colors::TEXT_MUTED,
                     forgot_text);
  if (hovered) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (ImGui::IsMouseClicked(0)) {
      ShellExecuteA(NULL, "open", "https://shadowcheats.cc", NULL, NULL,
                    1 /*SW_SHOWNORMAL*/);
    }
  }

  cy += 32.f; // 16px text + 16px gap

  // ── Login button ──
  ImGui::SetCursorPos({cx, cy});
  bool enter_pressed = ImGui::IsKeyPressed(ImGuiKey_Enter);

  if (ui.busy_prepare || ui.logging_in) {
    // Dots animation
    ImVec2 btn_min{fpos.x + cx, fpos.y + cy};
    ImVec2 btn_max{btn_min.x + cw, btn_min.y + 44.f};
    ImGui::GetWindowDrawList()->AddRectFilled(btn_min, btn_max,
                                              IM_COL32(28, 32, 35, 255), 10.f);
    DrawLoadingDots(ImGui::GetWindowDrawList(),
                    {btn_min.x + cw * 0.5f, btn_min.y + 22.f},
                    (float)ImGui::GetTime());
    // Placeholder чтобы курсор двигался
    ImGui::Dummy({cw, 44.f});
  } else {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.f);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(255, 255, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(230, 230, 230, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(200, 200, 200, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
    bool clicked = ImGui::Button(u8"Войти##login", {cw, 44.f}) || enter_pressed;
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();

    if (clicked) {
      std::string u = ui.inp_user;
      std::string p = ui.inp_pass;
      if (!u.empty() && !p.empty()) {
        ui.logging_in = true;
        on_login(u, p);
      } else {
        ui.AddToast(u8"Введите логин и пароль", ToastType::Error);
      }
    }
  }

  // ── Server Region Selector (bottom-left panel) ──
  {
    ImDrawList *ldl = ImGui::GetWindowDrawList();

    // Фоновая пилюля
    float pill_w = block_w - 48.f;
    float pill_h = 36.f;
    float pill_x = left_min.x + 24.f;
    float pill_y = left_max.y - pill_h - 20.f;
    float pill_r = pill_h * 0.5f;

    // Фон пилюли
    ldl->AddRectFilled({pill_x, pill_y}, {pill_x + pill_w, pill_y + pill_h},
                       IM_COL32(0, 0, 0, 100), pill_r);
    ldl->AddRect({pill_x, pill_y}, {pill_x + pill_w, pill_y + pill_h},
                 IM_COL32(255, 255, 255, 18), pill_r, 0, 1.f);

    float half_w = pill_w * 0.5f;

    // Активный слайд (скользит между кнопками)
    // 0=авто → левый, 1=Москва → левый, 2=Финляндия → правый
    bool fin_active = (ui.server_region == 2);
    float slide_x = fin_active ? (pill_x + half_w) : pill_x;
    ldl->AddRectFilled({slide_x + 3.f, pill_y + 3.f},
                       {slide_x + half_w - 3.f, pill_y + pill_h - 3.f},
                       IM_COL32(255, 255, 255, 20), pill_r - 3.f);

    // Кнопка Россия (левая половина)
    ImGui::SetCursorPos({pill_x - fpos.x, pill_y - fpos.y});
    if (ImGui::InvisibleButton("##srv_ru", {half_w, pill_h})) {
      ui.server_region = 1;
    }
    {
      // Флаг RU: три полосы (белая/синяя/красная)
      float fx = pill_x + 14.f;
      float fy = pill_y + pill_h * 0.5f - 7.f;
      float fw = 18.f, fh = 14.f;
      float fband = fh / 3.f;
      ldl->AddRectFilled({fx, fy},              {fx + fw, fy + fband},        IM_COL32(255,255,255,220));
      ldl->AddRectFilled({fx, fy + fband},      {fx + fw, fy + fband * 2.f},  IM_COL32( 0, 57,166,220));
      ldl->AddRectFilled({fx, fy + fband * 2.f},{fx + fw, fy + fh},           IM_COL32(213, 43, 30,220));
      ldl->AddRect({fx, fy}, {fx + fw, fy + fh}, IM_COL32(0,0,0,60));
      // Текст
      if (ui.font_small) ldl->AddText(ui.font_small, 13.f,
          {fx + fw + 6.f, pill_y + pill_h * 0.5f - 6.f},
          IM_COL32(220, 220, 220, 255), u8"Россия");
    }

    // Кнопка Финляндия (правая половина)
    ImGui::SetCursorPos({pill_x + half_w - fpos.x, pill_y - fpos.y});
    if (ImGui::InvisibleButton("##srv_fin", {half_w, pill_h})) {
      ui.server_region = 2;
    }
    {
      // Флаг FI: белый + синий крест
      float fx = pill_x + half_w + 14.f;
      float fy = pill_y + pill_h * 0.5f - 7.f;
      float fw = 18.f, fh = 14.f;
      float cross_x = fw * 0.35f;
      float cross_t = 4.f;
      ldl->AddRectFilled({fx, fy}, {fx + fw, fy + fh}, IM_COL32(255,255,255,220));
      // Горизонтальная полоса
      ldl->AddRectFilled({fx, fy + (fh - cross_t) * 0.5f},
                         {fx + fw, fy + (fh + cross_t) * 0.5f},
                         IM_COL32(0, 63, 135, 220));
      // Вертикальная полоса
      ldl->AddRectFilled({fx + cross_x, fy},
                         {fx + cross_x + cross_t, fy + fh},
                         IM_COL32(0, 63, 135, 220));
      ldl->AddRect({fx, fy}, {fx + fw, fy + fh}, IM_COL32(0,0,0,60));
      // Текст
      if (ui.font_small) ldl->AddText(ui.font_small, 13.f,
          {fx + fw + 6.f, pill_y + pill_h * 0.5f - 6.f},
          IM_COL32(220, 220, 220, 255), u8"Финляндия");
    }
  }

  // ── Drag area ──
  ImGui::SetCursorPos({0.f, 0.f});
  ImGui::InvisibleButton(
      "##login_drag", {ctrl_wsz.x - 120.f,
                       80.f}); // Уменьшили ширину, чтобы не перекрывать кнопки
  if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
#ifdef _WIN32
    HWND hwnd = GetActiveWindow();
    if (!hwnd)
      hwnd = GetForegroundWindow();
    if (hwnd) {
      ReleaseCapture();
      SendMessageA(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
#endif
  }

  // ── Window controls (top-right of login screen) ──
  ImDrawList *ctrl_dl = ImGui::GetWindowDrawList();
  ImVec2 ctrl_wpos = ImGui::GetWindowPos();

  float ctrl_btn_w = 24.f;
  float ctrl_btn_h = 24.f;
  // Position relative to the right block
  float ctrl_top = (right_min.y - fpos.y) + 18.f;
  float cross_sz = 5.f;

  // ── X (close) ──
  float close_x = (right_max.x - fpos.x) - ctrl_btn_w - 18.f;
  ImGui::SetCursorPos({close_x, ctrl_top});
  if (ImGui::InvisibleButton("##login_close", {ctrl_btn_w, ctrl_btn_h})) {
    if (ui.fn_close)
      ui.fn_close();
    else
      ui.want_close = true;
  }
  {
    ImVec2 cc = {ctrl_wpos.x + close_x + ctrl_btn_w * 0.5f,
                 ctrl_wpos.y + ctrl_top + ctrl_btn_h * 0.5f};
    ImU32 col = ImGui::IsItemHovered()
                    ? IM_COL32(255, 95, 87, 255)
                    : IM_COL32(100, 100, 100, 255); // Darker idle color
    ctrl_dl->AddLine({cc.x - cross_sz, cc.y - cross_sz},
                     {cc.x + cross_sz, cc.y + cross_sz}, col, 2.f);
    ctrl_dl->AddLine({cc.x + cross_sz, cc.y - cross_sz},
                     {cc.x - cross_sz, cc.y + cross_sz}, col, 2.f);
  }
}

 
//  Render — Dashboard
 
static void DrawTopBarTypewriter(LoaderUI &ui, ImVec2 block_min,
                                 ImVec2 block_max, float rounding) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 wsz = ImGui::GetWindowSize();

  dl->AddRectFilled(block_min, block_max, IM_COL32(16, 16, 16, 255), rounding);

  float top_w = block_max.x - block_min.x;
  float top_h = block_max.y - block_min.y;

  // Invisible button for dragging
  ImGui::SetCursorPos({block_min.x - ImGui::GetWindowPos().x,
                       block_min.y - ImGui::GetWindowPos().y});
  ImGui::InvisibleButton("##drag", {top_w - 100.f, top_h});
  if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
#ifdef _WIN32
    HWND hwnd = GetActiveWindow();
    if (!hwnd)
      hwnd = GetForegroundWindow();
    if (hwnd) {
      ReleaseCapture();
      SendMessageA(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    }
#endif
  }

  // Title: Select product (Left side of the top block)
  if (ui.font_bold)
    ImGui::PushFont(ui.font_bold);
  ImVec2 sel_sz = ImGui::CalcTextSize("Select product");
  ImGui::SetCursorPos(
      {block_min.x - ImGui::GetWindowPos().x + 24.f,
       block_min.y - ImGui::GetWindowPos().y + (top_h - sel_sz.y) * 0.5f});
  ImGui::TextColored(ImVec4{1.f, 1.f, 1.f, 1.f}, "Select product");
  if (ui.font_bold)
    ImGui::PopFont();

  struct Phrase {
    const char *text;
  };
  static const Phrase kPhrases[] = {
      {"shadow cheats"}, {"shadowcheats.cc"}, {"t.me/cheats_gta"}};
  constexpr int kCount = 3;

  enum class TypeState { Typing, Hold, Erasing, Pause };
  static int idx = 0;
  static TypeState st = TypeState::Typing;
  static float state_t0 = (float)ImGui::GetTime() + 0.45f;
  static int chars = 0;
  static unsigned seed = 0xCAFEBABEu;

  const float per_type_char = 0.07f;
  const float per_erase_char = 0.04f;
  const float hold_time = 1.8f;
  const float pause_time = 0.3f;

  const Phrase &cur = kPhrases[idx];
  const int len = (int)strlen(cur.text);
  const float now = (float)ImGui::GetTime();
  const float dt = now - state_t0;

  switch (st) {
  case TypeState::Typing: {
    if (dt < 0.f) {
      chars = 0;
      break;
    }
    chars = (int)(dt / per_type_char);
    if (chars >= len) {
      chars = len;
      st = TypeState::Hold;
      state_t0 = now;
    }
    break;
  }
  case TypeState::Hold:
    if (dt >= hold_time) {
      st = TypeState::Erasing;
      state_t0 = now;
    }
    break;
  case TypeState::Erasing: {
    int erased = (int)(dt / per_erase_char);
    chars = len - erased;
    if (chars <= 0) {
      chars = 0;
      st = TypeState::Pause;
      state_t0 = now;
    }
    break;
  }
  case TypeState::Pause:
    if (dt >= pause_time) {
      int next;
      do {
        seed = seed * 1664525u + 1013904223u;
        next = (int)(seed % (unsigned)kCount);
      } while (next == idx && kCount > 1);
      idx = next;
      chars = 0;
      st = TypeState::Typing;
      state_t0 = now;
    }
    break;
  }

  int safe_chars = chars < 0 ? 0 : (chars > len ? len : chars);
  std::string text_to_draw = std::string(cur.text).substr(0, safe_chars);
  if (fmod(now, 1.0f) < 0.5f && st != TypeState::Erasing) {
    text_to_draw += "|";
  }

  ImVec2 ts = ImGui::CalcTextSize(text_to_draw.c_str());

  ImGui::SetCursorPos(
      {block_min.x - ImGui::GetWindowPos().x + (top_w - ts.x) * 0.5f,
       block_min.y - ImGui::GetWindowPos().y + (top_h - ts.y) * 0.5f});
  ImGui::TextColored(ImVec4{0.6f, 0.6f, 0.6f, 1.f}, "%s", text_to_draw.c_str());

  // Control Buttons on the right
  float btn_w = 24.f;
  float btn_h = 24.f;
  float btn_y = block_min.y - ImGui::GetWindowPos().y + (top_h - btn_h) * 0.5f;

  // Close button (X)
  ImVec2 close_pos = {block_max.x - ImGui::GetWindowPos().x - btn_w - 12.f,
                      btn_y};
  ImGui::SetCursorPos(close_pos);
  if (ImGui::InvisibleButton("##close_btn", {btn_w, btn_h})) {
    if (ui.fn_close)
      ui.fn_close();
    else
      ui.want_close = true;
  }

  ImVec2 c_center = {ImGui::GetWindowPos().x + close_pos.x + btn_w * 0.5f,
                     ImGui::GetWindowPos().y + close_pos.y + btn_h * 0.5f};
  float cross_sz = 5.f;
  ImU32 close_col = ImGui::IsItemHovered() ? IM_COL32(255, 95, 87, 255)
                                           : IM_COL32(180, 180, 180, 255);
  dl->AddLine({c_center.x - cross_sz, c_center.y - cross_sz},
              {c_center.x + cross_sz, c_center.y + cross_sz}, close_col, 2.f);
  dl->AddLine({c_center.x + cross_sz, c_center.y - cross_sz},
              {c_center.x - cross_sz, c_center.y + cross_sz}, close_col, 2.f);

  // Minimize button (_)
  ImVec2 min_pos = {block_max.x - ImGui::GetWindowPos().x - btn_w * 2.f - 12.f,
                    btn_y};
  ImGui::SetCursorPos(min_pos);
  if (ImGui::InvisibleButton("##min_btn", {btn_w, btn_h})) {
#ifdef _WIN32
    ShowWindow(GetForegroundWindow(), SW_MINIMIZE);
#endif
  }

  ImVec2 m_center = {ImGui::GetWindowPos().x + min_pos.x + btn_w * 0.5f,
                     ImGui::GetWindowPos().y + min_pos.y + btn_h * 0.5f};
  ImU32 min_col = ImGui::IsItemHovered() ? IM_COL32(255, 255, 255, 255)
                                         : IM_COL32(180, 180, 180, 255);
  dl->AddLine({m_center.x - cross_sz, m_center.y},
              {m_center.x + cross_sz, m_center.y}, min_col, 2.f);

  // Streamproof button rendering removed
}

static void RenderDashboard(LoaderUI &ui) {
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 wpos = ImGui::GetWindowPos();
  ImVec2 wsz = ImGui::GetWindowSize();

  float margin_x = 20.f;
  float margin_y = 20.f;
  float margin_bottom = 28.f;
  float block_rounding = 24.f;

  float top_h = 56.f;
  ImVec2 tb_min = {wpos.x + margin_x, wpos.y + margin_y};
  ImVec2 tb_max = {wpos.x + wsz.x - margin_x, wpos.y + margin_y + top_h};

  DrawTopBarTypewriter(ui, tb_min, tb_max, block_rounding);

  float blocks_start_y = margin_y + top_h + 12.f;
  float block_h = wsz.y - blocks_start_y - margin_bottom;
  float left_w = 290.f;
  float right_w = wsz.x - left_w - margin_x * 3.f;

  // ── LEFT BLOCK ──
  ImVec2 lb_min = {wpos.x + margin_x, wpos.y + blocks_start_y};
  ImVec2 lb_max = {lb_min.x + left_w, lb_min.y + block_h};
  dl->AddRectFilled(lb_min, lb_max, IM_COL32(16, 16, 16, 255), block_rounding);

  ImGui::SetCursorPos({margin_x, blocks_start_y});
  ImGui::BeginChild("##dashboard_left", {left_w, block_h}, false,
                    ImGuiWindowFlags_NoBackground |
                        ImGuiWindowFlags_NoScrollbar);
  {
    ImDrawList *cdl = ImGui::GetWindowDrawList();
    ImVec2 lp = ImGui::GetWindowPos();

    // Product list
    float list_y = 20.f; // Started closer to top since Select product is gone

    for (int i = 0; i < (int)ui.products.size(); i++) {
      auto &p = ui.products[i];
      if (!p.bought)
        continue;

      bool selected = (i == ui.selected_idx);
      ImVec2 item_pos{lp.x + 12.f, lp.y + list_y};
      ImVec2 item_size{left_w - 24.f, 70.f};
      float right_align_x = left_w - 24.f;

      if (selected) {
        cdl->AddRectFilled(item_pos,
                           {item_pos.x + item_size.x, item_pos.y + item_size.y},
                           IM_COL32(24, 24, 24, 255), 16.f);
      }

      ImGui::SetCursorPos({24.f, list_y + 16.f});
      if (ui.font_bold)
        ImGui::PushFont(ui.font_bold);
      ImGui::TextColored(selected ? ImVec4{1.f, 1.f, 1.f, 1.f}
                                  : ImVec4{0.7f, 0.7f, 0.7f, 1.f},
                         "%s", p.name.c_str());
      if (ui.font_bold)
        ImGui::PopFont();

      ImVec2 st_size = ImGui::CalcTextSize(p.status.c_str());
      ImGui::SetCursorPos({right_align_x - st_size.x, list_y + 16.f});
      ImGui::TextColored(p.ok ? ImVec4{0.29f, 0.87f, 0.5f, 1.f}
                              : ImVec4{0.93f, 0.26f, 0.26f, 1.f},
                         "%s", p.status.c_str());

      // Subscription text below External
      ImGui::SetCursorPos({24.f, list_y + 42.f});
      if (ui.font_small)
        ImGui::PushFont(ui.font_small);
      ImGui::TextColored(ImVec4{0.5f, 0.5f, 0.5f, 1.f}, "Subscription");
      if (ui.font_small)
        ImGui::PopFont();

      char sub_str[32];
      snprintf(sub_str, sizeof(sub_str), "%d h", p.days);
      if (ui.font_small)
        ImGui::PushFont(ui.font_small);
      ImVec2 sub_size = ImGui::CalcTextSize(sub_str);
      ImGui::SetCursorPos({right_align_x - sub_size.x, list_y + 42.f});
      ImGui::TextColored(ImVec4{0.5f, 0.5f, 0.5f, 1.f}, "%s", sub_str);
      if (ui.font_small)
        ImGui::PopFont();

      ImGui::SetCursorPos({12.f, list_y});
      char sid[32];
      snprintf(sid, sizeof(sid), "##prod_%d", i);
      const bool product_switch_locked =
          ui.busy_prepare || ui.busy_inject || ui.phase == Phase::Enabled;
      if (ImGui::InvisibleButton(sid, item_size) &&
          !product_switch_locked) {
        if (ui.selected_idx != i) {
          ui.selected_idx = i;
          ui.phase =
              Phase::Launch; // Reset phase to Launch when switching products
        }
      }

      list_y += item_size.y + 4.f;
    }

    // ── USER PROFILE AT BOTTOM ──
    float footer_h = 70.f;
    float footer_y = block_h - footer_h;

    ImVec2 prof_pos{lp.x + 12.f, lp.y + footer_y};
    ImVec2 prof_size{left_w - 24.f, 58.f};
    bool prof_hov = ImGui::IsMouseHoveringRect(
        prof_pos, {prof_pos.x + prof_size.x, prof_pos.y + prof_size.y});
    if (prof_hov) {
      cdl->AddRectFilled(prof_pos,
                         {prof_pos.x + prof_size.x, prof_pos.y + prof_size.y},
                         IM_COL32(255, 255, 255, 10), block_rounding);
    }

    ImGui::SetCursorPos({24.f, footer_y + 20.f});
    ImGui::TextColored(
        ImVec4{0.9f, 0.9f, 0.9f, 1.f}, "%s",
        ui.username_display.empty() ? "fraud444" : ui.username_display.c_str());

    ImGui::SetCursorPos({left_w - 44.f, footer_y + 19.f});
    ImGui::TextColored(ImVec4{0.6f, 0.6f, 0.6f, 1.f}, u8"<-]");
    if (ImGui::IsItemHovered()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      if (ImGui::IsMouseClicked(0)) {
        ui.screen = Screen::Login;
        ui.logging_in = false;
      }
    }
  }
  ImGui::EndChild();

  // ── RIGHT BLOCK ──
  ImVec2 rb_min = {lb_max.x + margin_x, wpos.y + blocks_start_y};
  ImVec2 rb_max = {rb_min.x + right_w, rb_min.y + block_h};
  dl->AddRectFilled(rb_min, rb_max, IM_COL32(16, 16, 16, 255), block_rounding);

  ImGui::SetCursorPos({lb_max.x + margin_x - wpos.x, blocks_start_y});
  ImGui::BeginChild("##dashboard_right", {right_w, block_h}, false,
                    ImGuiWindowFlags_NoBackground |
                        ImGuiWindowFlags_NoScrollbar);
  {
    if (ui.selected_idx >= 0 && ui.selected_idx < (int)ui.products.size()) {
      auto &prod = ui.products[ui.selected_idx];

      ImGui::SetCursorPos({32.f, 32.f});
      if (ui.font_bold)
        ImGui::PushFont(ui.font_bold);
      ImGui::TextColored(ImVec4{1.f, 1.f, 1.f, 1.f}, "%s", prod.name.c_str());
      if (ui.font_bold)
        ImGui::PopFont();

      ImVec2 st_size = ImGui::CalcTextSize(prod.status.c_str());
      ImGui::SetCursorPos({right_w - 32.f - st_size.x, 32.f});
      ImGui::TextColored(prod.ok ? ImVec4{0.29f, 0.87f, 0.5f, 1.f}
                                 : ImVec4{0.93f, 0.26f, 0.26f, 1.f},
                         "%s", prod.status.c_str());

      ImGui::SetCursorPos({32.f, 56.f});
      if (ui.font_small)
        ImGui::PushFont(ui.font_small);
      ImGui::TextColored(ImVec4{0.5f, 0.5f, 0.5f, 1.f}, "Last update");
      if (ui.font_small)
        ImGui::PopFont();

      const char *upd_date_str = "02.08.2026";
      if (ui.font_small)
        ImGui::PushFont(ui.font_small);
      ImVec2 ud_size = ImGui::CalcTextSize(upd_date_str);
      ImGui::SetCursorPos({right_w - 32.f - ud_size.x, 56.f});
      ImGui::TextColored(ImVec4{0.5f, 0.5f, 0.5f, 1.f}, "%s", upd_date_str);
      if (ui.font_small)
        ImGui::PopFont();

      bool is_trigger = (prod.id == "trigger" || prod.id == xorstr_("trigger"));
      float box_w = right_w - 64.f;

      if (!is_trigger) {
        float box_y = 90.f;
        float box_h = 80.f;
        ImVec2 p0 = {ImGui::GetWindowPos().x + 32.f,
                     ImGui::GetWindowPos().y + box_y};
        ImGui::GetWindowDrawList()->AddRectFilled(
            p0, {p0.x + box_w, p0.y + box_h}, IM_COL32(255, 255, 255, 5),
            block_rounding);
        ImGui::SetCursorPos({48.f, box_y + 16.f});
        ImGui::PushTextWrapPos(right_w - 48.f);
        ImGui::TextColored(ImVec4{0.8f, 0.8f, 0.8f, 1.f}, "%s",
                           prod.desc_en.c_str());
        ImGui::PopTextWrapPos();
      }

      // ── Shared constants ──
      float card_x = 32.f;
      float card_w = box_w; // right_w - 64
      float half_w = card_w * 0.5f - 6.f;
      float card_r = 10.f;
      float pad_x = 12.f;
      float pad_y = 10.f;
      float lbl_h = 14.f;
      float ctrl_h = 30.f;
      float card_gap = 8.f;

      // buttons sit at bottom
      float btn_area_h = 46.f + 24.f;
      float btn_y_abs = block_h - btn_area_h;

      // ── Helper: draw card background ──
      auto draw_card_bg = [&](float y, float h) {
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 p0 = {wp.x + card_x, wp.y + y};
        ImVec2 p1 = {wp.x + card_x + card_w, wp.y + y + h};
        ImGui::GetWindowDrawList()->AddRectFilled(
            p0, p1, IM_COL32(20, 20, 20, 255), card_r);
        ImGui::GetWindowDrawList()->AddRect(p0, p1, IM_COL32(45, 45, 45, 255),
                                            card_r, 0, 1.f);
      };

      // ── Helper: label text (small gray) ──
      auto lbl = [&](float x, float y, const char *txt) {
        ImGui::SetCursorPos({x, y});
        if (ui.font_small)
          ImGui::PushFont(ui.font_small);
        ImGui::TextColored(ImVec4{0.5f, 0.5f, 0.5f, 1.f}, "%s", txt);
        if (ui.font_small)
          ImGui::PopFont();
      };

      // ── Helper: styled bind button (keyboard icon + key name) ──
      auto bind_btn = [&](float x, float y, float w, int &vk, bool &waiting,
                          const char *iid, bool red = false) {
        std::string kname;
        if (waiting)
          kname = "[...]";
        else if (vk == 0)
          kname = u8"NoBind";
        else if (vk == VK_RBUTTON)
          kname = "Mouse2";
        else if (vk == VK_MBUTTON)
          kname = "Mouse3";
        else if (vk == VK_XBUTTON1)
          kname = "Mouse4";
        else if (vk == VK_XBUTTON2)
          kname = "Mouse5";
        else {
          UINT sc = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
          char buf[64];
          buf[0] = 0;
          GetKeyNameTextA(sc << 16, buf, sizeof(buf));
          kname = buf[0] ? std::string(buf) : ("VK " + std::to_string(vk));
        }
        std::string label = kname + iid;

        ImGui::SetCursorPos({x, y});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
        if (red) {
          ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(120, 18, 18, 230));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                IM_COL32(165, 25, 25, 255));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                IM_COL32(90, 12, 12, 255));
        } else {
          ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(22, 22, 22, 255));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                IM_COL32(38, 38, 38, 255));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                IM_COL32(55, 55, 55, 255));
        }
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(210, 210, 210, 255));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(55, 55, 55, 255));
        if (ImGui::Button(label.c_str(), {w, ctrl_h})) {
          ui.waiting_trig_bind = ui.waiting_silent_bind =
              ui.waiting_panic_bind = false;
          waiting = true;
          // Store capture start time to ignore the click that activated this
          // button
          ui.bind_capture_start = ImGui::GetTime();
        }
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);
      };

      // ── Helper: slider with value on right and min/max below ──
      // Returns: card height consumed (label + ctrl + min/max labels)
      auto slider_f = [&](float x, float y, float w, float &val, float vmin,
                          float vmax, const char *fmt, const char *iid) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(10, 10, 10, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                              IM_COL32(18, 18, 18, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                              IM_COL32(10, 10, 10, 255));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,
                              IM_COL32(175, 175, 175, 255));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,
                              IM_COL32(215, 215, 215, 255));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 12.f);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 12.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);

        // Value text right of slider
        char valbuf[32];
        snprintf(valbuf, sizeof(valbuf), fmt, val);
        ImVec2 vsz = ImGui::CalcTextSize(valbuf);
        float slider_w = w - vsz.x - 8.f;

        ImGui::SetCursorPos({x, y});
        ImGui::PushItemWidth(slider_w);
        ImGui::SliderFloat(iid, &val, vmin, vmax, "");
        ImGui::PopItemWidth();

        // Value on right
        ImGui::SetCursorPos({x + slider_w + 4.f,
                             y + (ctrl_h - ImGui::GetTextLineHeight()) * 0.5f});
        ImGui::TextColored(ImVec4{0.9f, 0.9f, 0.9f, 1.f}, "%s", valbuf);

        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor(6);

        // Min/max labels below
        if (ui.font_small)
          ImGui::PushFont(ui.font_small);
        char mnbuf[16], mxbuf[16];
        snprintf(mnbuf, sizeof(mnbuf), fmt, vmin);
        snprintf(mxbuf, sizeof(mxbuf), fmt, vmax);
        float sub_y = y + ctrl_h + 2.f;
        ImGui::SetCursorPos({x, sub_y});
        ImGui::TextColored(ImVec4{0.35f, 0.35f, 0.35f, 1.f}, "%s", mnbuf);
        ImVec2 mxsz = ImGui::CalcTextSize(mxbuf);
        ImGui::SetCursorPos({x + slider_w - mxsz.x, sub_y});
        ImGui::TextColored(ImVec4{0.35f, 0.35f, 0.35f, 1.f}, "%s", mxbuf);
        if (ui.font_small)
          ImGui::PopFont();
      };

      auto slider_i = [&](float x, float y, float w, int &val, int vmin,
                          int vmax, const char *iid) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(10, 10, 10, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                              IM_COL32(18, 18, 18, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                              IM_COL32(10, 10, 10, 255));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab,
                              IM_COL32(175, 175, 175, 255));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,
                              IM_COL32(215, 215, 215, 255));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 12.f);
        ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 12.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);

        char valbuf[32];
        snprintf(valbuf, sizeof(valbuf), "%d ms", val);
        ImVec2 vsz = ImGui::CalcTextSize(valbuf);
        float slider_w = w - vsz.x - 8.f;

        ImGui::SetCursorPos({x, y});
        ImGui::PushItemWidth(slider_w);
        ImGui::SliderInt(iid, &val, vmin, vmax, "");
        ImGui::PopItemWidth();

        ImGui::SetCursorPos({x + slider_w + 4.f,
                             y + (ctrl_h - ImGui::GetTextLineHeight()) * 0.5f});
        ImGui::TextColored(ImVec4{0.9f, 0.9f, 0.9f, 1.f}, "%s", valbuf);

        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor(6);

        if (ui.font_small)
          ImGui::PushFont(ui.font_small);
        char mnbuf[16], mxbuf[16];
        snprintf(mnbuf, sizeof(mnbuf), "%d", vmin);
        snprintf(mxbuf, sizeof(mxbuf), "%d", vmax);
        float sub_y = y + ctrl_h + 2.f;
        ImGui::SetCursorPos({x, sub_y});
        ImGui::TextColored(ImVec4{0.35f, 0.35f, 0.35f, 1.f}, "%s", mnbuf);
        ImVec2 mxsz = ImGui::CalcTextSize(mxbuf);
        ImGui::SetCursorPos({x + slider_w - mxsz.x, sub_y});
        ImGui::TextColored(ImVec4{0.35f, 0.35f, 0.35f, 1.f}, "%s", mxbuf);
        if (ui.font_small)
          ImGui::PopFont();
      };

      if (is_trigger) {
        // Push global style to kill blue selection
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(50, 50, 50, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                              IM_COL32(62, 62, 62, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(38, 38, 38, 255));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(18, 18, 18, 255));

        // Count how many cards we have → position from top of the usable area
        float card_h_slim = pad_y + lbl_h + 4.f + ctrl_h +
                            pad_y; // bind|bind or bind|slider-no-sub
        float card_h_slide = pad_y + lbl_h + 4.f + ctrl_h + 16.f +
                             pad_y; // rows with min/max sub-label

        int num_cards = prod.has_silent ? 3 : 2;
        float total_h =
            (float)num_cards * card_h_slide + (float)(num_cards - 1) * card_gap;

        float available_top = 75.f;
        float available_bottom = btn_y_abs;
        float available_center_y = (available_top + available_bottom) * 0.5f;
        float start_y = available_center_y - (total_h * 0.5f);
        float cy = start_y;

        if (prod.has_silent) {
          // ── Row 1: Бинд (Silent) | FOV
          float inner_x1 = card_x + pad_x;
          float inner_x2 = card_x + card_w * 0.5f + 6.f;
          float ctrl_y = cy + pad_y + lbl_h + 4.f;

          lbl(inner_x1, cy + pad_y, u8"Бинд Silent");
          bind_btn(inner_x1, ctrl_y, half_w - pad_x, ui.silent_bind_vk,
                   ui.waiting_silent_bind, "##sbnd");

          lbl(inner_x2, cy + pad_y, u8"FOV");
          slider_f(inner_x2, ctrl_y, half_w - pad_x, ui.silent_fov, 0.f, 180.f,
                   "%.0f", "##sfov");

          cy += card_h_slide + card_gap;

          // ── Row 2: Кость | Кнопка паники
          ctrl_y = cy + pad_y + lbl_h + 4.f;

          lbl(inner_x1, cy + pad_y, u8"Кость");

          // Combo styled as dark button with bone icon + chevron
          {
            const char *hitbox_items[] = {
                u8"\U0001F9B4 Голова", u8"\U0001F9B4 Грудь", u8"\U0001F9B4 Пах",
                u8"\U0001F9B4 Nearest"};
            const char *plain_items[] = {u8"Голова", u8"Грудь", u8"Пах",
                                         u8"Nearest"};
            ImGui::SetCursorPos({inner_x1, ctrl_y});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
            float pad_y_frame = (32.f - ImGui::GetFontSize()) * 0.5f;
            ImGui::PushStyleVar(
                ImGuiStyleVar_FramePadding,
                ImVec2(ImGui::GetStyle().FramePadding.x, pad_y_frame));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(22, 22, 22, 255));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                                  IM_COL32(38, 38, 38, 255));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                                  IM_COL32(55, 55, 55, 255));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(210, 210, 210, 255));
            ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(55, 55, 55, 255));
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(22, 22, 22, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  IM_COL32(38, 38, 38, 255));
            // Arrow color
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,
                                  IM_COL32(120, 120, 120, 255));
            ImGui::PushItemWidth(half_w - pad_x);
            ImGui::Combo("##shitbox", &ui.silent_hitbox, plain_items, 4);
            ImGui::PopItemWidth();
            ImGui::PopStyleColor(8);
            ImGui::PopStyleVar(3);
          }

          lbl(inner_x2, cy + pad_y, u8"Кнопка паники");
          bind_btn(inner_x2, ctrl_y, half_w - pad_x, ui.panic_bind_vk,
                   ui.waiting_panic_bind, "##pbnd");

          cy += card_h_slim + card_gap;

          // ── Row last: Бинд (Trigger) | Задержка
          {
            float ctrl_y_last = cy + pad_y + lbl_h + 4.f;

            lbl(inner_x1, cy + pad_y, u8"Бинд (Trigger)");
            bind_btn(inner_x1, ctrl_y_last, half_w - pad_x, ui.trig_bind_vk,
                     ui.waiting_trig_bind, "##tbnd");

            lbl(inner_x2, cy + pad_y, u8"Задержка");
            slider_i(inner_x2, ctrl_y_last, half_w - pad_x, ui.trig_delay_ms, 0,
                     5, "##tdelay");
          }
        } else {
          // No silent: Row 1 is Delay (Full width), Row 2 is Trigger Bind |
          // Panic Bind
          float inner_x1 = card_x + pad_x;
          float full_w = card_w - pad_x * 2.f;
          float ctrl_y = cy + pad_y + lbl_h + 4.f;

          lbl(inner_x1, cy + pad_y, u8"Задержка");
          slider_i(inner_x1, ctrl_y, full_w, ui.trig_delay_ms, 0, 5,
                   "##tdelay");

          cy += card_h_slide + card_gap;

          float inner_x2 = card_x + card_w * 0.5f + 6.f;
          ctrl_y = cy + pad_y + lbl_h + 4.f;

          lbl(inner_x1, cy + pad_y, u8"Бинд (Trigger)");
          bind_btn(inner_x1, ctrl_y, half_w - pad_x, ui.trig_bind_vk,
                   ui.waiting_trig_bind, "##tbnd");

          lbl(inner_x2, cy + pad_y, u8"Кнопка паники");
          bind_btn(inner_x2, ctrl_y, half_w - pad_x, ui.panic_bind_vk,
                   ui.waiting_panic_bind, "##pbnd");
        }

        ImGui::PopStyleColor(4);

        // ── Bind key capture (with 150ms cooldown to skip the activating
        // click)
        if (ui.waiting_trig_bind || ui.waiting_silent_bind ||
            ui.waiting_panic_bind) {
          double elapsed = ImGui::GetTime() - ui.bind_capture_start;
          if (elapsed > 0.15) { // wait 150ms before reading keys
            for (int vk = 1; vk < 256; vk++) {
              if (vk == VK_LBUTTON)
                continue; // skip LMB (used to click the bind button)
              // Check both currently-held (0x8000) and recently-pressed (bit 0)
              // to catch fast key taps
              if (GetAsyncKeyState(vk) & 0x8001) {
                if (ui.waiting_trig_bind)
                  ui.trig_bind_vk = vk;
                else if (ui.waiting_silent_bind)
                  ui.silent_bind_vk = vk;
                else if (ui.waiting_panic_bind) {
                  ui.panic_bind_vk = vk;
                  ui.panic_bind_set_time =
                      ImGui::GetTime(); // запоминаем время установки
                }
                ui.waiting_trig_bind = ui.waiting_silent_bind =
                    ui.waiting_panic_bind = false;
                break;
              }
            }
          }
        }
      }

       
      // Inject / Enable / Clean Buttons (fixed at bottom)
       
      float btn_y = block_h - 46.f - 24.f;
      float btn_w_h = (box_w - 12.f) * 0.5f;

      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.f);

      if (ui.phase == Phase::Launch) {
        ImGui::SetCursorPos({32.f, btn_y});
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(36, 36, 38, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              IM_COL32(46, 46, 48, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(26, 26, 28, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 230, 230, 255));
        if (ui.busy_prepare) {
          ImGui::Button(u8"Loading...", {btn_w_h, 46.f});
        } else {
          ui.prepare_start_time = 0.f;
          if (ImGui::Button(u8"Launch##lnch", {btn_w_h, 46.f})) {
            if (ui.fn_prepare) {
              ui.busy_prepare = true;
              ui.last_prepare_result = "__pending__";
              ui.prepare_start_time = (float)ImGui::GetTime();
            }
          }
        }
        ImGui::PopStyleColor(4);
      } else if (ui.phase == Phase::Ready) {
        ImGui::SetCursorPos({32.f, btn_y});
        if (ui.busy_inject) {
          ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(36, 36, 38, 255));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                IM_COL32(36, 36, 38, 255));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                IM_COL32(36, 36, 38, 255));
          ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 230, 230, 255));
          ImGui::Button(u8"Loading...", {btn_w_h, 46.f});
          ImGui::PopStyleColor(4);
        } else {
          ui.inject_start_time = 0.f;
          ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(74, 222, 128, 200));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                IM_COL32(74, 222, 128, 255));
          ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                IM_COL32(74, 222, 128, 150));
          ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 255));
          if (ImGui::Button(u8"Enable##enbl", {btn_w_h, 46.f})) {
            if (ui.fn_inject) {
              ui.busy_inject = true;
              ui.last_inject_result = "__pending__";
              ui.inject_start_time = (float)ImGui::GetTime();
            }
          }
          ImGui::PopStyleColor(4);
        }
      } else {
        // Phase::Enabled
        ImGui::SetCursorPos({32.f, btn_y});
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(180, 30, 30, 220));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              IM_COL32(210, 40, 40, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              IM_COL32(140, 20, 20, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
        if (ImGui::Button(u8"Disable##dsbl", {btn_w_h, 46.f})) {
          if (ui.fn_disable)
            ui.fn_disable();
          // Trigger: драйвер остался — можно сразу Enable снова (Ready)
          // External: нужно заново скачать с сервера (Launch)
          ui.phase = is_trigger ? Phase::Ready : Phase::Launch;
        }
        ImGui::PopStyleColor(4);
      }

      ImGui::SetCursorPos({32.f + btn_w_h + 12.f, btn_y});
      ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(36, 36, 38, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(46, 46, 48, 255));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(26, 26, 28, 255));
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 230, 230, 255));
      if (ImGui::Button("Clean", {btn_w_h, 46.f})) {
        if (!ui.busy_cleanup) {
          if (ui.fn_cleanup)
            ui.fn_cleanup();
          ui.busy_cleanup = true;
          ui.cleanup_start_time = (float)ImGui::GetTime();
        }
      }
      ImGui::PopStyleColor(4);
      ImGui::PopStyleVar(); // FrameRounding
    }
  }
  ImGui::EndChild();



  if (ui.busy_prepare || ui.busy_inject) {
    float progress = 0.f;
    const char *text = "";
    if (ui.busy_prepare) {
      float t = (float)ImGui::GetTime();
      float elapsed = t - ui.prepare_start_time;
      if (elapsed < 4.5f) {
        progress = elapsed / 4.5f;
        text = "Loading driver...";
      } else {
        progress = (elapsed - 4.5f) / 10.0f;
        if (progress >= 0.99f) {
          progress = 0.99f;
          text = "Waiting...";
        } else {
          text = "Loading dll...";
        }
      }
    } else {
      float t = (float)ImGui::GetTime();
      progress = (t - ui.inject_start_time) / 2.0f;
      if (progress > 0.99f)
        progress = 0.99f;
      text = "Loading cheat...";
    }

    ImDrawList *fg_dl = ImGui::GetForegroundDrawList();

    fg_dl->AddRectFilled(wpos, {wpos.x + wsz.x, wpos.y + wsz.y},
                         IM_COL32(0, 0, 0, 255), 15.f);

    float pw = 300.f;
    float ph = 8.f;
    ImVec2 c = {wpos.x + wsz.x * 0.5f, wpos.y + wsz.y * 0.5f};

    ImVec2 p0 = {c.x - pw * 0.5f, c.y - ph * 0.5f};
    ImVec2 p1 = {c.x + pw * 0.5f, c.y + ph * 0.5f};
    fg_dl->AddRectFilled(p0, p1, IM_COL32(50, 50, 50, 255), ph * 0.5f);

    ImVec2 p_prog = {p0.x + pw * progress, p1.y};
    fg_dl->PushClipRect(p0, p_prog, true);
    fg_dl->AddRectFilled(p0, p1, IM_COL32(74, 222, 128, 255), ph * 0.5f);
    fg_dl->PopClipRect();

    if (ui.font_bold)
      ImGui::PushFont(ui.font_bold);
    ImVec2 tsz = ImGui::CalcTextSize(text);
    fg_dl->AddText({c.x - tsz.x * 0.5f, p0.y - tsz.y - 12.f},
                   IM_COL32(255, 255, 255, 255), text);
    if (ui.font_bold)
      ImGui::PopFont();

    char pct[32];
    snprintf(pct, sizeof(pct), "%d%%", (int)(progress * 100.f));
    ImVec2 ptsz = ImGui::CalcTextSize(pct);
    fg_dl->AddText({c.x - ptsz.x * 0.5f, p1.y + 12.f},
                   IM_COL32(200, 200, 200, 255), pct);
  }

  // ─── CLEANUP OVERLAY (синяя полоса + меняющийся текст)
  if (ui.busy_cleanup) {
    // Читаем реальный прогресс из clean_pipeline.hpp
    float prog = shadow_pipeline::g_progress.load();
    bool done = shadow_pipeline::g_done.load();

    // Текст меняется по порогам прогресса
    const char *stage_text;
    if (prog < 0.05f)
      stage_text = "Preparing cleanup...";
    else if (prog < 0.12f)
      stage_text = "Removing temp files...";
    else if (prog < 0.20f)
      stage_text = "Wiping Prefetch logs...";
    else if (prog < 0.25f)
      stage_text = "Wiping Shimcache...";
    else if (prog < 0.30f)
      stage_text = "Wiping PCA logs...";
    else if (prog < 0.35f)
      stage_text = "Removing Amcache.hve...";
    else if (prog < 0.43f)
      stage_text = "Wiping BAM/DAM registry...";
    else if (prog < 0.48f)
      stage_text = "Wiping Explorer history...";
    else if (prog < 0.52f)
      stage_text = "Wiping Event Log...";
    else if (prog < 0.56f)
      stage_text = "Flushing DNS / ARP cache...";
    else if (prog < 0.64f)
      stage_text = "Removing USN Journal...";
    else if (prog < 0.70f)
      stage_text = "Wiping SRUM (network activity)...";
    else if (prog < 0.74f)
      stage_text = "Wiping activity history...";
    else if (prog < 0.77f)
      stage_text = "Removing Shadow Copies...";
    else if (prog < 0.82f)
      stage_text = "Wiping WER / crash dumps...";
    else if (prog < 0.86f)
      stage_text = "Wiping Windows Search index...";
    else if (prog < 0.90f)
      stage_text = "Wiping PowerShell history...";
    else if (prog < 0.94f)
      stage_text = "Wiping network traces...";
    else if (prog < 0.98f)
      stage_text = "Zeroing free space...";
    else
      stage_text = "Finalizing...";

    ImDrawList *fg = ImGui::GetForegroundDrawList();

    // Чёрный оверлей на всё окно
    fg->AddRectFilled(wpos, {wpos.x + wsz.x, wpos.y + wsz.y},
                      IM_COL32(0, 0, 0, 252), 15.f);

    float pw = 300.f;
    float ph = 8.f;
    ImVec2 c = {wpos.x + wsz.x * 0.5f, wpos.y + wsz.y * 0.5f};
    ImVec2 p0 = {c.x - pw * 0.5f, c.y - ph * 0.5f};
    ImVec2 p1 = {c.x + pw * 0.5f, c.y + ph * 0.5f};

    // Трек полосы (серый)
    fg->AddRectFilled(p0, p1, IM_COL32(50, 50, 50, 255), ph * 0.5f);

    if (!done) {
      // СИНЯЯ полоса (идёт от 0 до 100 без сброса)
      ImVec2 p_fill = {p0.x + pw * prog, p1.y};
      fg->PushClipRect(p0, p_fill, true);
      fg->AddRectFilled(p0, p1, IM_COL32(56, 189, 248, 255),
                        ph * 0.5f); // голубой #38bdf8
      fg->PopClipRect();

      // Текст над полосой (меняется)
      if (ui.font_bold)
        ImGui::PushFont(ui.font_bold);
      ImVec2 tsz = ImGui::CalcTextSize(stage_text);
      fg->AddText({c.x - tsz.x * 0.5f, p0.y - tsz.y - 14.f},
                  IM_COL32(255, 255, 255, 255), stage_text);
      if (ui.font_bold)
        ImGui::PopFont();

      // Процент под полосой
      char pct[32];
      snprintf(pct, sizeof(pct), "%d%%", (int)(prog * 100.f));
      ImVec2 ptsz = ImGui::CalcTextSize(pct);
      fg->AddText({c.x - ptsz.x * 0.5f, p1.y + 14.f},
                  IM_COL32(150, 210, 255, 255), pct);
    } else {
      // Полоса 100%
      fg->AddRectFilled(p0, p1, IM_COL32(56, 189, 248, 255), ph * 0.5f);

      // Заголовок
      if (ui.font_bold)
        ImGui::PushFont(ui.font_bold);
      const char *ok_text = "Wiped successfully";
      ImVec2 osz = ImGui::CalcTextSize(ok_text);
      fg->AddText({c.x - osz.x * 0.5f, p0.y - osz.y - 14.f},
                  IM_COL32(56, 189, 248, 255), ok_text);
      if (ui.font_bold)
        ImGui::PopFont();

      // Отсчёт 5..4..3..2..1
      // Таймер: запоминаем момент done
      static float done_time = 0.f;
      static bool done_caught = false;
      if (!done_caught) {
        done_time = (float)ImGui::GetTime();
        done_caught = true;
      }
      float since_done = (float)ImGui::GetTime() - done_time;
      int countdown = 5 - (int)since_done;
      if (countdown <= 0) {
        countdown = 0;
        ui.want_close = true; // Принудительно закрываем UI, когда таймер истек
      }

      char cnt_buf[64];
      snprintf(cnt_buf, sizeof(cnt_buf), "Loader will close in: %d", countdown);
      ImVec2 csz = ImGui::CalcTextSize(cnt_buf);
      fg->AddText({c.x - csz.x * 0.5f, p1.y + 14.f},
                  IM_COL32(200, 200, 200, 255), cnt_buf);
    }
  }
}

 
//  Render — Toast notifications
 
static void RenderToasts(LoaderUI &ui, float dt, ImVec2 window_pos,
                         ImVec2 window_size) {
  float y_off = window_pos.y + window_size.y - 24.f;

  for (int i = (int)ui.toasts.size() - 1; i >= 0; i--) {
    auto &t = ui.toasts[i];
    t.life -= dt;
    if (t.life <= 0.f) {
      ui.toasts.erase(ui.toasts.begin() + i);
      continue;
    }

    float alpha =
        t.life < 0.3f
            ? t.life / 0.3f
            : (t.total - t.life < 0.25f ? (t.total - t.life) / 0.25f : 1.f);

    ImVec4 bg_v = (t.type == ToastType::Error)     ? Colors::TOAST_ERR
                  : (t.type == ToastType::Success) ? Colors::TOAST_OK
                                                   : Colors::TOAST_INFO;
    ImU32 bg = IM_COL32((int)(bg_v.x * 255), (int)(bg_v.y * 255),
                        (int)(bg_v.z * 255), (int)(alpha * 220));

    ImVec2 ts = ImGui::CalcTextSize(t.text.c_str());
    float pw = ts.x + 40.f, ph = ts.y + 24.f;
    float px = window_pos.x + (window_size.x - pw) * 0.5f;
    float py = y_off - ph;

    ImDrawList *ol = ImGui::GetForegroundDrawList();
    ol->AddRectFilled({px, py}, {px + pw, py + ph}, bg, 10.f);
    ol->AddText({px + 20.f, py + 12.f},
                IM_COL32(255, 255, 255, (int)(alpha * 255)), t.text.c_str());

    y_off -= ph + 8.f;
  }
}

 
//  Render — Success Modal
 
static bool RenderSuccessModal(bool &open, ImVec2 win_pos, ImVec2 win_sz) {
  if (!open)
    return false;

  ImDrawList *dl = ImGui::GetForegroundDrawList();
  // Overlay
  dl->AddRectFilled(win_pos, {win_pos.x + win_sz.x, win_pos.y + win_sz.y},
                    IM_COL32(0, 0, 0, 153));

  // Modal box (320x200 по центру)
  float mw = 320.f, mh = 200.f;
  float mx = win_pos.x + (win_sz.x - mw) * 0.5f;
  float my = win_pos.y + (win_sz.y - mh) * 0.5f;
  dl->AddRectFilled({mx, my}, {mx + mw, my + mh}, IM_COL32(28, 32, 35, 255),
                    16.f);
  dl->AddRect({mx, my}, {mx + mw, my + mh}, IM_COL32(255, 255, 255, 25), 16.f);

  // Checkmark circle
  dl->AddCircle({mx + mw * 0.5f, my + 52.f}, 24.f, IM_COL32(74, 222, 128, 255),
                32, 2.f);
  dl->AddText({mx + mw * 0.5f - 8.f, my + 40.f}, IM_COL32(74, 222, 128, 255),
              "✓");

  // Title
  const char *title = u8"Запущено успешно";
  ImVec2 ts = ImGui::CalcTextSize(title);
  dl->AddText({mx + (mw - ts.x) * 0.5f, my + 90.f},
              IM_COL32(255, 255, 255, 255), title);

  // Subtitle
  const char *sub = u8"Продукт успешно инжектирован.";
  ImVec2 ss = ImGui::CalcTextSize(sub);
  dl->AddText({mx + (mw - ss.x) * 0.5f, my + 118.f},
              IM_COL32(255, 255, 255, 140), sub);

  // Close button (X)
  ImGui::SetNextWindowPos({mx + mw - 32.f, my + 8.f});
  if (ImGui::IsMouseClicked(0)) {
    ImVec2 mp = ImGui::GetMousePos();
    bool in_box =
        mp.x >= mx && mp.x <= mx + mw && mp.y >= my && mp.y <= my + mh;
    if (!in_box) {
      open = false;
    }
    // Close X area (top-right corner of modal)
    if (mp.x >= mx + mw - 32.f && mp.x <= mx + mw - 8.f && mp.y >= my + 8.f &&
        mp.y <= my + 32.f)
      open = false;
  }
  return true; // still open
}
