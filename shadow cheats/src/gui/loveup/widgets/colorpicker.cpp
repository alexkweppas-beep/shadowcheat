#include "security/xorstr.hpp"
#include "../headers/includes.h"

static float wrap_01(float v)
{
    v = fmodf(v, 1.0f);
    if (v < 0.0f) v += 1.0f;
    return v;
}

bool hex_field(std::string name, char* buf, size_t buf_size)
{
    struct text_field_state
    {
        bool active;
        float alpha;
        ImVec4 text;
    };

    c_window* window = gui->get_window();
    c_id id = window->GetID(name.data());
    text_field_state* state = gui->anim_container<text_field_state>(window->GetID((std::stringstream{} << id << xorstr_("text_field_state")).str().data()));

    c_vec2 pos = window->DC.CursorPos;
    c_vec2 size = SCALE(110, 30);
    c_rect rect = { pos, pos + size };
    c_rect field = { rect.Min, rect.Max };

    gui->item_size(rect); 
    gui->item_add(rect, id);

    bool user_clicked = rect.Contains(gui->mouse_pos()) && gui->mouse_clicked(0) && gui->is_window_hovered(0);

    gui->easing(state->text, clr->white.Value, 24.f, dynamic_easing);
    gui->easing(state->alpha, strlen(buf) > 0 ? 0.f : 1.f, 24.f, dynamic_easing);

    draw->text_clipped(window->DrawList, font->get(sf_pro, 12), field.Min + SCALE(12, 0) + ImVec2((size.x / 2) * (1.f - state->alpha), 0), rect.Max, draw->get_clr(clr->text, state->alpha), name.data(), 0, 0, { 0, 0.5 });

    gui->push_var(style_var_frame_rounding, 0);
    gui->push_font(font->get(sf_pro, 12));

    bool value_changed = widgets->text_field_ex(name, "", buf, buf_size, SCALE(12), SCALE(10), draw->get_clr(state->text), false, field, &state->active, ImGuiInputTextFlags_EnterReturnsTrue, 0, 0, user_clicked);

    gui->pop_font();
    gui->pop_var();

    return value_changed;
}

static bool parse_three_floats(const char* input, float& a, float& b, float& c)
{
    std::string s(input ? input : "");

    for (char& ch : s)
    {
        if (ch == ',' || ch == ';' || ch == '/' || ch == '|')
            ch = ' ';
    }

    std::stringstream ss(s);
    return (ss >> a >> b >> c) ? true : false;
}

static void rgb_to_hls(float r, float g, float b, float& h, float& l, float& s)
{
    float max_c = ImMax(r, ImMax(g, b));
    float min_c = ImMin(r, ImMin(g, b));
    float d = max_c - min_c;

    l = (max_c + min_c) * 0.5f;

    if (d <= 0.00001f)
    {
        h = 0.0f;
        s = 0.0f;
        return;
    }

    s = l > 0.5f ? d / (2.0f - max_c - min_c) : d / (max_c + min_c);

    if (max_c == r)
        h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (max_c == g)
        h = (b - r) / d + 2.0f;
    else
        h = (r - g) / d + 4.0f;

    h /= 6.0f;
    h = wrap_01(h);
}

static float hls_hue_to_rgb(float p, float q, float t)
{
    t = wrap_01(t);

    if (t < 1.0f / 6.0f)
        return p + (q - p) * 6.0f * t;

    if (t < 1.0f / 2.0f)
        return q;

    if (t < 2.0f / 3.0f)
        return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;

    return p;
}

static void hls_to_rgb(float h, float l, float s, float& r, float& g, float& b)
{
    h = wrap_01(h);
    l = ImSaturate(l);
    s = ImSaturate(s);

    if (s <= 0.00001f)
    {
        r = g = b = l;
        return;
    }

    float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;

    r = hls_hue_to_rgb(p, q, h + 1.0f / 3.0f);
    g = hls_hue_to_rgb(p, q, h);
    b = hls_hue_to_rgb(p, q, h - 1.0f / 3.0f);
}

bool input_rgb_hex(std::string name, ImVec4* color, int input_mode)
{
    bool value_changed = false;

    if (input_mode == 0) // HEX
    {
        char buf[8];

        snprintf(buf, sizeof(buf), xorstr_("#%02X%02X%02X"),
            ImClamp((int)(color->x * 255.0f), 0, 255),
            ImClamp((int)(color->y * 255.0f), 0, 255),
            ImClamp((int)(color->z * 255.0f), 0, 255));

        value_changed = hex_field(name, buf, sizeof(buf));

        if (value_changed)
        {
            if (buf[0] != '#')
            {
                memmove(buf + 1, buf, sizeof(buf) - 2);
                buf[0] = '#';
            }

            for (int i = 1; i < 7; ++i)
            {
                if (buf[i] == '\0' || !isxdigit((unsigned char)buf[i]))
                    buf[i] = 'F';
            }

            buf[7] = '\0';

            int r = 255, g = 255, b = 255;
            sscanf(buf, xorstr_("#%02X%02X%02X"), &r, &g, &b);

            color->x = r / 255.0f;
            color->y = g / 255.0f;
            color->z = b / 255.0f;
        }
    }
    else if (input_mode == 1) // RGB: "255, 255, 255"
    {
        char buf[16];

        snprintf(buf, sizeof(buf), xorstr_("%d, %d, %d"),
            ImClamp((int)(color->x * 255.0f), 0, 255),
            ImClamp((int)(color->y * 255.0f), 0, 255),
            ImClamp((int)(color->z * 255.0f), 0, 255));

        value_changed = hex_field(name, buf, sizeof(buf));

        if (value_changed)
        {
            float rf = 255.0f, gf = 255.0f, bf = 255.0f;

            if (parse_three_floats(buf, rf, gf, bf))
            {
                int r = ImClamp((int)roundf(rf), 0, 255);
                int g = ImClamp((int)roundf(gf), 0, 255);
                int b = ImClamp((int)roundf(bf), 0, 255);

                color->x = r / 255.0f;
                color->y = g / 255.0f;
                color->z = b / 255.0f;
            }
        }
    }
    else if (input_mode == 2) // HLS: "H, L, S" -> 0..360, 0..100, 0..100
    {
        char buf[20];

        float h, l, s;
        rgb_to_hls(color->x, color->y, color->z, h, l, s);

        snprintf(buf, sizeof(buf), xorstr_("%d, %d, %d"),
            ImClamp((int)roundf(h * 360.0f), 0, 360),
            ImClamp((int)roundf(l * 100.0f), 0, 100),
            ImClamp((int)roundf(s * 100.0f), 0, 100));

        value_changed = hex_field(name, buf, sizeof(buf));

        if (value_changed)
        {
            float hf = 0.0f, lf = 100.0f, sf = 100.0f;

            if (parse_three_floats(buf, hf, lf, sf))
            {
                hf = wrap_01(hf / 360.0f);
                lf = ImClamp(lf, 0.0f, 100.0f) / 100.0f;
                sf = ImClamp(sf, 0.0f, 100.0f) / 100.0f;

                hls_to_rgb(hf, lf, sf, color->x, color->y, color->z);
            }
        }
    }
    else if (input_mode == 3) // HLB / HSB / HSV: "H, S, B" -> 0..360, 0..100, 0..100
    {
        char buf[20];

        float h, s, v;
        ImGui::ColorConvertRGBtoHSV(color->x, color->y, color->z, h, s, v);

        snprintf(buf, sizeof(buf), xorstr_("%d, %d, %d"),
            ImClamp((int)roundf(h * 360.0f), 0, 360),
            ImClamp((int)roundf(s * 100.0f), 0, 100),
            ImClamp((int)roundf(v * 100.0f), 0, 100));

        value_changed = hex_field(name, buf, sizeof(buf));

        if (value_changed)
        {
            float hf = 0.0f, sf = 100.0f, bf = 100.0f;

            if (parse_three_floats(buf, hf, sf, bf))
            {
                hf = wrap_01(hf / 360.0f);
                sf = ImClamp(sf, 0.0f, 100.0f) / 100.0f;
                bf = ImClamp(bf, 0.0f, 100.0f) / 100.0f;

                ImGui::ColorConvertHSVtoRGB(hf, sf, bf, color->x, color->y, color->z);
            }
        }
    }

    return value_changed;
}

void copy_color_to_clipboard(const ImVec4& color, int mode)
{
    char buf[32];

    int r = ImClamp((int)(color.x * 255.0f), 0, 255);
    int g = ImClamp((int)(color.y * 255.0f), 0, 255);
    int b = ImClamp((int)(color.z * 255.0f), 0, 255);

    if (mode == 0) // HEX
    {
        snprintf(buf, sizeof(buf), xorstr_("#%02X%02X%02X"), r, g, b);
    }
    else if (mode == 1) // RGB
    {
        snprintf(buf, sizeof(buf), xorstr_("%d, %d, %d"), r, g, b);
    }
    else if (mode == 2) // HLS: H, L, S
    {
        float h, l, s;

        rgb_to_hls(color.x, color.y, color.z, h, l, s);

        snprintf(buf, sizeof(buf), xorstr_("%d, %d, %d"),
            ImClamp((int)roundf(h * 360.0f), 0, 360),
            ImClamp((int)roundf(l * 100.0f), 0, 100),
            ImClamp((int)roundf(s * 100.0f), 0, 100));
    }
    else if (mode == 3) // HLB / HSB / HSV: H, S, B
    {
        float h, s, v;

        ImGui::ColorConvertRGBtoHSV(color.x, color.y, color.z, h, s, v);

        snprintf(buf, sizeof(buf), xorstr_("%d, %d, %d"),
            ImClamp((int)roundf(h * 360.0f), 0, 360),
            ImClamp((int)roundf(s * 100.0f), 0, 100),
            ImClamp((int)roundf(v * 100.0f), 0, 100));
    }
    else
    {
        return;
    }

    ImGui::SetClipboardText(buf);
}

static float hue_from_angle(float angle)
{
    return wrap_01((IM_PI - angle) / (2.0f * IM_PI));
}

static float angle_from_hue(float hue)
{
    return IM_PI - hue * 2.0f * IM_PI;
}

static ImU32 hsv_color(float h, float s, float v, float a = 1.0f)
{
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
}

static void add_quad_multi_color(
    ImDrawList* draw,
    ImVec2 p0, ImVec2 p1, ImVec2 p2, ImVec2 p3,
    ImU32 c0, ImU32 c1, ImU32 c2, ImU32 c3)
{
    const ImVec2 uv = ImGui::GetIO().Fonts->TexUvWhitePixel;

    draw->PrimReserve(6, 4);

    ImDrawIdx idx = (ImDrawIdx)draw->_VtxCurrentIdx;

    draw->PrimWriteIdx(idx + 0);
    draw->PrimWriteIdx(idx + 1);
    draw->PrimWriteIdx(idx + 2);

    draw->PrimWriteIdx(idx + 0);
    draw->PrimWriteIdx(idx + 2);
    draw->PrimWriteIdx(idx + 3);

    draw->PrimWriteVtx(p0, uv, c0);
    draw->PrimWriteVtx(p1, uv, c1);
    draw->PrimWriteVtx(p2, uv, c2);
    draw->PrimWriteVtx(p3, uv, c3);
}

static void draw_hsv_disc(ImDrawList* draw, ImVec2 center, float radius, float value)
{
    const int segments = SCALE(999);
    const int rings = 32;

    for (int r = 0; r < rings; r++)
    {
        float r0 = radius * ((float)r / rings);
        float r1 = radius * ((float)(r + 1) / rings);

        float s0 = r0 / radius;
        float s1 = r1 / radius;

        for (int i = 0; i < segments; i++)
        {
            float a0 = ((float)i / segments) * 2.0f * IM_PI;
            float a1 = ((float)(i + 1) / segments) * 2.0f * IM_PI;

            ImVec2 p0 = ImVec2(center.x + cosf(a0) * r0, center.y + sinf(a0) * r0);
            ImVec2 p1 = ImVec2(center.x + cosf(a1) * r0, center.y + sinf(a1) * r0);
            ImVec2 p2 = ImVec2(center.x + cosf(a1) * r1, center.y + sinf(a1) * r1);
            ImVec2 p3 = ImVec2(center.x + cosf(a0) * r1, center.y + sinf(a0) * r1);

            ImU32 c0 = hsv_color(hue_from_angle(a0), s0, value, var->style.alpha);
            ImU32 c1 = hsv_color(hue_from_angle(a1), s0, value, var->style.alpha);
            ImU32 c2 = hsv_color(hue_from_angle(a1), s1, value, var->style.alpha);
            ImU32 c3 = hsv_color(hue_from_angle(a0), s1, value, var->style.alpha);

            add_quad_multi_color(draw, p0, p1, p2, p3, c0, c1, c2, c3);
        }
    }
}

void mode_button(int i, std::string name, c_vec4* color, int* mode)
{
    gui->easing(*color, *mode == i ? clr->white.Value : clr->text.Value, 16.f, dynamic_easing);

    if (gui->invisible_button(name.data(), SCALE(205, 170) / 10))
    {
        *mode = i;
    }

    c_rect btn_rect = { GetItemRectMin(), GetItemRectMax() };

    draw->text_clipped(gui->window_drawlist(), font->get(sf_pro, 12), btn_rect.Min, btn_rect.Max, draw->get_clr(*color), name.data(), 0, 0, { 0.5, 0.5 });
}

auto clamp_point_to_circle = [](const c_vec2& point, const ImVec2& center, float radius) -> c_vec2
{
    float dx = point.x - center.x;
    float dy = point.y - center.y;

    float dist_sq = dx * dx + dy * dy;
    float radius_sq = radius * radius;

    if (dist_sq <= radius_sq)
        return point;

    float dist = sqrtf(dist_sq);
    float clamped_dist = ImClamp(dist, 0.0f, radius);

    return c_vec2(
        center.x + dx / dist * clamped_dist,
        center.y + dy / dist * clamped_dist
    );
};

struct colorpicker_state_t {
    int mode = 0;
    float line_anim_offset;
    std::vector<c_vec4> modes_colors{ clr->text.Value, clr->text.Value, clr->text.Value, clr->text.Value };
    float h, s, v, w;
    bool init_marker;
    c_vec2 marker;
    bool init_val = false;
    float v_anim_offset;
    float w_anim_offset;
    c_rect rect;
    c_vec4 last_color;
};

void c_widgets::colorpicker(std::string name, c_vec4* color, bool alpha)
{
    c_window* window = gui->get_window();
    c_id id = window->GetID(name.data());
    colorpicker_state_t* state = gui->anim_container<colorpicker_state_t>(id);

    bool color_changed_externally = (!state->init_val) ||
        fabsf(color->x - state->last_color.x) > 0.002f ||
        fabsf(color->y - state->last_color.y) > 0.002f ||
        fabsf(color->z - state->last_color.z) > 0.002f ||
        (alpha && fabsf(color->w - state->last_color.w) > 0.002f);

    if (color_changed_externally)
    {
        ImGui::ColorConvertRGBtoHSV(color->x, color->y, color->z, state->h, state->s, state->v);
        if (alpha)
            state->w = color->w;

        state->init_val = true;
        state->init_marker = false;
        state->last_color = *color;
    }

    c_vec2 pos = window->DC.CursorPos;
    c_vec2 size = { gui->content_avail().x, SCALE(20) };
    c_rect rect = { pos, pos + size };
    c_rect button = { rect.Max.x - SCALE(20), rect.GetCenter().y - SCALE(6), rect.Max.x, rect.GetCenter().y + SCALE(6) };

    gui->item_size(rect);
    gui->item_add(rect, id);

    bool pressed = gui->button_behavior(button, id, nullptr, nullptr);

    std::string popup_name = name + xorstr_("##popup");

    if (pressed)
    {
        widgets->open_popup(popup_name);
    }

    draw->text_clipped(var->widget.draw_list, font->get(sf_pro, 12), rect.Min, rect.Max, draw->get_clr(clr->white), name.data(), 0, 0, { 0, 0.5 });

    draw->rect(var->widget.draw_list, button.Min + SCALE(1, 1), button.Max - SCALE(1, 1), draw->get_clr(*color), SCALE(999), 0, SCALE(3));

    if (need_sep())
    {
        widgets->seperator();
    }

    if (widgets->popup(popup_name, SCALE(162, 0), rect.Max - SCALE(152, 0), SCALE(10, 10), SCALE(10, 10), SCALE(10), false, window_flags_no_scrollbar | window_flags_no_scroll_with_mouse))
    {
        window = gui->get_window();

        std::vector<std::string> modes_list = { xorstr_("HEX"), xorstr_("RGB"), xorstr_("HLS"), xorstr_("HLB") };

        for (int i = 0; i < modes_list.size(); i++)
        {
            mode_button(i, modes_list[i], &state->modes_colors[i], &state->mode);
            if (i != modes_list.size() - 1)
            {
                gui->sameline(0, SCALE(20));
            }
        }

        {
            gui->dummy({ gui->content_avail().x, 0 });
            c_rect line_rect = { GetItemRectMin(), GetItemRectMax() };

            gui->easing(state->line_anim_offset, state->mode == 0 ? 0.f : state->mode == 1 ? 39.f : state->mode == 2 ? 79.f : 120.f, 16.f, dynamic_easing);
        
            draw->line(window->DrawList, line_rect.Min, line_rect.Max, draw->get_clr(clr->popup_widget));
            draw->line(window->DrawList, line_rect.Min + SCALE(state->line_anim_offset, 0), line_rect.Min + SCALE(state->line_anim_offset + 22, 0), draw->get_clr(clr->accent));
        }

        {
            gui->invisible_button(xorstr_("picker"), SCALE(142, 142));

            c_rect picker_rect = { GetItemRectMin(), GetItemRectMax() };

            if (gui->is_item_active() && gui->mouse_down(0))
            {
                c_vec2 mouse = gui->mouse_pos();

                float dx = mouse.x - picker_rect.GetCenter().x;
                float dy = mouse.y - picker_rect.GetCenter().y;

                float dist = sqrtf(dx * dx + dy * dy);

                state->s = ImClamp(dist / SCALE(71), 0.0f, 1.0f);

                if (dist > 0.001f)
                {
                    float angle = atan2f(dy, dx);
                    state->h = hue_from_angle(angle);
                }
            }

            draw_hsv_disc(window->DrawList, picker_rect.GetCenter(), SCALE(71), state->v);
            draw->circle(window->DrawList, picker_rect.GetCenter(), SCALE(725) / 10, draw->get_clr(ImColor(40, 42, 50)), SCALE(999), SCALE(5));
            draw->circle(window->DrawList, picker_rect.GetCenter(), SCALE(685) / 10, draw->get_clr(clr->border, 0.2f), SCALE(999), SCALE(5));

            float marker_angle = angle_from_hue(state->h);
            float marker_radius = state->s * SCALE(71);

            c_vec2 marker = c_vec2(
                picker_rect.GetCenter().x + cosf(marker_angle) * marker_radius,
                picker_rect.GetCenter().y + sinf(marker_angle) * marker_radius
            );

            marker = clamp_point_to_circle(marker, picker_rect.GetCenter(), SCALE(62));

            gui->easing(state->marker.x, marker.x - window->Pos.x, 16.f, dynamic_easing);
            gui->easing(state->marker.y, marker.y - window->Pos.y, 16.f, dynamic_easing);

            if (!state->init_marker)
            {
                state->marker = marker - window->Pos;
                state->init_marker = true;
            }

            draw->circle(window->DrawList, state->marker + window->Pos, SCALE(7), draw->get_clr(clr->white), SCALE(999), SCALE(3));
        }

        {

            gui->invisible_button(xorstr_("v"), SCALE(142, 8));

            c_rect v_rect = { GetItemRectMin(), GetItemRectMax() };

            if (gui->is_item_active() && gui->mouse_down(0))
            {
                state->v = ImSaturate((gui->mouse_pos().x - v_rect.Min.x) / v_rect.GetWidth());
            }

            gui->easing(state->v_anim_offset, state->v, 16.f, dynamic_easing);

            draw->rect_filled(window->DrawList, v_rect.Min, v_rect.Max, draw->get_clr(clr->popup_widget), SCALE(999));

            float clip_x = v_rect.GetWidth() * state->v_anim_offset - SCALE(5);
            clip_x = ImClamp(clip_x, 0.f, v_rect.GetWidth() - SCALE(8));
            clip_x = roundf(clip_x);

            draw->push_clip_rect(window->DrawList, v_rect.Min, v_rect.Min + c_vec2{ clip_x, v_rect.GetHeight() }, true);
            draw->rect_filled_multi_color(window->DrawList, v_rect.Min, v_rect.Max, draw->get_clr(clr->black), draw->get_clr(clr->white), draw->get_clr(clr->white), draw->get_clr(clr->black), SCALE(999));
            draw->pop_clip_rect(window->DrawList);

            float grab_x = v_rect.GetWidth() * state->v_anim_offset;
            grab_x = ImClamp(grab_x, SCALE(3), v_rect.GetWidth() - SCALE(3));
            grab_x = roundf(grab_x);

            draw->rect_filled(window->DrawList, v_rect.Min + c_vec2{ grab_x - SCALE(3), SCALE(-2) }, v_rect.Min + c_vec2{ grab_x + SCALE(3), SCALE(10) }, draw->get_clr(clr->white), SCALE(2));

        }

        {
            gui->invisible_button(xorstr_("w"), SCALE(142, 8));

            c_rect w_rect = { GetItemRectMin(), GetItemRectMax() };

            if (gui->is_item_active() && gui->mouse_down(0))
            {
                state->w = ImSaturate((gui->mouse_pos().x - w_rect.Min.x) / w_rect.GetWidth());
            }

            gui->easing(state->w_anim_offset, state->w, 16.f, dynamic_easing);

            draw->rect_filled(window->DrawList, w_rect.Min, w_rect.Max, draw->get_clr(clr->popup_widget), SCALE(999));

            float clip_x = w_rect.GetWidth() * state->w_anim_offset - SCALE(5);
            clip_x = ImClamp(clip_x, 0.f, w_rect.GetWidth() - SCALE(8));
            clip_x = roundf(clip_x);

            draw->push_clip_rect(window->DrawList, w_rect.Min, w_rect.Min + c_vec2{ clip_x, w_rect.GetHeight() }, true);
            draw->rect_filled_multi_color(window->DrawList, w_rect.Min, w_rect.Max, draw->get_clr(clr->white), draw->get_clr(ImColor(color->x, color->y, color->z)), draw->get_clr(ImColor(color->x, color->y, color->z)), draw->get_clr(clr->white), SCALE(999));
            draw->pop_clip_rect(window->DrawList);

            float grab_x = w_rect.GetWidth() * state->w_anim_offset;
            grab_x = ImClamp(grab_x, SCALE(3), w_rect.GetWidth() - SCALE(3));
            grab_x = roundf(grab_x);

            draw->rect_filled(window->DrawList, w_rect.Min + c_vec2{ grab_x - SCALE(3), SCALE(-2) }, w_rect.Min + c_vec2{ grab_x + SCALE(3), SCALE(10) }, draw->get_clr(clr->white), SCALE(2));

        }

        {
            draw->rect_filled(window->DrawList, state->rect.Min, state->rect.Max, draw->get_clr(clr->popup_widget), SCALE(4));

            c_vec4 input_clr = *color;
            if (input_rgb_hex(name + xorstr_("##input_rgb_hex"), &input_clr, state->mode))
            {
                ImGui::ColorConvertRGBtoHSV(input_clr.x, input_clr.y, input_clr.z, state->h, state->s, state->v);
            }

            state->rect.Min = GetItemRectMin();

            gui->sameline(0.f, 0.f);

            if (gui->invisible_button(xorstr_("copy color"), SCALE(32, 30)))
            {
                copy_color_to_clipboard(*color, state->mode);
            }

            draw->text_clipped(window->DrawList, font->get(icons, 12), GetItemRectMin(), GetItemRectMax(), draw->get_clr(clr->white), xorstr_("U"), 0, 0, { 0.5, 0.5 });

            state->rect.Max = GetItemRectMax();
        }

        widgets->end_popup();
    }

    ImGui::ColorConvertHSVtoRGB(state->h, state->s, state->v, color->x, color->y, color->z);
    if (alpha)
        color->w = state->w;
    state->last_color = *color;
}
