#include "../../config/config.hpp"
#include "../../core/globals.hpp"
#include "../../sdk/math_utils.hpp"
#include "../../security/security.hpp"
#include "../../security/vmp_markers.hpp"
#include "headers/includes.h"
#include "security/xorstr.hpp"
#include <algorithm>
#include <settings.hpp>


void c_gui::render() {
  SHADOW_PROTECT_MUT(xorstr_("gui_render"));
  static char new_cfg_name[64] = "";
  static float last_refresh = 0.f;

  // Ensure sf_pro is the default ImGui font (index 0)
  font->get(sf_pro, 14);

  if (var->img.background) {
    draw->image(gui->background_drawlist(), var->img.background, {0, 0},
                {1920, 1080}, {0, 0}, {1, 1});
  }

  bool tab_changed = var->tab.tab_current != var->tab.tab_stored ||
                     var->tab.subtab_current != var->tab.subtab_stored ||
                     var->tab.item_current != var->tab.item_stored;

  gui->easing(var->tab.alpha, tab_changed ? 0.f : 1.f, 7.f, static_easing);
  if (var->tab.alpha == 0) {
    if (var->tab.tab_current != var->tab.tab_stored) {
      var->tab.item_current = 0;
      var->tab.item_stored = 0;
      var->tab.subtab_current = 0;
      var->tab.subtab_stored = 0;
      var->tab.tab_current = var->tab.tab_stored;
    }

    if (var->tab.subtab_current != var->tab.subtab_stored) {
      var->tab.item_current = 0;
      var->tab.item_stored = 0;
      var->tab.subtab_current = var->tab.subtab_stored;
    }

    if (var->tab.item_current != var->tab.item_stored)
      var->tab.item_current = var->tab.item_stored;
  }

  if (IsKeyPressed(ImGuiKey_F1)) {
    var->gui.stored_dpi += 10;
    var->gui.dpi_changed = true;
  }
  if (IsKeyPressed(ImGuiKey_F2)) {
    var->gui.stored_dpi -= 10;
    var->gui.dpi_changed = true;
  }

  widgets->init_keybinds();

  var->blackout.active = false;

  gui->initialize();

  // Theme from loveup menu_cfg (menu-internal setting)
  int theme = menu_cfg->get<dropdown_t>(xorstr_("Theme")).callback;
  if (theme == 0) // Dark mode
  {
    clr->layout = {21, 22, 26};
    clr->white = {255, 255, 255};
    clr->black = {0, 0, 0};
    clr->child = {27, 28, 32};
    clr->text = {124, 128, 144};
    clr->widget = {32, 34, 39};
    clr->border = {63, 67, 84};
    clr->tab_layout = {81, 83, 93};
    clr->popup_widget = {42, 43, 53};
    clr->popup = {33, 35, 41};
  } else if (theme == 1) // Light mode
  {
    clr->layout = {240, 240, 245};
    clr->white = {20, 20, 20};
    clr->black = {255, 255, 255};
    clr->child = {250, 250, 250};
    clr->text = {100, 100, 110};
    clr->widget = {230, 230, 235};
    clr->border = {210, 210, 215};
    clr->tab_layout = {180, 180, 190};
    clr->popup_widget = {240, 240, 245};
    clr->popup = {255, 255, 255};
  }

  gui->set_next_window_size(SCALE(elements->window.size));
  gui->begin(elements->window.name, nullptr,
             window_flags_no_scrollbar | window_flags_no_scroll_with_mouse |
                 window_flags_no_bring_to_front_on_focus |
                 window_flags_no_focus_on_appearing |
                 window_flags_no_background | window_flags_no_decoration);
  {
    gui->set_style();
    gui->draw_decorations();

    gui->begin_content(
        xorstr_("nav"), SCALE(elements->nav.size), {0, 0}, {0, 0},
        window_flags_no_scrollbar | window_flags_no_scroll_with_mouse);
    {
      gui->dummy({gui->content_avail().x, SCALE(55)});
      draw->text_clipped(gui->window_drawlist(), font->get(syne, 22),
                         GetItemRectMin() + SCALE(20, 0), GetItemRectMax(),
                         draw->get_clr(clr->white), xorstr_("SHADOW"), 0, 0,
                         {0, 0.5});

      gui->begin_content(xorstr_("tabs"), gui->content_avail(), SCALE(12, 0),
                         {0, 0},
                         window_flags_no_scroll_with_mouse |
                             window_flags_no_scrollbar | window_flags_no_move);
      {
        var->tab.draw_list = gui->window_drawlist();

        for (int i = 0; i < var->tab.list.size(); ++i) {
          tab_t tab = var->tab.list[i];

          if (widgets->tab_button(tab.name, tab.icon,
                                  var->tab.tab_stored == i)) {
            var->tab.tab_stored = i;
          }
          if (tab.subtabs_list.size() > 0) {
            widgets->subtab_section(tab.name, var->tab.tab_stored == i,
                                    &var->tab.list[i].subtabs_list);
          }
        }
      }
      gui->end_content();
    }
    gui->end_content();

    gui->sameline();

    gui->begin_content(
        xorstr_("content"), SCALE(elements->content.size),
        SCALE(elements->content.padding), SCALE(elements->content.padding),
        window_flags_no_scrollbar | window_flags_no_scroll_with_mouse);
    {
      gui->begin_content(xorstr_("header"), {gui->content_avail().x, SCALE(40)},
                         SCALE(0, 0), SCALE(0, 0));
      {
        if (var->tab.tab_current == 5) // Configs tab
        {
          if (widgets->config_field(xorstr_("Enter config name"), new_cfg_name,
                                    sizeof(new_cfg_name))) {
            if (strlen(new_cfg_name) > 0) {
              config_system::save(new_cfg_name);
              new_cfg_name[0] = '\0';
              last_refresh = 0.f; // force refresh
            }
          }
        } else {
          widgets->search(xorstr_("Search functions"));
        }

        gui->sameline();

        {
          gui->dummy({gui->content_avail()});
          c_rect rect = {GetItemRectMin(), GetItemRectMax()};

          draw->text_clipped(gui->window_drawlist(), font->get(sf_pro, 14),
                             rect.Min + SCALE(0, 6), rect.Max - SCALE(5, 0),
                             draw->get_clr(clr->white), xorstr_("External"), 0,
                             0, {1, 0});
          draw->text_clipped(gui->window_drawlist(), font->get(sf_pro, 12),
                             rect.Min, rect.Max - SCALE(5, 6),
                             draw->get_clr(clr->text),
                             xorstr_("shadowcheats.cc"), 0, 0, {1, 1});
        }
      }
      gui->end_content();

      gui->begin_content(xorstr_("nav_info"),
                         {gui->content_avail().x, SCALE(20)}, SCALE(0, 0),
                         SCALE(0, 0));
      {
        c_rect rect = gui->get_window()->Rect();

        gui->push_var(style_var_alpha, var->tab.alpha);

        std::string tab_name = var->tab.list[var->tab.tab_current].name;
        c_vec2 tab_name_size =
            gui->text_size(font->get(sf_pro, 12), tab_name.data());

        draw->text_clipped(gui->window_drawlist(), font->get(sf_pro, 12),
                           rect.Min, rect.Max, draw->get_clr(clr->text),
                           tab_name.data(), 0, 0, {0, 0.5});

        std::string subtab_name;
        c_vec2 subtab_name_size;
        if (!var->tab.list[var->tab.tab_current].subtabs_list.empty() &&
            var->tab.list[var->tab.tab_current].subtabs_list.size() > 0) {
          int safe_subtab = var->tab.subtab_current;
          if (safe_subtab >=
              (int)var->tab.list[var->tab.tab_current].subtabs_list.size())
            safe_subtab = 0;

          subtab_name = var->tab.list[var->tab.tab_current]
                            .subtabs_list[safe_subtab]
                            .name;

          draw->text_clipped(gui->window_drawlist(), font->get(icons, 10),
                             rect.Min + c_vec2{tab_name_size.x + SCALE(10), 0},
                             rect.Max, draw->get_clr(clr->text), xorstr_("N"),
                             0, 0, {0, 0.5});
          draw->text_clipped(gui->window_drawlist(), font->get(sf_pro, 12),
                             rect.Min + c_vec2{tab_name_size.x + SCALE(30), 0},
                             rect.Max, draw->get_clr(clr->text),
                             subtab_name.data(), 0, 0, {0, 0.5});

          subtab_name_size =
              gui->text_size(font->get(sf_pro, 12), subtab_name.data());

          std::string item_name;
          if (!var->tab.list[var->tab.tab_current]
                   .subtabs_list[safe_subtab]
                   .weapons_list.empty() &&
              var->tab.list[var->tab.tab_current]
                      .subtabs_list[safe_subtab]
                      .weapons_list.size() > 0) {
            int safe_item = var->tab.item_current;
            if (safe_item >= (int)var->tab.list[var->tab.tab_current]
                                 .subtabs_list[safe_subtab]
                                 .weapons_list.size())
              safe_item = 0;

            item_name = var->tab.list[var->tab.tab_current]
                            .subtabs_list[safe_subtab]
                            .weapons_list[safe_item];

            draw->text_clipped(
                gui->window_drawlist(), font->get(icons, 10),
                rect.Min +
                    c_vec2{tab_name_size.x + subtab_name_size.x + SCALE(40), 0},
                rect.Max, draw->get_clr(clr->text), xorstr_("N"), 0, 0,
                {0, 0.5});
            draw->text_clipped(
                gui->window_drawlist(), font->get(sf_pro, 12),
                rect.Min +
                    c_vec2{tab_name_size.x + subtab_name_size.x + SCALE(60), 0},
                rect.Max, draw->get_clr(clr->text), item_name.data(), 0, 0,
                {0, 0.5});
          }
        }

        gui->pop_var();
      }
      gui->end_content();

      gui->push_var(style_var_scrollbar_border_padding, {0, 0});
      gui->push_var(style_var_scrollbar_content_padding, SCALE(8));
      gui->push_var(style_var_scrollbar_size, SCALE(10));
      gui->push_var(style_var_alpha, var->tab.alpha);

      gui->begin_content(xorstr_("features"), gui->content_avail(), SCALE(0, 0),
                         SCALE(10, 10));
      {
        elements->child.width = (gui->content_avail().x - SCALE(10)) / 2;
        var->widget.draw_list = gui->window_drawlist();

        // Helper aliases to the active weapon config slot
        auto &acfg = cfg::aimbot_configs[cfg::enable_weapon_configs
                                             ? cfg::active_weapon_category
                                             : 0];

        if (var->tab.tab_current == 0) // Aimbot
        {
          if (var->tab.subtab_current == 0) // Aimbot subtab
          {
            gui->begin_group();
            {
              widgets->child(xorstr_("I"), xorstr_("Aimbot Settings"));
              {
                // Enable Weapon Configs — directly writes cfg:: variable
                if (widgets->checkbox_direct(xorstr_("Enable Weapon Configs"),
                                             &cfg::enable_weapon_configs)) {
                }
                if (cfg::enable_weapon_configs) {
                  std::vector<std::string> cats{
                      xorstr_("Global"), xorstr_("Pistol"),  xorstr_("SMG"),
                      xorstr_("Rifle"),  xorstr_("Shotgun"), xorstr_("Sniper"),
                      xorstr_("Heavy")};
                  widgets->dropdown_direct(xorstr_("Weapon Category"),
                                           &cfg::active_weapon_category, cats);
                }
                gui->dummy({gui->content_avail().x, SCALE(10)});
                widgets->checkbox_direct(xorstr_("Enable Aimbot"),
                                         &acfg.aimbot);
                {
                  std::vector<std::string> aimtypes{xorstr_("Vector"),
                                                    xorstr_("Silent")};
                  widgets->dropdown_direct(xorstr_("Aim Type"), &acfg.aimtype,
                                           aimtypes);
                }
                widgets->slider_direct(xorstr_("FOV"), &acfg.fov, 0, 360,
                                       xorstr_("%.f°"));
                widgets->slider_direct(xorstr_("Distance"), &acfg.aim_distance,
                                       0, 1000, xorstr_("%.fm"));
                widgets->slider_direct(xorstr_("Hit Chance"), &acfg.aim_chance,
                                       0, 100, xorstr_("%.f%%"));
              }
              widgets->end_child();
            }
            gui->end_group();

            gui->sameline();

            gui->begin_group();
            {
              widgets->child(xorstr_("I"), xorstr_("Advanced Settings"));
              {
                {
                  std::vector<std::string> bones{
                      xorstr_("Head"), xorstr_("Neck"), xorstr_("Chest"),
                      xorstr_("Body"), xorstr_("Nearest")};
                  widgets->dropdown_direct(xorstr_("Aim Bone"), &acfg.aimbone,
                                           bones);
                }
                widgets->checkbox_direct(xorstr_("Draw FOV"), &acfg.draw_fov);
                if (acfg.draw_fov) {
                  widgets->colorpicker(xorstr_("FOV Color"),
                                       (c_vec4 *)&acfg.col_Fov.Value, true);
                  widgets->checkbox_direct(xorstr_("Only When Aiming"),
                                           &acfg.show_fov_only_aiming);
                }
                if (acfg.aimtype == 0) // Vector
                {
                  widgets->checkbox_direct(xorstr_("Smooth"), &acfg.smooth);
                  if (acfg.smooth) {
                    std::vector<std::string> stypes{xorstr_("Dynamic"),
                                                    xorstr_("Constant")};
                    widgets->dropdown_direct(xorstr_("Smooth Type"),
                                             &acfg.smooth_type, stypes);
                    widgets->slider_direct_f(xorstr_("Smooth Factor"),
                                             &acfg.smooth_factor, 1.f, 100.f,
                                             xorstr_("%.1f"));
                  }
                }
                widgets->checkbox_direct(xorstr_("Ignore Friends"),
                                         &acfg.ignore_friends);
                widgets->checkbox_direct(xorstr_("Ignore Admins"),
                                         &acfg.ignore_admins);
              }
              widgets->end_child();
            }
            gui->end_group();
          } else if (var->tab.subtab_current == 1) // Trigger
          {
            gui->begin_group();
            {
              widgets->child(xorstr_("W"), xorstr_("Triggerbot"));
              {
                widgets->checkbox_direct(xorstr_("Enable Triggerbot"),
                                         &cfg::triggerbot);
                {
                  int delay = cfg::triggerdelay;
                  if (widgets->slider_direct(xorstr_("Trigger Delay"), &delay,
                                             0, 500, xorstr_("%.f ms")))
                    cfg::triggerdelay = delay;
                }
                widgets->checkbox_direct(xorstr_("Ignore Friends"),
                                         &cfg::trigger_ignore_friends);
                widgets->checkbox_direct(xorstr_("Ignore Admins"),
                                         &cfg::trigger_ignore_admins);
              }
              widgets->end_child();
            }
            gui->end_group();
          } else if (var->tab.subtab_current == 2) // Weapon
          {
            gui->begin_group();
            {
              widgets->child(xorstr_("W"), xorstr_("Weapon Settings"));
              {
                widgets->checkbox_direct(xorstr_("No Recoil (detected)"),
                                         &cfg::weapon_no_recoil);
                widgets->checkbox_direct(xorstr_("No Spread (detected)"),
                                         &cfg::weapon_no_spread);
                widgets->checkbox_direct(xorstr_("Wallshot (detected)"),
                                         &cfg::enable_wallshot);
                widgets->checkbox_direct(xorstr_("Double Tap (detected)"),
                                         &cfg::weapon_double_tap);
              }
              widgets->end_child();
            }
            gui->end_group();
          }
        } else if (var->tab.tab_current == 1) // Visuals
        {
          if (var->tab.subtab_current == 0) // Players
          {
            gui->begin_group();
            {
              widgets->child(xorstr_("U"), xorstr_("ESP Base"));
              {
                widgets->checkbox_direct(xorstr_("2D Box"), &cfg::ESP_Box);
                if (cfg::ESP_Box) {
                  std::vector<std::string> btypes{
                      xorstr_("Border"), xorstr_("Corners"), xorstr_("Filled"),
                      xorstr_("Filled & Corners")};
                  widgets->dropdown_direct(xorstr_("Box Type"),
                                           &cfg::esp_players_box_type, btypes);
                }
                widgets->checkbox_direct(xorstr_("Health Bar"),
                                         &cfg::ESP_HealthBar);
                if (cfg::ESP_HealthBar) {
                  std::vector<std::string> hbpos{
                      xorstr_("Top"), xorstr_("Bottom"), xorstr_("Left"),
                      xorstr_("Right")};
                  widgets->dropdown_direct(xorstr_("Health Bar Position"),
                                           &cfg::esp_players_bar_position,
                                           hbpos);
                  widgets->dropdown_direct(xorstr_("Armor Bar Pos"),
                                           &cfg::esp_players_armor_bar_position,
                                           hbpos);
                  widgets->checkbox_direct(xorstr_("Custom Health Colors"),
                                           &cfg::esp_custom_health_colors);
                }
                widgets->checkbox_direct(xorstr_("Draw Distance"),
                                         &cfg::ESP_Distance);
                widgets->checkbox_direct(xorstr_("Draw Weapon"),
                                         &cfg::ESP_Weapon);
                widgets->checkbox_direct(xorstr_("Skeleton Lines"),
                                         &cfg::ESP_SkeletonLines);
              }
              widgets->end_child();
            }
            gui->end_group();

            gui->sameline();

            gui->begin_group();
            {
              widgets->child(xorstr_("U"), xorstr_("ESP Advanced"));
              {
                // Sliders first
                widgets->slider_direct_f(xorstr_("Max Distance"),
                                         &cfg::ESP_MaxDistance, 50.f, 1000.f,
                                         xorstr_("%.fm"));

                if (cfg::ESP_Box) {
                  widgets->slider_direct_f(xorstr_("Box Thickness"),
                                           &cfg::esp_box_thickness, 1.f, 5.f,
                                           xorstr_("%.1f"));
                  widgets->slider_direct_f(xorstr_("Box Rounding"),
                                           &cfg::esp_box_rounding, 0.f, 15.f,
                                           xorstr_("%.1f"));
                }

                widgets->slider_direct_f(xorstr_("Skeleton Thickness"),
                                         &cfg::esp_skeleton_thickness, 1.f, 5.f,
                                         xorstr_("%.1f"));

                // Colors below
                if (cfg::ESP_Box)
                  widgets->colorpicker(xorstr_("Box Color"),
                                       (c_vec4 *)&cfg::col_players_box.Value,
                                       true);

                if (cfg::ESP_HealthBar && cfg::esp_custom_health_colors) {
                  widgets->colorpicker(xorstr_("Health Color"),
                                       (c_vec4 *)&cfg::col_health.Value, true);
                  widgets->colorpicker(xorstr_("Armor Color"),
                                       (c_vec4 *)&cfg::col_armor.Value, true);
                }

                if (cfg::ESP_Distance)
                  widgets->colorpicker(
                      xorstr_("Distance Color"),
                      (c_vec4 *)&cfg::col_players_distance.Value, true);

                if (cfg::ESP_Weapon)
                  widgets->colorpicker(xorstr_("Weapon Color"),
                                       (c_vec4 *)&cfg::col_players_weapon.Value,
                                       true);

                if (cfg::ESP_SkeletonLines)
                  widgets->colorpicker(
                      xorstr_("Skeleton Color"),
                      (c_vec4 *)&cfg::col_players_skeleton.Value, true);

                widgets->checkbox_direct(xorstr_("Snap Line"), &cfg::ESP_Line);
                if (cfg::ESP_Line)
                  widgets->colorpicker(xorstr_("Snap Line Color"),
                                       (c_vec4 *)&cfg::col_players_line.Value,
                                       true);

                widgets->checkbox_direct(xorstr_("Aim Line"),
                                         &cfg::ESP_AimLine);
                if (cfg::ESP_AimLine)
                  widgets->colorpicker(xorstr_("Aim Line Color"),
                                       (c_vec4 *)&cfg::col_AimLine.Value, true);
              }
              widgets->end_child();
            }
            gui->end_group();
          } else if (var->tab.subtab_current == 1) // Friends
          {
            gui->begin_group();
            {
              widgets->child(xorstr_("F"), xorstr_("Friends ESP Base"));
              {
                widgets->checkbox_direct(xorstr_("Friends 2D Box"),
                                         &cfg::ESP_Friends_Box);
                if (cfg::ESP_Friends_Box) {
                  std::vector<std::string> btypes{
                      xorstr_("Border"), xorstr_("Corners"), xorstr_("Filled"),
                      xorstr_("Filled & Corners")};
                  widgets->dropdown_direct(xorstr_("Box Type"),
                                           &cfg::esp_friends_box_type, btypes);
                }
                widgets->checkbox_direct(xorstr_("Friends Health Bar"),
                                         &cfg::ESP_Friends_HealthBar);
                if (cfg::ESP_Friends_HealthBar) {
                  std::vector<std::string> hbpos{
                      xorstr_("Top"), xorstr_("Bottom"), xorstr_("Left"),
                      xorstr_("Right")};
                  widgets->dropdown_direct(xorstr_("Friends Health Bar Pos"),
                                           &cfg::esp_friends_bar_position,
                                           hbpos);
                  widgets->dropdown_direct(xorstr_("Friends Armor Bar Pos"),
                                           &cfg::esp_friends_armor_bar_position,
                                           hbpos);
                  widgets->checkbox_direct(
                      xorstr_("Custom Health Colors"),
                      &cfg::esp_friends_custom_health_colors);
                }
                widgets->checkbox_direct(xorstr_("Friends Draw Distance"),
                                         &cfg::ESP_Friends_Distance);
                widgets->checkbox_direct(xorstr_("Friends Draw Weapon"),
                                         &cfg::ESP_Friends_Weapon);
                widgets->checkbox_direct(xorstr_("Friends Skeleton Lines"),
                                         &cfg::ESP_Friends_SkeletonLines);
              }
              widgets->end_child();
            }
            gui->end_group();

            gui->sameline();

            gui->begin_group();
            {
              widgets->child(xorstr_("F"), xorstr_("Friends ESP Advanced"));
              {
                // Sliders first
                if (cfg::ESP_Friends_Box) {
                  widgets->slider_direct_f(xorstr_("Friends Box Thickness"),
                                           &cfg::esp_friends_box_thickness,
                                           1.0f, 5.0f, xorstr_("%.1f"));
                  widgets->slider_direct_f(xorstr_("Friends Box Rounding"),
                                           &cfg::esp_friends_box_rounding, 0.0f,
                                           15.0f, xorstr_("%.1f"));
                }
                if (cfg::ESP_Friends_SkeletonLines)
                  widgets->slider_direct_f(
                      xorstr_("Friends Skeleton Thickness"),
                      &cfg::esp_friends_skeleton_thickness, 1.0f, 5.0f,
                      xorstr_("%.1f"));

                // Colors below
                if (cfg::ESP_Friends_Box)
                  widgets->colorpicker(xorstr_("Friends Box Color"),
                                       (c_vec4 *)&cfg::col_friends_box.Value,
                                       true);

                if (cfg::ESP_Friends_HealthBar &&
                    cfg::esp_friends_custom_health_colors) {
                  widgets->colorpicker(xorstr_("Health Color"),
                                       (c_vec4 *)&cfg::col_friends_health.Value,
                                       true);
                  widgets->colorpicker(xorstr_("Armor Color"),
                                       (c_vec4 *)&cfg::col_friends_armor.Value,
                                       true);
                }
                if (cfg::ESP_Friends_Distance)
                  widgets->colorpicker(
                      xorstr_("Friends Distance Color"),
                      (c_vec4 *)&cfg::col_friends_distance.Value, true);
                if (cfg::ESP_Friends_Weapon)
                  widgets->colorpicker(xorstr_("Friends Weapon Color"),
                                       (c_vec4 *)&cfg::col_friends_weapon.Value,
                                       true);
                if (cfg::ESP_Friends_SkeletonLines)
                  widgets->colorpicker(
                      xorstr_("Friends Skeleton Color"),
                      (c_vec4 *)&cfg::col_friends_skeleton.Value, true);
                widgets->checkbox_direct(xorstr_("Friends Snap Line"),
                                         &cfg::ESP_Friends_Line);
                if (cfg::ESP_Friends_Line)
                  widgets->colorpicker(xorstr_("Friends Snap Line Color"),
                                       (c_vec4 *)&cfg::col_friends_line.Value,
                                       true);
              }
              widgets->end_child();
            }
            gui->end_group();
          } else if (var->tab.subtab_current == 2) // Admins
          {
            gui->begin_group();
            {
              widgets->child(xorstr_("A"), xorstr_("Admins ESP Base"));
              {
                widgets->checkbox_direct(xorstr_("Enable Admin ESP"),
                                         &cfg::esp_show_admins);
                widgets->checkbox_direct(xorstr_("Admins 2D Box"),
                                         &cfg::ESP_Admins_Box);
                if (cfg::ESP_Admins_Box) {
                  std::vector<std::string> btypes{
                      xorstr_("Border"), xorstr_("Corners"), xorstr_("Filled"),
                      xorstr_("Filled & Corners")};
                  widgets->dropdown_direct(xorstr_("Box Type"),
                                           &cfg::esp_admins_box_type, btypes);
                }
                widgets->checkbox_direct(xorstr_("Admins Health Bar"),
                                         &cfg::ESP_Admins_HealthBar);
                if (cfg::ESP_Admins_HealthBar) {
                  std::vector<std::string> hbpos{
                      xorstr_("Top"), xorstr_("Bottom"), xorstr_("Left"),
                      xorstr_("Right")};
                  widgets->dropdown_direct(xorstr_("Admins Health Bar Pos"),
                                           &cfg::esp_admins_bar_position,
                                           hbpos);
                  widgets->dropdown_direct(xorstr_("Admins Armor Bar Pos"),
                                           &cfg::esp_admins_armor_bar_position,
                                           hbpos);
                  widgets->checkbox_direct(
                      xorstr_("Custom Health Colors"),
                      &cfg::esp_admins_custom_health_colors);
                }
                widgets->checkbox_direct(xorstr_("Admins Draw Distance"),
                                         &cfg::ESP_Admins_Distance);
                widgets->checkbox_direct(xorstr_("Admins Draw Weapon"),
                                         &cfg::ESP_Admins_Weapon);
                widgets->checkbox_direct(xorstr_("Admins Skeleton Lines"),
                                         &cfg::ESP_Admins_SkeletonLines);
              }
              widgets->end_child();
            }
            gui->end_group();

            gui->sameline();

            gui->begin_group();
            {
              widgets->child(xorstr_("A"), xorstr_("Admins ESP Advanced"));
              {
                // Sliders first
                if (cfg::ESP_Admins_Box) {
                  widgets->slider_direct_f(xorstr_("Admins Box Thickness"),
                                           &cfg::esp_admins_box_thickness, 1.0f,
                                           5.0f, xorstr_("%.1f"));
                  widgets->slider_direct_f(xorstr_("Admins Box Rounding"),
                                           &cfg::esp_admins_box_rounding, 0.0f,
                                           15.0f, xorstr_("%.1f"));
                }
                if (cfg::ESP_Admins_SkeletonLines)
                  widgets->slider_direct_f(xorstr_("Admins Skeleton Thickness"),
                                           &cfg::esp_admins_skeleton_thickness,
                                           1.0f, 5.0f, xorstr_("%.1f"));

                // Colors below
                if (cfg::ESP_Admins_Box)
                  widgets->colorpicker(xorstr_("Admins Box Color"),
                                       (c_vec4 *)&cfg::col_admins_box.Value,
                                       true);

                if (cfg::ESP_Admins_HealthBar &&
                    cfg::esp_admins_custom_health_colors) {
                  widgets->colorpicker(xorstr_("Health Color"),
                                       (c_vec4 *)&cfg::col_admins_health.Value,
                                       true);
                  widgets->colorpicker(xorstr_("Armor Color"),
                                       (c_vec4 *)&cfg::col_admins_armor.Value,
                                       true);
                }
                if (cfg::ESP_Admins_Distance)
                  widgets->colorpicker(
                      xorstr_("Admins Distance Color"),
                      (c_vec4 *)&cfg::col_admins_distance.Value, true);
                if (cfg::ESP_Admins_Weapon)
                  widgets->colorpicker(xorstr_("Admins Weapon Color"),
                                       (c_vec4 *)&cfg::col_admins_weapon.Value,
                                       true);
                if (cfg::ESP_Admins_SkeletonLines)
                  widgets->colorpicker(
                      xorstr_("Admins Skeleton Color"),
                      (c_vec4 *)&cfg::col_admins_skeleton.Value, true);
                widgets->checkbox_direct(xorstr_("Admins Snap Line"),
                                         &cfg::ESP_Admins_Line);
                if (cfg::ESP_Admins_Line)
                  widgets->colorpicker(xorstr_("Admins Snap Line Color"),
                                       (c_vec4 *)&cfg::col_admins_line.Value,
                                       true);
              }
              widgets->end_child();
            }
            gui->end_group();
          } else if (var->tab.subtab_current == 3) // Objects
          {
            gui->begin_group();
            {
              widgets->child(xorstr_("U"), xorstr_("Objects ESP"));
              {
                widgets->checkbox_direct(xorstr_("Draw Objects"),
                                         &cfg::esp_objects_enabled);
                widgets->checkbox_direct(xorstr_("Snap Line"),
                                         &cfg::esp_objects_snapline);
              }
              widgets->end_child();
            }
            gui->end_group();

            gui->sameline();

            gui->begin_group();
            {
              widgets->child(xorstr_("U"), xorstr_("Objects Advanced"));
              {
                // Sliders first
                widgets->slider_direct_f(xorstr_("Max Distance"),
                                         &cfg::esp_objects_max_distance, 50.f,
                                         1000.f, xorstr_("%.fm"));

                // Colors below
                if (cfg::esp_objects_enabled)
                  widgets->colorpicker(xorstr_("Objects Color"),
                                       (c_vec4 *)&cfg::col_objects.Value, true);
                if (cfg::esp_objects_snapline)
                  widgets->colorpicker(
                      xorstr_("Snap Line Color"),
                      (c_vec4 *)&cfg::col_objects_snapline.Value, true);
              }
              widgets->end_child();
            }
            gui->end_group();
          } else if (var->tab.subtab_current == 4) // Other
          {
            gui->begin_group();
            {
              widgets->child(xorstr_("U"), xorstr_("Arrows"));
              {
                widgets->checkbox_direct(xorstr_("Enable Arrows"),
                                         &cfg::targets_arrows_enable);
                if (cfg::targets_arrows_enable)
                  widgets->colorpicker(xorstr_("Arrows Color"),
                                       (c_vec4 *)&cfg::col_targets_arrows.Value,
                                       true);

                widgets->slider_direct_f(xorstr_("Size"),
                                         &cfg::targets_arrows_size, 1.f, 50.f,
                                         xorstr_("%.1f"));
                widgets->slider_direct_f(xorstr_("FOV"),
                                         &cfg::targets_arrows_fov, 0.f, 360.f,
                                         xorstr_("%.f"));
                widgets->checkbox_direct(xorstr_("Flicker"),
                                         &cfg::targets_arrows_flicker);
                widgets->checkbox_direct(xorstr_("Exclude visible"),
                                         &cfg::targets_arrows_exclude_visible);
              }
              widgets->end_child();

              widgets->child(xorstr_("U"), xorstr_("Radar"));
              {
                widgets->checkbox_direct(xorstr_("Enable Radar"),
                                         &cfg::targets_radar_enable);
                widgets->slider_direct_f(xorstr_("Opacity"),
                                         &cfg::targets_radar_opacity, 0.1f,
                                         1.0f, xorstr_("%.1f"));
                widgets->slider_direct_f(xorstr_("Range"),
                                         &cfg::targets_radar_range, 10.f,
                                         1000.f, xorstr_("%.fm"));
              }
              widgets->end_child();
            }
            gui->end_group();

            gui->sameline();

            gui->begin_group();
            {
              widgets->child(xorstr_("U"), xorstr_("Highlights"));
              {
                widgets->checkbox_direct(xorstr_("Heavy Sniper"),
                                         &cfg::targets_highlight_heavy_sniper);
                if (cfg::targets_highlight_heavy_sniper)
                  widgets->colorpicker(
                      xorstr_("Heavy Sniper Color"),
                      (c_vec4 *)&cfg::col_targets_highlight_heavy_sniper.Value,
                      true);

                widgets->checkbox_direct(xorstr_("Heavy MK2"),
                                         &cfg::targets_highlight_heavy_mk2);
                if (cfg::targets_highlight_heavy_mk2)
                  widgets->colorpicker(
                      xorstr_("Heavy MK2 Color"),
                      (c_vec4 *)&cfg::col_targets_highlight_heavy_mk2.Value,
                      true);

                widgets->checkbox_direct(
                    xorstr_("Precision Rifle"),
                    &cfg::targets_highlight_precision_rifle);
                if (cfg::targets_highlight_precision_rifle)
                  widgets->colorpicker(
                      xorstr_("Precision Rifle Color"),
                      (c_vec4 *)&cfg::col_targets_highlight_precision_rifle
                          .Value,
                      true);
              }
              widgets->end_child();

              widgets->child(xorstr_("U"), xorstr_("Around Counters"));
              {
                widgets->checkbox_direct(xorstr_("Admins Around"),
                                         &cfg::show_admins_around);
                if (cfg::show_admins_around) {
                  widgets->colorpicker(xorstr_("Admins Text Color"),
                                       (c_vec4 *)&cfg::col_admins_around.Value,
                                       true);
                }

                widgets->checkbox_direct(xorstr_("Players Around"),
                                         &cfg::show_players_around);
                if (cfg::show_players_around) {
                  widgets->colorpicker(xorstr_("Players Text Color"),
                                       (c_vec4 *)&cfg::col_players_around.Value,
                                       true);
                }
              }
              widgets->end_child();
            }
            gui->end_group();
          }
        } else if (var->tab.tab_current == 2) // Misc
        {
          gui->begin_group();
          {
            widgets->child(xorstr_("H"), xorstr_("Settings"));
            {
              widgets->checkbox_direct(xorstr_("Anti AFK"), &cfg::anti_afk);
              widgets->checkbox_direct(xorstr_("Hide On Record"),
                                       &cfg::bypass_obs);
              widgets->checkbox_direct(xorstr_("Godmode"), &cfg::godmode);

              widgets->slider_direct(xorstr_("Give Health Amount"),
                                     &cfg::give_health_val, 1, 100,
                                     xorstr_("%.0f"));
              if (widgets->button(xorstr_("GIVE HEALTH"))) {
                cfg::trigger_give_health = true;
              }

              widgets->slider_direct(xorstr_("Give Armor Amount"),
                                     &cfg::give_armor_val, 0, 100,
                                     xorstr_("%.0f"));
              if (widgets->button(xorstr_("GIVE ARMOR"))) {
                cfg::trigger_give_armor = true;
              }
            }
            widgets->end_child();
          }
          gui->end_group();

          gui->sameline();

          gui->begin_group();
          {
            widgets->child(xorstr_("M"), xorstr_("Actions"));
            {
              widgets->keybind_direct(xorstr_("Suicide Keybind"),
                                      &cfg::suicide_key);
              if (widgets->button(xorstr_("SUICIDE"))) {
                cfg::suicide_key = 1;
              }
              widgets->keybind_direct(xorstr_("TP to Waypoint Keybind"),
                                      &cfg::tp_waypoint_key);
              if (widgets->button(xorstr_("TP TO WAYPOINT"))) {
                cfg::tp_waypoint_key = 1;
              }
              widgets->keybind_direct(xorstr_("Unload Cheat Keybind"),
                                      &cfg::unload_cheat_key);
              if (widgets->button(xorstr_("UNLOAD CHEAT"))) {
                PostQuitMessage(0);
              }
            }
            widgets->end_child();
          }
          gui->end_group();
        } else if (var->tab.tab_current == 3) // Settings
        {
          gui->begin_group();
          {
            widgets->child(xorstr_("H"), xorstr_("Visual settings"));
            {
              widgets->dropdown(xorstr_("Theme"));
              std::vector<std::string> esp_fonts{xorstr_("SF Pro"),
                                                 xorstr_("Syne")};
              widgets->dropdown_direct(xorstr_("Font"), &cfg::ESP_Font,
                                       esp_fonts);
              widgets->colorpicker(xorstr_("Accent color"), &clr->accent.Value,
                                   true);

              gui->dummy({gui->content_avail().x, SCALE(10)});
              widgets->keybind_direct(xorstr_("Menu Keybind"),
                                      &cfg::ToggleMenuKey);
            }
            widgets->end_child();
          }
          gui->end_group();

          gui->sameline();

          gui->begin_group();
          {
            widgets->child(xorstr_("I"), xorstr_("UI Scale"));
            {
              widgets->slider(xorstr_("Interface size"));
            }
            widgets->end_child();

            widgets->child(xorstr_("O"), xorstr_("Entity Settings"));
            {
              int delay = cfg::EntityUpdateDelayMs;
              if (widgets->slider_direct(xorstr_("Entity Update Delay"), &delay,
                                         0, 100, xorstr_("%.f ms")))
                cfg::EntityUpdateDelayMs = delay;

              int skel_delay = cfg::SkeletonUpdateDelayMs;
              if (widgets->slider_direct(xorstr_("Skeleton Update Delay"),
                                         &skel_delay, 0, 100,
                                         xorstr_("%.f ms")))
                cfg::SkeletonUpdateDelayMs = skel_delay;

              int maxent = cfg::MaxEntities;
              if (widgets->slider_direct(xorstr_("Max Players"), &maxent, 16,
                                         1024, xorstr_("%.f")))
                cfg::MaxEntities = maxent;

              int maxweap = cfg::MaxWeapons;
              if (widgets->slider_direct(xorstr_("Max Weapons"), &maxweap, 1,
                                         1500, xorstr_("%.f")))
                cfg::MaxWeapons = maxweap;

              int maxpick = cfg::MaxPickups;
              if (widgets->slider_direct(xorstr_("Max Objects"), &maxpick, 1,
                                         1500, xorstr_("%.f")))
                cfg::MaxPickups = maxpick;
            }
            widgets->end_child();
          }
          gui->end_group();
        } else if (var->tab.tab_current == 4) // Friends
        {
          gui->begin_group();
          {
            widgets->child(xorstr_("F"), xorstr_("Manage Friends"));
            {
              widgets->slider_direct(xorstr_("Radius (m)"), &cfg::friend_radius,
                                     1, 200, xorstr_("%.f"));
              if (widgets->button(xorstr_("Add All in Radius"))) {
                if (g.localplayer && g.localplayer->IsValid()) {
                  std::vector<CPed> local_list;
                  {
                    std::lock_guard<std::mutex> lock(g.EntityListMutex);
                    local_list = g.EntityList;
                  }
                  for (auto &ped : local_list) {
                    if (!ped.IsValid() || ped.ptr == g.localplayer->ptr)
                      continue;
                    float dist = Vector3::Distance(
                        ped.m_pVecPosition, g.localplayer->m_pVecPosition);
                    if (dist <= cfg::friend_radius && dist > 0.1f) {
                      if (std::find(cfg::friend_list.begin(),
                                    cfg::friend_list.end(),
                                    ped.ptr) == cfg::friend_list.end()) {
                        cfg::friend_list.push_back(ped.ptr);
                        cfg::friend_names.push_back(ped.GetName().empty()
                                                        ? xorstr_("Player")
                                                        : ped.GetName());
                      }
                    }
                  }
                }
              }
              gui->dummy({gui->content_avail().x, SCALE(10)});
              widgets->slider_direct(xorstr_("Max dist (m)"),
                                     &cfg::friend_max_dist, 1, 1000,
                                     xorstr_("%.f"));
              if (widgets->button(xorstr_("Add Nearest"))) {
                if (g.localplayer && g.localplayer->IsValid()) {
                  float min_dist = cfg::friend_max_dist;
                  uintptr_t nearest_ped = 0;
                  std::string nearest_name;
                  std::vector<CPed> local_list2;
                  {
                    std::lock_guard<std::mutex> lock(g.EntityListMutex);
                    local_list2 = g.EntityList;
                  }
                  for (auto &ped : local_list2) {
                    if (!ped.IsValid() || ped.ptr == g.localplayer->ptr)
                      continue;
                    float dist = Vector3::Distance(
                        ped.m_pVecPosition, g.localplayer->m_pVecPosition);
                    if (dist <= min_dist && dist > 0.1f) {
                      if (std::find(cfg::friend_list.begin(),
                                    cfg::friend_list.end(),
                                    ped.ptr) == cfg::friend_list.end()) {
                        min_dist = dist;
                        nearest_ped = ped.ptr;
                        nearest_name = ped.GetName().empty() ? xorstr_("Player")
                                                             : ped.GetName();
                      }
                    }
                  }
                  if (nearest_ped != 0) {
                    cfg::friend_list.push_back(nearest_ped);
                    cfg::friend_names.push_back(nearest_name);
                  }
                }
              }
              if (widgets->button(xorstr_("Remove All"))) {
                cfg::friend_list.clear();
                cfg::friend_names.clear();
              }
            }
            widgets->end_child();
          }
          gui->end_group();

          gui->sameline();

          gui->begin_group();
          {
            widgets->child(xorstr_("F"), xorstr_("Friend List"));
            {
              for (int i = 0; i < (int)cfg::friend_names.size(); i++) {
                ImGui::PushID(i);
                bool selected = false;
                if (widgets->selectable(cfg::friend_names[i], selected)) {
                  cfg::friend_list.erase(cfg::friend_list.begin() + i);
                  cfg::friend_names.erase(cfg::friend_names.begin() + i);
                  ImGui::PopID();
                  break;
                }
                ImGui::PopID();
              }
            }
            widgets->end_child();
          }
          gui->end_group();
        } else if (var->tab.tab_current == 5) // Configs
        {
          static int loaded_cfg_idx = -1;
          gui->begin_group();
          {
            widgets->child(xorstr_("P"), xorstr_("Config list"), SCALE(10, 0));
            {
              static float anim_offset = 0.f;
              static std::vector<std::string> cfgs;

              gui->easing(anim_offset,
                          widgets->config_storage.size() > 0 ? 1.02f : -0.02f,
                          16.f, dynamic_easing);
              anim_offset = ImClamp(anim_offset, 0.f, 1.f);
              gui->dummy({gui->content_avail().x, SCALE(10) * anim_offset});

              if (ImGui::GetTime() - last_refresh > 1.0f) {
                cfgs = config_system::get_configs();
                last_refresh = ImGui::GetTime();
                widgets->config_storage.clear();
                for (size_t i = 0; i < cfgs.size(); i++) {
                  widgets->config_storage.push_back(
                      {(int)i, cfgs[i], xorstr_("Local Config")});
                }
              }
              for (int i = 0; i < (int)widgets->config_storage.size(); i++) {
                config_t &data = widgets->config_storage[i];
                config_button_t btn = widgets->config_button(
                    data.idx, data.name, data.date, loaded_cfg_idx == i);
                if (btn.pressed || btn.share) {
                  loaded_cfg_idx = i;
                  config_system::load(data.name);
                }
                if (btn.download) {
                  config_system::save(data.name);
                }
                if (btn.edit) {
                  strcpy_s(new_cfg_name, sizeof(new_cfg_name),
                           data.name.c_str());
                }
                if (btn.del) {
                  if (loaded_cfg_idx == i)
                    loaded_cfg_idx = -1;
                  config_system::remove(data.name);
                  last_refresh = 0.f;
                }
              }
            }
            widgets->end_child();
          }
          gui->end_group();

          gui->sameline();

          gui->begin_group();
          {
            widgets->child(xorstr_("?"), xorstr_("Config information"),
                           SCALE(15, 15));
            {
              std::string last_mod = xorstr_("N/A");
              if (loaded_cfg_idx >= 0 &&
                  loaded_cfg_idx < (int)widgets->config_storage.size()) {
                last_mod = widgets->config_storage[loaded_cfg_idx].date;
              }
              widgets->config_info(xorstr_("L"), xorstr_("Last modified"),
                                   last_mod.c_str());
              std::string creator = config_system::g_username.empty()
                                        ? xorstr_("Unknown")
                                        : config_system::g_username;
              widgets->config_info(xorstr_("A"), xorstr_("Creator's nickname"),
                                   creator.c_str());
            }
            widgets->end_child();
          }
          gui->end_group();
        }

        static float top_shadow_alpha = 0.f;
        gui->easing(top_shadow_alpha,
                    gui->get_window()->Scroll.y > SCALE(1) ? 1.f : 0.f, 12.f,
                    dynamic_easing);
        draw->rect_filled_multi_color(
            var->widget.draw_list, gui->get_window()->Rect().Min,
            gui->get_window()->Rect().GetTR() + SCALE(0, 30 * top_shadow_alpha),
            draw->get_clr(clr->layout, 1.f * top_shadow_alpha),
            draw->get_clr(clr->layout, 1.f * top_shadow_alpha),
            draw->get_clr(clr->layout, 0.f), draw->get_clr(clr->layout, 0.f));

        static float bottom_shadow_alpha = 0.f;
        gui->easing(bottom_shadow_alpha,
                    gui->get_window()->Scroll.y <
                            gui->get_window()->ScrollMax.y - SCALE(1)
                        ? 1.f
                        : 0.f,
                    12.f, dynamic_easing);
        draw->rect_filled_multi_color(
            var->widget.draw_list,
            gui->get_window()->Rect().GetBL() -
                SCALE(0, 30 * bottom_shadow_alpha),
            gui->get_window()->Rect().Max, draw->get_clr(clr->layout, 0.f),
            draw->get_clr(clr->layout, 0.f),
            draw->get_clr(clr->layout, 1.f * bottom_shadow_alpha),
            draw->get_clr(clr->layout, 1.f * bottom_shadow_alpha));
      }
      gui->end_content();

      gui->pop_var(4);
    }
    gui->end_content();

    widgets->blackout();
  }
  gui->end();

  gui->easing(var->blackout.alpha, var->blackout.active ? 1.f : 0.f, 7.f,
              static_easing);

  float dpi = menu_cfg->get<slider_t>(xorstr_("Interface size")).callback;
  if (gui->mouse_released(0) && round(dpi) != var->gui.stored_dpi) {
    var->gui.stored_dpi = round(dpi);
    var->gui.dpi_changed = true;
  }
  SHADOW_PROTECT_END();
}
