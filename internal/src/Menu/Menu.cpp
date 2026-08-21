#include "Menu.h"
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cfloat>

#define IMGUI_DEFINE_MATH_OPERATORS

#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"
#include "Leyawin/custom_widgets.hpp"
#include "Leyawin/fonts.h"
#include "Leyawin/leyawin_globals.h"
#include "Leyawin/leyawin_init.h"
#include "EspDrawHelpers.h"
#include "EspProjectedOverlay.h"
#include "MenuTexture.h"
#include "MenuUiHelpers.h"
#include "MenuState.h"
#include "MenuPages.h"
#include "MenuConfig.h"
#include "MenuSearch.h"
#include "MenuTheme.h"
#include "MenuType.h"
#include "MenuMotion.h"
#include "WeaponManager.h"
#include "Leyawin/leyawin_safe.h"
#include "IconsFontAwesome6.h"
#include "font_awesome.h"
#include "font_roboto.h"
#include "font_ibm_plex_mono.h"
#include "font_inter.h"
#include "majestic_weapon_icons_font.h"
#include "WeaponIconMap.h"
#include "../Features/Friends.h"
#include "../Features/MiscFeatures.h"
#include "../Features/QuantTools.h"
#include "../Features/UkraineTools.h"
#include "../Features/FiveRpFarmBot.h"
#include "../Features/RagePlayerNames.h"
#include "../Features/VehicleMovement.h"
#include "../Features/SilentAim.h"
#include "../Features/Aimbot.h"
#include "../Features/FiveRpMetadataResolver.h"
#include "../Features/RageV8Runtime.h"
#include "../Features/RuntimeProfiler.h"
#include "../Features/ScriptBridge.h"
#include "../Features/CloudStorage.h"
#include "../Features/FeatureBinds.h"
#include "../Features/ObsBypass.h"
#include "../Features/MeshSilhouette.h"
#include "../Features/PedPreview.h"
#include "../Main/Hooks.h"
#include "../SDK/Game.h"
#include "../SDK/RageBuildFlavor.h"
#include "../SDK/WeaponGroups.h"
#include "../SDK/WeaponCatalog.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace Menu
{
    static constexpr const char* kBrandName = "shadowcheats.cc";
    // 0..1 sweep of the highlight that runs across the wordmark on open.
    static float s_BrandShine = 1.f;

    int s_UIScalePercent = 100;
    Language s_Language = Language::English;

    int s_MenuToggleKey = VK_F4;
    int s_UnloadKey = 0;

    bool  s_Aim_EnableSilent = false;
    bool  s_Aim_VisibleCheck = true;
    bool  s_Aim_Nearest = true;
    bool  s_Aim_HitboxHead = true;
    bool  s_Aim_HitboxNeck = false;
    bool  s_Aim_HitboxChest = true;
    bool  s_Aim_HitboxPelvis = false;
    bool  s_Aim_DrawFov = false;
    float s_Aim_FOV = 50.0f;
    bool  s_Aim_MarkTarget = true;
    bool  s_Aim_LockOnLast = false;
    int   s_Aim_TargetBone = 0;
    bool  s_Aim_EnableHitchance = true;
    float s_Aim_Hitchance = 75.0f;
    bool  s_Aim_IgnoreFriends = false;
    bool  s_Aim_IgnoreDead = true;
    bool  s_Aim_IgnoreNpc = true;
    bool  s_Aim_IgnoreAdmins = true;

    bool  s_Vec_EnableVector = false;
    bool  s_Vec_VisibleCheck = true;
    bool  s_Vec_Nearest = true;
    bool  s_Vec_HitboxHead = true;
    bool  s_Vec_HitboxNeck = false;
    bool  s_Vec_HitboxChest = true;
    bool  s_Vec_HitboxPelvis = false;
    int   s_Vec_ActivationKey = VK_RBUTTON;
    bool  s_Vec_DrawFov = false;
    float s_Vec_FOV = 40.0f;
    float s_Vec_Smoothness = 1.0f; // sp¬ 1..100, larger = slower
    bool  s_Vec_RandomSmooth = false;
    float s_Vec_RandomSmoothAmt = 0.25f;
    bool  s_Vec_EnableSmartSmooth = false;
    bool  s_Vec_CameraShake = false;
    float s_Vec_CameraShakeAmt = 0.20f;


    EspProfileSettings s_EspProfiles[
        static_cast<int>(EspProfileId::Count)] = {};
    RadarSettings s_RadarSettings{};
    VehicleVisualSettings s_VehicleVisualSettings{};
    ObjectVisualSettings s_ObjectVisualSettings{};

    void InitDefaultEspProfiles() noexcept
    {
        static bool done = false;
        if (done)
            return;
        done = true;

        // Players
        {
            EspProfileSettings& p = s_EspProfiles[0];
            p.enabled = true;
            p.box = true;
            p.healthBar = true;
            p.armorBar = true;
            p.nameTag = true;
            p.factionTag = true;
            p.familyTag = true;
            p.skeleton = false;
            p.distance = true;
            p.weaponTag = true;
            p.dynamicIdTag = true;
            p.staticIdTag = true;
            p.showDead = true;
            p.visibleCheck = true;
            p.healthBarPos = EspHealthBarLeft;
            p.armorBarPos = EspHealthBarLeft;
            p.nameTagPos = EspTagAbove;
            p.factionTagPos = EspTagAbove;
            p.distancePos = EspTagBelow;
            p.boxStyle = EspBoxFull;
            p.healthStyle = EspBarDynamic;
            p.armorStyle = EspBarSolid;
            p.boxColor[0] = 1.0f; p.boxColor[1] = 0.1f; p.boxColor[2] = 0.1f;
            p.boxFillColor[0] = 1.0f; p.boxFillColor[1] = 0.1f; p.boxFillColor[2] = 0.1f;
            p.nameColor[0] = 1.0f; p.nameColor[1] = 1.0f; p.nameColor[2] = 1.0f;
            p.factionColor[0] = 1.0f; p.factionColor[1] = 0.85f; p.factionColor[2] = 0.25f;
            p.familyColor[0] = 0.85f; p.familyColor[1] = 0.45f; p.familyColor[2] = 1.0f;
            p.healthColor[0] = 0.0f; p.healthColor[1] = 1.0f; p.healthColor[2] = 0.0f;
            p.armorColor[0] = 0.25f; p.armorColor[1] = 0.55f; p.armorColor[2] = 1.0f;
            p.skeletonColor[0] = 1.0f; p.skeletonColor[1] = 0.55f; p.skeletonColor[2] = 0.1f;
            p.distanceColor[0] = 0.8f; p.distanceColor[1] = 0.8f; p.distanceColor[2] = 0.8f;
            p.hiddenColor[0] = 0.45f; p.hiddenColor[1] = 0.45f; p.hiddenColor[2] = 0.45f;
        }
        // Faction Players
        {
            EspProfileSettings& p = s_EspProfiles[1];
            p.enabled = true;
            p.box = true;
            p.healthBar = true;
            p.armorBar = true;
            p.nameTag = true;
            p.factionTag = true;
            p.familyTag = true;
            p.skeleton = false;
            p.distance = true;
            p.weaponTag = true;
            p.dynamicIdTag = true;
            p.staticIdTag = true;
            p.showDead = true;
            p.visibleCheck = true;
            p.healthBarPos = EspHealthBarLeft;
            p.armorBarPos = EspHealthBarLeft;
            p.nameTagPos = EspTagAbove;
            p.factionTagPos = EspTagAbove;
            p.distancePos = EspTagBelow;
            p.boxStyle = EspBoxFull;
            p.healthStyle = EspBarDynamic;
            p.armorStyle = EspBarSolid;
            p.boxColor[0] = 0.2f; p.boxColor[1] = 0.7f; p.boxColor[2] = 1.0f;
            p.boxFillColor[0] = 0.2f; p.boxFillColor[1] = 0.7f; p.boxFillColor[2] = 1.0f;
            p.nameColor[0] = 0.4f; p.nameColor[1] = 0.85f; p.nameColor[2] = 1.0f;
            p.factionColor[0] = 0.4f; p.factionColor[1] = 0.85f; p.factionColor[2] = 1.0f;
            p.familyColor[0] = 0.85f; p.familyColor[1] = 0.45f; p.familyColor[2] = 1.0f;
            p.healthColor[0] = 0.0f; p.healthColor[1] = 1.0f; p.healthColor[2] = 0.0f;
            p.armorColor[0] = 0.25f; p.armorColor[1] = 0.55f; p.armorColor[2] = 1.0f;
            p.skeletonColor[0] = 0.2f; p.skeletonColor[1] = 0.8f; p.skeletonColor[2] = 1.0f;
            p.distanceColor[0] = 0.8f; p.distanceColor[1] = 0.8f; p.distanceColor[2] = 0.8f;
            p.hiddenColor[0] = 0.25f; p.hiddenColor[1] = 0.45f; p.hiddenColor[2] = 0.6f;
        }
        // Family Players
        {
            EspProfileSettings& p = s_EspProfiles[2];
            p.enabled = true;
            p.box = true;
            p.healthBar = true;
            p.armorBar = true;
            p.nameTag = true;
            p.factionTag = true;
            p.familyTag = true;
            p.skeleton = false;
            p.distance = true;
            p.weaponTag = true;
            p.dynamicIdTag = true;
            p.staticIdTag = true;
            p.showDead = true;
            p.visibleCheck = true;
            p.healthBarPos = EspHealthBarLeft;
            p.armorBarPos = EspHealthBarLeft;
            p.nameTagPos = EspTagAbove;
            p.factionTagPos = EspTagAbove;
            p.distancePos = EspTagBelow;
            p.boxStyle = EspBoxFull;
            p.healthStyle = EspBarDynamic;
            p.armorStyle = EspBarSolid;
            p.boxColor[0] = 0.85f; p.boxColor[1] = 0.45f; p.boxColor[2] = 1.0f;
            p.boxFillColor[0] = 0.85f; p.boxFillColor[1] = 0.45f; p.boxFillColor[2] = 1.0f;
            p.nameColor[0] = 0.9f; p.nameColor[1] = 0.6f; p.nameColor[2] = 1.0f;
            p.factionColor[0] = 1.0f; p.factionColor[1] = 0.85f; p.factionColor[2] = 0.25f;
            p.familyColor[0] = 0.9f; p.familyColor[1] = 0.6f; p.familyColor[2] = 1.0f;
            p.healthColor[0] = 0.0f; p.healthColor[1] = 1.0f; p.healthColor[2] = 0.0f;
            p.armorColor[0] = 0.25f; p.armorColor[1] = 0.55f; p.armorColor[2] = 1.0f;
            p.skeletonColor[0] = 0.85f; p.skeletonColor[1] = 0.45f; p.skeletonColor[2] = 1.0f;
            p.distanceColor[0] = 0.8f; p.distanceColor[1] = 0.8f; p.distanceColor[2] = 0.8f;
            p.hiddenColor[0] = 0.5f; p.hiddenColor[1] = 0.3f; p.hiddenColor[2] = 0.6f;
        }
        // NPC
        {
            EspProfileSettings& p = s_EspProfiles[3];
            p.enabled = true;
            p.box = true;
            p.healthBar = true;
            p.armorBar = false;
            p.nameTag = false;
            p.factionTag = false;
            p.familyTag = false;
            p.skeleton = false;
            p.distance = true;
            p.visibleCheck = false;
            p.healthBarPos = EspHealthBarLeft;
            p.armorBarPos = EspHealthBarRight;
            p.nameTagPos = EspTagAbove;
            p.factionTagPos = EspTagAbove;
            p.distancePos = EspTagBelow;
            p.boxStyle = EspBoxCorners;
            p.healthStyle = EspBarDynamic;
            p.armorStyle = EspBarSolid;
            p.boxColor[0] = 0.55f; p.boxColor[1] = 0.55f; p.boxColor[2] = 0.55f;
            p.boxFillColor[0] = 0.55f; p.boxFillColor[1] = 0.55f; p.boxFillColor[2] = 0.55f;
            p.nameColor[0] = 0.75f; p.nameColor[1] = 0.75f; p.nameColor[2] = 0.75f;
            p.healthColor[0] = 0.2f; p.healthColor[1] = 0.85f; p.healthColor[2] = 0.25f;
            p.armorColor[0] = 0.35f; p.armorColor[1] = 0.55f; p.armorColor[2] = 0.85f;
            p.skeletonColor[0] = 0.65f; p.skeletonColor[1] = 0.65f; p.skeletonColor[2] = 0.65f;
            p.distanceColor[0] = 0.65f; p.distanceColor[1] = 0.65f; p.distanceColor[2] = 0.65f;
            p.hiddenColor[0] = 0.28f; p.hiddenColor[1] = 0.28f; p.hiddenColor[2] = 0.28f;
            p.factionColor[0] = 0.75f; p.factionColor[1] = 0.75f; p.factionColor[2] = 0.55f;
            p.familyColor[0] = 0.75f; p.familyColor[1] = 0.55f; p.familyColor[2] = 0.75f;
        }
        // Admin
        {
            EspProfileSettings& p = s_EspProfiles[4];
            p.enabled = true;
            p.box = true;
            p.healthBar = true;
            p.armorBar = true;
            p.nameTag = true;
            p.factionTag = true;
            p.familyTag = true;
            p.skeleton = true;
            p.distance = true;
            p.visibleCheck = true;
            p.healthBarPos = EspHealthBarLeft;
            p.armorBarPos = EspHealthBarLeft;
            p.nameTagPos = EspTagAbove;
            p.factionTagPos = EspTagAbove;
            p.distancePos = EspTagBelow;
            p.boxStyle = EspBoxFilled;
            p.healthStyle = EspBarDynamic;
            p.armorStyle = EspBarSolid;
            p.boxColor[0] = 0.7f; p.boxColor[1] = 0.25f; p.boxColor[2] = 1.0f;
            p.boxFillColor[0] = 0.7f; p.boxFillColor[1] = 0.25f; p.boxFillColor[2] = 1.0f;
            p.nameColor[0] = 1.0f; p.nameColor[1] = 0.85f; p.nameColor[2] = 0.2f;
            p.healthColor[0] = 0.15f; p.healthColor[1] = 1.0f; p.healthColor[2] = 0.4f;
            p.armorColor[0] = 0.35f; p.armorColor[1] = 0.7f; p.armorColor[2] = 1.0f;
            p.skeletonColor[0] = 0.85f; p.skeletonColor[1] = 0.35f; p.skeletonColor[2] = 1.0f;
            p.distanceColor[0] = 1.0f; p.distanceColor[1] = 0.85f; p.distanceColor[2] = 0.2f;
            p.hiddenColor[0] = 0.4f; p.hiddenColor[1] = 0.2f; p.hiddenColor[2] = 0.45f;
            p.factionColor[0] = 1.0f; p.factionColor[1] = 0.75f; p.factionColor[2] = 0.2f;
            p.familyColor[0] = 0.9f; p.familyColor[1] = 0.5f; p.familyColor[2] = 1.0f;
        }
        // Animal (not NPC — ambient/scripted fauna)
        {
            EspProfileSettings& p = s_EspProfiles[5];
            p.enabled = true;
            p.box = true;
            p.healthBar = false;
            p.armorBar = false;
            p.nameTag = true;
            p.factionTag = false;
            p.familyTag = false;
            p.skeleton = false;
            p.distance = true;
            p.visibleCheck = false;
            p.healthBarPos = EspHealthBarLeft;
            p.armorBarPos = EspHealthBarRight;
            p.nameTagPos = EspTagAbove;
            p.factionTagPos = EspTagAbove;
            p.distancePos = EspTagBelow;
            p.boxStyle = EspBoxCorners;
            p.healthStyle = EspBarDynamic;
            p.armorStyle = EspBarSolid;
            p.boxColor[0] = 0.45f; p.boxColor[1] = 0.70f; p.boxColor[2] = 0.35f;
            p.boxFillColor[0] = 0.45f; p.boxFillColor[1] = 0.70f; p.boxFillColor[2] = 0.35f;
            p.nameColor[0] = 0.55f; p.nameColor[1] = 0.85f; p.nameColor[2] = 0.40f;
            p.healthColor[0] = 0.2f; p.healthColor[1] = 0.85f; p.healthColor[2] = 0.25f;
            p.armorColor[0] = 0.35f; p.armorColor[1] = 0.55f; p.armorColor[2] = 0.85f;
            p.skeletonColor[0] = 0.55f; p.skeletonColor[1] = 0.75f; p.skeletonColor[2] = 0.40f;
            p.distanceColor[0] = 0.55f; p.distanceColor[1] = 0.75f; p.distanceColor[2] = 0.45f;
            p.hiddenColor[0] = 0.28f; p.hiddenColor[1] = 0.35f; p.hiddenColor[2] = 0.22f;
            p.factionColor[0] = 0.55f; p.factionColor[1] = 0.75f; p.factionColor[2] = 0.40f;
            p.familyColor[0] = 0.55f; p.familyColor[1] = 0.75f; p.familyColor[2] = 0.40f;
        }

        // Friends: off by default, because routing a player out of the Players
        // scope changes what the user already tuned. Blue to match the radar
        // friend marker, so one player reads the same on both.
        {
            EspProfileSettings& p =
                s_EspProfiles[static_cast<int>(EspProfileId::Friends)];
            p.enabled = false;
            p.box = true;
            p.healthBar = true;
            p.armorBar = true;
            p.nameTag = true;
            p.factionTag = false;
            p.familyTag = false;
            p.skeleton = false;
            p.distance = true;
            p.visibleCheck = true;
            p.healthBarPos = EspHealthBarLeft;
            p.armorBarPos = EspHealthBarRight;
            p.nameTagPos = EspTagAbove;
            p.factionTagPos = EspTagAbove;
            p.distancePos = EspTagBelow;
            p.boxStyle = EspBoxCorners;
            p.healthStyle = EspBarDynamic;
            p.armorStyle = EspBarSolid;
            p.boxColor[0] = 0.31f; p.boxColor[1] = 0.63f; p.boxColor[2] = 1.00f;
            p.boxFillColor[0] = 0.31f; p.boxFillColor[1] = 0.63f; p.boxFillColor[2] = 1.00f;
            p.nameColor[0] = 0.55f; p.nameColor[1] = 0.78f; p.nameColor[2] = 1.00f;
            p.healthColor[0] = 0.20f; p.healthColor[1] = 0.85f; p.healthColor[2] = 0.35f;
            p.armorColor[0] = 0.35f; p.armorColor[1] = 0.55f; p.armorColor[2] = 0.85f;
            p.skeletonColor[0] = 0.45f; p.skeletonColor[1] = 0.72f; p.skeletonColor[2] = 1.00f;
            p.distanceColor[0] = 0.52f; p.distanceColor[1] = 0.70f; p.distanceColor[2] = 0.92f;
            p.hiddenColor[0] = 0.20f; p.hiddenColor[1] = 0.33f; p.hiddenColor[2] = 0.50f;
            p.factionColor[0] = 0.55f; p.factionColor[1] = 0.78f; p.factionColor[2] = 1.00f;
            p.familyColor[0] = 0.55f; p.familyColor[1] = 0.78f; p.familyColor[2] = 1.00f;
            p.radarColor[0] = 0.31f; p.radarColor[1] = 0.63f; p.radarColor[2] = 1.00f;
        }

        for (int i = 0; i < static_cast<int>(EspProfileId::Count); ++i)
        {
            EspProfileSettings& p = s_EspProfiles[i];
            float* colors[] = {
                p.boxColor, p.boxFillColor, p.nameColor, p.factionColor,
                p.familyColor, p.healthColor, p.armorColor, p.skeletonColor,
                p.distanceColor, p.weaponColor, p.dynamicIdColor,
                p.staticIdColor, p.deadColor, p.hiddenColor, p.radarColor
            };
            for (float* c : colors)
                c[3] = 1.f;
            EspProjected::EnsureLayoutsMigrated(p);
        }
    }
    int s_VisualCategoryTab = 0;
    int s_VisualPedProfile = 0;
    char s_AdminVariableKey[32] = "adminLevel";
    int s_AdminLevelThreshold = 0;

    int   s_ConfigIndex = 0;


    float s_Aim_FovColor[3]  = { 1.0f, 1.0f, 1.0f };
    // The marker used to be near-black, which is invisible against the game
    // at night and against dark clothing. Nothing reads this from config or
    // exposes it in the UI, so the default is the value that ships.
    float s_Aim_MarkColor[3] = { 1.00f, 0.27f, 0.36f };
    float s_Vec_FovColor[3]      = { 0.96f, 0.62f, 0.80f };

    int s_AimWeaponTab = 0;
    AimWeaponProfile s_AimWeaponProfiles[kAimWeaponGroupCount]{};
    WeaponOverrideSlot s_WeaponOverrides[kMaxWeaponOverrides]{};
    int s_WeaponOverrideCount = 0;
    bool s_WeaponManagerOpen = false;


    AimWeaponProfile MakeDefaultAimProfile() noexcept
    {
        AimWeaponProfile p{};
        p.useCustom = false;
        p.vecEnabled = false;
        p.vecKey = VK_RBUTTON;
        p.vecPrediction = false;
        p.vecPredictionScale = 1.f;
        p.vecDrawFov = false;
        p.vecFov = 40.f;
        p.vecFovColor[0] = 0.96f;
        p.vecFovColor[1] = 0.62f;
        p.vecFovColor[2] = 0.80f;
        p.vecNearest = true;
        p.vecSticky = true;
        p.vecBoneNearestAny = false;
        p.vecVisible = true;
        p.vecBones[0] = true;  // head
        p.vecBones[2] = true;  // chest
        p.vecSmoothness = 1.f;
        p.vecRandomSmooth = false;
        p.vecRandomAmt = 0.25f;
        p.vecSmartSmooth = false;
        p.vecCamShake = false;
        p.vecShakeAmt = 0.20f;
        p.silentEnabled = false;
        p.silentKey = VK_RBUTTON;
        p.silentDrawFov = false;
        p.silentFov = 40.f;
        p.silentFovColor[0] = 1.f;
        p.silentFovColor[1] = 1.f;
        p.silentFovColor[2] = 1.f;
        p.silentMark = false;
        p.silentLock = false;
        p.silentNearest = true;
        p.silentBoneNearestAny = false;
        p.silentVisible = true;
        p.silentBones[0] = true;
        p.silentBones[2] = true;
        p.silentHitchance = true;
        p.silentHitchancePct = 100.f;
        p.triggerEnabled = false;
        p.triggerKey = 0;
        p.triggerFov = 7.f;
        p.triggerDelayMs = 35.f;
        p.triggerCooldownMs = 90.f;
        p.triggerSticky = false;
        p.triggerNearest = true;
        p.triggerVisible = true;
        p.triggerBoneNearestAny = false;
        p.triggerBones[0] = true;
        p.triggerBones[2] = true;
        p.ignoreDead = true;
        p.ignoreNpc = true;
        p.ignoreAdmins = true;
        return p;
    }

    void EnsureAimProfilesInitialized() noexcept
    {
        static bool done = false;
        if (done)
            return;
        done = true;
        for (int i = 0; i < kAimWeaponGroupCount; ++i)
            s_AimWeaponProfiles[i] = MakeDefaultAimProfile();
        s_AimWeaponProfiles[0].useCustom = true; // General always active
        PullLegacyIntoGeneral();
    }

    void PullLegacyIntoGeneral() noexcept
    {
        AimWeaponProfile& g = s_AimWeaponProfiles[0];
        g.useCustom = true;
        g.vecEnabled = s_Vec_EnableVector;
        g.vecKey = s_Vec_ActivationKey;
        g.vecDrawFov = s_Vec_DrawFov;
        g.vecFov = s_Vec_FOV;
        g.vecFovColor[0] = s_Vec_FovColor[0];
        g.vecFovColor[1] = s_Vec_FovColor[1];
        g.vecFovColor[2] = s_Vec_FovColor[2];
        g.vecNearest = s_Vec_Nearest;
        g.vecVisible = s_Vec_VisibleCheck;
        g.vecBones[0] = s_Vec_HitboxHead;
        g.vecBones[1] = s_Vec_HitboxNeck;
        g.vecBones[2] = s_Vec_HitboxChest;
        g.vecBones[3] = s_Vec_HitboxPelvis;
        g.vecSmoothness = s_Vec_Smoothness;
        g.vecRandomSmooth = s_Vec_RandomSmooth;
        g.vecRandomAmt = s_Vec_RandomSmoothAmt;
        g.vecSmartSmooth = s_Vec_EnableSmartSmooth;
        g.vecCamShake = s_Vec_CameraShake;
        g.vecShakeAmt = s_Vec_CameraShakeAmt;
        g.silentEnabled = s_Aim_EnableSilent;
        g.silentDrawFov = s_Aim_DrawFov;
        g.silentFov = s_Aim_FOV;
        g.silentFovColor[0] = s_Aim_FovColor[0];
        g.silentFovColor[1] = s_Aim_FovColor[1];
        g.silentFovColor[2] = s_Aim_FovColor[2];
        g.silentMark = s_Aim_MarkTarget;
        g.silentLock = s_Aim_LockOnLast;
        g.silentNearest = s_Aim_Nearest;
        g.silentVisible = s_Aim_VisibleCheck;
        g.silentBones[0] = s_Aim_HitboxHead;
        g.silentBones[1] = s_Aim_HitboxNeck;
        g.silentBones[2] = s_Aim_HitboxChest;
        g.silentBones[3] = s_Aim_HitboxPelvis;
        g.silentHitchance = s_Aim_EnableHitchance;
        g.silentHitchancePct = s_Aim_Hitchance;
        g.ignoreDead = s_Aim_IgnoreDead;
        g.ignoreNpc = s_Aim_IgnoreNpc;
        g.ignoreAdmins = s_Aim_IgnoreAdmins;
        g.ignoreFriends = s_Aim_IgnoreFriends;
    }

    void PushGeneralToLegacy() noexcept
    {
        const AimWeaponProfile& g = s_AimWeaponProfiles[0];
        s_Vec_EnableVector = g.vecEnabled;
        s_Vec_ActivationKey = g.vecKey;
        s_Vec_DrawFov = g.vecDrawFov;
        s_Vec_FOV = g.vecFov;
        s_Vec_FovColor[0] = g.vecFovColor[0];
        s_Vec_FovColor[1] = g.vecFovColor[1];
        s_Vec_FovColor[2] = g.vecFovColor[2];
        s_Vec_Nearest = g.vecNearest;
        s_Vec_VisibleCheck = g.vecVisible;
        s_Vec_HitboxHead = g.vecBones[0];
        s_Vec_HitboxNeck = g.vecBones[1];
        s_Vec_HitboxChest = g.vecBones[2];
        s_Vec_HitboxPelvis = g.vecBones[3];
        s_Vec_Smoothness = g.vecSmoothness;
        s_Vec_RandomSmooth = g.vecRandomSmooth;
        s_Vec_RandomSmoothAmt = g.vecRandomAmt;
        s_Vec_EnableSmartSmooth = g.vecSmartSmooth;
        s_Vec_CameraShake = g.vecCamShake;
        s_Vec_CameraShakeAmt = g.vecShakeAmt;
        s_Aim_EnableSilent = g.silentEnabled;
        s_Aim_DrawFov = g.silentDrawFov;
        s_Aim_FOV = g.silentFov;
        s_Aim_FovColor[0] = g.silentFovColor[0];
        s_Aim_FovColor[1] = g.silentFovColor[1];
        s_Aim_FovColor[2] = g.silentFovColor[2];
        s_Aim_MarkTarget = g.silentMark;
        s_Aim_LockOnLast = g.silentLock;
        s_Aim_Nearest = g.silentNearest;
        s_Aim_VisibleCheck = g.silentVisible;
        s_Aim_HitboxHead = g.silentBones[0];
        s_Aim_HitboxNeck = g.silentBones[1];
        s_Aim_HitboxChest = g.silentBones[2];
        s_Aim_HitboxPelvis = g.silentBones[3];
        s_Aim_EnableHitchance = g.silentHitchance;
        s_Aim_Hitchance = g.silentHitchancePct;
        s_Aim_IgnoreDead = g.ignoreDead;
        s_Aim_IgnoreNpc = g.ignoreNpc;
        s_Aim_IgnoreAdmins = g.ignoreAdmins;
        s_Aim_IgnoreFriends = g.ignoreFriends;
    }

    AimWeaponProfile& GetAimProfile(int groupIndex)
    {
        EnsureAimProfilesInitialized();
        if (groupIndex < 0 || groupIndex >= kAimWeaponGroupCount)
            groupIndex = 0;
        return s_AimWeaponProfiles[groupIndex];
    }

    int AimGroupFromWeaponHash(std::uint32_t hash) noexcept
    {
        WeaponGroups::Group group = WeaponGroups::Classify(hash);
        if (group != WeaponGroups::Group::General)
            return static_cast<int>(group);
        if (const WeaponCatalog::Entry* entry =
                WeaponCatalog::FindByHash(hash))
        {
            group = WeaponCatalog::ToAimGroup(entry->category);
        }
        return static_cast<int>(group);
    }

    int GetActiveAimWeaponGroup()
    {
        EnsureAimProfilesInitialized();
        const uint32_t hash = Game::GetLocalWeaponHash();
        return AimGroupFromWeaponHash(hash);
    }

    const AimWeaponProfile& GetActiveAimProfile()
    {
        EnsureAimProfilesInitialized();
        const uint32_t hash = Game::GetLocalWeaponHash();
        if (WeaponOverrideSlot* slot = FindWeaponOverride(hash))
        {
            if (slot->active)
                return slot->profile;
        }
        const int group = GetActiveAimWeaponGroup();
        if (group > 0 && s_AimWeaponProfiles[group].useCustom)
            return s_AimWeaponProfiles[group];
        return s_AimWeaponProfiles[0];
    }

    WeaponOverrideSlot* FindWeaponOverride(std::uint32_t hash) noexcept
    {
        if (!hash)
            return nullptr;
        for (int i = 0; i < s_WeaponOverrideCount; ++i)
        {
            if (s_WeaponOverrides[i].hash == hash)
                return &s_WeaponOverrides[i];
        }
        return nullptr;
    }

    WeaponOverrideSlot* GetOrCreateWeaponOverride(std::uint32_t hash) noexcept
    {
        EnsureAimProfilesInitialized();
        if (!hash)
            return nullptr;
        if (WeaponOverrideSlot* existing = FindWeaponOverride(hash))
            return existing;
        if (s_WeaponOverrideCount >= kMaxWeaponOverrides)
            return nullptr;
        WeaponOverrideSlot& slot = s_WeaponOverrides[s_WeaponOverrideCount++];
        slot = {};
        slot.hash = hash;
        slot.profile = MakeDefaultAimProfile();
        // Seed from current group profile.
        const int group = AimGroupFromWeaponHash(hash);
        if (group > 0 && s_AimWeaponProfiles[group].useCustom)
            slot.profile = s_AimWeaponProfiles[group];
        else
            slot.profile = s_AimWeaponProfiles[0];
        slot.profile.useCustom = true;
        return &slot;
    }

    bool RemoveWeaponOverride(std::uint32_t hash) noexcept
    {
        if (!hash)
            return false;
        for (int i = 0; i < s_WeaponOverrideCount; ++i)
        {
            if (s_WeaponOverrides[i].hash != hash)
                continue;
            for (int j = i; j + 1 < s_WeaponOverrideCount; ++j)
                s_WeaponOverrides[j] = s_WeaponOverrides[j + 1];
            --s_WeaponOverrideCount;
            s_WeaponOverrides[s_WeaponOverrideCount] = {};
            return true;
        }
        return false;
    }

    float EvaluateDynamicFov(
        const AimWeaponProfile& profile,
        float distanceMeters,
        bool isAds,
        float targetSpeed) noexcept
    {
        float fov = profile.vecFov;
        if (profile.dynamicFov)
        {
            const float distNorm = (std::min)(1.f, (std::max)(0.f, distanceMeters / 120.f));
            if (profile.useFovCurve)
                fov *= SampleAimCurve(profile.fovDistanceCurve, distNorm);
            else
                fov *= profile.fovDistanceMul;
            if (isAds)
                fov *= profile.dynamicFovAdsMul;
            fov *= 1.f + profile.dynamicFovSpeedExpand * (std::min)(1.f, targetSpeed / 12.f);
        }
        return (std::max)(1.f, fov);
    }

    float EvaluateSmoothness(
        const AimWeaponProfile& profile,
        float distanceMeters,
        float crosshairErrorPx) noexcept
    {
        float smoothness = profile.vecSmoothness;
        if (!(smoothness >= 1.f))
            smoothness = 1.f;
        const float distNorm = (std::min)(1.f, (std::max)(0.f, distanceMeters / 120.f));
        if (profile.useSmoothCurve)
            smoothness *= SampleAimCurve(profile.smoothDistanceCurve, distNorm);
        else
            smoothness *= profile.smoothDistanceMul;
        if (profile.humanize && profile.fatigue > 0.f)
        {
            // Far / long tracking → slightly slower.
            smoothness *= 1.f + profile.fatigue * distNorm * 0.5f;
        }
        (void)crosshairErrorPx;
        return (std::min)(250.f, (std::max)(1.f, smoothness));
    }

    float EvaluateSilentHitchance(
        const AimWeaponProfile& profile,
        float distanceMeters) noexcept
    {
        float chance = profile.silentHitchancePct;
        const float distNorm = (std::min)(1.f, (std::max)(0.f, distanceMeters / 120.f));
        if (profile.useSilentHcCurve)
            chance *= SampleAimCurve(profile.silentHitchanceCurve, distNorm);
        else
            chance *= profile.silentHcDistanceMul;
        return (std::min)(100.f, (std::max)(0.f, chance));
    }

    static POINT s_PrevMousePos = {};

    void OpenWeaponsTab() noexcept
    {
        s_MainPage = MainPage::Combat;
        s_AcceptedMainPage = MainPage::Combat;
        s_AimMode = 3;
        set->selection_alpha = 0.f;
        set->selection_add = 0.f;
    }

    void SetWeaponManagerOpen(bool open) noexcept
    {
        s_WeaponManagerOpen = open;
        if (open)
            OpenWeaponsTab();
    }

    bool IsWeaponManagerOpen() noexcept
    {
        return s_AcceptedMainPage == MainPage::Combat && s_AimMode == 3;
    }

    void ToggleWeaponManager() noexcept
    {
        if (IsWeaponManagerOpen())
        {
            s_MainPage = MainPage::Combat;
            set->selection_alpha = 0.f;
            set->selection_add = 0.f;
        }
        else
        {
            OpenWeaponsTab();
        }
    }

    void RenderWeaponManagerWindow() noexcept
    {
        // Weapon Manager owns its own rail page.
    }

    const char* Translate(
        const char* english,
        const char* russian,
        const char* ukrainian)
    {
        switch (s_Language) {
        case Language::Russian: return russian;
        case Language::Ukrainian: return ukrainian;
        default: return english;
        }
    }

    static const char* const kAimHitboxLabelsEn[kAimHitboxCount] = {
        "Head", "Neck", "Chest", "Pelvis",
        "L Upper Arm", "L Forearm", "L Hand",
        "R Upper Arm", "R Forearm", "R Hand",
        "L Thigh", "L Calf", "L Foot",
        "R Thigh", "R Calf", "R Foot"
    };
    static const char* const kAimHitboxLabelsRu[kAimHitboxCount] = {
        "Голова", "Шея", "Грудь", "Таз",
        "Л. плечо", "Л. предплечье", "Л. кисть",
        "П. плечо", "П. предплечье", "П. кисть",
        "Л. бедро", "Л. голень", "Л. стопа",
        "П. бедро", "П. голень", "П. стопа"
    };
    static const char* const kAimHitboxLabelsUk[kAimHitboxCount] = {
        "Голова", "Шия", "Груди", "Таз",
        "Л. плече", "Л. передпліччя", "Л. кисть",
        "П. плече", "П. передпліччя", "П. кисть",
        "Л. стегно", "Л. гомілка", "Л. стопа",
        "П. стегно", "П. гомілка", "П. стопа"
    };

    static const char* const* AimHitboxLabels() noexcept
    {
        switch (s_Language) {
        case Language::Russian: return kAimHitboxLabelsRu;
        case Language::Ukrainian: return kAimHitboxLabelsUk;
        default: return kAimHitboxLabelsEn;
        }
    }

    std::uint32_t BonesToMask(const bool bones[kAimHitboxCount]) noexcept
    {
        uint32_t mask = 0;
        for (int i = 0; i < kAimHitboxCount; ++i)
        {
            if (bones[i])
                mask |= (1u << i);
        }
        return mask;
    }

    void MaskToBones(std::uint32_t mask, bool bones[kAimHitboxCount]) noexcept
    {
        for (int i = 0; i < kAimHitboxCount; ++i)
            bones[i] = (mask & (1u << i)) != 0;
    }


    void FormatSubscriptionRemaining(
        char* buf,
        int capacity,
        std::int64_t expiresAt) noexcept
    {
        if (!buf || capacity < 8)
            return;
        buf[0] = 0;
        if (expiresAt <= 0)
        {
            lstrcpynA(buf, "—", capacity);
            return;
        }
        const std::int64_t now =
            static_cast<std::int64_t>(time(nullptr));
        std::int64_t sec = expiresAt - now;
        if (sec <= 0)
        {
            lstrcpynA(
                buf,
                T("Expired", "Истекла", "Закінчилась"),
                capacity);
            return;
        }
        const int days = static_cast<int>(sec / 86400);
        const int hours =
            static_cast<int>((sec % 86400) / 3600);
        const int minutes =
            static_cast<int>((sec % 3600) / 60);
        if (days > 0)
        {
            snprintf(
                buf,
                static_cast<size_t>(capacity),
                "%d%s %d%s",
                days,
                T("d", "д", "д"),
                hours,
                T("h", "ч", "год"));
        }
        else if (hours > 0)
        {
            snprintf(
                buf,
                static_cast<size_t>(capacity),
                "%d%s",
                hours,
                T("h", "ч", "год"));
        }
        else
        {
            snprintf(
                buf,
                static_cast<size_t>(capacity),
                "%d%s",
                ImMax(minutes, 1),
                T("m", "м", "хв"));
        }
    }

    enum class NavGroup : std::uint8_t
    {
        Combat = 0,
        World = 1,
        System = 2,
        Count = 3
    };

    struct NavGroupInfo final
    {
        NavGroup group;
        const char* titleEn;
        const char* titleRu;
        const char* titleUa;
    };

    static const NavGroupInfo kNavGroups[] = {
        { NavGroup::Combat, "COMBAT", "БОЙ", "БІЙ" },
        { NavGroup::World, "WORLD", "МИР", "СВІТ" },
        { NavGroup::System, "SYSTEM", "СИСТЕМА", "СИСТЕМА" }
    };

    static const char* NavGroupTitle(const NavGroupInfo& info) noexcept
    {
        return T(info.titleEn, info.titleRu, info.titleUa);
    }

    // Kept so page metadata stays the source for titles/subtitles.

    struct MainPageInfo final
    {
        MainPage page;
        NavGroup group;
        const char* id;
        const char* icon;
        const char* titleEn;
        const char* titleRu;
        const char* titleUa;
        const char* subtitleEn;
        const char* subtitleRu;
        const char* subtitleUa;
    };

    static const MainPageInfo kMainPages[] = {
        {
            MainPage::Combat,
            NavGroup::Combat,
            "aim",
            ICON_FA_CROSSHAIRS,
            "Aim", "Аим", "Аім",
            "Vector, silent and trigger",
            "Вектор, сайлент и триггер",
            "Вектор, сайлент і тригер"
        },
        {
            MainPage::Player,
            NavGroup::Combat,
            "player",
            ICON_FA_USER,
            "Player", "Игрок", "Гравець",
            "Self, movement and combat globals",
            "Себя, движение и глобальный бой",
            "Себе, рух і глобальний бій"
        },
        {
            MainPage::Visuals,
            NavGroup::World,
            "visuals",
            ICON_FA_EYE,
            "Visuals", "Визуалы", "Візуали",
            "Peds, world entities and radar",
            "Педы, объекты мира и радар",
            "Педи, об'єкти світу і радар"
        },
        {
            MainPage::Players,
            NavGroup::World,
            "players",
            ICON_FA_USER_GROUP,
            "Players", "Игроки", "Гравці",
            "Friends and nearby overlay",
            "Друзья и оверлей рядом",
            "Друзі та оверлей поруч"
        },
        {
            MainPage::Scripts,
            NavGroup::World,
            "scripts",
            ICON_FA_CODE,
            "Scripts", "Скрипты", "Скрипти",
            "Runtime tools and executor",
            "Инструменты runtime и executor",
            "Інструменти runtime та executor"
        },
        {
            MainPage::Cars,
            NavGroup::World,
            "cars",
            ICON_FA_CAR,
            "Cars", "Машины", "Авто",
            "Vehicle motion and utilities",
            "Движение и сервисы транспорта",
            "Рух і сервіси транспорту"
        },
        {
            MainPage::World,
            NavGroup::World,
            "world",
            ICON_FA_CLOUD_SUN,
            "World", "Мир", "Світ",
            "Weather, time and fog",
            "Погода, время и туман",
            "Погода, час і туман"
        },
#if RAGEMP_DEV_UI
        {
            MainPage::Server,
            NavGroup::World,
            "server",
            ICON_FA_SERVER,
            "Server", "Сервер", "Сервер",
            "Tools for the detected server",
            "Инструменты определённого сервера",
            "Інструменти визначеного сервера"
        },
#endif
        {
            MainPage::Settings,
            NavGroup::System,
            "settings",
            ICON_FA_GEAR,
            "Settings", "Настройки", "Налаштування",
            "Local and cloud configs",
            "Локальные и облачные конфиги",
            "Локальні та хмарні конфіги"
        }
    };

    static const MainPageInfo& PageInfo(MainPage page) noexcept
    {
        for (const MainPageInfo& info : kMainPages)
        {
            if (info.page == page)
                return info;
        }
        return kMainPages[0];
    }

    static const char* PageTitle(MainPage page) noexcept
    {
        const MainPageInfo& info = PageInfo(page);
        return T(info.titleEn, info.titleRu, info.titleUa);
    }

    static const char* PageSubtitle(MainPage page) noexcept
    {
        const MainPageInfo& info = PageInfo(page);
        return T(info.subtitleEn, info.subtitleRu, info.subtitleUa);
    }

    // Every page is always reachable now: per-server tools live inside the
    // Server page, which reports its own empty state when nothing matches.
    static bool IsPageAvailable(MainPage page) noexcept
    {
        if (!RAGEMP_DEV_UI && page == MainPage::Server)
            return false;
        return page != MainPage::Count &&
            page != MainPage::Weapons;
    }

    enum class DestRoot : int
    {
        Aim = 0,
        Visuals,
        Misc,
        Friends,
        Scripts,
#if RAGEMP_EVENT_LOGGER
        Logger,
#endif
        Configs,
        Count
    };

    struct DestChild final
    {
        const char* id;
        const char* icon;
        const char* titleEn;
        const char* titleRu;
        const char* titleUa;
        const char* subEn;
        const char* subRu;
        const char* subUa;
        MainPage page;
        int value;
    };

    struct DestRootInfo final
    {
        const char* id;
        const char* icon;
        const char* titleEn;
        const char* titleRu;
        const char* titleUa;
        const DestChild* children;
        int childCount;
        MainPage leafPage;
    };

    // Main holds silent and vector side by side, so the row stays at three
    // segments instead of splitting one aim setup across two destinations.
    static const DestChild kAimChildren[] = {
        {
            "main", ICON_FA_CROSSHAIRS,
            "Main", "Основное", "Основне",
            "Silent and vector",
            "Сайлент и вектор",
            "Сайлент і вектор",
            MainPage::Combat, 0
        },
        {
            "trigger", ICON_FA_BOLT,
            "Trigger", "Триггер", "Тригер",
            "Auto fire on target",
            "Автоогонь по цели",
            "Автовогонь по цілі",
            MainPage::Combat, 2
        }
    };

    static const DestChild kMiscChildren[] = {
        {
            "self", ICON_FA_USER,
            "Self", "Себя", "Себе",
            "Local player",
            "Локальный игрок",
            "Локальний гравець",
            MainPage::Player, 0
        },
        {
            "movement", ICON_FA_PERSON_RUNNING,
            "Movement", "Движение", "Рух",
            "Noclip, freecam, warp",
            "Noclip, freecam, варп",
            "Noclip, freecam, варп",
            MainPage::Player, 1
        },
        {
            "cars", ICON_FA_CAR,
            "Cars", "Машины", "Авто",
            "Vehicle motion",
            "Движение транспорта",
            "Рух транспорту",
            MainPage::Cars, 0
        },
        {
            "weather", ICON_FA_CLOUD_SUN,
            "Weather", "Погода", "Погода",
            "Time and weather",
            "Время и погода",
            "Час і погода",
            MainPage::World, 0
        },
        {
            "around", ICON_FA_USER_GROUP,
            "Around", "Вокруг", "Навколо",
            "Nearby players and admins",
            "Игроки и админы рядом",
            "Гравці та адміни поруч",
            MainPage::Misc, 0
        },
#if RAGEMP_DEV_UI
        {
            "server", ICON_FA_SERVER,
            "Server", "Сервер", "Сервер",
            "Detected server tools",
            "Инструменты сервера",
            "Інструменти сервера",
            MainPage::Server, 0
        }
#endif
    };

    // Configs is a leaf: account and runtime moved into the gear panel.
    static const DestChild* const kConfigChildren = nullptr;

    static const DestRootInfo kDestRoots[] = {
        {
            "aim", ICON_FA_CROSSHAIRS,
            "Aim", "Аим", "Аім",
            kAimChildren, IM_ARRAYSIZE(kAimChildren),
            MainPage::Combat
        },
        {
            // Scopes live inside the page as one chip strip.
            "visuals", ICON_FA_EYE,
            "Visuals", "Визуалы", "Візуали",
            nullptr, 0,
            MainPage::Visuals
        },
        {
            "misc", ICON_FA_BARS,
            "Misc", "Разное", "Різне",
            kMiscChildren, IM_ARRAYSIZE(kMiscChildren),
            MainPage::Player
        },
        {
            "friends", ICON_FA_USER_GROUP,
            "Friends", "Друзья", "Друзі",
            nullptr, 0,
            MainPage::Players
        },
        {
            "scripts", ICON_FA_CODE,
            "Scripts", "Скрипты", "Скрипти",
            nullptr, 0,
            MainPage::Scripts
        },
#if RAGEMP_EVENT_LOGGER
        {
            "logger", ICON_FA_LIST,
            "Logger", "Логгер", "Логер",
            nullptr, 0,
            MainPage::Scripts
        },
#endif
        {
            "configs", ICON_FA_FOLDER_OPEN,
            "Configs", "Конфиги", "Конфіги",
            kConfigChildren, 0,
            MainPage::Settings
        }
    };

    static DestRoot RootForPage(MainPage page) noexcept
    {
        switch (page)
        {
        case MainPage::Combat:
        case MainPage::Weapons:
            return DestRoot::Aim;
        case MainPage::Visuals:
            return DestRoot::Visuals;
        case MainPage::Players:
            return DestRoot::Friends;
        case MainPage::Scripts:
#if RAGEMP_EVENT_LOGGER
            // Both categories live on the Scripts page; the bridge decides
            // which of them is actually showing.
            if (ScriptBridge::CurrentPage() ==
                ScriptBridge::Page::EventLogger)
                return DestRoot::Logger;
#endif
            return DestRoot::Scripts;
        case MainPage::Settings:
            return DestRoot::Configs;
        default:
            return DestRoot::Misc;
        }
    }

    static bool IsChildActive(const DestChild& child) noexcept
    {
        if (s_MainPage != child.page)
            return false;
        switch (child.page)
        {
        case MainPage::Combat:
            // Silent (1) is part of Main (0) now.
            return child.value == 0
                ? (s_AimMode == 0 || s_AimMode == 1)
                : s_AimMode == child.value;
        case MainPage::Visuals:
            return s_VisualCategoryTab == child.value;
        case MainPage::Player:
            return s_MiscSubTab == child.value;
        case MainPage::Settings:
            return s_ConfigSubTab == child.value;
        default:
        return true;
        }
    }

    static void ApplyDestChild(const DestChild& child) noexcept
    {
        s_MainPage = child.page;
        switch (child.page)
        {
        case MainPage::Combat:
            s_AimMode = ImClamp(child.value, 0, 3);
                break;
        case MainPage::Visuals:
            s_VisualCategoryTab = child.value;
            break;
        case MainPage::Player:
            s_MiscSubTab = child.value;
            break;
        case MainPage::Settings:
            s_ConfigSubTab = child.value;
            break;
        default:
            break;
        }
    }

    static const DestChild* ActiveDestChild() noexcept
    {
        const DestRoot root = RootForPage(s_MainPage);
        const DestRootInfo& info = kDestRoots[static_cast<int>(root)];
        for (int i = 0; i < info.childCount; ++i)
        {
            if (IsChildActive(info.children[i]))
                return &info.children[i];
        }
        return nullptr;
    }

    static const char* DestSubtitle() noexcept
    {
        if (const DestChild* child = ActiveDestChild())
            return T(child->subEn, child->subRu, child->subUa);
        return PageSubtitle(s_MainPage);
    }

    static void RenderHitboxPicker(
        bool* nearestCrosshair,
        bool* nearestAny,
        bool* visibleCheck,
        bool bones[kAimHitboxCount],
        const char* idSuffix)
    {
        MenuUi::DrawBoneOptions(
            nearestCrosshair,
            nearestAny,
            visibleCheck,
            idSuffix);
        if (!*nearestAny)
        {
            MenuUi::SectionTitle(
                T("Bones", "Кости", "Кістки"));
            MenuUi::DrawBoneDropdown(bones, idSuffix);
        }
        else
        {
            ImGui::TextDisabled(
                "%s",
                T("Any skeleton bone — checklist ignored.",
                  "Любая кость скелета — список костей игнорируется.",
                  "Будь-яка кістка скелета — список кісток ігнорується."));
        }
    }

    void ApplyRawImGuiTheme(ImGuiStyle& style)
    {
        const MenuTheme::Palette& tokens = MenuTheme::Colors();

        style.FrameRounding = set->input_rounding;
        style.GrabRounding = set->input_rounding;
        style.ChildRounding = set->child_rounding;
        style.PopupRounding = set->window_rounding;
        style.TabRounding = set->selection_rounding;
        style.ScrollbarRounding = set->input_rounding;
        style.FrameBorderSize = 0.f;
        style.ChildBorderSize = 1.f;

        style.Colors[ImGuiCol_Button] = clr->combo_color;
        style.Colors[ImGuiCol_ButtonHovered] =
            clr->element_background;
        style.Colors[ImGuiCol_ButtonActive] =
            clr->accent_color;
        style.Colors[ImGuiCol_FrameBg] =
            clr->element_background;
        style.Colors[ImGuiCol_FrameBgHovered] =
            clr->child_top_bar;
        style.Colors[ImGuiCol_FrameBgActive] =
            clr->combo_color;
        style.Colors[ImGuiCol_Header] =
            clr->selection_active;
        style.Colors[ImGuiCol_HeaderHovered] =
            clr->element_background;
        style.Colors[ImGuiCol_HeaderActive] =
            clr->accent_color;
        style.Colors[ImGuiCol_CheckMark] =
            clr->accent_color;
        style.Colors[ImGuiCol_SliderGrab] =
            clr->accent_color;
        style.Colors[ImGuiCol_SliderGrabActive] =
            clr->text_active;
        style.Colors[ImGuiCol_Border] =
            clr->child_stroke_bar;
        style.Colors[ImGuiCol_ChildBg] =
            clr->child_bottom_bar;
        style.Colors[ImGuiCol_PopupBg] =
            clr->window_background;
        style.Colors[ImGuiCol_TitleBg] =
            clr->child_top_bar;
        style.Colors[ImGuiCol_TitleBgActive] =
            clr->child_top_bar;
        style.Colors[ImGuiCol_Text] =
            clr->text_active;
        style.Colors[ImGuiCol_TextDisabled] =
            clr->text;

        // These used to fall through to stock ImGui defaults.
        style.Colors[ImGuiCol_WindowBg] = tokens.canvas;
        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
        style.Colors[ImGuiCol_ScrollbarGrab] = tokens.borderStrong;
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = tokens.textFaint;
        style.Colors[ImGuiCol_ScrollbarGrabActive] = tokens.accent.active;
        style.Colors[ImGuiCol_Separator] = tokens.borderSubtle;
        style.Colors[ImGuiCol_SeparatorHovered] = tokens.borderStrong;
        style.Colors[ImGuiCol_SeparatorActive] = tokens.accent.active;
        style.Colors[ImGuiCol_Tab] = tokens.base;
        style.Colors[ImGuiCol_TabHovered] = tokens.hover;
        style.Colors[ImGuiCol_TabSelected] = tokens.raised;
        style.Colors[ImGuiCol_TabSelectedOverline] = tokens.accent.fill;
        style.Colors[ImGuiCol_TabDimmed] = tokens.base;
        style.Colors[ImGuiCol_TabDimmedSelected] = tokens.panel;
        style.Colors[ImGuiCol_NavHighlight] = tokens.borderFocus;
        style.Colors[ImGuiCol_DragDropTarget] = tokens.accent.fill;
        style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.f, 0.f, 0.f, 0.f);
        style.Colors[ImGuiCol_ResizeGripHovered] = tokens.borderStrong;
        style.Colors[ImGuiCol_ResizeGripActive] = tokens.accent.active;
        style.Colors[ImGuiCol_TextSelectedBg] =
            ImVec4(
                tokens.accent.fill.x,
                tokens.accent.fill.y,
                tokens.accent.fill.z,
                0.35f);
        style.Colors[ImGuiCol_ModalWindowDimBg] =
            ImVec4(0.f, 0.f, 0.f, 0.55f);
    }



    static constexpr int kUIFontVariantCount = 5;
    static constexpr float kUIFontScales[
        kUIFontVariantCount] = {
        1.f, 1.25f, 1.5f, 1.75f, 2.f
    };
    static ImFont* g_weapon_icon_fonts[kUIFontVariantCount]{};
    static ImFont* g_body_fonts[kUIFontVariantCount]{};
    static ImFont* g_label_fonts[kUIFontVariantCount]{};
    static ImFont* g_display_fonts[kUIFontVariantCount]{};
    static ImFont* g_mono_fonts[kUIFontVariantCount]{};
    static ImFont* g_micro_fonts[kUIFontVariantCount]{};
    static int g_ActiveFontVariant = 0;
    static ImFont* g_selection_fonts[kUIFontVariantCount]{};
    static ImFont* g_widget_fonts[kUIFontVariantCount]{};
    static ImFont* g_icon_fonts[kUIFontVariantCount]{};
    static ImFont* g_logo_fonts[kUIFontVariantCount]{};
    static ImFont* g_child_icon_fonts[kUIFontVariantCount]{};
    static ImFont* g_config_icon_fonts[kUIFontVariantCount]{};
    static ImFont* g_micro_icon_fonts[kUIFontVariantCount]{};
    static ImFont* g_weapon_icon_font = nullptr;

    static int UIFontVariantIndex() noexcept
    {
        return ImClamp(
            (s_UIScalePercent - 100) / 25,
            0,
            kUIFontVariantCount - 1);
    }

    static void ApplyUIFontVariant(ImGuiIO& io) noexcept
    {
        const int index = UIFontVariantIndex();
        g_ActiveFontVariant = index;
        set->poppins_selection = g_selection_fonts[index];
        set->poppins_widget = g_widget_fonts[index];
        set->icon = g_icon_fonts[index];
        set->inter_logo = g_logo_fonts[index];
        set->icon_child = g_child_icon_fonts[index];
        set->icon_cfg = g_config_icon_fonts[index];
        set->icon_micro = g_micro_icon_fonts[index];
        g_weapon_icon_font = g_weapon_icon_fonts[index];
        io.FontDefault = set->poppins_widget;
        io.FontGlobalScale = 1.f;
        if (ImGui::GetCurrentContext() &&
            set->poppins_widget &&
            set->poppins_widget->IsLoaded() &&
            GImGui->WithinFrameScope)
            ImGui::SetCurrentFont(set->poppins_widget);
    }

    ImFont* GetWeaponIconFont() noexcept
    {
        return g_weapon_icon_font;
    }

    namespace TypeRoles
    {
        ImFont* Micro() noexcept { return g_micro_fonts[g_ActiveFontVariant]; }
        ImFont* Body() noexcept { return g_body_fonts[g_ActiveFontVariant]; }
        ImFont* Label() noexcept { return g_label_fonts[g_ActiveFontVariant]; }
        ImFont* Title() noexcept { return g_selection_fonts[g_ActiveFontVariant]; }
        ImFont* Display() noexcept { return g_display_fonts[g_ActiveFontVariant]; }
        ImFont* Mono() noexcept { return g_mono_fonts[g_ActiveFontVariant]; }
        ImFont* Icon() noexcept { return g_icon_fonts[g_ActiveFontVariant]; }
        float Scale() noexcept { return kUIFontScales[g_ActiveFontVariant]; }
    }

    void InitializeFonts(ImGuiIO& io)
    {
        InitLeyawinGlobals();

        static const ImWchar kFontAwesomeRanges[] = {
            ICON_MIN_FA,
            ICON_MAX_FA,
            0
        };

        static const ImWchar kWeaponIconRanges[] = {
            0xE000,
            0xE100,
            0
        };

        // Mono only needs digits, units and punctuation.
        static const ImWchar kMonoRanges[] = {
            0x0020,
            0x00FF,
            0
        };

        constexpr float kMicroSize = 12.5f;
        constexpr float kLabelSize = 13.f;
        constexpr float kBodySize = 15.f;
        constexpr float kMonoSize = 14.5f;
        constexpr float kTitleSize = 17.f;
        constexpr float kDisplaySize = 23.f;

        ImFont* fallback = io.Fonts->AddFontDefault();
        io.Fonts->TexDesiredWidth = 4096;
        for (int i = 0; i < kUIFontVariantCount; ++i)
        {
            const float fontScale = kUIFontScales[i];
            ImFontConfig cfg;
            cfg.FontDataOwnedByAtlas = false;
            cfg.PixelSnapH = true;
            cfg.OversampleH = 2;
            cfg.OversampleV = 1;

            g_weapon_icon_fonts[i] =
                io.Fonts->AddFontFromMemoryTTF(
                    const_cast<unsigned char*>(
                        embedded_fonts::majestic_weapon_icons_ttf),
                    sizeof(
                        embedded_fonts::majestic_weapon_icons_ttf),
                    18.f * fontScale,
                    &cfg,
                    kWeaponIconRanges);
            g_selection_fonts[i] =
                io.Fonts->AddFontFromMemoryTTF(
                    const_cast<unsigned char*>(
                        butterfly_font_inter_semibold_ttf),
                    static_cast<int>(
                        sizeof(butterfly_font_inter_semibold_ttf)),
                    kTitleSize * fontScale,
                    &cfg,
                    io.Fonts->GetGlyphRangesCyrillic());
            g_widget_fonts[i] =
                io.Fonts->AddFontFromMemoryTTF(
                    const_cast<unsigned char*>(
                        butterfly_font_inter_medium_ttf),
                    static_cast<int>(
                        sizeof(butterfly_font_inter_medium_ttf)),
                    kBodySize * fontScale,
                    &cfg,
                    io.Fonts->GetGlyphRangesCyrillic());
            g_body_fonts[i] = g_widget_fonts[i];
            g_label_fonts[i] =
                io.Fonts->AddFontFromMemoryTTF(
                    const_cast<unsigned char*>(
                        butterfly_font_inter_medium_ttf),
                    static_cast<int>(
                        sizeof(butterfly_font_inter_medium_ttf)),
                    kLabelSize * fontScale,
                    &cfg,
                    io.Fonts->GetGlyphRangesCyrillic());
            g_display_fonts[i] =
                io.Fonts->AddFontFromMemoryTTF(
                    const_cast<unsigned char*>(
                        butterfly_font_inter_semibold_ttf),
                    static_cast<int>(
                        sizeof(butterfly_font_inter_semibold_ttf)),
                    kDisplaySize * fontScale,
                    &cfg,
                    io.Fonts->GetGlyphRangesCyrillic());
            g_micro_fonts[i] =
                io.Fonts->AddFontFromMemoryTTF(
                    const_cast<unsigned char*>(
                        butterfly_font_inter_regular_ttf),
                    static_cast<int>(
                        sizeof(butterfly_font_inter_regular_ttf)),
                    kMicroSize * fontScale,
                    &cfg,
                    io.Fonts->GetGlyphRangesCyrillic());
            g_mono_fonts[i] =
                io.Fonts->AddFontFromMemoryTTF(
                    const_cast<unsigned char*>(
                        butterfly_font_ibm_plex_mono_ttf),
                    static_cast<int>(
                        sizeof(butterfly_font_ibm_plex_mono_ttf)),
                    kMonoSize * fontScale,
                    &cfg,
                    kMonoRanges);

        ImFontConfig mergeCfg;
        mergeCfg.FontDataOwnedByAtlas = false;
        mergeCfg.MergeMode = true;
        mergeCfg.PixelSnapH = true;
            if (g_selection_fonts[i])
            {
                mergeCfg.DstFont = g_selection_fonts[i];
            io.Fonts->AddFontFromMemoryTTF(
                    const_cast<unsigned char*>(
                        Roboto_Regular_ttf),
                Roboto_Regular_ttf_len,
                    18.f * fontScale,
                &mergeCfg,
                io.Fonts->GetGlyphRangesCyrillic());
        }
            // IBM Plex Mono ships Latin-1 only here, so translated status
            // values rendered as question marks in the instrument bar until
            // the mono face got the same Cyrillic fallback as the rest.
            const struct { ImFont* font; float size; } kCyrillicTargets[] = {
                { g_widget_fonts[i], kBodySize },
                { g_label_fonts[i], kLabelSize },
                { g_display_fonts[i], kDisplaySize },
                { g_micro_fonts[i], kMicroSize },
                { g_mono_fonts[i], kMonoSize }
            };
            for (const auto& target : kCyrillicTargets)
            {
                if (!target.font)
                    continue;
                mergeCfg.DstFont = target.font;
            io.Fonts->AddFontFromMemoryTTF(
                    const_cast<unsigned char*>(
                        Roboto_Regular_ttf),
                Roboto_Regular_ttf_len,
                    target.size * fontScale,
                &mergeCfg,
                io.Fonts->GetGlyphRangesCyrillic());
        }

            ImFontConfig iconCfg = cfg;
            iconCfg.PixelSnapH = true;
            g_icon_fonts[i] =
                io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned char*>(FontAwesome_ttf),
            FontAwesome_ttf_len,
                    18.f * fontScale,
                    &iconCfg,
            kFontAwesomeRanges);
            g_logo_fonts[i] =
                io.Fonts->AddFontFromMemoryTTF(
                    inter_bold,
                    sizeof(inter_bold),
                    40.f * fontScale,
                    &cfg,
                    io.Fonts->GetGlyphRangesCyrillic());
            g_child_icon_fonts[i] =
                io.Fonts->AddFontFromMemoryTTF(
                    glypher_icon,
                    sizeof(glypher_icon),
                    14.f * fontScale,
                    &cfg,
                    io.Fonts->GetGlyphRangesCyrillic());
            g_config_icon_fonts[i] =
                io.Fonts->AddFontFromMemoryTTF(
                    cfg_icons,
                    sizeof(cfg_icons),
                    16.f * fontScale,
                    &cfg,
                    io.Fonts->GetGlyphRangesCyrillic());
            g_micro_icon_fonts[i] =
                io.Fonts->AddFontFromMemoryTTF(
                    glypher_icon,
                    sizeof(glypher_icon),
                    7.f * fontScale,
                    &cfg,
                    io.Fonts->GetGlyphRangesCyrillic());

            if (!g_widget_fonts[i])
                g_widget_fonts[i] = fallback;
            if (!g_selection_fonts[i])
                g_selection_fonts[i] = g_widget_fonts[i];
            if (!g_icon_fonts[i])
                g_icon_fonts[i] = g_widget_fonts[i];
            if (!g_logo_fonts[i])
                g_logo_fonts[i] = g_widget_fonts[i];
            if (!g_child_icon_fonts[i])
                g_child_icon_fonts[i] = g_widget_fonts[i];
            if (!g_config_icon_fonts[i])
                g_config_icon_fonts[i] = g_widget_fonts[i];
            if (!g_micro_icon_fonts[i])
                g_micro_icon_fonts[i] = g_widget_fonts[i];
            if (!g_weapon_icon_fonts[i])
                g_weapon_icon_fonts[i] = g_widget_fonts[i];
            if (!g_body_fonts[i])
                g_body_fonts[i] = g_widget_fonts[i];
            if (!g_label_fonts[i])
                g_label_fonts[i] = g_widget_fonts[i];
            if (!g_display_fonts[i])
                g_display_fonts[i] = g_selection_fonts[i];
            if (!g_micro_fonts[i])
                g_micro_fonts[i] = g_widget_fonts[i];
            if (!g_mono_fonts[i])
                g_mono_fonts[i] = g_widget_fonts[i];
        }
        ApplyUIFontVariant(io);
    }

    void InitializeStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = set->window_padding;
        style.WindowBorderSize = set->window_border_size;
        style.WindowRounding = set->window_rounding;
        style.ScrollbarSize = set->window_scrollbar_size;
        style.Colors[ImGuiCol_WindowBg] = clr->window_background;
        ApplyRawImGuiTheme(style);
    }

    void Watermark()
    {
        if (!s_Ready || !set->watermark)
            return;

        // The old watermark printed a fixed "RAGEMP / GTA V / DX11 / ONLINE"
        // strip that was true by coincidence at best. Show measured values.
        const MenuTheme::Palette& tokens = MenuTheme::Colors();
        const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
        const float scale = UIScale();
        ImGuiIO& io = ImGui::GetIO();

        char fps[24]{};
        _snprintf(fps, sizeof(fps) - 1, "%.0f fps", io.Framerate);
        const auto runtime = RageV8Runtime::GetStatus();
        const char* v8 =
            (runtime.state == RageV8Runtime::State::Ready &&
             runtime.rootCaptured)
                ? "v8 ready"
                : "v8 offline";
        const char* obs = ObsBypass::StatusText();

        const char* cells[] = { "BUTTERFLY", fps, v8, obs };
        constexpr int kCells = IM_ARRAYSIZE(cells);

        ImFont* mono = MenuType::Mono();
        if (!mono)
            return;
        const float fontSize = MenuType::MonoSize();
        const float padX = metrics.space3 * scale;
        const float padY = metrics.space2 * scale;
        const float gap = metrics.space3 * scale;

        float width = padX * 2.f;
        float cellW[kCells]{};
        for (int i = 0; i < kCells; ++i)
        {
            cellW[i] = mono->CalcTextSizeA(
                fontSize, FLT_MAX, 0.f, cells[i]).x;
            width += cellW[i];
            if (i)
                width += gap * 2.f;
        }
        const float height = fontSize + padY * 2.f;

        const ImVec2 boxMin(
            io.DisplaySize.x - width - metrics.space4 * scale,
            metrics.space4 * scale);
        const ImVec2 boxMax = boxMin + ImVec2(width, height);

        ImDrawList* draw = ImGui::GetForegroundDrawList();
        draw->AddRectFilled(
            boxMin,
            boxMax,
            gui->get_clr(tokens.panel, 0.92f),
            metrics.radiusPanel * scale);
        draw->AddRect(
            boxMin,
            boxMax,
            gui->get_clr(tokens.borderSubtle, 1.f),
            metrics.radiusPanel * scale,
            0,
            metrics.stroke);

        float x = boxMin.x + padX;
        for (int i = 0; i < kCells; ++i)
        {
            if (i)
            {
                draw->AddLine(
                    ImVec2(x + gap - 1.f, boxMin.y + padY),
                    ImVec2(x + gap - 1.f, boxMax.y - padY),
                    gui->get_clr(tokens.borderSubtle, 1.f),
                    metrics.stroke);
                x += gap * 2.f;
            }
            draw->AddText(
                mono,
                fontSize,
                ImVec2(x, boxMin.y + padY),
                gui->get_clr(
                    i == 0 ? tokens.accent.brand : tokens.textMuted, 1.f),
                cells[i]);
            x += cellW[i];
        }
    }

    void PushNotification(
        const char* title,
        const char* message,
        bool success,
        int durationMs)
    {
        InitLeyawinGlobals();
        notify->AddNotification(
            title ? title : "Notification",
            message ? message : "",
            durationMs,
            ImGui::GetColorU32(
            success
                    ? MenuTheme::Colors().success
                    : MenuTheme::Colors().error));
    }

    void SetAccountProfile(
        const char* name,
        std::int64_t expiresAtUnix) noexcept
    {
        lstrcpynA(
            s_AccountName,
            name && name[0] ? name : "User",
            static_cast<int>(sizeof(s_AccountName)));
        s_AccountExpiresAt = expiresAtUnix > 0 ? expiresAtUnix : 0;
    }



    // ------------------------------------------------------------------
    //  Shell
    //  Fixed icon rail (pin to expand), 44 px header with the page identity,
    //  flat canvas, and an instrument bar fed only by authoritative state.
    // ------------------------------------------------------------------

    static void DrawShellBackdrop(
        ImDrawList* draw,
        const ImVec2& pos,
        const ImVec2& size) noexcept
    {
        const MenuTheme::Palette& tokens = MenuTheme::Colors();
        const float rounding = set->window_rounding;

        draw->AddRectFilled(
            pos,
            pos + size,
            gui->get_clr(tokens.canvas, 1.f),
            rounding);

        if (set->window_gradient)
        {
            // One quiet vertical wash instead of a four-corner accent bloom.
            const ImVec4 top = ImLerp(
                tokens.canvas,
                tokens.panel,
                MenuTheme::IsLight() ? 0.55f : 0.75f);
            gui->add_rect_filled_multi_color(
                draw,
                pos,
                ImVec2(pos.x + size.x, pos.y + size.y * 0.55f),
                gui->get_clr(top, 1.f),
                gui->get_clr(top, 1.f),
                gui->get_clr(tokens.canvas, 0.f),
                gui->get_clr(tokens.canvas, 0.f),
                rounding,
                ImDrawFlags_RoundCornersTop);
        }

        draw->AddRect(
            pos,
            pos + size,
            gui->get_clr(tokens.borderStrong, 1.f),
            rounding,
            0,
            MenuTheme::Sizes().stroke);
    }

    static void DrawInstrumentBar(
        ImDrawList* draw,
        const ImVec2& min,
        const ImVec2& max,
        float contentStartX) noexcept
    {
        const MenuTheme::Palette& tokens = MenuTheme::Colors();
        const float scale = UIScale();
        ImFont* font = MenuType::Mono();
        ImFont* labelFont = MenuType::Micro();
        const float monoSize = MenuType::MonoSize() * 0.92f;
        const float labelSize = MenuType::MicroSize() * 0.92f;

        draw->AddLine(
            ImVec2(min.x, min.y),
            ImVec2(max.x, min.y),
            gui->get_clr(tokens.borderSubtle, 1.f),
            MenuTheme::Sizes().stroke);

        float x = contentStartX;
        const float centerY = (min.y + max.y) * 0.5f;

        auto slot = [&](const char* label,
                        const char* value,
                        const ImVec4& dot) {
            if (x > max.x - 40.f * scale)
                return;
            if (dot.w > 0.f)
            {
                draw->AddCircleFilled(
                    ImVec2(x + 3.f * scale, centerY),
                    3.f * scale,
                    gui->get_clr(dot, 1.f),
                    10);
                x += 11.f * scale;
            }
            const ImVec2 labelSz = labelFont->CalcTextSizeA(
                labelSize, FLT_MAX, 0.f, label);
            draw->AddText(
                labelFont,
                labelSize,
                ImVec2(x, centerY - labelSz.y * 0.5f),
                gui->get_clr(tokens.textFaint, 1.f),
                label);
            x += labelSz.x + 5.f * scale;
            const ImVec2 valueSz = font->CalcTextSizeA(
                monoSize, FLT_MAX, 0.f, value);
            draw->AddText(
                font,
                monoSize,
                ImVec2(x, centerY - valueSz.y * 0.5f),
                gui->get_clr(tokens.textMuted, 1.f),
                value);
            x += valueSz.x + 16.f * scale;
        };

        char buffer[64]{};

        // Frame-cost readouts are for working on the cheat, not for the
        // person using it; the numbers still go to the log either way.
        if (RAGEMP_DEV_UI && RuntimeProfiler::Enabled())
        {
            const RuntimeProfiler::Stats menuStats =
                RuntimeProfiler::GetStats(RuntimeProfiler::Scope::Menu);
            _snprintf(
                buffer,
                sizeof(buffer) - 1,
                "%.1f/%.1f ms",
                menuStats.p50Us / 1000.0,
                menuStats.p95Us / 1000.0);
            slot("UI", buffer, ImVec4(0.f, 0.f, 0.f, 0.f));

            const RuntimeProfiler::WorkCounters work =
                RuntimeProfiler::GetWorkCounters();
            _snprintf(
                buffer,
                sizeof(buffer) - 1,
                "%u/%u",
                work.drawnPeds,
                work.activePeds);
            slot(
                T("PEDS", "ПЕДЫ", "ПЕДИ"),
                buffer,
                ImVec4(0.f, 0.f, 0.f, 0.f));
        }

        // The bottom bar only carries the search and unload controls; the
        // loaded config is already named on the Configs page.
    }

    static void ApplyPreviewForcedTab() noexcept
    {
        if (s_PreviewForcedTab < 0)
            return;
        // Wait for the page transition to settle, then apply once: holding the
        // forced tab every frame made sub-tabs unclickable in the preview.
        if (s_AcceptedMainPage != s_MainPage)
            return;
        const int tab = s_PreviewForcedTab;
        s_PreviewForcedTab = -1;
        switch (s_AcceptedMainPage)
        {
        case MainPage::Combat:
            s_AimMode = ImClamp(tab, 0, 3);
            break;
        case MainPage::Visuals:
            s_VisualCategoryTab =
                ImClamp(tab, 0, static_cast<int>(VisualCategory::Count) - 1);
            break;
        case MainPage::Player:
            s_MiscSubTab = tab;
            break;
        case MainPage::Settings:
            s_ConfigSubTab = tab;
            break;
        default:
            break;
        }
    }

    static const char* PageDiagName(MainPage page) noexcept
    {
        switch (page)
        {
        case MainPage::Combat: return "page:aim";
        case MainPage::Weapons: return "page:weapons";
        case MainPage::Visuals: return "page:visuals";
        case MainPage::Scripts: return "page:scripts";
        case MainPage::Players: return "page:friends";
        case MainPage::Misc: return "page:around";
        case MainPage::Player: return "page:misc";
        case MainPage::Cars: return "page:cars";
        case MainPage::World: return "page:world";
        case MainPage::Server: return "page:server";
        case MainPage::Settings: return "page:configs";
        default: return "page:?";
        }
    }

    static void RenderActivePage(ImGuiStyle* style)
    {
        ApplyPreviewForcedTab();
        switch (s_AcceptedMainPage)
        {
        case MainPage::Combat:
            RenderCombatPage();
            break;
        case MainPage::Weapons:
            RenderWeaponsPage();
            break;
        case MainPage::Visuals:
            RenderVisualsPage();
            break;
        case MainPage::Scripts:
            ScriptBridge::RenderUI();
            break;
        case MainPage::Players:
            RenderPlayersPage();
            break;
        case MainPage::Misc:
            RenderAroundPage();
            break;
        case MainPage::Player:
            RenderPlayerPage();
            break;
        case MainPage::Cars:
            RenderCarsPage();
            break;
        case MainPage::World:
            RenderWorldPage();
            break;
        case MainPage::Settings:
            RenderConfigPage(style);
            break;
        case MainPage::Server:
            RenderServerPage();
            break;
        default:
            break;
        }
    }

    namespace
    {
        const char* volatile g_RenderPhase = "idle";
    }

    void SetRenderPhase(const char* phase) noexcept
    {
        g_RenderPhase = phase ? phase : "?";
    }

    const char* RenderPhase() noexcept
    {
        const char* phase = g_RenderPhase;
        return phase ? phase : "?";
    }

    void Render(bool& bOpen)
    {
        SetRenderPhase("enter");
        InitLeyawinGlobals();
        InitDefaultEspProfiles();

        if (!s_Ready) {
            InitializeStyle();
            ApplyLeyawinTheme(s_LightTheme);
            s_Ready = true;
        }

        SetRenderPhase("config-bootstrap");
        TickConfigBootstrap();

        const float scale = UIScale();
        const MenuTheme::Metrics& metrics = MenuTheme::Sizes();
        const MenuTheme::Palette& tokens = MenuTheme::Colors();
        ImGuiIO& io = ImGui::GetIO();
        ImGuiStyle& globalStyle = ImGui::GetStyle();
        ApplyRawImGuiTheme(globalStyle);
        ApplyUIFontVariant(io);

        s_MenuAlpha = MenuMotion::Approach(
            s_MenuAlpha,
            bOpen ? 1.f : 0.f,
            MenuMotion::kPage);
        set->menu_alpha = s_MenuAlpha;
        SetRenderPhase("notifications");
        notify->DrawNotifications();
        SetRenderPhase("quant-widgets");
        QuantTools::RenderWidgets();
        SetRenderPhase("ukraine-widgets");
        UkraineTools::RenderWidgets();
        ApplyRawImGuiTheme(globalStyle);

        struct OpenCloseMotion final
        {
            float offsetY = 0.f;
            bool wasOpen = false;
        };
        OpenCloseMotion& openMotion =
            LeyawinAnim<OpenCloseMotion, 8802>().get(0x0F40u);
        if (bOpen && !openMotion.wasOpen)
        {
            openMotion.offsetY = 6.f;
            s_BrandShine = 0.f;
        }
        openMotion.wasOpen = bOpen;
        s_BrandShine = MenuMotion::Advance(s_BrandShine, true, 0.9f);
        openMotion.offsetY = MenuMotion::Approach(
            openMotion.offsetY,
            bOpen ? 0.f : 6.f,
            MenuMotion::kPage);

        if (s_MenuAlpha < 0.005f && !bOpen) {
            PedPreview::SetActive(false);
            SetRenderPhase("closed");
            return;
        }

        SetRenderPhase("backdrop");
        ImDrawList* draw_bg = ImGui::GetBackgroundDrawList();
        draw_bg->AddRectFilled(
            { 0, 0 },
            io.DisplaySize,
            gui->get_clr(clr->black_color, 0.35f * s_MenuAlpha),
            0.f);

        POINT currentMousePos;
        GetCursorPos(&currentMousePos);
        if (set->particle_cursor && s_MenuAlpha > 0.05f &&
            (set->particle_clicked
                ? ImGui::IsMouseDown(0)
                : (currentMousePos.x != s_PrevMousePos.x ||
                   currentMousePos.y != s_PrevMousePos.y))) {
            particle->EmitParticles(ImGui::GetMousePos());
            s_PrevMousePos = currentMousePos;
        }

        ImVec2 menuSize(
            set->window_size.x * scale,
            set->window_size.y * scale);
        if (io.DisplaySize.x > 0.f && io.DisplaySize.y > 0.f) {
            menuSize.x = ImMin(menuSize.x, io.DisplaySize.x * 0.95f);
            menuSize.y = ImMin(menuSize.y, io.DisplaySize.y * 0.95f);
            menuSize.x = ImMax(menuSize.x, 320.f);
            menuSize.y = ImMax(menuSize.y, 240.f);
        }
        static bool s_SettingsOpen = false;
        static float s_SettingsFlyoutFade = 0.f;
        s_SettingsFlyoutFade = MenuMotion::Approach(
            s_SettingsFlyoutFade,
            s_SettingsOpen ? 1.f : 0.f,
            MenuMotion::kSelection);

        gui->push_style_var(ImGuiStyleVar_Alpha, s_MenuAlpha);

        // A standalone panel: it sits beside the shell with a real gap and can
        // be dragged wherever the user wants it.
        if (s_SettingsFlyoutFade > 0.004f)
        {
            const ImGuiWindow* host = ImGui::FindWindowByName("window");
            const ImVec2 hostPos = host ? host->Pos : ImVec2(0.f, 0.f);
            const ImVec2 hostSize = host ? host->Size : menuSize;
            const float ease =
                MenuMotion::EaseStandard(s_SettingsFlyoutFade);
            const float margin = 12.f * scale;
            const float panelW = ImMin(
                268.f * scale,
                ImMax(180.f * scale, io.DisplaySize.x - margin * 2.f));
            const float roomRight =
                io.DisplaySize.x - (hostPos.x + hostSize.x) - margin * 2.f;
            const float defaultX = roomRight >= panelW
                ? hostPos.x + hostSize.x + margin
                : ImClamp(
                    hostPos.x - panelW - margin,
                    margin,
                    ImMax(margin, io.DisplaySize.x - panelW - margin));
            const float panelTop = ImClamp(
                hostPos.y + 40.f * scale,
                margin,
                ImMax(margin, io.DisplaySize.y - 200.f * scale));

            ImGui::SetNextWindowPos(
                ImVec2(defaultX, panelTop),
                ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(
                ImVec2(panelW, 0.f),
                ImGuiCond_Always);
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(panelW, 0.f),
                ImVec2(
                    panelW,
                    ImMax(
                        160.f * scale,
                        io.DisplaySize.y - panelTop - margin)));
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, s_MenuAlpha * ease);
            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowPadding,
                ImVec2(metrics.space3, metrics.space3) * scale);
            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowRounding,
                metrics.radiusPanel * scale);
            ImGui::PushStyleVar(
                ImGuiStyleVar_WindowBorderSize,
                metrics.stroke);
            ImGui::PushStyleColor(
                ImGuiCol_WindowBg,
                gui->get_clr(tokens.panel, 0.99f));
            ImGui::PushStyleColor(
                ImGuiCol_Border,
                gui->get_clr(tokens.borderStrong, 1.f));
            ImGuiWindowFlags flyoutFlags =
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoSavedSettings;
            if (!s_SettingsOpen)
                flyoutFlags |= ImGuiWindowFlags_NoInputs;
            SetRenderPhase("flyout-begin");
            if (ImGui::Begin("##menu_settings_flyout", nullptr, flyoutFlags))
                RenderMenuSettingsFlyout();
            SetRenderPhase("flyout-end");
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(4);
        }

        // Set right before the shell opens: the flyout above submits its own
        // window first, and a pending SetNextWindow* is consumed by whichever
        // Begin() runs next — the shell used to lose its size and position to
        // the panel and collapse.
        ImGui::SetNextWindowSize(menuSize, ImGuiCond_Always);
        // Raising the UI scale grows the window around its saved top-left, so
        // without this the instrument bar walked off the bottom of the screen.
        if (ImGuiWindow* host = ImGui::FindWindowByName("window"))
        {
            const ImVec2 limit(
                ImMax(0.f, io.DisplaySize.x - menuSize.x),
                ImMax(0.f, io.DisplaySize.y - menuSize.y));
            const ImVec2 clamped(
                ImClamp(host->Pos.x, 0.f, limit.x),
                ImClamp(host->Pos.y, 0.f, limit.y));
            if (clamped.x != host->Pos.x || clamped.y != host->Pos.y)
                ImGui::SetNextWindowPos(clamped, ImGuiCond_Always);
        }

        SetRenderPhase("shell-begin");
        gui->begin("window", nullptr, set->window_flags);
        {
            const ImVec2 pos = ImGui::GetWindowPos();
            const ImVec2 size = ImGui::GetWindowSize();
            ImGuiStyle* style = &ImGui::GetStyle();
            ImDrawList* draw = ImGui::GetWindowDrawList();

            const ImVec4 accentBase = MenuTheme::AccentBase();
            if (accentBase.x != func->accent_color[0] ||
                accentBase.y != func->accent_color[1] ||
                accentBase.z != func->accent_color[2])
            {
                MenuTheme::SetAccentBase(
                    ImVec4(
                        func->accent_color[0],
                        func->accent_color[1],
                        func->accent_color[2],
                        1.f));
            }

            style->WindowPadding = set->window_padding;
            style->WindowBorderSize = set->window_border_size;
            style->WindowRounding = set->window_rounding;
            style->ScrollbarSize = metrics.stroke * 6.f * scale;
            style->Colors[ImGuiCol_WindowBg] = tokens.canvas;

            const bool showQuantTab = QuantTools::IsQuantServer();
            const bool showUkraineTab = UkraineTools::IsUkraineServer();
            const bool showFiveRpTab = FiveRpFarmBot::IsFiveRpServer();
            if (s_MainPage == MainPage::Weapons ||
                s_AcceptedMainPage == MainPage::Weapons)
            {
                s_MainPage = MainPage::Combat;
                s_AcceptedMainPage = MainPage::Combat;
                s_AimMode = 3;
            }
            if (!IsPageAvailable(s_MainPage) ||
                !IsPageAvailable(s_AcceptedMainPage))
            {
                s_MainPage = MainPage::Combat;
                s_AcceptedMainPage = MainPage::Combat;
            }

            // One search entry point: CTRL+F or the button in the bottom bar.
            static bool s_SearchOpen = false;
            static bool s_FocusGlobalSearch = false;
            static bool s_ConfirmUnload = false;
            static char s_SearchQuery[64]{};
            static float s_SearchPanelFade = 0.f;
            static float s_SearchHeight = 0.f;
            static float s_UnloadHot = 0.f;
            static float s_UnloadFade = 0.f;
            // Captured from the live dialog so the backdrop can dim around it
            // instead of over it.
            static ImVec2 s_UnloadDialogMin{};
            static ImVec2 s_UnloadDialogMax{};
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
            {
                s_SearchOpen = true;
                s_FocusGlobalSearch = true;
            }
            s_SearchPanelFade = MenuMotion::Approach(
                s_SearchPanelFade,
                s_SearchOpen ? 1.f : 0.f,
                MenuMotion::kSelection);
            s_UnloadFade = MenuMotion::Approach(
                s_UnloadFade,
                s_ConfirmUnload ? 1.f : 0.f,
                MenuMotion::kSelection);
            DrawShellBackdrop(draw, pos, size);

            // ---- Top navigation ----------------------------------------
            // One row of categories; the active category slides its children
            // out on a second row underneath. No side rail.
            const float padX = 16.f * scale;
            const float headerH = 54.f * scale;
            const float avatarSize = 28.f * scale;

            ImFont* brandFont = MenuType::Title();
            const float brandSize = MenuType::TitleSize();
            const ImVec2 wordSz = brandFont->CalcTextSizeA(
                brandSize, FLT_MAX, 0.f, kBrandName);
            const ImVec2 wordPos(
                pos.x + padX + 14.f * scale,
                pos.y + (headerH - wordSz.y) * 0.5f);
            // A highlight sweeps the wordmark once per open: the base text sits
            // dimmed while a band of overlapping clip rects stacks up to full
            // white as it passes, which fakes a gradient without a shader.
            const bool shining = s_BrandShine < 1.f;
            draw->AddText(
                brandFont,
                brandSize,
                wordPos,
                gui->get_clr(tokens.textPrimary, shining ? 0.46f : 1.f),
                kBrandName);
            if (shining)
            {
                const float travel = wordSz.x + 60.f * scale;
                const float head =
                    wordPos.x - 30.f * scale +
                    travel * MenuMotion::EaseStandard(s_BrandShine);
                constexpr int kBands = 5;
                const float bandHalf = 26.f * scale;
                for (int band = kBands; band >= 1; --band)
                {
                    const float half =
                        bandHalf * static_cast<float>(band) /
                        static_cast<float>(kBands);
                    draw->PushClipRect(
                        ImVec2(head - half, wordPos.y - 4.f * scale),
                        ImVec2(
                            head + half,
                            wordPos.y + wordSz.y + 4.f * scale),
                        true);
                    draw->AddText(
                        brandFont,
                        brandSize,
                        wordPos,
                        gui->get_clr(tokens.textPrimary, 0.24f),
                        kBrandName);
                    draw->AddText(
                        brandFont,
                        brandSize,
                        wordPos,
                        gui->get_clr(tokens.accent.tint, 0.10f),
                        kBrandName);
                    draw->PopClipRect();
                }
            }
            const float brandRight = wordPos.x + wordSz.x;

            constexpr int kRootCount = static_cast<int>(DestRoot::Count);
            const DestRoot currentRoot = RootForPage(s_MainPage);
            // The child row belongs to the page that is actually on screen.
            // Following the requested page instead made the strip vanish on the
            // click, so the old page jumped up before it had even faded out.
            const DestRoot shownRootId = RootForPage(s_AcceptedMainPage);
            const DestRootInfo& activeRoot =
                kDestRoots[static_cast<int>(shownRootId)];

            const float catH = 34.f * scale;
            const float catInset = 4.f * scale;
            const float catLeft = brandRight + 22.f * scale;
            // Reserve the right cluster: the settings gear.
            const float catRight =
                pos.x + size.x - padX - avatarSize - 18.f * scale;
            const float catAvail = ImMax(200.f * scale, catRight - catLeft);
            const float segW = ImMin(
                126.f * scale,
                (catAvail - catInset * 2.f) /
                    static_cast<float>(kRootCount));
            const float catTotal = segW * static_cast<float>(kRootCount);
            const float catX0 =
                catLeft + (catAvail - catTotal) * 0.5f;
            const float catY0 = pos.y + (headerH - catH) * 0.5f;

            draw->AddRectFilled(
                ImVec2(catX0 - catInset, catY0 - catInset),
                ImVec2(catX0 + catTotal + catInset, catY0 + catH + catInset),
                gui->get_clr(tokens.base, 0.92f),
                metrics.radiusPanel * scale);
            draw->AddRect(
                ImVec2(catX0 - catInset, catY0 - catInset),
                ImVec2(catX0 + catTotal + catInset, catY0 + catH + catInset),
                gui->get_clr(tokens.borderSubtle, 1.f),
                metrics.radiusPanel * scale,
                0,
                metrics.stroke);

            // Roots and children share ids ("configs" names both a category
            // and its first child), so each row owns its own id scope.
            ImGui::PushID("nav_root");
            for (int root = 0; root < kRootCount; ++root)
            {
                const DestRootInfo& info = kDestRoots[root];
                ImGui::SetCursorScreenPos(
                    ImVec2(
                        catX0 + segW * static_cast<float>(root),
                        catY0));
                if (MenuUi::NavTab(
                        info.id,
                        info.icon,
                        T(info.titleEn, info.titleRu, info.titleUa),
                        currentRoot == static_cast<DestRoot>(root),
                        true,
                        ImVec2(segW, catH)))
                {
                    if (info.childCount > 0)
                        ApplyDestChild(info.children[0]);
                    else
                    {
                        s_MainPage = info.leafPage;
                        if (info.leafPage == MainPage::Scripts)
                        {
#if RAGEMP_EVENT_LOGGER
                            ScriptBridge::OpenPage(
                                static_cast<DestRoot>(root) ==
                                        DestRoot::Logger
                                    ? ScriptBridge::Page::EventLogger
                                    : ScriptBridge::Page::Scripts);
#else
                            ScriptBridge::OpenPage(
                                ScriptBridge::Page::Scripts);
#endif
                        }
                    }
                }
                // No tooltip: the tab label already names the category, and the
                // hint covered the row underneath it.
            }
            ImGui::PopID();

            // ---- Child row ---------------------------------------------
            struct SubRowMotion final
            {
                float open = 0.f;
                float slide = 1.f;
                int shownRoot = -1;
                int drawnRoot = -1;
            };
            SubRowMotion& subRow =
                LeyawinAnim<SubRowMotion, 8811>().get(0x0F55u);
            if (subRow.shownRoot != static_cast<int>(shownRootId))
            {
                subRow.shownRoot = static_cast<int>(shownRootId);
                subRow.slide = 0.f;
            }
            if (activeRoot.childCount > 0)
                subRow.drawnRoot = static_cast<int>(shownRootId);
            subRow.slide = MenuMotion::Advance(
                subRow.slide, true, MenuMotion::kSelection);
            // Linear time with an ease: the exponential tail of Approach left a
            // few pixels of height creeping for several frames, which read as a
            // stutter while the page underneath was settling.
            subRow.open = MenuMotion::Advance(
                subRow.open,
                activeRoot.childCount > 0,
                MenuMotion::kSelection);

            const float subStripH = 40.f * scale;
            const float subGap = 6.f * scale;
            const float subOpen = MenuMotion::EaseStandard(subRow.open);
            const float subH = (subStripH + subGap) * subOpen;
            // While the row retracts it keeps painting the children it was
            // opened with, so the strip slides away instead of blinking out and
            // leaving an empty band behind.
            const DestRootInfo& stripRoot =
                activeRoot.childCount > 0 ||
                    subRow.drawnRoot < 0
                    ? activeRoot
                    : kDestRoots[subRow.drawnRoot];
            if (subRow.open > 0.02f && stripRoot.childCount > 0)
            {
                const float ease =
                    MenuMotion::EaseStandard(subRow.slide) * subOpen;
                const ImVec2 stripMin(
                    pos.x + padX,
                    pos.y + headerH + subGap * subOpen);
                const ImVec2 stripMax(
                    pos.x + size.x - padX,
                    stripMin.y + subStripH);
                // Clipped to the animated height so the row reads as sliding
                // out from under the category it belongs to.
                draw->PushClipRect(
                    ImVec2(stripMin.x, pos.y + headerH),
                    ImVec2(stripMax.x, pos.y + headerH + subH),
                    true);
                draw->AddRectFilled(
                    stripMin,
                    stripMax,
                    gui->get_clr(tokens.base, 0.92f * subOpen),
                    metrics.radiusPanel * scale);
                draw->AddRect(
                    stripMin,
                    stripMax,
                    gui->get_clr(tokens.borderSubtle, subOpen),
                    metrics.radiusPanel * scale,
                    0,
                    metrics.stroke);

                const float inner = 4.f * scale;
                const float childW =
                    (stripMax.x - stripMin.x - inner * 2.f) /
                    static_cast<float>(stripRoot.childCount);
                const float childH = subStripH - inner * 2.f;
                const float lift = (1.f - ease) * 12.f * scale;
                ImGui::PushStyleVar(
                    ImGuiStyleVar_Alpha, style->Alpha * ease);
                ImGui::PushID("nav_child");
                for (int i = 0; i < stripRoot.childCount; ++i)
                {
                    const DestChild& child = stripRoot.children[i];
                    ImGui::SetCursorScreenPos(
                        ImVec2(
                            stripMin.x + inner +
                                childW * static_cast<float>(i),
                            stripMin.y + inner - lift));
                    if (MenuUi::NavTab(
                            child.id,
                            child.icon,
                            T(child.titleEn, child.titleRu, child.titleUa),
                            IsChildActive(child),
                            false,
                            ImVec2(childW, childH)) &&
                        activeRoot.childCount > 0)
                    {
                        ApplyDestChild(child);
                    }
                }
                ImGui::PopID();
                ImGui::PopStyleVar();
                draw->PopClipRect();
            }

            const float navH = headerH + subH;
            draw->AddLine(
                ImVec2(pos.x, pos.y + navH),
                ImVec2(pos.x + size.x, pos.y + navH),
                gui->get_clr(tokens.borderSubtle, 1.f),
                metrics.stroke);

            // Account, subscription and customisation all live behind this
            // gear, so the top bar keeps a single control.
            {
                const ImVec2 gearMin(
                    pos.x + size.x - padX - avatarSize,
                    pos.y + (headerH - avatarSize) * 0.5f);
                const ImVec2 gearMax = gearMin + ImVec2(avatarSize, avatarSize);
                ImGui::SetCursorScreenPos(gearMin);
                if (ImGui::InvisibleButton(
                        "##menu_settings_toggle",
                        ImVec2(avatarSize, avatarSize)))
                    s_SettingsOpen = !s_SettingsOpen;
                const bool gearHot =
                    ImGui::IsItemHovered() || s_SettingsOpen;
                if (gearHot)
                {
                    draw->AddRectFilled(
                        gearMin,
                        gearMax,
                        gui->get_clr(
                            s_SettingsOpen ? tokens.raised : tokens.hover,
                            0.92f),
                        metrics.radiusControl * scale);
                }
                ImFont* gearFont = set->icon;
                if (gearFont)
                {
                    const float gearGlyph = 15.f * scale;
                    const ImVec2 gearSz = gearFont->CalcTextSizeA(
                        gearGlyph, FLT_MAX, 0.f, ICON_FA_GEAR);
                    draw->AddText(
                        gearFont,
                        gearGlyph,
                        ImVec2(
                            IM_ROUND(
                                gearMin.x + (avatarSize - gearSz.x) * 0.5f),
                            IM_ROUND(
                                gearMin.y + (avatarSize - gearSz.y) * 0.5f)),
                        gui->get_clr(
                            gearHot
                                ? tokens.textPrimary
                                : tokens.textMuted,
                            1.f),
                        ICON_FA_GEAR);
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip(
                        "%s",
                        T("Account and menu settings",
                          "Аккаунт и настройки меню",
                          "Акаунт і налаштування меню"));
                }
            }

            // ---- Content ------------------------------------------------
            set->selection_alpha = ImClamp(
                set->selection_alpha +
                    (MenuMotion::Delta() / MenuMotion::kPage) *
                        (s_MainPage == s_AcceptedMainPage ? 1.f : -1.f),
                0.f,
                1.f);
            // Cross-fade instead of fading fully out and back in: the old
            // through-zero swap read as a stutter on every page change.
            if (set->selection_alpha <= 0.35f &&
                s_MainPage != s_AcceptedMainPage)
                s_AcceptedMainPage = s_MainPage;

            struct ContentMotion final
            {
                MainPage lastPage = MainPage::Count;
                float offsetY = 0.f;
            };
            ContentMotion& motion =
                LeyawinAnim<ContentMotion, 8801>().get(
                    ImGui::GetID("##tab_motion"));
            const bool pageChanged =
                motion.lastPage != s_AcceptedMainPage;
            if (pageChanged)
            {
                motion.lastPage = s_AcceptedMainPage;
                motion.offsetY = 4.f;
            }
            motion.offsetY = MenuMotion::Approach(
                motion.offsetY,
                0.f,
                MenuMotion::kPage);
            const float contentNudge =
                motion.offsetY + openMotion.offsetY;

            const float instrumentH = s_ShowInstruments
                ? metrics.instrumentHeight * scale
                : 0.f;
            // The search hint and the instrument readout share one bottom
            // band, so neither of them floats over a card.
            const float hintH = 30.f * scale;
            const float bottomH = ImMax(instrumentH, hintH + 12.f * scale);
            const ImVec2 contentSize(
                size.x,
                size.y - navH - bottomH);
            gui->set_cursor_pos(ImVec2(0.f, navH));
            ImGui::PushStyleColor(
                ImGuiCol_ChildBg,
                ImVec4(0.f, 0.f, 0.f, 0.f));
            // Aim and Visuals scroll inside their own columns. Letting the
            // shared content child scroll too meant the wheel could leave it a
            // few pixels off the top, and the cards crept under the nav row.
            gui->begin_content(
                contentSize,
                (s_AcceptedMainPage == MainPage::Visuals ||
                 s_AcceptedMainPage == MainPage::Combat)
                    ? (ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoScrollWithMouse)
                    : 0);
            {
                if (pageChanged)
                    ImGui::SetScrollY(0.f);
                gui->push_style_var(
                    ImGuiStyleVar_Alpha,
                    ImMax(set->selection_alpha, 0.35f) * style->Alpha);
                if (contentNudge > 0.05f)
                    ImGui::SetCursorPosY(
                        ImGui::GetCursorPosY() + contentNudge);
                if (s_AcceptedMainPage != MainPage::Visuals)
                    PedPreview::SetActive(false);
                SetRenderPhase(PageDiagName(s_AcceptedMainPage));
                RenderActivePage(style);
                SetRenderPhase("page-done");
                gui->pop_style_var();
            }
            gui->end_content();
            ImGui::PopStyleColor();

            if (s_AcceptedMainPage == MainPage::Combat)
            {
                std::uint32_t addedWeapon = 0;
                if (WeaponManagerUi::RenderAddWeaponWindow(&addedWeapon) &&
                    addedWeapon)
                {
                    s_AimPresetKind = AimPresetKind::Weapon;
                    s_AimPresetWeapon = addedWeapon;
                }
            }

            // ---- Bottom bar ---------------------------------------------
            SetRenderPhase("bottom-bar");
            const float barTop = pos.y + size.y - bottomH;
            const float barCenterY = barTop + bottomH * 0.5f;

            auto drawBarIcon = [&](const ImVec2& center,
                                   const char* icon,
                                   ImU32 color,
                                   float glyph) noexcept
            {
                ImFont* iconFont = set->icon;
                if (!iconFont)
                    return;
                const ImVec2 sz =
                    iconFont->CalcTextSizeA(glyph, FLT_MAX, 0.f, icon);
                draw->AddText(
                    iconFont,
                    glyph,
                    ImVec2(
                        IM_ROUND(center.x - sz.x * 0.5f),
                        IM_ROUND(center.y - sz.y * 0.5f)),
                    color,
                    icon);
            };

            ImFont* mono = MenuType::Mono();
            const float monoSize = MenuType::MonoSize();
            const char* chord = "CTRL+F";
            const ImVec2 chordSz =
                mono->CalcTextSizeA(monoSize, FLT_MAX, 0.f, chord);
            const float glyphSize = 15.f * scale;
            const float searchBtnW =
                15.f * scale + glyphSize + 8.f * scale + chordSz.x;
            const ImVec2 searchBtnMin(
                pos.x + padX,
                barCenterY - hintH * 0.5f);
            bool searchButtonHovered = false;
            {
                ImGui::SetCursorScreenPos(searchBtnMin);
                if (ImGui::InvisibleButton(
                        "##open_search",
                        ImVec2(searchBtnW, hintH)))
                {
                    s_SearchOpen = !s_SearchOpen;
                    s_FocusGlobalSearch = s_SearchOpen;
                    if (!s_SearchOpen)
                        s_SearchQuery[0] = 0;
                }
                searchButtonHovered = ImGui::IsItemHovered();
                const ImVec2 searchBtnMax =
                    searchBtnMin + ImVec2(searchBtnW, hintH);
                // Bare glyph and chord by default; the surface only appears
                // once the overlay is actually open.
                if (s_SearchPanelFade > 0.01f)
                {
                    draw->AddRectFilled(
                        searchBtnMin,
                        searchBtnMax,
                        gui->get_clr(
                            tokens.raised, 0.92f * s_SearchPanelFade),
                        metrics.radiusControl * scale);
                    draw->AddRect(
                        searchBtnMin,
                        searchBtnMax,
                        gui->get_clr(
                            tokens.borderSubtle, s_SearchPanelFade),
                        metrics.radiusControl * scale,
                        0,
                        metrics.stroke);
                }
                drawBarIcon(
                    ImVec2(
                        searchBtnMin.x + 7.f * scale + glyphSize * 0.5f,
                        barCenterY),
                    ICON_FA_MAGNIFYING_GLASS,
                    gui->get_clr(tokens.textPrimary, 1.f),
                    glyphSize);
                draw->AddText(
                    mono,
                    monoSize,
                    ImVec2(
                        searchBtnMin.x + 15.f * scale + glyphSize,
                        barCenterY - chordSz.y * 0.5f),
                    gui->get_clr(tokens.textPrimary, 1.f),
                    chord);
            }

            {
                const float unloadS = 26.f * scale;
                const char* unloadLabel =
                    T("Unload", "Выгрузить", "Вивантажити");
                ImFont* unloadFont = set->poppins_widget
                    ? set->poppins_widget
                    : ImGui::GetFont();
                const float unloadLabelSize = MenuType::BodySize();
                const ImVec2 unloadLabelSz = unloadFont->CalcTextSizeA(
                    unloadLabelSize, FLT_MAX, 0.f, unloadLabel);
                const ImVec2 iconMin(
                    pos.x + size.x - padX - unloadS,
                    barCenterY - unloadS * 0.5f);
                const ImVec2 iconMax = iconMin + ImVec2(unloadS, unloadS);

                // The pill grows leftwards on hover; geometry follows last
                // frame's value so the widening area keeps its own hover.
                const float grow = unloadLabelSz.x + 14.f * scale;
                const ImVec2 pillMin(
                    iconMin.x - grow * s_UnloadHot,
                    iconMin.y);
                ImGui::SetCursorScreenPos(pillMin);
                if (ImGui::InvisibleButton(
                        "##unload_button",
                        ImVec2(iconMax.x - pillMin.x, unloadS)))
                    s_ConfirmUnload = true;
                s_UnloadHot = MenuMotion::Approach(
                    s_UnloadHot,
                    ImGui::IsItemHovered() ? 1.f : 0.f,
                    MenuMotion::kHover);

                draw->AddRectFilled(
                    pillMin,
                    iconMax,
                    gui->get_clr(
                        tokens.error, 0.14f + 0.14f * s_UnloadHot),
                    metrics.radiusControl * scale);
                draw->AddRect(
                    pillMin,
                    iconMax,
                    gui->get_clr(
                        tokens.error, 0.45f + 0.55f * s_UnloadHot),
                    metrics.radiusControl * scale,
                    0,
                    metrics.stroke);
                if (s_UnloadHot > 0.01f)
                {
                    draw->PushClipRect(pillMin, iconMax, true);
                    draw->AddText(
                        unloadFont,
                        unloadLabelSize,
                        ImVec2(
                            pillMin.x + 11.f * scale,
                            barCenterY - unloadLabelSz.y * 0.5f),
                        gui->get_clr(tokens.error, s_UnloadHot),
                        unloadLabel);
                    draw->PopClipRect();
                }
                drawBarIcon(
                    ImVec2(iconMin.x + unloadS * 0.5f, barCenterY),
                    ICON_FA_POWER_OFF,
                    gui->get_clr(tokens.error, 1.f),
                    13.f * scale);
            }

            if (s_ShowInstruments)
            {
                DrawInstrumentBar(
                    draw,
                    ImVec2(pos.x, barTop),
                    ImVec2(pos.x + size.x, pos.y + size.y),
                    searchBtnMin.x + searchBtnW + 16.f * scale);
            }

            if (s_SearchPanelFade > 0.004f)
            {
                const float searchEase =
                    MenuMotion::EaseStandard(s_SearchPanelFade);
                const float overlayW =
                    ImMin(440.f * scale, size.x - padX * 2.f);
                const float inputH = metrics.controlHeight * scale;
                const float overlayPad = 8.f * scale;
                const float rowH = 38.f * scale;
                const float spacing = ImGui::GetStyle().ItemSpacing.y;

                // Results are ranked before the window opens so the panel can
                // grow towards the height they need instead of snapping to it.
                RankedSearchResult results[8]{};
                int resultCount = 0;
                if (s_SearchQuery[0])
                {
                    resultCount = BuildRankedSearchResults(
                        s_SearchQuery,
                        showQuantTab,
                        showUkraineTab,
                        showFiveRpTab,
                        results,
                        IM_ARRAYSIZE(results));
                }
                const bool searchEmpty =
                    s_SearchQuery[0] && resultCount <= 0;
                float bodyH = 0.f;
                if (searchEmpty)
                    bodyH = spacing + 88.f * scale;
                else if (resultCount > 0)
                {
                    bodyH = spacing +
                        static_cast<float>(resultCount) * (rowH + spacing) -
                        spacing;
                }
                const float targetH = overlayPad * 2.f + inputH + bodyH;
                s_SearchHeight = MenuMotion::Approach(
                    s_SearchHeight, targetH, MenuMotion::kSelection);

                ImGui::SetNextWindowPos(
                    ImVec2(
                        pos.x + padX,
                        barTop - 10.f * scale +
                            (1.f - searchEase) * 12.f * scale),
                    ImGuiCond_Always,
                    ImVec2(0.f, 1.f));
                ImGui::SetNextWindowSize(
                    ImVec2(overlayW, s_SearchHeight),
                    ImGuiCond_Always);
                ImGui::PushStyleVar(
                    ImGuiStyleVar_Alpha, style->Alpha * searchEase);
                ImGui::PushStyleVar(
                    ImGuiStyleVar_WindowPadding,
                    ImVec2(overlayPad, overlayPad));
                ImGui::PushStyleVar(
                    ImGuiStyleVar_WindowRounding,
                    metrics.radiusPanel * scale);
                ImGui::PushStyleVar(
                    ImGuiStyleVar_WindowBorderSize,
                    metrics.stroke * 2.f);
                ImGui::PushStyleColor(
                    ImGuiCol_WindowBg,
                    gui->get_clr(tokens.raised, 1.f));
                // Accent outline marks the overlay as the focused surface;
                // against a card the neutral border was invisible.
                ImGui::PushStyleColor(
                    ImGuiCol_Border,
                    gui->get_clr(tokens.borderFocus, 1.f));
                ImGuiWindowFlags overlayFlags =
                    ImGuiWindowFlags_NoDecoration |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoSavedSettings |
                    ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse;
                if (!s_SearchOpen)
                    overlayFlags |= ImGuiWindowFlags_NoInputs;
                bool overlayHovered = false;
                if (ImGui::Begin(
                        "##search_overlay",
                        nullptr,
                        overlayFlags))
                {
                    overlayHovered = ImGui::IsWindowHovered(
                        ImGuiHoveredFlags_ChildWindows |
                        ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                    if (s_FocusGlobalSearch)
                    {
                        ImGui::SetKeyboardFocusHere();
                        s_FocusGlobalSearch = false;
                    }
                    const bool submitted = MenuUi::SearchRow(
                        "overlay_ranked_search",
                        T("Search pages and settings...",
                          "Поиск страниц и настроек...",
                          "Пошук сторінок і налаштувань..."),
                        s_SearchQuery,
                        sizeof(s_SearchQuery),
                        ImVec2(
                            ImGui::GetContentRegionAvail().x,
                            inputH),
                        ImGuiInputTextFlags_EnterReturnsTrue);

                    if (submitted && resultCount > 0)
                    {
                        NavigateToSearchDestination(results[0].destination);
                        s_SearchQuery[0] = 0;
                        s_SearchOpen = false;
                    }
                    else if (searchEmpty)
                    {
                        MenuUi::EmptyState(
                            "search_no_results",
                            ICON_FA_MAGNIFYING_GLASS,
                            T("No nearby match",
                              "Совпадений нет",
                              "Збігів немає"),
                            T("Try a shorter title",
                              "Попробуйте короче",
                              "Спробуйте коротший запит"),
                            72.f);
                    }
                    else
                    {
                        for (int resultIndex = 0;
                             resultIndex < resultCount;
                             ++resultIndex)
                        {
                            const int destination =
                                results[resultIndex].destination;
                            MenuUi::ListRowDesc row{};
                            row.title =
                                SearchDestinationTitle(destination);
                            row.meta =
                                SearchDestinationCategory(destination);
                            row.accentDot = resultIndex == 0;
                            row.height = 38.f;
                            if (MenuUi::DrawListRow(
                                    SearchDestinationId(destination),
                                    row))
                            {
                                NavigateToSearchDestination(destination);
                                s_SearchQuery[0] = 0;
                                s_SearchOpen = false;
                            }
                        }
                    }
                }
                ImGui::End();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(4);

                if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                {
                    s_SearchOpen = false;
                    s_SearchQuery[0] = 0;
                }
                // Clicking anywhere outside dismisses it, so the button never
                // stays lit waiting for a second press.
                if (s_SearchOpen &&
                    ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                    !overlayHovered &&
                    !searchButtonHovered)
                {
                    s_SearchOpen = false;
                    s_SearchQuery[0] = 0;
                }
            }
            else
            {
                s_SearchHeight = 0.f;
            }

            if (s_UnloadFade > 0.004f)
            {
                const float ease = MenuMotion::EaseStandard(s_UnloadFade);
                // Shade the four bands around the dialog rect, never the
                // dialog itself. The rect comes from the live window below.
                if (ImDrawList* fg = ImGui::GetForegroundDrawList())
                {
                    const ImU32 shade =
                        IM_COL32(0, 0, 0, static_cast<int>(150.f * ease));
                    const ImVec2 mn = s_UnloadDialogMin;
                    const ImVec2 mx = s_UnloadDialogMax;
                    fg->PushClipRect(pos, pos + size, true);
                    if (mx.x <= mn.x || mx.y <= mn.y)
                    {
                        fg->AddRectFilled(pos, pos + size, shade);
                    }
                    else
                    {
                        fg->AddRectFilled(
                            pos,
                            ImVec2(pos.x + size.x, mn.y),
                            shade);
                        fg->AddRectFilled(
                            ImVec2(pos.x, mx.y),
                            pos + size,
                            shade);
                        fg->AddRectFilled(
                            ImVec2(pos.x, mn.y),
                            ImVec2(mn.x, mx.y),
                            shade);
                        fg->AddRectFilled(
                            ImVec2(mx.x, mn.y),
                            ImVec2(pos.x + size.x, mx.y),
                            shade);
                    }
                    fg->PopClipRect();
                }
                const float dialogW = ImMin(340.f * scale, size.x - padX * 2.f);
                ImGui::PushStyleVar(
                    ImGuiStyleVar_Alpha, style->Alpha * ease);
                ImGui::SetNextWindowPos(
                    ImVec2(
                        pos.x + size.x * 0.5f,
                        pos.y + size.y * 0.5f + (1.f - ease) * 14.f * scale),
                    ImGuiCond_Always,
                    ImVec2(0.5f, 0.5f));
                ImGui::SetNextWindowSize(
                    ImVec2(dialogW, 0.f),
                    ImGuiCond_Always);
                ImGui::PushStyleVar(
                    ImGuiStyleVar_WindowPadding,
                    ImVec2(metrics.space4, metrics.space4) * scale);
                ImGui::PushStyleVar(
                    ImGuiStyleVar_WindowRounding,
                    metrics.radiusPanel * scale);
                ImGui::PushStyleVar(
                    ImGuiStyleVar_WindowBorderSize,
                    metrics.stroke);
                ImGui::PushStyleColor(
                    ImGuiCol_WindowBg,
                    gui->get_clr(tokens.panel, 1.f));
                ImGui::PushStyleColor(
                    ImGuiCol_Border,
                    gui->get_clr(tokens.borderStrong, 1.f));
                if (s_ConfirmUnload)
                    ImGui::SetNextWindowFocus();
                ImGuiWindowFlags dialogFlags =
                    ImGuiWindowFlags_NoDecoration |
                    ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoSavedSettings;
                // While fading out the dialog must not eat clicks.
                if (!s_ConfirmUnload)
                    dialogFlags |= ImGuiWindowFlags_NoInputs;
                if (ImGui::Begin(
                        "##unload_confirm",
                        nullptr,
                        dialogFlags))
                {
                    s_UnloadDialogMin = ImGui::GetWindowPos();
                    s_UnloadDialogMax =
                        s_UnloadDialogMin + ImGui::GetWindowSize();
                    ImDrawList* dlg = ImGui::GetWindowDrawList();
                    const ImVec2 titlePos = ImGui::GetCursorScreenPos();
                    const char* title = T(
                        "Unload from game?",
                        "Выгрузить из игры?",
                        "Вивантажити з гри?");
                    const ImVec2 titleSz =
                        MenuType::Title()->CalcTextSizeA(
                            MenuType::TitleSize(), FLT_MAX, 0.f, title);
                    dlg->AddText(
                        MenuType::Title(),
                        MenuType::TitleSize(),
                        titlePos,
                        gui->get_clr(tokens.textPrimary, 1.f),
                        title);
                    const char* detail = T(
                        "The menu closes and the module detaches.",
                        "Меню закроется, модуль отключится от игры.",
                        "Меню закриється, модуль відключиться від гри.");
                    dlg->AddText(
                        MenuType::Micro(),
                        MenuType::MicroSize(),
                        ImVec2(
                            titlePos.x,
                            titlePos.y + titleSz.y + 4.f * scale),
                        gui->get_clr(tokens.textMuted, 1.f),
                        detail);
                    ImGui::Dummy(
                        ImVec2(
                            0.f,
                            titleSz.y + MenuType::MicroSize() +
                                metrics.space5 * scale));

                    const float btnGap = metrics.space2 * scale;
                    const float btnW =
                        (ImGui::GetContentRegionAvail().x - btnGap) * 0.5f;
                    const float btnH = 32.f * scale;
                    if (MenuUi::ActionButton(
                            "unload_cancel",
                            T("Cancel", "Отмена", "Скасувати"),
                            ImVec2(btnW, btnH)))
                        s_ConfirmUnload = false;
                    ImGui::SameLine(0.f, btnGap);
                    if (MenuUi::ActionButton(
                            "unload_confirm",
                            T("Unload", "Выгрузить", "Вивантажити"),
                            ImVec2(btnW, btnH),
                            true,
                            ICON_FA_POWER_OFF))
                    {
                        s_ConfirmUnload = false;
                        hooks::RequestUnload();
                    }
                }
                ImGui::End();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(4);

                if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                    s_ConfirmUnload = false;
            }
            else
            {
                s_UnloadDialogMin = ImVec2();
                s_UnloadDialogMax = ImVec2();
            }
        }

        SetRenderPhase("shell-end");
        gui->end();

        // The panel is built before the shell so its geometry can follow the
        // host rect, so lift it above the shell once both exist.
        if (s_SettingsFlyoutFade > 0.004f)
        {
            if (ImGuiWindow* flyout =
                    ImGui::FindWindowByName("##menu_settings_flyout"))
                ImGui::BringWindowToDisplayFront(flyout);
        }

        SetRenderPhase("popups-front");
        gui->bring_popups_to_front();

        SetRenderPhase("particles");
        particle->UpdateParticles(io.DeltaTime);
        gui->pop_style_var();

        io.FontGlobalScale = 1.f;
        SetRenderPhase("done");
    }

    bool IsVisible()
    {
        return s_MenuAlpha > 0.01f;
    }

    bool NeedsPlayerCache() noexcept
    {
        if (!IsVisible())
            return false;
        return s_AcceptedMainPage == MainPage::Players;
    }
}
