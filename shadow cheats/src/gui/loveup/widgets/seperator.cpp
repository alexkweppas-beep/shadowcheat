#include "../headers/includes.h"

void c_widgets::seperator()
{
    gui->dummy({ gui->content_avail().x, 0 });
    draw->line(var->widget.draw_list, GetItemRectMin(), GetItemRectMax(), draw->get_clr(clr->widget));
}
