#include "security/xorstr.hpp"
#include "../headers/includes.h"

static float subtab_button_height()
{
    return SCALE(7);
}

static float subtab_gap()
{
    return SCALE(20);
}

static float calc_buttons_height(int count)
{
    if (count <= 0)
        return 0.f;

    return subtab_button_height() * count + subtab_gap() * (count - 1);
}

static float calc_items_section_height(const std::vector<std::string>& weapons_list)
{
    if (weapons_list.empty())
        return 0.f;

    // SCALE(20) — это твой dummy перед items content
    return SCALE(20) + calc_buttons_height((int)weapons_list.size());
}

static float calc_subtab_section_height(std::vector<subtab_t>* subtabs_list, int active_subtab)
{
    float height = 0.f;

    for (int i = 0; i < subtabs_list->size(); i++)
    {
        const subtab_t& subtab = subtabs_list->at(i);

        height += subtab_button_height();

        if (!subtab.weapons_list.empty() && active_subtab == i)
            height += calc_items_section_height(subtab.weapons_list);

        if (i + 1 < subtabs_list->size())
            height += subtab_gap();
    }

    // Компенсация padding из begin_content(name, ..., SCALE(0, 10), ...)
    height += SCALE(20);

    return height;
}

struct subtab_button_state_t {
    c_vec4 text;
    float alpha;
};

bool subtab_button(std::string name, bool is_active)
{
    c_window* window = gui->get_window();
    c_id id = window->GetID(std::string(name + xorstr_("##subtab_button")).data());
    subtab_button_state_t* state = gui->anim_container<subtab_button_state_t>(id);

    c_vec2 pos = window->DC.CursorPos;
    c_vec2 size = { gui->content_avail().x, SCALE(7) };
    c_rect rect = { pos, pos + size };

    gui->item_size(rect);
    gui->item_add(rect, id);

    bool hover, held, pressed = gui->button_behavior(c_rect(rect.Min - SCALE(0, 5), rect.Max + SCALE(0, 5)), id, &hover, &held);

    gui->easing(state->text, is_active ? clr->white.Value : clr->text.Value, 16.f, dynamic_easing);
    gui->easing(state->alpha, is_active ? 1.f : 0.f, 7.f, static_easing);

    widgets->draw_segment(var->tab.draw_list, { rect.Min.x, rect.GetCenter().y - SCALE(3) }, draw->get_clr(clr->border, state->alpha));
    draw->text_clipped(var->tab.draw_list, font->get(sf_pro, 12), rect.Min + SCALE(15, -5), rect.Max + SCALE(0, 5), draw->get_clr(state->text), name.data(), 0, 0, { 0, 0.5 });

    return pressed;
}

struct items_section_state_t {
    float anim_height{ 0.f };
    float alpha{ 0.f };
};

bool items_section(std::string name, bool is_active, std::vector<std::string> weapons_list)
{
    c_window* window = gui->get_window();
    c_id id = window->GetID(std::string(name + xorstr_("##items_section")).data());
    items_section_state_t* state = gui->anim_container<items_section_state_t>(id);

    const float target_content_height = calc_buttons_height((int)weapons_list.size());

    gui->easing(
        state->anim_height,
        is_active ? target_content_height : 0.f,
        16.f,
        dynamic_easing
    );

    gui->easing(
        state->alpha,
        is_active ? 1.f : 0.f,
        16.f,
        dynamic_easing
    );

    if (state->anim_height <= 0.5f && state->alpha <= 0.01f)
        return false;

    float height_progress = target_content_height > 0.f
        ? ImClamp(state->anim_height / target_content_height, 0.f, 1.f)
        : 0.f;

    gui->push_var(style_var_alpha, state->alpha * var->style.alpha);

    gui->set_pos(gui->get_pos().x + SCALE(30), pos_x);
    gui->dummy({ gui->content_avail().x, SCALE(20) * height_progress });

    gui->set_pos(gui->get_pos().x + SCALE(15), pos_x);

    gui->begin_content(
        name + xorstr_("##items_section"),
        { gui->content_avail().x, state->anim_height },
        SCALE(0, 0),
        SCALE(0, 0),
        window_flags_no_scroll_with_mouse | window_flags_no_scrollbar
    );
    {
        c_rect rect = gui->get_window()->Rect();

        draw->push_clip_rect(var->tab.draw_list, rect.Min, rect.Max + SCALE(0, 6), true);

        draw->line(var->tab.draw_list, rect.GetTL(), rect.GetBL(), draw->get_clr(clr->border));
        widgets->draw_segment(var->tab.draw_list, rect.GetBL(), draw->get_clr(clr->border));

        for (int i = 0; i < weapons_list.size(); i++)
        {
            if (subtab_button(weapons_list[i], var->tab.item_stored == i))
                var->tab.item_stored = i;

            if (i + 1 < weapons_list.size())
                gui->dummy({ gui->content_avail().x, SCALE(20) });
        }

        draw->pop_clip_rect(var->tab.draw_list);
    }
    gui->end_content();

    gui->pop_var();

    return true;
}

struct subtab_section_state_t {
    float anim_height{ 1.f };
    float alpha{ 0.f };
};

bool c_widgets::subtab_section(std::string name, bool is_active, std::vector<subtab_t>* subtabs_list)
{
    c_window* window = gui->get_window();
    c_id id = window->GetID(std::string(name + xorstr_("##subtab_section")).data());
    subtab_section_state_t* state = gui->anim_container<subtab_section_state_t>(id);

    const float target_height = calc_subtab_section_height(
        subtabs_list,
        var->tab.subtab_current
    );

    gui->easing(
        state->anim_height,
        is_active ? target_height : 1.f,
        16.f,
        dynamic_easing
    );

    gui->easing(
        state->alpha,
        is_active ? 1.f : 0.f,
        7.f,
        static_easing
    );

    gui->push_var(style_var_alpha, state->alpha * var->style.alpha);

    gui->set_pos(gui->get_pos().x + SCALE(15), pos_x);

    gui->begin_content(
        name + xorstr_("##subtab_section"),
        { gui->content_avail().x, state->anim_height },
        SCALE(0, 10),
        SCALE(0, 0),
        window_flags_no_scroll_with_mouse | window_flags_no_scrollbar
    );
    {
        c_rect rect = gui->get_window()->Rect();

        draw->push_clip_rect(var->tab.draw_list, rect.Min, rect.Max + SCALE(0, 6), true);

        draw->line(
            var->tab.draw_list,
            rect.GetTL() + SCALE(0, 10),
            rect.GetBL() - SCALE(0, 10),
            draw->get_clr(clr->border)
        );

        widgets->draw_segment(
            var->tab.draw_list,
            rect.GetBL() - SCALE(0, 10),
            draw->get_clr(clr->border)
        );

        for (int i = 0; i < subtabs_list->size(); i++)
        {
            const subtab_t& subtab = subtabs_list->at(i);

            if (subtab_button(subtab.name, var->tab.subtab_stored == i))
                var->tab.subtab_stored = i;

            if (!subtab.weapons_list.empty())
            {
                items_section(
                    subtab.name,
                    var->tab.subtab_current == i,
                    subtab.weapons_list
                );
            }

            if (i + 1 < subtabs_list->size())
                gui->dummy({ gui->content_avail().x, SCALE(20) });
        }

        draw->pop_clip_rect(var->tab.draw_list);
    }
    gui->end_content();

    gui->pop_var();

    return true;
}
