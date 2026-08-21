#include "security/xorstr.hpp"
#include "../headers/includes.h"
#include <algorithm>

bool search_field(std::string name, char* buf, int buf_size, bool force_click = false) 
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

    draw->text_clipped(window->DrawList, font->get(icons, 12), rect.Min + SCALE(15, 0), rect.Max, draw->get_clr(clr->white), xorstr_("X"), 0, 0, { 0, 0.5 });

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

    widgets->text_field_ex(
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
        0,
        0,
        0,
        user_clicked
    );


    gui->pop_font();
    gui->pop_var();

    return state->active || user_clicked;
}

bool c_widgets::search(std::string name) 
{
    struct search_state 
    {
        c_vec2 pos{};
        float alpha = 0.f;

        bool active = false;
        bool is_popups_open = false;

        float height = 0.f;
        float height_anim = 0.f;
        float width_anim = 0.f;

        float rect_alpha = 0.f;
        c_rect win_rect{};

        char buf[64]{};
    };

    c_window* parent_window = gui->get_window();
    c_id id = parent_window->GetID(std::string(name + xorstr_("##search_field_data")).data());

    search_state* state = gui->anim_container<search_state>(id);

    c_rect placeholder = {
        parent_window->DC.CursorPos,
        parent_window->DC.CursorPos + SCALE(250, 37)
    };

    gui->item_size(placeholder);
    gui->item_add(placeholder, id);

    if (!state->active && state->alpha <= 0.01f)
        state->pos = placeholder.Min;

    bool clicked_input_from_parent =
        placeholder.Contains(gui->mouse_pos()) &&
        gui->mouse_clicked(0) &&
        gui->is_window_hovered(0);

    if (clicked_input_from_parent)
        state->active = true;

    if (state->width_anim <= 0.f)
        state->width_anim = SCALE(250);

    gui->easing(state->alpha, state->active ? 1.f : 0.f, 7.f, static_easing);
    gui->easing(state->width_anim, state->active || state->alpha > 0.01f ? SCALE(350) : SCALE(250), 24.f, dynamic_easing);
    gui->easing(state->height_anim, state->active ? state->height : 0.f, 24.f, dynamic_easing);

    float animated_height = round(state->height_anim);
    float window_height = SCALE(37);

    if (animated_height > 1.f)
        window_height += SCALE(10) + animated_height;

    gui->set_next_window_pos(state->pos);
    gui->set_next_window_size({ (float)round(state->width_anim), window_height });

    gui->push_var(style_var_window_padding, SCALE(0, 0));
    gui->push_var(style_var_item_spacing, SCALE(0, 0));

    gui->begin(
        name,
        nullptr,
        window_flags_no_decoration |
        window_flags_no_background |
        window_flags_no_scroll_with_mouse |
        window_flags_no_scrollbar
    );

    bool had_popup_open = state->is_popups_open;
    state->is_popups_open = false;

    if (state->active && !had_popup_open)
        gui->set_window_focus();

    c_rect search_window_rect = gui->get_window()->Rect();

    if (state->alpha > 0.01f)
        var->blackout.active = true;

    bool ret = search_field(name, state->buf, sizeof(state->buf), clicked_input_from_parent);

    if (ret)
        state->active = true;

    if (state->active || state->alpha > 0.01f || state->height_anim > 1.f)
    {
        gui->dummy({ gui->content_avail().x, SCALE(10) });

        gui->push_var(style_var_alpha, state->alpha);
        gui->push_var(style_var_scrollbar_border_padding, SCALE(10, 10));
        gui->push_var(style_var_scrollbar_content_padding, SCALE(0));
        gui->push_var(style_var_scrollbar_size, SCALE(10));

        gui->easing(
            state->rect_alpha,
            state->height < SCALE(11) ? 0.f : state->alpha,
            7.f,
            static_easing
        );

        draw->rect_filled(
            gui->window_drawlist(),
            state->win_rect.Min,
            state->win_rect.Max,
            draw->get_clr(clr->child, state->rect_alpha),
            SCALE(15)
        );

        gui->begin_content(
            name + xorstr_("##features"),
            { gui->content_avail().x, (float)round(state->height_anim) },
            SCALE(10, 10),
            SCALE(10, 10),
            state->height == SCALE(206) ? 0 : window_flags_no_scrollbar | window_flags_no_scroll_with_mouse
        );
        {
            std::string buf_lower = state->buf;

            std::transform(
                buf_lower.begin(),
                buf_lower.end(),
                buf_lower.begin(),
                [](unsigned char c) { return (char)std::tolower(c); }
            );

            state->height = SCALE(10);

            var->widget.draw_list = gui->window_drawlist();

            for (const auto& widget : menu_cfg->order) 
            {
                std::string name_lower = widget.first;

                std::transform(
                    name_lower.begin(),
                    name_lower.end(),
                    name_lower.begin(),
                    [](unsigned char c) { return (char)std::tolower(c); }
                );

                if (name_lower.find(buf_lower) == std::string::npos)
                    continue;

                bool added = false;

                if (widget.second == checkbox_type) 
                {
                    if (!menu_cfg->get<checkbox_t>(widget.first).is_weapon)
                    {
                        widgets->checkbox(widget.first);
                        state->height += SCALE(30);
                        added = true;

                        if (widgets->is_popup_open(widget.first + xorstr_("##keybind")))
                            state->is_popups_open = true;
                    }
                }
                else if (widget.second == slider_type) 
                {
                    widgets->slider(widget.first);
                    state->height += SCALE(30);
                    added = true;
                }
                else if (widget.second == dropdown_type) 
                {   
                    widgets->dropdown(widget.first); 
                    state->height += SCALE(57);
                    added = true;

                    if (widgets->is_popup_open(widget.first + xorstr_("##popup")))
                        state->is_popups_open = true;
                }
                else if (widget.second == multi_dropdown_type) 
                {
                    widgets->multi_dropdown(widget.first);
                    state->height += SCALE(57);
                    added = true;

                    if (widgets->is_popup_open(widget.first + xorstr_("##popup")))
                        state->is_popups_open = true;
                }

                if (added && gui->content_avail().y + gui->get_window()->Scroll.y > 0)
                    state->height += SCALE(11);
            }

            state->height = ImClamp(state->height, 0.f, SCALE(206));
            state->win_rect = gui->get_window()->Rect();
        }

        gui->end_content();

        gui->pop_var(4);
    }

    search_window_rect = gui->get_window()->Rect();

    if (
        state->active &&
        gui->mouse_clicked(0) &&
        !search_window_rect.Contains(gui->mouse_pos()) &&
        !state->is_popups_open
    )
    {
        state->active = false;
    }

    gui->end();

    gui->pop_var(2);

    return state->active || state->alpha > 0.01f || state->height_anim > 1.f;
}
