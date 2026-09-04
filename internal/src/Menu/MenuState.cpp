#include "MenuState.h"

#define IMGUI_DEFINE_MATH_OPERATORS

#include "../imgui/imgui.h"

namespace Menu
{
    MainPage s_MainPage = MainPage::Combat;
    MainPage s_AcceptedMainPage = MainPage::Combat;
    int s_MiscSubTab = 0;
    int s_ConfigSubTab = 0;
    int s_FilterSubTab = 0;
    int s_PreviewForcedTab = -1;
    bool s_PreviewOpenSettings = false;

    AimPresetKind s_AimPresetKind = AimPresetKind::Group;
    int s_AimPresetGroup = 0;
    std::uint32_t s_AimPresetWeapon = 0;
    std::uint32_t s_AimRemovePending = 0;
    bool s_AimRemoveRequest = false;
    int s_AimMode = 0;

    bool s_LightTheme = false;
    bool s_Particles = false;
    bool s_ShowInstruments = true;
    float s_MenuAlpha = 0.f;
    bool s_Ready = false;
    char s_AccountName[48] = "User";
    std::int64_t s_AccountExpiresAt = 0;

    float UIScale() noexcept
    {
        switch (s_UIScalePercent) {
        case 75:  return 0.75f;
        case 125: return 1.25f;
        case 150: return 1.50f;
        case 175: return 1.75f;
        case 200: return 2.00f;
        default:  return 1.00f;
        }
    }
}
