#pragma once
#include "security/xorstr.hpp"
#include "../../core/globals.hpp"
#include "../../config/settings.hpp"
#include "../../memory/memory_access.hpp"
#include <windows.h>
#include "../../security/vmp_markers.hpp"

namespace features
{
    inline void TeleportToWaypoint()
    {
        if (offsets::BlipList == 0)
        {
            return;
        }

        if (!g.localplayer || !g.localplayer->IsValid())
        {
            return;
        }

        uintptr_t blipList = g.ImageBase + offsets::BlipList;
        if (!blipList) return;

        bool found = false;
        Vector3 waypointCoords;

        for (int i = 0; i < 1500; i++)
        {
            uintptr_t blip = read<uintptr_t>(blipList + (i * 8));
            if (!blip) continue;

            int icon = read<int>(blip + 0x40);
            int color = read<int>(blip + 0x48);

            if (icon == 8) // 8 is usually Waypoint
            {
                // Coordinates are usually at +0x10
                waypointCoords.x = read<float>(blip + 0x10);
                waypointCoords.y = read<float>(blip + 0x14);
                waypointCoords.z = read<float>(blip + 0x18);
                
                // Waypoint usually lacks a proper Z, so we add height so we don't fall under map
                if (waypointCoords.z == 0.0f || waypointCoords.z == 20.0f)
                {
                    waypointCoords.z = g.localplayer->m_pVecPosition.z + 2.0f; // 2 meters above current Z
                }
                
                found = true;
                break;
            }
        }

        if (found)
        {
            // Teleport the player
            // You can also write to Navigation ptr +0x50, but we will try Position first
            
            uintptr_t nav = read<uintptr_t>(g.localplayer->ptr + 0x30);
            if (nav)
            {
                write<Vector3>(nav + 0x50, waypointCoords);
            }
            
            g.localplayer->SetPosition(waypointCoords);
            
            // Teleport vehicle if inside one (0xD28/0xD30 is usually the vehicle ptr, try 0xD30)
            uintptr_t veh = read<uintptr_t>(g.localplayer->ptr + 0xD30);
            if (!veh) veh = read<uintptr_t>(g.localplayer->ptr + 0xD28); // fallback
            
            if (veh)
            {
                write<Vector3>(veh + 0x90, waypointCoords);
                uintptr_t veh_nav = read<uintptr_t>(veh + 0x30);
                if (veh_nav) {
                    write<Vector3>(veh_nav + 0x50, waypointCoords);
                }
            }
            
            // To prevent falling under ground, often vehicles or peds need visual update
            // For now just basic write.
        }
    }
}



