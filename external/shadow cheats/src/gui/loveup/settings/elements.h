#pragma once
#include "security/xorstr.hpp"
#include <string>
#include "imgui.h"

class c_elements
{
public:

    struct 
    {
        std::string name{ xorstr_("Astral Gui") };
        c_vec2 size{ 800, 500 };
        c_vec2 padding = { 0, 0 };
        float rounding{ 16 };
    } window;

    struct {
        c_vec2 size = { 190, 500 };
    } nav;

    struct {
        c_vec2 size = { 610, 500 };
        c_vec2 padding = { 10, 10 };
    } content;
    
    struct {
        float width;
        float rounding = 10;
        c_vec2 padding = { 10, 10 };
    } child;
};

inline std::unique_ptr<c_elements> elements = std::make_unique<c_elements>();
