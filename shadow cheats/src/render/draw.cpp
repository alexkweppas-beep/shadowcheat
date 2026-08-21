#include "draw.hpp"
#include <algorithm>

namespace
{
    inline ImDrawList* dl() { return ImGui::GetBackgroundDrawList(); }
}

namespace draw
{
    void DrawLine(const ImVec2& a, const ImVec2& b, ImU32 c, float t) { dl()->AddLine(a, b, c, t); }
    void DrawRect(const ImVec2& mn, const ImVec2& mx, ImU32 c, float r, float t) { dl()->AddRect(mn, mx, c, r, 0, t); }
    void FillRect(const ImVec2& mn, const ImVec2& mx, ImU32 c) { dl()->AddRectFilled(mn, mx, c); }
    void FillRectRounded(const ImVec2& mn, const ImVec2& mx, ImU32 c, float r) { dl()->AddRectFilled(mn, mx, c, r); }

    void String(const ImVec2& p, ImU32 c, const char* s) { dl()->AddText(p, c, s); }
    void StringScaled(const ImVec2& p, ImU32 c, const char* s, float size) { dl()->AddText(ImGui::GetFont(), size, p, c, s); }

    void StringOutlined(const ImVec2& p, ImU32 c, ImU32 o, const char* s)
    {
        dl()->AddText(ImVec2(p.x - 1, p.y), o, s);
        dl()->AddText(ImVec2(p.x + 1, p.y), o, s);
        dl()->AddText(ImVec2(p.x, p.y - 1), o, s);
        dl()->AddText(ImVec2(p.x, p.y + 1), o, s);
        dl()->AddText(p, c, s);
    }

    void StringScaledOutlined(const ImVec2& p, ImU32 c, ImU32 o, const char* s, float size)
    {
        ImFont* font = ImGui::GetFont();
        dl()->AddText(font, size, ImVec2(p.x - 1, p.y), o, s);
        dl()->AddText(font, size, ImVec2(p.x + 1, p.y), o, s);
        dl()->AddText(font, size, ImVec2(p.x, p.y - 1), o, s);
        dl()->AddText(font, size, ImVec2(p.x, p.y + 1), o, s);
        dl()->AddText(font, size, p, c, s);
    }

    void HealthBar(float x, float y, float w, float h, float cur, float max)
    {
        if (max <= 0.f) return;
        const float ratio = std::clamp(cur / max, 0.f, 1.f);
        FillRect(ImVec2(x, y),         ImVec2(x + w, y + h),         IM_COL32(0, 0, 0, 200));
        DrawRect(ImVec2(x, y),         ImVec2(x + w, y + h),         IM_COL32(0, 0, 0, 255));
        FillRect(ImVec2(x, y + h*(1-ratio)), ImVec2(x + w, y + h),   IM_COL32((int)(255*(1-ratio)), (int)(255*ratio), 0, 255));
    }

    void ArmorBar(float x, float y, float w, float h, float cur, float max)
    {
        if (max <= 0.f) return;
        const float ratio = std::clamp(cur / max, 0.f, 1.f);
        FillRect(ImVec2(x, y),         ImVec2(x + w, y + h),         IM_COL32(0, 0, 0, 200));
        DrawRect(ImVec2(x, y),         ImVec2(x + w, y + h),         IM_COL32(0, 0, 0, 255));
        FillRect(ImVec2(x, y + h*(1-ratio)), ImVec2(x + w, y + h),   IM_COL32(80, 160, 255, 255));
    }
}
