#include "CPed.hpp"
#include "../config/settings.hpp"
#include "../core/globals.hpp"
#include "../memory/memory_access.hpp"
#include "bones.hpp"
#include <chrono>
#include <unordered_map>

    static std::unordered_map<uintptr_t, float>
        original_recoils;
static std::unordered_map<uintptr_t, float> original_spreads;
static std::unordered_map<uintptr_t, float> original_times;

static uint64_t GetTimeMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

bool CPed::get_player(uintptr_t base) {
  ptr = base;
  return ptr != 0;
}

uintptr_t CPed::get_weaponmanager() {
  return read<uintptr_t>(ptr + offsets::ped::WeaponManager);
}
uintptr_t CPed::get_weaponinfo() {
  return read<uintptr_t>(weaponmanager + offsets::weapon::WeaponInfo);
}
uintptr_t CPed::get_currentweapon() {
  return read<uintptr_t>(weaponmanager + 0x88);
}
uint32_t CPed::get_weaponhash() {
  return read<uint32_t>(weaponinfo + offsets::weapon::Hash);
}

bool CPed::isPlayer() {
  if (player_info == 0)
    return false;

  uintptr_t model_info = read<uintptr_t>(ptr + 0x20);
  if (!model_info)
    return false;

  uint32_t hash = read<uint32_t>(model_info + 0x18);

  return hash == 0x705E61F2 || hash == 0x9C9EFFD8;
}
bool CPed::isDead() {
  float health = m_fHealth;
  if (health <= 0.1f)
    return true;
  return false;
}

bool CPed::IsVisible() {
  BYTE VisibilityFlag = read<BYTE>(ptr + offsets::ped::VisibleFlag);
  return VisibilityFlag == 8;
}

BYTE CPed::get_alpha() {
  if (!IsValid()) return 0;
  return read<BYTE>(ptr + 0xAC);
}

bool CPed::Update() {
  if (!ptr) {
    // Reset cached fields if ptr is gone
    player_info = 0;
    weaponmanager = 0;
    weaponinfo = 0;
    return false;
  }
  player_info = read<uintptr_t>(ptr + offsets::ped::PlayerInfo);
  weaponmanager = get_weaponmanager();
  if (weaponmanager)
    weaponinfo = get_weaponinfo();

  if (this == g.localplayer) {
    if (cfg::weapon_no_recoil) {
      if (weaponinfo) {
        if (original_recoils.find(weaponinfo) == original_recoils.end()) {
          original_recoils[weaponinfo] =
              read<float>(weaponinfo + offsets::weapon::Recoil);
        }
        write<float>(weaponinfo + offsets::weapon::Recoil, 0.0f);
      }
    } else if (!original_recoils.empty()) {
      for (const auto &pair : original_recoils) {
        write<float>(pair.first + offsets::weapon::Recoil, pair.second);
      }
      original_recoils.clear();
    }

    if (cfg::weapon_no_spread) {
      if (weaponinfo) {
        if (original_spreads.find(weaponinfo) == original_spreads.end()) {
          original_spreads[weaponinfo] =
              read<float>(weaponinfo + offsets::weapon::Spread);
        }
        write<float>(weaponinfo + offsets::weapon::Spread, 0.0f);
      }
    } else if (!original_spreads.empty()) {
      for (const auto &pair : original_spreads) {
        write<float>(pair.first + offsets::weapon::Spread, pair.second);
      }
      original_spreads.clear();
    }

    if (cfg::weapon_double_tap) {
      if (weaponinfo) {
        if (original_times.find(weaponinfo) == original_times.end()) {
          original_times[weaponinfo] =
              read<float>(weaponinfo + offsets::weapon::TimeToShoot);
        }
        write<float>(weaponinfo + offsets::weapon::TimeToShoot, 0.0f); 
      }
    } else if (!original_times.empty()) {
      for (const auto &pair : original_times) {
        write<float>(pair.first + offsets::weapon::TimeToShoot, pair.second);
      }
      original_times.clear();
    }

    if (cfg::weapon_inf_ammo) {
      if (weaponmanager && weaponinfo) {
          uintptr_t ammoInfo = read<uintptr_t>(weaponinfo + offsets::weapon::AmmoInfo);
          if (ammoInfo) {
              uintptr_t ammoCountPtr = read<uintptr_t>(ammoInfo + offsets::weapon::AmmoCount);
              if (ammoCountPtr) {
                  uintptr_t primaryAmmoPtr = read<uintptr_t>(ammoCountPtr + 0x0); // CPrimaryAmmoCount* _PrimaryAmmoCount;
                  if (primaryAmmoPtr) {
                      write<int32_t>(primaryAmmoPtr + offsets::weapon::AmmoValue, 9999); // AmmoCount
                  }
              }
          }
      }
    }

  }

  m_fHealth = read<float>(ptr + offsets::ped::Health);
  m_pVecPosition = read<Vector3>(ptr + offsets::ped::Position);
  m_fMaxHealth = read<float>(ptr + offsets::ped::MaxHealth);

  if (!bones::UpdateFast(this))
    return false;

  if (isDead() && this != g.localplayer)
    return false;

  m_fArmor = read<float>(ptr + offsets::ped::Armor);

  return true;
}

bool CPed::UpdateFast() {
  m_fHealth = read<float>(ptr + offsets::ped::Health);
  m_pVecPosition = read<Vector3>(ptr + offsets::ped::Position);
  m_fMaxHealth = read<float>(ptr + offsets::ped::MaxHealth);

  if (!bones::UpdateFast(this))
    return false;

  if (isDead() && this != g.localplayer)
    return false;

  return true;
}

bool CPed::IsInVehicle() {
  if (!IsValid())
    return false;
  return read<BYTE>(ptr + offsets::ped::IsInVehicle) == 1;
}

uintptr_t CPed::GetVehicle() {
  if (!IsValid())
    return 0;
  return read<uintptr_t>(ptr + offsets::ped::Vehicle);
}

const std::string &CPed::GetName() {
  static std::string empty_str = "";
  return empty_str;
}

BYTE CPed::GetTeam() {
  uint64_t current_time = GetTimeMs();
  if (current_time - last_team_update > 2000) {
    if (current_time - last_name_update > 500) {
      BYTE team = read<BYTE>(ptr + 0x10A9);
      if (team == 0)
        team = read<BYTE>(ptr + 0x10B9);
      cached_team = team;
      last_team_update = current_time;
    }
  }
  return cached_team;
}

Vector3 CPed::GetBonePosition(int index) const {
  if (index >= 0 && index < bones::BONE_COUNT)
    return BoneList[index];
  return Vector3(0, 0, 0);
}

void CPed::SetPosition(Vector3 pos) {
  if (!IsValid())
    return;

  // We need to write to the Position offset in the ped entity
  // First update our local struct
  m_pVecPosition = pos;

  // Then write to game memory
  write<Vector3>(ptr + offsets::ped::Position, pos);

  // Also updating the navigation structure is sometimes needed but we'll try
  // just the base Position first It is usually found at (ptr + 0x30) -> 0x50.
  // But for now just the base position.
}

void CPed::SetRagdoll(bool state) {
  if (!IsValid())
    return;
  // 0x1098 — актуальный оффсет ragdoll флага
  write<BYTE>(ptr + 0x1098, state ? 0x20 : 0x1);
}

void CPed::SetCollision(float v) {
  if (!IsValid())
    return;
  uintptr_t value = read<uintptr_t>(ptr + 0x30);
  if (!value)
    return;
  uintptr_t value1 = read<uintptr_t>(value + 0x10);
  if (!value1)
    return;
  uintptr_t value2 = read<uintptr_t>(value1 + 0x20);
  if (!value2)
    return;
  uintptr_t value3 = read<uintptr_t>(value2 + 0x70);
  if (!value3)
    return;
  uintptr_t value4 = read<uintptr_t>(value3 + 0x0);
  if (!value4)
    return;

  write<float>(value4 + 0x2C, v);
}

bool CPed::IsFriend() {
  // Check pointer list first — peds may not have a readable name yet
  for (auto ptr_val : cfg::friend_list) {
    if (ptr == ptr_val)
      return true;
  }

  // Team check requires a valid name
  const std::string &name = GetName();
  if (!name.empty() && IsTeamMember()) {
    return true;
  }

  return false;
}

bool CPed::IsTeamMember() {
  if (!g.localplayer || !g.localplayer->IsValid())
    return false;
  BYTE myTeam = g.localplayer->GetTeam();
  BYTE pedTeam = GetTeam();
  return myTeam == pedTeam && myTeam != 0;
}

bool CPed::IsAdmin() {
    return get_alpha() < 250;
}
