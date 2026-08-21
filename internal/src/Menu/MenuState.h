#pragma once

#include <cstdint>

#include "Menu.h"

struct ImGuiStyle;

// Shared menu state and small helpers used by every menu translation unit.
// Menu.cpp owns the shell, the page files own their pages, and both read the
// selection state from here instead of file-local statics.
namespace Menu
{
    enum class AimPresetKind : std::uint8_t
    {
        Group = 0,
        Weapon
    };

    // Navigation.
    extern MainPage s_MainPage;
    extern MainPage s_AcceptedMainPage;
    extern int s_MiscSubTab;
    extern int s_ConfigSubTab;

    // -1 leaves sub-tabs alone. MenuPreview sets it from --tab so screenshot
    // runs can reach every sub-page without synthesising clicks.
    extern int s_PreviewForcedTab;

    // Aim page selection.
    extern AimPresetKind s_AimPresetKind;
    extern int s_AimPresetGroup;
    extern std::uint32_t s_AimPresetWeapon;
    extern std::uint32_t s_AimRemovePending;
    extern bool s_AimRemoveRequest;
    extern int s_AimMode;

    // Global keys owned by the gear panel. The toggle never resolves to
    // "unbound" so the menu can always be reached; unload starts unassigned.
    extern int s_MenuToggleKey;
    extern int s_UnloadKey;

    // Appearance state that outlives a single page.
    extern bool s_LightTheme;
    extern bool s_Particles;
    extern bool s_ShowInstruments;
    extern float s_MenuAlpha;
    extern bool s_Ready;
    extern char s_AccountName[48];
    extern std::int64_t s_AccountExpiresAt;

    // Localisation shorthand shared by all menu sources.
    inline const char* T(
        const char* english,
        const char* russian,
        const char* ukrainian)
    {
        return Translate(english, russian, ukrainian);
    }

    float UIScale() noexcept;

    void InitDefaultEspProfiles() noexcept;
    void EnsureAimProfilesInitialized() noexcept;
    AimWeaponProfile MakeDefaultAimProfile() noexcept;
    void PullLegacyIntoGeneral() noexcept;
    void PushGeneralToLegacy() noexcept;
    int AimGroupFromWeaponHash(std::uint32_t hash) noexcept;

    std::uint32_t BonesToMask(const bool bones[kAimHitboxCount]) noexcept;
    void MaskToBones(
        std::uint32_t mask,
        bool bones[kAimHitboxCount]) noexcept;

    void ApplyRawImGuiTheme(ImGuiStyle& style);
}
