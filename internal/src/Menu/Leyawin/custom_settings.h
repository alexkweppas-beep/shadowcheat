#pragma once

#include "../IconsFontAwesome6.h"
#include "imgui.h"
#include <d3d11.h>

class c_settings {
public:
  DWORD popup_flags =
      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing;
  DWORD window_flags = ImGuiWindowFlags_NoDecoration;

  ImVec2 window_size{1160.f, 740.f};
  ImVec2 window_padding{};
  ImVec2 widgets_padding{10.f, 10.f};
  ImVec2 widgets_spacing{0.f, 10.f};

  bool watermark = false;
  bool show_menu = true;
  bool particle_cursor = false;
  bool particle_clicked = false;
  bool obs_bypass = false;
  bool nvidia_bypass = false;
  bool window_gradient = true;

  float window_rounding = 8.f;
  float window_border_size = 0.f;
  float window_scrollbar_size = 8.f;
  float progress = 50.f;

  int particle_gravity = 500;
  int particle_num = 10;

  float particle_color[4] = {255 / 255.f, 255 / 255.f, 255 / 255.f, 1.f};

  int selection_count = 0;
  int sub_selection_count = 0;
  int selection_accept = 0;
  int config_count = 0;

  float selection_alpha = 1.f;
  float selection_add = 0.f;
  float selection_width = 0.f;
  float menu_alpha = 1.f;

  char create_config[15] = {""};

  static constexpr int kSelectionCount = 8;
  static constexpr int kSubSelectionCount = 4;
  static constexpr int kConfigSlots = 8;

  const char *selection_icons[kSelectionCount] = {
      ICON_FA_CROSSHAIRS, ICON_FA_EYE,     ICON_FA_CODE, ICON_FA_USER_GROUP,
      ICON_FA_CUBES,      ICON_FA_TOOLBOX, ICON_FA_GEAR, ICON_FA_FLAG};
  const char *selection_labels[kSelectionCount] = {
      "Combat", "Visuals", "Scripts",  "Social",
      "Quant",  "Misc",    "Settings", "Ukraine"};
  const char *sub_selection[kSubSelectionCount] = {"Thread", "Enemy", "Other",
                                                   "Team"};

  char selection_config[kConfigSlots][15] = {};

  ImVec2 child_spacing;

  float child_rounding = 6.f;
  float selection_rounding = 4.f;
  float checkbox_rounding = 2.f;
  float input_rounding = 3.f;
  float color_rounding = 2.f;
  float keybind_rounding = 2.f;
  float watermark_rounding = 3.f;
  float combo_rounding = 3.f;
  float content_size = 0.f;
  float combo_spacing = 10.f;

  ID3D11ShaderResourceView *background_texture = nullptr;
  ID3D11ShaderResourceView *logo = nullptr;

  ImFont *poppins_selection = nullptr;
  ImFont *poppins_widget = nullptr;
  ImFont *inter_logo = nullptr;
  ImFont *icon = nullptr;
  ImFont *icon_cfg = nullptr;
  ImFont *icon_child = nullptr;
  ImFont *icon_micro = nullptr;
};

class c_function {
public:
  bool enable = true;
  bool auto_fire = true;
  bool silent_aim = false;
  bool auto_revolver = false;
  bool anti_step = true;
  bool draw_fov = true;
  bool visible_fov = false;
  bool head_safety_if_lethal = true;
  bool auto_step = true;
  bool auto_scope = false;

  bool autofire = true;
  bool hide_shots = true;
  bool double_tap = false;
  bool mode = false;
  bool teleport_boots = true;
  bool body_set[5] = {true, true, true, true, false};
  bool misses_walls = true;

  bool show_in_enable = true;
  bool show_in_fire = true;
  bool show_in_safety = true;
  bool show_in_modem = true;
  bool show_in_step = true;

  float silent_count_size = 0.5f;
  float backtracking = 100.f;
  float visible = 50.f;
  float auto_wall = 50.f;
  float picker_checkbox[4] = {140 / 255.f, 155 / 255.f, 253 / 255.f, 1.f};

  float accent_color[4] = {161.f / 255.f, 129.f / 255.f, 212.f / 255.f,
                           1.f}; // #CD81D4

  int first_bullet_delay = 0;
  int after_kill = 500;
  int max_misses = 100;
  int visible_count = 1;
  int force_count = 1;
  int mark_count = 1;
  int conditions_count = 0;
  int body_count = 1;
  int hitchance = 50;

  int enable_key = 0;
  int mode_key = 0;
  int enable_fire = 0;
  int mode_fire = 0;
  int enable_safety = 0;
  int mode_safety = 0;
  int enable_modem = 0;
  int mode_modem = 0;
  int enable_step = 0;
  int mode_step = 0;

  const char *conditions_list[8] = {"Select", "List",   "Status", "Test",
                                    "Bruh",   "Struct", "Pack",   "Hack"};
  const char *visible_list[3] = {"Default", "Selection", "Inactive"};
  const char *force_list[3] = {"Select", "Type", "Count"};
  const char *body_list[5] = {"Head", "Body", "Chest", "Neck", "Legs"};
  const char *mark_list[4] = {"Upper Left", "Upper Right", "Bottom Left",
                              "Bottom Right"};

  char input[55] = {""};
};
