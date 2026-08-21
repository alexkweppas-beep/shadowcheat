#include "../core/entity_list.hpp"
#include "../config/settings.hpp"
#include "../core/globals.hpp"
#include "../features/esp/objects.hpp"
#include "../memory/memory_access.hpp"
#include "../render/debug_overlay.hpp"
#include "../sdk/CPed.hpp"
#include "../sdk/offsets.hpp"
#include "security/xorstr.hpp"
#include <algorithm>
#include <chrono>


// ─── Лимиты сканирования объектов ────────────────────────────────────────────
static constexpr int MAX_SCAN_PICKUPS = 512;
static constexpr int MAX_SCAN_OBJECTS = 512;

// Буферы для bulk-чтения указателей (выделяются один раз)
static uint64_t s_pickup_ptrs[MAX_SCAN_PICKUPS];
static uint64_t s_object_ptrs[MAX_SCAN_OBJECTS];

// ─── Множество интересующих хэшей оружия ──────────────────────────────
static constexpr uint32_t WEAPON_HASHES[] = {
    0xB7E2DDAF, 0x83BF0278, 0x291381DC, // Carbine Rifle
    0x97F39713, 0xC0A3098D, 0xA8B3F2A7, // Special Carbine
    0x1053C3FD, 0xBFEFFF6D,             // Assault Rifle
    0xD42D39D,  0x1D073A89,             // Pump Shotgun
    0x5311D716,                         // Revolver
    0xA2C5D73C,                         // 11.43mm
    0x8BF1A0D6,                         // 7.62mm
    0x9D2BF7C5,                         // 12mm
};
static constexpr int WEAPON_HASHES_COUNT =
    (int)(sizeof(WEAPON_HASHES) / sizeof(WEAPON_HASHES[0]));

static inline bool is_weapon_hash(uint32_t hash) {
  if (hash == 0 || hash == 0xFFFFFFFF)
    return false;
  for (int i = 0; i < WEAPON_HASHES_COUNT; ++i)
    if (WEAPON_HASHES[i] == hash)
      return true;
  return false;
}

// Структура для чтения позиции (фаза 2 — только для совпавших хэшей)
#pragma pack(push, 1)
struct RawObjNav {
  uint8_t _pad[0x30];  // 0x00..0x2F
  uint64_t nav;        // 0x30 — навигационная структура
  uint8_t _pad2[0x58]; // 0x38..0x8F
  float pos_x;         // 0x90
  float pos_y;         // 0x94
  float pos_z;         // 0x98
};
#pragma pack(pop)

namespace features {
void UpdateEntityListLoop() {
  auto windowStart = std::chrono::steady_clock::now();
  unsigned int iterCount = 0;

  while (g.Run) {
    std::vector<CPed> list;
    std::vector<Object> obj_list;

    g.GWorld = read<uintptr_t>(g.ImageBase + offsets::GameWorld);
    g.Viewport = read<uintptr_t>(g.ImageBase + offsets::ViewPort);
    g.Replayinterface = read<uintptr_t>(g.ImageBase + offsets::ReplayInterface);

    {
      std::lock_guard<std::mutex> lock(g.EntityListMutex);
      if (!g.localplayer)
        g.localplayer = new CPed();
      g.localplayer->ptr = (g.GWorld ? read<uintptr_t>(g.GWorld + 0x8) : 0);
    }
    // Update outside mutex: memory reads don't block the render thread
    if (g.localplayer && g.localplayer->ptr) {
      g.localplayer->Update();
    }

    const uintptr_t listPtr =
        read<uintptr_t>(g.Replayinterface + offsets::EntityListPtr);
    const uintptr_t entities =
        read<uintptr_t>(listPtr + offsets::EntityListBase);
    const int count = std::clamp(256, 0, cfg::MaxEntities);

    for (int i = 0; i < count; ++i) {
      const uintptr_t pEntity = read<uintptr_t>(entities + (i * 0x10));
      if (pEntity == 0 || pEntity == g.localplayer->ptr)
        continue;

      CPed ped{};

      auto it = std::find_if(
          g.EntityList.begin(), g.EntityList.end(),
          [pEntity](const CPed &old_ped) { return old_ped.ptr == pEntity; });
      if (it != g.EntityList.end()) {
        ped = *it;
      }

      if (ped.get_player(pEntity) && ped.Update()) {
        list.push_back(ped);
        continue;
      }
    }

    auto processObject = [&](uintptr_t pItem, uint32_t hash, Vector3 pos) {
      Object obj;
      obj.ptr = pItem;
      obj.position = pos;
      obj.hash = hash;
      obj.attachmentExtension = read<uintptr_t>(pItem + 0xD0);
      if (obj.attachmentExtension == 0) {
        uintptr_t attach2 = read<uintptr_t>(pItem + 0x1A8);
        if (attach2 != 0)
          obj.attachmentExtension = attach2;
      }

      bool found = false;
      std::string w_name;
      if (hash == 0xB7E2DDAF || hash == 0x83BF0278 || hash == 0x291381DC) {
        w_name = xorstr_("Carbine Rifle");
        found = true;
      } else if (hash == 0x97F39713 || hash == 0xC0A3098D ||
                 hash == 0xA8B3F2A7) {
        w_name = xorstr_("Special Rifle");
        found = true;
      } else if (hash == 0x1053C3FD || hash == 0xBFEFFF6D) {
        w_name = xorstr_("Assault Rifle");
        found = true;
      } else if (hash == 0xD42D39D || hash == 0x1D073A89) {
        w_name = xorstr_("Pump Shotgun");
        found = true;
      } else if (hash == 0x5311D716) {
        w_name = xorstr_("Revolver");
        found = true;
      } else if (hash == 0xA2C5D73C) {
        w_name = xorstr_("11.43mm");
        found = true;
      } else if (hash == 0x8BF1A0D6) {
        w_name = xorstr_("7.62mm");
        found = true;
      } else if (hash == 0x9D2BF7C5) {
        w_name = xorstr_("12mm");
        found = true;
      }

      if (!found)
        return;

      strcpy_s(obj.name, sizeof(obj.name), w_name.c_str());
      obj.should_draw = true;
      if (g.localplayer)
        obj.dist_to_local =
            Vector3::Distance(pos, g.localplayer->m_pVecPosition);

      if (obj.attachmentExtension != 0) {
        obj.should_draw = false;
      } else {
        if (w_name == xorstr_("Carbine Rifle") ||
            w_name == xorstr_("Special Rifle") ||
            w_name == xorstr_("Assault Rifle")) {
          if (obj.dist_to_local > 100.f) {
            obj.should_draw = false;
          } else {
            bool held = false;

            // Check against localplayer
            if (g.localplayer && g.localplayer->ptr) {
              float dx = pos.x - g.localplayer->m_pVecPosition.x;
              float dy = pos.y - g.localplayer->m_pVecPosition.y;
              float dz = pos.z - g.localplayer->m_pVecPosition.z;
              if ((dx * dx + dy * dy + dz * dz) < 9.0f) {
                held = true;
              }
            }

            // Check against other players
            if (!held) {
              for (const auto &p : list) {
                if (!p.ptr)
                  continue;
                float dx = pos.x - p.m_pVecPosition.x;
                float dy = pos.y - p.m_pVecPosition.y;
                float dz = pos.z - p.m_pVecPosition.z;
                if ((dx * dx + dy * dy + dz * dz) < 9.0f) {
                  held = true;
                  break;
                }
              }
            }

            if (held)
              obj.should_draw = false;
          }
        }
      }

      if (obj.should_draw) {
        obj_list.push_back(obj);
      }
    };

    // ─── Pickups: 2-фазная фильтрация по хэшам
    // ──────────────────────────────── Фаза 1: modelInfo ptr (8 б) + hash (4 б)
    // — если не оружие, skip Фаза 2: nav + pos — только для совпавших хэшей
    if (cfg::esp_objects_enabled) {
      uintptr_t pickupInterface = read<uintptr_t>(g.Replayinterface + 0x20);
      if (pickupInterface) {
        uintptr_t pickupList = read<uintptr_t>(pickupInterface + 0x100);
        // cfg::MaxWeapons — настраивается в меню, ограничен буфером
        // MAX_SCAN_PICKUPS
        int pickupCount = read<int>(pickupInterface + 0x110);
        pickupCount = std::clamp(pickupCount, 0,
                                 std::min(cfg::MaxWeapons, MAX_SCAN_PICKUPS));

        int n_pickups =
            read_ptr_stride_array(pickupList, (size_t)pickupCount, 0x10,
                                  s_pickup_ptrs, MAX_SCAN_PICKUPS);

        for (int i = 0; i < n_pickups; ++i) {
          const uintptr_t pPickup = s_pickup_ptrs[i];

          // Фаза 1: читаем только modelInfo ptr (8 байт), проверяем хэш
          const uint64_t modelInfo = read<uint64_t>(pPickup + 0x20);
          if (!modelInfo)
            continue;

          const uint32_t hash = read<uint32_t>(modelInfo + 0x18);
          if (!is_weapon_hash(hash))
            continue; // ← ранний выход для не-оружий

          // Фаза 2: хэш совпал — читаем nav + pos за один вызов
          RawObjNav nav_pos{};
          if (!read_array<RawObjNav>(pPickup, &nav_pos, 1))
            continue;

          Vector3 pos(nav_pos.pos_x, nav_pos.pos_y, nav_pos.pos_z);
          if (nav_pos.nav) {
            Vector3 navPos = read<Vector3>(nav_pos.nav + 0x50);
            if (navPos != Vector3(0, 0, 0))
              pos = navPos;
          }
          if (pos == Vector3(0, 0, 0))
            continue;

          processObject(pPickup, hash, pos);
        }
      }
    }

    // ─── Objects: аналогично Pickups, 2-фазная фильтрация ──────────────────
    if (cfg::esp_objects_enabled) {
      uintptr_t objectInterface = read<uintptr_t>(g.Replayinterface + 0x28);
      if (objectInterface) {
        uintptr_t objectList = read<uintptr_t>(objectInterface + 0x158);
        // cfg::MaxPickups — настраивается в меню, ограничен буфером
        // MAX_SCAN_OBJECTS
        int objectCount = read<int>(objectInterface + 0x168);
        objectCount = std::clamp(objectCount, 0,
                                 std::min(cfg::MaxPickups, MAX_SCAN_OBJECTS));

        int n_objects =
            read_ptr_stride_array(objectList, (size_t)objectCount, 0x10,
                                  s_object_ptrs, MAX_SCAN_OBJECTS);

        for (int i = 0; i < n_objects; ++i) {
          const uintptr_t pObject = s_object_ptrs[i];

          // Фаза 1
          const uint64_t modelInfo = read<uint64_t>(pObject + 0x20);
          if (!modelInfo)
            continue;

          const uint32_t hash = read<uint32_t>(modelInfo + 0x18);
          if (!is_weapon_hash(hash))
            continue;

          // Фаза 2
          RawObjNav nav_pos{};
          if (!read_array<RawObjNav>(pObject, &nav_pos, 1))
            continue;

          Vector3 pos(nav_pos.pos_x, nav_pos.pos_y, nav_pos.pos_z);
          if (nav_pos.nav) {
            Vector3 navPos = read<Vector3>(nav_pos.nav + 0x50);
            if (navPos != Vector3(0, 0, 0))
              pos = navPos;
          }
          if (pos == Vector3(0, 0, 0))
            continue;

          processObject(pObject, hash, pos);
        }
      }
    }

    {
      std::lock_guard<std::mutex> lock(g.EntityListMutex);
      g.EntityList = list;
      g.ObjectList = obj_list;
    }

    ++iterCount;
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - windowStart)
            .count();
    if (elapsed >= 1000) {
      g_EntityLoopHz.store(
          static_cast<unsigned int>(iterCount * 1000 / elapsed),
          std::memory_order_relaxed);
      iterCount = 0;
      windowStart = now;
    }

    Sleep(cfg::EntityUpdateDelayMs);
  }
}
} // namespace features
