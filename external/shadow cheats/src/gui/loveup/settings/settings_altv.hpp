#pragma once
#include "security/xorstr.hpp"
#include <imgui.h>
#include <vector>
#include <string>
#include <unordered_set>

namespace cfg
{
    
    inline int   EntityUpdateDelayMs = 45;
    inline int   MaxEntities = 256;
    inline int   ToggleMenuKey = 0x2D;
    inline bool  Misc_Debug = false;
    inline int   suicide_key = 0;
    inline int   tp_waypoint_key = 0;
    inline int   cheat_language = 0; 
    inline bool  anti_afk = false;
    
    inline bool  weapon_no_recoil = false;
    inline bool  weapon_no_spread = false;

    
    struct WeaponAimbotConfig {
        bool  aimbot = false;
        int   aimtype = 1; 
        int   fov = 0;
        int   aim_distance = 500;
        int   aim_chance = 100;
        int   aimbone = 0; 

        bool  draw_fov = false;
        bool  show_fov_only_aiming = true;
        ImColor col_Fov = ImColor(1.f, 1.f, 1.f, 1.f);
        bool  smooth = false;
        int   smooth_type = 0; 
        float smooth_factor = 1.0f;
        bool  ignore_friends = true;
    };
    inline WeaponAimbotConfig aimbot_configs[7]; 
    inline bool enable_weapon_configs = false;
    inline int active_weapon_category = 0; 
    inline int current_weapon_category = 0; 
    inline int aimkey = 0x0;
    inline int aimkey_mode = 1; // 0 = Toggle, 1 = Hold

    inline bool triggerbot = false;
    inline int triggerkey = 0x0;
    inline int triggerkey_mode = 1; // 0 = Toggle, 1 = Hold
    inline int   triggerdelay = 0;
    inline bool trigger_ignore_friends = true;

    
    inline float ESP_MaxDistance = 1000.f;
    inline bool  ESP_PlayerOnly = false;

    
    inline bool  esp_show_friends = true;
    inline bool  esp_show_admins = true;


    
    inline bool  ESP_AimLine = false;
    inline ImColor col_AimLine = ImColor(1.f, 1.f, 1.f, 1.f);

    
    inline bool  ESP_Box = false;
    inline float esp_box_thickness = 1.5f;
    inline float esp_box_rounding = 0.0f;
    inline float esp_skeleton_thickness = 1.0f;
    inline bool  ESP_Line = false;
    inline bool  ESP_HealthBar = false;
    inline bool  ESP_Distance = false;
    inline bool  ESP_Weapon = false;
    inline bool  ESP_Skeleton = false;
    inline bool  ESP_SkeletonLines = false;
    inline bool esp_players_name = false;

    inline int   esp_players_box_type = 2; 
    inline int   esp_players_bar_position = 1; 
    inline int   esp_players_armor_bar_position = 1; 
    inline ImColor col_players_box = ImColor(255, 255, 255, 255);
    inline ImColor col_players_skeleton = ImColor(255, 255, 255, 255);
    inline ImColor col_players_line = ImColor(255, 255, 255, 255);
    inline ImColor col_players_distance = ImColor(255, 255, 255, 255);
    inline ImColor col_players_weapon = ImColor(255, 255, 255, 255);

    inline bool esp_custom_health_colors = false;
    inline ImColor col_health = ImColor(0.f, 1.f, 0.f, 1.f);
    inline ImColor col_armor = ImColor(0.2f, 0.6f, 1.f, 1.f);

    inline bool  esp_objects_enabled = false;
    inline float esp_objects_max_distance = 500.f;
    inline ImColor col_objects = ImColor(255, 255, 255, 255);

    inline bool  ESP_Friends_Box = false;
    inline int   esp_friends_box_type = 2;
    inline ImColor col_friends_box = ImColor(0, 255, 0, 255);
    inline bool  ESP_Friends_HealthBar = false;
    inline int   esp_friends_bar_position = 1;
    inline int   esp_friends_armor_bar_position = 1;
    inline bool  ESP_Friends_Distance = false;
    inline ImColor col_friends_distance = ImColor(0, 255, 0, 255);
    inline bool  ESP_Friends_Weapon = false;
    inline ImColor col_friends_weapon = ImColor(0, 255, 0, 255);
    inline bool  ESP_Friends_SkeletonLines = false;
    inline ImColor col_friends_skeleton = ImColor(0, 255, 0, 255);
    inline bool  ESP_Friends_Line = false;
    inline ImColor col_friends_line = ImColor(0, 255, 0, 255);

    inline int friend_radius = 5;
    inline int friend_max_dist = 30;
    inline std::vector<uintptr_t> friend_list;
    inline std::vector<std::string> friend_names;
    
    // Admins
    inline bool  ESP_Admins_Box = false;
    inline int   esp_admins_box_type = 2;
    inline ImColor col_admins_box = ImColor(255, 0, 0, 255);
    inline bool  ESP_Admins_HealthBar = false;
    inline int   esp_admins_bar_position = 1;
    inline int   esp_admins_armor_bar_position = 1;
    inline bool  ESP_Admins_Distance = false;
    inline ImColor col_admins_distance = ImColor(255, 0, 0, 255);
    inline bool  ESP_Admins_Weapon = false;
    inline ImColor col_admins_weapon = ImColor(255, 0, 0, 255);
    inline bool  ESP_Admins_SkeletonLines = false;
    inline ImColor col_admins_skeleton = ImColor(255, 0, 0, 255);
    inline bool  ESP_Admins_Line = false;
    inline ImColor col_admins_line = ImColor(255, 0, 0, 255);
    inline float esp_admins_box_thickness = 1.5f;
    inline float esp_admins_box_rounding = 0.0f;
    inline float esp_admins_skeleton_thickness = 1.0f;
    inline bool esp_admins_custom_health_colors = false;
    inline ImColor col_admins_health = ImColor(0.f, 1.f, 0.f, 1.f);
    inline ImColor col_admins_armor = ImColor(0.2f, 0.6f, 1.f, 1.f);
    inline ImColor col_admins_around = ImColor(255, 255, 255, 255);
    inline ImColor col_players_around = ImColor(255, 255, 255, 255);
    
    inline bool show_admins_around = false;
    inline bool show_players_around = false;
    
    inline ImVec2 admins_around_pos = ImVec2(100.f, 100.f);
    inline ImVec2 players_around_pos = ImVec2(100.f, 130.f);
    inline bool  bypass_obs = false;

    
    inline std::string current_config_name = xorstr_("default");
    
    struct config_state {
        std::vector<std::string> list_container{ xorstr_("default") };
        int selected_index = 0;
        char config_name[64] = { "" };
    };
    inline config_state config;
}
