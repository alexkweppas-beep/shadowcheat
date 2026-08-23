#define IMGUI_DEFINE_MATH_OPERATORS

#include "custom_widgets.hpp"
#include "leyawin_globals.h"
#include "leyawin_safe.h"
#include "../Menu.h"
#include "../MenuTheme.h"
#include "../MenuType.h"
#include "../MenuMotion.h"
#include "../../Features/FeatureBinds.h"
#include "../imgui/imstb_textedit.h"
#include <Windows.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace ImGui;
using namespace ImStb;

namespace {
inline float leyawin_ui_scale() noexcept
{
    return ImClamp(
        static_cast<float>(Menu::s_UIScalePercent) / 100.f,
        1.f,
        2.f);
}

struct popup_w_state
{
    ImVec2 window_padding = ImVec2(10, 10);
    ImVec2 anchor_tl = ImVec2(0.f, 0.f);
    ImVec2 anchor_br = ImVec2(0.f, 0.f);
    bool window_opened = false;
    bool hovered = false;
    bool block_reopen = false;
    float begin_offset = 0.f;
    float alpha_popup = 0.f;
    int open_frame = -1;
};

inline LeyawinAnimTable<popup_w_state, 701>& popup_w_anim_table()
{
    return LeyawinAnim<popup_w_state, 701>();
}

popup_w_state& popup_w_get(const char* name)
{
    ImGuiWindow* window = GetCurrentWindow();
    return MapGet(popup_w_anim_table(), window->GetID(name));
}

// These panels are modeless but mutually exclusive: opening a second bind or
// overflow panel used to leave the previous one floating on screen.
void popup_w_close_others(ImGuiID keep)
{
    for (auto& entry : popup_w_anim_table().entries)
    {
        if (!entry.used || entry.key == keep)
            continue;
        entry.value.window_opened = false;
    }
}

popup_w_state& popup_w_get_close_target(const char* name)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window && window->Name)
    {
        char* end = nullptr;
        const unsigned long parsed =
            strtoul(window->Name, &end, 10);
        if (parsed != 0 &&
            end &&
            strcmp(end, "_popup") == 0)
        {
            return MapGet(
                popup_w_anim_table(),
                static_cast<ImGuiID>(parsed));
        }
    }
    return popup_w_get(name);
}
} // namespace

bool c_widgets::begin_popup_w(const char* name, float size_x, int open_mouse_button)
{
    ImGuiContext& g = *GImGui;
    popup_w_state& state = popup_w_get(name);

    if (!g.IO.MouseDown[0])
        state.block_reopen = false;

    const bool overAnchor = IsMouseHoveringRect(
        state.anchor_tl - ImVec2(4.f, 4.f),
        state.anchor_br + ImVec2(4.f, 4.f),
        false);

    if (open_mouse_button >= 0 && IsItemHovered() && g.IO.MouseClicked[open_mouse_button]) {
        state.window_opened = !state.window_opened;
        if (state.window_opened) {
            state.open_frame = g.FrameCount;
            state.anchor_tl = g.LastItemData.Rect.GetTL();
            state.anchor_br = g.LastItemData.Rect.GetBR();
            popup_w_close_others(GetCurrentWindow()->GetID(name));
        }
        else
            state.block_reopen = true;
    }
    else if (state.window_opened &&
        g.IO.MouseClicked[0] &&
        g.FrameCount > state.open_frame &&
        !overAnchor &&
        !state.hovered)
    {
        bool over_combo = false;
        for (ImGuiWindow* w : g.Windows) {
            if (!w || !w->Active || !w->Name)
                continue;
            if (!strstr(w->Name, "_combo_popup"))
                continue;
            if (IsMouseHoveringRect(w->Pos, w->Pos + w->Size, false)) {
                over_combo = true;
                break;
            }
        }
        if (!over_combo)
        {
            state.window_opened = false;
            state.block_reopen = true;
        }
    }

    state.begin_offset = ImLerp(state.begin_offset, state.window_opened ? -5.f : 15, gui->fixed_speed(10.f));
    state.alpha_popup = ImClamp(state.alpha_popup + (gui->fixed_speed(8.f) * (state.window_opened ? 1.f : -1.f)), 0.f, 1.f);

    if (!state.window_opened && state.alpha_popup <= 0.001f)
        return false;

    PushStyleVar(
        ImGuiStyleVar_Alpha, GetStyle().Alpha * state.alpha_popup);
    PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    PushStyleVar(ImGuiStyleVar_WindowPadding, state.window_padding);
    PushStyleColor(ImGuiCol_WindowBg, gui->get_clr(clr->combo_color));
    PushStyleColor(ImGuiCol_Border, gui->get_clr(clr->separator));

    ImVec2 popup_pos(
        state.anchor_tl.x - size_x - 8.f,
        state.anchor_tl.y);
    const ImVec2 display = g.IO.DisplaySize;
    if (popup_pos.x < 8.f)
        popup_pos.x = state.anchor_br.x + 8.f;
    if (popup_pos.x + size_x > display.x - 8.f)
        popup_pos.x = display.x - size_x - 8.f;
    if (popup_pos.x < 8.f)
        popup_pos.x = 8.f;
    if (popup_pos.y + 180.f > display.y)
        popup_pos.y = ImMax(8.f, state.anchor_tl.y - 180.f);
    if (popup_pos.y < 8.f)
        popup_pos.y = 8.f;

    SetNextWindowPos(popup_pos);
    SetNextWindowSize(ImVec2(size_x, 0.f));
    if (state.window_opened && g.FrameCount == state.open_frame)
        SetNextWindowFocus();

    ImGuiWindowFlags flags = set->popup_flags;
    if (!state.window_opened)
        flags |= ImGuiWindowFlags_NoInputs;

    char popup_name[64];
    wsprintfA(popup_name, "%u_popup", (unsigned)GetCurrentWindow()->GetID(name));
    gui->begin(popup_name, NULL, flags);

    state.hovered = state.window_opened &&
        IsMouseHoveringRect(GetWindowPos(), GetWindowPos() + GetWindowSize());

    return true;
}

bool c_widgets::is_popup_w_open(const char* name)
{
    return popup_w_get(name).window_opened;
}

void c_widgets::close_popup_w(const char* name)
{
    popup_w_state& state =
        popup_w_get_close_target(name);
    state.window_opened = false;
    state.block_reopen = true;
}

void c_widgets::dismiss_popup_w(const char* name)
{
    popup_w_state& state =
        popup_w_get_close_target(name);
    state.window_opened = false;
    state.block_reopen = true;
    state.alpha_popup = 0.f;
    state.hovered = false;
}

void c_widgets::open_popup_w(const char* name)
{
    ImGuiContext& g = *GImGui;
    open_popup_w(
        name,
        g.LastItemData.Rect.GetTL(),
        g.LastItemData.Rect.GetBR());
}

void c_widgets::open_popup_w(const char* name, const ImVec2& tl, const ImVec2& br)
{
    ImGuiContext& g = *GImGui;
    popup_w_state& state = popup_w_get(name);
    if (state.block_reopen)
        return;
    if (state.window_opened)
    {
        state.window_opened = false;
        state.block_reopen = true;
        return;
    }
    state.window_opened = true;
    state.open_frame = g.FrameCount;
    state.anchor_tl = tl;
    state.anchor_br = br;
    popup_w_close_others(GetCurrentWindow()->GetID(name));
}

void c_widgets::end_popup_w()
{
    gui->end();

    PopStyleVar(3);
    PopStyleColor(2);
}

void c_widgets::bring_popups_to_front()
{
    ImGuiContext& g = *GImGui;
    // Bind/key popups first, then combo lists on top so Mode Hold/Toggle stays clickable.
    for (ImGuiWindow* window : g.Windows) {
        if (!window->Active || !window->Name)
            continue;
        const char* name = window->Name;
        if (strstr(name, "_combo_popup"))
            continue;
        const char* marker = strstr(name, "_popup");
        if (!marker || marker[6] != '\0')
            continue;
        BringWindowToDisplayFront(window);
    }
    for (ImGuiWindow* window : g.Windows) {
        if (!window->Active || !window->Name)
            continue;
        if (!strstr(window->Name, "_combo_popup"))
            continue;
        BringWindowToDisplayFront(window);
    }
}

void c_widgets::separator()
{
    ImGuiWindow* window = GetCurrentWindow();
    const ImVec2 pos = GetCursorScreenPos();
    window->DrawList->AddLine(
        pos,
        pos + ImVec2(ImMax(GetContentRegionAvail().x, 1.f), 0.f),
        gui->get_clr(MenuTheme::Colors().borderSubtle, 1.f),
        MenuTheme::Sizes().stroke);
    Spacing();
}

#if 0 // Legacy leyawin input_text — incompatible with current ImGui InputTextState API
static ImVec2 input_text_calc_text_size_w(ImGuiContext* ctx, const ImWchar* text_begin, const ImWchar* text_end, const ImWchar** remaining = NULL, ImVec2* out_offset = NULL, bool stop_on_new_line = false)
{
    ImGuiContext& g = *ctx;
    ImFont* font = g.Font;
    const float line_height = g.FontSize;
    const float scale = line_height / font->FontSize;

    ImVec2 text_size = ImVec2(0, 0);
    float line_width = 0.0f;

    const ImWchar* s = text_begin;
    while (s < text_end)
    {
        unsigned int c = (unsigned int)(*s++);
        if (c == '\n')
        {
            text_size.x = ImMax(text_size.x, line_width);
            text_size.y += line_height;
            line_width = 0.0f;
            if (stop_on_new_line)
                break;
            continue;
        }
        if (c == '\r')
            continue;

        const float char_width = font->GetCharAdvance((ImWchar)c) * scale;
        line_width += char_width;
    }

    if (text_size.x < line_width)
        text_size.x = line_width;

    if (out_offset)
        *out_offset = ImVec2(line_width, text_size.y + line_height);  // offset allow for the possibility of sitting after a trailing \n

    if (line_width > 0 || text_size.y == 0.0f)                        // whereas size.y will ignore the trailing \n
        text_size.y += line_height;

    if (remaining)
        *remaining = s;

    return text_size;
}

static const char* items_array_getter(void* data, int idx)
{
    const char* const* items = (const char* const*)data;
    return items[idx];
}

namespace ImStb
{

    static int     STB_TEXTEDIT_STRINGLEN(const ImGuiInputTextState* obj) { return obj->CurLenW; }
    static ImWchar STB_TEXTEDIT_GETCHAR(const ImGuiInputTextState* obj, int idx) { IM_ASSERT(idx <= obj->CurLenW); return obj->TextW[idx]; }
    static float   STB_TEXTEDIT_GETWIDTH(ImGuiInputTextState* obj, int line_start_idx, int char_idx) { ImWchar c = obj->TextW[line_start_idx + char_idx]; if (c == '\n') return IMSTB_TEXTEDIT_GETWIDTH_NEWLINE; ImGuiContext& g = *obj->Ctx; return g.Font->GetCharAdvance(c) * (g.FontSize / g.Font->FontSize); }
    static int     STB_TEXTEDIT_KEYTOTEXT(int key) { return key >= 0x200000 ? 0 : key; }
    static ImWchar STB_TEXTEDIT_NEWLINE = '\n';
    static void    STB_TEXTEDIT_LAYOUTROW(StbTexteditRow* r, ImGuiInputTextState* obj, int line_start_idx)
    {
        const ImWchar* text = obj->TextW.Data;
        const ImWchar* text_remaining = NULL;
        const ImVec2 size = input_text_calc_text_size_w(obj->Ctx, text + line_start_idx, text + obj->CurLenW, &text_remaining, NULL, true);
        r->x0 = 0.0f;
        r->x1 = size.x;
        r->baseline_y_delta = size.y;
        r->ymin = 0.0f;
        r->ymax = size.y;
        r->num_chars = (int)(text_remaining - (text + line_start_idx));
    }

    static bool is_separator(unsigned int c)
    {
        return c == ',' || c == ';' || c == '(' || c == ')' || c == '{' || c == '}' || c == '[' || c == ']' || c == '|' || c == '\n' || c == '\r' || c == '.' || c == '!';
    }

    static int is_word_boundary_from_right(ImGuiInputTextState* obj, int idx)
    {
        // When ImGuiInputTextFlags_Password is set, we don't want actions such as CTRL+Arrow to leak the fact that underlying data are blanks or separators.
        if ((obj->Flags & ImGuiInputTextFlags_Password) || idx <= 0)
            return 0;

        bool prev_white = ImCharIsBlankW(obj->TextW[idx - 1]);
        bool prev_separ = is_separator(obj->TextW[idx - 1]);
        bool curr_white = ImCharIsBlankW(obj->TextW[idx]);
        bool curr_separ = is_separator(obj->TextW[idx]);
        return ((prev_white || prev_separ) && !(curr_separ || curr_white)) || (curr_separ && !prev_separ);
    }
    static int is_word_boundary_from_left(ImGuiInputTextState* obj, int idx)
    {
        if ((obj->Flags & ImGuiInputTextFlags_Password) || idx <= 0)
            return 0;

        bool prev_white = ImCharIsBlankW(obj->TextW[idx]);
        bool prev_separ = is_separator(obj->TextW[idx]);
        bool curr_white = ImCharIsBlankW(obj->TextW[idx - 1]);
        bool curr_separ = is_separator(obj->TextW[idx - 1]);
        return ((prev_white) && !(curr_separ || curr_white)) || (curr_separ && !prev_separ);
    }
    static int  STB_TEXTEDIT_MOVEWORDLEFT_IMPL(ImGuiInputTextState* obj, int idx) { idx--; while (idx >= 0 && !is_word_boundary_from_right(obj, idx)) idx--; return idx < 0 ? 0 : idx; }
    static int  STB_TEXTEDIT_MOVEWORDRIGHT_MAC(ImGuiInputTextState* obj, int idx) { idx++; int len = obj->CurLenW; while (idx < len && !is_word_boundary_from_left(obj, idx)) idx++; return idx > len ? len : idx; }
    static int  STB_TEXTEDIT_MOVEWORDRIGHT_WIN(ImGuiInputTextState* obj, int idx) { idx++; int len = obj->CurLenW; while (idx < len && !is_word_boundary_from_right(obj, idx)) idx++; return idx > len ? len : idx; }
    static int  STB_TEXTEDIT_MOVEWORDRIGHT_IMPL(ImGuiInputTextState* obj, int idx) { ImGuiContext& g = *obj->Ctx; if (g.IO.ConfigMacOSXBehaviors) return STB_TEXTEDIT_MOVEWORDRIGHT_MAC(obj, idx); else return STB_TEXTEDIT_MOVEWORDRIGHT_WIN(obj, idx); }
#define STB_TEXTEDIT_MOVEWORDLEFT   STB_TEXTEDIT_MOVEWORDLEFT_IMPL  // They need to be #define for stb_textedit.h
#define STB_TEXTEDIT_MOVEWORDRIGHT  STB_TEXTEDIT_MOVEWORDRIGHT_IMPL

    static void STB_TEXTEDIT_DELETECHARS(ImGuiInputTextState* obj, int pos, int n)
    {
        ImWchar* dst = obj->TextW.Data + pos;

        // We maintain our buffer length in both UTF-8 and wchar formats
        obj->Edited = true;
        obj->CurLenA -= ImTextCountUtf8BytesFromStr(dst, dst + n);
        obj->CurLenW -= n;

        // Offset remaining text (FIXME-OPT: Use memmove)
        const ImWchar* src = obj->TextW.Data + pos + n;
        while (ImWchar c = *src++)
            *dst++ = c;
        *dst = '\0';
    }

    static bool STB_TEXTEDIT_INSERTCHARS(ImGuiInputTextState* obj, int pos, const ImWchar* new_text, int new_text_len)
    {
        const bool is_resizable = (obj->Flags & ImGuiInputTextFlags_CallbackResize) != 0;
        const int text_len = obj->CurLenW;
        IM_ASSERT(pos <= text_len);

        const int new_text_len_utf8 = ImTextCountUtf8BytesFromStr(new_text, new_text + new_text_len);
        if (!is_resizable && (new_text_len_utf8 + obj->CurLenA + 1 > obj->BufCapacityA))
            return false;

        // Grow internal buffer if needed
        if (new_text_len + text_len + 1 > obj->TextW.Size)
        {
            if (!is_resizable)
                return false;
            IM_ASSERT(text_len < obj->TextW.Size);
            obj->TextW.resize(text_len + ImClamp(new_text_len * 4, 32, ImMax(256, new_text_len)) + 1);
        }

        ImWchar* text = obj->TextW.Data;
        if (pos != text_len)
            memmove(text + pos + new_text_len, text + pos, (size_t)(text_len - pos) * sizeof(ImWchar));
        memcpy(text + pos, new_text, (size_t)new_text_len * sizeof(ImWchar));

        obj->Edited = true;
        obj->CurLenW += new_text_len;
        obj->CurLenA += new_text_len_utf8;
        obj->TextW[obj->CurLenW] = '\0';

        return true;
    }

    // We don't use an enum so we can build even with conflicting symbols (if another user of stb_textedit.h leak their STB_TEXTEDIT_K_* symbols)
#define STB_TEXTEDIT_K_LEFT         0x200000 // keyboard input to move cursor left
#define STB_TEXTEDIT_K_RIGHT        0x200001 // keyboard input to move cursor right
#define STB_TEXTEDIT_K_UP           0x200002 // keyboard input to move cursor up
#define STB_TEXTEDIT_K_DOWN         0x200003 // keyboard input to move cursor down
#define STB_TEXTEDIT_K_LINESTART    0x200004 // keyboard input to move cursor to start of line
#define STB_TEXTEDIT_K_LINEEND      0x200005 // keyboard input to move cursor to end of line
#define STB_TEXTEDIT_K_TEXTSTART    0x200006 // keyboard input to move cursor to start of text
#define STB_TEXTEDIT_K_TEXTEND      0x200007 // keyboard input to move cursor to end of text
#define STB_TEXTEDIT_K_DELETE       0x200008 // keyboard input to delete selection or character under cursor
#define STB_TEXTEDIT_K_BACKSPACE    0x200009 // keyboard input to delete selection or character left of cursor
#define STB_TEXTEDIT_K_UNDO         0x20000A // keyboard input to perform undo
#define STB_TEXTEDIT_K_REDO         0x20000B // keyboard input to perform redo
#define STB_TEXTEDIT_K_WORDLEFT     0x20000C // keyboard input to move cursor left one word
#define STB_TEXTEDIT_K_WORDRIGHT    0x20000D // keyboard input to move cursor right one word
#define STB_TEXTEDIT_K_PGUP         0x20000E // keyboard input to move cursor up a page
#define STB_TEXTEDIT_K_PGDOWN       0x20000F // keyboard input to move cursor down a page
#define STB_TEXTEDIT_K_SHIFT        0x400000

#define IMSTB_TEXTEDIT_IMPLEMENTATION
#define IMSTB_TEXTEDIT_memmove memmove
#include "imstb_textedit.h"

// stb_textedit internally allows for a single undo record to do addition and deletion, but somehow, calling
// the stb_textedit_paste() function creates two separate records, so we perform it manually. (FIXME: Report to nothings/stb?)
    static void stb_textedit_replace(ImGuiInputTextState* str, STB_TexteditState* state, const IMSTB_TEXTEDIT_CHARTYPE* text, int text_len)
    {
        stb_text_makeundo_replace(str, state, 0, str->CurLenW, text_len);
        ImStb::STB_TEXTEDIT_DELETECHARS(str, 0, str->CurLenW);
        state->cursor = state->select_start = state->select_end = 0;
        if (text_len <= 0)
            return;
        if (ImStb::STB_TEXTEDIT_INSERTCHARS(str, 0, text, text_len))
        {
            state->cursor = state->select_start = state->select_end = text_len;
            state->has_preferred_x = 0;
            return;
        }
        IM_ASSERT(0); // Failed to insert character, normally shouldn't happen because of how we currently use stb_textedit_replace()
    }

} // namespace ImStb

#endif // legacy ImStb input helpers

ImU32 c_widgets::get_clr(const ImVec4& col, float alpha)
{
    ImGuiStyle& style = GImGui->Style;
    ImVec4 c = col;
    c.w *= style.Alpha * alpha;
    return ColorConvertFloat4ToU32(c);
}

void c_widgets::text_colored(const ImU32 col, const char* text)
{
    if (!text)
        return;
    const ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(col);
    ImGui::TextColored(rgba, "%s", text);
}

void c_widgets::render_text_clipped(ImFont* font, const ImVec2& p_min, const ImVec2& p_max, ImU32 col, const char* text, const ImVec2& align)
{
    if (!font || !text)
        return;
    PushFont(font);
    PushStyleColor(ImGuiCol_Text, col);
    RenderTextClipped(p_min, p_max, text, NULL, NULL, align, NULL);
    PopStyleColor();
    PopFont();
}


void c_widgets::set_cursor_pos(const ImVec2& local_pos)
{
    ImGui::SetCursorPos(local_pos);
}

void c_widgets::begin_group()
{
    ImGui::BeginGroup();
}

void c_widgets::end_group()
{
    ImGui::EndGroup();
}

void c_widgets::push_style_var(ImGuiStyleVar idx, const ImVec2& val)
{
    ImGui::PushStyleVar(idx, val);
}

void c_widgets::push_style_var(ImGuiStyleVar idx, const float val)
{
    ImGui::PushStyleVar(idx, val);
}

void c_widgets::pop_style_var(int num)
{
    ImGui::PopStyleVar(num);
}

void c_widgets::sameline()
{
    ImGui::SameLine(0.f, -1.f);
}

bool c_widgets::begin(const char* name, bool* open, ImGuiWindowFlags flags)
{
    return ImGui::Begin(name, open, flags);
}

void c_widgets::end()
{
    ImGui::End();
}

void c_widgets::add_rect_filled_multi_color(ImDrawList* draw, const ImVec2& p_min, const ImVec2& p_max, ImU32 col_upr_left, ImU32 col_upr_right, ImU32 col_bot_right, ImU32 col_bot_left, float rounding, ImDrawFlags flags)
{
    if (((col_upr_left | col_upr_right | col_bot_right | col_bot_left) & IM_COL32_A_MASK) == 0) return;

    auto fix_rect_corner_flags{ [](ImDrawFlags rflags) {    if ((rflags & ImDrawFlags_RoundCornersMask_) == 0)
                                                            rflags |= ImDrawFlags_RoundCornersAll;
                                                            return rflags;
                                                        } };

    flags = fix_rect_corner_flags(flags);
    rounding = ImMin(rounding, ImFabs(p_max.x - p_min.x) * (((flags & ImDrawFlags_RoundCornersTop) == ImDrawFlags_RoundCornersTop) || ((flags & ImDrawFlags_RoundCornersBottom) == ImDrawFlags_RoundCornersBottom) ? 0.5f : 1.0f) - 1.0f);
    rounding = ImMin(rounding, ImFabs(p_max.y - p_min.y) * (((flags & ImDrawFlags_RoundCornersLeft) == ImDrawFlags_RoundCornersLeft) || ((flags & ImDrawFlags_RoundCornersRight) == ImDrawFlags_RoundCornersRight) ? 0.5f : 1.0f) - 1.0f);

    if (rounding > 0.0f)
    {
        const int size_before = draw->VtxBuffer.Size;
        draw->AddRectFilled(p_min, p_max, IM_COL32_WHITE, rounding, flags);
        const int size_after = draw->VtxBuffer.Size;

        for (int i = size_before; i < size_after; i++)
        {
            ImDrawVert* vert = draw->VtxBuffer.Data + i;

            ImVec4 upr_left = ImGui::ColorConvertU32ToFloat4(col_upr_left);
            ImVec4 bot_left = ImGui::ColorConvertU32ToFloat4(col_bot_left);
            ImVec4 up_right = ImGui::ColorConvertU32ToFloat4(col_upr_right);
            ImVec4 bot_right = ImGui::ColorConvertU32ToFloat4(col_bot_right);

            float X = ImClamp((vert->pos.x - p_min.x) / (p_max.x - p_min.x), 0.0f, 1.0f);

            // 4 colors - 8 deltas

            float r1 = upr_left.x + (up_right.x - upr_left.x) * X;
            float r2 = bot_left.x + (bot_right.x - bot_left.x) * X;

            float g1 = upr_left.y + (up_right.y - upr_left.y) * X;
            float g2 = bot_left.y + (bot_right.y - bot_left.y) * X;

            float b1 = upr_left.z + (up_right.z - upr_left.z) * X;
            float b2 = bot_left.z + (bot_right.z - bot_left.z) * X;

            float a1 = upr_left.w + (up_right.w - upr_left.w) * X;
            float a2 = bot_left.w + (bot_right.w - bot_left.w) * X;


            float Y = ImClamp((vert->pos.y - p_min.y) / (p_max.y - p_min.y), 0.0f, 1.0f);
            float r = r1 + (r2 - r1) * Y;
            float g = g1 + (g2 - g1) * Y;
            float b = b1 + (b2 - b1) * Y;
            float a = a1 + (a2 - a1) * Y;
            ImVec4 RGBA(r, g, b, a);

            RGBA = RGBA * ImGui::ColorConvertU32ToFloat4(vert->col);

            vert->col = ImColor(RGBA);
        }
        return;
    }

    const ImVec2 uv = draw->_Data->TexUvWhitePixel;
    draw->PrimReserve(6, 4);
    draw->PrimWriteIdx((ImDrawIdx)(draw->_VtxCurrentIdx)); draw->PrimWriteIdx((ImDrawIdx)(draw->_VtxCurrentIdx + 1)); draw->PrimWriteIdx((ImDrawIdx)(draw->_VtxCurrentIdx + 2));
    draw->PrimWriteIdx((ImDrawIdx)(draw->_VtxCurrentIdx)); draw->PrimWriteIdx((ImDrawIdx)(draw->_VtxCurrentIdx + 2)); draw->PrimWriteIdx((ImDrawIdx)(draw->_VtxCurrentIdx + 3));
    draw->PrimWriteVtx(p_min, uv, col_upr_left);
    draw->PrimWriteVtx(ImVec2(p_max.x, p_min.y), uv, col_upr_right);
    draw->PrimWriteVtx(p_max, uv, col_bot_right);
    draw->PrimWriteVtx(ImVec2(p_min.x, p_max.y), uv, col_bot_left);
}

bool c_widgets::sub_selection(const char* label, int selection_id, int& selection_variable, const ImVec2& size)
{
    if (!label)
        return false;

    struct sub_selection_state
    {
        ImVec4 text = clr->text, icon = clr->accent_color;
        float opticaly = 0.f;
        float hover = 0.f;
    };

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const bool selected = selection_id == selection_variable;

    const ImVec2 pos = window->DC.CursorPos;
    const float uiScale = leyawin_ui_scale();
    ImVec2 actualSize = size;
    actualSize.x = ImMax(
        actualSize.x,
        ImGui::CalcTextSize(label, nullptr, true).x +
            28.f * uiScale);
    actualSize.y = ImMax(
        actualSize.y,
        ImGui::GetTextLineHeight() + 12.f * uiScale);

    // Key by widget ImGuiID — never share anim slots across different strips
    // that reuse the same selection_id (e.g. WM categories vs detail tabs).
    auto& anim = LEYAWIN_ANIM(ImGuiID, sub_selection_state);
    sub_selection_state& state = MapGet(anim, id);

    const ImRect rect(pos, pos + actualSize);

    ItemSize(rect, 0);
    if (!ItemAdd(rect, id)) return false;

    bool hovered, held, pressed = ButtonBehavior(rect, id, &hovered, &held);
    if (pressed) selection_variable = selection_id;

    state.opticaly = ImLerp(state.opticaly, selected ? 1.f : 0.f, gui->fixed_speed(6.f));
    state.hover = ImLerp(
        state.hover,
        hovered ? 1.f : 0.f,
        gui->fixed_speed(14.f));
    state.icon = ImLerp(state.icon, selected ? clr->accent_color : clr->text, gui->fixed_speed(8.f));
    state.text = ImLerp(state.text, selected ? clr->text_active : clr->text, gui->fixed_speed(8.f));

    window->DrawList->AddRectFilled(
        rect.Min,
        rect.Max,
        get_clr(
            clr->element_background,
            0.18f + state.hover * 0.42f),
        set->selection_rounding);
    window->DrawList->AddRectFilled(
        rect.Min,
        rect.Max,
        get_clr(clr->selection_active, state.opticaly * 0.96f),
        set->selection_rounding);
    window->DrawList->AddRect(
        rect.Min,
        rect.Max,
        get_clr(
            selected ? clr->accent_color : clr->child_stroke_bar,
            selected ? 0.34f : 0.55f + state.hover * 0.3f),
        set->selection_rounding);
    window->DrawList->AddRectFilled(
        rect.Min + ImVec2(
            actualSize.x / 3.f,
            actualSize.y - 2.f * uiScale),
        rect.Max - ImVec2(actualSize.x / 3.f, 0.f),
        get_clr(clr->accent_color, state.opticaly),
        set->selection_rounding);

    gui->render_text_clipped(set->poppins_selection, rect.Min, rect.Max, gui->get_clr(state.text), label, { 0.5, 0.5 });

    return pressed;
}

bool c_widgets::sub_selection(
    const char* icon_label,
    const char* label,
    int selection_id,
    int& selection_variable,
    const ImVec2& size)
{
    if (!icon_label || !label)
        return false;

    struct sub_selection_icon_state
    {
        ImVec4 text = clr->text;
        ImVec4 icon = clr->text;
        float opticaly = 0.f;
        float hover = 0.f;
    };

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    const ImGuiID id = window->GetID(label);
    const bool selected =
        selection_id == selection_variable;
    const ImVec2 pos = window->DC.CursorPos;
    const float uiScale = leyawin_ui_scale();
    ImVec2 actualSize = size;
    actualSize.y = ImMax(
        actualSize.y,
        ImGui::GetTextLineHeight() + 12.f * uiScale);
    const float minimumIconWidth =
        actualSize.y * 0.82f;
    actualSize.x = ImMax(
        actualSize.x,
        minimumIconWidth +
            ImGui::CalcTextSize(label, nullptr, true).x +
            18.f * uiScale);
    const ImRect rect(pos, pos + actualSize);
    auto& anim = LEYAWIN_ANIM(ImGuiID, sub_selection_icon_state);
    sub_selection_icon_state& state = MapGet(anim, id);

    ItemSize(rect, 0);
    if (!ItemAdd(rect, id))
        return false;

    bool hovered = false;
    bool held = false;
    const bool pressed = ButtonBehavior(
        rect,
        id,
        &hovered,
        &held);
    if (pressed)
        selection_variable = selection_id;

    state.opticaly = ImLerp(
        state.opticaly,
        selected ? 1.f : 0.f,
        gui->fixed_speed(6.f));
    state.hover = ImLerp(
        state.hover,
        hovered ? 1.f : 0.f,
        gui->fixed_speed(14.f));
    state.icon = ImLerp(
        state.icon,
        selected ? clr->accent_color : clr->text,
        gui->fixed_speed(8.f));
    state.text = ImLerp(
        state.text,
        selected ? clr->text_active : clr->text,
        gui->fixed_speed(8.f));

    window->DrawList->AddRectFilled(
        rect.Min,
        rect.Max,
        get_clr(
            clr->element_background,
            0.18f + state.hover * 0.42f),
        set->selection_rounding);
    window->DrawList->AddRectFilled(
        rect.Min,
        rect.Max,
        get_clr(
            clr->selection_active,
            state.opticaly * 0.96f),
        set->selection_rounding);
    window->DrawList->AddRect(
        rect.Min,
        rect.Max,
        get_clr(
            selected ? clr->accent_color : clr->child_stroke_bar,
            selected ? 0.34f : 0.55f + state.hover * 0.3f),
        set->selection_rounding);
    window->DrawList->AddRectFilled(
        rect.Min + ImVec2(
            actualSize.x / 3.f,
            actualSize.y - 2.f * uiScale),
        rect.Max - ImVec2(actualSize.x / 3.f, 0.f),
        get_clr(
            clr->accent_color,
            state.opticaly),
        set->selection_rounding);

    const float iconWidth = actualSize.y * 0.82f;
    gui->render_text_clipped(
        set->icon,
        rect.Min,
        ImVec2(rect.Min.x + iconWidth, rect.Max.y),
        gui->get_clr(state.icon),
        icon_label,
        {0.5f, 0.5f});
    gui->render_text_clipped(
        set->poppins_selection,
        ImVec2(rect.Min.x + iconWidth, rect.Min.y),
        rect.Max,
        gui->get_clr(state.text),
        label,
        {0.0f, 0.5f});
    return pressed;
}

bool c_widgets::selection(const char* icon_label, const char* label, int selection_id, int& selection_variable, const ImVec2& size)
{
    if (!icon_label || !label)
        return false;

    struct selection_state
    {
        ImVec4 text{};
        ImVec4 icon{};
        float opticaly = 0.f;
    };

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiID id = window->GetID(icon_label);
    const bool selected = selection_id == selection_variable;

    const ImVec2 pos = window->DC.CursorPos;

    static LeyawinAnimArray<selection_state, 16> s_anim;
    selection_state& state = s_anim.get(selection_id);

    const ImRect rect(pos, pos + size);

    ItemSize(rect, 0);
    if (!ItemAdd(rect, id)) return false;

    bool hovered, held, pressed = ButtonBehavior(rect, id, &hovered, &held);
    if (pressed) selection_variable = selection_id;

    state.opticaly = ImLerp(state.opticaly, selected ? 1.f : 0.f, gui->fixed_speed(6.f));
    state.icon = ImLerp(state.icon, selected ? clr->accent_color : clr->text, gui->fixed_speed(8.f));
    state.text = ImLerp(state.text, selected ? clr->text_active : clr->text, gui->fixed_speed(8.f));

    window->DrawList->AddRectFilled(rect.Min, rect.Max, get_clr(clr->selection_active, state.opticaly), set->selection_rounding);
    window->DrawList->AddRectFilled(rect.Max - ImVec2(19, (size.y - 15)), rect.Max - ImVec2(15, 15), get_clr(clr->accent_color, state.opticaly), set->selection_rounding);

    gui->render_text_clipped(set->icon, rect.Min, rect.Min + ImVec2(size.y, size.y), gui->get_clr(state.icon), icon_label, {0.5, 0.5});
    gui->render_text_clipped(set->poppins_selection, rect.Min + ImVec2(size.y, 0), rect.Max, gui->get_clr(state.text), label, {0.0, 0.5});

    return pressed;
}

bool c_widgets::nav_rail_item(
    const char* id,
    const char* icon_label,
    const char* tooltip,
    int selection_id,
    int& selection_variable,
    const ImVec2& size)
{
    if (!id || !icon_label)
        return false;

    struct nav_state
    {
        ImVec4 icon{};
        float active{};
        float hover{};
    };

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;
    const ImGuiID itemId = window->GetID(id);
    const bool selected = selection_id == selection_variable;
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect rect(pos, pos + size);
    ItemSize(rect, 0.f);
    if (!ItemAdd(rect, itemId))
        return false;

    bool hovered = false;
    bool held = false;
    const bool pressed =
        ButtonBehavior(rect, itemId, &hovered, &held);
    if (pressed)
        selection_variable = selection_id;

    static LeyawinAnimArray<nav_state, 16> s_anim;
    nav_state& state = s_anim.get(selection_id);
    state.active = ImLerp(
        state.active,
        selected ? 1.f : 0.f,
        gui->fixed_speed(10.f));
    state.hover = ImLerp(
        state.hover,
        hovered ? 1.f : 0.f,
        gui->fixed_speed(12.f));
    state.icon = ImLerp(
        state.icon,
        selected ? clr->accent_color : clr->text,
        gui->fixed_speed(10.f));

    const float rounding =
        ImMin(set->child_rounding, size.y * 0.24f);
    window->DrawList->AddRectFilled(
        rect.Min,
        rect.Max,
        get_clr(
            clr->element_background,
            0.35f + state.hover * 0.35f +
                state.active * 0.25f),
        rounding);
    if (state.active > 0.01f)
    {
        window->DrawList->AddRectFilled(
            ImVec2(rect.Min.x, rect.Min.y + 9.f),
            ImVec2(rect.Min.x + 3.f, rect.Max.y - 9.f),
            get_clr(clr->accent_color, state.active),
            2.f);
        window->DrawList->AddRect(
            rect.Min,
            rect.Max,
            get_clr(clr->accent_color, state.active * 0.18f),
            rounding);
    }
    gui->render_text_clipped(
        set->icon,
        rect.Min,
        rect.Max,
        gui->get_clr(state.icon),
        icon_label,
        {0.5f, 0.5f});

    if (hovered && tooltip && tooltip[0])
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(tooltip);
        ImGui::EndTooltip();
    }
    return pressed;
}

void c_widgets::begin_content(const ImVec2& size_arg, ImGuiWindowFlags extra_flags)
{
    PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.f, 10.f));
    PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 10.f));
    ImVec2 size = size_arg;
    const ImVec2 avail = GetContentRegionAvail();
    if (size.x <= 0.f)
        size.x = avail.x;
    if (size.y <= 0.f)
        size.y = avail.y;
    BeginChild("##leyawin_content", size, ImGuiChildFlags_None,
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | extra_flags);
}

void c_widgets::end_content()
{
    EndChild();
    ImGui::PopStyleVar(2);
}

bool begin_child_ex(const char* icon, const char* name, ImGuiID id, const ImVec2& size_arg, ImGuiChildFlags child_flags, ImGuiWindowFlags window_flags)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* parent_window = g.CurrentWindow;
    IM_ASSERT(id != 0);

    const ImGuiChildFlags ImGuiChildFlags_SupportedMask_ = ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_ResizeX | ImGuiChildFlags_ResizeY | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize | ImGuiChildFlags_FrameStyle;
    IM_UNUSED(ImGuiChildFlags_SupportedMask_);
    IM_ASSERT((child_flags & ~ImGuiChildFlags_SupportedMask_) == 0 && "Illegal ImGuiChildFlags value. Did you pass ImGuiWindowFlags values instead of ImGuiChildFlags?");
    IM_ASSERT((window_flags & ImGuiWindowFlags_AlwaysAutoResize) == 0 && "Cannot specify ImGuiWindowFlags_AlwaysAutoResize for BeginChild(). Use ImGuiChildFlags_AlwaysAutoResize!");
    if (child_flags & ImGuiChildFlags_AlwaysAutoResize)
    {
        IM_ASSERT((child_flags & (ImGuiChildFlags_ResizeX | ImGuiChildFlags_ResizeY)) == 0 && "Cannot use ImGuiChildFlags_ResizeX or ImGuiChildFlags_ResizeY with ImGuiChildFlags_AlwaysAutoResize!");
        IM_ASSERT((child_flags & (ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY)) != 0 && "Must use ImGuiChildFlags_AutoResizeX or ImGuiChildFlags_AutoResizeY with ImGuiChildFlags_AlwaysAutoResize!");
    }
#ifndef IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    if (window_flags & ImGuiWindowFlags_AlwaysUseWindowPadding)
        child_flags |= ImGuiChildFlags_AlwaysUseWindowPadding;
#endif
    if (child_flags & ImGuiChildFlags_AutoResizeX) child_flags &= ~ImGuiChildFlags_ResizeX;
    if (child_flags & ImGuiChildFlags_AutoResizeY) child_flags &= ~ImGuiChildFlags_ResizeY;

    window_flags |= ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_NoTitleBar;
    window_flags |= (parent_window->Flags & ImGuiWindowFlags_NoMove);

    if (child_flags & (ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize)) window_flags |= ImGuiWindowFlags_AlwaysAutoResize;
    if ((child_flags & (ImGuiChildFlags_ResizeX | ImGuiChildFlags_ResizeY)) == 0) window_flags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;

    if (child_flags & ImGuiChildFlags_FrameStyle)
    {
        PushStyleColor(ImGuiCol_ChildBg, g.Style.Colors[ImGuiCol_FrameBg]);
        PushStyleVar(ImGuiStyleVar_ChildRounding, g.Style.FrameRounding);
        PushStyleVar(ImGuiStyleVar_ChildBorderSize, g.Style.FrameBorderSize);
        PushStyleVar(ImGuiStyleVar_WindowPadding, g.Style.FramePadding);
        child_flags |= ImGuiChildFlags_Border | ImGuiChildFlags_AlwaysUseWindowPadding;
        window_flags |= ImGuiWindowFlags_NoMove;
    }

    g.NextWindowData.Flags |= ImGuiNextWindowDataFlags_HasChildFlags;
    g.NextWindowData.ChildFlags = child_flags;

    auto& anim = LEYAWIN_ANIM(ImGuiID, float);
    float& state = MapGet(anim, id);

    ImVec2 size = size_arg;
    if (size.x <= 0) size.x = (GetContentRegionMax().x - g.Style.ItemSpacing.x - g.Style.ScrollbarSize - (set->widgets_padding.x + 2)) / 2;
    if (size.y <= 0) size.y = state;
    if (size.y <= 0.f) size.y = 120.f;

    SetNextWindowSize(size);
    SetNextWindowPos(parent_window->DC.CursorPos + ImVec2(0, 40));

    const char* temp_window_name;

    if (name) ImFormatStringToTempBuffer(&temp_window_name, NULL, "%s/%s_%08X", parent_window->Name, name, id);
    else ImFormatStringToTempBuffer(&temp_window_name, NULL, "%s/%08X", parent_window->Name, id);

    const float backup_border_size = g.Style.ChildBorderSize;
    if ((child_flags & ImGuiChildFlags_Border) == 0) g.Style.ChildBorderSize = 0.0f;

    // Panel chrome on tokens: no accent tick, no decorative corner glyph.
    const MenuTheme::Palette& panelTokens = MenuTheme::Colors();
    const MenuTheme::Metrics& panelMetrics = MenuTheme::Sizes();
    const float panelScale = leyawin_ui_scale();
    const float headerH = 40.f;
    const ImVec2 panelMin = parent_window->DC.CursorPos;
    const ImVec2 panelMax =
        parent_window->DC.CursorPos + size + ImVec2(0.f, headerH);
    parent_window->DrawList->AddRectFilled(
        panelMin,
        panelMax,
        gui->get_clr(panelTokens.base, 1.f),
        panelMetrics.radiusPanel * panelScale);
    parent_window->DrawList->AddLine(
        panelMin + ImVec2(0.f, headerH),
        panelMin + ImVec2(size.x, headerH),
        gui->get_clr(panelTokens.borderSubtle, 1.f),
        panelMetrics.stroke);
    parent_window->DrawList->AddRect(
        panelMin,
        panelMax,
        gui->get_clr(panelTokens.borderSubtle, 1.f),
        panelMetrics.radiusPanel * panelScale,
        0,
        panelMetrics.stroke);

    gui->render_text_clipped(
        set->icon_child,
        panelMin,
        panelMin + ImVec2(headerH, headerH),
        gui->get_clr(panelTokens.textFaint, 1.f),
        icon,
        { 0.5, 0.5 });
    gui->render_text_clipped(
        set->poppins_selection,
        panelMin + ImVec2(headerH, 0.f),
        panelMin + ImVec2(size.x, headerH),
        gui->get_clr(panelTokens.textPrimary, 1.f),
        name,
        {0.0, 0.5});

    const bool ret = Begin(temp_window_name, NULL, window_flags);

    if (child_flags & ImGuiChildFlags_FrameStyle)
    {
        PopStyleVar(3);
        PopStyleColor();
    }

    ImGuiWindow* child_window = g.CurrentWindow;
    child_window->ChildId = id;
    state = child_window->ContentSize.y + (GetStyle().WindowPadding.y * 2);

    if (child_window->BeginCount == 1) parent_window->DC.CursorPos = child_window->Pos;

    const ImGuiID temp_id_for_activation = ImHashStr("##Child", 0, id);
    if (g.ActiveId == temp_id_for_activation) ClearActiveID();
    if (g.NavActivateId == id && !(window_flags & ImGuiWindowFlags_NavFlattened) && (child_window->DC.NavLayersActiveMask != 0 || child_window->DC.NavWindowHasScrollY))
    {
        FocusWindow(child_window);
        NavInitWindow(child_window, false);
        SetActiveID(temp_id_for_activation, child_window);
        g.ActiveIdSource = g.NavInputSource;
    }
    return ret;
}

bool c_widgets::begin_child(const char* icon, const char* name, const ImVec2& size_arg)
{
    ImGuiID id = GetCurrentWindow()->GetID(name);
    PushStyleVar(ImGuiStyleVar_WindowPadding, set->widgets_padding);
    PushStyleVar(ImGuiStyleVar_ItemSpacing, set->widgets_spacing);
    return begin_child_ex(icon, name, id, size_arg, ImGuiChildFlags_None, ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoMove);
}

void c_widgets::end_child()
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* child_window = g.CurrentWindow;

    IM_ASSERT(g.WithinEndChild == false);
    IM_ASSERT(child_window->Flags & ImGuiWindowFlags_ChildWindow);

    g.WithinEndChild = true;
    ImVec2 child_size = child_window->Size;
    ImGui::End();
    if (child_window->BeginCount == 1)
    {
        ImGuiWindow* parent_window = g.CurrentWindow;
        ImRect bb(parent_window->DC.CursorPos, parent_window->DC.CursorPos + child_size);
        ItemSize(child_size);
        if ((child_window->DC.NavLayersActiveMask != 0 || child_window->DC.NavWindowHasScrollY) && !(child_window->Flags & ImGuiWindowFlags_NavFlattened))
        {
            ItemAdd(bb, child_window->ChildId);
            RenderNavHighlight(bb, child_window->ChildId);

            if (child_window->DC.NavLayersActiveMask == 0 && child_window == g.NavWindow) RenderNavHighlight(ImRect(bb.Min - ImVec2(2, 2), bb.Max + ImVec2(2, 2)), g.NavId, ImGuiNavHighlightFlags_Compact);
        }
        else
        {
            ItemAdd(bb, 0);
            if (child_window->Flags & ImGuiWindowFlags_NavFlattened) parent_window->DC.NavLayersActiveMaskNext |= child_window->DC.NavLayersActiveMaskNext;
        }
        if (g.HoveredWindow == child_window)
            g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_HoveredWindow;
    }
    PopStyleVar(2);
    g.WithinEndChild = false;
    g.LogLinePosY = -FLT_MAX;
}

static void RenderFeatureBindPopup(
    FeatureBinds::Binding* binding,
    const char* popupName)
{
    if (!binding || !popupName)
        return;
    // The checkbox already opens this popup explicitly on right-click.
    // Disable begin_popup_w's own click-toggle or the same click closes it
    // again in the very same frame.
    if (!gui->begin_popup_w(popupName, 220, -1))
        return;

    FeatureBinds::SetCaptureActive(true);
    ImGui::TextDisabled("%s", binding->label);
    if (gui->keybind("Key##feature_bind", &binding->key))
        FeatureBinds::ResetEdge(binding);
    gui->separator();
    const char* modes[] = {"Hold", "Toggle"};
    gui->dropdown(
        "Mode##feature_bind",
        &binding->mode,
        modes,
        IM_ARRAYSIZE(modes),
        2);
    gui->separator();
    if (gui->button(
            "Clear bind##feature_bind",
            ImVec2(200.f, 30.f)))
    {
        binding->key = 0;
        binding->previousDown = false;
    }
    gui->end_popup_w();
}

static const char* key_label(int vk) noexcept;

bool c_widgets::checkbox(const char* label, bool* callback, bool popup, int* key, int* mode, bool* show_in_binds, bool bindable)
{
    struct checkbox_state
    {
        float on = 0.f, hot = 0.f, press = 0.f;
    };

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;

    PushID(callback);
    const ImGuiID id = window->GetID(label);

    const MenuTheme::Palette& tokens = MenuTheme::Colors();
    const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
    const float uiScale = leyawin_ui_scale();

    // Identical geometry to MenuUi::SettingRow: one toggle shape everywhere.
    const ImVec2 pos = window->DC.CursorPos;
    const float width = ImMax(GetContentRegionAvail().x, 1.f);
    const float rect_size = metrics.rowHeight * uiScale;
    const float pad = metrics.space2 * uiScale;
    const float toggleW = 36.f * uiScale;
    const float toggleH = 20.f * uiScale;

    const ImRect rect(pos, pos + ImVec2(width, rect_size));
    const ImRect box(
        pos + ImVec2(width - pad - toggleW, (rect_size - toggleH) * 0.5f),
        pos + ImVec2(width - pad, (rect_size + toggleH) * 0.5f));

    ItemSize(rect, 0);
    if (!ItemAdd(rect, id)) {
        PopID();
        return false;
    }

    bool hovered, held, pressed = ButtonBehavior(rect, id, &hovered, &held);
    FeatureBinds::Binding* binding = bindable
        ? FeatureBinds::Register(callback, label)
        : nullptr;
    char bindPopup[64]{};
    sprintf_s(
        bindPopup,
        "##feature_bind_%p",
        static_cast<void*>(callback));
    if (bindable &&
        hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        // The hit area is the whole row, so the toggle box is passed as the
        // anchor to keep the panel next to the switch that was clicked.
        gui->open_popup_w(bindPopup, box.Min, box.Max);
    }

    if (pressed)
    {
        *callback = !(*callback);
        MarkItemEdited(id);
    }

    auto& anim = LEYAWIN_ANIM(ImGuiID, checkbox_state);
    checkbox_state& state = MapGet(anim, id);
    state.on = MenuMotion::Approach(
        state.on, *callback ? 1.f : 0.f, MenuMotion::kSelection);
    state.hot = MenuMotion::Approach(
        state.hot, hovered ? 1.f : 0.f, MenuMotion::kHover);
    state.press = MenuMotion::Approach(
        state.press, held ? 1.f : 0.f, MenuMotion::kPress);

    ImDrawList* draw = window->DrawList;
    // The row keeps no hover or pressed plate; the pill carries the state.
    draw->AddRectFilled(
        box.Min,
        box.Max,
        gui->get_clr(ImLerp(tokens.raised, tokens.accent.fill, state.on), 1.f),
        toggleH * 0.5f);
    if (state.on < 0.99f)
    {
        draw->AddRect(
            box.Min,
            box.Max,
            gui->get_clr(tokens.borderStrong, 1.f - state.on),
            toggleH * 0.5f,
            0,
            metrics.stroke);
    }
    draw->AddCircleFilled(
        ImVec2(
            box.Min.x + toggleH * 0.5f + (toggleW - toggleH) * state.on,
            (box.Min.y + box.Max.y) * 0.5f),
        toggleH * 0.5f - 2.f * uiScale,
        gui->get_clr(tokens.textPrimary, 1.f),
        16);

    (void)popup;
    (void)key;
    (void)mode;
    (void)show_in_binds;
    RenderFeatureBindPopup(binding, bindPopup);

    float labelMaxX = box.Min.x - metrics.space2 * uiScale;
    if (binding && binding->key)
    {
        // Show the actual key instead of a glyph nobody can decode.
        const char* keyText = key_label(binding->key);
        ImFont* monoFont = MenuType::Mono();
        if (keyText && monoFont)
        {
            const float monoSize = MenuType::MonoSize();
            const ImVec2 sz =
                monoFont->CalcTextSizeA(monoSize, FLT_MAX, 0.f, keyText);
            draw->AddText(
                monoFont,
                monoSize,
                ImVec2(
                    labelMaxX - sz.x,
                    rect.Min.y + (rect_size - sz.y) * 0.5f),
                gui->get_clr(tokens.textFaint, 1.f),
                keyText);
            labelMaxX -= sz.x + metrics.space2 * uiScale;
        }
    }

    gui->render_text_clipped(
        set->poppins_widget,
        rect.Min + ImVec2(pad, 0.f),
        ImVec2(labelMaxX, rect.Max.y),
        gui->get_clr(
            ImLerp(tokens.textMuted, tokens.textPrimary, ImMax(state.on, state.hot)),
            1.f),
        label,
        {0.0, 0.5});

    PopID();
    return pressed;
}

bool c_widgets::checkbox_with_picker(const char* label, bool* callback, float col[3], ImGuiColorEditFlags flags, bool bindable)
{
    struct checkbox_state
    {
        ImVec4 background = clr->element_background, text = clr->text;
        ImVec2 offset, pump;
        float alpha = 0.f, alpha_mark = 0.f, mark_size;
    };

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    (void)g;
    PushID(callback);
    const ImVec2 pos = window->DC.CursorPos;
    const float uiScale = leyawin_ui_scale();
    const float width =
        GetContentRegionMax().x - g.Style.WindowPadding.x;
    const MenuTheme::Palette& tokens = MenuTheme::Colors();
    const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
    const float toggleW = 36.f * uiScale;
    const float toggleH = 20.f * uiScale;
    const float rect_size = metrics.rowHeight * uiScale;
    const float swatchD = 16.f * uiScale;
    const float pad = metrics.space2 * uiScale;

    const ImRect rect(pos, pos + ImVec2(width, rect_size));
    const ImRect box(
        pos + ImVec2(width - pad - toggleW, (rect_size - toggleH) * 0.5f),
        pos + ImVec2(width - pad, (rect_size + toggleH) * 0.5f));
    const ImRect icon_rect(
        ImVec2(
            box.Min.x - pad - swatchD,
            pos.y + (rect_size - swatchD) * 0.5f),
        ImVec2(
            box.Min.x - pad,
            pos.y + (rect_size + swatchD) * 0.5f));

    char key_popup[128];
    wsprintfA(key_popup, "%skeywindow", label);
    FeatureBinds::Binding* binding = bindable
        ? FeatureBinds::Register(callback, label)
        : nullptr;
    char bindPopup[64]{};
    sprintf_s(
        bindPopup,
        "##feature_bind_%p",
        static_cast<void*>(callback));

    ItemSize(rect, 0);

    SetCursorScreenPos(icon_rect.Min);
    const bool picker_pressed = InvisibleButton("##picker", icon_rect.GetSize());
    if (picker_pressed)
        open_popup_w(key_popup);
    if (bindable &&
        ImGui::IsMouseHoveringRect(
            rect.Min,
            rect.Max) &&
        ImGui::IsMouseClicked(
            ImGuiMouseButton_Right))
    {
        // Without an explicit anchor this took the colour swatch rect, which
        // is the last item submitted above.
        open_popup_w(bindPopup, box.Min, box.Max);
    }

    PushID("toggle");
    const ImGuiID box_id = window->GetID("##box");
    if (!ItemAdd(box, box_id)) {
        PopID();
        PopID();
        return false;
    }

    bool hovered, held, pressed = ButtonBehavior(box, box_id, &hovered, &held);

    if (pressed)
    {
        *callback = !(*callback);
        MarkItemEdited(box_id);
    }
    PopID();

    auto& anim = LEYAWIN_ANIM(ImGuiID, checkbox_state);
    checkbox_state& state = MapGet(anim, box_id);

    state.background = ImLerp(state.background, *callback ? tokens.accent.fill : tokens.raised, gui->fixed_speed(12.f));
    state.alpha = ImLerp(state.alpha, *callback ? 1.f : 0.f, gui->fixed_speed(10.f));
    state.text = ImLerp(state.text, *callback ? tokens.textPrimary : tokens.textMuted, gui->fixed_speed(8.f));

    window->DrawList->AddRectFilled(
        box.Min,
        box.Max,
        gui->get_clr(state.background),
        toggleH * 0.5f);
    if (state.alpha < 0.99f)
    {
        window->DrawList->AddRect(
            box.Min,
            box.Max,
            gui->get_clr(tokens.borderStrong, 1.f - state.alpha),
            toggleH * 0.5f,
            0,
            metrics.stroke);
    }
    window->DrawList->AddCircleFilled(
        ImVec2(
            box.Min.x + toggleH * 0.5f + (toggleW - toggleH) * state.alpha,
            (box.Min.y + box.Max.y) * 0.5f),
        toggleH * 0.5f - 2.f * uiScale,
        gui->get_clr(tokens.textPrimary, 1.f),
        16);

    if (gui->begin_popup_w(key_popup, 210, -1)) {
        float col4[4] = { col[0], col[1], col[2], 1.f };
        gui->color_picker("picker", col4, flags | ImGuiColorEditFlags_NoAlpha, NULL);
        col[0] = col4[0];
        col[1] = col4[1];
        col[2] = col4[2];
        gui->end_popup_w();
    }
    RenderFeatureBindPopup(binding, bindPopup);

    window->DrawList->AddCircleFilled(
        icon_rect.GetCenter(),
        swatchD * 0.5f,
        gui->get_clr(ImColor(col[0], col[1], col[2], 1.f)),
        20);
    window->DrawList->AddCircle(
        icon_rect.GetCenter(),
        swatchD * 0.5f,
        gui->get_clr(tokens.borderStrong, 1.f),
        20,
        metrics.stroke);
    gui->render_text_clipped(
        set->poppins_widget,
        rect.Min + ImVec2(pad, 0.f),
        ImVec2(icon_rect.Min.x - pad, rect.Max.y),
        gui->get_clr(state.text),
        label,
        { 0.0, 0.5 });

    PopID();
    return pressed;
}

template<typename TYPE, typename SIGNEDTYPE, typename FLOATTYPE>
bool slider_behavior_t(const ImRect& bb, ImGuiID id, ImGuiDataType data_type, TYPE* v, const TYPE v_min, const TYPE v_max, const char* format, ImGuiSliderFlags flags, ImRect* out_grab_bb)
{
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    const ImGuiAxis axis = (flags & ImGuiSliderFlags_Vertical) ? ImGuiAxis_Y : ImGuiAxis_X;
    const bool is_logarithmic = (flags & ImGuiSliderFlags_Logarithmic) != 0;
    const bool is_floating_point = (data_type == ImGuiDataType_Float) || (data_type == ImGuiDataType_Double);
    const float v_range_f = (float)(v_min < v_max ? v_max - v_min : v_min - v_max); // We don't need high precision for what we do with it.

    // Calculate bounds
    const float grab_padding = 2.0f; // FIXME: Should be part of style.
    const float slider_sz = (bb.Max[axis] - bb.Min[axis]) - grab_padding * 2.0f;
    float grab_sz = 13.f;
    const float slider_usable_sz = slider_sz - grab_sz;
    const float slider_usable_pos_min = bb.Min[axis] + grab_padding + grab_sz * 0.5f;
    const float slider_usable_pos_max = bb.Max[axis] - grab_padding - grab_sz * 0.5f;

    float logarithmic_zero_epsilon = 0.0f; // Only valid when is_logarithmic is true
    float zero_deadzone_halfsize = 0.0f; // Only valid when is_logarithmic is true
    if (is_logarithmic)
    {
        // When using logarithmic sliders, we need to clamp to avoid hitting zero, but our choice of clamp value greatly affects slider precision. We attempt to use the specified precision to estimate a good lower bound.
        const int decimal_precision = is_floating_point ? ImParseFormatPrecision(format, 3) : 1;
        logarithmic_zero_epsilon = ImPow(0.1f, (float)decimal_precision);
        zero_deadzone_halfsize = (style.LogSliderDeadzone * 0.5f) / ImMax(slider_usable_sz, 1.0f);
    }

    // Process interacting with the slider
    bool value_changed = false;
    if (g.ActiveId == id)
    {
        bool set_new_value = false;
        float clicked_t = 0.0f;
        if (g.ActiveIdSource == ImGuiInputSource_Mouse)
        {
            if (!g.IO.MouseDown[0])
            {
                ClearActiveID();
            }
            else
            {
                const float mouse_abs_pos = g.IO.MousePos[axis];
                if (g.ActiveIdIsJustActivated)
                {
                    float grab_t = ScaleRatioFromValueT<TYPE, SIGNEDTYPE, FLOATTYPE>(data_type, *v, v_min, v_max, is_logarithmic, logarithmic_zero_epsilon, zero_deadzone_halfsize);
                    if (axis == ImGuiAxis_Y)
                        grab_t = 1.0f - grab_t;
                    const float grab_pos = ImLerp(slider_usable_pos_min, slider_usable_pos_max, grab_t);
                    const bool clicked_around_grab = (mouse_abs_pos >= grab_pos - grab_sz * 0.5f - 1.0f) && (mouse_abs_pos <= grab_pos + grab_sz * 0.5f + 1.0f); // No harm being extra generous here.
                    g.SliderGrabClickOffset = (clicked_around_grab && is_floating_point) ? mouse_abs_pos - grab_pos : 0.0f;
                }
                if (slider_usable_sz > 0.0f)
                    clicked_t = ImSaturate((mouse_abs_pos - g.SliderGrabClickOffset - slider_usable_pos_min) / slider_usable_sz);
                if (axis == ImGuiAxis_Y)
                    clicked_t = 1.0f - clicked_t;
                set_new_value = true;
            }
        }
        else if (g.ActiveIdSource == ImGuiInputSource_Keyboard || g.ActiveIdSource == ImGuiInputSource_Gamepad)
        {
            if (g.ActiveIdIsJustActivated)
            {
                g.SliderCurrentAccum = 0.0f; // Reset any stored nav delta upon activation
                g.SliderCurrentAccumDirty = false;
            }

            float input_delta = (axis == ImGuiAxis_X) ? GetNavTweakPressedAmount(axis) : -GetNavTweakPressedAmount(axis);
            if (input_delta != 0.0f)
            {
                const bool tweak_slow = IsKeyDown((g.NavInputSource == ImGuiInputSource_Gamepad) ? ImGuiKey_NavGamepadTweakSlow : ImGuiKey_NavKeyboardTweakSlow);
                const bool tweak_fast = IsKeyDown((g.NavInputSource == ImGuiInputSource_Gamepad) ? ImGuiKey_NavGamepadTweakFast : ImGuiKey_NavKeyboardTweakFast);
                const int decimal_precision = is_floating_point ? ImParseFormatPrecision(format, 3) : 0;
                if (decimal_precision > 0)
                {
                    input_delta /= 100.0f;    // Gamepad/keyboard tweak speeds in % of slider bounds
                    if (tweak_slow)
                        input_delta /= 10.0f;
                }
                else
                {
                    if ((v_range_f >= -100.0f && v_range_f <= 100.0f && v_range_f != 0.0f) || tweak_slow)
                        input_delta = ((input_delta < 0.0f) ? -1.0f : +1.0f) / v_range_f; // Gamepad/keyboard tweak speeds in integer steps
                    else
                        input_delta /= 100.0f;
                }
                if (tweak_fast)
                    input_delta *= 10.0f;

                g.SliderCurrentAccum += input_delta;
                g.SliderCurrentAccumDirty = true;
            }

            float delta = g.SliderCurrentAccum;
            if (g.NavActivatePressedId == id && !g.ActiveIdIsJustActivated)
            {
                ClearActiveID();
            }
            else if (g.SliderCurrentAccumDirty)
            {
                clicked_t = ScaleRatioFromValueT<TYPE, SIGNEDTYPE, FLOATTYPE>(data_type, *v, v_min, v_max, is_logarithmic, logarithmic_zero_epsilon, zero_deadzone_halfsize);

                if ((clicked_t >= 1.0f && delta > 0.0f) || (clicked_t <= 0.0f && delta < 0.0f)) // This is to avoid applying the saturation when already past the limits
                {
                    set_new_value = false;
                    g.SliderCurrentAccum = 0.0f; // If pushing up against the limits, don't continue to accumulate
                }
                else
                {
                    set_new_value = true;
                    float old_clicked_t = clicked_t;
                    clicked_t = ImSaturate(clicked_t + delta);

                    // Calculate what our "new" clicked_t will be, and thus how far we actually moved the slider, and subtract this from the accumulator
                    TYPE v_new = ScaleValueFromRatioT<TYPE, SIGNEDTYPE, FLOATTYPE>(data_type, clicked_t, v_min, v_max, is_logarithmic, logarithmic_zero_epsilon, zero_deadzone_halfsize);
                    if (is_floating_point && !(flags & ImGuiSliderFlags_NoRoundToFormat))
                        v_new = RoundScalarWithFormatT<TYPE>(format, data_type, v_new);
                    float new_clicked_t = ScaleRatioFromValueT<TYPE, SIGNEDTYPE, FLOATTYPE>(data_type, v_new, v_min, v_max, is_logarithmic, logarithmic_zero_epsilon, zero_deadzone_halfsize);

                    if (delta > 0)
                        g.SliderCurrentAccum -= ImMin(new_clicked_t - old_clicked_t, delta);
                    else
                        g.SliderCurrentAccum -= ImMax(new_clicked_t - old_clicked_t, delta);
                }

                g.SliderCurrentAccumDirty = false;
            }
        }

        if (set_new_value)
            if ((g.LastItemData.InFlags & ImGuiItemFlags_ReadOnly) || (flags & ImGuiSliderFlags_ReadOnly))
                set_new_value = false;

        if (set_new_value)
        {
            TYPE v_new = ScaleValueFromRatioT<TYPE, SIGNEDTYPE, FLOATTYPE>(data_type, clicked_t, v_min, v_max, is_logarithmic, logarithmic_zero_epsilon, zero_deadzone_halfsize);

            // Round to user desired precision based on format string
            if (is_floating_point && !(flags & ImGuiSliderFlags_NoRoundToFormat))
                v_new = RoundScalarWithFormatT<TYPE>(format, data_type, v_new);

            // Apply result
            if (*v != v_new)
            {
                *v = v_new;
                value_changed = true;
            }
        }
    }

    if (slider_sz < 1.0f)
    {
        *out_grab_bb = ImRect(bb.Min, bb.Min);
    }
    else
    {
        // Output grab position so it can be displayed by the caller
        float grab_t = ScaleRatioFromValueT<TYPE, SIGNEDTYPE, FLOATTYPE>(data_type, *v, v_min, v_max, is_logarithmic, logarithmic_zero_epsilon, zero_deadzone_halfsize);
        if (axis == ImGuiAxis_Y)
            grab_t = 1.0f - grab_t;
        const float grab_pos = ImLerp(slider_usable_pos_min, slider_usable_pos_max, grab_t);
        if (axis == ImGuiAxis_X)
            *out_grab_bb = ImRect(grab_pos - grab_sz * 0.5f, bb.Min.y + grab_padding, grab_pos + grab_sz * 0.5f, bb.Max.y - grab_padding);
        else
            *out_grab_bb = ImRect(bb.Min.x + grab_padding, grab_pos - grab_sz * 0.5f, bb.Max.x - grab_padding, grab_pos + grab_sz * 0.5f);
    }

    return value_changed;
}

bool slider_behavior(const ImRect& bb, ImGuiID id, ImGuiDataType data_type, void* p_v, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags, ImRect* out_grab_bb)
{
    IM_ASSERT((flags == 1 || (flags & ImGuiSliderFlags_InvalidMask_) == 0) && "Invalid ImGuiSliderFlags flag!  Has the 'float power' argument been mistakenly cast to flags? Call function with ImGuiSliderFlags_Logarithmic flags instead.");

    switch (data_type)
    {
    case ImGuiDataType_S8: { ImS32 v32 = (ImS32) * (ImS8*)p_v;  bool r = slider_behavior_t<ImS32, ImS32, float>(bb, id, ImGuiDataType_S32, &v32, *(const ImS8*)p_min, *(const ImS8*)p_max, format, flags, out_grab_bb); if (r) *(ImS8*)p_v = (ImS8)v32;  return r; }
    case ImGuiDataType_U8: { ImU32 v32 = (ImU32) * (ImU8*)p_v;  bool r = slider_behavior_t<ImU32, ImS32, float>(bb, id, ImGuiDataType_U32, &v32, *(const ImU8*)p_min, *(const ImU8*)p_max, format, flags, out_grab_bb); if (r) *(ImU8*)p_v = (ImU8)v32;  return r; }
    case ImGuiDataType_S16: { ImS32 v32 = (ImS32) * (ImS16*)p_v; bool r = slider_behavior_t<ImS32, ImS32, float>(bb, id, ImGuiDataType_S32, &v32, *(const ImS16*)p_min, *(const ImS16*)p_max, format, flags, out_grab_bb); if (r) *(ImS16*)p_v = (ImS16)v32; return r; }
    case ImGuiDataType_U16: { ImU32 v32 = (ImU32) * (ImU16*)p_v; bool r = slider_behavior_t<ImU32, ImS32, float>(bb, id, ImGuiDataType_U32, &v32, *(const ImU16*)p_min, *(const ImU16*)p_max, format, flags, out_grab_bb); if (r) *(ImU16*)p_v = (ImU16)v32; return r; }
    case ImGuiDataType_S32:
        IM_ASSERT(*(const ImS32*)p_min >= INT_MIN / 2 && *(const ImS32*)p_max <= INT_MAX / 2);
        return slider_behavior_t<ImS32, ImS32, float >(bb, id, data_type, (ImS32*)p_v, *(const ImS32*)p_min, *(const ImS32*)p_max, format, flags, out_grab_bb);
    case ImGuiDataType_U32:
        IM_ASSERT(*(const ImU32*)p_max <= UINT_MAX / 2);
        return slider_behavior_t<ImU32, ImS32, float >(bb, id, data_type, (ImU32*)p_v, *(const ImU32*)p_min, *(const ImU32*)p_max, format, flags, out_grab_bb);
    case ImGuiDataType_S64:
        IM_ASSERT(*(const ImS64*)p_min >= LLONG_MIN / 2 && *(const ImS64*)p_max <= LLONG_MAX / 2);
        return slider_behavior_t<ImS64, ImS64, double>(bb, id, data_type, (ImS64*)p_v, *(const ImS64*)p_min, *(const ImS64*)p_max, format, flags, out_grab_bb);
    case ImGuiDataType_U64:
        IM_ASSERT(*(const ImU64*)p_max <= ULLONG_MAX / 2);
        return slider_behavior_t<ImU64, ImS64, double>(bb, id, data_type, (ImU64*)p_v, *(const ImU64*)p_min, *(const ImU64*)p_max, format, flags, out_grab_bb);
    case ImGuiDataType_Float:
        IM_ASSERT(*(const float*)p_min >= -FLT_MAX / 2.0f && *(const float*)p_max <= FLT_MAX / 2.0f);
        return slider_behavior_t<float, float, float >(bb, id, data_type, (float*)p_v, *(const float*)p_min, *(const float*)p_max, format, flags, out_grab_bb);
    case ImGuiDataType_Double:
        IM_ASSERT(*(const double*)p_min >= -DBL_MAX / 2.0f && *(const double*)p_max <= DBL_MAX / 2.0f);
        return slider_behavior_t<double, double, double>(bb, id, data_type, (double*)p_v, *(const double*)p_min, *(const double*)p_max, format, flags, out_grab_bb);
    case ImGuiDataType_COUNT: break;
    }
    IM_ASSERT(0);
    return false;
}

bool slider_scalar(const char* label, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const char* format, ImGuiSliderFlags flags, const char* icon = nullptr)
{
    struct slider_state
    {
        float grab_slide = 0.f;
        float knob = 0.f;
        float hot = 0.f;
    };

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    PushID(p_data);
    const ImGuiID id = window->GetID(label);

    const MenuTheme::Palette& tokens = MenuTheme::Colors();
    const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
    const float uiScale = leyawin_ui_scale();

    // Same rhythm as MenuUi::SettingRow so labels, icons and the right edge
    // line up when sliders and toggles are stacked in one section.
    const float width = ImMax(GetContentRegionAvail().x, 1.f);
    const float rect_size = metrics.rowHeight * uiScale;
    const float pad = metrics.space2 * uiScale;
    const float iconSz = metrics.iconRow * uiScale;
    const ImVec2 pos = window->DC.CursorPos;

    const bool labelHidden =
        label && label[0] == '#' && label[1] == '#';
    // Label and value share a caption line and the rail spans the full width
    // underneath, so long labels never squeeze the track.
    const float captionH = ImMax(
        ImGui::GetTextLineHeight(),
        MenuType::BodySize());
    const float railH = 4.f * uiScale;
    const float trackRowH = 14.f * uiScale;
    const float captionGap = 5.f * uiScale;
    const float rowTotal = captionH + captionGap + trackRowH;
    const float trackX0 = pad;
    const float trackW = ImMax(40.f * uiScale, width - pad * 2.f);
    const float trackCenter =
        pos.y + captionH + captionGap + trackRowH * 0.5f;

    const ImRect rect(pos, pos + ImVec2(width, rowTotal));
    const ImRect slider(
        ImVec2(pos.x + trackX0, trackCenter - railH * 0.5f),
        ImVec2(pos.x + trackX0 + trackW, trackCenter + railH * 0.5f));
    const ImRect clickable(
        ImVec2(pos.x + trackX0, pos.y + captionH + captionGap * 0.5f),
        ImVec2(pos.x + trackX0 + trackW, pos.y + rowTotal));

    const bool temp_input_allowed = (flags & ImGuiSliderFlags_NoInput) == 0;

    ItemSize(rect, 0);
    if (!ItemAdd(rect, id, &clickable, temp_input_allowed ? ImGuiItemFlags_Inputable : 0)) {
        PopID();
        return false;
    }

    if (format == NULL) format = DataTypeGetInfo(data_type)->PrintFmt;

    bool hovered, held;
    const bool pressed = ButtonBehavior(clickable, id, &hovered, &held, NULL);
    (void)pressed;
    bool temp_input_is_active = temp_input_allowed && TempInputIsActive(id);
    (void)temp_input_is_active;

    ImRect grab_bb;
    const bool value_changed = slider_behavior(
        ImRect(
            clickable.Min - ImVec2(3.f * uiScale, 0.f),
            clickable.Max + ImVec2(2.f * uiScale, 0.f)),
        id,
        data_type,
        p_data,
        p_min,
        p_max,
        format,
        flags,
        &grab_bb);
    if (value_changed) MarkItemEdited(id);

    char value_buf[64];
    const char* value_buf_end = value_buf + DataTypeFormatString(value_buf, IM_ARRAYSIZE(value_buf), data_type, p_data, format);

    auto& anim = LEYAWIN_ANIM(ImGuiID, slider_state);
    slider_state& state = MapGet(anim, id);

    const bool rowHot = hovered || held ||
        IsMouseHoveringRect(rect.Min, rect.Max, false);
    state.hot = MenuMotion::Approach(
        state.hot, rowHot ? 1.f : 0.f, MenuMotion::kHover);
    state.knob = MenuMotion::Approach(
        state.knob, held ? 1.f : (rowHot ? 0.55f : 0.f), MenuMotion::kPress);
    state.grab_slide = MenuMotion::Approach(
        state.grab_slide,
        grab_bb.Min.x - clickable.Min.x,
        MenuMotion::kSelection);

    // The grab rect is wider than the track, so at either end the knob used to
    // hang past the rail; keep its whole circle inside the track.
    const float knobRadius = 7.f * uiScale;
    const float knobX = ImClamp(
        slider.Min.x + state.grab_slide + 5.f * uiScale,
        slider.Min.x + knobRadius,
        slider.Max.x - knobRadius);
    window->DrawList->AddRectFilled(
        slider.Min,
        slider.Max,
        gui->get_clr(tokens.raised, 1.f),
        railH * 0.5f);
    window->DrawList->AddRectFilled(
        slider.Min,
        ImVec2(knobX, slider.Max.y),
        gui->get_clr(tokens.accent.fill, 1.f),
        railH * 0.5f);

    const float knobR = (5.5f + 1.2f * state.knob) * uiScale;
    window->DrawList->AddCircleFilled(
        ImVec2(knobX, (slider.Min.y + slider.Max.y) * 0.5f),
        knobR,
        gui->get_clr(tokens.textPrimary, 1.f),
        20);
    window->DrawList->AddCircle(
        ImVec2(knobX, (slider.Min.y + slider.Max.y) * 0.5f),
        knobR,
        gui->get_clr(tokens.accent.fill, 0.55f),
        20,
        metrics.stroke);

    // Sliders carry no icon: the label alone reads faster in a stacked list.
    (void)icon;
    (void)iconSz;

    // The value is set in the same Inter face as the label; the mono face read
    // as a foreign element on an otherwise sans row. Right alignment keeps the
    // column steady even though the digits are proportional.
    ImFont* valueFont = set->poppins_widget
        ? set->poppins_widget
        : ImGui::GetFont();
    const float valueSizePx = MenuType::BodySize();
    float valueLeft = rect.Max.x - pad;
    if (valueFont)
    {
        const ImVec2 valueSize = valueFont->CalcTextSizeA(
            valueSizePx, FLT_MAX, 0.f, value_buf, value_buf_end);
        valueLeft = rect.Max.x - pad - valueSize.x;
        window->DrawList->AddText(
            valueFont,
            valueSizePx,
            ImVec2(
                valueLeft,
                rect.Min.y + (captionH - valueSize.y) * 0.5f),
            gui->get_clr(tokens.textPrimary, 1.f),
            value_buf,
            value_buf_end);
    }

    if (!labelHidden)
    {
        const ImVec4 labelCol = ImLerp(
            tokens.textMuted,
            tokens.textPrimary,
            ImMax(state.hot, held ? 1.f : 0.f));
        gui->render_text_clipped(
            set->poppins_widget,
            ImVec2(rect.Min.x + pad, rect.Min.y),
            ImVec2(
                valueLeft - metrics.space3 * uiScale,
                rect.Min.y + captionH),
            gui->get_clr(labelCol, 1.f),
            label,
            {0.0f, 0.5f});
    }

    PopID();
    return value_changed;
}

bool c_widgets::slider_float(const char* label, float* v, float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
{
    return slider_scalar(label, ImGuiDataType_Float, v, &v_min, &v_max, format, flags);
}

bool c_widgets::slider_float_icon(const char* icon, const char* label, float* v, float v_min, float v_max, const char* format, ImGuiSliderFlags flags)
{
    return slider_scalar(
        label,
        ImGuiDataType_Float,
        v,
        &v_min,
        &v_max,
        format,
        flags,
        icon);
}

bool c_widgets::slider_int(const char* label, int* v, int v_min, int v_max, const char* format, ImGuiSliderFlags flags)
{
    return slider_scalar(label, ImGuiDataType_S32, v, &v_min, &v_max, format, flags);
}

#if 0 // legacy input_text_ex block
static bool input_text_filter_character(ImGuiContext* ctx, unsigned int* p_char, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data, ImGuiInputSource input_source)
{
    IM_ASSERT(input_source == ImGuiInputSource_Keyboard || input_source == ImGuiInputSource_Clipboard);
    unsigned int c = *p_char;

    // Filter non-printable (NB: isprint is unreliable! see #2467)
    bool apply_named_filters = true;
    if (c < 0x20)
    {
        bool pass = false;
        pass |= (c == '\n') && (flags & ImGuiInputTextFlags_Multiline) != 0; // Note that an Enter KEY will emit \r and be ignored (we poll for KEY in InputText() code)
        pass |= (c == '\t') && (flags & ImGuiInputTextFlags_AllowTabInput) != 0;
        if (!pass)
            return false;
        apply_named_filters = false; // Override named filters below so newline and tabs can still be inserted.
    }

    if (input_source != ImGuiInputSource_Clipboard)
    {
        // We ignore Ascii representation of delete (emitted from Backspace on OSX, see #2578, #2817)
        if (c == 127)
            return false;

        // Filter private Unicode range. GLFW on OSX seems to send private characters for special keys like arrow keys (FIXME)
        if (c >= 0xE000 && c <= 0xF8FF)
            return false;
    }

    // Filter Unicode ranges we are not handling in this build
    if (c > IM_UNICODE_CODEPOINT_MAX)
        return false;

    // Generic named filters
    if (apply_named_filters && (flags & (ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase | ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_CharsScientific)))
    {
        // The libc allows overriding locale, with e.g. 'setlocale(LC_NUMERIC, "de_DE.UTF-8");' which affect the output/input of printf/scanf to use e.g. ',' instead of '.'.
        // The standard mandate that programs starts in the "C" locale where the decimal point is '.'.
        // We don't really intend to provide widespread support for it, but out of empathy for people stuck with using odd API, we support the bare minimum aka overriding the decimal point.
        // Change the default decimal_point with:
        //   ImGui::GetIO()->PlatformLocaleDecimalPoint = *localeconv()->decimal_point;
        // Users of non-default decimal point (in particular ',') may be affected by word-selection logic (is_word_boundary_from_right/is_word_boundary_from_left) functions.
        ImGuiContext& g = *ctx;
        const unsigned c_decimal_point = (unsigned int)g.IO.PlatformLocaleDecimalPoint;
        if (flags & (ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsScientific))
            if (c == '.' || c == ',')
                c = c_decimal_point;

        // Full-width -> half-width conversion for numeric fields (https://en.wikipedia.org/wiki/Halfwidth_and_Fullwidth_Forms_(Unicode_block)
        // While this is mostly convenient, this has the side-effect for uninformed users accidentally inputting full-width characters that they may
        // scratch their head as to why it works in numerical fields vs in generic text fields it would require support in the font.
        if (flags & (ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_CharsScientific | ImGuiInputTextFlags_CharsHexadecimal))
            if (c >= 0xFF01 && c <= 0xFF5E)
                c = c - 0xFF01 + 0x21;

        // Allow 0-9 . - + * /
        if (flags & ImGuiInputTextFlags_CharsDecimal)
            if (!(c >= '0' && c <= '9') && (c != c_decimal_point) && (c != '-') && (c != '+') && (c != '*') && (c != '/'))
                return false;

        // Allow 0-9 . - + * / e E
        if (flags & ImGuiInputTextFlags_CharsScientific)
            if (!(c >= '0' && c <= '9') && (c != c_decimal_point) && (c != '-') && (c != '+') && (c != '*') && (c != '/') && (c != 'e') && (c != 'E'))
                return false;

        // Allow 0-9 a-F A-F
        if (flags & ImGuiInputTextFlags_CharsHexadecimal)
            if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F'))
                return false;

        // Turn a-z into A-Z
        if (flags & ImGuiInputTextFlags_CharsUppercase)
            if (c >= 'a' && c <= 'z')
                c += (unsigned int)('A' - 'a');

        if (flags & ImGuiInputTextFlags_CharsNoBlank)
            if (ImCharIsBlankW(c))
                return false;

        *p_char = c;
    }

    // Custom callback filter
    if (flags & ImGuiInputTextFlags_CallbackCharFilter)
    {
        ImGuiContext& g = *GImGui;
        ImGuiInputTextCallbackData callback_data;
        callback_data.Ctx = &g;
        callback_data.EventFlag = ImGuiInputTextFlags_CallbackCharFilter;
        callback_data.EventChar = (ImWchar)c;
        callback_data.Flags = flags;
        callback_data.UserData = user_data;
        if (callback(&callback_data) != 0)
            return false;
        *p_char = callback_data.EventChar;
        if (!callback_data.EventChar)
            return false;
    }

    return true;
}

static int input_text_calc_text_len_and_line_count(const char* text_begin, const char** out_text_end)
{
    int line_count = 0;
    const char* s = text_begin;
    while (char c = *s++) // We are only matching for \n so we can ignore UTF-8 decoding
        if (c == '\n')
            line_count++;
    s--;
    if (s[0] != '\n' && s[0] != '\r')
        line_count++;
    *out_text_end = s;
    return line_count;
}

static void input_text_reconcile_undo_state_after_user_callback(ImGuiInputTextState* state, const char* new_buf_a, int new_length_a)
{
    ImGuiContext& g = *GImGui;
    const ImWchar* old_buf = state->TextW.Data;
    const int old_length = state->CurLenW;
    const int new_length = ImTextCountCharsFromUtf8(new_buf_a, new_buf_a + new_length_a);
    g.TempBuffer.reserve_discard((new_length + 1) * sizeof(ImWchar));
    ImWchar* new_buf = (ImWchar*)(void*)g.TempBuffer.Data;
    ImTextStrFromUtf8(new_buf, new_length + 1, new_buf_a, new_buf_a + new_length_a);

    const int shorter_length = ImMin(old_length, new_length);
    int first_diff;
    for (first_diff = 0; first_diff < shorter_length; first_diff++)
        if (old_buf[first_diff] != new_buf[first_diff])
            break;
    if (first_diff == old_length && first_diff == new_length)
        return;

    int old_last_diff = old_length - 1;
    int new_last_diff = new_length - 1;
    for (; old_last_diff >= first_diff && new_last_diff >= first_diff; old_last_diff--, new_last_diff--)
        if (old_buf[old_last_diff] != new_buf[new_last_diff])
            break;

    const int insert_len = new_last_diff - first_diff + 1;
    const int delete_len = old_last_diff - first_diff + 1;
    if (insert_len > 0 || delete_len > 0)
        if (IMSTB_TEXTEDIT_CHARTYPE* p = stb_text_createundo(&state->Stb.undostate, first_diff, delete_len, insert_len))
            for (int i = 0; i < delete_len; i++)
                p[i] = ImStb::STB_TEXTEDIT_GETCHAR(state, first_diff + i);
}

bool input_text_ex(const char* label, const char* hint, char* buf, int buf_size, const ImVec2& size_arg, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* callback_user_data)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;

    IM_ASSERT(buf != NULL && buf_size >= 0);
    IM_ASSERT(!((flags & ImGuiInputTextFlags_CallbackHistory) && (flags & ImGuiInputTextFlags_Multiline)));        // Can't use both together (they both use up/down keys)
    IM_ASSERT(!((flags & ImGuiInputTextFlags_CallbackCompletion) && (flags & ImGuiInputTextFlags_AllowTabInput))); // Can't use both together (they both use tab key)

    ImGuiContext& g = *GImGui;
    ImGuiIO& io = g.IO;
    const ImGuiStyle& style = g.Style;

    const bool RENDER_SELECTION_WHEN_INACTIVE = false;
    const bool is_multiline = (flags & ImGuiInputTextFlags_Multiline) != 0;

    if (is_multiline) BeginGroup();
    const ImGuiID id = window->GetID(label);

    const ImVec2 pos = window->DC.CursorPos;
    const float width = GetContentRegionMax().x - g.Style.WindowPadding.x;

    const ImRect rect(pos, pos + size_arg);
    const ImRect clickable(pos, pos + size_arg);

    ImGuiWindow* draw_window = window;
    ImVec2 inner_size = clickable.GetSize();
    ImGuiLastItemData item_data_backup;
    if (is_multiline)
    {
        ImVec2 backup_pos = window->DC.CursorPos;
        ItemSize(rect, style.FramePadding.y);
        if (!ItemAdd(rect, id, &clickable, ImGuiItemFlags_Inputable))
        {
            EndGroup();
            return false;
        }
        item_data_backup = g.LastItemData;
        window->DC.CursorPos = backup_pos;

        // Prevent NavActivation from Tabbing when our widget accepts Tab inputs: this allows cycling through widgets without stopping.
        if (g.NavActivateId == id && (g.NavActivateFlags & ImGuiActivateFlags_FromTabbing) && (flags & ImGuiInputTextFlags_AllowTabInput))
            g.NavActivateId = 0;

        // Prevent NavActivate reactivating in BeginChild() when we are already active.
        const ImGuiID backup_activate_id = g.NavActivateId;
        if (g.ActiveId == id) // Prevent reactivation
            g.NavActivateId = 0;

        // We reproduce the contents of BeginChildFrame() in order to provide 'label' so our window internal data are easier to read/debug.
        PushStyleColor(ImGuiCol_ChildBg, style.Colors[ImGuiCol_FrameBg]);
        PushStyleVar(ImGuiStyleVar_ChildRounding, style.FrameRounding);
        PushStyleVar(ImGuiStyleVar_ChildBorderSize, style.FrameBorderSize);
        PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); // Ensure no clip rect so mouse hover can reach FramePadding edges
        bool child_visible = BeginChildEx(label, id, clickable.GetSize(), true, ImGuiWindowFlags_NoMove);
        g.NavActivateId = backup_activate_id;
        PopStyleVar(3);
        PopStyleColor();
        if (!child_visible)
        {
            EndChild();
            EndGroup();
            return false;
        }
        draw_window = g.CurrentWindow; // Child window
        draw_window->DC.NavLayersActiveMaskNext |= (1 << draw_window->DC.NavLayerCurrent); // This is to ensure that EndChild() will display a navigation highlight so we can "enter" into it.
        draw_window->DC.CursorPos += style.FramePadding;
        inner_size.x -= draw_window->ScrollbarSizes.x;
    }
    else
    {
        // Support for internal ImGuiInputTextFlags_MergedItem flag, which could be redesigned as an ItemFlags if needed (with test performed in ItemAdd)
        ItemSize(rect, style.FramePadding.y);
        if (!(flags & ImGuiInputTextFlags_MergedItem))
            if (!ItemAdd(rect, id, &clickable, ImGuiItemFlags_Inputable)) return false;
    }
    const bool hovered = ItemHoverable(clickable, id, g.LastItemData.InFlags);

    PushFont(set->poppins_widget);

    ImGuiInputTextState* state = GetInputTextState(id);

    if (g.LastItemData.InFlags & ImGuiItemFlags_ReadOnly)
        flags |= ImGuiInputTextFlags_ReadOnly;
    const bool is_readonly = (flags & ImGuiInputTextFlags_ReadOnly) != 0;
    const bool is_password = (flags & ImGuiInputTextFlags_Password) != 0;
    const bool is_undoable = (flags & ImGuiInputTextFlags_NoUndoRedo) == 0;
    const bool is_resizable = (flags & ImGuiInputTextFlags_CallbackResize) != 0;
    if (is_resizable)
        IM_ASSERT(callback != NULL); // Must provide a callback if you set the ImGuiInputTextFlags_CallbackResize flag!

    const bool input_requested_by_nav = (g.ActiveId != id) && ((g.NavActivateId == id) && ((g.NavActivateFlags & ImGuiActivateFlags_PreferInput) || (g.NavInputSource == ImGuiInputSource_Keyboard)));

    const bool user_clicked = hovered && io.MouseClicked[0];
    const bool user_scroll_finish = is_multiline && state != NULL && g.ActiveId == 0 && g.ActiveIdPreviousFrame == GetWindowScrollbarID(draw_window, ImGuiAxis_Y);
    const bool user_scroll_active = is_multiline && state != NULL && g.ActiveId == GetWindowScrollbarID(draw_window, ImGuiAxis_Y);
    bool clear_active_id = false;
    bool select_all = false;

    float scroll_y = is_multiline ? draw_window->Scroll.y : FLT_MAX;

    const bool init_reload_from_user_buf = (state != NULL && state->ReloadUserBuf);
    const bool init_changed_specs = (state != NULL && state->Stb.single_line != !is_multiline); // state != NULL means its our state.
    const bool init_make_active = (user_clicked || user_scroll_finish || input_requested_by_nav);
    const bool init_state = (init_make_active || user_scroll_active);
    if ((init_state && g.ActiveId != id) || init_changed_specs || init_reload_from_user_buf)
    {
        // Access state even if we don't own it yet.
        state = &g.InputTextState;
        state->CursorAnimReset();
        state->ReloadUserBuf = false;

        // Backup state of deactivating item so they'll have a chance to do a write to output buffer on the same frame they report IsItemDeactivatedAfterEdit (#4714)
        InputTextDeactivateHook(state->ID);

        // From the moment we focused we are normally ignoring the content of 'buf' (unless we are in read-only mode)
        const int buf_len = (int)strlen(buf);
        if (!init_reload_from_user_buf)
        {
            // Take a copy of the initial buffer value.
            state->InitialTextA.resize(buf_len + 1);    // UTF-8. we use +1 to make sure that .Data is always pointing to at least an empty string.
            memcpy(state->InitialTextA.Data, buf, buf_len + 1);
        }

        // Preserve cursor position and undo/redo stack if we come back to same widget
        // FIXME: Since we reworked this on 2022/06, may want to differentiate recycle_cursor vs recycle_undostate?
        bool recycle_state = (state->ID == id && !init_changed_specs && !init_reload_from_user_buf);
        if (recycle_state && (state->CurLenA != buf_len || (state->TextAIsValid && strncmp(state->TextA.Data, buf, buf_len) != 0)))
            recycle_state = false;

        // Start edition
        const char* buf_end = NULL;
        state->ID = id;
        state->TextW.resize(buf_size + 1);          // wchar count <= UTF-8 count. we use +1 to make sure that .Data is always pointing to at least an empty string.
        state->TextA.resize(0);
        state->TextAIsValid = false;                // TextA is not valid yet (we will display buf until then)
        state->CurLenW = ImTextStrFromUtf8(state->TextW.Data, buf_size, buf, NULL, &buf_end);
        state->CurLenA = (int)(buf_end - buf);      // We can't get the result from ImStrncpy() above because it is not UTF-8 aware. Here we'll cut off malformed UTF-8.

        if (recycle_state)
        {
            // Recycle existing cursor/selection/undo stack but clamp position
            // Note a single mouse click will override the cursor/position immediately by calling stb_textedit_click handler.
            state->CursorClamp();
        }
        else
        {
            state->ScrollX = 0.0f;
            stb_textedit_initialize_state(&state->Stb, !is_multiline);
        }

        if (init_reload_from_user_buf)
        {
            state->Stb.select_start = state->ReloadSelectionStart;
            state->Stb.cursor = state->Stb.select_end = state->ReloadSelectionEnd;
            state->CursorClamp();
        }
        else if (!is_multiline)
        {
            if (flags & ImGuiInputTextFlags_AutoSelectAll)
                select_all = true;
            if (input_requested_by_nav && (!recycle_state || !(g.NavActivateFlags & ImGuiActivateFlags_TryToPreserveState)))
                select_all = true;
            if (user_clicked && io.KeyCtrl)
                select_all = true;
        }

        if (flags & ImGuiInputTextFlags_AlwaysOverwrite)
            state->Stb.insert_mode = 1; // stb field name is indeed incorrect (see #2863)
    }

    const bool is_osx = io.ConfigMacOSXBehaviors;
    if (g.ActiveId != id && init_make_active)
    {
        IM_ASSERT(state && state->ID == id);
        SetActiveID(id, window);
        SetFocusID(id, window);
        FocusWindow(window);
    }
    if (g.ActiveId == id)
    {
        // Declare some inputs, the other are registered and polled via Shortcut() routing system.
        if (user_clicked)
            SetKeyOwner(ImGuiKey_MouseLeft, id);
        g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
        if (is_multiline || (flags & ImGuiInputTextFlags_CallbackHistory))
            g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Up) | (1 << ImGuiDir_Down);
        SetKeyOwner(ImGuiKey_Enter, id);
        SetKeyOwner(ImGuiKey_KeypadEnter, id);
        SetKeyOwner(ImGuiKey_Home, id);
        SetKeyOwner(ImGuiKey_End, id);
        if (is_multiline)
        {
            SetKeyOwner(ImGuiKey_PageUp, id);
            SetKeyOwner(ImGuiKey_PageDown, id);
        }
        if (is_osx)
            SetKeyOwner(ImGuiMod_Alt, id);
    }

    // We have an edge case if ActiveId was set through another widget (e.g. widget being swapped), clear id immediately (don't wait until the end of the function)
    if (g.ActiveId == id && state == NULL)
        ClearActiveID();

    // Release focus when we click outside
    if (g.ActiveId == id && io.MouseClicked[0] && !init_state && !init_make_active) //-V560
        clear_active_id = true;

    // Lock the decision of whether we are going to take the path displaying the cursor or selection
    bool render_cursor = (g.ActiveId == id) || (state && user_scroll_active);
    bool render_selection = state && (state->HasSelection() || select_all) && (RENDER_SELECTION_WHEN_INACTIVE || render_cursor);
    bool value_changed = false;
    bool validated = false;

    // When read-only we always use the live data passed to the function
    // FIXME-OPT: Because our selection/cursor code currently needs the wide text we need to convert it when active, which is not ideal :(
    if (is_readonly && state != NULL && (render_cursor || render_selection))
    {
        const char* buf_end = NULL;
        state->TextW.resize(buf_size + 1);
        state->CurLenW = ImTextStrFromUtf8(state->TextW.Data, state->TextW.Size, buf, NULL, &buf_end);
        state->CurLenA = (int)(buf_end - buf);
        state->CursorClamp();
        render_selection &= state->HasSelection();
    }

    // Select the buffer to render.
    const bool buf_display_from_state = (render_cursor || render_selection || g.ActiveId == id) && !is_readonly && state && state->TextAIsValid;
    const bool is_displaying_hint = (hint != NULL && (buf_display_from_state ? state->TextA.Data : buf)[0] == 0);

    // Password pushes a temporary font with only a fallback glyph
    if (is_password && !is_displaying_hint)
    {
        const ImFontGlyph* glyph = g.Font->FindGlyph('*');
        ImFont* password_font = &g.InputTextPasswordFont;
        password_font->FontSize = g.Font->FontSize;
        password_font->Scale = g.Font->Scale;
        password_font->Ascent = g.Font->Ascent;
        password_font->Descent = g.Font->Descent;
        password_font->ContainerAtlas = g.Font->ContainerAtlas;
        password_font->FallbackGlyph = glyph;
        password_font->FallbackAdvanceX = glyph->AdvanceX;
        IM_ASSERT(password_font->Glyphs.empty() && password_font->IndexAdvanceX.empty() && password_font->IndexLookup.empty());
        PushFont(password_font);
    }

    // Process mouse inputs and character inputs
    int backup_current_text_length = 0;
    if (g.ActiveId == id)
    {
        IM_ASSERT(state != NULL);
        backup_current_text_length = state->CurLenA;
        state->Edited = false;
        state->BufCapacityA = buf_size;
        state->Flags = flags;

        // Although we are active we don't prevent mouse from hovering other elements unless we are interacting right now with the widget.
        // Down the line we should have a cleaner library-wide concept of Selected vs Active.
        g.ActiveIdAllowOverlap = !io.MouseDown[0];

        // Edit in progress
        const float mouse_x = (io.MousePos.x - clickable.Min.x - style.FramePadding.x) + state->ScrollX;
        const float mouse_y = (is_multiline ? (io.MousePos.y - draw_window->DC.CursorPos.y) : (g.FontSize * 0.5f));

        if (select_all)
        {
            state->SelectAll();
            state->SelectedAllMouseLock = true;
        }
        else if (hovered && io.MouseClickedCount[0] >= 2 && !io.KeyShift)
        {
            stb_textedit_click(state, &state->Stb, mouse_x, mouse_y);
            const int multiclick_count = (io.MouseClickedCount[0] - 2);
            if ((multiclick_count % 2) == 0)
            {
                // Double-click: Select word
                // We always use the "Mac" word advance for double-click select vs CTRL+Right which use the platform dependent variant:
                // FIXME: There are likely many ways to improve this behavior, but there's no "right" behavior (depends on use-case, software, OS)
                const bool is_bol = (state->Stb.cursor == 0) || ImStb::STB_TEXTEDIT_GETCHAR(state, state->Stb.cursor - 1) == '\n';
                if (STB_TEXT_HAS_SELECTION(&state->Stb) || !is_bol)
                    state->OnKeyPressed(STB_TEXTEDIT_K_WORDLEFT);
                //state->OnKeyPressed(STB_TEXTEDIT_K_WORDRIGHT | STB_TEXTEDIT_K_SHIFT);
                if (!STB_TEXT_HAS_SELECTION(&state->Stb))
                    ImStb::stb_textedit_prep_selection_at_cursor(&state->Stb);
                state->Stb.cursor = ImStb::STB_TEXTEDIT_MOVEWORDRIGHT_MAC(state, state->Stb.cursor);
                state->Stb.select_end = state->Stb.cursor;
                ImStb::stb_textedit_clamp(state, &state->Stb);
            }
            else
            {
                // Triple-click: Select line
                const bool is_eol = ImStb::STB_TEXTEDIT_GETCHAR(state, state->Stb.cursor) == '\n';
                state->OnKeyPressed(STB_TEXTEDIT_K_LINESTART);
                state->OnKeyPressed(STB_TEXTEDIT_K_LINEEND | STB_TEXTEDIT_K_SHIFT);
                state->OnKeyPressed(STB_TEXTEDIT_K_RIGHT | STB_TEXTEDIT_K_SHIFT);
                if (!is_eol && is_multiline)
                {
                    ImSwap(state->Stb.select_start, state->Stb.select_end);
                    state->Stb.cursor = state->Stb.select_end;
                }
                state->CursorFollow = false;
            }
            state->CursorAnimReset();
        }
        else if (io.MouseClicked[0] && !state->SelectedAllMouseLock)
        {
            if (hovered)
            {
                if (io.KeyShift)
                    stb_textedit_drag(state, &state->Stb, mouse_x, mouse_y);
                else
                    stb_textedit_click(state, &state->Stb, mouse_x, mouse_y);
                state->CursorAnimReset();
            }
        }
        else if (io.MouseDown[0] && !state->SelectedAllMouseLock && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
        {
            stb_textedit_drag(state, &state->Stb, mouse_x, mouse_y);
            state->CursorAnimReset();
            state->CursorFollow = true;
        }
        if (state->SelectedAllMouseLock && !io.MouseDown[0])
            state->SelectedAllMouseLock = false;

        // We expect backends to emit a Tab key but some also emit a Tab character which we ignore (#2467, #1336)
        // (For Tab and Enter: Win32/SFML/Allegro are sending both keys and chars, GLFW and SDL are only sending keys. For Space they all send all threes)
        if ((flags & ImGuiInputTextFlags_AllowTabInput) && !is_readonly)
        {
            if (Shortcut(ImGuiKey_Tab, id, ImGuiInputFlags_Repeat))
            {
                unsigned int c = '\t'; // Insert TAB
                if (input_text_filter_character(&g, &c, flags, callback, callback_user_data, ImGuiInputSource_Keyboard))
                    state->OnKeyPressed((int)c);
            }
            // FIXME: Implement Shift+Tab
            /*
            if (Shortcut(ImGuiKey_Tab | ImGuiMod_Shift, id, ImGuiInputFlags_Repeat))
            {
            }
            */
        }

        // Process regular text input (before we check for Return because using some IME will effectively send a Return?)
        // We ignore CTRL inputs, but need to allow ALT+CTRL as some keyboards (e.g. German) use AltGR (which _is_ Alt+Ctrl) to input certain characters.
        const bool ignore_char_inputs = (io.KeyCtrl && !io.KeyAlt) || (is_osx && io.KeySuper);
        if (io.InputQueueCharacters.Size > 0)
        {
            if (!ignore_char_inputs && !is_readonly && !input_requested_by_nav)
                for (int n = 0; n < io.InputQueueCharacters.Size; n++)
                {
                    // Insert character if they pass filtering
                    unsigned int c = (unsigned int)io.InputQueueCharacters[n];
                    if (c == '\t') // Skip Tab, see above.
                        continue;
                    if (input_text_filter_character(&g, &c, flags, callback, callback_user_data, ImGuiInputSource_Keyboard))
                        state->OnKeyPressed((int)c);
                }

            // Consume characters
            io.InputQueueCharacters.resize(0);
        }
    }

    // Process other shortcuts/key-presses
    bool revert_edit = false;
    if (g.ActiveId == id && !g.ActiveIdIsJustActivated && !clear_active_id)
    {
        IM_ASSERT(state != NULL);

        const int row_count_per_page = ImMax((int)((inner_size.y - style.FramePadding.y) / g.FontSize), 1);
        state->Stb.row_count_per_page = row_count_per_page;

        const int k_mask = (io.KeyShift ? STB_TEXTEDIT_K_SHIFT : 0);
        const bool is_wordmove_key_down = is_osx ? io.KeyAlt : io.KeyCtrl;                     // OS X style: Text editing cursor movement using Alt instead of Ctrl
        const bool is_startend_key_down = is_osx && io.KeySuper && !io.KeyCtrl && !io.KeyAlt;  // OS X style: Line/Text Start and End using Cmd+Arrows instead of Home/End

        // Using Shortcut() with ImGuiInputFlags_RouteFocused (default policy) to allow routing operations for other code (e.g. calling window trying to use CTRL+A and CTRL+B: formet would be handled by InputText)
        // Otherwise we could simply assume that we own the keys as we are active.
        const ImGuiInputFlags f_repeat = ImGuiInputFlags_Repeat;
        const bool is_cut = (Shortcut(ImGuiMod_Shortcut | ImGuiKey_X, id, f_repeat) || Shortcut(ImGuiMod_Shift | ImGuiKey_Delete, id, f_repeat)) && !is_readonly && !is_password && (!is_multiline || state->HasSelection());
        const bool is_copy = (Shortcut(ImGuiMod_Shortcut | ImGuiKey_C, id) || Shortcut(ImGuiMod_Ctrl | ImGuiKey_Insert, id)) && !is_password && (!is_multiline || state->HasSelection());
        const bool is_paste = (Shortcut(ImGuiMod_Shortcut | ImGuiKey_V, id, f_repeat) || Shortcut(ImGuiMod_Shift | ImGuiKey_Insert, id, f_repeat)) && !is_readonly;
        const bool is_undo = (Shortcut(ImGuiMod_Shortcut | ImGuiKey_Z, id, f_repeat)) && !is_readonly && is_undoable;
        const bool is_redo = (Shortcut(ImGuiMod_Shortcut | ImGuiKey_Y, id, f_repeat) || (is_osx && Shortcut(ImGuiMod_Shortcut | ImGuiMod_Shift | ImGuiKey_Z, id, f_repeat))) && !is_readonly && is_undoable;
        const bool is_select_all = Shortcut(ImGuiMod_Shortcut | ImGuiKey_A, id);

        // We allow validate/cancel with Nav source (gamepad) to makes it easier to undo an accidental NavInput press with no keyboard wired, but otherwise it isn't very useful.
        const bool nav_gamepad_active = (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0 && (io.BackendFlags & ImGuiBackendFlags_HasGamepad) != 0;
        const bool is_enter_pressed = IsKeyPressed(ImGuiKey_Enter, true) || IsKeyPressed(ImGuiKey_KeypadEnter, true);
        const bool is_gamepad_validate = nav_gamepad_active && (IsKeyPressed(ImGuiKey_NavGamepadActivate, false) || IsKeyPressed(ImGuiKey_NavGamepadInput, false));
        const bool is_cancel = Shortcut(ImGuiKey_Escape, id, f_repeat) || (nav_gamepad_active && Shortcut(ImGuiKey_NavGamepadCancel, id, f_repeat));

        // FIXME: Should use more Shortcut() and reduce IsKeyPressed()+SetKeyOwner(), but requires modifiers combination to be taken account of.
        if (IsKeyPressed(ImGuiKey_LeftArrow)) { state->OnKeyPressed((is_startend_key_down ? STB_TEXTEDIT_K_LINESTART : is_wordmove_key_down ? STB_TEXTEDIT_K_WORDLEFT : STB_TEXTEDIT_K_LEFT) | k_mask); }
        else if (IsKeyPressed(ImGuiKey_RightArrow)) { state->OnKeyPressed((is_startend_key_down ? STB_TEXTEDIT_K_LINEEND : is_wordmove_key_down ? STB_TEXTEDIT_K_WORDRIGHT : STB_TEXTEDIT_K_RIGHT) | k_mask); }
        else if (IsKeyPressed(ImGuiKey_UpArrow) && is_multiline) { if (io.KeyCtrl) SetScrollY(draw_window, ImMax(draw_window->Scroll.y - g.FontSize, 0.0f)); else state->OnKeyPressed((is_startend_key_down ? STB_TEXTEDIT_K_TEXTSTART : STB_TEXTEDIT_K_UP) | k_mask); }
        else if (IsKeyPressed(ImGuiKey_DownArrow) && is_multiline) { if (io.KeyCtrl) SetScrollY(draw_window, ImMin(draw_window->Scroll.y + g.FontSize, GetScrollMaxY())); else state->OnKeyPressed((is_startend_key_down ? STB_TEXTEDIT_K_TEXTEND : STB_TEXTEDIT_K_DOWN) | k_mask); }
        else if (IsKeyPressed(ImGuiKey_PageUp) && is_multiline) { state->OnKeyPressed(STB_TEXTEDIT_K_PGUP | k_mask); scroll_y -= row_count_per_page * g.FontSize; }
        else if (IsKeyPressed(ImGuiKey_PageDown) && is_multiline) { state->OnKeyPressed(STB_TEXTEDIT_K_PGDOWN | k_mask); scroll_y += row_count_per_page * g.FontSize; }
        else if (IsKeyPressed(ImGuiKey_Home)) { state->OnKeyPressed(io.KeyCtrl ? STB_TEXTEDIT_K_TEXTSTART | k_mask : STB_TEXTEDIT_K_LINESTART | k_mask); }
        else if (IsKeyPressed(ImGuiKey_End)) { state->OnKeyPressed(io.KeyCtrl ? STB_TEXTEDIT_K_TEXTEND | k_mask : STB_TEXTEDIT_K_LINEEND | k_mask); }
        else if (IsKeyPressed(ImGuiKey_Delete) && !is_readonly && !is_cut)
        {
            if (!state->HasSelection())
            {
                // OSX doesn't seem to have Super+Delete to delete until end-of-line, so we don't emulate that (as opposed to Super+Backspace)
                if (is_wordmove_key_down)
                    state->OnKeyPressed(STB_TEXTEDIT_K_WORDRIGHT | STB_TEXTEDIT_K_SHIFT);
            }
            state->OnKeyPressed(STB_TEXTEDIT_K_DELETE | k_mask);
        }
        else if (IsKeyPressed(ImGuiKey_Backspace) && !is_readonly)
        {
            if (!state->HasSelection())
            {
                if (is_wordmove_key_down)
                    state->OnKeyPressed(STB_TEXTEDIT_K_WORDLEFT | STB_TEXTEDIT_K_SHIFT);
                else if (is_osx && io.KeySuper && !io.KeyAlt && !io.KeyCtrl)
                    state->OnKeyPressed(STB_TEXTEDIT_K_LINESTART | STB_TEXTEDIT_K_SHIFT);
            }
            state->OnKeyPressed(STB_TEXTEDIT_K_BACKSPACE | k_mask);
        }
        else if (is_enter_pressed || is_gamepad_validate)
        {
            // Determine if we turn Enter into a \n character
            bool ctrl_enter_for_new_line = (flags & ImGuiInputTextFlags_CtrlEnterForNewLine) != 0;
            if (!is_multiline || is_gamepad_validate || (ctrl_enter_for_new_line && !io.KeyCtrl) || (!ctrl_enter_for_new_line && io.KeyCtrl))
            {
                validated = true;
                if (io.ConfigInputTextEnterKeepActive && !is_multiline)
                    state->SelectAll(); // No need to scroll
                else
                    clear_active_id = true;
            }
            else if (!is_readonly)
            {
                unsigned int c = '\n'; // Insert new line
                if (input_text_filter_character(&g, &c, flags, callback, callback_user_data, ImGuiInputSource_Keyboard))
                    state->OnKeyPressed((int)c);
            }
        }
        else if (is_cancel)
        {
            if (flags & ImGuiInputTextFlags_EscapeClearsAll)
            {
                if (buf[0] != 0)
                {
                    revert_edit = true;
                }
                else
                {
                    render_cursor = render_selection = false;
                    clear_active_id = true;
                }
            }
            else
            {
                clear_active_id = revert_edit = true;
                render_cursor = render_selection = false;
            }
        }
        else if (is_undo || is_redo)
        {
            state->OnKeyPressed(is_undo ? STB_TEXTEDIT_K_UNDO : STB_TEXTEDIT_K_REDO);
            state->ClearSelection();
        }
        else if (is_select_all)
        {
            state->SelectAll();
            state->CursorFollow = true;
        }
        else if (is_cut || is_copy)
        {
            // Cut, Copy
            if (io.SetClipboardTextFn)
            {
                const int ib = state->HasSelection() ? ImMin(state->Stb.select_start, state->Stb.select_end) : 0;
                const int ie = state->HasSelection() ? ImMax(state->Stb.select_start, state->Stb.select_end) : state->CurLenW;
                const int clipboard_data_len = ImTextCountUtf8BytesFromStr(state->TextW.Data + ib, state->TextW.Data + ie) + 1;
                char* clipboard_data = (char*)IM_ALLOC(clipboard_data_len * sizeof(char));
                ImTextStrToUtf8(clipboard_data, clipboard_data_len, state->TextW.Data + ib, state->TextW.Data + ie);
                SetClipboardText(clipboard_data);
                MemFree(clipboard_data);
            }
            if (is_cut)
            {
                if (!state->HasSelection())
                    state->SelectAll();
                state->CursorFollow = true;
                stb_textedit_cut(state, &state->Stb);
            }
        }
        else if (is_paste)
        {
            if (const char* clipboard = GetClipboardText())
            {
                // Filter pasted buffer
                const int clipboard_len = (int)strlen(clipboard);
                ImWchar* clipboard_filtered = (ImWchar*)IM_ALLOC((clipboard_len + 1) * sizeof(ImWchar));
                int clipboard_filtered_len = 0;
                for (const char* s = clipboard; *s != 0; )
                {
                    unsigned int c;
                    s += ImTextCharFromUtf8(&c, s, NULL);
                    if (!input_text_filter_character(&g, &c, flags, callback, callback_user_data, ImGuiInputSource_Clipboard))
                        continue;
                    clipboard_filtered[clipboard_filtered_len++] = (ImWchar)c;
                }
                clipboard_filtered[clipboard_filtered_len] = 0;
                if (clipboard_filtered_len > 0) // If everything was filtered, ignore the pasting operation
                {
                    stb_textedit_paste(state, &state->Stb, clipboard_filtered, clipboard_filtered_len);
                    state->CursorFollow = true;
                }
                MemFree(clipboard_filtered);
            }
        }

        // Update render selection flag after events have been handled, so selection highlight can be displayed during the same frame.
        render_selection |= state->HasSelection() && (RENDER_SELECTION_WHEN_INACTIVE || render_cursor);
    }

    // Process callbacks and apply result back to user's buffer.
    const char* apply_new_text = NULL;
    int apply_new_text_length = 0;
    if (g.ActiveId == id)
    {
        IM_ASSERT(state != NULL);
        if (revert_edit && !is_readonly)
        {
            if (flags & ImGuiInputTextFlags_EscapeClearsAll)
            {
                // Clear input
                IM_ASSERT(buf[0] != 0);
                apply_new_text = "";
                apply_new_text_length = 0;
                value_changed = true;
                IMSTB_TEXTEDIT_CHARTYPE empty_string;
                stb_textedit_replace(state, &state->Stb, &empty_string, 0);
            }
            else if (strcmp(buf, state->InitialTextA.Data) != 0)
            {
                // Restore initial value. Only return true if restoring to the initial value changes the current buffer contents.
                // Push records into the undo stack so we can CTRL+Z the revert operation itself
                apply_new_text = state->InitialTextA.Data;
                apply_new_text_length = state->InitialTextA.Size - 1;
                value_changed = true;
                ImVector<ImWchar> w_text;
                if (apply_new_text_length > 0)
                {
                    w_text.resize(ImTextCountCharsFromUtf8(apply_new_text, apply_new_text + apply_new_text_length) + 1);
                    ImTextStrFromUtf8(w_text.Data, w_text.Size, apply_new_text, apply_new_text + apply_new_text_length);
                }
                stb_textedit_replace(state, &state->Stb, w_text.Data, (apply_new_text_length > 0) ? (w_text.Size - 1) : 0);
            }
        }

        // Apply ASCII value
        if (!is_readonly)
        {
            state->TextAIsValid = true;
            state->TextA.resize(state->TextW.Size * 4 + 1);
            ImTextStrToUtf8(state->TextA.Data, state->TextA.Size, state->TextW.Data, NULL);
        }

        // When using 'ImGuiInputTextFlags_EnterReturnsTrue' as a special case we reapply the live buffer back to the input buffer
        // before clearing ActiveId, even though strictly speaking it wasn't modified on this frame.
        // If we didn't do that, code like InputInt() with ImGuiInputTextFlags_EnterReturnsTrue would fail.
        // This also allows the user to use InputText() with ImGuiInputTextFlags_EnterReturnsTrue without maintaining any user-side storage
        // (please note that if you use this property along ImGuiInputTextFlags_CallbackResize you can end up with your temporary string object
        // unnecessarily allocating once a frame, either store your string data, either if you don't then don't use ImGuiInputTextFlags_CallbackResize).
        const bool apply_edit_back_to_user_buffer = !revert_edit || (validated && (flags & ImGuiInputTextFlags_EnterReturnsTrue) != 0);
        if (apply_edit_back_to_user_buffer)
        {
            // Apply new value immediately - copy modified buffer back
            // Note that as soon as the input box is active, the in-widget value gets priority over any underlying modification of the input buffer
            // FIXME: We actually always render 'buf' when calling DrawList->AddText, making the comment above incorrect.
            // FIXME-OPT: CPU waste to do this every time the widget is active, should mark dirty state from the stb_textedit callbacks.

            // User callback
            if ((flags & (ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackEdit | ImGuiInputTextFlags_CallbackAlways)) != 0)
            {
                IM_ASSERT(callback != NULL);

                // The reason we specify the usage semantic (Completion/History) is that Completion needs to disable keyboard TABBING at the moment.
                ImGuiInputTextFlags event_flag = 0;
                ImGuiKey event_key = ImGuiKey_None;
                if ((flags & ImGuiInputTextFlags_CallbackCompletion) != 0 && Shortcut(ImGuiKey_Tab, id))
                {
                    event_flag = ImGuiInputTextFlags_CallbackCompletion;
                    event_key = ImGuiKey_Tab;
                }
                else if ((flags & ImGuiInputTextFlags_CallbackHistory) != 0 && IsKeyPressed(ImGuiKey_UpArrow))
                {
                    event_flag = ImGuiInputTextFlags_CallbackHistory;
                    event_key = ImGuiKey_UpArrow;
                }
                else if ((flags & ImGuiInputTextFlags_CallbackHistory) != 0 && IsKeyPressed(ImGuiKey_DownArrow))
                {
                    event_flag = ImGuiInputTextFlags_CallbackHistory;
                    event_key = ImGuiKey_DownArrow;
                }
                else if ((flags & ImGuiInputTextFlags_CallbackEdit) && state->Edited)
                {
                    event_flag = ImGuiInputTextFlags_CallbackEdit;
                }
                else if (flags & ImGuiInputTextFlags_CallbackAlways)
                {
                    event_flag = ImGuiInputTextFlags_CallbackAlways;
                }

                if (event_flag)
                {
                    ImGuiInputTextCallbackData callback_data;
                    callback_data.Ctx = &g;
                    callback_data.EventFlag = event_flag;
                    callback_data.Flags = flags;
                    callback_data.UserData = callback_user_data;

                    char* callback_buf = is_readonly ? buf : state->TextA.Data;
                    callback_data.EventKey = event_key;
                    callback_data.Buf = callback_buf;
                    callback_data.BufTextLen = state->CurLenA;
                    callback_data.BufSize = state->BufCapacityA;
                    callback_data.BufDirty = false;

                    // We have to convert from wchar-positions to UTF-8-positions, which can be pretty slow (an incentive to ditch the ImWchar buffer, see https://github.com/nothings/stb/issues/188)
                    ImWchar* text = state->TextW.Data;
                    const int utf8_cursor_pos = callback_data.CursorPos = ImTextCountUtf8BytesFromStr(text, text + state->Stb.cursor);
                    const int utf8_selection_start = callback_data.SelectionStart = ImTextCountUtf8BytesFromStr(text, text + state->Stb.select_start);
                    const int utf8_selection_end = callback_data.SelectionEnd = ImTextCountUtf8BytesFromStr(text, text + state->Stb.select_end);

                    // Call user code
                    callback(&callback_data);

                    // Read back what user may have modified
                    callback_buf = is_readonly ? buf : state->TextA.Data; // Pointer may have been invalidated by a resize callback
                    IM_ASSERT(callback_data.Buf == callback_buf);         // Invalid to modify those fields
                    IM_ASSERT(callback_data.BufSize == state->BufCapacityA);
                    IM_ASSERT(callback_data.Flags == flags);
                    const bool buf_dirty = callback_data.BufDirty;
                    if (callback_data.CursorPos != utf8_cursor_pos || buf_dirty) { state->Stb.cursor = ImTextCountCharsFromUtf8(callback_data.Buf, callback_data.Buf + callback_data.CursorPos); state->CursorFollow = true; }
                    if (callback_data.SelectionStart != utf8_selection_start || buf_dirty) { state->Stb.select_start = (callback_data.SelectionStart == callback_data.CursorPos) ? state->Stb.cursor : ImTextCountCharsFromUtf8(callback_data.Buf, callback_data.Buf + callback_data.SelectionStart); }
                    if (callback_data.SelectionEnd != utf8_selection_end || buf_dirty) { state->Stb.select_end = (callback_data.SelectionEnd == callback_data.SelectionStart) ? state->Stb.select_start : ImTextCountCharsFromUtf8(callback_data.Buf, callback_data.Buf + callback_data.SelectionEnd); }
                    if (buf_dirty)
                    {
                        IM_ASSERT(!is_readonly);
                        IM_ASSERT(callback_data.BufTextLen == (int)strlen(callback_data.Buf)); // You need to maintain BufTextLen if you change the text!
                        input_text_reconcile_undo_state_after_user_callback(state, callback_data.Buf, callback_data.BufTextLen); // FIXME: Move the rest of this block inside function and rename to InputTextReconcileStateAfterUserCallback() ?
                        if (callback_data.BufTextLen > backup_current_text_length && is_resizable)
                            state->TextW.resize(state->TextW.Size + (callback_data.BufTextLen - backup_current_text_length)); // Worse case scenario resize
                        state->CurLenW = ImTextStrFromUtf8(state->TextW.Data, state->TextW.Size, callback_data.Buf, NULL);
                        state->CurLenA = callback_data.BufTextLen;  // Assume correct length and valid UTF-8 from user, saves us an extra strlen()
                        state->CursorAnimReset();
                    }
                }
            }

            // Will copy result string if modified
            if (!is_readonly && strcmp(state->TextA.Data, buf) != 0)
            {
                apply_new_text = state->TextA.Data;
                apply_new_text_length = state->CurLenA;
                value_changed = true;
            }
        }
    }

    // Handle reapplying final data on deactivation (see InputTextDeactivateHook() for details)
    if (g.InputTextDeactivatedState.ID == id)
    {
        if (g.ActiveId != id && IsItemDeactivatedAfterEdit() && !is_readonly && strcmp(g.InputTextDeactivatedState.TextA.Data, buf) != 0)
        {
            apply_new_text = g.InputTextDeactivatedState.TextA.Data;
            apply_new_text_length = g.InputTextDeactivatedState.TextA.Size - 1;
            value_changed = true;
            //IMGUI_DEBUG_LOG("InputText(): apply Deactivated data for 0x%08X: \"%.*s\".\n", id, apply_new_text_length, apply_new_text);
        }
        g.InputTextDeactivatedState.ID = 0;
    }

    // Copy result to user buffer. This can currently only happen when (g.ActiveId == id)
    if (apply_new_text != NULL)
    {
        // We cannot test for 'backup_current_text_length != apply_new_text_length' here because we have no guarantee that the size
        // of our owned buffer matches the size of the string object held by the user, and by design we allow InputText() to be used
        // without any storage on user's side.
        IM_ASSERT(apply_new_text_length >= 0);
        if (is_resizable)
        {
            ImGuiInputTextCallbackData callback_data;
            callback_data.Ctx = &g;
            callback_data.EventFlag = ImGuiInputTextFlags_CallbackResize;
            callback_data.Flags = flags;
            callback_data.Buf = buf;
            callback_data.BufTextLen = apply_new_text_length;
            callback_data.BufSize = ImMax(buf_size, apply_new_text_length + 1);
            callback_data.UserData = callback_user_data;
            callback(&callback_data);
            buf = callback_data.Buf;
            buf_size = callback_data.BufSize;
            apply_new_text_length = ImMin(callback_data.BufTextLen, buf_size - 1);
            IM_ASSERT(apply_new_text_length <= buf_size);
        }
        //IMGUI_DEBUG_PRINT("InputText(\"%s\"): apply_new_text length %d\n", label, apply_new_text_length);

        // If the underlying buffer resize was denied or not carried to the next frame, apply_new_text_length+1 may be >= buf_size.
        ImStrncpy(buf, apply_new_text, ImMin(apply_new_text_length + 1, buf_size));
    }

    // Release active ID at the end of the function (so e.g. pressing Return still does a final application of the value)
    // Otherwise request text input ahead for next frame.
    if (g.ActiveId == id && clear_active_id)
        ClearActiveID();
    else if (g.ActiveId == id)
        g.WantTextInputNextFrame = 1;

    auto& anim = LEYAWIN_ANIM(ImGuiID, float);
    float& it_anim = MapGet(anim, id);

    // Render frame
    if (!is_multiline)
    {
        window->DrawList->AddRectFilled(clickable.Min, clickable.Max, gui->get_clr(clr->combo_color), set->input_rounding);
    }

    const ImVec4 clip_rect(clickable.Min.x, clickable.Min.y, clickable.Min.x + inner_size.x, clickable.Min.y + inner_size.y); // Not using frame_bb.Max because we have adjusted size
    ImVec2 draw_pos = is_multiline ? draw_window->DC.CursorPos : clickable.Min + ImVec2((size_arg.y - CalcTextSize(hint).y), (size_arg.y - CalcTextSize(hint).y) - 1) / 2;
    ImVec2 text_size(0.0f, 0.0f);

    // Set upper limit of single-line InputTextEx() at 2 million characters strings. The current pathological worst case is a long line
    // without any carriage return, which would makes ImFont::RenderText() reserve too many vertices and probably crash. Avoid it altogether.
    // Note that we only use this limit on single-line InputText(), so a pathologically large line on a InputTextMultiline() would still crash.
    const int buf_display_max_length = 2 * 1024 * 1024;
    const char* buf_display = buf_display_from_state ? state->TextA.Data : buf; //-V595
    const char* buf_display_end = NULL; // We have specialized paths below for setting the length
    if (is_displaying_hint)
    {
        buf_display = hint;
        buf_display_end = hint + strlen(hint);
    }

    // Render text. We currently only render selection when the widget is active or while scrolling.
    // FIXME: We could remove the '&& render_cursor' to keep rendering selection when inactive.
    if (render_cursor || render_selection)
    {
        IM_ASSERT(state != NULL);
        if (!is_displaying_hint)
            buf_display_end = buf_display + state->CurLenA;

        // Render text (with cursor and selection)
        // This is going to be messy. We need to:
        // - Display the text (this alone can be more easily clipped)
        // - Handle scrolling, highlight selection, display cursor (those all requires some form of 1d->2d cursor position calculation)
        // - Measure text height (for scrollbar)
        // We are attempting to do most of that in **one main pass** to minimize the computation cost (non-negligible for large amount of text) + 2nd pass for selection rendering (we could merge them by an extra refactoring effort)
        // FIXME: This should occur on buf_display but we'd need to maintain cursor/select_start/select_end for UTF-8.
        const ImWchar* text_begin = state->TextW.Data;
        ImVec2 cursor_offset, select_start_offset;

        {
            // Find lines numbers straddling 'cursor' (slot 0) and 'select_start' (slot 1) positions.
            const ImWchar* searches_input_ptr[2] = { NULL, NULL };
            int searches_result_line_no[2] = { -1000, -1000 };
            int searches_remaining = 0;
            if (render_cursor)
            {
                searches_input_ptr[0] = text_begin + state->Stb.cursor;
                searches_result_line_no[0] = -1;
                searches_remaining++;
            }
            if (render_selection)
            {
                searches_input_ptr[1] = text_begin + ImMin(state->Stb.select_start, state->Stb.select_end);
                searches_result_line_no[1] = -1;
                searches_remaining++;
            }

            // Iterate all lines to find our line numbers
            // In multi-line mode, we never exit the loop until all lines are counted, so add one extra to the searches_remaining counter.
            searches_remaining += is_multiline ? 1 : 0;
            int line_count = 0;
            //for (const ImWchar* s = text_begin; (s = (const ImWchar*)wcschr((const wchar_t*)s, (wchar_t)'\n')) != NULL; s++)  // FIXME-OPT: Could use this when wchar_t are 16-bit
            for (const ImWchar* s = text_begin; *s != 0; s++)
                if (*s == '\n')
                {
                    line_count++;
                    if (searches_result_line_no[0] == -1 && s >= searches_input_ptr[0]) { searches_result_line_no[0] = line_count; if (--searches_remaining <= 0) break; }
                    if (searches_result_line_no[1] == -1 && s >= searches_input_ptr[1]) { searches_result_line_no[1] = line_count; if (--searches_remaining <= 0) break; }
                }
            line_count++;
            if (searches_result_line_no[0] == -1)
                searches_result_line_no[0] = line_count;
            if (searches_result_line_no[1] == -1)
                searches_result_line_no[1] = line_count;

            // Calculate 2d position by finding the beginning of the line and measuring distance
            cursor_offset.x = input_text_calc_text_size_w(&g, ImStrbolW(searches_input_ptr[0], text_begin), searches_input_ptr[0]).x;
            cursor_offset.y = searches_result_line_no[0] * g.FontSize;
            if (searches_result_line_no[1] >= 0)
            {
                select_start_offset.x = input_text_calc_text_size_w(&g, ImStrbolW(searches_input_ptr[1], text_begin), searches_input_ptr[1]).x;
                select_start_offset.y = searches_result_line_no[1] * g.FontSize;
            }

            // Store text height (note that we haven't calculated text width at all, see GitHub issues #383, #1224)
            if (is_multiline)
                text_size = ImVec2(inner_size.x, line_count * g.FontSize);
        }

        // Scroll
        if (render_cursor && state->CursorFollow)
        {
            // Horizontal scroll in chunks of quarter width
            if (!(flags & ImGuiInputTextFlags_NoHorizontalScroll))
            {
                const float scroll_increment_x = inner_size.x * 0.25f;
                const float visible_width = inner_size.x - style.FramePadding.x;
                if (cursor_offset.x < state->ScrollX)
                    state->ScrollX = IM_TRUNC(ImMax(0.0f, cursor_offset.x - scroll_increment_x));
                else if (cursor_offset.x - visible_width >= state->ScrollX)
                    state->ScrollX = IM_TRUNC(cursor_offset.x - visible_width + scroll_increment_x);
            }
            else
            {
                state->ScrollX = 0.0f;
            }

            // Vertical scroll
            if (is_multiline)
            {
                // Test if cursor is vertically visible
                if (cursor_offset.y - g.FontSize < scroll_y)
                    scroll_y = ImMax(0.0f, cursor_offset.y - g.FontSize);
                else if (cursor_offset.y - (inner_size.y - style.FramePadding.y * 2.0f) >= scroll_y)
                    scroll_y = cursor_offset.y - inner_size.y + style.FramePadding.y * 2.0f;
                const float scroll_max_y = ImMax((text_size.y + style.FramePadding.y * 2.0f) - inner_size.y, 0.0f);
                scroll_y = ImClamp(scroll_y, 0.0f, scroll_max_y);
                draw_pos.y += (draw_window->Scroll.y - scroll_y);   // Manipulate cursor pos immediately avoid a frame of lag
                draw_window->Scroll.y = scroll_y;
            }

            state->CursorFollow = false;
        }

        // Draw selection
        const ImVec2 draw_scroll = ImVec2(state->ScrollX, 0.0f);
        if (render_selection)
        {
            const ImWchar* text_selected_begin = text_begin + ImMin(state->Stb.select_start, state->Stb.select_end);
            const ImWchar* text_selected_end = text_begin + ImMax(state->Stb.select_start, state->Stb.select_end);

            ImU32 bg_color = gui->get_clr(clr->accent_color, render_cursor ? 1.0f : 0.6f); // FIXME: current code flow mandate that render_cursor is always true here, we are leaving the transparent one for tests.
            float bg_offy_up = is_multiline ? 0.0f : -1.0f;    // FIXME: those offsets should be part of the style? they don't play so well with multi-line selection.
            float bg_offy_dn = is_multiline ? 0.0f : 2.0f;
            ImVec2 rect_pos = draw_pos + select_start_offset - draw_scroll;
            for (const ImWchar* p = text_selected_begin; p < text_selected_end; )
            {
                if (rect_pos.y > clip_rect.w + g.FontSize)
                    break;
                if (rect_pos.y < clip_rect.y)
                {
                    //p = (const ImWchar*)wmemchr((const wchar_t*)p, '\n', text_selected_end - p);  // FIXME-OPT: Could use this when wchar_t are 16-bit
                    //p = p ? p + 1 : text_selected_end;
                    while (p < text_selected_end)
                        if (*p++ == '\n')
                            break;
                }
                else
                {
                    ImVec2 rect_size = input_text_calc_text_size_w(&g, p, text_selected_end, &p, NULL, true);
                    if (rect_size.x <= 0.0f) rect_size.x = IM_TRUNC(g.Font->GetCharAdvance((ImWchar)' ') * 0.50f); // So we can see selected empty lines
                    ImRect rect(rect_pos + ImVec2(0.0f, bg_offy_up - g.FontSize), rect_pos + ImVec2(rect_size.x, bg_offy_dn));
                    rect.ClipWith(clip_rect);
                    if (rect.Overlaps(clip_rect)) draw_window->DrawList->AddRectFilled(rect.Min, rect.Max, gui->get_clr(clr->accent_color, 0.3f), 2.f);
                }
                rect_pos.x = draw_pos.x - draw_scroll.x;
                rect_pos.y += g.FontSize;
            }
        }

        // We test for 'buf_display_max_length' as a way to avoid some pathological cases (e.g. single-line 1 MB string) which would make ImDrawList crash.
        if (is_multiline || (buf_display_end - buf_display) < buf_display_max_length)
            draw_window->DrawList->AddText(g.Font, g.FontSize, draw_pos - draw_scroll, GetColorU32(is_displaying_hint ? clr->text : clr->text_active), buf_display, buf_display_end, 0.0f, is_multiline ? NULL : &clip_rect);

        if (render_cursor)
        {
            state->CursorAnim += io.DeltaTime;
            bool cursor_is_visible = (!g.IO.ConfigInputTextCursorBlink) || (state->CursorAnim <= 0.0f) || ImFmod(state->CursorAnim, 1.20f) <= 0.80f;
            ImVec2 cursor_screen_pos = ImTrunc(draw_pos + cursor_offset - draw_scroll);
            ImRect cursor_screen_rect(cursor_screen_pos.x, cursor_screen_pos.y - g.FontSize + 0.5f, cursor_screen_pos.x + 1.0f, cursor_screen_pos.y - 1.5f);
            it_anim = ImLerp(it_anim, cursor_screen_rect.Min.x - clickable.Min.x, gui->fixed_speed(20.f));

            if (cursor_is_visible && cursor_screen_rect.Overlaps(clip_rect))
                draw_window->DrawList->AddLine(ImVec2(clickable.Min.x + it_anim, cursor_screen_rect.Min.y), ImVec2(clickable.Min.x + it_anim, cursor_screen_rect.GetBL().y), GetColorU32(clr->white_color));

            // Notify OS of text input position for advanced IME (-1 x offset so that Windows IME can cover our cursor. Bit of an extra nicety.)
            if (!is_readonly)
            {
                g.PlatformImeData.WantVisible = true;
                g.PlatformImeData.InputPos = ImVec2(cursor_screen_pos.x - 1.0f, cursor_screen_pos.y - g.FontSize);
                g.PlatformImeData.InputLineHeight = g.FontSize;
            }
        }
    }
    else
    {
        if (is_multiline) text_size = ImVec2(inner_size.x, input_text_calc_text_len_and_line_count(buf_display, &buf_display_end) * g.FontSize); // We don't need width
        else if (!is_displaying_hint && g.ActiveId == id) buf_display_end = buf_display + state->CurLenA;
        else if (!is_displaying_hint) buf_display_end = buf_display + strlen(buf_display);

        if (is_multiline || (buf_display_end - buf_display) < buf_display_max_length)
            draw_window->DrawList->AddText(g.Font, g.FontSize, draw_pos, GetColorU32(is_displaying_hint ? clr->text : clr->text_active), buf_display, buf_display_end, 0.0f, is_multiline ? NULL : &clip_rect);
    }

    if (is_password && !is_displaying_hint) PopFont();

    if (is_multiline)
    {
        Dummy(ImVec2(text_size.x, text_size.y + style.FramePadding.y));
        g.NextItemData.ItemFlags |= ImGuiItemFlags_Inputable | ImGuiItemFlags_NoTabStop;
        EndChild();
        item_data_backup.StatusFlags |= (g.LastItemData.StatusFlags & ImGuiItemStatusFlags_HoveredWindow);

        EndGroup();
        if (g.LastItemData.ID == 0)
        {
            g.LastItemData.ID = id;
            g.LastItemData.InFlags = item_data_backup.InFlags;
            g.LastItemData.StatusFlags = item_data_backup.StatusFlags;
        }
    }

    if (value_changed && !(flags & ImGuiInputTextFlags_NoMarkEdited)) MarkItemEdited(id);

    PopFont();

    if ((flags & ImGuiInputTextFlags_EnterReturnsTrue) != 0) return validated;
    else return value_changed;
}
#endif

bool c_widgets::text_field(const char* label, const char* hint, const ImVec2& size, char* buf, size_t buf_size, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data)
{
    IM_ASSERT(!(flags & ImGuiInputTextFlags_Multiline));

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;

    const float width = size.x > 0.f ? size.x : (GetContentRegionMax().x - GetStyle().WindowPadding.x);
    const float height = size.y > 0.f ? size.y : 35.f;
    const float verticalPadding =
        ImMax(0.f, (height - GetTextLineHeight()) * 0.5f);

    // InputText must own the item ID and active state. The old implementation
    // submitted a decorative item with the same ID immediately before
    // InputText, leaving a conflicting ActiveId after the backing buffer was
    // cleared by the config-create button.
    PushStyleColor(
        ImGuiCol_FrameBg,
        gui->get_clr(clr->element_background, 0.76f));
    PushStyleColor(
        ImGuiCol_FrameBgHovered,
        gui->get_clr(clr->element_background, 0.96f));
    PushStyleColor(
        ImGuiCol_FrameBgActive,
        gui->get_clr(clr->combo_color, 0.98f));
    PushStyleColor(
        ImGuiCol_Border,
        gui->get_clr(clr->child_stroke_bar, 0.92f));
    PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    PushStyleVar(ImGuiStyleVar_FrameRounding, set->input_rounding + 4.f);
    PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(10.f, verticalPadding));
    PushItemWidth(width);
    const bool changed =
        InputTextWithHint(label, hint, buf, buf_size, flags, callback, user_data);
    PopItemWidth();
    PopStyleVar(3);
    PopStyleColor(4);
    return changed;
}

static const char* items_array_getter(void* data, int idx)
{
    const char* const* items = (const char* const*)data;
    return items[idx];
}

#if 0
// legacy text_field stub
#endif

static void color_edit_restore_hs(const float* col, float* H, float* S, float* V)
{
    ImGuiContext& g = *GImGui;

    if (*S == 0.0f || (*H == 0.0f && g.ColorEditSavedHue == 1))
        *H = g.ColorEditSavedHue;

    if (*V == 0.0f) *S = g.ColorEditSavedSat;
}

// Accepts what people actually have on the clipboard: #RGB, #RGBA, #RRGGBB,
// #RRGGBBAA (bare or 0x-prefixed), rgb()/rgba() and plain comma lists. Values
// are 0-255 unless one of them is written with a decimal point.
static bool parse_color_string(const char* text, float out[4]) noexcept
{
    if (!text || !out)
        return false;
    while (*text == ' ' || *text == '\t')
        ++text;
    if (!*text)
        return false;

    if (strchr(text, ',') || _strnicmp(text, "rgb", 3) == 0)
    {
        const char* cursor = text;
        if (_strnicmp(cursor, "rgba", 4) == 0)
            cursor += 4;
        else if (_strnicmp(cursor, "rgb", 3) == 0)
            cursor += 3;
        while (*cursor == ' ' || *cursor == '(')
            ++cursor;

        double values[4]{};
        int count = 0;
        bool fractional = false;
        while (count < 4 && *cursor)
        {
            char* end = nullptr;
            const double value = strtod(cursor, &end);
            if (end == cursor)
                break;
            for (const char* scan = cursor; scan < end; ++scan)
            {
                if (*scan == '.')
                    fractional = true;
            }
            values[count++] = value;
            cursor = end;
            while (*cursor == ' ' || *cursor == ',')
                ++cursor;
        }
        if (count < 3)
            return false;

        const double divisor = fractional ? 1.0 : 255.0;
        for (int i = 0; i < 3; ++i)
            out[i] = ImSaturate(static_cast<float>(values[i] / divisor));
        out[3] = count > 3
            ? ImSaturate(static_cast<float>(values[3] / divisor))
            : 1.f;
        return true;
    }

    if (*text == '#')
        ++text;
    else if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        text += 2;

    char digits[9]{};
    int length = 0;
    for (const char* scan = text; *scan; ++scan)
    {
        if (*scan == ' ')
            continue;
        if (!isxdigit(static_cast<unsigned char>(*scan)) || length >= 8)
            return false;
        digits[length++] = *scan;
    }
    if (length != 3 && length != 4 && length != 6 && length != 8)
        return false;

    const bool shorthand = length <= 4;
    const int channels = (length == 4 || length == 8) ? 4 : 3;
    out[3] = 1.f;
    for (int i = 0; i < channels; ++i)
    {
        char component[3]{};
        component[0] = shorthand ? digits[i] : digits[i * 2];
        component[1] = shorthand ? digits[i] : digits[i * 2 + 1];
        out[i] =
            static_cast<float>(strtol(component, nullptr, 16)) / 255.f;
    }
    return true;
}

bool c_widgets::color_picker(const char* label, float col[4], ImGuiColorEditFlags flags, const float* ref_col)
{
    struct picker_state
    {
        ImVec2 sv;
        float hue, alpha;
    };

    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImDrawList* draw_list = window->DrawList;
    ImGuiStyle& style = g.Style;
    ImGuiIO& io = g.IO;

    const float width = CalcItemWidth();
    g.NextItemData.ClearFlags();

    PushID(label);
    BeginGroup();
    PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 0));

    if (!(flags & ImGuiColorEditFlags_NoSidePreview)) flags |= ImGuiColorEditFlags_NoSmallPreview;

    if (!(flags & ImGuiColorEditFlags_NoOptions)) ColorPickerOptionsPopup(col, flags);

    if (!(flags & ImGuiColorEditFlags_PickerMask_)) flags |= ((g.ColorEditOptions & ImGuiColorEditFlags_PickerMask_) ? g.ColorEditOptions : ImGuiColorEditFlags_DefaultOptions_) & ImGuiColorEditFlags_PickerMask_;
    if (!(flags & ImGuiColorEditFlags_InputMask_)) flags |= ((g.ColorEditOptions & ImGuiColorEditFlags_InputMask_) ? g.ColorEditOptions : ImGuiColorEditFlags_DefaultOptions_) & ImGuiColorEditFlags_InputMask_;

    IM_ASSERT(ImIsPowerOfTwo(flags & ImGuiColorEditFlags_PickerMask_));
    IM_ASSERT(ImIsPowerOfTwo(flags & ImGuiColorEditFlags_InputMask_));

    if (!(flags & ImGuiColorEditFlags_NoOptions)) flags |= (g.ColorEditOptions & ImGuiColorEditFlags_AlphaBar);
    int components = (flags & ImGuiColorEditFlags_NoAlpha) ? 3 : 4;
    bool alpha_bar = (flags & ImGuiColorEditFlags_AlphaBar) && !(flags & ImGuiColorEditFlags_NoAlpha);
    ImVec2 picker_pos = window->DC.CursorPos + ImVec2(0, 0);
    ImVec2 bar_pos = window->DC.CursorPos + ImVec2(0, 130);

    float picker_size = 190.f;
    float bar_width = 190.f;
    float bar_height = 4.f;

    float backup_initial_col[4];
    memcpy(backup_initial_col, col, components * sizeof(float));

    float H = col[0], S = col[1], V = col[2];
    float R = col[0], G = col[1], B = col[2];
    if (flags & ImGuiColorEditFlags_InputRGB)
    {
        ColorConvertRGBtoHSV(R, G, B, H, S, V);
        color_edit_restore_hs(col, &H, &S, &V);
    }
    else if (flags & ImGuiColorEditFlags_InputHSV)
    {
        ColorConvertHSVtoRGB(H, S, V, R, G, B);
    }

    bool value_changed = false, value_changed_h = false, value_changed_sv = false;

    PushItemFlag(ImGuiItemFlags_NoNav, true);

    SetCursorScreenPos(ImVec2(picker_pos.x, picker_pos.y));
    InvisibleButton("sv", ImVec2(picker_size, picker_size));
    if (IsItemActive())
    {
        S = ImSaturate((io.MousePos.x - picker_pos.x) / (picker_size - 1));
        V = 1.0f - ImSaturate((io.MousePos.y - picker_pos.y) / (picker_size - 1));

        if (g.ColorEditSavedColor == ColorConvertFloat4ToU32(ImVec4(col[0], col[1], col[2], 0))) H = g.ColorEditSavedHue;
        value_changed = value_changed_sv = true;
    }

    SetCursorScreenPos(ImVec2(picker_pos.x, picker_pos.y + 204.f));
    InvisibleButton("hue", ImVec2(bar_width, bar_height + 8));
    if (IsItemActive())
    {
        H = 1.f - ImSaturate((io.MousePos.x - picker_pos.x) / (bar_width - 1));
        value_changed = value_changed_h = true;
    }

    if (alpha_bar)
    {
        SetCursorScreenPos(ImVec2(picker_pos.x, picker_pos.y + 227.f));
        InvisibleButton("alpha", ImVec2(bar_width, bar_height + 8));
        if (IsItemActive())
        {
            col[3] = ImSaturate((io.MousePos.x - picker_pos.x) / (bar_width - 1));
            value_changed = true;
        }
    }
    PopItemFlag();

    if (value_changed_h || value_changed_sv)
    {
        if (flags & ImGuiColorEditFlags_InputRGB)
        {
            ColorConvertHSVtoRGB(H, S, V, col[0], col[1], col[2]);
            g.ColorEditSavedHue = H;
            g.ColorEditSavedSat = S;
            g.ColorEditSavedColor = ColorConvertFloat4ToU32(ImVec4(col[0], col[1], col[2], 0));
        }
        else if (flags & ImGuiColorEditFlags_InputHSV)
        {
            col[0] = H;
            col[1] = S;
            col[2] = V;
        }
    }

    const bool value_changed_fix_hue_wrap = false;

    // One paste-friendly field replaces the stacked RGB/HSV/Hex rows: those
    // called back into color_edit, which is a settings row now and stretched
    // the popup with three redundant copies of itself.
    {
        const MenuTheme::Palette& pickerTokens = MenuTheme::Colors();
        const MenuTheme::Metrics& pickerMetrics = MenuTheme::Sizes();
        const float pickerScale = leyawin_ui_scale();
        const float fieldTop =
            picker_pos.y + (alpha_bar ? 246.f : 222.f);

        float rgba[4] = {R, G, B, components > 3 ? col[3] : 1.f};
        char text[72]{};
        if (components > 3)
        {
            sprintf_s(
                text,
                "#%02X%02X%02X%02X",
                IM_F32_TO_INT8_SAT(rgba[0]),
                IM_F32_TO_INT8_SAT(rgba[1]),
                IM_F32_TO_INT8_SAT(rgba[2]),
                IM_F32_TO_INT8_SAT(rgba[3]));
        }
        else
        {
            sprintf_s(
                text,
                "#%02X%02X%02X",
                IM_F32_TO_INT8_SAT(rgba[0]),
                IM_F32_TO_INT8_SAT(rgba[1]),
                IM_F32_TO_INT8_SAT(rgba[2]));
        }

        SetCursorScreenPos(ImVec2(picker_pos.x, fieldTop));
        PushStyleVar(
            ImGuiStyleVar_FrameRounding,
            pickerMetrics.radiusSmall * pickerScale);
        PushStyleVar(
            ImGuiStyleVar_FramePadding,
            ImVec2(7.f * pickerScale, 4.f * pickerScale));
        PushStyleColor(ImGuiCol_FrameBg, pickerTokens.raised);
        PushStyleColor(ImGuiCol_Border, pickerTokens.borderSubtle);
        PushStyleVar(ImGuiStyleVar_FrameBorderSize, pickerMetrics.stroke);
        SetNextItemWidth(bar_width);
        const bool submitted = InputText(
            "##color_text",
            text,
            IM_ARRAYSIZE(text),
            ImGuiInputTextFlags_EnterReturnsTrue |
                ImGuiInputTextFlags_AutoSelectAll);
        const bool fieldHovered = IsItemHovered();
        PopStyleVar();
        PopStyleColor(2);
        PopStyleVar(2);

        if (fieldHovered)
        {
            SetTooltip(
                "%s",
                "#RRGGBB  #RRGGBBAA  #RGB\n"
                "rgb(255, 0, 128)  rgba(255, 0, 128, 128)\n"
                "255, 0, 128");
        }

        if (submitted && parse_color_string(text, rgba))
        {
            if (flags & ImGuiColorEditFlags_InputHSV)
            {
                ColorConvertRGBtoHSV(
                    rgba[0], rgba[1], rgba[2],
                    col[0], col[1], col[2]);
            }
            else
            {
                col[0] = rgba[0];
                col[1] = rgba[1];
                col[2] = rgba[2];
            }
            if (components > 3)
                col[3] = rgba[3];
            value_changed = true;
        }
    }

    if (value_changed_fix_hue_wrap && (flags & ImGuiColorEditFlags_InputRGB))
    {
        float new_H, new_S, new_V;
        ColorConvertRGBtoHSV(col[0], col[1], col[2], new_H, new_S, new_V);
        if (new_H <= 0 && H > 0)
        {
            if (new_V <= 0 && V != new_V)
                ColorConvertHSVtoRGB(H, S, new_V <= 0 ? V * 0.5f : new_V, col[0], col[1], col[2]);
            else if (new_S <= 0)
                ColorConvertHSVtoRGB(H, new_S <= 0 ? S * 0.5f : new_S, new_V, col[0], col[1], col[2]);
        }
    }

    if (value_changed)
    {
        if (flags & ImGuiColorEditFlags_InputRGB)
        {
            R = col[0];
            G = col[1];
            B = col[2];
            ColorConvertRGBtoHSV(R, G, B, H, S, V);
            color_edit_restore_hs(col, &H, &S, &V);
        }
        else if (flags & ImGuiColorEditFlags_InputHSV)
        {
            H = col[0];
            S = col[1];
            V = col[2];
            ColorConvertHSVtoRGB(H, S, V, R, G, B);
        }
    }

    const int style_alpha8 = IM_F32_TO_INT8_SAT(style.Alpha);
    const ImU32 col_black = IM_COL32(0, 0, 0, style_alpha8);
    const ImU32 col_white = IM_COL32(255, 255, 255, style_alpha8);
    const ImU32 col_midgrey = IM_COL32(128, 128, 128, style_alpha8);
    const ImU32 col_hues[7] = { IM_COL32(255,0,0,style_alpha8), IM_COL32(255,0,255,style_alpha8), IM_COL32(0,0,255,style_alpha8),IM_COL32(0,255,255,style_alpha8), IM_COL32(0,255,0,style_alpha8), IM_COL32(255,255,0,style_alpha8), IM_COL32(255,0,0,style_alpha8) };

    ImVec4 hue_color_f(1, 1, 1, style.Alpha); ColorConvertHSVtoRGB(H, 1, 1, hue_color_f.x, hue_color_f.y, hue_color_f.z);
    ImU32 hue_color32 = ColorConvertFloat4ToU32(hue_color_f);
    ImU32 user_col32_striped_of_alpha = ColorConvertFloat4ToU32(ImVec4(R, G, B, style.Alpha));

    ImVec2 sv_cursor_pos;

    const int vtx_idx_0 = draw_list->VtxBuffer.Size;
    draw_list->AddRectFilled(picker_pos, picker_pos + ImVec2(picker_size, picker_size - 2), col_white, 3.0f);
    const int vtx_idx_1 = draw_list->VtxBuffer.Size;
    ShadeVertsLinearColorGradientKeepAlpha(draw_list, vtx_idx_0, vtx_idx_1, picker_pos, picker_pos + ImVec2(picker_size, 0.0f), col_white, hue_color32);

    gui->add_rect_filled_multi_color(draw_list, picker_pos, picker_pos + ImVec2(picker_size, picker_size), 0, 0, col_black, col_black, 3.f);

    sv_cursor_pos.x = ImClamp(IM_ROUND(picker_pos.x + ImSaturate(S) * picker_size), picker_pos.x + 2, picker_pos.x + picker_size - 2);
    sv_cursor_pos.y = ImClamp(IM_ROUND(picker_pos.y + ImSaturate(1 - V) * (picker_size)), picker_pos.y + 2, picker_pos.y + picker_size - 2);

    auto& anim = LEYAWIN_ANIM(ImGuiID, picker_state);
    picker_state& state = MapGet(anim, GetID(label));

    for (int i = 0; i < 6; ++i)
        gui->add_rect_filled_multi_color(draw_list, ImVec2(picker_pos.x + i * (bar_width / 6) - (i == 5 ? 1 : 0), picker_pos.y + 208), ImVec2(picker_pos.x + (i + 1) * (bar_width / 6) + (i == 0 ? 1 : 0), picker_pos.y + 208 + bar_height), col_hues[i], col_hues[i + 1], col_hues[i + 1], col_hues[i], 2.f, i == 0 ? ImDrawFlags_RoundCornersLeft : i == 5 ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersNone);
    float bar0_line_x = IM_ROUND(picker_pos.x + (1.f - H) * bar_width);
    bar0_line_x = ImClamp(bar0_line_x, picker_pos.x + 3.f, picker_pos.x + 186.f);

    state.hue = ImLerp(state.hue, bar0_line_x, g.IO.DeltaTime * 20.f);
    draw_list->AddCircleFilled(ImVec2(state.hue, picker_pos.y + 210), 5, GetColorU32(ImVec4(1.f, 1.f, 1.f, 1.f)));

    state.sv = ImLerp(state.sv, sv_cursor_pos, g.IO.DeltaTime * 10.f);

    draw_list->AddCircle(state.sv, 5.f, col_white, 12, 4.f);
    draw_list->AddCircleFilled(state.sv, 5.f, user_col32_striped_of_alpha, 12);

    if (alpha_bar)
    {
        float alpha = ImSaturate(col[3]);
        gui->add_rect_filled_multi_color(draw_list, picker_pos + ImVec2(0, 231), picker_pos + ImVec2(bar_width, 231 + bar_height), col_black, user_col32_striped_of_alpha, user_col32_striped_of_alpha, col_black, 2.f);
        float bar1_line_x = IM_ROUND(picker_pos.x + alpha * bar_width);
        bar1_line_x = ImClamp(bar1_line_x, picker_pos.x + 3.f, picker_pos.x + 186.f);

        state.alpha = ImLerp(state.alpha, bar1_line_x, g.IO.DeltaTime * 20.f);
        draw_list->AddCircleFilled(ImVec2(state.alpha, picker_pos.y + 233), 5, GetColorU32(ImVec4(1.f, 1.f, 1.f, 1.f)));
    }

    PopStyleVar();
    EndGroup();

    if (value_changed && memcmp(backup_initial_col, col, components * sizeof(float)) == 0) value_changed = false;
    if (value_changed && g.LastItemData.ID != 0)  MarkItemEdited(g.LastItemData.ID);

    PopID();

    return value_changed;
}

bool color_button(const char* desc_id, const ImVec4& col, ImGuiColorEditFlags flags, const ImVec2& size_arg, bool popup = false)
{
    struct cbutton_state
    {
        ImVec4 text;
    };

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiID id = window->GetID(desc_id);
    const float default_size = GetFrameHeight();

    const float width = GetWindowWidth() - g.Style.WindowPadding.x * 2;
    const float box_size = 18, rect_correct_size = 25;

    const ImVec2 rect_size = ImVec2(box_size, box_size);
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect rect(pos, pos + ImVec2(width, rect_correct_size));

    ItemSize(ImRect(rect.Min, rect.Max - ImVec2(0, 0)));
    if (!ItemAdd((flags & ImGuiColorEditFlags_NoLabel) ? rect : rect, id)) return false;

    bool hovered, held, pressed = ButtonBehavior(rect, id, &hovered, &held);

    if (flags & ImGuiColorEditFlags_NoAlpha) flags &= ~(ImGuiColorEditFlags_AlphaPreview | ImGuiColorEditFlags_AlphaPreviewHalf);

    ImVec4 col_rgb = col;
    if (flags & ImGuiColorEditFlags_InputHSV) ColorConvertHSVtoRGB(col_rgb.x, col_rgb.y, col_rgb.z, col_rgb.x, col_rgb.y, col_rgb.z);

    auto& anim = LEYAWIN_ANIM(ImGuiID, cbutton_state);
    cbutton_state& state = MapGet(anim, id);

    gui->render_text_clipped(set->poppins_widget, rect.Min, rect.Max, gui->get_clr(clr->text), desc_id, { 0.0, 0.5 });

    RenderColorRectWithAlphaCheckerboard(window->DrawList, ImVec2(rect.Max.x - rect_size.x, rect.Min.y + (rect_correct_size - box_size) / 2), rect.Max - ImVec2(0, (rect_correct_size - box_size) / 2), gui->get_clr(col_rgb), ImMin(20, 20) / 2.99f, ImVec2(0.f, 0.f), set->color_rounding);

    return pressed;
}

bool c_widgets::color_edit(const char* label, float col[4], ImGuiColorEditFlags flags, bool popup)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const float square_sz = GetFrameHeight();
    const float w_full = CalcItemWidth();
    const float w_button = (flags & ImGuiColorEditFlags_NoSmallPreview) ? 0.0f : (square_sz + style.ItemInnerSpacing.x);
    const float w_inputs = w_full - w_button;
    const char* label_display_end = FindRenderedTextEnd(label);
    (void)label_display_end;
    g.NextItemData.ClearFlags();

    BeginGroup();
    PushID(label);

    const ImGuiColorEditFlags flags_untouched = flags;
    if (flags & ImGuiColorEditFlags_NoInputs) flags = (flags & (~ImGuiColorEditFlags_DisplayMask_)) | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_NoOptions;

    if (!(flags & ImGuiColorEditFlags_NoOptions)) ColorEditOptionsPopup(col, flags);

    if (!(flags & ImGuiColorEditFlags_DisplayMask_)) flags |= (g.ColorEditOptions & ImGuiColorEditFlags_DisplayMask_);
    if (!(flags & ImGuiColorEditFlags_DataTypeMask_)) flags |= (g.ColorEditOptions & ImGuiColorEditFlags_DataTypeMask_);
    if (!(flags & ImGuiColorEditFlags_PickerMask_)) flags |= (g.ColorEditOptions & ImGuiColorEditFlags_PickerMask_);
    if (!(flags & ImGuiColorEditFlags_InputMask_)) flags |= (g.ColorEditOptions & ImGuiColorEditFlags_InputMask_);

    flags |= (g.ColorEditOptions & ~(ImGuiColorEditFlags_DisplayMask_ | ImGuiColorEditFlags_DataTypeMask_ | ImGuiColorEditFlags_PickerMask_ | ImGuiColorEditFlags_InputMask_));

    IM_ASSERT(ImIsPowerOfTwo(flags & ImGuiColorEditFlags_DisplayMask_));
    IM_ASSERT(ImIsPowerOfTwo(flags & ImGuiColorEditFlags_InputMask_));

    const bool alpha = (flags & ImGuiColorEditFlags_NoAlpha) == 0;
    const bool hdr = (flags & ImGuiColorEditFlags_HDR) != 0;
    const int components = alpha ? 4 : 3;

    float f[4] = { col[0], col[1], col[2], alpha ? col[3] : 1.0f };
    if ((flags & ImGuiColorEditFlags_InputHSV) && (flags & ImGuiColorEditFlags_DisplayRGB))
        ColorConvertHSVtoRGB(f[0], f[1], f[2], f[0], f[1], f[2]);
    else if ((flags & ImGuiColorEditFlags_InputRGB) && (flags & ImGuiColorEditFlags_DisplayHSV))
    {
        ColorConvertRGBtoHSV(f[0], f[1], f[2], f[0], f[1], f[2]);
        color_edit_restore_hs(col, &f[0], &f[1], &f[2]);
    }
    int i[4] = { IM_F32_TO_INT8_UNBOUND(f[0]), IM_F32_TO_INT8_UNBOUND(f[1]), IM_F32_TO_INT8_UNBOUND(f[2]), IM_F32_TO_INT8_UNBOUND(f[3]) };

    bool value_changed = false;
    bool value_changed_as_float = false;

    (void)w_full;
    (void)w_button;
    (void)w_inputs;
    (void)square_sz;
    (void)style;
    (void)components;
    (void)hdr;
    (void)popup;

    const MenuTheme::Palette& tokens = MenuTheme::Colors();
    const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
    const float uiScale = leyawin_ui_scale();
    const float width = ImMax(GetContentRegionAvail().x, 1.f);
    const float rowH = metrics.rowHeight * uiScale;
    const float pad = metrics.space2 * uiScale;
    const ImVec2 pos = window->DC.CursorPos;
    const ImRect row(pos, pos + ImVec2(width, rowH));
    ItemSize(row, 0);

    const float swatchD = 16.f * uiScale;
    const float hexW = 64.f * uiScale;
    const float hexH = 22.f * uiScale;
    const float swatchX = width - pad - swatchD;
    const ImRect swatch(
        pos + ImVec2(swatchX, (rowH - swatchD) * 0.5f),
        pos + ImVec2(swatchX + swatchD, (rowH + swatchD) * 0.5f));
    const float hexX = swatchX - pad * 1.5f - hexW;
    const ImRect hexRect(
        pos + ImVec2(hexX, (rowH - hexH) * 0.5f),
        pos + ImVec2(hexX + hexW, (rowH + hexH) * 0.5f));

    ImGuiWindow* picker_active_window = NULL;
    if (!(flags & ImGuiColorEditFlags_NoSmallPreview))
    {
        char key_popup[128];
        wsprintfA(key_popup, "%skeywindow", label);
        SetCursorScreenPos(swatch.Min);
        if (InvisibleButton("##swatch", swatch.GetSize()))
            open_popup_w(key_popup);

        char hex[16]{};
        sprintf_s(
            hex,
            "#%02X%02X%02X",
            ImClamp(i[0], 0, 255),
            ImClamp(i[1], 0, 255),
            ImClamp(i[2], 0, 255));
        SetCursorScreenPos(hexRect.Min);
        PushStyleVar(
            ImGuiStyleVar_FrameRounding,
            metrics.radiusSmall * uiScale);
        PushStyleVar(
            ImGuiStyleVar_FramePadding,
            ImVec2(6.f * uiScale, 3.f * uiScale));
        PushStyleColor(ImGuiCol_FrameBg, tokens.raised);
        SetNextItemWidth(hexW);
        if (InputText(
                "##hex",
                hex,
                IM_ARRAYSIZE(hex),
                ImGuiInputTextFlags_CharsHexadecimal |
                    ImGuiInputTextFlags_CharsUppercase))
        {
            const char* p = hex[0] == '#' ? hex + 1 : hex;
            unsigned int packed = 0;
            if (sscanf_s(p, "%x", &packed) == 1)
            {
                if (strlen(p) <= 4)
                {
                    i[0] = ((packed >> 8) & 0xF) * 17;
                    i[1] = ((packed >> 4) & 0xF) * 17;
                    i[2] = (packed & 0xF) * 17;
                }
                else
                {
                    i[0] = (packed >> 16) & 0xFF;
                    i[1] = (packed >> 8) & 0xFF;
                    i[2] = packed & 0xFF;
                }
                value_changed = true;
                value_changed_as_float = false;
            }
        }
        PopStyleColor();
        PopStyleVar(2);

        // Swatch goes last: the input frame is drawn after the widget above it
        // and used to paint over the circle.
        window->DrawList->AddCircleFilled(
            swatch.GetCenter(),
            swatchD * 0.5f,
            gui->get_clr(
                ImVec4(col[0], col[1], col[2], alpha ? col[3] : 1.f),
                1.f),
            20);
        window->DrawList->AddCircle(
            swatch.GetCenter(),
            swatchD * 0.5f,
            gui->get_clr(tokens.borderStrong, 1.f),
            20,
            metrics.stroke);

        gui->render_text_clipped(
            set->poppins_widget,
            row.Min + ImVec2(pad, 0.f),
            ImVec2(hexRect.Min.x - pad, row.Max.y),
            gui->get_clr(tokens.textPrimary, 1.f),
            label,
            {0.0f, 0.5f});

        if (gui->begin_popup_w(key_popup, 210, -1)) {
            picker_active_window = g.CurrentWindow;

            ImGuiColorEditFlags picker_flags_to_forward = ImGuiColorEditFlags_DataTypeMask_ | ImGuiColorEditFlags_PickerMask_ | ImGuiColorEditFlags_InputMask_ | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_AlphaBar;
            // The alpha bar is always offered; it is ignored for colours that
            // declared NoAlpha.
            ImGuiColorEditFlags picker_flags = (flags_untouched & picker_flags_to_forward) | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaPreviewHalf;
            SetNextItemWidth(18.f * 12.0f);
            value_changed |= color_picker("##picker", col, picker_flags, &g.ColorPickerRef.x);
            gui->end_popup_w();
        }
    }

    if (value_changed && picker_active_window == NULL)
    {
        if (!value_changed_as_float)
            for (int n = 0; n < 4; n++)
                f[n] = i[n] / 255.0f;
        if ((flags & ImGuiColorEditFlags_DisplayHSV) && (flags & ImGuiColorEditFlags_InputRGB))
        {
            g.ColorEditSavedHue = f[0];
            g.ColorEditSavedSat = f[1];
            ColorConvertHSVtoRGB(f[0], f[1], f[2], f[0], f[1], f[2]);
            g.ColorEditSavedColor = ColorConvertFloat4ToU32(ImVec4(f[0], f[1], f[2], 0));
        }
        if ((flags & ImGuiColorEditFlags_DisplayRGB) && (flags & ImGuiColorEditFlags_InputHSV))
            ColorConvertRGBtoHSV(f[0], f[1], f[2], f[0], f[1], f[2]);

        col[0] = f[0];
        col[1] = f[1];
        col[2] = f[2];
        if (alpha)
            col[3] = f[3];
    }

    PopID();
    EndGroup();

    if (picker_active_window && g.ActiveId != 0 && g.ActiveIdWindow == picker_active_window) g.LastItemData.ID = g.ActiveId;

    if (value_changed && g.LastItemData.ID != 0)  MarkItemEdited(g.LastItemData.ID);

    return value_changed;
}

bool c_widgets::button(const char* label, const ImVec2& size_arg)
{
    struct button_state
    {
        float hover = 0.f;
        float press = 0.f;
    };

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiID id = window->GetID(label);
    const MenuTheme::Palette& tokens = MenuTheme::Colors();
    const MenuTheme::Metrics& metrics = MenuTheme::Sizes();

    const ImVec2 pos = window->DC.CursorPos;
    const float uiScale = leyawin_ui_scale();
    ImVec2 size = size_arg;
    if (size.x <= 0.f)
        size.x = GetContentRegionAvail().x;
    if (size.y <= 0.f)
        size.y = metrics.controlHeight * uiScale;
    size.y = ImMax(size.y, ImGui::GetTextLineHeight() + metrics.space2 * uiScale);

    const ImRect rect(pos, pos + size);
    ItemSize(rect, 0);
    if (!ItemAdd(rect, id)) return false;

    bool hovered, held, pressed = ButtonBehavior(rect, id, &hovered, &held, 0);

    auto& anim = LEYAWIN_ANIM(ImGuiID, button_state);
    button_state& state = MapGet(anim, id);
    state.hover = MenuMotion::Approach(
        state.hover, hovered ? 1.f : 0.f, MenuMotion::kHover);
    state.press = MenuMotion::Approach(
        state.press, held ? 1.f : 0.f, MenuMotion::kPress);

    // Secondary button: quiet surface, accent only on the border while hot.
    const ImVec4 fill = ImLerp(
        ImLerp(tokens.raised, tokens.hover, state.hover),
        tokens.pressed,
        state.press);
    const float radius = metrics.radiusControl * uiScale;
    window->DrawList->AddRectFilled(
        rect.Min, rect.Max, gui->get_clr(fill, 1.f), radius);
    window->DrawList->AddRect(
        rect.Min,
        rect.Max,
        gui->get_clr(
            ImLerp(tokens.borderSubtle, tokens.accent.fill, state.hover),
            1.f),
        radius,
        0,
        metrics.stroke);
    gui->render_text_clipped(
        set->poppins_widget,
        rect.Min,
        rect.Max,
        gui->get_clr(
            ImLerp(tokens.textMuted, tokens.textPrimary, state.hover),
            1.f),
        label,
        {0.5, 0.5});

    return pressed;
}

static float calc_combo_size(int items_count, float item_size)
{
    ImGuiContext& g = *GImGui;
    if (items_count <= 0)
        return FLT_MAX;
    return item_size * items_count + g.Style.ItemSpacing.y * (items_count - 1);
}

bool c_widgets::selectable_ex(const char* label, bool active)
{
    struct selectable_state
    {
        float alpha = 0.f, offset = 0.f;
        ImVec4 rect = clr->element_background, text = clr->text_active;
    };

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    const float uiScale = leyawin_ui_scale();
    const float width =
        GetContentRegionMax().x -
        2.f * set->combo_spacing * uiScale;
    const ImVec2 pos = window->DC.CursorPos;
    const float rowHeight = 31.f * uiScale;
    const ImRect rect(pos, pos + ImVec2(width, rowHeight));
    ItemSize(rect, 0);
    if (!ItemAdd(rect, id)) return false;

    bool hovered = IsItemHovered(), pressed = hovered && g.IO.MouseClicked[0];

    if (pressed) MarkItemEdited(id);

    auto& anim = LEYAWIN_ANIM(ImGuiID, selectable_state);
    selectable_state& state = MapGet(anim, id);

    state.alpha = ImClamp(state.alpha + (gui->fixed_speed(6.f) * (active ? 1.f : -1.f)), 0.f, 1.f);
    state.rect = ImLerp(state.rect, active ? clr->accent_color : clr->text, gui->fixed_speed(12.f));
    state.text = ImLerp(state.text, active ? clr->accent_color : clr->text_active, gui->fixed_speed(12.f));
    state.offset = ImLerp(
        state.offset,
        (active ? 7.f : 9.f) * uiScale,
        gui->fixed_speed(12.f));

    window->DrawList->AddRectFilled(
        rect.Max - ImVec2(
            5.f * uiScale,
            rowHeight - state.offset),
        rect.Max - ImVec2(0.f, state.offset),
        gui->get_clr(state.rect),
        30.f * uiScale);
    gui->render_text_clipped(set->poppins_widget, rect.Min, rect.Max, gui->get_clr(state.text), label, { 0.0, 0.5 });

    return pressed;
}

bool selectable(const char* label, bool* p_selected)
{
    if (gui->selectable_ex(label, *p_selected))
    {
        *p_selected = !*p_selected;
        return true;
    }
    return false;
}

bool begin_combo(const char* label, const char* preview_value, int val, int max_count, ImGuiComboFlags flags, bool multi, const char* icon = nullptr)
{
    struct combo_state
    {
        ImVec4 text = clr->text;
        bool combo_opened = false, hovered = false;
        float alpha = 0.f, offset = 0.f;
        float hot = 0.f, chevron = 0.f;
    };

    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = GetCurrentWindow();

    ImGuiNextWindowDataFlags backup_next_window_data_flags = g.NextWindowData.Flags;
    g.NextWindowData.ClearFlags();
    if (window->SkipItems) return false;

    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    IM_ASSERT((flags & (ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_NoPreview)) != (ImGuiComboFlags_NoArrowButton | ImGuiComboFlags_NoPreview)); // Can't use both flags together
    if (flags & ImGuiComboFlags_WidthFitPreview) IM_ASSERT((flags & (ImGuiComboFlags_NoPreview | ImGuiComboFlags_CustomPreview)) == 0);

    if (val > max_count) val = max_count;
    const ImVec2 pos = window->DC.CursorPos;
    const float uiScale = leyawin_ui_scale();
    const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
    const float width = ImMax(GetContentRegionAvail().x, 1.f);
    const float rowHeight = metrics.rowHeight * uiScale;
    const float comboHeight = 28.f * uiScale;
    const float openWidth = ImMin(width, 148.f * uiScale);
    const ImRect rect(
        pos,
        pos + ImVec2(width, rowHeight));

    const ImRect combo(
        pos + ImVec2(0.f, (rowHeight - comboHeight) * 0.5f),
        pos + ImVec2(width, (rowHeight + comboHeight) * 0.5f));
    const ImRect open_combo(
        pos + ImVec2(width - openWidth, (rowHeight - comboHeight) * 0.5f),
        pos + ImVec2(width, (rowHeight + comboHeight) * 0.5f));

    ItemSize(rect, 0.f);
    if (!ItemAdd(rect, id)) return false;

    bool hovered, held, pressed = ButtonBehavior(rect, id, &hovered, &held);

    auto& anim = LEYAWIN_ANIM(ImGuiID, combo_state);
    combo_state& state = MapGet(anim, id);

    if (hovered && g.IO.MouseClicked[0] || state.combo_opened && g.IO.MouseClicked[0] && !state.hovered) state.combo_opened = !state.combo_opened;

    state.text = ImLerp(state.text, state.combo_opened ? clr->text_active : clr->text, gui->fixed_speed(12.f));
    state.alpha = ImClamp(state.alpha + (gui->fixed_speed(8.f) * (state.combo_opened ? 1.f : -1.f)), 0.f, 1.f);
    state.offset = ImLerp(
        state.offset,
        state.combo_opened ? 18.f * uiScale : 0.f,
        gui->fixed_speed(12.f));

    if (!IsRectVisible(
            rect.Min,
            rect.Max + ImVec2(0.f, 2.f * uiScale)))
    {
        state.combo_opened = false;
        state.alpha = 0.f;
    }

    const MenuTheme::Palette& tokens = MenuTheme::Colors();
    const float pad = metrics.space2 * uiScale;
    const float iconSz = metrics.iconRow * uiScale;

    state.hot = MenuMotion::Approach(
        state.hot,
        (hovered || state.combo_opened) ? 1.f : 0.f,
        MenuMotion::kHover);
    state.chevron = MenuMotion::Approach(
        state.chevron,
        state.combo_opened ? 1.f : 0.f,
        MenuMotion::kSelection);

    // Only the box itself reacts, never the whole row.
    window->DrawList->AddRectFilled(
        open_combo.Min,
        open_combo.Max,
        gui->get_clr(tokens.raised, 1.f),
        metrics.radiusControl * uiScale);
    window->DrawList->AddRect(
        open_combo.Min,
        open_combo.Max,
        gui->get_clr(
            ImLerp(tokens.borderSubtle, tokens.accent.fill, state.hot),
            1.f),
        metrics.radiusControl * uiScale,
        0,
        metrics.stroke);

    const float chevronBox = 12.f * uiScale;
    gui->render_text_clipped(
        set->poppins_widget,
        open_combo.Min + ImVec2(pad, 0.f),
        ImVec2(
            open_combo.Max.x - pad - chevronBox - metrics.space1 * uiScale,
            open_combo.Max.y),
        gui->get_clr(
            ImLerp(tokens.textMuted, tokens.textPrimary, state.hot),
            1.f),
        preview_value,
        {0.0f, 0.5f});

    // Chevron is drawn, not glyphed: the icon font mapping used to fall back
    // to an unrelated symbol whenever the atlas changed.
    {
        const ImVec2 c(
            rect.Max.x - pad - chevronBox * 0.5f,
            rect.Min.y + rowHeight * 0.5f);
        const float w = chevronBox * 0.32f;
        const float h = chevronBox * 0.19f;
        const float flip = 1.f - 2.f * state.chevron;
        const ImU32 col = gui->get_clr(
            ImLerp(tokens.textFaint, tokens.accent.active, state.hot),
            1.f);
        window->DrawList->PathClear();
        window->DrawList->PathLineTo(ImVec2(c.x - w, c.y - h * flip));
        window->DrawList->PathLineTo(ImVec2(c.x, c.y + h * flip));
        window->DrawList->PathLineTo(ImVec2(c.x + w, c.y - h * flip));
        window->DrawList->PathStroke(col, 0, 1.4f * uiScale);
    }

    ImVec2 labelMin = rect.Min + ImVec2(pad, 0.f);
    if (icon && icon[0])
    {
        ImFont* iconFont =
            set->icon ? set->icon : ImGui::GetFont();
        const ImVec2 iconTextSize =
            iconFont->CalcTextSizeA(iconSz, FLT_MAX, 0.f, icon);
        window->DrawList->AddText(
            iconFont,
            iconSz,
            ImVec2(
                rect.Min.x + pad + (iconSz - iconTextSize.x) * 0.5f,
                rect.Min.y + (rowHeight - iconTextSize.y) * 0.5f),
            gui->get_clr(tokens.textFaint, 1.f),
            icon);
        labelMin.x += iconSz + metrics.space2 * uiScale;
    }
    gui->render_text_clipped(
        set->poppins_widget,
        labelMin,
        ImVec2(open_combo.Min.x - pad, rect.Max.y),
        gui->get_clr(
            ImLerp(tokens.textMuted, tokens.textPrimary, state.hot),
            1.f),
        label,
        {0.0f, 0.5f});

    if (!state.combo_opened && state.alpha < 0.1f) return false;

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing;

    PushStyleVar(ImGuiStyleVar_Alpha, GetStyle().Alpha * state.alpha);
    PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(5.f * uiScale, 0.f));
    PushStyleVar(
        ImGuiStyleVar_WindowRounding,
        set->combo_rounding * uiScale);
    PushStyleColor(ImGuiCol_WindowBg, gui->get_clr(clr->combo_color));
    const float popup_height =
        calc_combo_size(val, 31.f * uiScale);
    ImVec2 popup_position =
        open_combo.GetBL() + ImVec2(0.f, 4.f * uiScale);
    if (popup_position.y + popup_height >
        g.IO.DisplaySize.y - 8.f * uiScale)
    {
        popup_position.y =
            open_combo.Min.y - popup_height - 4.f * uiScale;
    }
    SetNextWindowPos(popup_position);
    SetNextWindowSize(
        ImVec2(open_combo.GetWidth(), popup_height));
    if (state.combo_opened)
        SetNextWindowFocus();
    char popup_name[64]{};
    wsprintfA(
        popup_name,
        "%u_combo_popup",
        static_cast<unsigned>(id));
    Begin(popup_name, NULL, window_flags);
    {
        if (state.combo_opened)
            BringWindowToDisplayFront(GetCurrentWindow());
        SetCursorPosX(
            GetCursorPosX() +
            set->combo_spacing * uiScale);
        BeginGroup();
        state.hovered = IsWindowHovered();

        if (!multi)
            if (IsWindowHovered() && g.IO.MouseClicked[0]) state.combo_opened = false;
    }
    return true;
}

void end_combo()
{
    EndGroup();
    PopStyleColor();
    PopStyleVar(4);
    ImGui::End();
}

bool combo(const char* label, int* current_item, const char* (*getter)(void* user_data, int idx), void* user_data, int items_count, int popup_max_height_in_items, int max_count)
{
    ImGuiContext& g = *GImGui;
    const float uiScale = leyawin_ui_scale();

    const char* preview_value = NULL;
    if (*current_item >= 0 && *current_item < items_count) preview_value = getter(user_data, *current_item);

    if (popup_max_height_in_items != -1 &&
        !(g.NextWindowData.Flags &
            ImGuiNextWindowDataFlags_HasSizeConstraint))
    {
        SetNextWindowSizeConstraints(
            ImVec2(0, 0),
            ImVec2(
                FLT_MAX,
                calc_combo_size(
                    popup_max_height_in_items,
                    31.f * uiScale)));
    }

    if (!begin_combo(label, preview_value, items_count, max_count, ImGuiComboFlags_None, false)) return false;

    bool value_changed = false;
    for (int i = 0; i < items_count; i++)
    {
        const char* item_text = getter(user_data, i);
        if (item_text == NULL) item_text = "*Unknown item*";

        PushID(i);
        const bool item_selected = (i == *current_item);
        if (gui->selectable_ex(item_text, item_selected) && *current_item != i)
        {
            value_changed = true;
            *current_item = i;
        }
        if (item_selected) SetItemDefaultFocus();
        PopID();
    }

    end_combo();

    if (value_changed) MarkItemEdited(g.LastItemData.ID);

    return value_changed;
}

bool c_widgets::dropdown(const char* label, int* current_item, const char* const items[], int items_count, int max_count)
{
    const bool value_changed = combo(label, current_item, items_array_getter, (void*)items, items_count, -1, max_count);
    return value_changed;
}

void c_widgets::multi_dropdown(const char* label, bool variable[], const char* labels[], int count, int max_count)
{
    ImGuiContext& g = *GImGui;

    char preview[256] = "Select";
    int preview_len = 0;

    for (auto i = 0, j = 0; i < count; i++)
    {
        if (variable[i])
        {
            if (j) {
                lstrcatA(preview, ", ");
            } else {
                preview[0] = '\0';
            }
            lstrcatA(preview, labels[i]);
            j++;
        }
    }

    if (begin_combo(label, preview, count, max_count, 0, true))
    {
        for (auto i = 0; i < count; i++)
            selectable(labels[i], &variable[i]);
        end_combo();
    }
}

void c_widgets::multi_dropdown_icon(
    const char* icon,
    const char* label,
    bool variable[],
    const char* labels[],
    int count,
    int max_count)
{
    char preview[256] = "Select";
    for (int i = 0, selected = 0; i < count; ++i)
    {
        if (!variable[i])
            continue;
        if (selected)
            lstrcatA(preview, ", ");
        else
            preview[0] = '\0';
        lstrcatA(preview, labels[i]);
        ++selected;
    }

    if (begin_combo(
            label,
            preview,
            count,
            max_count,
            0,
            true,
            icon))
    {
        for (int i = 0; i < count; ++i)
            selectable(labels[i], &variable[i]);
        end_combo();
    }
}

const char* keys[] =
{
    "-",
    "Mouse 1",
    "Mouse 2",
    "CN",
    "Mouse 3",
    "Mouse 4",
    "Mouse 5",
    "-",
    "Back",
    "Tab",
    "-",
    "-",
    "CLR",
    "Enter",
    "-",
    "-",
    "Shift",
    "CTL",
    "Menu",
    "Pause",
    "Caps Lock",
    "KAN",
    "-",
    "JUN",
    "FIN",
    "KAN",
    "-",
    "Escape",
    "CON",
    "NCO",
    "ACC",
    "MAD",
    "Space",
    "PGU",
    "PGD",
    "End",
    "Home",
    "Left",
    "Up",
    "Right",
    "Down",
    "SEL",
    "PRI",
    "EXE",
    "PRI",
    "INS",
    "Delete",
    "HEL",
    "0",
    "1",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
    "8",
    "9",
    "-",
    "-",
    "-",
    "-",
    "-",
    "-",
    "-",
    "A",
    "B",
    "C",
    "D",
    "E",
    "F",
    "G",
    "H",
    "I",
    "J",
    "K",
    "L",
    "M",
    "N",
    "O",
    "P",
    "Q",
    "R",
    "S",
    "T",
    "U",
    "V",
    "W",
    "X",
    "Y",
    "Z",
    "WIN",
    "WIN",
    "APP",
    "-",
    "SLE",
    "Numpad 0",
    "Numpad 1",
    "Numpad 2",
    "Numpad 3",
    "Numpad 4",
    "Numpad 5",
    "Numpad 6",
    "Numpad 7",
    "Numpad 8",
    "Numpad 9",
    "MUL",
    "ADD",
    "SEP",
    "MIN",
    "Delete",
    "DIV",
    "F1",
    "F2",
    "F3",
    "F4",
    "F5",
    "F6",
    "F7",
    "F8",
    "F9",
    "F10",
    "F11",
    "F12",
    "F13",
    "F14",
    "F15",
    "F16",
    "F17",
    "F18",
    "F19",
    "F20",
    "F21",
    "F22",
    "F23",
    "F24",
    "-",
    "-",
    "-",
    "-",
    "-",
    "-",
    "-",
    "-",
    "NUM",
    "SCR",
    "EQU",
    "MAS",
    "TOY",
    "OYA",
    "OYA",
    "-",
    "-",
    "-",
    "-",
    "-",
    "-",
    "-",
    "-",
    "-",
    "Shift",
    "Shift",
    "Ctrl",
    "Ctrl",
    "Alt",
    "Alt"
};

static const char* key_label(int vk) noexcept
{
    if (vk <= 0 || vk >= IM_ARRAYSIZE(keys))
        return nullptr;
    const char* name = keys[vk];
    if (!name || !name[0] || (name[0] == '-' && !name[1]))
        return nullptr;
    return name;
}

const char* c_widgets::key_name(int vk)
{
    return key_label(vk);
}

bool c_widgets::keybind(const char* label, int* key, const char* icon)
{
    struct keybind_state
    {
        float slow = 0.f;
        float hot = 0.f;
        float capture = 0.f;
        float initialized = 0.f;
    };

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    ImGuiIO& io = g.IO;
    const MenuTheme::Palette& tokens = MenuTheme::Colors();
    const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
    const float uiScale = leyawin_ui_scale();

    const ImGuiID id = window->GetID(label);

    const ImVec2 pos = window->DC.CursorPos;
    const float width = ImMax(GetContentRegionAvail().x, 1.f);
    const float rowH = metrics.rowHeight * uiScale;
    const float pad = metrics.space2 * uiScale;
    const float iconSz = metrics.iconRow * uiScale;
    const ImRect rect(pos, pos + ImVec2(width, rowH));

    ImGui::ItemSize(rect, 0.f);
    if (!ImGui::ItemAdd(rect, id, &rect))  return false;

    char buf_display[64] = "None";

    bool value_changed = false;
    int k = *key;

    if (*key != 0 && g.ActiveId != id) {
        lstrcpynA(buf_display, keys[*key], (int)sizeof(buf_display));
    }
    else if (g.ActiveId == id) {
        strcpy_s(buf_display, "Press key...");
    }

    bool hovered = ItemHoverable(rect, id, 0);

    if (hovered && io.MouseClicked[0])
    {
        if (g.ActiveId != id) {
            memset(io.MouseDown, 0, sizeof(io.MouseDown));
            memset(io.KeysDown, 0, sizeof(io.KeysDown));
            *key = 0;
        }
        ImGui::SetActiveID(id, window);
        ImGui::FocusWindow(window);
    }
    else if (io.MouseClicked[0]) {

        if (g.ActiveId == id) ImGui::ClearActiveID();
    }

    if (g.ActiveId == id) {
        for (auto i = 0; i < 5; i++) {
            if (io.MouseDown[i]) {
                switch (i) {
                case 0:
                    k = 0x01;
                    break;
                case 1:
                    k = 0x02;
                    break;
                case 2:
                    k = 0x04;
                    break;
                case 3:
                    k = 0x05;
                    break;
                case 4:
                    k = 0x06;
                    break;
                }
                value_changed = true;
                ImGui::ClearActiveID();
            }
        }
        if (!value_changed) {
            for (auto i = 0x08; i <= 0xA5; i++) {
                if (io.KeysDown[i]) {
                    k = i;
                    value_changed = true;
                    ImGui::ClearActiveID();
                }
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            *key = 0;
            ImGui::ClearActiveID();
        }
        else {
            *key = k;
        }
    }

    auto& anim = LEYAWIN_ANIM(ImGuiID, keybind_state);
    keybind_state& state = MapGet(anim, id);

    const bool capturing = g.ActiveId == id;

    // The badge hugs its text, so it slides as the key name changes width.
    ImFont* monoFont = MenuType::Mono();
    const float monoSize = MenuType::MonoSize();
    const float valueW = monoFont
        ? monoFont->CalcTextSizeA(monoSize, FLT_MAX, 0.f, buf_display).x
        : CalcTextSize(buf_display).x;
    const float badgeW = valueW + metrics.space3 * 2.f * uiScale;
    const float targetOffset = width - pad - badgeW;
    if (state.initialized < 0.5f)
    {
        state.slow = targetOffset;
        state.initialized = 1.f;
    }
    else
    {
        state.slow = MenuMotion::Approach(
            state.slow, targetOffset, MenuMotion::kSelection);
    }
    state.hot = MenuMotion::Approach(
        state.hot, hovered ? 1.f : 0.f, MenuMotion::kHover);
    state.capture = MenuMotion::Approach(
        state.capture, capturing ? 1.f : 0.f, MenuMotion::kSelection);

    const ImRect badge(
        ImVec2(
            rect.Min.x + state.slow,
            rect.Min.y + 3.f * uiScale),
        ImVec2(
            rect.Min.x + state.slow + badgeW,
            rect.Max.y - 3.f * uiScale));
    window->DrawList->AddRectFilled(
        badge.Min,
        badge.Max,
        gui->get_clr(tokens.raised, 1.f),
        metrics.radiusControl * uiScale);
    window->DrawList->AddRect(
        badge.Min,
        badge.Max,
        gui->get_clr(
            ImLerp(
                tokens.borderStrong,
                tokens.accent.fill,
                ImMax(state.capture, state.hot)),
            1.f),
        metrics.radiusControl * uiScale,
        0,
        metrics.stroke);

    ImVec2 labelMin = rect.Min + ImVec2(pad, 0.f);
    if (icon && icon[0])
    {
        ImFont* iconFont =
            set->icon ? set->icon : ImGui::GetFont();
        const ImVec2 iconTextSize =
            iconFont->CalcTextSizeA(iconSz, FLT_MAX, 0.f, icon);
        window->DrawList->AddText(
            iconFont,
            iconSz,
            ImVec2(
                rect.Min.x + pad + (iconSz - iconTextSize.x) * 0.5f,
                rect.Min.y + (rowH - iconTextSize.y) * 0.5f),
            gui->get_clr(tokens.textFaint, 1.f),
            icon);
        labelMin.x += iconSz + metrics.space2 * uiScale;
    }

    if (monoFont)
    {
        window->DrawList->AddText(
            monoFont,
            monoSize,
            ImVec2(
                badge.Min.x + (badgeW - valueW) * 0.5f,
                badge.Min.y + (badge.GetHeight() - monoSize) * 0.5f),
            gui->get_clr(
                capturing ? tokens.accent.brand : tokens.textPrimary, 1.f),
            buf_display);
    }
    gui->render_text_clipped(
        set->poppins_widget,
        labelMin,
        ImVec2(badge.Min.x - metrics.space2 * uiScale, rect.Max.y),
        gui->get_clr(
            ImLerp(tokens.textMuted, tokens.textPrimary, state.hot), 1.f),
        label,
        {0.0f, 0.5f});

    return value_changed;
}

int c_widgets::config_panel(const char* name, const char* modified, const char* author, int config_id, int& config_variable, const ImVec2& size)
{
    IM_UNUSED(author);

    struct config_state
    {
        ImVec4 background = clr->combo_color;
    };

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems) return 0;

    const ImGuiID panelId = window->GetID("##config_panel");
    const ImVec2 pos = window->DC.CursorPos;
    const bool selected = config_id == config_variable;

    auto& anim = LEYAWIN_ANIM(ImGuiID, config_state);
    config_state& state = MapGet(anim, panelId);

    const ImRect rect(pos, pos + size);

    ItemSize(rect, 0);
    if (!ItemAdd(rect, panelId)) return 0;

    state.background = ImLerp(state.background, selected ? clr->accent_color : clr->element_background, fixed_speed(15.f));

    window->DrawList->AddRectFilled(rect.Min, rect.Max, get_clr(clr->combo_color), set->selection_rounding);
    if (selected) {
        window->DrawList->AddRect(
            rect.Min,
            rect.Max,
            get_clr(state.background),
            set->selection_rounding,
            0,
            1.5f);
    }

    gui->render_text_clipped(set->poppins_selection, rect.Min + ImVec2(10, 0), rect.Max, gui->get_clr(clr->text_active), name, { 0.0f, 0.2f });

    gui->render_text_clipped(set->poppins_selection, rect.Min + ImVec2(10, 0), rect.Max, gui->get_clr(clr->text_active), "Modified:", {0.0f, 0.8f});
    gui->render_text_clipped(set->poppins_selection, rect.Min + ImVec2(ImGui::CalcTextSize("Modified:").x + 15, 0), rect.Max, gui->get_clr(clr->accent_color), modified, { 0.0f, 0.8f });

    struct action_button
    {
        const char* id;
        const char* label;
        int action;
        float width;
    };
    static constexpr action_button buttons[] = {
        { "##config_save", "SAVE", 2, 64.f },
        { "##config_load", "LOAD", 1, 64.f },
        { "##config_delete", "DEL", 3, 54.f },
    };

    float buttonX = rect.Max.x - 10.f;
    int action = 0;
    for (int i = IM_ARRAYSIZE(buttons) - 1; i >= 0; --i) {
        const action_button& button = buttons[i];
        const ImRect buttonRect(
            ImVec2(buttonX - button.width, rect.Min.y + 10.f),
            ImVec2(buttonX, rect.Max.y - 10.f));
        buttonX = buttonRect.Min.x - 6.f;

        const ImGuiID buttonId = window->GetID(button.id);
        bool hovered = false;
        bool held = false;
        if (ItemAdd(buttonRect, buttonId) &&
            ButtonBehavior(
                buttonRect,
                buttonId,
                &hovered,
                &held)) {
            action = button.action;
            config_variable = config_id;
        }

        const ImVec4 targetColor =
            held
                ? clr->accent_color
                : hovered
                    ? clr->selection_active
                    : clr->element_background;
        window->DrawList->AddRectFilled(
            buttonRect.Min,
            buttonRect.Max,
            get_clr(targetColor),
            set->selection_rounding);
        gui->render_text_clipped(
            set->poppins_selection,
            buttonRect.Min,
            buttonRect.Max,
            gui->get_clr(clr->text_active),
            button.label,
            { 0.5f, 0.5f });
    }

    return action;
}
