#include "../headers/includes.h" 

void c_widgets::draw_segment(ImDrawList* draw_list, c_vec2 center, ImU32 col)
{
    float radius = SCALE(6);
    ImVec2 bl = center;
    ImVec2 new_center = ImVec2(bl.x + radius + 0.3f, bl.y - SCALE(2));

    draw_list->PathClear();
    draw_list->PathArcTo(new_center, radius, IM_PI * 0.55f, IM_PI, SCALE(999));
    draw_list->PathStroke(col, 0, 1.0f);   
}
