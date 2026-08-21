#include "../headers/includes.h"

void c_widgets::config_info(std::string icon, std::string name, std::string info)
{
    gui->dummy({ gui->content_avail().x, SCALE(10) });

    c_rect rect = { GetItemRectMin(), GetItemRectMax() };

    draw->text_clipped(var->widget.draw_list, font->get(icons, 12), rect.Min - SCALE(0, 2), rect.Max + SCALE(0, 2), draw->get_clr(clr->white), icon.data(), 0, 0, { 0, 0.5 });
    draw->text_clipped(var->widget.draw_list, font->get(sf_pro, 12), rect.Min + SCALE(27, -2), rect.Max + SCALE(0, 2), draw->get_clr(clr->white), name.data(), 0, 0, { 0, 0.5 });
    draw->text_clipped(var->widget.draw_list, font->get(sf_pro, 12), rect.Min - SCALE(0, 2), rect.Max + SCALE(0, 2), draw->get_clr(clr->white), info.data(), 0, 0, { 1, 0.5 });

    if (need_sep())
    {
        widgets->seperator();
    }
}
