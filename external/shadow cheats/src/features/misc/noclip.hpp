#pragma once
#include "../../config/settings.hpp"
#include "../../core/globals.hpp"
#include "../../memory/memory_access.hpp"
#include "../../render/overlay.hpp"
#include "../../sdk/offsets.hpp"
#include "../../security/vmp_markers.hpp"
#include "security/xorstr.hpp"
#include <cmath>
#include <windows.h>


namespace features {
// ─── NoClip ──────────────────────────────────────────────────────────────
// Механика: каждый тик читаем направление камеры из view-матрицы,
// двигаем позицию игрока по клавишам W/A/S/D/E/Q (без коллизий).

inline void NoclipLoop() {
  bool was_noclip_active = false;
  bool was_collision_disabled = false;
  Vector3 noclip_pos(0, 0, 0);

  while (g.Run) {
    if (!cfg::noclip_enabled || !g.localplayer || !g.localplayer->IsValid()) {
      if (was_noclip_active && g.localplayer && g.localplayer->IsValid()) {
        // Восстанавливаем физику и коллизию (если не включена отдельная опция
        // отключения коллизии)
        g.localplayer->SetRagdoll(false);
        g.localplayer->SetCollision(cfg::disable_collision ? -1.0f : 0.25f);
        was_noclip_active = false;
        noclip_pos =
            Vector3(0, 0, 0); // Сбрасываем позицию для следующего включения
      }

      // Независимая опция отключения коллизии (работает когда ноуклип выключен)
      if (!was_noclip_active && g.localplayer && g.localplayer->IsValid()) {
        if (cfg::disable_collision && !was_collision_disabled) {
          g.localplayer->SetCollision(-1.0f);
          was_collision_disabled = true;
        } else if (!cfg::disable_collision && was_collision_disabled) {
          g.localplayer->SetCollision(0.25f);
          was_collision_disabled = false;
        }
      }

      Sleep(50);
      continue;
    }

    was_noclip_active = true;
    // При включенном ноуклипе коллизия отключается в любом случае,
    // поэтому сбрасываем флаг, чтобы при выключении ноуклипа коллизия
    // восстановилась правильно
    was_collision_disabled = false;

    // Включаем T-позу/Ragdoll, чтобы полностью отключить обработку ходьбы
    // движком игры
    g.localplayer->SetRagdoll(false);
    g.localplayer->SetCollision(-1.0f);

    // ── 1. Читаем View-матрицу из камеры ─────────────────────────────
    // ── 1. Читаем направление камеры через Viewport / Camera ─────────────
    Matrix viewMat = read<Matrix>(g.Viewport + offsets::viewport::ViewMatrix);

    // В глобальной матрице View (от World к View) вектора лежат по столбцам:
    // Right: _11, _21, _31
    // Up: _12, _22, _32
    // Forward: _13, _23, _33 (инвертированный, так как камера смотрит в -Z в
    // пространстве вида)

    float fx = viewMat._12;
    float fy = viewMat._22;
    float fz = viewMat._32;

    float rx = viewMat._11;
    float ry = viewMat._21;
    float rz = viewMat._31;

    // Нормализуем вектора
    float fLen = sqrt(fx * fx + fy * fy + fz * fz);
    if (fLen > 0.0001f) {
      fx /= fLen;
      fy /= fLen;
      fz /= fLen;
    }

    float rLen = sqrt(rx * rx + ry * ry + rz * rz);
    if (rLen > 0.0001f) {
      rx /= rLen;
      ry /= rLen;
      rz /= rLen;
    }

    // ── 3. Скорость
    float speed = cfg::noclip_speed;

    // Если только включили ноуклип — берем текущую позицию из памяти
    if (noclip_pos.x == 0 && noclip_pos.y == 0 && noclip_pos.z == 0) {
      noclip_pos = read<Vector3>(g.localplayer->ptr + offsets::ped::Position);
    }

    // ── 4. Клавиши движения ───────────────────────────────────────────
    int key_fw = overlay::menu_open ? 'W' : cfg::noclip_key_forward;
    int key_bw = overlay::menu_open ? 'S' : cfg::noclip_key_back;
    int key_lt = overlay::menu_open ? 'A' : cfg::noclip_key_left;
    int key_rt = overlay::menu_open ? 'D' : cfg::noclip_key_right;
    int key_up = overlay::menu_open ? 'E' : cfg::noclip_key_up;
    int key_dn = overlay::menu_open ? 'Q' : cfg::noclip_key_down;

    // Вперёд по камере
    if (GetAsyncKeyState(key_fw) & 0x8000) {
      noclip_pos.x += speed * fx;
      noclip_pos.y += speed * fy;
      noclip_pos.z += speed * fz;
    }
    // Назад
    if (GetAsyncKeyState(key_bw) & 0x8000) {
      noclip_pos.x -= speed * fx;
      noclip_pos.y -= speed * fy;
      noclip_pos.z -= speed * fz;
    }
    // Стрейф влево
    if (GetAsyncKeyState(key_lt) & 0x8000) {
      noclip_pos.x -= speed * rx;
      noclip_pos.y -= speed * ry;
      noclip_pos.z -= speed * rz;
    }
    // Стрейф вправо
    if (GetAsyncKeyState(key_rt) & 0x8000) {
      noclip_pos.x += speed * rx;
      noclip_pos.y += speed * ry;
      noclip_pos.z += speed * rz;
    }
    // Вверх
    if (GetAsyncKeyState(key_up) & 0x8000) {
      noclip_pos.z += speed;
    }
    // Вниз
    if (GetAsyncKeyState(key_dn) & 0x8000) {
      noclip_pos.z -= speed;
    }

    // ── 5. Всегда перезаписываем позицию и обнуляем Velocity ──────────
    write<Vector3>(g.localplayer->ptr + offsets::ped::Position, noclip_pos);

    // Также пишем в navigation structure (обязательно для камеры)
    uintptr_t nav = read<uintptr_t>(g.localplayer->ptr + 0x30);
    if (nav)
      write<Vector3>(nav + 0x50, noclip_pos);

    // Обнуляем скорость (Velocity), чтобы гравитация не тянула вниз (0x320 -
    // Velocity в CObject)
    write<Vector3>(g.localplayer->ptr + 0x320, Vector3(0.0f, 0.0f, 0.0f));

    Sleep(10);
  }
}
}



