#pragma once

#include "leyawin_globals.h"
#include "../IconsFontAwesome6.h"

namespace leyawin_init_detail {

static const char* kSelectionIcons[] = {
    ICON_FA_CROSSHAIRS,
    ICON_FA_EYE,
    ICON_FA_CODE,
    ICON_FA_USER_GROUP,
    ICON_FA_CUBES,
    ICON_FA_TOOLBOX,
    ICON_FA_GEAR,
    ICON_FA_FLAG
};
static const char* kSelectionLabels[] = {
    "Combat", "Visuals", "Scripts", "Social",
    "Quant", "Misc", "Settings", "Ukraine"
};
static const char* kSubSelection[] = { "Thread", "Enemy", "Other", "Team" };
static const char* kConditionsList[] = { "Select", "List", "Status", "Test", "Bruh", "Struct", "Pack", "Hack" };
static const char* kVisibleList[] = { "Default", "Selection", "Inactive" };
static const char* kForceList[] = { "Select", "Type", "Count" };
static const char* kBodyList[] = { "Head", "Body", "Chest", "Neck", "Legs" };
static const char* kMarkList[] = { "Upper Left", "Upper Right", "Bottom Left", "Bottom Right" };

template<typename T, size_t N>
inline void CopyPtrTable(T (&dst)[N], const char* const (&src)[N])
{
    for (size_t i = 0; i < N; ++i)
        dst[i] = src[i];
}

} // namespace leyawin_init_detail

// Theme values now come from MenuTheme tokens; this stays as the call site
// used by older code paths.
void ApplyLeyawinTheme(bool light);

// Manual-mapped DLL: in-class default member initializers never run on globals.
// Call once before any Leyawin widget reads set/func/clr.
inline void InitLeyawinGlobals()
{
    static bool initialized = false;
    if (initialized)
        return;
    initialized = true;

    using namespace leyawin_init_detail;

    set->popup_flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    set->window_flags = ImGuiWindowFlags_NoDecoration;

    set->window_size = ImVec2(1160.f, 740.f);
    set->window_padding = ImVec2(0.f, 0.f);
    set->widgets_padding = ImVec2(10.f, 10.f);
    set->widgets_spacing = ImVec2(0.f, 10.f);
    set->child_spacing = ImVec2(18.f, 10.f);

    set->watermark = false;
    set->show_menu = true;
    set->particle_cursor = false;
    set->window_gradient = true;
    set->window_rounding = 12.f;
    set->window_border_size = 0.f;
    set->window_scrollbar_size = 8.f;
    set->progress = 50.f;
    set->particle_gravity = 500;
    set->particle_num = 10;
    set->particle_color[0] = 1.f;
    set->particle_color[1] = 1.f;
    set->particle_color[2] = 1.f;
    set->particle_color[3] = 1.f;
    set->selection_alpha = 1.f;
    set->menu_alpha = 1.f;
    set->child_rounding = 10.f;
    set->selection_rounding = 6.f;
    set->checkbox_rounding = 2.f;
    set->input_rounding = 3.f;
    set->color_rounding = 2.f;
    set->keybind_rounding = 2.f;
    set->watermark_rounding = 3.f;
    set->combo_rounding = 3.f;
    set->combo_spacing = 10.f;

    CopyPtrTable(set->selection_icons, kSelectionIcons);
    CopyPtrTable(set->selection_labels, kSelectionLabels);
    CopyPtrTable(set->sub_selection, kSubSelection);

    func->enable = true;
    func->auto_fire = true;
    func->anti_step = true;
    func->draw_fov = true;
    func->head_safety_if_lethal = true;
    func->auto_step = true;
    func->autofire = true;
    func->hide_shots = true;
    func->teleport_boots = true;
    func->body_set[0] = true;
    func->body_set[1] = true;
    func->body_set[2] = true;
    func->body_set[3] = true;
    func->body_set[4] = false;
    func->misses_walls = true;
    func->show_in_enable = true;
    func->show_in_fire = true;
    func->show_in_safety = true;
    func->show_in_modem = true;
    func->show_in_step = true;
    func->silent_count_size = 0.5f;
    func->backtracking = 100.f;
    func->visible = 50.f;
    func->auto_wall = 50.f;
    func->picker_checkbox[0] = 246.f / 255.f;
    func->picker_checkbox[1] = 160.f / 255.f;
    func->picker_checkbox[2] = 204.f / 255.f;
    func->picker_checkbox[3] = 1.f;
    func->accent_color[0] = 205.f / 255.f; // #CD81D4
    func->accent_color[1] = 129.f / 255.f;
    func->accent_color[2] = 212.f / 255.f;
    func->accent_color[3] = 1.f;
    func->after_kill = 500;
    func->max_misses = 100;
    func->visible_count = 1;
    func->force_count = 1;
    func->mark_count = 1;
    func->body_count = 1;
    func->hitchance = 50;

    CopyPtrTable(func->conditions_list, kConditionsList);
    CopyPtrTable(func->visible_list, kVisibleList);
    CopyPtrTable(func->force_list, kForceList);
    CopyPtrTable(func->body_list, kBodyList);
    CopyPtrTable(func->mark_list, kMarkList);

    ApplyLeyawinTheme(false);
}
