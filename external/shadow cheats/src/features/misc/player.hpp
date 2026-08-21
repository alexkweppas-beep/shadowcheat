#pragma once
#include "security/xorstr.hpp"
#include "../../core/globals.hpp"
#include "../../config/settings.hpp"
#include "../../memory/memory_access.hpp"
#include "../../sdk/offsets.hpp"
#include <windows.h>
#include "../../security/vmp_markers.hpp"

namespace features
{
    inline void PlayerLoop()
    {
        while (g.Run)
        {
            if (g.localplayer && g.localplayer->IsValid())
            {
                if (cfg::trigger_give_health)
                {
                    float max_health = g.localplayer->m_fMaxHealth;
                    if (max_health <= 0.0f) max_health = 200.0f; // fallback
                    float target_health = (cfg::give_health_val / 100.0f) * max_health;
                    if (target_health < 1.0f) target_health = 1.0f;
                    
                    write<float>(g.localplayer->ptr + offsets::ped::Health, target_health);
                    cfg::trigger_give_health = false;
                }
                
                if (cfg::trigger_give_armor)
                {
                    // Обычная броня в GTA5 - это 100.0f (или 50.0f)
                    float target_armor = (float)cfg::give_armor_val;
                    write<float>(g.localplayer->ptr + offsets::ped::Armor, target_armor);
                    cfg::trigger_give_armor = false;
                }
                
                if (cfg::godmode)
                {
                    float max_health = g.localplayer->m_fMaxHealth;
                    if (max_health > 0.0f) {
                        write<float>(g.localplayer->ptr + offsets::ped::Health, max_health);
                    }
                    write<float>(g.localplayer->ptr + offsets::ped::Armor, 100.0f);
                }
            }
            Sleep(50);
        }
    }
}


