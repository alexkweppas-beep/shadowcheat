#pragma once
#include "security/xorstr.hpp"
#include "includes.h"
#include "../headers/config.h"

#define IMGUI_DEFINE_MATH_OPERATORS

struct popup_data_t
{
    std::string name{};
    c_id owner_window_id{};
    bool open{ false };
    float alpha{ 0.f };
};

inline std::string get_key_name_vk(int vk)
{
    if (vk == 0) return xorstr_("None");
    if (vk == VK_LBUTTON) return xorstr_("Mouse 1");
    if (vk == VK_RBUTTON) return xorstr_("Mouse 2");
    if (vk == VK_MBUTTON) return xorstr_("Mouse 3");
    if (vk == VK_XBUTTON1) return xorstr_("Mouse 4");
    if (vk == VK_XBUTTON2) return xorstr_("Mouse 5");
    if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) return xorstr_("Shift");
    if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL) return xorstr_("Ctrl");
    if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU) return xorstr_("Alt");
    if (vk == VK_CAPITAL) return xorstr_("Caps Lock");
    if (vk == VK_SPACE) return xorstr_("Space");
    if (vk == VK_RETURN) return xorstr_("Enter");
    if (vk == VK_ESCAPE) return xorstr_("Escape");
    if (vk == VK_TAB) return xorstr_("Tab");
    if (vk == VK_INSERT) return xorstr_("Insert");
    if (vk == VK_DELETE) return xorstr_("Delete");
    if (vk == VK_HOME) return xorstr_("Home");
    if (vk == VK_END) return xorstr_("End");
    
    char name[128] = { 0 };
    UINT scanCode = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    int result = GetKeyNameTextA((scanCode << 16), name, sizeof(name));
    if (result > 0) return std::string(name);
    
    return xorstr_("Unknown");
}

inline std::string utf8_safe_truncate(const std::string& str, size_t max_chars)
{
    size_t i = 0;       
    size_t char_count = 0;

    while (i < str.size() && char_count < max_chars)
    {
        unsigned char c = static_cast<unsigned char>(str[i]);

        size_t char_size = 1;

        if ((c & 0x80) == 0x00)         char_size = 1; // ASCII
        else if ((c & 0xE0) == 0xC0)    char_size = 2; // 2 байта
        else if ((c & 0xF0) == 0xE0)    char_size = 3; // 3 байта
        else if ((c & 0xF8) == 0xF0)    char_size = 4; // 4 байта

        if (i + char_size > str.size())
            break;

        i += char_size;
        char_count++;
    }

    std::string result = str.substr(0, i);

    if (i < str.size())
        result += xorstr_("...");

    return result;
}

inline bool need_sep()
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    float cursor_y = window->DC.CursorPos.y - window->DC.CursorStartPos.y;
    float content_bottom_y = window->ContentSize.y;

    float avail = content_bottom_y - cursor_y;

    avail = ImMax(avail, 0.0f);

    return avail > 1.0f;
}

struct config_button_t {
    bool pressed, share, download, edit, del;
};

struct config_t {
    int idx;
    std::string name, date;
};

class c_widgets
{
public:
    std::vector<popup_data_t> popup_storage;
    std::vector<config_t> config_storage;
    int config_idx = 0;
    int keybind_idx = 0;
    void open_popup(std::string name);
    void close_popup(std::string name);
    bool is_popup_open(std::string name);
    bool popup(std::string name, c_vec2 size, c_vec2 pos = { 0, 0 }, c_vec2 padding = { 0, 0 }, c_vec2 spacing = { 0, 0 }, float rounding = 0.f, bool always_on_screen = false, window_flags flags = 0);
    void end_popup();
    void load_texture(std::string path, ImTextureID* img);
    bool tab_button(std::string name, std::string icon, bool is_active);
    bool subtab_section(std::string name, bool is_active, std::vector<subtab_t>* subtabs_list);
    void draw_segment(ImDrawList* draw_list, c_vec2 center, ImU32 col);
    void child(std::string icon, std::string name, c_vec2 padding = { -1, -1 });
    void end_child();
    bool button(std::string name);
    bool checkbox(std::string name);
    void seperator();
    bool slider(std::string name);
    bool dropdown(std::string name);
    bool multi_dropdown(std::string name);
    bool selectable(std::string name, bool is_selected);
    bool curve(std::string name, float* first_x, float* first_y, float* second_x, float* second_y);
    bool weapon_checkbox(std::string checkbox_data_name, std::string weapon_name, std::string class_name, ImTextureID weapon, c_vec2 weapon_image_size);
    void keybind(std::string name, c_rect rect, bool pressed, std::vector<keybind_t>* keybinds_list);
    void init_keybinds();
    void blackout();
    void weapon_info(std::string name, bool* open, std::string weapon_class, std::string weapon_name, ImTextureID weapon_img, c_vec2 weapon_img_size, std::string ammo, std::string scope, std::string barrel);
    config_button_t config_button(int idx, std::string name, std::string date, bool loaded);
    void config_info(std::string icon, std::string name, std::string info);
    void colorpicker(std::string name, c_vec4* color, bool alpha = false);
    bool text_field_ex(std::string_view label, std::string_view hint, char* buf, int buf_size, float text_padding, float line_padding, ImU32 text_col, bool centered, const ImRect& rect, bool* active, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* callback_user_data, bool user_clicked);
    bool search(std::string name);
    bool config_field(std::string name, char* buf, size_t buf_size);
    bool keybind_direct(std::string name, int* key);

    // ----------------------------------------------------------------
    // Direct binding helpers — wrap cfg:: variables without loveup config system
    // ----------------------------------------------------------------

    inline bool checkbox_direct(std::string name, bool* value)
    {
        extern bool checkbox_ex(std::string, bool*, bool, std::vector<keybind_t>*);
        checkbox_t* data = menu_cfg->fill<checkbox_t>(name);
        
        data->external_ptr = value;
        
        // Sync cheat variable to menu variable before rendering
        if (data->enabled != *value) {
            data->enabled = *value;
        }

        bool pressed = checkbox_ex(data->name, &data->enabled, data->keybinds_enabled, &data->keybinds_list);
        
        // Sync menu variable back to cheat variable (handles both clicks and keybind toggles)
        *value = data->enabled;
        
        return pressed;
    }

    inline bool slider_direct(std::string name, int* value, int vmin, int vmax, const char* fmt = xorstr_("%d"))
    {
        extern bool slider_ex(std::string, float*, float, float, std::string);
        float fval = (float)*value;
        bool changed = slider_ex(name, &fval, (float)vmin, (float)vmax, fmt);
        if (changed) *value = (int)fval;
        return changed;
    }

    inline bool slider_direct_f(std::string name, float* value, float vmin, float vmax, const char* fmt = xorstr_("%.1f"))
    {
        extern bool slider_ex(std::string, float*, float, float, std::string);
        return slider_ex(name, value, vmin, vmax, fmt);
    }

    inline bool dropdown_direct(std::string name, int* value, std::vector<std::string>& items)
    {
        extern bool dropdown_ex(std::string, std::string);
        extern bool selectable_ex(std::string, bool);
        
        bool changed = false;
        std::string preview = (items.empty() || *value < 0 || *value >= (int)items.size()) ? "" : items[*value];
        
        if (dropdown_ex(name, preview))
        {
            for (int i = 0; i < (int)items.size(); i++)
            {
                if (selectable_ex(items[i], *value == i))
                {
                    *value = i;
                    changed = true;
                }
            }
            end_popup();
        }
        if (changed) {
            close_popup(name + xorstr_("##popup"));
        }
        return changed;
    }
};

inline std::unique_ptr<c_widgets> widgets = std::make_unique<c_widgets>();

enum notify_type
{
    success = 0,
    warning = 1,
    error = 2
};

struct notify_state
{
    int notify_id;
    std::string text;
    notify_type type{ success };

    ImVec2 window_size{ 0, 0 };
    float notify_alpha{ 0 };
    bool active_notify{ true };
    float notify_timer{ 0 };
    float notify_pos{ 0 };
};

class c_notify
{
public:
    void setup_notify();

    void add_notify(std::string text, notify_type type);

private:
    ImVec2 render_notify(int cur_notify_value, float notify_alpha, float notify_percentage, float notify_pos, std::string text, notify_type type);

    float notify_time{ 15 };
    int notify_count{ 0 };

    float notify_spacing{ 20 };
    ImVec2 notify_padding{ 20, 20 };

    std::vector<notify_state> notifications;

};

inline std::unique_ptr<c_notify> notify = std::make_unique<c_notify>();
