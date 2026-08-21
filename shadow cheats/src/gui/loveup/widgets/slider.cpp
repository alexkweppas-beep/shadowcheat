#include "../headers/includes.h"

struct slider_state_t {
    float callback;
    float anim_offset;
    c_vec4 text;
};

bool slider_ex(std::string name, float* callback, float min, float max, std::string format)
{
    c_window* window = gui->get_window();
    c_id id = window->GetID(name.data());
    slider_state_t* state = gui->anim_container<slider_state_t>(id);

    c_vec2 pos = window->DC.CursorPos;
    c_vec2 size = { gui->content_avail().x, SCALE(20) };
    c_rect rect = { pos, pos + size };
    c_rect button = { rect.Max.x - SCALE(85), rect.GetCenter().y - SCALE(4), rect.Max.x, rect.GetCenter().y + SCALE(4) };

    gui->item_size(rect);
    gui->item_add(rect, id);

    bool hover = rect.Contains(gui->mouse_pos()) && gui->is_window_hovered(0);
    bool held, pressed = gui->button_behavior(button, id, nullptr, &held);

    float normalized = ImSaturate((gui->mouse_pos().x - button.Min.x) / button.GetWidth());

    if (held)
    {
        *callback = min + (max - min) * normalized;
    }

    float anim_offset = ImSaturate((*callback - min) / (max - min));
    gui->easing(state->anim_offset, anim_offset, 16.f, dynamic_easing);
    gui->easing(state->callback, *callback, 16.f, dynamic_easing);
    gui->easing(state->text, held ? clr->white.Value : clr->text.Value, 16.f, dynamic_easing);

    draw->text_clipped(var->widget.draw_list, font->get(sf_pro, 12), rect.Min, rect.Max, draw->get_clr(state->text), name.data(), 0, 0, { 0, 0.5 });
    
    char buf[64];
    gui->get_fmt(buf, &state->callback, format);

    draw->text_clipped(var->widget.draw_list, font->get(sf_pro, 12), rect.Min, { button.Min.x - SCALE(10), rect.Max.y }, draw->get_clr(clr->white), buf, 0, 0, { 1, 0.5 });
    
    draw->rect_filled(var->widget.draw_list, button.Min, button.Max, draw->get_clr(clr->widget), SCALE(999));
    
    float space = button.GetWidth() / 9;
    float x = space;
    for (int i = 0; i < 8; i++)
    {
        draw->line(var->widget.draw_list, button.Min + c_vec2{ x, 0 }, button.Min + c_vec2{ x, button.GetHeight() }, draw->get_clr(clr->border, 0.5f));
        x += space;
    }

    float clip_x = button.GetWidth() * state->anim_offset - SCALE(5);
    clip_x = ImClamp(clip_x, 0.f, button.GetWidth() - SCALE(8));
    clip_x = roundf(clip_x);

    draw->push_clip_rect(var->widget.draw_list, button.Min, button.Min + c_vec2{ clip_x, button.GetHeight() }, true);
    draw->rect_filled(var->widget.draw_list, button.Min, button.Max, draw->get_clr(clr->accent), SCALE(999));
    draw->rect_filled_multi_color(var->widget.draw_list, button.Min, button.Max, draw->get_clr(clr->black, 0.f), draw->get_clr(clr->black, 0.3f), draw->get_clr(clr->black, 0.3f), draw->get_clr(clr->black, 0.f), SCALE(999));
    draw->pop_clip_rect(var->widget.draw_list);

    float grab_x = button.GetWidth() * state->anim_offset;
    grab_x = ImClamp(grab_x, SCALE(3), button.GetWidth() - SCALE(3));
    grab_x = roundf(grab_x);

    draw->rect_filled(var->widget.draw_list, button.Min + c_vec2{ grab_x - SCALE(3), SCALE(-2) }, button.Min + c_vec2{ grab_x + SCALE(3), SCALE(10) }, draw->get_clr(clr->white), SCALE(2));

    if (need_sep())
    {
        widgets->seperator();
    }

    return held;
}

bool c_widgets::slider(std::string name)
{
    slider_t* data = menu_cfg->fill<slider_t>(name);
    return slider_ex(data->name, &data->callback, data->min, data->max, data->format);
}
