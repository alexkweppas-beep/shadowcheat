#include "security/xorstr.hpp"
#include "../headers/includes.h"

void c_widgets::weapon_info(std::string name, bool* open, std::string weapon_class, std::string weapon_name, ImTextureID weapon_img, c_vec2 weapon_img_size, std::string ammo, std::string scope, std::string barrel)
{
    static float alpha = 1.f;
    gui->easing(alpha, *open ? 1.f : 0.f, 7.f, static_easing);

    if (alpha == 0)
        return;

    gui->set_next_window_size(SCALE(300, 209));
    gui->push_var(style_var_window_padding, SCALE(15, 15));
    gui->push_var(style_var_item_spacing, SCALE(15, 15));
    gui->push_var(style_var_alpha, alpha);
    gui->begin(name, nullptr, window_flags_no_background | window_flags_no_decoration | window_flags_no_scroll_with_mouse | window_flags_no_scrollbar);
    {
        c_window* window = gui->get_window();
        c_rect rect = window->Rect();
        
        draw_background_blur(window->DrawList, var->dx.swap_chain, var->dx.device, var->dx.ctx, rect.Min, rect.Max, SCALE(16));
        draw->rect_filled(window->DrawList, rect.Min, rect.Max, draw->get_clr(clr->layout, 0.9f), SCALE(16));

        gui->dummy({ gui->content_avail().x, SCALE(10) });
        rect = { GetItemRectMin(), GetItemRectMax() };

        draw->text_clipped(window->DrawList, font->get(sf_pro, 12), rect.Min, rect.Max, draw->get_clr(clr->white), weapon_class.data(), 0, 0, { 0, 0.5 });
        c_vec2 weapon_class_size = gui->text_size(font->get(sf_pro, 12), weapon_class.data());
        draw->text_clipped(window->DrawList, font->get(icons, 10), rect.Min + c_vec2{ weapon_class_size.x + SCALE(10), 0 }, rect.Max, draw->get_clr(clr->text), xorstr_("N"), 0, 0, { 0, 0.5 });
        draw->text_clipped(window->DrawList, font->get(sf_pro, 12), rect.Min + c_vec2{ weapon_class_size.x + SCALE(30), 0 }, rect.Max, draw->get_clr(clr->text), weapon_name.data(), 0, 0, { 0, 0.5 });
    
        static c_vec4 close_clr;
        c_rect close_rect = { rect.Max - SCALE(10, 10), rect.Max };
        gui->easing(close_clr, close_rect.Contains(gui->mouse_pos()) && gui->is_window_hovered(0) ? clr->white.Value : clr->text.Value, 16.f, dynamic_easing);
        draw->text_clipped(window->DrawList, font->get(icons, 10), rect.Min, rect.Max, draw->get_clr(close_clr), xorstr_("O"), 0, 0, { 1, 0.5 });
    
        gui->dummy({ gui->content_avail().x, SCALE(100) });
        rect = { GetItemRectMin(), GetItemRectMax() };
        
        draw->dashed_rect(window->DrawList, rect.Min, rect.Max, draw->get_clr(clr->border), SCALE(4), SCALE(10));

        draw->image(window->DrawList, weapon_img, rect.GetCenter() - weapon_img_size / 2, rect.GetCenter() + weapon_img_size / 2, { 0, 0 }, { 1, 1 }, draw->get_clr(clr->white));
    
        c_rect ammo_rect = { rect.Max - SCALE(10, 10) - SCALE(54, 15), rect.Max - SCALE(10, 10) };

        draw->text_clipped(window->DrawList, font->get(icons, 16), ammo_rect.Min, ammo_rect.Max, draw->get_clr(clr->text), xorstr_("M"), 0, 0, { 1, 0.5 });

        draw->text_clipped(window->DrawList, font->get(sf_pro, 12), ammo_rect.Min + SCALE(0, 1), ammo_rect.Max, draw->get_clr(clr->text), ammo.data(), 0, 0, { 0, 0.5 });
   
        gui->dummy({ gui->content_avail().x, SCALE(12) });
        rect = { GetItemRectMin(), GetItemRectMax() };

        draw->text_clipped(window->DrawList, font->get(icons, 12), rect.Min, rect.Max, draw->get_clr(clr->white), xorstr_("K"), 0, 0, { 0, 0.5 });
        draw->text_clipped(window->DrawList, font->get(sf_pro, 12), rect.Min + SCALE(22, 0), rect.Max, draw->get_clr(clr->white), xorstr_("Scope"), 0, 0, { 0, 0.5 });
        draw->text_clipped(window->DrawList, font->get(sf_pro, 12), rect.Min, rect.Max, draw->get_clr(clr->white), scope.data(), 0, 0, { 1, 0.5 });

        gui->dummy({ gui->content_avail().x, SCALE(12) });
        rect = { GetItemRectMin(), GetItemRectMax() };

        draw->text_clipped(window->DrawList, font->get(icons, 12), rect.Min, rect.Max, draw->get_clr(clr->white), xorstr_("L"), 0, 0, { 0, 0.5 });
        draw->text_clipped(window->DrawList, font->get(sf_pro, 12), rect.Min + SCALE(22, 0), rect.Max, draw->get_clr(clr->white), xorstr_("Barrel"), 0, 0, { 0, 0.5 });
        draw->text_clipped(window->DrawList, font->get(sf_pro, 12), rect.Min, rect.Max, draw->get_clr(clr->white), barrel.data(), 0, 0, { 1, 0.5 });

    }
    gui->end();
    gui->pop_var(3);
}
