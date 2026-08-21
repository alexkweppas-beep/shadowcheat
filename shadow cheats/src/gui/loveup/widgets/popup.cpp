#include "../headers/includes.h"

int get_popup_idx(std::string name)
{
    int idx = -1;
    for (int i = 0; i < widgets->popup_storage.size(); ++i)
    {
        if (name == widgets->popup_storage[i].name && 
            std::to_string(gui->get_window()->ID) == std::to_string(widgets->popup_storage[i].owner_window_id)
        ) {
            idx = i;
            break;
        }
    }
    return idx;
}

void c_widgets::open_popup(std::string name)
{
    int idx = get_popup_idx(name);
    if (idx == -1)
    {
        widgets->popup_storage.push_back({ name, gui->get_window()->ID, true, 0.f });
    }
    else
    {
        widgets->popup_storage[idx].open = true;
    }
}

void c_widgets::close_popup(std::string name)
{
    int idx = get_popup_idx(name);
    if (idx != -1)
    {
        widgets->popup_storage[idx].open = false;
    }
}

bool c_widgets::is_popup_open(std::string name)
{
    int idx = get_popup_idx(name);
    if (idx == -1)
    {
        return false;
    }
    else
    {
        return widgets->popup_storage[idx].alpha > 0;
    }
}

bool c_widgets::popup(std::string name, c_vec2 size, c_vec2 pos, c_vec2 padding, c_vec2 spacing, float rounding, bool always_on_screen, window_flags flags)
{
    int idx = get_popup_idx(name);
    if (idx == -1)
    {
        return false;
    }

    gui->easing(widgets->popup_storage[idx].alpha, widgets->popup_storage[idx].open ? 1.f : 0.f, 7.f, static_easing);

    if (widgets->popup_storage[idx].alpha > 0)
    {
        var->blackout.active = true;

        gui->push_var(style_var_alpha, widgets->popup_storage[idx].alpha);
        gui->push_var(style_var_window_padding, padding);
        gui->push_var(style_var_item_spacing, spacing);

        gui->set_next_window_pos(gui->adjust_window_pos(pos, size), gui_cond_always);
        gui->set_next_window_size(size, gui_cond_always);

        gui->begin(name, nullptr, flags | window_flags_no_background | window_flags_no_decoration | (size.y == 0 ? window_flags_always_auto_resize : 0));

        c_window* window = gui->get_window();
        c_rect rect = window->Rect();

        draw_background_blur(window->DrawList, var->dx.swap_chain, var->dx.device, var->dx.ctx, rect.Min, rect.Max, rounding);
        draw->rect_filled(window->DrawList, rect.Min, rect.Max, draw->get_clr(clr->popup, 0.8f), rounding);

        if (!always_on_screen)
        {
            gui->set_window_focus();

            if (widgets->popup_storage[idx].alpha > 0.5f)
            {
                if (!gui->get_window()->Rect().Contains(gui->mouse_pos()) && (gui->mouse_clicked(0) || ImGui::GetIO().MouseWheel != 0))
                {
                    widgets->popup_storage[idx].open = false;
                }
            }
        }
    }

    return widgets->popup_storage[idx].alpha > 0;
}

void c_widgets::end_popup()
{
    gui->end();
    gui->pop_var(3);
}
