#pragma once
#include "security/xorstr.hpp"
#include <map>
#include <variant>
#include <array>
#include <vector>
#include <string>
#include <mutex>
#include <memory>
#include <imgui.h>

struct keybind_t
{
    int idx;
    int key;
    int mode;
};

struct checkbox_t
{
    std::string name;
    bool enabled = false;
    bool keybinds_enabled = true;
    std::vector<keybind_t> keybinds_list;
    bool is_weapon = false;
    
    keybind_t last_keybind = { 0, 0, 0 };
    bool* external_ptr = nullptr;
};

struct slider_t
{
    std::string name;
    float callback;
    float min;
    float max;
    std::string format = xorstr_("%.1f");
};

struct dropdown_t
{
    std::string name;
    int callback;
    std::vector<std::string> variants;
};

struct multi_dropdown_t
{
    std::string name;
    std::vector<bool> callback;
    std::vector<std::string> variants;
};

struct colorpicker_t
{
    std::string name;
    ImVec4 color = { 1.f, 1.f, 1.f, 1.f };
};

enum config_type
{
    checkbox_type,
    slider_type,
    dropdown_type,
    multi_dropdown_type,
    colorpicker_type
};

using config_variant = std::variant<checkbox_t, slider_t, dropdown_t, multi_dropdown_t, colorpicker_t>;

class c_config
{
public:

    void init_config();

    template <typename T>
    T& get(const std::string& name) { return std::get<T>(options[name]); }

    template <typename T>
    T* fill(const std::string& name)
    {
        auto it = options.find(name);
        if (it == options.end())
        {
            T option;
            if constexpr (std::is_same_v<T, checkbox_t>) {
                option.name = name;
                option.keybinds_enabled = true;
            }
            options[name] = option;
            order.push_back({ name, get_type<T>() });
            return std::get_if<T>(&options[name]);
        }

        return std::get_if<T>(&it->second);
    }

    std::vector<std::pair<std::string, int>> order;

private:

    template <typename T, typename... Args>
    void add_option(const std::string& name, Args&&... args)
    {
        T option{ name, std::forward<Args>(args)... };
        options[name] = option;
        order.push_back({ name, get_type<T>() });
    }

    template <typename T>
    config_type get_type()
    {
        if constexpr (std::is_same_v<T, checkbox_t>)
            return checkbox_type;
        else if constexpr (std::is_same_v<T, slider_t>)
            return slider_type;
        else if constexpr (std::is_same_v<T, dropdown_t>)
            return dropdown_type;
        else if constexpr (std::is_same_v<T, multi_dropdown_t>)
            return multi_dropdown_type;
        else if constexpr (std::is_same_v<T, colorpicker_t>)
            return colorpicker_type;
    }

    std::map<std::string, config_variant> options;
};

inline std::unique_ptr<c_config> menu_cfg = std::make_unique<c_config>();
