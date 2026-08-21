#include "security/xorstr.hpp"
#include "../headers/includes.h"

static ImVec2 ToScreen(const ImRect& r, ImVec2 uv)
{
    return ImVec2(
        r.Min.x + uv.x * r.GetWidth(),
        r.Min.y + (1.0f - uv.y) * r.GetHeight()
    );
}

static ImVec2 FromScreen(const ImRect& r, ImVec2 p)
{
    float x = (p.x - r.Min.x) / r.GetWidth();
    float y = 1.0f - (p.y - r.Min.y) / r.GetHeight();

    return ImVec2(
        ImClamp(x, 0.0f, 1.0f),
        ImClamp(y, 0.0f, 1.0f)
    );
}

bool c_widgets::curve(std::string name, float* first_x, float* first_y, float* second_x, float* second_y)
{
    struct curve_state
    {
        int active_point = -1;

        c_vec2 p0, p1, p2, p3;
        c_vec4 circle[2];

        float first_x, first_y, second_x, second_y;
    };

    c_window* window = gui->get_window();
    c_id id = window->GetID(name.data());
    curve_state* state = gui->anim_container<curve_state>(id);

    c_vec2 pos = window->DC.CursorPos;
    c_vec2 size = { gui->content_avail().x, SCALE(93) };
    c_rect rect = { pos, pos + size };
    c_rect button = { rect.Min + SCALE(0, 17), rect.Max };
    c_rect button_inner = { button.Min + SCALE(10, 10), button.Max - SCALE(10, 10) };

    gui->item_size(rect);
    gui->item_add(rect, id);

    bool hover, held;
    bool click = gui->button_behavior(button, id, &hover, &held);

    draw->text_clipped(var->widget.draw_list, font->get(sf_pro, 12), rect.Min - SCALE(0, 2), rect.Max, draw->get_clr(clr->white), name.data(), 0, 0, { 0, 0 });

    gui->easing(state->first_x, *first_x, 16.f, dynamic_easing);
    gui->easing(state->second_x, *second_x, 16.f, dynamic_easing);
    gui->easing(state->first_y, *first_y, 16.f, dynamic_easing);
    gui->easing(state->second_y, *second_y, 16.f, dynamic_easing);

    char buf1[64], buf2[64], buf3[64], buf4[64];
    gui->get_fmt(buf1, &state->first_x, xorstr_("%.2f"));
    gui->get_fmt(buf2, &state->first_y, xorstr_("%.2f"));
    gui->get_fmt(buf3, &state->second_x, xorstr_("%.2f"));
    gui->get_fmt(buf4, &state->second_y, xorstr_("%.2f"));
    std::string output = xorstr_("( ") + std::string(buf1) + xorstr_(", ") + std::string(buf2) + xorstr_(" )    ( ") + std::string(buf3) + xorstr_(", ") + std::string(buf4) + xorstr_(" )");

    draw->text_clipped(var->widget.draw_list, font->get(sf_pro, 12), rect.Min - SCALE(0, 2), rect.Max, draw->get_clr(clr->white), output.data(), 0, 0, { 1, 0 });

    draw->rect(var->widget.draw_list, button.Min, button.Max, draw->get_clr(clr->widget), SCALE(5));

    const int lines_at_grid_x = 24;
    const int lines_at_grid_y = 7;

    float space_grid_x = button.GetWidth() / (float)lines_at_grid_x;
    float space_grid_y = button.GetHeight() / (float)lines_at_grid_y;

    for (int i = 1; i < lines_at_grid_x; i++)
    {
        float x = button.Min.x + i * space_grid_x;
        x = roundf(x);
        draw->line(var->widget.draw_list,
            { x, button.Min.y },
            { x, button.Max.y },
            draw->get_clr(clr->widget, 0.3f));
    }

    for (int i = 1; i < lines_at_grid_y; i++)
    {
        float y = button.Min.y + i * space_grid_y;
        y = roundf(y);
        draw->line(var->widget.draw_list,
            { button.Min.x, y },
            { button.Max.x, y },
            draw->get_clr(clr->widget, 0.3f));
    }

    *first_x = ImClamp(*first_x, 0.0f, 1.0f);
    *first_y = ImClamp(*first_y, 0.0f, 1.0f);
    *second_x = ImClamp(*second_x, 0.0f, 1.0f);
    *second_y = ImClamp(*second_y, 0.0f, 1.0f);

    ImVec2 uv0 = ImVec2(0.0f, 0.0f);
    ImVec2 uv1 = ImVec2(*first_x, *first_y);
    ImVec2 uv2 = ImVec2(*second_x, *second_y);
    ImVec2 uv3 = ImVec2(1.0f, 1.0f);

    ImVec2 p0 = ToScreen(button_inner, uv0);
    ImVec2 p1 = ToScreen(button_inner, uv1);
    ImVec2 p2 = ToScreen(button_inner, uv2);
    ImVec2 p3 = ToScreen(button_inner, uv3);

    ImVec2 mouse = gui->mouse_pos();
    float point_radius = SCALE(3.0f);
    float point_pick_radius = SCALE(10.0f);

    auto dist2 = [](const ImVec2& a, const ImVec2& b) -> float
    {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        return dx * dx + dy * dy;
    };

    bool changed = false;

    if (hover && gui->mouse_clicked(0))
    {
        float d1 = dist2(mouse, p1);
        float d2 = dist2(mouse, p2);
        float max_dist = point_pick_radius * point_pick_radius;

        if (d1 <= max_dist || d2 <= max_dist)
            state->active_point = (d1 < d2) ? 0 : 1;
    }

    if (state->active_point != -1)
    {
        if (gui->mouse_down(0))
        {
            ImVec2 clamped_mouse = mouse;
            clamped_mouse.x = ImClamp(clamped_mouse.x, button_inner.Min.x, button_inner.Max.x);
            clamped_mouse.y = ImClamp(clamped_mouse.y, button_inner.Min.y, button_inner.Max.y);

            ImVec2 uv = FromScreen(button_inner, clamped_mouse);

            if (state->active_point == 0)
            {
                *first_x = uv.x;
                *first_y = uv.y;
                changed = true;
            }
            else if (state->active_point == 1)
            {
                *second_x = uv.x;
                *second_y = uv.y;
                changed = true;
            }
        }
        else
        {
            state->active_point = -1;
        }
    }

    *first_x = ImClamp(*first_x, 0.0f, 1.0f);
    *first_y = ImClamp(*first_y, 0.0f, 1.0f);
    *second_x = ImClamp(*second_x, 0.0f, 1.0f);
    *second_y = ImClamp(*second_y, 0.0f, 1.0f);

    uv1 = ImVec2(*first_x, *first_y);
    uv2 = ImVec2(*second_x, *second_y);

    p1 = ToScreen(button_inner, uv1);
    p2 = ToScreen(button_inner, uv2);

    gui->easing(state->p0.x, p0.x - window->Pos.x, 16.f, dynamic_easing);
    gui->easing(state->p0.y, p0.y - window->Pos.y, 16.f, dynamic_easing);
    gui->easing(state->p1.x, p1.x - window->Pos.x, 16.f, dynamic_easing);
    gui->easing(state->p1.y, p1.y - window->Pos.y, 16.f, dynamic_easing);
    gui->easing(state->p2.x, p2.x - window->Pos.x, 16.f, dynamic_easing);
    gui->easing(state->p2.y, p2.y - window->Pos.y, 16.f, dynamic_easing);
    gui->easing(state->p3.x, p3.x - window->Pos.x, 16.f, dynamic_easing);
    gui->easing(state->p3.y, p3.y - window->Pos.y, 16.f, dynamic_easing);

    draw->line(var->widget.draw_list, state->p0 + window->Pos, state->p1 + window->Pos, draw->get_clr(clr->widget));
    draw->line(var->widget.draw_list, state->p2 + window->Pos, state->p3 + window->Pos, draw->get_clr(clr->widget));


    var->widget.draw_list->AddBezierCubic(
        state->p0 + window->Pos, state->p1 + window->Pos, state->p2 + window->Pos, state->p3 + window->Pos,
        draw->get_clr(clr->accent),
        1.f,
        SCALE(999)
    );

    var->widget.draw_list->AddCircleFilled(state->p0 + window->Pos, SCALE(3), draw->get_clr(clr->white));
    var->widget.draw_list->AddCircleFilled(state->p3 + window->Pos, SCALE(3), draw->get_clr(clr->white));

    gui->easing(state->circle[0], state->active_point == 0 ? clr->accent.Value : clr->white.Value, 16.f, dynamic_easing);
    gui->easing(state->circle[1], state->active_point == 1 ? clr->accent.Value : clr->white.Value, 16.f, dynamic_easing);

    var->widget.draw_list->AddCircleFilled(
        state->p1  + window->Pos,
        point_radius,
        draw->get_clr(state->circle[0])
    );

    var->widget.draw_list->AddCircleFilled(
        state->p2  + window->Pos,
        point_radius,
        draw->get_clr(state->circle[1])
    );

    if (need_sep())
    {
        widgets->seperator();
    }

    return click || changed;
}
