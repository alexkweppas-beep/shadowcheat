#include "security/xorstr.hpp"
#include "../headers/includes.h"

void c_widgets::blackout()
{
    if (var->blackout.alpha == 0)
    {
        return;
    }

    gui->push_var(style_var_alpha, var->blackout.alpha);

    gui->set_pos({ 0, 0 }, pos_all);
    gui->begin_content(xorstr_("Blackout"), SCALE(elements->window.size), { 0, 0 }, { 0, 0 }, window_flags_no_move);
    {
        c_window* window = gui->get_window();
        c_rect rect = window->Rect();

        draw->rect_filled(window->DrawList, rect.Min, rect.Max, draw->get_clr(clr->layout, 0.5f), SCALE(elements->window.rounding));
    }
    gui->end_content();

    gui->pop_var();
}
