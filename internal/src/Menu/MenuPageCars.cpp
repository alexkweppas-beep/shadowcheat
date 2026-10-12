#include "MenuPages.h"

#define IMGUI_DEFINE_MATH_OPERATORS

#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"
#include "Leyawin/leyawin_globals.h"
#include "Leyawin/leyawin_init.h"
#include "IconsFontAwesome6.h"
#include "MenuUiHelpers.h"
#include "MenuState.h"
#include "MenuTheme.h"
#include "../Features/MiscFeatures.h"
#include "../Features/VehicleMovement.h"

namespace Menu
{
    void RenderCarsPage()
    {
        MenuUi::EmptyState(
            "cars_empty",
            ICON_FA_WRENCH,
            T("Moved to Movement", "Перенесено в Движение", "Перенесено у Рух"),
            T("Vehicle settings are now in the Movement tab.", "Настройки машины теперь во вкладке Движение.", "Налаштування машини тепер у вкладці Рух."),
            220.f);
    }
}
