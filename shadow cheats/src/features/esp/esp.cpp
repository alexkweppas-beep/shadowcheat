#include "esp.hpp"
#include "../../config/settings.hpp"
#include "../../core/globals.hpp"
#include "../../memory/memory_access.hpp"
#include "../../render/draw.hpp"
#include "../../sdk/CPed.hpp"
#include "../../sdk/bones.hpp"
#include "../../sdk/math_utils.hpp"
#include "../../sdk/offsets.hpp"
#include "../../sdk/weapons.hpp"
#include "../../sdk/world_to_screen.hpp"
#include "../../security/vmp_markers.hpp"
#include "security/xorstr.hpp"
#include <algorithm>
#include <imgui.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <chrono>

namespace {
void DrawBoneLine(const Matrix &view, const Vector3 &a, const Vector3 &b,
                  ImU32 color, float thickness = 1.0f) {
  if (a == Vector3(0, 0, 0) || b == Vector3(0, 0, 0))
    return;
  Vector2 sa{}, sb{};
  if (!WorldToScreen(view, a, sa))
    return;
  if (!WorldToScreen(view, b, sb))
    return;

  float screenW = (float)ImGui::GetIO().DisplaySize.x;
  float screenH = (float)ImGui::GetIO().DisplaySize.y;

  bool aOff = (sa.x < -100 || sa.x > screenW + 100 || sa.y < -100 ||
               sa.y > screenH + 100);
  bool bOff = (sb.x < -100 || sb.x > screenW + 100 || sb.y < -100 ||
               sb.y > screenH + 100);
  if (aOff && bOff)
    return;

  // Fix ImGui crash with huge coordinates when entity is very close to camera
  if (std::abs(sa.x) > 16384.f || std::abs(sa.y) > 16384.f ||
      std::abs(sb.x) > 16384.f || std::abs(sb.y) > 16384.f)
    return;

  draw::DrawLine(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y), color, thickness);
}

} // namespace

namespace features {
static const std::unordered_map<uint32_t, std::string> objectNames = {

    {0x92A27487, "Dagger"},
    {0x958A4A8F, "Bat"},
    {0xF9E6AA4B, "Bottle"},
    {0x84BD7BFD, "Crowbar"},
    {0xA2719263, "Unarmed"},
    {0x8BB05FD7, "Flashlight"},
    {0x440E4788, "Golf Club"},
    {0x4E875F73, "Hammer"},
    {0xF9DCBF2D, "Hatchet"},
    {0xD8DF3C3C, "Knuckle Dusters"},
    {0x99B507EA, "Knife"},
    {0xDD5DF8D9, "Machete"},
    {0xDFE37640, "Switchblade"},
    {0x678B81B1, "Nightstick"},
    {0x19044EE0, "Wrench"},
    {0xCD274149, "Battle Axe"},
    {0x94117305, "Pool Cue"},
    {0x3813FC08, "Stone Hatchet"},
    {0x6589186A, "Candy Cane"},
    {0xDAC00025, "Stun Rod"},

    {0x1B06D571, "Pistol"},
    {0xBFE256D4, "Pistol Mk II"},
    {0x5EF9FEC4, "Combat Pistol"},
    {0x22D8FE39, "AP Pistol"},
    {0x3656C8C1, "Stun Gun"},
    {0x99AEEB3B, "Pistol .50"},
    {0xBFD21232, "SNS Pistol"},
    {0x88374054, "SNS Pistol Mk II"},
    {0xD205520E, "Heavy Pistol"},
    {0x83839C4, "Vintage Pistol"},
    {0x47757124, "Flare Gun"},
    {0xDC4DB296, "Marksman Pistol"},
    {0xC1B3C3D1, "Revolver"},
    {0xCB96392F, "Revolver Mk II"},
    {0x97EA20B8, "Double-Action Revolver"},
    {0xAF3696A1, "Ray Pistol"},
    {0x2B5EF5EC, "Ceramic Pistol"},
    {0x917F6C8C, "Navy Revolver"},

    {0x13532244, "Micro SMG"},
    {0x2BE6766B, "SMG"},
    {0x78A97CD0, "SMG Mk II"},
    {0xEFE7E2DF, "Assault SMG"},
    {0xA3D4D34, "Combat PDW"},
    {0xDB1AA450, "Machine Pistol"},
    {0xBD248B55, "Mini SMG"},
    {0x476BF155, "Ray Carbine"},

    {0x1D073A89, "Pump Shotgun"},
    {0x555AF99A, "Pump Shotgun Mk II"},
    {0x7846A318, "Sawed-Off Shotgun"},
    {0xE284C527, "Assault Shotgun"},
    {0x9D61E50F, "Bullpup Shotgun"},
    {0x3AABBBAA, "Heavy Shotgun"},
    {0xEF951FBB, "Double Barrel Shotgun"},
    {0x12E82D3D, "Auto Shotgun"},

    {0xBFEFFF6D, "Assault Rifle"},
    {0x394F415C, "Assault Rifle Mk II"},
    {0x83BF0278, "Carbine Rifle"},
    {0xFAD1F1C9, "Carbine Rifle Mk II"},
    {0xAF113F99, "Advanced Rifle"},
    {0xC0A3098D, "Special Carbine"},
    {0x969C3D67, "Special Carbine Mk II"},
    {0x7F229F94, "Bullpup Rifle"},
    {0x84D6FAFD, "Bullpup Rifle Mk II"},
    {0x624FE830, "Compact Rifle"},

    {0x9D07F764, "MG"},
    {0x7FD62962, "Combat MG"},
    {0xDBBD7280, "Combat MG Mk II"},
    {0x61012683, "Gusenberg"},

    {0x5FC3C11, "Sniper Rifle"},
    {0xC472FE2, "Heavy Sniper"},
    {0xA914799, "Heavy Sniper Mk II"},
    {0xC734385A, "Marksman Rifle"},
    {0x6A6C02E0, "Marksman Rifle Mk II"},
    {0xA89CB99E, "Musket"},

    {0xB1CA77B1, "RPG"},
    {0xA284510B, "Grenade Launcher"},
    {0x4DD2DC56, "Grenade Launcher Smoke"},
    {0x42BF8A85, "Minigun"},
    {0x7F7497E5, "Firework"},
    {0x6D544C99, "Railgun"},
    {0x63AB0442, "Homing Launcher"},
    {0x781FE4A, "Compact Launcher"},
    {0xB62D1F67, "Ray Minigun"},

    {0x93E220BD, "Grenade"},
    {0xA0973D5E, "BZ Gas"},
    {0xFDBC8A50, "Smoke Grenade"},
    {0x497FACC3, "Flare"},
    {0x24B17070, "Molotov"},
    {0x2C3731D9, "Sticky Bomb"},
    {0xAB564B93, "Prox Mine"},
    {0x787F0BB, "Snowball"},
    {0xBA45E8B8, "Pipe Bomb"},
    {0x23C9F95C, "Ball"},

    {0x34A67B97, "Petrol Can"},
    {0x60EC506, "Fire Extinguisher"},
    {0xFBAB5776, "Parachute"},
    {0xBA536372, "Hazard Can"},
};

void RenderESP_Impl() {
  // Snapshot localplayer + entity list under the same mutex to avoid race
  // with UpdateEntityListLoop which can set g.localplayer = nullptr
  CPed local_copy;
  std::vector<CPed> local_list;
  {
    std::lock_guard<std::mutex> lock(g.EntityListMutex);
    if (!g.localplayer)
      return;
    local_copy = *g.localplayer;
    local_list = g.EntityList;
  }
  CPed *pLocal = &local_copy;
  if (!pLocal->Update())
    return;

  if (!g.Viewport)
    return; // game not yet located — skip frame
  const Matrix view = read<Matrix>(g.Viewport + offsets::viewport::ViewMatrix);
  const Vector2 screenCenter2D = {ImGui::GetIO().DisplaySize.x * 0.5f,
                                  ImGui::GetIO().DisplaySize.y * 0.5f};

  float bestScreenDist = 999999.f;
  Vector2 bestBoneScreen;

  int countPed_Admin = 0;

  // Skeleton cache: declared outside the loop so the eviction block can access them
  static std::unordered_map<uintptr_t, std::pair<uint64_t, std::array<Vector3, bones::BONE_COUNT>>> skeleton_cache;
  static std::unordered_set<uintptr_t> active_ptrs_this_frame;

  for (auto &ped : local_list) {
    CPed *pEntity = &ped;
    if (pEntity->ptr ==
        pLocal->ptr) // Compare by game ptr, not snapshot address
      continue;
    if (!pEntity->Update())
      continue;

    const float distance =
        GetDistance(pEntity->m_pVecPosition, pLocal->m_pVecPosition);
    if (distance > cfg::ESP_MaxDistance)
      continue;
    if (cfg::ESP_PlayerOnly && !pEntity->isPlayer())
      continue;

    bool isPlayer = pEntity->isPlayer();

    if (!isPlayer)
      continue;

    auto getBonePos = [&](int idx) -> Vector3 {
      if (idx >= 0 && idx < bones::BONE_COUNT)
        return pEntity->BoneList[idx];
      return pEntity->m_pVecPosition;
    };

    auto &acfg = cfg::aimbot_configs[cfg::current_weapon_category];
    Vector3 aimBonePos;
    if (acfg.aimbone == 4) {
      const int nearestBones[] = {
          bones::BONE_HEAD,       bones::BONE_NECK,   bones::BONE_PELVIS,
          bones::BONE_SPINE0,     bones::BONE_SPINE1, bones::BONE_LEFT_THIGH,
          bones::BONE_RIGHT_THIGH};
      float nearestDist = 99999.f;
      for (int b : nearestBones) {
        Vector3 bonePos = getBonePos(b);
        Vector2 screenPos;
        if (WorldToScreen(view, bonePos, screenPos)) {
          float dx = screenPos.x - screenCenter2D.x;
          float dy = screenPos.y - screenCenter2D.y;
          float d = sqrtf(dx * dx + dy * dy);
          if (d < nearestDist) {
            nearestDist = d;
            aimBonePos = bonePos;
          }
        }
      }
      if (nearestDist == 99999.f)
        aimBonePos = pEntity->m_pVecPosition;
    } else {
      const int boneMap[] = {bones::BONE_HEAD, bones::BONE_NECK,
                             bones::BONE_SPINE2, bones::BONE_PELVIS};
      aimBonePos = getBonePos(boneMap[acfg.aimbone]);
    }

    Vector2 boneScreenPos;
    if (WorldToScreen(view, aimBonePos, boneScreenPos)) {
      float dx = boneScreenPos.x - screenCenter2D.x;
      float dy = boneScreenPos.y - screenCenter2D.y;
      float screenDist = sqrtf(dx * dx + dy * dy);
      if (screenDist < bestScreenDist && screenDist < (float)acfg.fov) {
        bestScreenDist = screenDist;
        bestBoneScreen = boneScreenPos;
      }
    }

    bool showBox = false, showLine = false, showHealth = false,
         showArmor = false, showDist = false, showWeapon = false,
         showSkel = false;
    int boxType = 0, healthBarPos = 0, armorBarPos = 0;
    ImColor colorBox(255, 255, 255, 255), colorSkel(255, 255, 255, 255),
        colorLine(255, 255, 255, 255), colorText(255, 255, 255, 255),
        colorDist(255, 255, 255, 255), colorWep(255, 255, 255, 255);

    if (isPlayer) {
      // ─── Оптимизация: IsFriend() читает память — вызываем ТОЛЬКО если friends ESP включён ───
      const bool isFriend = cfg::esp_show_friends && pEntity->IsFriend();

      if (isFriend) {
        showBox = cfg::ESP_Friends_Box;
        showLine = cfg::ESP_Friends_Line;
        showHealth = cfg::ESP_Friends_HealthBar;
        showArmor = cfg::ESP_Friends_HealthBar;
        showDist = cfg::ESP_Friends_Distance;
        showWeapon = cfg::ESP_Friends_Weapon;
        showSkel = cfg::ESP_Friends_SkeletonLines;
        boxType = cfg::esp_friends_box_type;
        healthBarPos = cfg::esp_friends_bar_position;
        armorBarPos = cfg::esp_friends_armor_bar_position;
        colorBox = cfg::col_friends_box;
        colorSkel = cfg::col_friends_skeleton;
        colorLine = cfg::col_friends_line;
        colorDist = cfg::col_friends_distance;
        colorWep = cfg::col_friends_weapon;
        colorText = ImColor(255, 255, 255, 255);
      } else {
        showBox = cfg::ESP_Box;
        showLine = cfg::ESP_Line;
        showHealth = cfg::ESP_HealthBar;
        showArmor = cfg::ESP_HealthBar;
        showDist = cfg::ESP_Distance;
        showWeapon = cfg::ESP_Weapon;
        showSkel = cfg::ESP_SkeletonLines;
        boxType = cfg::esp_players_box_type;
        healthBarPos = cfg::esp_players_bar_position;
        armorBarPos = cfg::esp_players_armor_bar_position;
        colorBox = cfg::col_players_box;
        colorSkel = cfg::col_players_skeleton;
        colorLine = cfg::col_players_line;
        colorDist = cfg::col_players_distance;
        colorWep = cfg::col_players_weapon;
        colorText = ImColor(255, 255, 255, 255);

        // ─── Оптимизация: get_alpha() читает память — вызываем ТОЛЬКО если Admins ESP включён ───
        if (cfg::esp_show_admins) {
          BYTE alpha = pEntity->get_alpha();
          if (alpha < 250 && distance <= 100.f) {
            countPed_Admin++;
            showBox = cfg::ESP_Admins_Box;
            showLine = cfg::ESP_Admins_Line;
            showHealth = cfg::ESP_Admins_HealthBar;
            showArmor = cfg::ESP_Admins_HealthBar;
            showDist = cfg::ESP_Admins_Distance;
            showWeapon = cfg::ESP_Admins_Weapon;
            showSkel = cfg::ESP_Admins_SkeletonLines;
            boxType = cfg::esp_admins_box_type;
            healthBarPos = cfg::esp_admins_bar_position;
            armorBarPos = cfg::esp_admins_armor_bar_position;
            colorBox = cfg::col_admins_box;
            colorSkel = cfg::col_admins_skeleton;
            colorLine = cfg::col_admins_line;
            colorDist = cfg::col_admins_distance;
            colorWep = cfg::col_admins_weapon;
            colorText = ImColor(255, 255, 255, 255);
          }
        }
      }
    }

    // ─── Early camera visibility check ───
    // Skip bone reads entirely if entity base pos is behind the camera.
    // WorldToScreen returns false when Z < 0 (behind frustum).
    {
        Vector2 _check{};
        if (!WorldToScreen(view, pEntity->m_pVecPosition, _check))
            continue; // Behind camera — no bones read, no draw
    }


    bool has_detailed_bones = false;
    uint64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto& cache_entry = skeleton_cache[pEntity->ptr];
    active_ptrs_this_frame.insert(pEntity->ptr);

    if (cfg::SkeletonUpdateDelayMs <= 0 || (current_time - cache_entry.first) >= static_cast<uint64_t>(cfg::SkeletonUpdateDelayMs)) {
        has_detailed_bones = bones::UpdateDetailed(pEntity);
        if (has_detailed_bones) {
            cache_entry.first = current_time;
            for (int i = 0; i < bones::BONE_COUNT; ++i) {
                cache_entry.second[i] = pEntity->BoneList[i];
            }
        }
    } else {
        has_detailed_bones = true; // Use cached
        for (int i = 0; i < bones::BONE_COUNT; ++i) {
            pEntity->BoneList[i] = cache_entry.second[i];
        }
    }

    Vector2 pBase{}, pHead{}, pNeck{}, pLeftFoot{}, pRightFoot{};
    if (!WorldToScreen(view, pEntity->m_pVecPosition, pBase) ||
        !WorldToScreen(view, pEntity->BoneList[bones::BONE_HEAD], pHead) ||
        !WorldToScreen(view, pEntity->BoneList[bones::BONE_NECK], pNeck) ||
        !WorldToScreen(view, pEntity->BoneList[bones::BONE_LEFT_FOOT],
                       pLeftFoot) ||
        !WorldToScreen(view, pEntity->BoneList[bones::BONE_RIGHT_FOOT],
                       pRightFoot))
      continue;

    float pBottom = pLeftFoot.y > pRightFoot.y ? pLeftFoot.y : pRightFoot.y;
    float pTop = pHead.y;

    float rawHeight = pBottom - pTop;
    if (rawHeight < 1.0f)
      rawHeight = 1.0f;

    pTop -= rawHeight * 0.15f; // Margin above the head

    float pHeight = pBottom - pTop;
    float pWidth = pHeight / 3.5f;

    float l = pBase.x - pWidth;
    float r = pBase.x + pWidth;

    float scaleFactor =
        std::clamp(25.0f / (distance > 0.0f ? distance : 1.0f), 0.5f, 1.0f);
    float boxThickness = 1.5f * scaleFactor;
    float fontSize = std::round(14.0f * scaleFactor);

    if (showLine)
      draw::DrawLine(ImVec2(g.GameRect.right / 2.f, g.GameRect.bottom),
                     ImVec2(pBase.x, pBottom), colorLine,
                     cfg::esp_box_thickness);

    if (showBox) {
      float thickness = cfg::esp_box_thickness;
      float rounding = cfg::esp_box_rounding;

      if (pEntity->IsFriend()) {
        thickness = cfg::esp_friends_box_thickness;
        rounding = cfg::esp_friends_box_rounding;
      } else if (pEntity->get_alpha() < 250 && distance <= 100.f &&
                 cfg::esp_show_admins) {
        thickness = cfg::esp_admins_box_thickness;
        rounding = cfg::esp_admins_box_rounding;
      }

      DrawESPBox(ImVec2(l, pTop), ImVec2(r, pBottom), colorBox, boxType,
                 thickness * scaleFactor, rounding);
    }

    if (showHealth || showArmor) {
      ImVec2 topLeft(l, pTop);
      ImVec2 bottomRight(r, pBottom);

      ImColor hc = cfg::col_health;
      ImColor ac = cfg::col_armor;

      if (pEntity->IsFriend() && cfg::esp_friends_custom_health_colors) {
        hc = cfg::col_friends_health;
        ac = cfg::col_friends_armor;
      } else if (pEntity->get_alpha() < 250 && distance <= 100.f &&
                 cfg::esp_show_admins && cfg::esp_admins_custom_health_colors) {
        hc = cfg::col_admins_health;
        ac = cfg::col_admins_armor;
      } else if (!cfg::esp_custom_health_colors) {
        hc = ImColor(0, 255, 0, 255);
        ac = ImColor(0, 150, 255, 255);
      }

      DrawHealthArmorBars(
          topLeft, bottomRight, pEntity->m_fHealth, pEntity->m_fMaxHealth,
          pEntity->m_fArmor, healthBarPos, armorBarPos, showHealth,
          showArmor && pEntity->m_fArmor > 0.f, scaleFactor, hc, ac);
    }

    float textYOffset = std::max(1.0f, std::round(2.0f * scaleFactor));
    if (healthBarPos == 1 && showHealth)
      textYOffset += std::round(6.5f * scaleFactor);
    if (armorBarPos == 1 && showArmor && pEntity->m_fArmor > 0.f)
      textYOffset += std::round(5.0f * scaleFactor);

    if (showDist) {
      std::string txt = std::to_string((int)distance) + xorstr_("m");
      ImFont *curFont = ImGui::GetFont();
      float tw =
          curFont
              ? curFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, txt.c_str()).x
              : 0.f;
      draw::StringScaledOutlined(
          ImVec2(l + (r - l) * 0.5f - tw * 0.5f, pBottom + textYOffset),
          isPlayer ? colorDist : colorText, ImColor(0, 0, 0, 255), txt.c_str(),
          fontSize);
      textYOffset += fontSize - 2.0f;
    }

    if (showWeapon) {
      std::string txt = weapons::get_weapon_name(pEntity->get_weaponhash());
      ImFont *curFont = ImGui::GetFont();
      float tw =
          curFont
              ? curFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, txt.c_str()).x
              : 0.f;
      draw::StringScaledOutlined(
          ImVec2(l + (r - l) * 0.5f - tw * 0.5f, pBottom + textYOffset),
          isPlayer ? colorWep : colorText, ImColor(0, 0, 0, 255), txt.c_str(),
          fontSize);
      textYOffset += fontSize - 2.0f;
    }

    if (showSkel) {
      if (has_detailed_bones) {
        ImU32 sc = colorSkel;
        auto &B = pEntity->BoneList;
        float skel_thickness = cfg::esp_skeleton_thickness;
        if (pEntity->IsFriend()) {
          skel_thickness = cfg::esp_friends_skeleton_thickness;
        } else if (pEntity->get_alpha() < 250 && distance <= 100.f &&
                   cfg::esp_show_admins) {
          skel_thickness = cfg::esp_admins_skeleton_thickness;
        }

        auto is_collapsed = [&](int b) {
          Vector3 root(pEntity->skeleton.Arg1._41, pEntity->skeleton.Arg1._42,
                       pEntity->skeleton.Arg1._43);
          return GetDistance(B[b], B[bones::BONE_PELVIS]) < 0.05f ||
                 GetDistance(B[b], pEntity->m_pVecPosition) < 0.05f ||
                 GetDistance(B[b], root) < 0.05f;
        };
        auto draw_limb = [&](int b1, int b2, int b3, int b4) {
          int path[] = {b1, b2, b3, b4};
          int prevBone = path[0];
          for (int i = 1; i < 4; i++) {

            if (i == 3 || !is_collapsed(path[i])) {
              DrawBoneLine(view, B[prevBone], B[path[i]], sc,
                           skel_thickness * scaleFactor);
              prevBone = path[i];
            }
          }
        };

        // шея → голова
        // DrawBoneLine(view, B[bones::BONE_NECK], B[bones::BONE_HEAD], sc,
        //              skel_thickness * scaleFactor);

        draw_limb(bones::BONE_NECK, bones::BONE_LEFT_UPPER_ARM,
                  bones::BONE_LEFT_FOREARM, bones::BONE_LEFT_HAND);

        draw_limb(bones::BONE_NECK, bones::BONE_RIGHT_UPPER_ARM,
                  bones::BONE_RIGHT_FOREARM, bones::BONE_RIGHT_HAND);

        DrawBoneLine(view, B[bones::BONE_NECK], B[bones::BONE_PELVIS], sc,
                     skel_thickness * scaleFactor);

        draw_limb(bones::BONE_PELVIS, bones::BONE_LEFT_THIGH,
                  bones::BONE_LEFT_CALF, bones::BONE_LEFT_FOOT);

        draw_limb(bones::BONE_PELVIS, bones::BONE_RIGHT_THIGH,
                  bones::BONE_RIGHT_CALF, bones::BONE_RIGHT_FOOT);
      }
    }
  }

  // ─── Evict skeleton cache entries for entities no longer in draw radius ───
  // Prevents unbounded memory growth when players leave/disconnect
  {
    for (auto it = skeleton_cache.begin(); it != skeleton_cache.end(); ) {
      if (active_ptrs_this_frame.find(it->first) == active_ptrs_this_frame.end())
        it = skeleton_cache.erase(it);
      else
        ++it;
    }
    active_ptrs_this_frame.clear();
  }

  if (cfg::ESP_AimLine && bestScreenDist < 999999.f) {
    if ((GetAsyncKeyState(cfg::aimkey) & 0x8000) ||
        (GetAsyncKeyState(cfg::triggerkey) & 0x8000)) {
      draw::DrawLine(ImVec2(screenCenter2D.x, screenCenter2D.y),
                     ImVec2(bestBoneScreen.x, bestBoneScreen.y),
                     cfg::col_AimLine, 1.5f);
    }
  }

  // Objects
  if (cfg::esp_objects_enabled) {
    std::vector<Object> local_objects;
    {
      std::lock_guard<std::mutex> lock(g.EntityListMutex);
      local_objects = g.ObjectList;
    }
    for (const auto &obj : local_objects) {
      if (!obj.should_draw)
        continue;

      Vector2 screenPos;
      if (!WorldToScreen(view, obj.position, screenPos))
        continue;

      if (cfg::esp_objects_snapline) {
        draw::DrawLine(ImVec2(g.GameRect.right / 2.f, g.GameRect.bottom),
                       ImVec2(screenPos.x, screenPos.y),
                       cfg::col_objects_snapline, 1.5f);
      }

      char distBuf[32];
      snprintf(distBuf, sizeof(distBuf) - 1, xorstr_("%.0fm"),
               obj.dist_to_local);
      distBuf[sizeof(distBuf) - 1] = '\0';
      std::string displayText =
          std::string(obj.name) + xorstr_(" [") + distBuf + xorstr_("]");
      ImFont *curFont = ImGui::GetFont();
      float tw = curFont ? curFont
                               ->CalcTextSizeA(14.0f, FLT_MAX, 0.0f,
                                               displayText.c_str())
                               .x
                         : 0.f;

      // Draw text
      draw::StringScaledOutlined(ImVec2(screenPos.x - tw * 0.5f, screenPos.y),
                                 cfg::col_objects, ImColor(0, 0, 0, 255),
                                 displayText.c_str(), 14.f);
    }
  }

  // Targets (Highlights and Arrows)
  if (cfg::targets_highlight_heavy_sniper || cfg::targets_highlight_heavy_mk2 ||
      cfg::targets_highlight_precision_rifle || cfg::targets_arrows_enable) {
    std::vector<CPed> local_list2;
    {
      std::lock_guard<std::mutex> lock(g.EntityListMutex);
      local_list2 = g.EntityList;
    }
    for (auto &ped : local_list2) {
      CPed *pEntity = &ped;
      if (pEntity->ptr == pLocal->ptr)
        continue;
      if (!pEntity->isPlayer())
        continue; // Usually applied only to players

      uint32_t weaponHash = pEntity->get_weaponhash();

      bool highlightWeapon = false;
      ImColor highlightColor;

      if (cfg::targets_highlight_heavy_sniper && weaponHash == 0xC472FE2) {
        highlightWeapon = true;
        highlightColor = cfg::col_targets_highlight_heavy_sniper;
      } else if (cfg::targets_highlight_heavy_mk2 && weaponHash == 0xA914799) {
        highlightWeapon = true;
        highlightColor = cfg::col_targets_highlight_heavy_mk2;
      } else if (cfg::targets_highlight_precision_rifle &&
                 weaponHash == 0x6A6C02E0) { // Using Marksman Mk2 or similar
                                             // for Precision Rifle if hash
                                             // varies, here 0x6A6C02E0
        highlightWeapon = true;
        highlightColor = cfg::col_targets_highlight_precision_rifle;
      }

      if (highlightWeapon) {
        // Highlight the player
        Vector2 pBase{}, pTopScreen{};
        if (WorldToScreen(view, pEntity->m_pVecPosition, pBase) &&
            WorldToScreen(view, pEntity->BoneList[bones::BONE_HEAD],
                          pTopScreen)) {
          float pBottom = pBase.y;
          float pTop = pTopScreen.y;
          float rawHeight = pBottom - pTop;
          if (rawHeight < 1.0f)
            rawHeight = 1.0f;
          pTop -= rawHeight * 0.15f;
          float pHeight = pBottom - pTop;
          float pWidth = pHeight / 3.5f;
          float l = pBase.x - pWidth;
          float r = pBase.x + pWidth;

          DrawESPBox(ImVec2(l, pTop), ImVec2(r, pBottom), highlightColor, 0,
                     2.0f, 0.0f); // Draw border box

          // Draw snapline for highlighted weapon
          draw::DrawLine(ImVec2(g.GameRect.right / 2.f, g.GameRect.bottom),
                         ImVec2(pBase.x, pBottom), highlightColor, 1.5f);
        }
      }

      if (cfg::targets_arrows_enable) {
        Vector3 entityPos = pEntity->m_pVecPosition;

        Vector2 screenPos;
        bool isVisible = WorldToScreen(view, entityPos, screenPos);

        if (!cfg::targets_arrows_exclude_visible || !isVisible) {
          const Matrix v = view.Transpose();
          float vec_out_x = v._21 * entityPos.x + v._22 * entityPos.y +
                            v._23 * entityPos.z + v._24;
          float vec_out_y = v._31 * entityPos.x + v._32 * entityPos.y +
                            v._33 * entityPos.z + v._34;
          float vec_out_z = v._41 * entityPos.x + v._42 * entityPos.y +
                            v._43 * entityPos.z + v._44;

          if (vec_out_z <= 0.001f) {
            vec_out_x *= -1.0f;
            vec_out_y *= -1.0f;
          }

          float angle = atan2f(-vec_out_y, vec_out_x);
          float arrowRadius =
              ImGui::GetIO().DisplaySize.y * (cfg::targets_arrows_fov / 360.f);

          ImVec2 center(ImGui::GetIO().DisplaySize.x / 2.f,
                        ImGui::GetIO().DisplaySize.y / 2.f);
          ImVec2 arrowPos(center.x + cosf(angle) * arrowRadius,
                          center.y + sinf(angle) * arrowRadius);

          bool drawArrow = true;
          if (cfg::targets_arrows_flicker) {
            if ((int)(ImGui::GetTime() * 10) % 2 == 0)
              drawArrow = false;
          }

          if (drawArrow) {
            float size = cfg::targets_arrows_size;
            ImVec2 p1(arrowPos.x + cosf(angle) * size,
                      arrowPos.y + sinf(angle) * size);
            ImVec2 p2(arrowPos.x + cosf(angle + 2.5f) * size,
                      arrowPos.y + sinf(angle + 2.5f) * size);
            ImVec2 p3(arrowPos.x + cosf(angle - 2.5f) * size,
                      arrowPos.y + sinf(angle - 2.5f) * size);

            ImColor arrowColor = cfg::col_targets_arrows;
            if (highlightWeapon) {
              arrowColor = highlightColor;
            }

            ImGui::GetBackgroundDrawList()->AddTriangleFilled(p1, p2, p3,
                                                              arrowColor);
          }
        }
      }
    }
  }

  if (cfg::show_admins_around) {
    char adminText[64];
    snprintf(adminText, sizeof(adminText) - 1, "Admins around you: %d",
             countPed_Admin);
    adminText[sizeof(adminText) - 1] = '\0';

    ImGui::SetNextWindowPos(cfg::admins_around_pos, ImGuiCond_FirstUseEver);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0)); // No bg
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);    // No border

    if (ImGui::Begin(
            "AdminsAround", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
      ImVec2 pos = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##drag_admins", ImGui::CalcTextSize(adminText));

      draw::StringOutlined(pos, cfg::col_admins_around, ImColor(0, 0, 0, 255),
                           adminText);

      cfg::admins_around_pos = ImGui::GetWindowPos(); // Save position
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
  }

  if (cfg::show_players_around) {
    char playerText[64];
    snprintf(playerText, sizeof(playerText) - 1, "Players around you: %d",
             std::max(0, (int)local_list.size() - 1));
    playerText[sizeof(playerText) - 1] = '\0';

    ImGui::SetNextWindowPos(cfg::players_around_pos, ImGuiCond_FirstUseEver);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0)); // No bg
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);    // No border

    if (ImGui::Begin(
            "PlayersAround", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
      ImVec2 pos = ImGui::GetCursorScreenPos();
      ImGui::InvisibleButton("##drag_players", ImGui::CalcTextSize(playerText));

      draw::StringOutlined(pos, cfg::col_players_around, ImColor(0, 0, 0, 255),
                           playerText);

      cfg::players_around_pos = ImGui::GetWindowPos(); // Save position
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
  }

  // Radar
  if (cfg::targets_radar_enable) {
    ImGui::SetNextWindowSize(
        ImVec2(cfg::targets_radar_size * 2, cfg::targets_radar_size * 2),
        ImGuiCond_FirstUseEver);

    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(10.f / 255.f, 10.f / 255.f, 10.f / 255.f,
                                 cfg::targets_radar_opacity));
    ImGui::PushStyleColor(ImGuiCol_Border,
                          ImVec4(100.f / 255.f, 100.f / 255.f, 100.f / 255.f,
                                 cfg::targets_radar_opacity));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    // allow resizing and moving by clicking the title bar/corners.
    if (ImGui::Begin(xorstr_("Radar##TargetRadar"), nullptr,
                     ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoScrollbar |
                         ImGuiWindowFlags_NoTitleBar)) {
      ImVec2 windowPos = ImGui::GetWindowPos();
      ImVec2 windowSize = ImGui::GetWindowSize();

      ImVec2 radarCenter = ImVec2(windowPos.x + windowSize.x * 0.5f,
                                  windowPos.y + windowSize.y * 0.5f);
      float radius = (std::min(windowSize.x, windowSize.y) * 0.5f) - 5.0f;

      ImDrawList *drawList = ImGui::GetWindowDrawList();

      // Draw local player point
      drawList->AddCircleFilled(
          radarCenter, 4.0f,
          ImColor(255, 255, 255, (int)(cfg::targets_radar_opacity * 255.0f)));

      Vector3 localPos = pLocal->m_pVecPosition;

      float localYaw = 0.f;

      std::vector<CPed> local_list3;
      {
        std::lock_guard<std::mutex> lock(g.EntityListMutex);
        local_list3 = g.EntityList;
      }

      for (auto &ped : local_list3) {
        CPed *pEntity = &ped;
        if (pEntity->ptr == pLocal->ptr)
          continue;
        if (!pEntity->isPlayer())
          continue;
        if (!pEntity->Update())
          continue;

        Vector3 entityPos = pEntity->m_pVecPosition;
        Vector3 delta = entityPos - localPos;

        float distance = sqrtf(delta.x * delta.x + delta.y * delta.y);
        if (distance > cfg::targets_radar_range)
          continue;
        if (distance < 0.1f)
          continue;

        float yaw = atan2f(delta.y, delta.x);
        float rotatedYaw = yaw - localYaw - 1.5708f; // -90 deg for north up

        float radarDist = (distance / cfg::targets_radar_range) * radius;

        ImVec2 pointPos = ImVec2(radarCenter.x + cosf(rotatedYaw) * radarDist,
                                 radarCenter.y + sinf(rotatedYaw) * radarDist);

        ImColor pointColor =
            ImColor(255, 0, 0, (int)(cfg::targets_radar_opacity * 255.0f));

        if (pEntity->IsFriend()) {
          pointColor =
              ImColor(0, 255, 0, (int)(cfg::targets_radar_opacity * 255.0f));
        } else {
          uint32_t weaponHash = pEntity->get_weaponhash();
          if (cfg::targets_highlight_heavy_sniper && weaponHash == 0xC472FE2) {
            pointColor = cfg::col_targets_highlight_heavy_sniper;
          } else if (cfg::targets_highlight_heavy_mk2 &&
                     weaponHash == 0xA914799) {
            pointColor = cfg::col_targets_highlight_heavy_mk2;
          } else if (cfg::targets_highlight_precision_rifle &&
                     weaponHash == 0x6A6C02E0) {
            pointColor = cfg::col_targets_highlight_precision_rifle;
          }
        }

        pointColor.Value.w = cfg::targets_radar_opacity;
        drawList->AddCircleFilled(pointPos, 3.5f, pointColor);
      }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
  }
}

void RenderESP() {
    RenderESP_Impl();
}

} // namespace features
