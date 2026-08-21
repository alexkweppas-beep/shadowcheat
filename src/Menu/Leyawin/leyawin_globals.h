#pragma once

#include "custom_settings.h"
#include "custom_colors.h"

// Manual-mapped DLL: no dynamic global constructors — POD singletons only.
inline c_settings  g_LeyawinSettings;
inline c_function  g_LeyawinFunction;
inline c_colors    g_LeyawinColors;

inline c_settings* set = &g_LeyawinSettings;
inline c_function* func = &g_LeyawinFunction;
inline c_colors*   clr = &g_LeyawinColors;
