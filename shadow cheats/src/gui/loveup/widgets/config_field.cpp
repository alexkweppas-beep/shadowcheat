#include "security/xorstr.hpp"
#include "../headers/includes.h"

bool config_field_ex(std::string name, char* buf, int buf_size, bool force_click = false) 
{
    struct text_field_state
    {
        bool active = false;
        float alpha = 1.f;
        ImVec4 text = clr->text.Value;
        ImVec4 bg = clr->child.Value;
    };

    c_window* window = gui->get_window();
    c_id id = window->GetID(name.data());

    text_field_state* state = gui->anim_container<text_field_state>(
        window->GetID((std::stringstream{} << id << xorstr_("text_field_state")).str().data())
    );

    c_vec2 pos = window->DC.CursorPos;
    c_vec2 size = SCALE(250, 37);

    c_rect rect = { pos, pos + size };
    c_rect field = { rect.Min + SCALE(48, 0), rect.Max };

    gui->item_size(rect); 
    gui->item_add(rect, id);

    bool user_clicked =
        force_click ||
        rect.Contains(gui->mouse_pos()) &&
        gui->mouse_clicked(0) &&
        gui->is_window_hovered(0);

    gui->easing(state->text, state->active ? clr->white.Value : clr->text.Value, 24.f, dynamic_easing);
    gui->easing(state->alpha, strlen(buf) > 0 ? 0.f : 1.f, 24.f, dynamic_easing);
    gui->easing(state->bg, rect.Contains(gui->mouse_pos()) || state->active ? clr->child.Value : clr->child.Value, 24.f, dynamic_easing);

    draw->rect_filled(window->DrawList, rect.Min, rect.Max, draw->get_clr(state->bg), SCALE(10));

    c_vec2 center = { rect.Min.x + SCALE(21), rect.GetCenter().y };

    draw->text_clipped(window->DrawList, font->get(icons, 12), rect.Min + SCALE(15, 0), rect.Max, draw->get_clr(clr->white), xorstr_("P"), 0, 0, { 0, 0.5 });

    draw->line(window->DrawList, { rect.Min.x + SCALE(37), rect.GetCenter().y - SCALE(4) }, { rect.Min.x + SCALE(37), rect.GetCenter().y + SCALE(4) }, draw->get_clr(clr->border));

    draw->text_clipped(
        window->DrawList,
        font->get(sf_pro, 12),
        field.Min + ImVec2((size.x / 2) * (1.f - state->alpha), 0),
        rect.Max,
        draw->get_clr(clr->text, state->alpha),
        name.data(),
        0,
        0,
        { 0, 0.5 }
    );

    gui->push_var(style_var_frame_rounding, 0);
    gui->push_font(font->get(sf_pro, 12));

    bool ret = widgets->text_field_ex(
        name,
        "",
        buf,
        buf_size,
        0,
        SCALE(10),
        draw->get_clr(state->text),
        false,
        field,
        &state->active,
        ImGuiInputTextFlags_EnterReturnsTrue,
        0,
        0,
        user_clicked
    );

    if (ret) state->active = false;

    gui->pop_font();
    gui->pop_var();

    return ret;
}

bool c_widgets::config_field(std::string name, char* buf, size_t buf_size)
{
    return config_field_ex(name, buf, buf_size, false);
}
