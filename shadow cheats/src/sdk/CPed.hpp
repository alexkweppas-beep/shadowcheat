#pragma once
#include "../math/SimpleMath.h"
#include "bones.hpp"
#include "offsets.hpp"
#include <cstdint>
#include <string>

using namespace DirectX::SimpleMath;

class CPed {
public:
  uintptr_t ptr = 0;
  uintptr_t player_info = 0;
  uintptr_t weaponmanager = 0;
  uintptr_t weaponinfo = 0;

  float m_fArmor = 0.f;
  float m_fMaxArmor = 0.f;
  float m_fHealth = 0.f;
  float m_fMaxHealth = 0.f;
  float m_fDyingHealthThreshold = 100.0f;
  Vector3 m_pVecPosition{};

  void SetPosition(Vector3 pos);
  void SetRagdoll(bool state);
  void SetCollision(float v);

  Matrix m_pBoneMatrix{};
  Vector3 BoneList[bones::BONE_COUNT]{};
  int BoneIndexCache[bones::BONE_COUNT]{};
  uintptr_t LastSkeletonPtr = 0;
  bones::cSkeleton_t skeleton{};

  bool IsValid() const { return ptr != 0; }
  bool isPlayer();
  bool isDead();
  bool IsVisible();
  
  bool IsInVehicle();
  uintptr_t GetVehicle();

  bool get_player(uintptr_t base);
  bool Update();
  bool UpdateFast();

  uintptr_t get_weaponmanager();
  uintptr_t get_weaponinfo();
  uintptr_t get_currentweapon();
  uint32_t get_weaponhash();

  const std::string &GetName();
  BYTE GetTeam();
  Vector3 GetBonePosition(int index) const;

  bool IsFriend();
  bool IsTeamMember();
  bool IsAdmin();
  
  BYTE get_alpha();

  std::string cached_name;
  BYTE cached_team = 0;
  uint64_t last_name_update = 0;
  uint64_t last_team_update = 0;
};
