#include "security/xorstr.hpp"
#include "../headers/includes.h"
#include "../../../config/settings.hpp"

struct mode_button_state_t {
    c_vec4 toggle_col;
    c_vec4 hold_col;
    float anim_offset;
};

bool mode_button(std::string name, int* mode)
{
    c_window* window = gui->get_window();
    c_id id = window->GetID(name.data());
    mode_button_state_t* state = gui->anim_container<mode_button_state_t>(id);

    c_vec2 pos = window->DC.CursorPos;
    c_vec2 size = { gui->content_avail().x, SCALE(30) };
    c_rect rect = { pos, pos + size };
    c_rect toggle_rect = { rect.Min, rect.Min + SCALE(85, 30) };
    c_rect hold_rect = { rect.Max - SCALE(85, 30), rect.Max };

    gui->item_size(rect);
    gui->item_add(rect, id);

    bool pressed = gui->button_behavior(rect, id, nullptr, nullptr);

    if (pressed)
    {
        if (toggle_rect.Contains(gui->mouse_pos()))
            *mode = 0;
        else
            *mode = 1;
    }

    gui->easing(state->anim_offset, *mode == 0 ? 0.f : 85.f, 16.f, dynamic_easing);
    gui->easing(state->hold_col, *mode == 1 ? clr->black.Value : clr->white.Value, 16.f, dynamic_easing);
    gui->easing(state->toggle_col, *mode == 0 ? clr->black.Value : clr->white.Value, 16.f, dynamic_easing);

    draw->rect_filled(window->DrawList, rect.Min, rect.Max, draw->get_clr(clr->popup_widget), SCALE(5));
    draw->rect_filled(window->DrawList, rect.Min + SCALE(state->anim_offset, 0.f), rect.Min + SCALE(85 + state->anim_offset, 30), draw->get_clr(clr->accent), SCALE(5));

    draw->text_clipped(window->DrawList, font->get(sf_pro, 12), toggle_rect.Min, toggle_rect.Max, draw->get_clr(state->toggle_col), xorstr_("Toggle"), 0, 0, { 0.5, 0.5 });
    draw->text_clipped(window->DrawList, font->get(sf_pro, 12), hold_rect.Min, hold_rect.Max, draw->get_clr(state->hold_col), xorstr_("Hold"), 0, 0, { 0.5, 0.5 });

    return pressed;
}

struct keybind_button_t {
    bool value_changed;
    bool add_pressed;
};

struct keybind_button_state_t {
    bool active;
    float timer;
};

keybind_button_t keybind_button(std::string name, int* key)
{
    c_window* window = gui->get_window();
    c_id id = window->GetID(name.data());
    keybind_button_state_t* state = gui->anim_container<keybind_button_state_t>(id);

    c_vec2 pos = window->DC.CursorPos;
    c_vec2 size = { gui->content_avail().x, SCALE(30) };
    c_rect rect = { pos, pos + size };
    c_rect key_rect = { rect.Min, rect.Max - SCALE(30, 0) };
    c_rect add_rect = { rect.Max - SCALE(30, 30), rect.Max };

    gui->item_size(rect);
    gui->item_add(rect, id);

    bool pressed = gui->button_behavior(rect, id, nullptr, nullptr);
    bool key_pressed = key_rect.Contains(gui->mouse_pos()) && pressed;
    bool add_pressed = add_rect.Contains(gui->mouse_pos()) && pressed;

    if (key_pressed && !state->active)
    {
        state->active = true;
        state->timer = ImGui::GetTime();
    }
    else if (!key_rect.Contains(gui->mouse_pos()) && GetIO().MouseClicked[0]) {
        state->active = false;
    }

    if (state->active) 
    {
        if (ImGui::GetTime() - state->timer > 0.1f) 
        {
            for (int i = 1; i < 256; ++i) 
            {
                if (GetAsyncKeyState(i) & 0x8000) 
                {
                    // Block keys that conflict with system/overlay functions
                    if (i == VK_ESCAPE) {
                        *key = 0;
                        state->active = false;
                        break;
                    }
                    // Skip ToggleMenuKey itself to prevent self-capture crash
                    if (i == cfg::ToggleMenuKey) continue;
                    // Skip other problematic system keys
                    if (i == VK_SNAPSHOT || i == VK_PAUSE || i == VK_NUMLOCK ||
                        i == VK_SCROLL || i == VK_CAPITAL) continue;
                    *key = i;
                    state->active = false;
                    break;
                }
            }
        }
    }

    draw->rect_filled(window->DrawList, rect.Min, rect.Max, draw->get_clr(clr->popup_widget), SCALE(5));

    draw->text_clipped(window->DrawList, font->get(sf_pro, 12), rect.Min + SCALE(10, 0), rect.Max, draw->get_clr(clr->white), state->active ? xorstr_("...") : get_key_name_vk(*key).data(), 0, 0, { 0, 0.5 });

    draw->text_clipped(window->DrawList, font->get(icons, 10), add_rect.Min, add_rect.Max, draw->get_clr(clr->white), xorstr_("c"), 0, 0, { 0.5, 0.5 });

    return { key_pressed, add_pressed };
}

struct keybind_nav_state_t {
    float anim_offset;
};

bool keybind_nav(std::string name, std::string icon, c_vec4 text_col, int btn_type, bool is_active)
{
    c_window* window = gui->get_window();
    c_id id = window->GetID(name.data());
    keybind_nav_state_t* state = gui->anim_container<keybind_nav_state_t>(id);

    c_vec2 pos = window->DC.CursorPos;
    c_vec2 size = { gui->content_avail().x, SCALE(24) };
    c_rect rect = { pos, pos + size };

    gui->item_size(rect);
    gui->item_add(rect, id);

    bool pressed = gui->button_behavior(rect, id, nullptr, nullptr);

    gui->easing(state->anim_offset, is_active ? 1.02f : -0.02f, 16.f, dynamic_easing);
    state->anim_offset = ImClamp(state->anim_offset, 0.f, 1.f);

    if (btn_type == 0)
    {
        draw->rect_filled(window->DrawList, rect.Min, rect.Max, draw->get_clr(clr->popup_widget, state->anim_offset), SCALE(5));
    }

    draw->text_clipped(window->DrawList, font->get(icons, 12), rect.Min + SCALE(6, 0), rect.Max, draw->get_clr(text_col), icon.data(), 0, 0, { 0, 0.5 });
    draw->text_clipped(window->DrawList, font->get(sf_pro, 12), rect.Min + SCALE(30, 0), rect.Max, draw->get_clr(text_col), name.data(), 0, 0, { 0, 0.5 });

    if (btn_type == 1)
    {
        draw->text_clipped(window->DrawList, font->get(icons, 12), rect.Min, rect.Max - SCALE(6 * state->anim_offset, 0), draw->get_clr(clr->white, state->anim_offset), xorstr_("T"), 0, 0, { 1, 0.5 });
    }

    return pressed;
}

struct keybind_list_button_t {
    bool pressed;
    bool del;
};

struct keybind_list_button_state_t {
    float anim_offset;
    float alpha;
    c_vec4 trash;
    bool is_delete;
};

keybind_list_button_t keybind_list_button(int idx, std::string key, bool is_selected, bool delete_pressed)
{
    c_window* window = gui->get_window();
    c_id id = window->GetID(std::string(std::to_string(idx) + xorstr_("##keybind_list_button")).data());
    keybind_list_button_state_t* state = gui->anim_container<keybind_list_button_state_t>(id);

    c_vec2 pos = window->DC.CursorPos;
    c_vec2 size = { gui->content_avail().x, 0.01f + SCALE(24) * state->anim_offset };
    c_rect rect = { pos, pos + size };
    c_rect del_rect = { rect.GetTR() - SCALE(24, 0), rect.Max };

    gui->item_size(rect);
    gui->item_add(rect, id);

    bool pressed = gui->button_behavior(rect, id, nullptr, nullptr);
    bool del_pressed = del_rect.Contains(gui->mouse_pos()) && pressed;

    if (del_pressed || delete_pressed)
    {
        state->is_delete = true;
    }

    gui->easing(state->anim_offset, state->is_delete ? -0.02f : 1.02f, 16.f, dynamic_easing);
    state->anim_offset = ImClamp(state->anim_offset, 0.f, 1.f);

    gui->easing(state->alpha, is_selected ? 1.02f : -0.02f, 16.f, dynamic_easing);
    state->alpha = ImClamp(state->alpha, 0.f, 1.f);

    gui->easing(state->trash, del_rect.Contains(gui->mouse_pos()) ? clr->red.Value : clr->text.Value, 16.f, dynamic_easing);

    gui->push_var(style_var_alpha, state->anim_offset * var->style.alpha);

    draw->rect_filled(window->DrawList, rect.Min, rect.Max, draw->get_clr(clr->popup_widget, state->alpha), SCALE(5));

    draw->text_clipped(window->DrawList, font->get(icons, 12), rect.Min + SCALE(6, 0), rect.Max, draw->get_clr(clr->white), xorstr_("["), 0, 0, { 0, 0.5 });
    draw->text_clipped(window->DrawList, font->get(sf_pro, 12), rect.Min + SCALE(30, 0), rect.Max, draw->get_clr(clr->white), key.data(), 0, 0, { 0, 0.5 });
    draw->text_clipped(window->DrawList, font->get(icons, 12), rect.Min, rect.Max - SCALE(6, 0), draw->get_clr(state->trash), xorstr_("^"), 0, 0, { 1, 0.5 });

    gui->pop_var();

    return { pressed && !del_rect.Contains(gui->mouse_pos()), state->anim_offset == 0.f && state->is_delete };
}

struct keybind_state_t {
    bool is_change_hotkey_open;
    int key;
    int mode;
    float anim_offset;
    int selected_keybind_idx = -1;
    int selected_keybind_idx_to_del = -1;
    bool favorites;
    bool hide_overlay;
};

void c_widgets::keybind(std::string name, c_rect rect, bool pressed, std::vector<keybind_t>* keybinds_list)
{
    c_window* window = gui->get_window();
    c_id id = window->GetID(name.data());
    keybind_state_t* state = gui->anim_container<keybind_state_t>(id);

    gui->easing(state->anim_offset, keybinds_list->size() > 0 ? 1.02f : -0.02f, 16.f, dynamic_easing);
    state->anim_offset = ImClamp(state->anim_offset, 0.f, 1.f);

    if (rect.Contains(gui->mouse_pos()) && pressed)
        widgets->open_popup(name);

    if (widgets->popup(name, SCALE(160, 0), rect.Min, SCALE(6, 0), SCALE(0, 0), SCALE(10), state->is_change_hotkey_open, window_flags_no_scrollbar | window_flags_no_scroll_with_mouse))
    {
        gui->dummy({ gui->content_avail().x, SCALE(6 * state->anim_offset) });

        int remove_index = -1;

        for (int i = 0; i < keybinds_list->size(); i++)
        {
            auto& bind = keybinds_list->at(i);

            keybind_list_button_t btn = keybind_list_button(
                bind.idx,
                get_key_name_vk(bind.key),
                state->selected_keybind_idx == bind.idx,
                state->selected_keybind_idx_to_del == bind.idx
            );

            if (btn.pressed)
            {
                state->selected_keybind_idx = bind.idx;
            }

            if (btn.del)
            {
                if (state->selected_keybind_idx == bind.idx)
                    state->selected_keybind_idx = -1;

                if (state->selected_keybind_idx_to_del == bind.idx)
                    state->selected_keybind_idx_to_del = -1;

                remove_index = i;
            }
        }

        if (remove_index != -1)
        {
            keybinds_list->erase(keybinds_list->begin() + remove_index);
        }

        gui->dummy({ gui->content_avail().x, SCALE(6 * state->anim_offset) });

        gui->dummy({ gui->content_avail().x, 0 });

        draw->line(gui->window_drawlist(), GetItemRectMin(), GetItemRectMax(), draw->get_clr(clr->popup_widget, state->anim_offset));

        gui->dummy({ gui->content_avail().x, SCALE(6) });

        state->is_change_hotkey_open = widgets->is_popup_open(xorstr_("Change hotkey"));

        if (keybind_nav(xorstr_("Change hotkey"), xorstr_("b"), clr->white, 0, state->is_change_hotkey_open))
        {
            widgets->open_popup(xorstr_("Change hotkey"));
        }

        if (widgets->popup(xorstr_("Change hotkey"), SCALE(190, 0), GetItemRectMax() + SCALE(11, -24), SCALE(10, 10), SCALE(10, 10), SCALE(10), false, window_flags_no_scrollbar | window_flags_no_scroll_with_mouse))
        {
            if (keybind_button(name + xorstr_("##keybind_button"), &state->key).add_pressed)
            {
                if (state->key != 0)
                {
                    widgets->keybind_idx += 1;
                    keybinds_list->push_back({ widgets->keybind_idx, state->key, state->mode });
                    state->key = 0;
                    state->mode = 0;
                }
            }
            mode_button(name + xorstr_("##mode_button"), &state->mode);

            widgets->end_popup();
        }

        // Favorites and Hide overlay buttons removed

        if (keybind_nav(xorstr_("Delete hotkey"), xorstr_("^"), clr->red, 3, false))
        {
            state->selected_keybind_idx_to_del = state->selected_keybind_idx;
        }

        gui->dummy({ gui->content_avail().x, SCALE(6) });

        widgets->end_popup();
    }
}

bool c_widgets::keybind_direct(std::string name, int* key)
{
    c_window* window = gui->get_window();
    c_id id = window->GetID(name.data());
    keybind_button_state_t* state = gui->anim_container<keybind_button_state_t>(id);

    c_vec2 pos = window->DC.CursorPos;
    c_vec2 size = { gui->content_avail().x, SCALE(30) };
    c_rect rect = { pos, pos + size };
    c_rect key_rect = { rect.Max - SCALE(100, 30), rect.Max };

    gui->item_size(rect);
    gui->item_add(rect, id);

    bool pressed = gui->button_behavior(key_rect, id, nullptr, nullptr);

    if (pressed && !state->active)
    {
        state->active = true;
        state->timer = ImGui::GetTime();
    }
    else if (!key_rect.Contains(gui->mouse_pos()) && GetIO().MouseClicked[0]) {
        state->active = false;
    }

    if (state->active) 
    {
        if (ImGui::GetTime() - state->timer > 0.1f) 
        {
            for (int i = 1; i < 256; ++i) 
            {
                if (GetAsyncKeyState(i) & 0x8000) 
                {
                    // Block keys that conflict with system/overlay functions
                    if (i == VK_ESCAPE) {
                        *key = 0;
                        state->active = false;
                        break;
                    }
                    // Skip ToggleMenuKey itself to prevent self-capture crash
                    if (i == cfg::ToggleMenuKey) continue;
                    // Skip other problematic system keys
                    if (i == VK_SNAPSHOT || i == VK_PAUSE || i == VK_NUMLOCK ||
                        i == VK_SCROLL || i == VK_CAPITAL) continue;
                    *key = i;
                    state->active = false;
                    break;
                }
            }
        }
    }

    // Render name
    draw->text_clipped(window->DrawList, font->get(sf_pro, 12), rect.Min, rect.Max - SCALE(100, 0), draw->get_clr(clr->text), name.data(), 0, 0, { 0, 0.5 });

    // Render button
    draw->rect_filled(window->DrawList, key_rect.Min, key_rect.Max, draw->get_clr(clr->popup_widget), SCALE(5));
    draw->text_clipped(window->DrawList, font->get(sf_pro, 12), key_rect.Min, key_rect.Max, draw->get_clr(clr->white), state->active ? xorstr_("...") : get_key_name_vk(*key).data(), 0, 0, { 0.5, 0.5 });

    return pressed;
}
