#include "security/xorstr.hpp"
#include "../headers/includes.h"

struct selectable_state_t {
    c_vec4 text;
    float alpha;
};

bool selectable_ex(std::string name, bool is_selected)
{
    c_window* window = gui->get_window();
    c_id id = window->GetID(name.data());
    selectable_state_t* state = gui->anim_container<selectable_state_t>(id);
    
    c_vec2 pos = window->DC.CursorPos;
    c_vec2 size = { gui->content_avail().x, SCALE(24) };
    c_rect rect = { pos, pos + size };

    gui->item_size(rect);
    gui->item_add(rect, id);

    bool pressed = gui->button_behavior(rect, id, nullptr, nullptr);

    gui->easing(state->alpha, is_selected ? 1.02f : -0.02f, 16.f, dynamic_easing);
    state->alpha = ImClamp(state->alpha, 0.f, 1.f);
    gui->easing(state->text, is_selected ? clr->white.Value : clr->text.Value, 16.f, dynamic_easing);

    draw->rect_filled(window->DrawList, rect.Min, rect.Max, draw->get_clr(clr->popup_widget, state->alpha), SCALE(5));

    draw->text_clipped(window->DrawList, font->get(sf_pro, 12), rect.Min + SCALE(6, 0), rect.Max, draw->get_clr(state->text), name.data(), 0, 0, { 0, 0.5 });

    draw->text_clipped(window->DrawList, font->get(icons, 12), rect.Min, rect.Max - SCALE(6 * state->alpha, 0), draw->get_clr(clr->white, state->alpha), xorstr_("T"), 0, 0, { 1, 0.5 });

    return pressed;
}

bool c_widgets::selectable(std::string name, bool is_selected)
{
    return selectable_ex(name, is_selected);
}

struct dropdown_state_t {
    c_vec4 text;
    float anim_offset;
};

bool dropdown_ex(std::string name, std::string preview)
{
    c_window* window = gui->get_window();
    c_id id = window->GetID(name.data());
    dropdown_state_t* state = gui->anim_container<dropdown_state_t>(id);

    c_vec2 pos = window->DC.CursorPos;
    c_vec2 size = { gui->content_avail().x, SCALE(47) };
    c_rect rect = { pos, pos + size };
    c_rect button = { rect.GetBL() - SCALE(0, 30), rect.Max };

    gui->item_size(rect);
    gui->item_add(rect, id);

    bool pressed = gui->button_behavior(button, id, nullptr, nullptr);

    std::string popup_name = name + xorstr_("##popup");

    if (pressed)
    {
        widgets->open_popup(popup_name);
    }

    bool is_active = widgets->is_popup_open(popup_name);

    gui->easing(state->text, is_active ? clr->white.Value : clr->text.Value, 16.f, dynamic_easing);
    gui->easing(state->anim_offset, is_active ? 1.02f : -0.02f, 16.f, dynamic_easing);
    state->anim_offset = ImClamp(state->anim_offset, 0.f, 1.f);

    draw->text_clipped(var->widget.draw_list, font->get(sf_pro, 12), rect.Min - SCALE(0, 2), rect.Max, draw->get_clr(state->text), name.data(), 0, 0, { 0, 0 });

    draw->rect_filled(var->widget.draw_list, button.Min, button.Max, draw->get_clr(clr->widget), SCALE(7));

    preview = utf8_safe_truncate(preview, 50);
    draw->text_clipped(var->widget.draw_list, font->get(sf_pro, 12), button.Min + SCALE(10, 0), button.Max, draw->get_clr(clr->white), preview.data(), 0, 0, { 0, 0.5 });

    c_rect arrow = { button.GetTR() - SCALE(32, 0), button.Max };

    draw->rotate_start(var->widget.draw_list);
    draw->text_clipped(var->widget.draw_list, font->get(icons, 12), arrow.Min, arrow.Max, draw->get_clr(clr->white), xorstr_("N"), 0, 0, { 0.5, 0.5 });
    draw->rotate_end(var->widget.draw_list, 180.f * state->anim_offset, arrow.GetCenter());

    if (need_sep())
    {
        widgets->seperator();
    }
    
    if (widgets->popup(popup_name, { button.GetWidth(), 0 }, button.GetBL() + SCALE(0, 4), SCALE(6, 6), SCALE(6, 6), SCALE(7)))
    {
        return true;
    }
    return false;
}

bool c_widgets::dropdown(std::string name)
{
    dropdown_t* data = menu_cfg->fill<dropdown_t>(name);

    bool value_changed = false;

    if (dropdown_ex(data->name, data->variants[data->callback]))
    {
        for (int i = 0; i < data->variants.size(); i++)
        {
            if (widgets->selectable(data->variants[i], data->callback == i))
            {
                data->callback = i;
                value_changed = true;
            }
        }

        widgets->end_popup();
    }

    if (value_changed)
    {
        widgets->close_popup(data->name + xorstr_("##popup"));
    }

    return value_changed;
}


bool c_widgets::multi_dropdown(std::string name)
{
    multi_dropdown_t* data = menu_cfg->fill<multi_dropdown_t>(name);

    bool value_changed = false;

    std::string preview = xorstr_("None");

    for (int i = 0; i < data->variants.size(); i++)
    {
        if (data->callback[i])
        {
            if (preview == xorstr_("None"))
                preview = data->variants[i];
            else
                preview += (xorstr_(", ")) + data->variants[i];
        }
    }

    if (dropdown_ex(data->name, preview))
    {
        for (int i = 0; i < data->variants.size(); i++)
        {
            if (widgets->selectable(data->variants[i], data->callback[i]))
            {
                data->callback[i] = !data->callback[i];
                value_changed = true;
            }
        }

        widgets->end_popup();
    }

    return value_changed;
}
