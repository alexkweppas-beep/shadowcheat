#pragma once

#include <cstdint>

namespace Menu
{
    struct AimCurve;
}

namespace WeaponManagerUi
{
    // Attachments / combat tuning embedded in Misc -> Weapons.
    void Render() noexcept;

    // Stable curve primitive reused by the unified Aim workspace.
    void RenderCurveEditor(
        const char* id,
        Menu::AimCurve& curve,
        float minimum,
        float maximum) noexcept;

    // Separate Add weapon window. Returns true once a weapon was added.
    void OpenAddWeapon() noexcept;
    bool RenderAddWeaponWindow(std::uint32_t* addedHash) noexcept;
}
