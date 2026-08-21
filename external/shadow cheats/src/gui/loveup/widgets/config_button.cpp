#include "security/xorstr.hpp"
#include "../headers/includes.h"

struct config_button_state_t {
    c_vec4 border;
    c_vec4 text;
    float anim_offset = 0.f;
    bool is_delete = false;
    std::string bound_name;
};

config_button_t c_widgets::config_button(int idx, std::string name, std::string date, bool loaded)
{
    c_window* window = gui->get_window();
    c_id id = window->GetID((name + xorstr_("##config_button")).c_str());
    config_button_state_t* state = gui->anim_container<config_button_state_t>(id);

    // If this state container is new or rebound to a different name, initialize it cleanly
    if (state->bound_name != name) {
        state->bound_name = name;
        state->is_delete = false;
        state->anim_offset = 0.f;
        state->border = loaded ? clr->accent.Value : clr->widget.Value;
        state->text = loaded ? clr->white.Value : clr->text.Value;
    }

    c_vec2 pos = window->DC.CursorPos;

    c_vec2 size = { gui->content_avail().x, SCALE(57) * state->anim_offset };
    c_rect rect = { pos, pos + size };
    c_rect del_rect = { rect.GetTR() - SCALE(25, -15), rect.GetTR() - SCALE(15, -25) };
    c_rect edit_rect = { del_rect.GetTL() - SCALE(25, 0), del_rect.GetBL() - SCALE(15, 0) };
    c_rect download_rect = { edit_rect.GetTL() - SCALE(25, 0), edit_rect.GetBL() - SCALE(15, 0) };
    c_rect share_rect = { download_rect.GetTL() - SCALE(25, 0), download_rect.GetBL() - SCALE(15, 0) };

    gui->item_size(rect);
    gui->item_add(rect, id);

    name = utf8_safe_truncate(name, 20);

    gui->easing(state->border, loaded ? clr->accent.Value : clr->widget.Value, 24.f, dynamic_easing);
    gui->easing(state->text, loaded ? clr->white.Value : clr->text.Value, 24.f, dynamic_easing);
    gui->easing(state->anim_offset, state->is_delete ? -0.02f : 1.02f, 16.f, dynamic_easing);
    state->anim_offset = ImClamp(state->anim_offset, 0.f, 1.f);

    bool pressed = gui->button_behavior(rect, id, nullptr, nullptr);

    gui->push_var(style_var_alpha, state->anim_offset * var->style.alpha);

    draw->push_clip_rect(var->widget.draw_list, rect.Min, rect.Max, true);

    draw->rect(var->widget.draw_list, rect.Min, rect.Max, draw->get_clr(state->border), SCALE(8));

    draw->text_clipped(var->widget.draw_list, font->get(icons, 10), del_rect.Min, del_rect.Max, draw->get_clr(state->text), xorstr_("^"), 0, 0, { 0.5, 0.5 });
    draw->text_clipped(var->widget.draw_list, font->get(icons, 10), edit_rect.Min, edit_rect.Max, draw->get_clr(state->text), xorstr_("`"), 0, 0, { 0.5, 0.5 });
    draw->text_clipped(var->widget.draw_list, font->get(icons, 10), download_rect.Min, download_rect.Max, draw->get_clr(state->text), xorstr_("F"), 0, 0, { 0.5, 0.5 });
    draw->text_clipped(var->widget.draw_list, font->get(icons, 10), share_rect.Min, share_rect.Max, draw->get_clr(state->text), xorstr_("Y"), 0, 0, { 0.5, 0.5 });

    draw->text_clipped(var->widget.draw_list, font->get(sf_pro, 12), rect.Min + SCALE(15, 14), rect.Max, draw->get_clr(state->text), name.data(), 0, 0, { 0, 0 });

    draw->text_clipped(var->widget.draw_list, font->get(sf_pro, 12), rect.Min + SCALE(15, 0), rect.Max - SCALE(0, 14), draw->get_clr(clr->text), date.data(), 0, 0, { 0, 1 });

    bool del_pressed = del_rect.Contains(gui->mouse_pos()) && pressed;
    bool edit_pressed = edit_rect.Contains(gui->mouse_pos()) && pressed;
    bool download_pressed = download_rect.Contains(gui->mouse_pos()) && pressed;
    bool share_pressed = share_rect.Contains(gui->mouse_pos()) && pressed;

    if (del_pressed)
    {
        state->is_delete = true;
    }

    pressed = pressed && !del_pressed && !edit_pressed && !download_pressed && !share_pressed;

    gui->dummy({ gui->content_avail().x, SCALE(10) * state->anim_offset });

    draw->pop_clip_rect(var->widget.draw_list);
    gui->pop_var();

    bool is_del_finished = (state->anim_offset <= 0.05f && state->is_delete);
    if (is_del_finished) {
        state->is_delete = false;
        state->anim_offset = 0.f;
    }

    return { pressed, share_pressed, download_pressed, edit_pressed, is_del_finished };
}
