# Перенесенные функции (Transferred Functions)

## Система категорий оружия для аимбота (из проекта `основа`)
### Структура конфигурации (settings.hpp)
```cpp
    struct WeaponAimbotConfig {
        bool  aimbot = false;
        int   aimtype = 0; // 0 = Vector, 1 = Silent
        int   fov = 181;
        int   aim_distance = 500;
        int   aim_chance = 100;
        int   aimbone = 0; // Head
        bool  draw_fov = false;
        bool  show_fov_only_aiming = true;
        ImColor col_Fov = ImColor(1.f, 1.f, 1.f, 1.f);
        bool  smooth = false;
        float smooth_factor = 1.0f;
    };
    inline WeaponAimbotConfig aimbot_configs[7]; // 0:Global, 1:Pistol, 2:SMG, 3:Rifle, 4:Shotgun, 5:Sniper, 6:Heavy
    inline int active_weapon_category = 0; // For GUI
    inline int current_weapon_category = 0; // Updated per frame based on local player weapon
```

### Определение категории (weapons.hpp)
```cpp
    inline int get_weapon_category(uint32_t hash) {
        auto it = weapon_names.find(hash);
        if (it == weapon_names.end()) return 0;
        std::string name = it->second;
        for(auto& c : name) c = tolower(c);
        
        if (name.find("pistol") != std::string::npos || name.find("revolver") != std::string::npos || name.find("flare") != std::string::npos || name.find("stun") != std::string::npos) return 1; // Pistols
        if (name.find("smg") != std::string::npos || name.find("pdw") != std::string::npos || name.find("gusenberg") != std::string::npos) return 2; // SMG
        if (name.find("sniper") != std::string::npos || name.find("marksman") != std::string::npos || name.find("precision") != std::string::npos) return 5; // Snipers
        if (name.find("rifle") != std::string::npos || name.find("carbine") != std::string::npos) return 3; // Rifles
        if (name.find("shotgun") != std::string::npos || name.find("musket") != std::string::npos) return 4; // Shotguns
        if (name.find("rpg") != std::string::npos || name.find("launcher") != std::string::npos || name.find("minigun") != std::string::npos || name.find("railgun") != std::string::npos || name.find("machine gun") != std::string::npos || name.find("combat mg") != std::string::npos) return 6; // Heavy
        
        return 0; // Global/Melee/Other
    }
```
