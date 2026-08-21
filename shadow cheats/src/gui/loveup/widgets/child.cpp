#include "../headers/includes.h"

void c_widgets::child(std::string icon, std::string name, c_vec2 padding)
{
    gui->dummy({ elements->child.width, SCALE(12) });

    draw->text_clipped(var->widget.draw_list, font->get(icons, 11), GetItemRectMin(), GetItemRectMax(), draw->get_clr(clr->text), icon.data(), 0, 0, { 0, 0.5 });
    draw->text_clipped(var->widget.draw_list, font->get(sf_pro, 12), GetItemRectMin() + SCALE(22, 0), GetItemRectMax(), draw->get_clr(clr->text), name.data(), 0, 0, { 0, 0.5 });

    gui->begin_content(name, { elements->child.width, 0 }, padding.x != -1 ? padding : SCALE(10, 10), padding.x != -1 ? padding : SCALE(10, 10), window_flags_no_scrollbar | window_flags_no_scroll_with_mouse | window_flags_no_move, child_flags_always_auto_resize | child_flags_auto_resize_y);

    draw->rect_filled(var->widget.draw_list, gui->get_window()->Rect().Min, gui->get_window()->Rect().Max, draw->get_clr(clr->child), SCALE(elements->child.rounding));
}

void c_widgets::end_child()
{
    gui->end_content();
}
