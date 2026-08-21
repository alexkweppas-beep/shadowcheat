#include "imgui.h"

class c_colors
{
public:
    // Initialized by InitLeyawinGlobals() — ImColor() globals do not run under manual-map.
    ImVec4 white_color;
    ImVec4 black_color;
    ImVec4 accent_color;
    ImVec4 window_background;
    ImVec4 selection_active;
    ImVec4 child_top_bar;
    ImVec4 child_bottom_bar;
    ImVec4 child_stroke_bar;
    ImVec4 element_background;
    ImVec4 keybind_background;
    ImVec4 combo_color;
    ImVec4 separator;
    ImVec4 text_active;
    ImVec4 text;
};

// clr is defined in leyawin_globals.h
