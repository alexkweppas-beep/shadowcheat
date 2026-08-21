#include "../gui/loveup/headers/config.h"
#include "../security/hwid_dll.hpp"
#include "../security/lazy_importer.hpp"
#include "../security/vmp_markers.hpp"
#include "../utils/http.hpp"
#include "config.hpp"
#include "security/xorstr.hpp"
#include "settings.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>

std::string serialize() {
  std::stringstream ss;
  ss << xorstr_("EntityUpdateDelayMs=") << cfg::EntityUpdateDelayMs
     << xorstr_(";\n");
  ss << xorstr_("SkeletonUpdateDelayMs=") << cfg::SkeletonUpdateDelayMs
     << xorstr_(";\n");
  ss << xorstr_("MaxEntities=") << cfg::MaxEntities << xorstr_(";\n");
  ss << xorstr_("MaxWeapons=") << cfg::MaxWeapons << xorstr_(";\n");
  ss << xorstr_("MaxPickups=") << cfg::MaxPickups << xorstr_(";\n");

  ss << xorstr_("ToggleMenuKey=") << cfg::ToggleMenuKey << xorstr_(";\n");
  ss << xorstr_("Misc_Debug=") << (cfg::Misc_Debug ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("suicide_key=") << cfg::suicide_key << xorstr_(";\n");
  ss << xorstr_("tp_waypoint_key=") << cfg::tp_waypoint_key << xorstr_(";\n");
  ss << xorstr_("unload_cheat_key=") << cfg::unload_cheat_key << xorstr_(";\n");
  ss << xorstr_("weapon_no_recoil=") << (cfg::weapon_no_recoil ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("weapon_no_spread=") << (cfg::weapon_no_spread ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("weapon_inf_ammo=") << (cfg::weapon_inf_ammo ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("weapon_double_tap=") << (cfg::weapon_double_tap ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("enable_wallshot=") << (cfg::enable_wallshot ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("enable_weapon_configs=")
     << (cfg::enable_weapon_configs ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("active_weapon_category=") << cfg::active_weapon_category
     << xorstr_(";\n");
  ss << xorstr_("current_weapon_category=") << cfg::current_weapon_category
     << xorstr_(";\n");
  ss << xorstr_("aimkey=") << cfg::aimkey << xorstr_(";\n");
  ss << xorstr_("aimkey_mode=") << cfg::aimkey_mode << xorstr_(";\n");
  ss << xorstr_("triggerbot=") << (cfg::triggerbot ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("triggerkey=") << cfg::triggerkey << xorstr_(";\n");
  ss << xorstr_("triggerkey_mode=") << cfg::triggerkey_mode << xorstr_(";\n");
  ss << xorstr_("triggerdelay=") << cfg::triggerdelay << xorstr_(";\n");
  ss << xorstr_("trigger_ignore_friends=")
     << (cfg::trigger_ignore_friends ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("trigger_ignore_admins=")
     << (cfg::trigger_ignore_admins ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("ESP_MaxDistance=") << cfg::ESP_MaxDistance << xorstr_(";\n");
  ss << xorstr_("ESP_PlayerOnly=") << (cfg::ESP_PlayerOnly ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("esp_show_friends=") << (cfg::esp_show_friends ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("esp_show_admins=") << (cfg::esp_show_admins ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("ESP_AimLine=") << (cfg::ESP_AimLine ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("col_AimLine=") << cfg::col_AimLine.Value.x << xorstr_(",")
     << cfg::col_AimLine.Value.y << xorstr_(",") << cfg::col_AimLine.Value.z
     << xorstr_(",") << cfg::col_AimLine.Value.w << xorstr_(";\n");
  ss << xorstr_("ESP_Box=") << (cfg::ESP_Box ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("esp_box_thickness=") << cfg::esp_box_thickness
     << xorstr_(";\n");
  ss << xorstr_("esp_box_rounding=") << cfg::esp_box_rounding << xorstr_(";\n");
  ss << xorstr_("esp_skeleton_thickness=") << cfg::esp_skeleton_thickness
     << xorstr_(";\n");
  ss << xorstr_("ESP_Line=") << (cfg::ESP_Line ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("ESP_HealthBar=") << (cfg::ESP_HealthBar ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("ESP_Distance=") << (cfg::ESP_Distance ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("ESP_Weapon=") << (cfg::ESP_Weapon ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("ESP_Skeleton=") << (cfg::ESP_Skeleton ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("ESP_SkeletonLines=") << (cfg::ESP_SkeletonLines ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("esp_players_name=") << (cfg::esp_players_name ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("esp_players_box_type=") << cfg::esp_players_box_type
     << xorstr_(";\n");
  ss << xorstr_("esp_players_bar_position=") << cfg::esp_players_bar_position
     << xorstr_(";\n");
  ss << xorstr_("esp_players_armor_bar_position=")
     << cfg::esp_players_armor_bar_position << xorstr_(";\n");
  ss << xorstr_("col_players_box=") << cfg::col_players_box.Value.x
     << xorstr_(",") << cfg::col_players_box.Value.y << xorstr_(",")
     << cfg::col_players_box.Value.z << xorstr_(",")
     << cfg::col_players_box.Value.w << xorstr_(";\n");
  ss << xorstr_("col_players_skeleton=") << cfg::col_players_skeleton.Value.x
     << xorstr_(",") << cfg::col_players_skeleton.Value.y << xorstr_(",")
     << cfg::col_players_skeleton.Value.z << xorstr_(",")
     << cfg::col_players_skeleton.Value.w << xorstr_(";\n");
  ss << xorstr_("col_players_line=") << cfg::col_players_line.Value.x
     << xorstr_(",") << cfg::col_players_line.Value.y << xorstr_(",")
     << cfg::col_players_line.Value.z << xorstr_(",")
     << cfg::col_players_line.Value.w << xorstr_(";\n");
  ss << xorstr_("col_players_distance=") << cfg::col_players_distance.Value.x
     << xorstr_(",") << cfg::col_players_distance.Value.y << xorstr_(",")
     << cfg::col_players_distance.Value.z << xorstr_(",")
     << cfg::col_players_distance.Value.w << xorstr_(";\n");
  ss << xorstr_("col_players_weapon=") << cfg::col_players_weapon.Value.x
     << xorstr_(",") << cfg::col_players_weapon.Value.y << xorstr_(",")
     << cfg::col_players_weapon.Value.z << xorstr_(",")
     << cfg::col_players_weapon.Value.w << xorstr_(";\n");
  ss << xorstr_("esp_custom_health_colors=")
     << (cfg::esp_custom_health_colors ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("col_health=") << cfg::col_health.Value.x << xorstr_(",")
     << cfg::col_health.Value.y << xorstr_(",") << cfg::col_health.Value.z
     << xorstr_(",") << cfg::col_health.Value.w << xorstr_(";\n");
  ss << xorstr_("col_armor=") << cfg::col_armor.Value.x << xorstr_(",")
     << cfg::col_armor.Value.y << xorstr_(",") << cfg::col_armor.Value.z
     << xorstr_(",") << cfg::col_armor.Value.w << xorstr_(";\n");
  ss << xorstr_("esp_objects_enabled=") << (cfg::esp_objects_enabled ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("esp_objects_max_distance=") << cfg::esp_objects_max_distance
     << xorstr_(";\n");
  ss << xorstr_("col_objects=") << cfg::col_objects.Value.x << xorstr_(",")
     << cfg::col_objects.Value.y << xorstr_(",") << cfg::col_objects.Value.z
     << xorstr_(",") << cfg::col_objects.Value.w << xorstr_(";\n");
  ss << xorstr_("esp_objects_snapline=") << (cfg::esp_objects_snapline ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("col_objects_snapline=") << cfg::col_objects_snapline.Value.x
     << xorstr_(",") << cfg::col_objects_snapline.Value.y << xorstr_(",")
     << cfg::col_objects_snapline.Value.z << xorstr_(",")
     << cfg::col_objects_snapline.Value.w << xorstr_(";\n");

  ss << xorstr_("ESP_Friends_Box=") << (cfg::ESP_Friends_Box ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("esp_friends_box_type=") << cfg::esp_friends_box_type
     << xorstr_(";\n");
  ss << xorstr_("esp_friends_box_thickness=") << cfg::esp_friends_box_thickness
     << xorstr_(";\n");
  ss << xorstr_("esp_friends_box_rounding=") << cfg::esp_friends_box_rounding
     << xorstr_(";\n");
  ss << xorstr_("esp_friends_skeleton_thickness=")
     << cfg::esp_friends_skeleton_thickness << xorstr_(";\n");
  ss << xorstr_("col_friends_box=") << cfg::col_friends_box.Value.x
     << xorstr_(",") << cfg::col_friends_box.Value.y << xorstr_(",")
     << cfg::col_friends_box.Value.z << xorstr_(",")
     << cfg::col_friends_box.Value.w << xorstr_(";\n");
  ss << xorstr_("ESP_Friends_HealthBar=")
     << (cfg::ESP_Friends_HealthBar ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("esp_friends_bar_position=") << cfg::esp_friends_bar_position
     << xorstr_(";\n");
  ss << xorstr_("esp_friends_armor_bar_position=")
     << cfg::esp_friends_armor_bar_position << xorstr_(";\n");
  ss << xorstr_("ESP_Friends_Distance=") << (cfg::ESP_Friends_Distance ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("col_friends_distance=") << cfg::col_friends_distance.Value.x
     << xorstr_(",") << cfg::col_friends_distance.Value.y << xorstr_(",")
     << cfg::col_friends_distance.Value.z << xorstr_(",")
     << cfg::col_friends_distance.Value.w << xorstr_(";\n");
  ss << xorstr_("ESP_Friends_Weapon=") << (cfg::ESP_Friends_Weapon ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("col_friends_weapon=") << cfg::col_friends_weapon.Value.x
     << xorstr_(",") << cfg::col_friends_weapon.Value.y << xorstr_(",")
     << cfg::col_friends_weapon.Value.z << xorstr_(",")
     << cfg::col_friends_weapon.Value.w << xorstr_(";\n");
  ss << xorstr_("ESP_Friends_SkeletonLines=")
     << (cfg::ESP_Friends_SkeletonLines ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("col_friends_skeleton=") << cfg::col_friends_skeleton.Value.x
     << xorstr_(",") << cfg::col_friends_skeleton.Value.y << xorstr_(",")
     << cfg::col_friends_skeleton.Value.z << xorstr_(",")
     << cfg::col_friends_skeleton.Value.w << xorstr_(";\n");
  ss << xorstr_("ESP_Friends_Line=") << (cfg::ESP_Friends_Line ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("col_friends_line=") << cfg::col_friends_line.Value.x
     << xorstr_(",") << cfg::col_friends_line.Value.y << xorstr_(",")
     << cfg::col_friends_line.Value.z << xorstr_(",")
     << cfg::col_friends_line.Value.w << xorstr_(";\n");
  ss << xorstr_("esp_friends_custom_health_colors=")
     << (cfg::esp_friends_custom_health_colors ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("col_friends_health=") << cfg::col_friends_health.Value.x
     << xorstr_(",") << cfg::col_friends_health.Value.y << xorstr_(",")
     << cfg::col_friends_health.Value.z << xorstr_(",")
     << cfg::col_friends_health.Value.w << xorstr_(";\n");
  ss << xorstr_("col_friends_armor=") << cfg::col_friends_armor.Value.x
     << xorstr_(",") << cfg::col_friends_armor.Value.y << xorstr_(",")
     << cfg::col_friends_armor.Value.z << xorstr_(",")
     << cfg::col_friends_armor.Value.w << xorstr_(";\n");
  ss << xorstr_("ESP_Admins_Box=") << (cfg::ESP_Admins_Box ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("esp_admins_box_type=") << cfg::esp_admins_box_type
     << xorstr_(";\n");
  ss << xorstr_("esp_admins_box_thickness=") << cfg::esp_admins_box_thickness
     << xorstr_(";\n");
  ss << xorstr_("esp_admins_box_rounding=") << cfg::esp_admins_box_rounding
     << xorstr_(";\n");
  ss << xorstr_("esp_admins_skeleton_thickness=")
     << cfg::esp_admins_skeleton_thickness << xorstr_(";\n");
  ss << xorstr_("col_admins_box=") << cfg::col_admins_box.Value.x
     << xorstr_(",") << cfg::col_admins_box.Value.y << xorstr_(",")
     << cfg::col_admins_box.Value.z << xorstr_(",")
     << cfg::col_admins_box.Value.w << xorstr_(";\n");
  ss << xorstr_("ESP_Admins_HealthBar=") << (cfg::ESP_Admins_HealthBar ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("esp_admins_bar_position=") << cfg::esp_admins_bar_position
     << xorstr_(";\n");
  ss << xorstr_("esp_admins_armor_bar_position=")
     << cfg::esp_admins_armor_bar_position << xorstr_(";\n");
  ss << xorstr_("ESP_Admins_Distance=") << (cfg::ESP_Admins_Distance ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("col_admins_distance=") << cfg::col_admins_distance.Value.x
     << xorstr_(",") << cfg::col_admins_distance.Value.y << xorstr_(",")
     << cfg::col_admins_distance.Value.z << xorstr_(",")
     << cfg::col_admins_distance.Value.w << xorstr_(";\n");
  ss << xorstr_("ESP_Admins_Weapon=") << (cfg::ESP_Admins_Weapon ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("col_admins_weapon=") << cfg::col_admins_weapon.Value.x
     << xorstr_(",") << cfg::col_admins_weapon.Value.y << xorstr_(",")
     << cfg::col_admins_weapon.Value.z << xorstr_(",")
     << cfg::col_admins_weapon.Value.w << xorstr_(";\n");
  ss << xorstr_("ESP_Admins_SkeletonLines=")
     << (cfg::ESP_Admins_SkeletonLines ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("col_admins_skeleton=") << cfg::col_admins_skeleton.Value.x
     << xorstr_(",") << cfg::col_admins_skeleton.Value.y << xorstr_(",")
     << cfg::col_admins_skeleton.Value.z << xorstr_(",")
     << cfg::col_admins_skeleton.Value.w << xorstr_(";\n");
  ss << xorstr_("ESP_Admins_Line=") << (cfg::ESP_Admins_Line ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("col_admins_line=") << cfg::col_admins_line.Value.x
     << xorstr_(",") << cfg::col_admins_line.Value.y << xorstr_(",")
     << cfg::col_admins_line.Value.z << xorstr_(",")
     << cfg::col_admins_line.Value.w << xorstr_(";\n");
  ss << xorstr_("esp_admins_custom_health_colors=")
     << (cfg::esp_admins_custom_health_colors ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("col_admins_health=") << cfg::col_admins_health.Value.x
     << xorstr_(",") << cfg::col_admins_health.Value.y << xorstr_(",")
     << cfg::col_admins_health.Value.z << xorstr_(",")
     << cfg::col_admins_health.Value.w << xorstr_(";\n");
  ss << xorstr_("col_admins_armor=") << cfg::col_admins_armor.Value.x
     << xorstr_(",") << cfg::col_admins_armor.Value.y << xorstr_(",")
     << cfg::col_admins_armor.Value.z << xorstr_(",")
     << cfg::col_admins_armor.Value.w << xorstr_(";\n");
  ss << xorstr_("show_admins_around=") << (cfg::show_admins_around ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("col_admins_around=") << cfg::col_admins_around.Value.x
     << xorstr_(",") << cfg::col_admins_around.Value.y << xorstr_(",")
     << cfg::col_admins_around.Value.z << xorstr_(",")
     << cfg::col_admins_around.Value.w << xorstr_(";\n");
  ss << xorstr_("show_players_around=") << (cfg::show_players_around ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("col_players_around=") << cfg::col_players_around.Value.x
     << xorstr_(",") << cfg::col_players_around.Value.y << xorstr_(",")
     << cfg::col_players_around.Value.z << xorstr_(",")
     << cfg::col_players_around.Value.w << xorstr_(";\n");
  ss << xorstr_("friend_radius=") << cfg::friend_radius << xorstr_(";\n");
  ss << xorstr_("friend_max_dist=") << cfg::friend_max_dist << xorstr_(";\n");
  ss << xorstr_("ESP_Font=") << cfg::ESP_Font << xorstr_(";\n");
  ss << xorstr_("bypass_obs=") << (cfg::bypass_obs ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("targets_arrows_enable=")
     << (cfg::targets_arrows_enable ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("col_targets_arrows=") << cfg::col_targets_arrows.Value.x
     << xorstr_(",") << cfg::col_targets_arrows.Value.y << xorstr_(",")
     << cfg::col_targets_arrows.Value.z << xorstr_(",")
     << cfg::col_targets_arrows.Value.w << xorstr_(";\n");
  ss << xorstr_("targets_arrows_size=") << cfg::targets_arrows_size
     << xorstr_(";\n");
  ss << xorstr_("targets_arrows_fov=") << cfg::targets_arrows_fov
     << xorstr_(";\n");
  ss << xorstr_("targets_arrows_flicker=")
     << (cfg::targets_arrows_flicker ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("targets_arrows_exclude_visible=")
     << (cfg::targets_arrows_exclude_visible ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("targets_highlight_heavy_sniper=")
     << (cfg::targets_highlight_heavy_sniper ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("col_targets_highlight_heavy_sniper=")
     << cfg::col_targets_highlight_heavy_sniper.Value.x << xorstr_(",")
     << cfg::col_targets_highlight_heavy_sniper.Value.y << xorstr_(",")
     << cfg::col_targets_highlight_heavy_sniper.Value.z << xorstr_(",")
     << cfg::col_targets_highlight_heavy_sniper.Value.w << xorstr_(";\n");
  ss << xorstr_("targets_highlight_heavy_mk2=")
     << (cfg::targets_highlight_heavy_mk2 ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("col_targets_highlight_heavy_mk2=")
     << cfg::col_targets_highlight_heavy_mk2.Value.x << xorstr_(",")
     << cfg::col_targets_highlight_heavy_mk2.Value.y << xorstr_(",")
     << cfg::col_targets_highlight_heavy_mk2.Value.z << xorstr_(",")
     << cfg::col_targets_highlight_heavy_mk2.Value.w << xorstr_(";\n");
  ss << xorstr_("targets_highlight_precision_rifle=")
     << (cfg::targets_highlight_precision_rifle ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("col_targets_highlight_precision_rifle=")
     << cfg::col_targets_highlight_precision_rifle.Value.x << xorstr_(",")
     << cfg::col_targets_highlight_precision_rifle.Value.y << xorstr_(",")
     << cfg::col_targets_highlight_precision_rifle.Value.z << xorstr_(",")
     << cfg::col_targets_highlight_precision_rifle.Value.w << xorstr_(";\n");
  ss << xorstr_("targets_radar_enable=") << (cfg::targets_radar_enable ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("targets_radar_size=") << cfg::targets_radar_size
     << xorstr_(";\n");
  ss << xorstr_("targets_radar_opacity=") << cfg::targets_radar_opacity
     << xorstr_(";\n");
  ss << xorstr_("targets_radar_range=") << cfg::targets_radar_range
     << xorstr_(";\n");

  ss << xorstr_("aimbot_0_aimbot=") << (cfg::aimbot_configs[0].aimbot ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_0_aimtype=") << cfg::aimbot_configs[0].aimtype
     << xorstr_(";\n");
  ss << xorstr_("aimbot_0_fov=") << cfg::aimbot_configs[0].fov
     << xorstr_(";\n");
  ss << xorstr_("aimbot_0_aim_distance=") << cfg::aimbot_configs[0].aim_distance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_0_aim_chance=") << cfg::aimbot_configs[0].aim_chance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_0_aimbone=") << cfg::aimbot_configs[0].aimbone
     << xorstr_(";\n");
  ss << xorstr_("aimbot_0_draw_fov=")
     << (cfg::aimbot_configs[0].draw_fov ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_0_show_fov_only_aiming=")
     << (cfg::aimbot_configs[0].show_fov_only_aiming ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_0_smooth=") << (cfg::aimbot_configs[0].smooth ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_0_smooth_type=") << cfg::aimbot_configs[0].smooth_type
     << xorstr_(";\n");
  ss << xorstr_("aimbot_0_smooth_factor=")
     << cfg::aimbot_configs[0].smooth_factor << xorstr_(";\n");
  ss << xorstr_("aimbot_0_ignore_friends=")
     << (cfg::aimbot_configs[0].ignore_friends ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_0_ignore_admins=")
     << (cfg::aimbot_configs[0].ignore_admins ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_1_aimbot=") << (cfg::aimbot_configs[1].aimbot ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_1_aimtype=") << cfg::aimbot_configs[1].aimtype
     << xorstr_(";\n");
  ss << xorstr_("aimbot_1_fov=") << cfg::aimbot_configs[1].fov
     << xorstr_(";\n");
  ss << xorstr_("aimbot_1_aim_distance=") << cfg::aimbot_configs[1].aim_distance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_1_aim_chance=") << cfg::aimbot_configs[1].aim_chance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_1_aimbone=") << cfg::aimbot_configs[1].aimbone
     << xorstr_(";\n");
  ss << xorstr_("aimbot_1_draw_fov=")
     << (cfg::aimbot_configs[1].draw_fov ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_1_show_fov_only_aiming=")
     << (cfg::aimbot_configs[1].show_fov_only_aiming ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_1_smooth=") << (cfg::aimbot_configs[1].smooth ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_1_smooth_type=") << cfg::aimbot_configs[1].smooth_type
     << xorstr_(";\n");
  ss << xorstr_("aimbot_1_smooth_factor=")
     << cfg::aimbot_configs[1].smooth_factor << xorstr_(";\n");
  ss << xorstr_("aimbot_1_ignore_friends=")
     << (cfg::aimbot_configs[1].ignore_friends ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_1_ignore_admins=")
     << (cfg::aimbot_configs[1].ignore_admins ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_2_aimbot=") << (cfg::aimbot_configs[2].aimbot ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_2_aimtype=") << cfg::aimbot_configs[2].aimtype
     << xorstr_(";\n");
  ss << xorstr_("aimbot_2_fov=") << cfg::aimbot_configs[2].fov
     << xorstr_(";\n");
  ss << xorstr_("aimbot_2_aim_distance=") << cfg::aimbot_configs[2].aim_distance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_2_aim_chance=") << cfg::aimbot_configs[2].aim_chance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_2_aimbone=") << cfg::aimbot_configs[2].aimbone
     << xorstr_(";\n");
  ss << xorstr_("aimbot_2_draw_fov=")
     << (cfg::aimbot_configs[2].draw_fov ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_2_show_fov_only_aiming=")
     << (cfg::aimbot_configs[2].show_fov_only_aiming ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_2_smooth=") << (cfg::aimbot_configs[2].smooth ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_2_smooth_type=") << cfg::aimbot_configs[2].smooth_type
     << xorstr_(";\n");
  ss << xorstr_("aimbot_2_smooth_factor=")
     << cfg::aimbot_configs[2].smooth_factor << xorstr_(";\n");
  ss << xorstr_("aimbot_2_ignore_friends=")
     << (cfg::aimbot_configs[2].ignore_friends ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_2_ignore_admins=")
     << (cfg::aimbot_configs[2].ignore_admins ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_3_aimbot=") << (cfg::aimbot_configs[3].aimbot ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_3_aimtype=") << cfg::aimbot_configs[3].aimtype
     << xorstr_(";\n");
  ss << xorstr_("aimbot_3_fov=") << cfg::aimbot_configs[3].fov
     << xorstr_(";\n");
  ss << xorstr_("aimbot_3_aim_distance=") << cfg::aimbot_configs[3].aim_distance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_3_aim_chance=") << cfg::aimbot_configs[3].aim_chance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_3_aimbone=") << cfg::aimbot_configs[3].aimbone
     << xorstr_(";\n");
  ss << xorstr_("aimbot_3_draw_fov=")
     << (cfg::aimbot_configs[3].draw_fov ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_3_show_fov_only_aiming=")
     << (cfg::aimbot_configs[3].show_fov_only_aiming ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_3_smooth=") << (cfg::aimbot_configs[3].smooth ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_3_smooth_type=") << cfg::aimbot_configs[3].smooth_type
     << xorstr_(";\n");
  ss << xorstr_("aimbot_3_smooth_factor=")
     << cfg::aimbot_configs[3].smooth_factor << xorstr_(";\n");
  ss << xorstr_("aimbot_3_ignore_friends=")
     << (cfg::aimbot_configs[3].ignore_friends ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_3_ignore_admins=")
     << (cfg::aimbot_configs[3].ignore_admins ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_4_aimbot=") << (cfg::aimbot_configs[4].aimbot ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_4_aimtype=") << cfg::aimbot_configs[4].aimtype
     << xorstr_(";\n");
  ss << xorstr_("aimbot_4_fov=") << cfg::aimbot_configs[4].fov
     << xorstr_(";\n");
  ss << xorstr_("aimbot_4_aim_distance=") << cfg::aimbot_configs[4].aim_distance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_4_aim_chance=") << cfg::aimbot_configs[4].aim_chance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_4_aimbone=") << cfg::aimbot_configs[4].aimbone
     << xorstr_(";\n");
  ss << xorstr_("aimbot_4_draw_fov=")
     << (cfg::aimbot_configs[4].draw_fov ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_4_show_fov_only_aiming=")
     << (cfg::aimbot_configs[4].show_fov_only_aiming ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_4_smooth=") << (cfg::aimbot_configs[4].smooth ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_4_smooth_type=") << cfg::aimbot_configs[4].smooth_type
     << xorstr_(";\n");
  ss << xorstr_("aimbot_4_smooth_factor=")
     << cfg::aimbot_configs[4].smooth_factor << xorstr_(";\n");
  ss << xorstr_("aimbot_4_ignore_friends=")
     << (cfg::aimbot_configs[4].ignore_friends ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_4_ignore_admins=")
     << (cfg::aimbot_configs[4].ignore_admins ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_5_aimbot=") << (cfg::aimbot_configs[5].aimbot ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_5_aimtype=") << cfg::aimbot_configs[5].aimtype
     << xorstr_(";\n");
  ss << xorstr_("aimbot_5_fov=") << cfg::aimbot_configs[5].fov
     << xorstr_(";\n");
  ss << xorstr_("aimbot_5_aim_distance=") << cfg::aimbot_configs[5].aim_distance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_5_aim_chance=") << cfg::aimbot_configs[5].aim_chance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_5_aimbone=") << cfg::aimbot_configs[5].aimbone
     << xorstr_(";\n");
  ss << xorstr_("aimbot_5_draw_fov=")
     << (cfg::aimbot_configs[5].draw_fov ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_5_show_fov_only_aiming=")
     << (cfg::aimbot_configs[5].show_fov_only_aiming ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_5_smooth=") << (cfg::aimbot_configs[5].smooth ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_5_smooth_type=") << cfg::aimbot_configs[5].smooth_type
     << xorstr_(";\n");
  ss << xorstr_("aimbot_5_smooth_factor=")
     << cfg::aimbot_configs[5].smooth_factor << xorstr_(";\n");
  ss << xorstr_("aimbot_5_ignore_friends=")
     << (cfg::aimbot_configs[5].ignore_friends ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_5_ignore_admins=")
     << (cfg::aimbot_configs[5].ignore_admins ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_6_aimbot=") << (cfg::aimbot_configs[6].aimbot ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_6_aimtype=") << cfg::aimbot_configs[6].aimtype
     << xorstr_(";\n");
  ss << xorstr_("aimbot_6_fov=") << cfg::aimbot_configs[6].fov
     << xorstr_(";\n");
  ss << xorstr_("aimbot_6_aim_distance=") << cfg::aimbot_configs[6].aim_distance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_6_aim_chance=") << cfg::aimbot_configs[6].aim_chance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_6_aimbone=") << cfg::aimbot_configs[6].aimbone
     << xorstr_(";\n");
  ss << xorstr_("aimbot_6_draw_fov=")
     << (cfg::aimbot_configs[6].draw_fov ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_6_show_fov_only_aiming=")
     << (cfg::aimbot_configs[6].show_fov_only_aiming ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_6_smooth=") << (cfg::aimbot_configs[6].smooth ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_6_smooth_type=") << cfg::aimbot_configs[6].smooth_type
     << xorstr_(";\n");
  ss << xorstr_("aimbot_6_smooth_factor=")
     << cfg::aimbot_configs[6].smooth_factor << xorstr_(";\n");
  ss << xorstr_("aimbot_6_ignore_friends=")
     << (cfg::aimbot_configs[6].ignore_friends ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_6_ignore_admins=")
     << (cfg::aimbot_configs[6].ignore_admins ? 1 : 0) << xorstr_(";\n");

  // Keybinds serialization
  for (const auto &item : menu_cfg->order) {
    if (item.second == checkbox_type) {
      checkbox_t *data = menu_cfg->fill<checkbox_t>(item.first);
      if (data && (data->keybinds_enabled || data->is_weapon) &&
          !data->keybinds_list.empty()) {
        ss << xorstr_("kb_") << item.first << xorstr_("=");
        for (size_t i = 0; i < data->keybinds_list.size(); ++i) {
          ss << data->keybinds_list[i].idx << xorstr_(",")
             << data->keybinds_list[i].key << xorstr_(",")
             << data->keybinds_list[i].mode;
          if (i < data->keybinds_list.size() - 1)
            ss << xorstr_("|");
        }
        ss << xorstr_(";\n");
      }
    }
  }
  ss << xorstr_("col_friends_distance=") << cfg::col_friends_distance.Value.x
     << xorstr_(",") << cfg::col_friends_distance.Value.y << xorstr_(",")
     << cfg::col_friends_distance.Value.z << xorstr_(",")
     << cfg::col_friends_distance.Value.w << xorstr_(";\n");
  ss << xorstr_("ESP_Friends_Weapon=") << (cfg::ESP_Friends_Weapon ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("col_friends_weapon=") << cfg::col_friends_weapon.Value.x
     << xorstr_(",") << cfg::col_friends_weapon.Value.y << xorstr_(",")
     << cfg::col_friends_weapon.Value.z << xorstr_(",")
     << cfg::col_friends_weapon.Value.w << xorstr_(";\n");
  ss << xorstr_("ESP_Friends_SkeletonLines=")
     << (cfg::ESP_Friends_SkeletonLines ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("col_friends_skeleton=") << cfg::col_friends_skeleton.Value.x
     << xorstr_(",") << cfg::col_friends_skeleton.Value.y << xorstr_(",")
     << cfg::col_friends_skeleton.Value.z << xorstr_(",")
     << cfg::col_friends_skeleton.Value.w << xorstr_(";\n");
  ss << xorstr_("ESP_Friends_Line=") << (cfg::ESP_Friends_Line ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("col_friends_line=") << cfg::col_friends_line.Value.x
     << xorstr_(",") << cfg::col_friends_line.Value.y << xorstr_(",")
     << cfg::col_friends_line.Value.z << xorstr_(",")
     << cfg::col_friends_line.Value.w << xorstr_(";\n");
  ss << xorstr_("friend_radius=") << cfg::friend_radius << xorstr_(";\n");
  ss << xorstr_("friend_max_dist=") << cfg::friend_max_dist << xorstr_(";\n");
  ss << xorstr_("ESP_Font=") << cfg::ESP_Font << xorstr_(";\n");
  ss << xorstr_("bypass_obs=") << (cfg::bypass_obs ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("targets_arrows_enable=")
     << (cfg::targets_arrows_enable ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("col_targets_arrows=") << cfg::col_targets_arrows.Value.x
     << xorstr_(",") << cfg::col_targets_arrows.Value.y << xorstr_(",")
     << cfg::col_targets_arrows.Value.z << xorstr_(",")
     << cfg::col_targets_arrows.Value.w << xorstr_(";\n");
  ss << xorstr_("targets_arrows_size=") << cfg::targets_arrows_size
     << xorstr_(";\n");
  ss << xorstr_("targets_arrows_fov=") << cfg::targets_arrows_fov
     << xorstr_(";\n");
  ss << xorstr_("targets_arrows_flicker=")
     << (cfg::targets_arrows_flicker ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("targets_arrows_exclude_visible=")
     << (cfg::targets_arrows_exclude_visible ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("targets_highlight_heavy_sniper=")
     << (cfg::targets_highlight_heavy_sniper ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("col_targets_highlight_heavy_sniper=")
     << cfg::col_targets_highlight_heavy_sniper.Value.x << xorstr_(",")
     << cfg::col_targets_highlight_heavy_sniper.Value.y << xorstr_(",")
     << cfg::col_targets_highlight_heavy_sniper.Value.z << xorstr_(",")
     << cfg::col_targets_highlight_heavy_sniper.Value.w << xorstr_(";\n");
  ss << xorstr_("targets_highlight_heavy_mk2=")
     << (cfg::targets_highlight_heavy_mk2 ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("col_targets_highlight_heavy_mk2=")
     << cfg::col_targets_highlight_heavy_mk2.Value.x << xorstr_(",")
     << cfg::col_targets_highlight_heavy_mk2.Value.y << xorstr_(",")
     << cfg::col_targets_highlight_heavy_mk2.Value.z << xorstr_(",")
     << cfg::col_targets_highlight_heavy_mk2.Value.w << xorstr_(";\n");
  ss << xorstr_("targets_highlight_precision_rifle=")
     << (cfg::targets_highlight_precision_rifle ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("col_targets_highlight_precision_rifle=")
     << cfg::col_targets_highlight_precision_rifle.Value.x << xorstr_(",")
     << cfg::col_targets_highlight_precision_rifle.Value.y << xorstr_(",")
     << cfg::col_targets_highlight_precision_rifle.Value.z << xorstr_(",")
     << cfg::col_targets_highlight_precision_rifle.Value.w << xorstr_(";\n");
  ss << xorstr_("targets_radar_enable=") << (cfg::targets_radar_enable ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("targets_radar_size=") << cfg::targets_radar_size
     << xorstr_(";\n");
  ss << xorstr_("targets_radar_opacity=") << cfg::targets_radar_opacity
     << xorstr_(";\n");
  ss << xorstr_("targets_radar_range=") << cfg::targets_radar_range
     << xorstr_(";\n");

  ss << xorstr_("aimbot_0_aimbot=") << (cfg::aimbot_configs[0].aimbot ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_0_aimtype=") << cfg::aimbot_configs[0].aimtype
     << xorstr_(";\n");
  ss << xorstr_("aimbot_0_fov=") << cfg::aimbot_configs[0].fov
     << xorstr_(";\n");
  ss << xorstr_("aimbot_0_aim_distance=") << cfg::aimbot_configs[0].aim_distance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_0_aim_chance=") << cfg::aimbot_configs[0].aim_chance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_0_aimbone=") << cfg::aimbot_configs[0].aimbone
     << xorstr_(";\n");
  ss << xorstr_("aimbot_0_draw_fov=")
     << (cfg::aimbot_configs[0].draw_fov ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_0_show_fov_only_aiming=")
     << (cfg::aimbot_configs[0].show_fov_only_aiming ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_0_smooth=") << (cfg::aimbot_configs[0].smooth ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_0_smooth_type=") << cfg::aimbot_configs[0].smooth_type
     << xorstr_(";\n");
  ss << xorstr_("aimbot_0_smooth_factor=")
     << cfg::aimbot_configs[0].smooth_factor << xorstr_(";\n");
  ss << xorstr_("aimbot_0_ignore_friends=")
     << (cfg::aimbot_configs[0].ignore_friends ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_1_aimbot=") << (cfg::aimbot_configs[1].aimbot ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_1_aimtype=") << cfg::aimbot_configs[1].aimtype
     << xorstr_(";\n");
  ss << xorstr_("aimbot_1_fov=") << cfg::aimbot_configs[1].fov
     << xorstr_(";\n");
  ss << xorstr_("aimbot_1_aim_distance=") << cfg::aimbot_configs[1].aim_distance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_1_aim_chance=") << cfg::aimbot_configs[1].aim_chance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_1_aimbone=") << cfg::aimbot_configs[1].aimbone
     << xorstr_(";\n");
  ss << xorstr_("aimbot_1_draw_fov=")
     << (cfg::aimbot_configs[1].draw_fov ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_1_show_fov_only_aiming=")
     << (cfg::aimbot_configs[1].show_fov_only_aiming ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_1_smooth=") << (cfg::aimbot_configs[1].smooth ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_1_smooth_type=") << cfg::aimbot_configs[1].smooth_type
     << xorstr_(";\n");
  ss << xorstr_("aimbot_1_smooth_factor=")
     << cfg::aimbot_configs[1].smooth_factor << xorstr_(";\n");
  ss << xorstr_("aimbot_1_ignore_friends=")
     << (cfg::aimbot_configs[1].ignore_friends ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_2_aimbot=") << (cfg::aimbot_configs[2].aimbot ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_2_aimtype=") << cfg::aimbot_configs[2].aimtype
     << xorstr_(";\n");
  ss << xorstr_("aimbot_2_fov=") << cfg::aimbot_configs[2].fov
     << xorstr_(";\n");
  ss << xorstr_("aimbot_2_aim_distance=") << cfg::aimbot_configs[2].aim_distance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_2_aim_chance=") << cfg::aimbot_configs[2].aim_chance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_2_aimbone=") << cfg::aimbot_configs[2].aimbone
     << xorstr_(";\n");
  ss << xorstr_("aimbot_2_draw_fov=")
     << (cfg::aimbot_configs[2].draw_fov ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_2_show_fov_only_aiming=")
     << (cfg::aimbot_configs[2].show_fov_only_aiming ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_2_smooth=") << (cfg::aimbot_configs[2].smooth ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_2_smooth_type=") << cfg::aimbot_configs[2].smooth_type
     << xorstr_(";\n");
  ss << xorstr_("aimbot_2_smooth_factor=")
     << cfg::aimbot_configs[2].smooth_factor << xorstr_(";\n");
  ss << xorstr_("aimbot_2_ignore_friends=")
     << (cfg::aimbot_configs[2].ignore_friends ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_3_aimbot=") << (cfg::aimbot_configs[3].aimbot ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_3_aimtype=") << cfg::aimbot_configs[3].aimtype
     << xorstr_(";\n");
  ss << xorstr_("aimbot_3_fov=") << cfg::aimbot_configs[3].fov
     << xorstr_(";\n");
  ss << xorstr_("aimbot_3_aim_distance=") << cfg::aimbot_configs[3].aim_distance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_3_aim_chance=") << cfg::aimbot_configs[3].aim_chance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_3_aimbone=") << cfg::aimbot_configs[3].aimbone
     << xorstr_(";\n");
  ss << xorstr_("aimbot_3_draw_fov=")
     << (cfg::aimbot_configs[3].draw_fov ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_3_show_fov_only_aiming=")
     << (cfg::aimbot_configs[3].show_fov_only_aiming ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_3_smooth=") << (cfg::aimbot_configs[3].smooth ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_3_smooth_type=") << cfg::aimbot_configs[3].smooth_type
     << xorstr_(";\n");
  ss << xorstr_("aimbot_3_smooth_factor=")
     << cfg::aimbot_configs[3].smooth_factor << xorstr_(";\n");
  ss << xorstr_("aimbot_3_ignore_friends=")
     << (cfg::aimbot_configs[3].ignore_friends ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_4_aimbot=") << (cfg::aimbot_configs[4].aimbot ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_4_aimtype=") << cfg::aimbot_configs[4].aimtype
     << xorstr_(";\n");
  ss << xorstr_("aimbot_4_fov=") << cfg::aimbot_configs[4].fov
     << xorstr_(";\n");
  ss << xorstr_("aimbot_4_aim_distance=") << cfg::aimbot_configs[4].aim_distance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_4_aim_chance=") << cfg::aimbot_configs[4].aim_chance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_4_aimbone=") << cfg::aimbot_configs[4].aimbone
     << xorstr_(";\n");
  ss << xorstr_("aimbot_4_draw_fov=")
     << (cfg::aimbot_configs[4].draw_fov ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_4_show_fov_only_aiming=")
     << (cfg::aimbot_configs[4].show_fov_only_aiming ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_4_smooth=") << (cfg::aimbot_configs[4].smooth ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_4_smooth_type=") << cfg::aimbot_configs[4].smooth_type
     << xorstr_(";\n");
  ss << xorstr_("aimbot_4_smooth_factor=")
     << cfg::aimbot_configs[4].smooth_factor << xorstr_(";\n");
  ss << xorstr_("aimbot_4_ignore_friends=")
     << (cfg::aimbot_configs[4].ignore_friends ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_5_aimbot=") << (cfg::aimbot_configs[5].aimbot ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_5_aimtype=") << cfg::aimbot_configs[5].aimtype
     << xorstr_(";\n");
  ss << xorstr_("aimbot_5_fov=") << cfg::aimbot_configs[5].fov
     << xorstr_(";\n");
  ss << xorstr_("aimbot_5_aim_distance=") << cfg::aimbot_configs[5].aim_distance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_5_aim_chance=") << cfg::aimbot_configs[5].aim_chance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_5_aimbone=") << cfg::aimbot_configs[5].aimbone
     << xorstr_(";\n");
  ss << xorstr_("aimbot_5_draw_fov=")
     << (cfg::aimbot_configs[5].draw_fov ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_5_show_fov_only_aiming=")
     << (cfg::aimbot_configs[5].show_fov_only_aiming ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_5_smooth=") << (cfg::aimbot_configs[5].smooth ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_5_smooth_type=") << cfg::aimbot_configs[5].smooth_type
     << xorstr_(";\n");
  ss << xorstr_("aimbot_5_smooth_factor=")
     << cfg::aimbot_configs[5].smooth_factor << xorstr_(";\n");
  ss << xorstr_("aimbot_5_ignore_friends=")
     << (cfg::aimbot_configs[5].ignore_friends ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_6_aimbot=") << (cfg::aimbot_configs[6].aimbot ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_6_aimtype=") << cfg::aimbot_configs[6].aimtype
     << xorstr_(";\n");
  ss << xorstr_("aimbot_6_fov=") << cfg::aimbot_configs[6].fov
     << xorstr_(";\n");
  ss << xorstr_("aimbot_6_aim_distance=") << cfg::aimbot_configs[6].aim_distance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_6_aim_chance=") << cfg::aimbot_configs[6].aim_chance
     << xorstr_(";\n");
  ss << xorstr_("aimbot_6_aimbone=") << cfg::aimbot_configs[6].aimbone
     << xorstr_(";\n");
  ss << xorstr_("aimbot_6_draw_fov=")
     << (cfg::aimbot_configs[6].draw_fov ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_6_show_fov_only_aiming=")
     << (cfg::aimbot_configs[6].show_fov_only_aiming ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_6_smooth=") << (cfg::aimbot_configs[6].smooth ? 1 : 0)
     << xorstr_(";\n");
  ss << xorstr_("aimbot_6_smooth_type=") << cfg::aimbot_configs[6].smooth_type
     << xorstr_(";\n");
  ss << xorstr_("aimbot_6_smooth_factor=")
     << cfg::aimbot_configs[6].smooth_factor << xorstr_(";\n");
  ss << xorstr_("aimbot_6_ignore_friends=")
     << (cfg::aimbot_configs[6].ignore_friends ? 1 : 0) << xorstr_(";\n");
  ss << xorstr_("aimbot_6_ignore_admins=")
     << (cfg::aimbot_configs[6].ignore_admins ? 1 : 0) << xorstr_(";\n");

  // Keybinds serialization
  for (const auto &item : menu_cfg->order) {
    if (item.second == checkbox_type) {
      checkbox_t *data = menu_cfg->fill<checkbox_t>(item.first);
      if (data && !data->keybinds_list.empty()) {
        ss << xorstr_("kb_") << item.first << xorstr_("=");
        for (size_t i = 0; i < data->keybinds_list.size(); ++i) {
          ss << data->keybinds_list[i].idx << xorstr_(",")
             << data->keybinds_list[i].key << xorstr_(",")
             << data->keybinds_list[i].mode;
          if (i < data->keybinds_list.size() - 1)
            ss << xorstr_("|");
        }
        ss << xorstr_(";\n");
      }
    }
  }

  std::string result = ss.str();

  return result;
}

static inline int safe_stoi(const std::string &str, int def = 0) {
  if (str.empty())
    return def;
  try {
    size_t idx = 0;
    return std::stoi(str, &idx);
  } catch (...) {
    return def;
  }
}

static inline float safe_stof(const std::string &str, float def = 0.0f) {
  if (str.empty())
    return def;
  try {
    size_t idx = 0;
    return std::stof(str, &idx);
  } catch (...) {
    return def;
  }
}

static inline ImColor parse_col(const std::string &s) {
  size_t c1 = s.find(',');
  if (c1 == std::string::npos)
    return ImColor(1.f, 1.f, 1.f, 1.f);
  size_t c2 = s.find(',', c1 + 1);
  if (c2 == std::string::npos)
    return ImColor(1.f, 1.f, 1.f, 1.f);
  size_t c3 = s.find(',', c2 + 1);
  if (c3 == std::string::npos)
    return ImColor(1.f, 1.f, 1.f, 1.f);
  return ImColor(safe_stof(s.substr(0, c1), 1.f),
                 safe_stof(s.substr(c1 + 1, c2 - c1 - 1), 1.f),
                 safe_stof(s.substr(c2 + 1, c3 - c2 - 1), 1.f),
                 safe_stof(s.substr(c3 + 1), 1.f));
}

void deserialize(const std::string &data) {
  try {
    // Clear existing keybinds from all checkboxes before loading new config
    for (const auto &item : menu_cfg->order) {
      if (item.second == checkbox_type) {
        checkbox_t *cb = menu_cfg->fill<checkbox_t>(item.first);
        if (cb) {
          cb->keybinds_list.clear();
          cb->last_keybind = {0, 0, 0};
        }
      }
    }

    std::istringstream stream(data);
    std::string line;
    while (std::getline(stream, line)) {
      // strip trailing whitespace/CR
      while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
                               line.back() == ' ' || line.back() == '\t'))
        line.pop_back();
      if (line.empty())
        continue;

      size_t delim = line.find('=');
      if (delim == std::string::npos)
        continue;

      std::string key = line.substr(0, delim);
      std::string val = line.substr(delim + 1);

      // strip trailing ;/whitespace from value
      while (!val.empty() &&
             (val.back() == ';' || val.back() == '\r' || val.back() == '\n' ||
              val.back() == ' ' || val.back() == '\t'))
        val.pop_back();
      // strip leading whitespace from value
      size_t vs = val.find_first_not_of(" \t");
      if (vs != std::string::npos && vs > 0)
        val = val.substr(vs);

      if (val.empty())
        continue;

      if (key == xorstr_("EntityUpdateDelayMs")) {
        cfg::EntityUpdateDelayMs = safe_stoi(val);
      }
      if (key == xorstr_("SkeletonUpdateDelayMs")) {
        cfg::SkeletonUpdateDelayMs = safe_stoi(val);
      } else if (key == xorstr_("MaxEntities")) {
        cfg::MaxEntities = safe_stoi(val);
      } else if (key == xorstr_("MaxWeapons")) {
        cfg::MaxWeapons = std::clamp(safe_stoi(val), 1, 1500);
      } else if (key == xorstr_("MaxPickups")) {
        cfg::MaxPickups = std::clamp(safe_stoi(val), 1, 1500);
      } else if (key == xorstr_("ToggleMenuKey")) {
        cfg::ToggleMenuKey = safe_stoi(val);
      } else if (key == xorstr_("Misc_Debug")) {
        cfg::Misc_Debug = safe_stoi(val) != 0;
      } else if (key == xorstr_("suicide_key")) {
        cfg::suicide_key = safe_stoi(val);
      } else if (key == xorstr_("tp_waypoint_key")) {
        cfg::tp_waypoint_key = safe_stoi(val);
      } else if (key == xorstr_("unload_cheat_key")) {
        cfg::unload_cheat_key = safe_stoi(val);
      } else if (key == xorstr_("weapon_no_recoil")) {
        cfg::weapon_no_recoil = safe_stoi(val) != 0;
      } else if (key == xorstr_("weapon_no_spread")) {
        cfg::weapon_no_spread = safe_stoi(val) != 0;
      } else if (key == xorstr_("weapon_inf_ammo")) {
        cfg::weapon_inf_ammo = safe_stoi(val) != 0;
      } else if (key == xorstr_("weapon_double_tap")) {
        cfg::weapon_double_tap = safe_stoi(val) != 0;
      } else if (key == xorstr_("enable_wallshot")) {
        cfg::enable_wallshot = safe_stoi(val) != 0;
      } else if (key == xorstr_("enable_weapon_configs")) {
        cfg::enable_weapon_configs = safe_stoi(val) != 0;
      } else if (key == xorstr_("active_weapon_category")) {
        cfg::active_weapon_category = safe_stoi(val);
      } else if (key == xorstr_("current_weapon_category")) {
        cfg::current_weapon_category = safe_stoi(val);
      } else if (key == xorstr_("aimkey")) {
        cfg::aimkey = safe_stoi(val);
      } else if (key == xorstr_("aimkey_mode")) {
        cfg::aimkey_mode = safe_stoi(val);
      } else if (key == xorstr_("triggerbot")) {
        cfg::triggerbot = safe_stoi(val) != 0;
      } else if (key == xorstr_("triggerkey")) {
        cfg::triggerkey = safe_stoi(val);
      } else if (key == xorstr_("triggerkey_mode")) {
        cfg::triggerkey_mode = safe_stoi(val);
      } else if (key == xorstr_("triggerdelay")) {
        cfg::triggerdelay = safe_stoi(val);
      } else if (key == xorstr_("trigger_ignore_friends")) {
        cfg::trigger_ignore_friends = safe_stoi(val) != 0;
      } else if (key == xorstr_("trigger_ignore_admins")) {
        cfg::trigger_ignore_admins = safe_stoi(val) != 0;
      } else if (key == xorstr_("ESP_MaxDistance")) {
        cfg::ESP_MaxDistance = safe_stof(val);
      } else if (key == xorstr_("ESP_PlayerOnly")) {
        cfg::ESP_PlayerOnly = safe_stoi(val) != 0;
      } else if (key == xorstr_("esp_show_friends")) {
        cfg::esp_show_friends = safe_stoi(val) != 0;
      } else if (key == xorstr_("esp_show_admins")) {
        cfg::esp_show_admins = safe_stoi(val) != 0;
      } else if (key == xorstr_("ESP_AimLine")) {
        cfg::ESP_AimLine = safe_stoi(val) != 0;
      } else if (key == xorstr_("col_AimLine")) {
        cfg::col_AimLine = parse_col(val);
      } else if (key == xorstr_("ESP_Box")) {
        cfg::ESP_Box = safe_stoi(val) != 0;
      } else if (key == xorstr_("esp_box_thickness")) {
        cfg::esp_box_thickness = safe_stof(val);
      } else if (key == xorstr_("esp_box_rounding")) {
        cfg::esp_box_rounding = safe_stof(val);
      } else if (key == xorstr_("esp_skeleton_thickness")) {
        cfg::esp_skeleton_thickness = safe_stof(val);
      }
      if (key == xorstr_("ESP_Line")) {
        cfg::ESP_Line = safe_stoi(val) != 0;
      } else if (key == xorstr_("ESP_HealthBar")) {
        cfg::ESP_HealthBar = safe_stoi(val) != 0;
      } else if (key == xorstr_("ESP_Distance")) {
        cfg::ESP_Distance = safe_stoi(val) != 0;
      } else if (key == xorstr_("ESP_Weapon")) {
        cfg::ESP_Weapon = safe_stoi(val) != 0;
      } else if (key == xorstr_("ESP_Skeleton")) {
        cfg::ESP_Skeleton = safe_stoi(val) != 0;
      } else if (key == xorstr_("ESP_SkeletonLines")) {
        cfg::ESP_SkeletonLines = safe_stoi(val) != 0;
      } else if (key == xorstr_("esp_players_name")) {
        cfg::esp_players_name = safe_stoi(val) != 0;
      } else if (key == xorstr_("esp_players_box_type")) {
        cfg::esp_players_box_type = safe_stoi(val);
      } else if (key == xorstr_("esp_players_bar_position")) {
        cfg::esp_players_bar_position = safe_stoi(val);
      } else if (key == xorstr_("esp_players_armor_bar_position")) {
        cfg::esp_players_armor_bar_position = safe_stoi(val);
      } else if (key == xorstr_("col_players_box")) {
        cfg::col_players_box = parse_col(val);
      } else if (key == xorstr_("col_players_skeleton")) {
        cfg::col_players_skeleton = parse_col(val);
      } else if (key == xorstr_("col_players_line")) {
        cfg::col_players_line = parse_col(val);
      } else if (key == xorstr_("col_players_distance")) {
        cfg::col_players_distance = parse_col(val);
      } else if (key == xorstr_("col_players_weapon")) {
        cfg::col_players_weapon = parse_col(val);
      } else if (key == xorstr_("esp_custom_health_colors")) {
        cfg::esp_custom_health_colors = safe_stoi(val) != 0;
      } else if (key == xorstr_("col_health")) {
        cfg::col_health = parse_col(val);
      } else if (key == xorstr_("col_armor")) {
        cfg::col_armor = parse_col(val);
      } else if (key == xorstr_("esp_objects_enabled")) {
        cfg::esp_objects_enabled = safe_stoi(val) != 0;
      } else if (key == xorstr_("esp_objects_max_distance")) {
        cfg::esp_objects_max_distance = safe_stof(val);
      } else if (key == xorstr_("col_objects")) {
        cfg::col_objects = parse_col(val);
      } else if (key == xorstr_("ESP_Friends_Box")) {
        cfg::ESP_Friends_Box = safe_stoi(val) != 0;
      } else if (key == xorstr_("esp_friends_box_type")) {
        cfg::esp_friends_box_type = safe_stoi(val);
      } else if (key == xorstr_("esp_friends_box_thickness")) {
        cfg::esp_friends_box_thickness = safe_stof(val);
      } else if (key == xorstr_("esp_friends_box_rounding")) {
        cfg::esp_friends_box_rounding = safe_stof(val);
      } else if (key == xorstr_("esp_friends_skeleton_thickness")) {
        cfg::esp_friends_skeleton_thickness = safe_stof(val);
      } else if (key == xorstr_("col_friends_box")) {
        cfg::col_friends_box = parse_col(val);
      }
      if (key == xorstr_("ESP_Friends_HealthBar")) {
        cfg::ESP_Friends_HealthBar = safe_stoi(val) != 0;
      } else if (key == xorstr_("esp_friends_bar_position")) {
        cfg::esp_friends_bar_position = safe_stoi(val);
      } else if (key == xorstr_("esp_friends_armor_bar_position")) {
        cfg::esp_friends_armor_bar_position = safe_stoi(val);
      } else if (key == xorstr_("ESP_Friends_Distance")) {
        cfg::ESP_Friends_Distance = safe_stoi(val) != 0;
      } else if (key == xorstr_("col_friends_distance")) {
        cfg::col_friends_distance = parse_col(val);
      } else if (key == xorstr_("ESP_Friends_Weapon")) {
        cfg::ESP_Friends_Weapon = safe_stoi(val) != 0;
      } else if (key == xorstr_("col_friends_weapon")) {
        cfg::col_friends_weapon = parse_col(val);
      } else if (key == xorstr_("ESP_Friends_SkeletonLines")) {
        cfg::ESP_Friends_SkeletonLines = safe_stoi(val) != 0;
      } else if (key == xorstr_("col_friends_skeleton")) {
        cfg::col_friends_skeleton = parse_col(val);
      } else if (key == xorstr_("ESP_Friends_Line")) {
        cfg::ESP_Friends_Line = safe_stoi(val) != 0;
      } else if (key == xorstr_("col_friends_line")) {
        cfg::col_friends_line = parse_col(val);
      } else if (key == xorstr_("esp_friends_custom_health_colors")) {
        cfg::esp_friends_custom_health_colors = safe_stoi(val) != 0;
      } else if (key == xorstr_("col_friends_health")) {
        cfg::col_friends_health = parse_col(val);
      } else if (key == xorstr_("col_friends_armor")) {
        cfg::col_friends_armor = parse_col(val);
      } else if (key == xorstr_("ESP_Admins_Box")) {
        cfg::ESP_Admins_Box = safe_stoi(val) != 0;
      } else if (key == xorstr_("esp_admins_box_type")) {
        cfg::esp_admins_box_type = safe_stoi(val);
      }
      if (key == xorstr_("esp_admins_box_thickness")) {
        cfg::esp_admins_box_thickness = safe_stof(val);
      } else if (key == xorstr_("esp_admins_box_rounding")) {
        cfg::esp_admins_box_rounding = safe_stof(val);
      } else if (key == xorstr_("esp_admins_skeleton_thickness")) {
        cfg::esp_admins_skeleton_thickness = safe_stof(val);
      } else if (key == xorstr_("col_admins_box")) {
        cfg::col_admins_box = parse_col(val);
      } else if (key == xorstr_("ESP_Admins_HealthBar")) {
        cfg::ESP_Admins_HealthBar = safe_stoi(val) != 0;
      } else if (key == xorstr_("esp_admins_bar_position")) {
        cfg::esp_admins_bar_position = safe_stoi(val);
      } else if (key == xorstr_("esp_admins_armor_bar_position")) {
        cfg::esp_admins_armor_bar_position = safe_stoi(val);
      } else if (key == xorstr_("ESP_Admins_Distance")) {
        cfg::ESP_Admins_Distance = safe_stoi(val) != 0;
      } else if (key == xorstr_("col_admins_distance")) {
        cfg::col_admins_distance = parse_col(val);
      } else if (key == xorstr_("ESP_Admins_Weapon")) {
        cfg::ESP_Admins_Weapon = safe_stoi(val) != 0;
      } else if (key == xorstr_("col_admins_weapon")) {
        cfg::col_admins_weapon = parse_col(val);
      } else if (key == xorstr_("ESP_Admins_SkeletonLines")) {
        cfg::ESP_Admins_SkeletonLines = safe_stoi(val) != 0;
      } else if (key == xorstr_("col_admins_skeleton")) {
        cfg::col_admins_skeleton = parse_col(val);
      } else if (key == xorstr_("ESP_Admins_Line")) {
        cfg::ESP_Admins_Line = safe_stoi(val) != 0;
      } else if (key == xorstr_("col_admins_line")) {
        cfg::col_admins_line = parse_col(val);
      } else if (key == xorstr_("esp_admins_custom_health_colors")) {
        cfg::esp_admins_custom_health_colors = safe_stoi(val) != 0;
      } else if (key == xorstr_("col_admins_health")) {
        cfg::col_admins_health = parse_col(val);
      } else if (key == xorstr_("col_admins_armor")) {
        cfg::col_admins_armor = parse_col(val);
      } else if (key == xorstr_("show_admins_around")) {
        cfg::show_admins_around = safe_stoi(val) != 0;
      } else if (key == xorstr_("col_admins_around")) {
        cfg::col_admins_around = parse_col(val);
      } else if (key == xorstr_("show_players_around")) {
        cfg::show_players_around = safe_stoi(val) != 0;
      } else if (key == xorstr_("col_players_around")) {
        cfg::col_players_around = parse_col(val);
      } else if (key == xorstr_("friend_radius")) {
        cfg::friend_radius = safe_stoi(val);
      } else if (key == xorstr_("friend_max_dist")) {
        cfg::friend_max_dist = safe_stoi(val);
      }
      if (key == xorstr_("ESP_Font")) {
        cfg::ESP_Font = safe_stoi(val);
      } else if (key == xorstr_("bypass_obs")) {
        cfg::bypass_obs = safe_stoi(val) != 0;
      } else if (key == xorstr_("targets_arrows_enable")) {
        cfg::targets_arrows_enable = safe_stoi(val) != 0;
      } else if (key == xorstr_("col_targets_arrows")) {
        cfg::col_targets_arrows = parse_col(val);
      } else if (key == xorstr_("targets_arrows_size")) {
        cfg::targets_arrows_size = safe_stof(val);
      } else if (key == xorstr_("targets_arrows_fov")) {
        cfg::targets_arrows_fov = safe_stof(val);
      } else if (key == xorstr_("targets_arrows_flicker")) {
        cfg::targets_arrows_flicker = safe_stoi(val) != 0;
      } else if (key == xorstr_("targets_arrows_exclude_visible")) {
        cfg::targets_arrows_exclude_visible = safe_stoi(val) != 0;
      } else if (key == xorstr_("targets_highlight_heavy_sniper")) {
        cfg::targets_highlight_heavy_sniper = safe_stoi(val) != 0;
      } else if (key == xorstr_("col_targets_highlight_heavy_sniper")) {
        cfg::col_targets_highlight_heavy_sniper = parse_col(val);
      } else if (key == xorstr_("targets_highlight_heavy_mk2")) {
        cfg::targets_highlight_heavy_mk2 = safe_stoi(val) != 0;
      } else if (key == xorstr_("col_targets_highlight_heavy_mk2")) {
        cfg::col_targets_highlight_heavy_mk2 = parse_col(val);
      } else if (key == xorstr_("targets_highlight_precision_rifle")) {
        cfg::targets_highlight_precision_rifle = safe_stoi(val) != 0;
      } else if (key == xorstr_("col_targets_highlight_precision_rifle")) {
        cfg::col_targets_highlight_precision_rifle = parse_col(val);
      } else if (key == xorstr_("targets_radar_enable")) {
        cfg::targets_radar_enable = safe_stoi(val) != 0;
      } else if (key == xorstr_("targets_radar_size")) {
        cfg::targets_radar_size = safe_stof(val);
      } else if (key == xorstr_("targets_radar_opacity")) {
        cfg::targets_radar_opacity = safe_stof(val);
      } else if (key == xorstr_("targets_radar_range")) {
        cfg::targets_radar_range = safe_stof(val);
      }
      // aimbot configs 0-6
      else if (key.rfind(xorstr_("aimbot_"), 0) == 0) {
        int idx = safe_stoi(key.substr(7, 1));
        if (idx >= 0 && idx < 7) {
          std::string sub = key.substr(9); // after "aimbot_N_"
          if (sub == xorstr_("aimbot"))
            cfg::aimbot_configs[idx].aimbot = safe_stoi(val) != 0;
          else if (sub == xorstr_("aimtype"))
            cfg::aimbot_configs[idx].aimtype = safe_stoi(val);
          else if (sub == xorstr_("fov"))
            cfg::aimbot_configs[idx].fov = safe_stoi(val);
          else if (sub == xorstr_("aim_distance"))
            cfg::aimbot_configs[idx].aim_distance = safe_stoi(val);
          else if (sub == xorstr_("aim_chance"))
            cfg::aimbot_configs[idx].aim_chance = safe_stoi(val);
          else if (sub == xorstr_("aimbone"))
            cfg::aimbot_configs[idx].aimbone = safe_stoi(val);
          else if (sub == xorstr_("draw_fov"))
            cfg::aimbot_configs[idx].draw_fov = safe_stoi(val) != 0;
          else if (sub == xorstr_("show_fov_only_aiming"))
            cfg::aimbot_configs[idx].show_fov_only_aiming = safe_stoi(val) != 0;
          else if (sub == xorstr_("smooth"))
            cfg::aimbot_configs[idx].smooth = safe_stoi(val) != 0;
          else if (sub == xorstr_("smooth_type"))
            cfg::aimbot_configs[idx].smooth_type = safe_stoi(val);
          else if (sub == xorstr_("smooth_factor"))
            cfg::aimbot_configs[idx].smooth_factor = safe_stof(val);
          else if (sub == xorstr_("ignore_friends"))
            cfg::aimbot_configs[idx].ignore_friends = safe_stoi(val) != 0;
          else if (sub == xorstr_("ignore_admins"))
            cfg::aimbot_configs[idx].ignore_admins = safe_stoi(val) != 0;
        }
      }
      // keybinds
      else if (key.rfind(xorstr_("kb_"), 0) == 0) {
        std::string option_name = key.substr(3);
        checkbox_t *cbdata = menu_cfg->fill<checkbox_t>(option_name);
        if (cbdata) {
          cbdata->keybinds_list.clear();
          std::stringstream kb_ss(val);
          std::string kb_item;
          while (std::getline(kb_ss, kb_item, '|')) {
            while (!kb_item.empty() &&
                   (kb_item.back() == ';' || kb_item.back() == '\r' ||
                    kb_item.back() == ' '))
              kb_item.pop_back();
            size_t c1 = kb_item.find(',');
            size_t c2 = (c1 != std::string::npos) ? kb_item.find(',', c1 + 1)
                                                  : std::string::npos;
            if (c1 != std::string::npos && c2 != std::string::npos) {
              keybind_t kb;
              kb.idx = safe_stoi(kb_item.substr(0, c1));
              kb.key = safe_stoi(kb_item.substr(c1 + 1, c2 - c1 - 1));
              kb.mode = safe_stoi(kb_item.substr(c2 + 1));
              if (kb.key != 0) {
                cbdata->keybinds_list.push_back(kb);
                cbdata->last_keybind = kb;
              }
            }
          }
        }
      }
    }
  } catch (...) {
  }
}

static void log_cfg(const std::string &msg) {
  // Logging disabled
}

#include "../security/shared_ipc.hpp"

namespace config_system {

std::string g_token = "";
std::string g_hwid = "";
std::string g_username = "";

void set_creds(const std::string &token, const std::string &hwid,
               const std::string &username) {
  if (!token.empty())
    g_token = token;
  if (!hwid.empty())
    g_hwid = hwid;
  if (!username.empty())
    g_username = username;
}

void init() {
  if (g_token.empty() || g_username.empty() || g_hwid.empty()) {
    ShadowSharedAuth auth = {};
    if (ReadSharedAuth(auth)) {
      g_token = auth.token;
      g_hwid = auth.hwid;
      g_username = auth.username;
      log_cfg("init() loaded from shared memory: user=" + g_username +
              " hwid=" + g_hwid.substr(0, 16));
    }
  }
  if (g_hwid.empty()) {
    g_hwid = HWID_DLL::GenerateLocalHWID();
  }
  log_cfg("init() called, user=" + g_username +
          " hwid=" + g_hwid.substr(0, 16));
  get_configs();
}

static std::vector<std::string> cached_configs;
static std::mutex config_mutex;
static std::atomic<bool> is_fetching_configs(false);

std::vector<std::string> get_configs() {
  if (g_hwid.empty()) {
    g_hwid = HWID_DLL::GenerateLocalHWID();
  }

  if (!is_fetching_configs.exchange(true)) {
    std::thread([]() {
      std::string url = xorstr_("https://shadowcheats.cc/api/configs/list");
      log_cfg("Fetching config list with user=" + g_username +
              " hwid=" + g_hwid.substr(0, 16) + "...");
      std::string response = http::get(url, g_token, g_hwid);
      log_cfg("Fetch response: '" + response + "'");

      if (response != xorstr_("Error") && response != xorstr_("Unauthorized") &&
          response.find(xorstr_("error:")) == std::string::npos &&
          response.find(xorstr_("<html")) == std::string::npos &&
          response.find(xorstr_("<!DOCTYPE")) == std::string::npos) {
        std::vector<std::string> new_configs;
        if (!response.empty()) {
          std::stringstream ss(response);
          std::string item;
          while (std::getline(ss, item, ',')) {
            while (!item.empty() && (item.back() == '\r' ||
                                     item.back() == '\n' || item.back() == ' '))
              item.pop_back();
            while (!item.empty() &&
                   (item.front() == '\r' || item.front() == '\n' ||
                    item.front() == ' '))
              item.erase(item.begin());
            if (!item.empty()) {
              new_configs.push_back(item);
            }
          }
        }
        std::lock_guard<std::mutex> lock(config_mutex);
        cached_configs = std::move(new_configs);
        log_cfg("Cached configs updated, count=" +
                std::to_string(cached_configs.size()));
      } else {
        log_cfg("Fetch did not return new configs list - keeping current "
                "cached configs");
      }
      is_fetching_configs = false;
    }).detach();
  }

  std::vector<std::string> result;
  {
    std::lock_guard<std::mutex> lock(config_mutex);
    result = cached_configs;
  }

  return result;
}

void save(const std::string &name) {
  if (name.empty()) {
    return;
  }
  if (g_hwid.empty()) {
    g_hwid = HWID_DLL::GenerateLocalHWID();
  }

  log_cfg("save('" + name + "') starting, user=" + g_username +
          " hwid=" + g_hwid.substr(0, 16) + "...");

  // Сразу добавляем в локальный кэш для мгновенного отображения в UI
  {
    std::lock_guard<std::mutex> lock(config_mutex);
    bool found = false;
    for (const auto &c : cached_configs) {
      if (c == name) {
        found = true;
        break;
      }
    }
    if (!found)
      cached_configs.insert(cached_configs.begin(), name);
  }

  std::string data = serialize();
  std::string escaped_data;
  escaped_data.reserve(data.size() * 2);
  for (char c : data) {
    if (c == '"')
      escaped_data += "\\\"";
    else if (c == '\\')
      escaped_data += "\\\\";
    else if (c == '\n')
      escaped_data += "\\n";
    else if (c == '\r')
      escaped_data += "\\r";
    else if (c == '\t')
      escaped_data += "\\t";
    else
      escaped_data += c;
  }

  std::string json_payload =
      xorstr_("{\"name\":\"") + name + xorstr_("\",\"data\":\"") +
      escaped_data + xorstr_("\",\"hwid\":\"") + g_hwid +
      xorstr_("\",\"username\":\"") + g_username + xorstr_("\",\"token\":\"") +
      g_token + xorstr_("\"}");
  std::string url = xorstr_("https://shadowcheats.cc/api/configs/save");
  std::thread([url, json_payload, name]() {
    std::string res = http::post(url, json_payload, g_token, g_hwid);
    log_cfg("save('" + name + "') POST response: '" + res + "'");
    is_fetching_configs = false;
    if (res == xorstr_("OK")) {
      get_configs();
    }
  }).detach();
}

void load(const std::string &name) {
  try {
    if (name.empty()) {
      return;
    }
    if (g_hwid.empty()) {
      g_hwid = HWID_DLL::GenerateLocalHWID();
    }

    log_cfg("load('" + name + "') starting, user=" + g_username +
            " hwid=" + g_hwid.substr(0, 16) + "...");
    std::string json_payload =
        xorstr_("{\"name\":\"") + name + xorstr_("\",\"hwid\":\"") + g_hwid +
        xorstr_("\",\"username\":\"") + g_username +
        xorstr_("\",\"token\":\"") + g_token + xorstr_("\"}");
    std::string url = xorstr_("https://shadowcheats.cc/api/configs/load");
    std::string response = http::post(url, json_payload, g_token, g_hwid);
    log_cfg("load('" + name +
            "') response length: " + std::to_string(response.size()));

    size_t data_pos = response.find(xorstr_("\"data\":"));
    if (data_pos != std::string::npos) {
      data_pos = response.find('"', data_pos + 7);
      if (data_pos != std::string::npos) {
        data_pos++;
        size_t end_pos = response.rfind('"');
        if (end_pos != std::string::npos && end_pos > data_pos) {
          std::string data = response.substr(data_pos, end_pos - data_pos);
          std::string unescaped;
          unescaped.reserve(data.size());
          for (size_t i = 0; i < data.length(); ++i) {
            if (data[i] == '\\' && i + 1 < data.length()) {
              if (data[i + 1] == '"') {
                unescaped += '"';
                i++;
              } else if (data[i + 1] == '\\') {
                unescaped += '\\';
                i++;
              } else if (data[i + 1] == 'n') {
                unescaped += '\n';
                i++;
              } else if (data[i + 1] == 'r') {
                unescaped += '\r';
                i++;
              } else if (data[i + 1] == 't') {
                unescaped += '\t';
                i++;
              } else {
                unescaped += '\\';
                unescaped += data[i + 1];
                i++;
              }
            } else {
              unescaped += data[i];
            }
          }
          deserialize(unescaped);
          log_cfg("load('" + name + "') deserialize completed successfully");
        }
      }
    }
  } catch (...) {
    log_cfg("load('" + name + "') exception caught");
  }
}

void remove(const std::string &name) {
  if (name.empty()) {
    return;
  }
  if (g_hwid.empty()) {
    g_hwid = HWID_DLL::GenerateLocalHWID();
  }

  log_cfg("remove('" + name + "') called");
  // Сразу убираем из локального кэша для мгновенного обновления в UI
  {
    std::lock_guard<std::mutex> lock(config_mutex);
    cached_configs.erase(
        std::remove(cached_configs.begin(), cached_configs.end(), name),
        cached_configs.end());
  }

  std::string json_payload =
      xorstr_("{\"name\":\"") + name + xorstr_("\",\"hwid\":\"") + g_hwid +
      xorstr_("\",\"username\":\"") + g_username + xorstr_("\",\"token\":\"") +
      g_token + xorstr_("\"}");
  std::string url = xorstr_("https://shadowcheats.cc/api/configs/delete");
  std::thread([url, json_payload, name]() {
    std::string res = http::post(url, json_payload, g_token, g_hwid);
    log_cfg("delete('" + name + "') response: '" + res + "'");
    is_fetching_configs = false;
    if (res == xorstr_("OK")) {
      get_configs();
    }
  }).detach();
}
} // namespace config_system
