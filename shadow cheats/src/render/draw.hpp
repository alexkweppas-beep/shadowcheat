#pragma once
#include "../config/settings.hpp"
#include "../sdk/CPed.hpp"
#include "../sdk/bones.hpp"
#include "../sdk/world_to_screen.hpp"
#include <algorithm>
#include <imgui.h>

namespace draw {
void DrawLine(const ImVec2 &a, const ImVec2 &b, ImU32 color,
              float thickness = 1.f);
void DrawRect(const ImVec2 &min, const ImVec2 &max, ImU32 color, float r = 0.f,
              float thickness = 1.f);
void FillRect(const ImVec2 &min, const ImVec2 &max, ImU32 color);
void FillRectRounded(const ImVec2 &min, const ImVec2 &max, ImU32 color,
                     float r);
void String(const ImVec2 &pos, ImU32 color, const char *text);
void StringScaled(const ImVec2 &pos, ImU32 color, const char *text, float size);
void StringOutlined(const ImVec2 &pos, ImU32 color, ImU32 outline,
                    const char *text);
void StringScaledOutlined(const ImVec2 &pos, ImU32 color, ImU32 outline,
                          const char *text, float size);
void HealthBar(float x, float y, float w, float h, float cur, float max);
void ArmorBar(float x, float y, float w, float h, float cur, float max);
} // namespace draw

inline void DrawLine(const ImVec2 &a, const ImVec2 &b, const ImColor &c,
                     float t = 1.f) {
  draw::DrawLine(a, b, (ImU32)c, t);
}
inline void String(const ImVec2 &p, const ImColor &c, const char *s) {
  draw::String(p, (ImU32)c, s);
}
inline void StringScaled(const ImVec2 &p, const ImColor &c, const char *s,
                         float size) {
  draw::StringScaled(p, (ImU32)c, s, size);
}
inline void StringOutlined(const ImVec2 &p, const ImColor &c, const ImColor &o,
                           const char *s) {
  draw::StringOutlined(p, (ImU32)c, (ImU32)o, s);
}
inline void StringScaledOutlined(const ImVec2 &p, const ImColor &c,
                                 const ImColor &o, const char *s, float size) {
  draw::StringScaledOutlined(p, (ImU32)c, (ImU32)o, s, size);
}
inline void FillRect(const ImVec2 &min, const ImVec2 &max, const ImColor &c) {
  draw::FillRect(min, max, (ImU32)c);
}
inline void FillRectRounded(const ImVec2 &min, const ImVec2 &max,
                            const ImColor &c, float r) {
  draw::FillRectRounded(min, max, (ImU32)c, r);
}
inline void DrawRect(const ImVec2 &min, const ImVec2 &max, const ImColor &c,
                     float r = 0.f, float t = 1.f) {
  draw::DrawRect(min, max, (ImU32)c, r, t);
}

inline ImColor HPColor(float fraction, bool custom = false,
                       ImColor custom_col = ImColor()) {
  if (custom)
    return custom_col;
  if (cfg::esp_custom_health_colors)
    return cfg::col_health;
  int r = (int)((1.f - fraction) * 255);
  int g = (int)(fraction * 255);
  return ImColor(r, g, 0, 200);
}
inline ImColor ArmorColor(bool custom = false, ImColor custom_col = ImColor()) {
  if (custom)
    return custom_col;
  if (cfg::esp_custom_health_colors)
    return cfg::col_armor;
  return ImColor(50, 100, 255, 200);
}

inline void DrawESPBox(const ImVec2 &topLeft, const ImVec2 &bottomRight,
                       const ImColor &color, int type, float thickness = 1.5f,
                       float r = 0.f) {
  switch (type) {
  case 0:
    DrawRect(topLeft, bottomRight, color, r, thickness);
    break;
  case 1: {
    float len =
        std::min(bottomRight.x - topLeft.x, bottomRight.y - topLeft.y) * 0.25f;
    if (r > 0.0f) {
      float current_r = std::min(r, len);
      ImDrawList *dl = ImGui::GetBackgroundDrawList();
      ImU32 col = (ImU32)color;

      // Top Left
      dl->PathLineTo(ImVec2(topLeft.x, topLeft.y + len));
      dl->PathLineTo(ImVec2(topLeft.x, topLeft.y + current_r));
      dl->PathArcToFast(ImVec2(topLeft.x + current_r, topLeft.y + current_r),
                        current_r, 6, 9);
      dl->PathLineTo(ImVec2(topLeft.x + len, topLeft.y));
      dl->PathStroke(col, 0, thickness);

      // Top Right
      dl->PathLineTo(ImVec2(bottomRight.x - len, topLeft.y));
      dl->PathLineTo(ImVec2(bottomRight.x - current_r, topLeft.y));
      dl->PathArcToFast(
          ImVec2(bottomRight.x - current_r, topLeft.y + current_r), current_r,
          9, 12);
      dl->PathLineTo(ImVec2(bottomRight.x, topLeft.y + len));
      dl->PathStroke(col, 0, thickness);

      // Bottom Right
      dl->PathLineTo(ImVec2(bottomRight.x, bottomRight.y - len));
      dl->PathLineTo(ImVec2(bottomRight.x, bottomRight.y - current_r));
      dl->PathArcToFast(
          ImVec2(bottomRight.x - current_r, bottomRight.y - current_r),
          current_r, 0, 3);
      dl->PathLineTo(ImVec2(bottomRight.x - len, bottomRight.y));
      dl->PathStroke(col, 0, thickness);

      // Bottom Left
      dl->PathLineTo(ImVec2(topLeft.x + len, bottomRight.y));
      dl->PathLineTo(ImVec2(topLeft.x + current_r, bottomRight.y));
      dl->PathArcToFast(
          ImVec2(topLeft.x + current_r, bottomRight.y - current_r), current_r,
          3, 6);
      dl->PathLineTo(ImVec2(topLeft.x, bottomRight.y - len));
      dl->PathStroke(col, 0, thickness);
    } else {
      ImVec2 tr(bottomRight.x, topLeft.y);
      ImVec2 bl(topLeft.x, bottomRight.y);
      DrawLine(topLeft, ImVec2(topLeft.x + len, topLeft.y), color, thickness);
      DrawLine(topLeft, ImVec2(topLeft.x, topLeft.y + len), color, thickness);
      DrawLine(tr, ImVec2(tr.x - len, tr.y), color, thickness);
      DrawLine(tr, ImVec2(tr.x, tr.y + len), color, thickness);
      DrawLine(bl, ImVec2(bl.x + len, bl.y), color, thickness);
      DrawLine(bl, ImVec2(bl.x, bl.y - len), color, thickness);
      DrawLine(bottomRight, ImVec2(bottomRight.x - len, bottomRight.y), color,
               thickness);
      DrawLine(bottomRight, ImVec2(bottomRight.x, bottomRight.y - len), color,
               thickness);
    }
    break;
  }
  case 2:
    FillRectRounded(topLeft, bottomRight,
                    ImColor(color.Value.x, color.Value.y, color.Value.z, 0.15f),
                    r);
    break;
  case 3:
    FillRectRounded(topLeft, bottomRight,
                    ImColor(color.Value.x, color.Value.y, color.Value.z, 0.15f),
                    r);
    DrawESPBox(topLeft, bottomRight, color, 1, thickness, r);
    break;
  }
}

inline void DrawSkeleton(CPed &ped, const ImColor &color,
                         const Matrix &viewMatrix, float thickness = 2.0f) {
  using namespace bones;
  struct BonePair {
    int a, b;
  };
  static const BonePair connections[] = {/* {SKEL_Neck, SKEL_Head},  шея → голова */
                                         {SKEL_Neck, SKEL_Spine},
                                         {SKEL_Spine, SKEL_Pelvis},
                                         {SKEL_Neck, SKEL_L_UpperArm},
                                         {SKEL_L_UpperArm, SKEL_L_Forearm},
                                         {SKEL_L_Forearm, SKEL_L_Hand},
                                         {SKEL_Neck, SKEL_R_UpperArm},
                                         {SKEL_R_UpperArm, SKEL_R_Forearm},
                                         {SKEL_R_Forearm, SKEL_R_Hand},
                                         {SKEL_Pelvis, SKEL_L_Thigh},
                                         {SKEL_L_Thigh, SKEL_L_Calf},
                                         {SKEL_L_Calf, SKEL_L_Foot},
                                         {SKEL_Pelvis, SKEL_R_Thigh},
                                         {SKEL_R_Thigh, SKEL_R_Calf},
                                         {SKEL_R_Calf, SKEL_R_Foot}};

  for (auto &p : connections) {
    Vector3 posA = ped.GetBonePosition(p.a);
    Vector3 posB = ped.GetBonePosition(p.b);
    Vector2 outA, outB;
    if (WorldToScreen(viewMatrix, posA, outA) &&
        WorldToScreen(viewMatrix, posB, outB)) {
      ImVec2 sA = ImVec2(outA.x, outA.y);
      ImVec2 sB = ImVec2(outB.x, outB.y);
      DrawLine(sA, sB, color, thickness);
    }
  }
}

inline void DrawHealthArmorBars(const ImVec2 &topLeft,
                                const ImVec2 &bottomRight, float health,
                                float maxHealth, float armor, int healthBarPos,
                                int armorBarPos, bool drawHealth,
                                bool drawArmor, float scaleFactor = 1.0f,
                                ImColor hc = ImColor(0, 255, 0, 255),
                                ImColor ac = ImColor(0, 150, 255, 255),
                                float extraPadding = 0.0f) {
  float boxW = bottomRight.x - topLeft.x;
  float boxH = bottomRight.y - topLeft.y;
  float hpPerc = std::clamp(health / maxHealth, 0.0f, 1.0f);
  float armPerc = std::clamp(armor / 100.0f, 0.0f, 1.0f);
  const float barThickness = std::max(2.0f, std::round(3.0f * scaleFactor));
  const float gap =
      std::max(1.0f, std::round(2.5f * scaleFactor)) + extraPadding;

  auto drawBar = [&](float fraction, const ImColor &col, float offset,
                     int position) {
    ImVec2 p1, p2, bg_p1, bg_p2;
    float r = 2.0f;
    switch (position) {
    case 0:
      bg_p1 = ImVec2(topLeft.x, topLeft.y - barThickness - offset);
      bg_p2 = ImVec2(bottomRight.x, topLeft.y - offset);
      p1 = bg_p1;
      p2 = ImVec2(topLeft.x + boxW * fraction, bg_p2.y);
      break;
    case 1:
      bg_p1 = ImVec2(topLeft.x, bottomRight.y + offset);
      bg_p2 = ImVec2(bottomRight.x, bottomRight.y + barThickness + offset);
      p1 = bg_p1;
      p2 = ImVec2(topLeft.x + boxW * fraction, bg_p2.y);
      break;
    case 2:
      bg_p1 = ImVec2(topLeft.x - barThickness - offset, topLeft.y);
      bg_p2 = ImVec2(topLeft.x - offset, bottomRight.y);
      p1 = ImVec2(bg_p1.x, bottomRight.y - boxH * fraction);
      p2 = bg_p2;
      break;
    case 3:
      bg_p1 = ImVec2(bottomRight.x + offset, topLeft.y);
      bg_p2 = ImVec2(bottomRight.x + barThickness + offset, bottomRight.y);
      p1 = ImVec2(bg_p1.x, bottomRight.y - boxH * fraction);
      p2 = bg_p2;
      break;
    }
    draw::FillRectRounded(bg_p1, bg_p2, IM_COL32(30, 30, 30, 150), r);
    draw::FillRectRounded(p1, p2, (ImU32)col, r);
  };

  float baseOffset = std::max(2.0f, std::round(4.0f * scaleFactor));

  if (drawHealth) {
    bool customHealth =
        (hc.Value.x != 0.0f || hc.Value.y != 1.0f || hc.Value.z != 0.0f) ||
        (hc.Value.x != 0.0f && hc.Value.y != 255.0f && hc.Value.z != 0.0f);
    drawBar(hpPerc, HPColor(hpPerc, customHealth, hc), baseOffset,
            healthBarPos);
  }

  float armOffset = baseOffset;
  if (drawHealth && healthBarPos == armorBarPos)
    armOffset += barThickness + gap;

  if (drawArmor) {
    bool customArmor =
        (ac.Value.x != 0.0f || ac.Value.y != 150.0f / 255.0f ||
         ac.Value.z != 1.0f) ||
        (ac.Value.x != 0.0f && ac.Value.y != 150.0f && ac.Value.z != 255.0f);
    drawBar(armPerc, ArmorColor(customArmor, ac), armOffset, armorBarPos);
  }
}
