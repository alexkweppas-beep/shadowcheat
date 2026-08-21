#include "security/xorstr.hpp"
#include "../headers/includes.h"

struct weapon_checkbox_state_t {
    c_vec4 bottom;
    float alpha;
    c_vec4 line;
    float line_offset;
    float bg_alpha;
    float circle_offset;
    c_vec4 gear;
};

bool c_widgets::weapon_checkbox(std::string checkbox_data_name, std::string weapon_name, std::string class_name, ImTextureID weapon, c_vec2 weapon_image_size)
{
    checkbox_t* data = menu_cfg->fill<checkbox_t>(checkbox_data_name);
    if (!data->is_weapon)
        return false;

    c_window* window = gui->get_window();
    c_id id = window->GetID(checkbox_data_name.data());
    weapon_checkbox_state_t* state = gui->anim_container<weapon_checkbox_state_t>(id);

    c_vec2 pos = window->DC.CursorPos;
    c_vec2 size = { elements->child.width, SCALE(120) };
    c_rect rect = { pos, pos + size };
    c_rect header = { rect.Min + SCALE(12, 12), rect.GetTR() - SCALE(12, -24) };
    c_rect kb = { header.GetTR() - SCALE(12, 0), header.Max };
    c_rect bottom = { rect.GetBL() - SCALE(-12, 32), rect.Max - SCALE(12, 12) };
    c_rect button = { bottom.GetTR() - SCALE(35, 0), bottom.Max };

    gui->item_size(rect);
    gui->item_add(rect, id);

    bool hover, pressed = gui->button_behavior(rect, id, &hover, nullptr);

    bool kb_hover = kb.Contains(gui->mouse_pos()) && hover;

    if (pressed && !kb_hover)
    {
        data->enabled = !data->enabled;
    }

    gui->easing(state->gear, kb_hover ? clr->white.Value : clr->text.Value, 16.f, dynamic_easing);
    gui->easing(state->bottom, data->enabled ? clr->accent.Value : clr->widget.Value, 16.f, dynamic_easing);
    gui->easing(state->alpha, data->enabled ? 1.02f : -0.02f, 16.f, dynamic_easing);
    state->alpha = ImClamp(state->alpha, 0.f, 1.f);

    draw->rect_filled(var->widget.draw_list, rect.Min, rect.Max, draw->get_clr(clr->child), SCALE(10));

    draw->rect_filled_multi_color(var->widget.draw_list, rect.GetBL() - SCALE(0, 30 * state->alpha), rect.Max, draw->get_clr(clr->child, 0.f), draw->get_clr(clr->child, 0.f), draw->get_clr(clr->accent, 0.05f * state->alpha), draw->get_clr(clr->accent, 0.05f * state->alpha), SCALE(10), draw_flags_round_corners_bottom);

    draw->push_clip_rect(var->widget.draw_list, rect.GetBL() - SCALE(0, 3), rect.Max, true);
    draw->rect_filled(var->widget.draw_list, rect.Min, rect.Max, draw->get_clr(state->bottom), SCALE(10));
    draw->pop_clip_rect(var->widget.draw_list);

    draw->image(var->widget.draw_list, weapon, rect.GetCenter() - weapon_image_size / 2, rect.GetCenter() + weapon_image_size / 2, { 0, 0 }, { 1, 1 }, draw->get_clr(clr->white));

    c_vec2 class_name_size = gui->text_size(font->get(sf_pro, 12), class_name.data());

    draw->text_clipped(var->widget.draw_list, font->get(sf_pro, 12), header.Min, header.Max, draw->get_clr(clr->white), class_name.data(), 0, 0, { 0, 0.5 });
    draw->text_clipped(var->widget.draw_list, font->get(icons, 10), header.Min + c_vec2{ class_name_size.x + SCALE(10), 0 }, header.Max, draw->get_clr(clr->text), xorstr_("N"), 0, 0, { 0, 0.5 });
    draw->text_clipped(var->widget.draw_list, font->get(sf_pro, 12), header.Min + c_vec2{ class_name_size.x + SCALE(30), 0 }, header.Max, draw->get_clr(clr->text), weapon_name.data(), 0, 0, { 0, 0.5 });
    draw->text_clipped(var->widget.draw_list, font->get(icons, 12), header.Min, header.Max, draw->get_clr(state->gear), xorstr_("R"), 0, 0, { 1, 0.5 });

    bool is_none_kb = get_key_name_vk((int)data->last_keybind.key) == xorstr_("None");

    c_vec2 keybind_name_size = gui->text_size(font->get(sf_pro, 12), is_none_kb ? xorstr_("None") : get_key_name_vk((int)data->last_keybind.key).data());

    draw->text_clipped(var->widget.draw_list, font->get(icons, 12), bottom.Min, bottom.Max, draw->get_clr(clr->white), xorstr_("V"), 0, 0, { 0, 0.5 });
    draw->rect_filled(var->widget.draw_list, bottom.Min + SCALE(10, 0), bottom.Max - SCALE(gui->content_avail().x - keybind_name_size.x - 30, 0), draw->get_clr(clr->popup_widget), SCALE(5));
    draw->text_clipped(var->widget.draw_list, font->get(sf_pro, 12), bottom.Min + SCALE(22, 0), bottom.Max, draw->get_clr(clr->white), is_none_kb ? xorstr_("None") : get_key_name_vk((int)data->last_keybind.key).data(), 0, 0, { 0, 0.5 });
    draw->line(var->widget.draw_list, { bottom.Min.x + keybind_name_size.x + SCALE(32), bottom.GetCenter().y - SCALE(4) }, { bottom.Min.x + keybind_name_size.x + SCALE(32), bottom.GetCenter().y + SCALE(4) }, draw->get_clr(clr->border));
    draw->text_clipped(var->widget.draw_list, font->get(sf_pro, 12), bottom.Min + c_vec2{ keybind_name_size.x + SCALE(42), 0 }, bottom.Max, draw->get_clr(clr->text), is_none_kb ? xorstr_("None") : data->last_keybind.mode == 0 ? xorstr_("Toggle") : xorstr_("Hold"), 0, 0, { 0, 0.5 });
    
    gui->easing(state->line, data->enabled ? clr->accent.Value : clr->border.Value, 16.f, dynamic_easing);
    gui->easing(state->line_offset, data->enabled ? 0.f : 13.f, 16.f, dynamic_easing);
    gui->easing(state->circle_offset, data->enabled ? 15.f : 0.f, 16.f, dynamic_easing);
    gui->easing(state->bg_alpha, data->enabled ? 1.f : 0.f, 7.f, static_easing);

    draw->rect_filled(var->widget.draw_list, button.Min, button.Max, draw->get_clr(clr->widget), SCALE(999));
    draw->rect_filled(var->widget.draw_list, button.Min, button.Max, draw->get_clr(clr->accent, 0.15f * state->bg_alpha), SCALE(999));
    draw->rect_filled(var->widget.draw_list, button.Min + SCALE(10 + state->line_offset, 6), button.Min + SCALE(12 + state->line_offset, 14), draw->get_clr(state->line, 1.f - (0.5f * state->bg_alpha)), SCALE(999));
    if (!data->enabled)
        draw->circle_filled(var->widget.draw_list, button.Min + SCALE(10 + state->circle_offset, 10), SCALE(7), draw->get_clr(state->line), SCALE(999));
    else
        draw->circle(var->widget.draw_list, button.Min + SCALE(10 + state->circle_offset, 10), SCALE(5), draw->get_clr(state->line), SCALE(999), SCALE(4));

    widgets->keybind(checkbox_data_name + xorstr_("##keybind"), kb, pressed, &data->keybinds_list);

    return pressed;
}
