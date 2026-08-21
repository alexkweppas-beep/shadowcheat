#include "../headers/includes.h"

struct button_state_t {
    c_vec4 bg;
    c_vec4 text;
};

bool c_widgets::button(std::string name)
{
    c_window* window = gui->get_window();
    c_id id = window->GetID(name.data());
    button_state_t* state = gui->anim_container<button_state_t>(id);

    c_vec2 pos = window->DC.CursorPos;
    c_vec2 size = { gui->content_avail().x, SCALE(30) };
    c_rect rect = { pos, pos + size };

    gui->item_size(rect);
    gui->item_add(rect, id);

    bool hover, held, pressed = gui->button_behavior(rect, id, &hover, &held);

    gui->easing(state->bg, held ? clr->accent.Value : hover ? clr->border.Value : clr->widget.Value, 16.f, dynamic_easing);
    gui->easing(state->text, held ? clr->white.Value : clr->text.Value, 16.f, dynamic_easing);

    draw->rect_filled(var->widget.draw_list, rect.Min, rect.Max, draw->get_clr(state->bg), SCALE(4));
    draw->text_clipped(var->widget.draw_list, font->get(sf_pro, 14), rect.Min, rect.Max, draw->get_clr(state->text), name.data(), 0, 0, { 0.5, 0.5 });

    if (need_sep())
    {
        widgets->seperator();
    }

    return pressed;
}
