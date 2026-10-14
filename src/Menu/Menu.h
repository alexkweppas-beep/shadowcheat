#pragma once
#include <cstdint>

 
//  RAGEMP — Modern Minimalist ImGui Cheat Menu
//
//  Anti-cheat safe:
//    • NO dynamic allocation in the render loop (no new/malloc/STL)
//    • NO disk I/O — all fonts are embedded as byte arrays in headers
//    • Pure ImGui + ImDrawList rendering
//
//  Public API consumed by Hooks.cpp:
//    Menu::InitializeStyle()   — Apply the minimalist dark theme
//    Menu::InitializeFonts()   — Load embedded fonts, stash pointers
//    Menu::Render(bool&)       — Draw the full UI (call from hkPresent)
//    Menu::Watermark()         — Minimal on-screen watermark (always visible)
//
//  UI Scale:
//    s_UIScalePercent — 75..200 in steps of 25, selects DPI font + layout
//      scale. Each step has its own rasterised font atlas.
 

struct ImGuiIO;
struct ImFont;

namespace Menu
{
    enum class Language : int
    {
        English = 0,
        Russian,
        Ukrainian
    };

    // Stable navigation identity. Values are explicit so rail order can be
    // rearranged without moving routes. Server hosts the per-server tools.
    enum class MainPage : std::uint8_t
    {
        Combat = 0,
        Weapons = 1,
        Visuals = 2,
        Players = 3,
        Scripts = 4,
        Misc = 5,
        Server = 6,
        Settings = 7,
        Player = 8,
        Cars = 9,
        World = 10,
        Count = 11
    };

    enum AimHitboxBit : std::uint32_t
    {
        AimHitboxHead = 1u << 0,
        AimHitboxNeck = 1u << 1,
        AimHitboxChest = 1u << 2,
        AimHitboxPelvis = 1u << 3,
        AimHitboxLeftUpperArm = 1u << 4,
        AimHitboxLeftForearm = 1u << 5,
        AimHitboxLeftHand = 1u << 6,
        AimHitboxRightUpperArm = 1u << 7,
        AimHitboxRightForearm = 1u << 8,
        AimHitboxRightHand = 1u << 9,
        AimHitboxLeftThigh = 1u << 10,
        AimHitboxLeftCalf = 1u << 11,
        AimHitboxLeftFoot = 1u << 12,
        AimHitboxRightThigh = 1u << 13,
        AimHitboxRightCalf = 1u << 14,
        AimHitboxRightFoot = 1u << 15
    };

    static constexpr int kAimHitboxCount = 16;
    static constexpr int kAimCurvePoints = 5;
    static constexpr int kMaxWeaponOverrides = 96;
    static constexpr int kMaxWeaponAttachments = 12;

    struct AimCurve final
    {
        // x is fixed 0,.25,.5,.75,1 — only y is edited (multiplier / scale).
        float y[kAimCurvePoints]{1.f, 1.f, 1.f, 1.f, 1.f};
    };

    inline float SampleAimCurve(const AimCurve& curve, float x) noexcept
    {
        if (!(x > 0.f))
            return curve.y[0];
        if (x >= 1.f)
            return curve.y[kAimCurvePoints - 1];
        const float scaled = x * static_cast<float>(kAimCurvePoints - 1);
        const int i0 = static_cast<int>(scaled);
        const int i1 = i0 + 1 < kAimCurvePoints ? i0 + 1 : i0;
        const float t = scaled - static_cast<float>(i0);
        return curve.y[i0] + (curve.y[i1] - curve.y[i0]) * t;
    }

    enum class EspProfileId : int
    {
        Players = 0,
        FactionPlayers,
        FamilyPlayers,
        Npc,
        Admin,
        Animal,
        // Appended rather than inserted: saved configs key their per-profile
        // settings by this index, so reordering would silently move everyone's
        // colours onto the wrong scope.
        Friends,
        Count
    };

    // Visual navigation is separate from ped profile routing: the ped profile
    // is a scope selector inside Peds, not a tab of its own.
    enum class VisualCategory : int
    {
        Peds = 0,
        World,
        Radar,
        Count
    };

    static constexpr int kMaxVisualModelFilters = 24;

    // Where a snap line starts. Shared by ped, vehicle and object ESP.
    enum class EspSnapOrigin : int
    {
        BottomCenter = 0,
        TopCenter,
        Crosshair,
        Count
    };

    struct EspElementLayout final
    {
        int side{};   // EspProjected::Side (0 top .. 3 right)
        int order{};  // stack order within side
        float size{1.f};
    };

    struct EspProfileSettings final
    {
        bool enabled{};
        bool box{};
        bool healthBar{};
        bool armorBar{};
        bool nameTag{};
        bool factionTag{};
        bool familyTag{};
        bool skeleton{};
        bool distance{};
        bool weaponTag{};
        bool dynamicIdTag{};
        bool staticIdTag{};
        bool showDead{true};
        bool visibleCheck{};
        // How much of a target behind cover the visible check recolours.
        int  hiddenStyle{}; // EspHiddenStyle
        bool radarOverlay{};
        // Snap line from a screen edge to the target. Origin is shared by every
        // category so the lines all converge on the same point.
        bool snapLine{};
        int  snapLineOrigin{}; // EspSnapOrigin
        // Legacy positions (kept for migrate / old UI paths).
        int healthBarPos{}; // EspHealthBarPosition
        int armorBarPos{};  // EspHealthBarPosition
        int nameTagPos{};    // EspTagSide
        int factionTagPos{}; // EspTagSide
        int distancePos{1};  // EspTagSide (default Below)
        int weaponTagPos{1}; // EspTagSide
        int boxStyle{};     // EspBoxStyle
        // Corner radius in pixels, shared by all three box styles. Zero is the
        // square box every build shipped with, so nobody's saved look changes.
        float boxRounding{};
        float boxThickness{1.8f};
        float skeletonThickness{1.5f};
        int healthStyle{};  // EspBarStyle
        int armorStyle{};   // EspBarStyle
        int weaponDisplay{}; // EspWeaponDisplay
        // Projected layout: name,faction,family,distance,weapon,health,armor,dynamicId,staticId
        EspElementLayout layouts[9]{};
        bool layoutsMigrated{};
        bool healthShowValue{true};
        bool armorShowValue{true};
        float boxColor[4]{0.f, 0.f, 0.f, 1.f};
        float boxFillColor[4]{0.1f, 0.1f, 0.1f, 1.f};
        float nameColor[4]{0.f, 0.f, 0.f, 1.f};
        float factionColor[4]{1.f, 0.85f, 0.25f, 1.f};
        float familyColor[4]{0.85f, 0.45f, 1.0f, 1.f};
        float healthColor[4]{0.f, 0.f, 0.f, 1.f};
        float armorColor[4]{0.25f, 0.55f, 1.0f, 1.f};
        float skeletonColor[4]{0.f, 0.f, 0.f, 1.f};
        float distanceColor[4]{0.f, 0.f, 0.f, 1.f};
        float weaponColor[4]{0.95f, 0.75f, 0.35f, 1.f};
        float dynamicIdColor[4]{0.7f, 0.85f, 1.f, 1.f};
        float staticIdColor[4]{1.f, 0.9f, 0.45f, 1.f};
        float deadColor[4]{0.75f, 0.2f, 0.2f, 1.f};
        float hiddenColor[4]{0.f, 0.f, 0.f, 1.f};
        float snapLineColor[4]{0.55f, 0.80f, 1.f, 0.85f};
        float radarColor[4]{0.95f, 0.25f, 0.25f, 1.f};
        float radarRange{150.f};
        float radarPosX{-1.f}; // -1 = use minimap default
        float radarPosY{-1.f};
        float radarSize{0.f};  // 0 = use minimap default
    };

    struct RadarSettings final
    {
        bool enabled{};
        bool showPlayers{true};
        bool showFriends{true};
        bool showNpcs{};
        bool showVehicles{};
        float range{150.f};
        float posX{-1.f};
        float posY{-1.f};
        float size{0.f};
        float backgroundOpacity{0.67f};
        float markerSize{4.5f};
        float directionLength{11.f};
        float enemyColor[4]{0.95f, 0.25f, 0.25f, 1.f};
        float friendColor[4]{0.31f, 0.63f, 1.f, 1.f};
        float npcColor[4]{0.72f, 0.72f, 0.72f, 1.f};
        float vehicleColor[4]{1.f, 0.72f, 0.25f, 1.f};
    };

    struct VehicleVisualSettings final
    {
        bool enabled{};
        bool box{true};
        bool name{true};
        bool distance{true};
        bool remoteId{};
        bool health{};
        bool trunk{};
        bool showOccupied{true};
        bool showEmpty{true};
        bool showCars{true};
        bool showBikes{true};
        bool showBoats{true};
        bool showAircraft{true};
        bool showOther{true};
        bool snapLine{};
        int  snapLineOrigin{}; // EspSnapOrigin
        float maxDistance{350.f};
        float boxColor[4]{0.30f, 0.72f, 1.f, 1.f};
        float textColor[4]{0.88f, 0.94f, 1.f, 1.f};
        float snapLineColor[4]{0.30f, 0.72f, 1.f, 0.85f};
        std::uint32_t includeModels[kMaxVisualModelFilters]{};
        int includeModelCount{};
        std::uint32_t excludeModels[kMaxVisualModelFilters]{};
        int excludeModelCount{};
    };

    struct ObjectVisualSettings final
    {
        bool enabled{};
        bool box{true};
        bool name{true};
        bool distance{true};
        bool remoteId{};
        bool showObjects{true};
        bool showPickups{true};
        // Bypasses the shown-list without emptying it: keep the list you
        // built and still see everything for a moment.
        bool showAll{};
        bool snapLine{};
        int  snapLineOrigin{}; // EspSnapOrigin
        float maxDistance{180.f};
        float objectColor[4]{0.82f, 0.62f, 1.f, 1.f};
        float pickupColor[4]{0.38f, 0.92f, 0.55f, 1.f};
        float textColor[4]{0.92f, 0.92f, 0.96f, 1.f};
        float snapLineColor[4]{0.82f, 0.62f, 1.f, 0.85f};
        std::uint32_t includeModels[kMaxVisualModelFilters]{};
        int includeModelCount{};
        std::uint32_t excludeModels[kMaxVisualModelFilters]{};
        int excludeModelCount{};
    };

    // Per-weapon-group aim settings (tabs under Aimbot).
    // General is the fallback; other groups apply when useCustom is set.
    struct AimWeaponProfile final
    {
        bool useCustom{}; // ignored for General

        bool  vecEnabled{};
        int   vecKey{0x02}; // VK_RBUTTON
        bool  vecPrediction{};
        float vecPredictionScale{1.f};
        bool  vecDrawFov{};
        float vecFov{40.f};
        float vecFovColor[3]{0.96f, 0.62f, 0.80f};
        bool  vecNearest{true};      // nearest ped/bone among candidates
        bool  vecSticky{true};
        bool  vecBoneNearestAny{};   // any skeleton bone (not only selected)
        bool  vecVisible{true};
        bool  vecBones[kAimHitboxCount]{
            true, false, true, false}; // head + chest defaults
        float vecSmoothness{1.f};
        bool  vecRandomSmooth{};
        float vecRandomAmt{0.25f};
        bool  vecSmartSmooth{};
        bool  vecCamShake{};
        float vecShakeAmt{0.20f};

        // Dynamic FOV + curves (Weapon Manager / advanced).
        bool  dynamicFov{};
        float dynamicFovAdsMul{0.75f};
        float dynamicFovSpeedExpand{0.35f};
        bool  useFovCurve{};       // false = single slider multiplier
        float fovDistanceMul{1.f}; // used when !useFovCurve
        AimCurve fovDistanceCurve{};
        bool  useSmoothCurve{};
        float smoothDistanceMul{1.f};
        AimCurve smoothDistanceCurve{};

        // Humanize (vector).
        bool  humanize{};
        float reactionDelayMs{};
        float microJitter{0.15f};
        float overshoot{0.10f};
        float targetSwitchDelayMs{80.f};
        float fatigue{0.20f};

        bool  silentEnabled{};
        int   silentKey{0x02}; // VK_RBUTTON
        bool  silentDrawFov{};
        float silentFov{40.f};
        float silentFovColor[3]{0.35f, 0.85f, 1.0f};
        bool  silentMark{};
        // How the marked target is drawn. See EspTargetMarkStyle.
        int   silentMarkStyle{};
        bool  silentLock{};
        bool  silentNearest{true};
        bool  silentBoneNearestAny{};
        bool  silentVisible{true};
        bool  silentWallshot{};
        bool  silentBones[kAimHitboxCount]{
            true, false, true, false};
        bool  silentHitchance{true};
        float silentHitchancePct{100.f};

        // Silent suite.
        bool  silentPrediction{};
        float silentLeadScale{1.0f};
        float silentReactionMs{40.f};
        float silentMissBias{}; // 0..1 toward bone edge
        bool  useSilentHcCurve{};
        float silentHcDistanceMul{1.f};
        AimCurve silentHitchanceCurve{};

        // Trigger fires when the crosshair is on one of the enabled hitboxes.
        // There is no radius: the test is against the ped's projected
        // silhouette, which already scales with distance.
        bool  triggerEnabled{};
        // 0 = always armed while enabled. Anything else has to be held.
        int   triggerKey{};
        float triggerDelayMs{35.f};
        float triggerCooldownMs{90.f};
        bool  triggerVisible{true};
        bool  triggerBones[kAimHitboxCount]{
            true, true, true, true};

        // Shared filters for vector + silent.
        bool  ignoreDead{true};
        bool  ignoreNpc{true};
        bool  ignoreAdmins{true};
        bool  ignoreFriends{};

        // Per-weapon combat patches (writer may be quarantined).
        bool  noSpread{};
        float noSpreadPct{100.f};
        bool  noRecoil{};
        float noRecoilPct{100.f};
        bool  reloadSpeed{};
        float reloadSpeedPct{100.f};
    };

    struct WeaponOverrideSlot final
    {
        std::uint32_t hash{};
        bool active{}; // when true, replaces group profile for this weapon
        AimWeaponProfile profile{};
        std::uint32_t attachments[kMaxWeaponAttachments]{};
        int attachmentCount{};
    };

    extern bool s_Aim_IgnoreFriends;

    static constexpr int kAimWeaponGroupCount = 7; // matches WeaponGroups::Group::Count

    const AimWeaponProfile& GetActiveAimProfile();
    AimWeaponProfile& GetAimProfile(int groupIndex);
    int GetActiveAimWeaponGroup();

    WeaponOverrideSlot* FindWeaponOverride(std::uint32_t hash) noexcept;
    WeaponOverrideSlot* GetOrCreateWeaponOverride(std::uint32_t hash) noexcept;
    bool RemoveWeaponOverride(std::uint32_t hash) noexcept;
    float EvaluateDynamicFov(
        const AimWeaponProfile& profile,
        float distanceMeters,
        bool isAds,
        float targetSpeed) noexcept;
    float EvaluateSmoothness(
        const AimWeaponProfile& profile,
        float distanceMeters,
        float crosshairErrorPx) noexcept;
    float EvaluateSilentHitchance(
        const AimWeaponProfile& profile,
        float distanceMeters) noexcept;

    void OpenWeaponsTab() noexcept;
    void SetWeaponManagerOpen(bool open) noexcept;
    bool IsWeaponManagerOpen() noexcept;
    void ToggleWeaponManager() noexcept;
    void RenderWeaponManagerWindow() noexcept;

    void InitializeStyle();
    void InitializeFonts(ImGuiIO& io);
    ImFont* GetWeaponIconFont() noexcept;
    void Render(bool& bOpen);
    void Watermark();

    // The frame is wrapped in SEH, so a fault is swallowed and everything after
    // it silently stops drawing. The shell records the stage it is in so the
    // handler can name the code that raised the fault.
    void SetRenderPhase(const char* phase) noexcept;
    const char* RenderPhase() noexcept;

    // How deep ImGui's own stacks are, so a swallowed fault can be unwound
    // back to it. Without this a fault inside a page left our window open and
    // style values pushed, and everything drawn afterwards in that frame —
    // including the separate on-screen panels — ended up nested inside the
    // menu instead of on the screen.
    struct UiStackDepth final
    {
        int windows{};
        int styleVars{};
        int colors{};
        int fonts{};
        int ids{};
        bool valid{};
    };

    UiStackDepth CaptureUiStackDepth() noexcept;
    void UnwindUiStack(const UiStackDepth& depth) noexcept;
    bool IsVisible();
    bool NeedsPlayerCache() noexcept;
    const char* Translate(
        const char* english,
        const char* russian,
        const char* ukrainian);
    void PushNotification(
        const char* title,
        const char* message,
        bool success,
        int durationMs = 5000);
    void SetAccountProfile(
        const char* name,
        std::int64_t expiresAtUnix = 0) noexcept;

    // UI scale state (percent: 100, 125, 150, 175, 200).
    extern int s_UIScalePercent;
    extern Language s_Language;
    extern EspProfileSettings
        s_EspProfiles[static_cast<int>(EspProfileId::Count)];
    extern RadarSettings s_RadarSettings;
    extern VehicleVisualSettings s_VehicleVisualSettings;
    extern ObjectVisualSettings s_ObjectVisualSettings;

    // A name the user gave to a model hash, so a filter list reads as words
    // instead of eight hex digits. Kept apart from the filter lists and keyed
    // by the hash, because a hash means the same object wherever it appears:
    // naming it once names it in every list that mentions it.
    inline constexpr int kMaxModelLabels = 96;

    struct ModelLabel final
    {
        std::uint32_t hash{};
        char name[28]{};
    };

    extern ModelLabel s_ModelLabels[kMaxModelLabels];
    extern int s_ModelLabelCount;

    // A model the user double-clicked on screen while the menu was open, so
    // the filter editor can open on it already filled in. Zero when nothing is
    // waiting; the editor clears it as it takes it.
    extern std::uint32_t s_PickedObjectModel;

    // Asking for the window has to happen where the window is drawn: an
    // ImGui popup is keyed by the id stack it was opened from, and the page
    // sits inside child windows, so a request raised there never matches the
    // one the shell begins. The page raises this instead.
    extern bool s_ObjectFilterOpenRequest;

    // Empty string when the hash has no name, never null.
    const char* ModelLabelFor(std::uint32_t hash) noexcept;
    // An empty or blank name removes the entry.
    void SetModelLabel(std::uint32_t hash, const char* name) noexcept;

    // The shown-list starts with the pickups worth seeing on this server. They
    // are seeded exactly once and the fact is written to the config, so
    // deleting one keeps it deleted instead of having it come back on the next
    // launch.
    extern bool s_ObjectFiltersSeeded;
    void SeedDefaultObjectFilters() noexcept;
    extern int s_VisualCategoryTab;
    // Ped profile scope inside VisualCategory::Peds (EspProfileId).
    extern int s_VisualPedProfile;
    extern char s_AdminVariableKey[32];
    extern int s_AdminLevelThreshold;
    extern int s_AimWeaponTab;
    extern AimWeaponProfile s_AimWeaponProfiles[kAimWeaponGroupCount];
    extern WeaponOverrideSlot s_WeaponOverrides[kMaxWeaponOverrides];
    extern int s_WeaponOverrideCount;
    extern bool s_WeaponManagerOpen;

    // Legacy globals mirror General profile (config / older call sites).
    extern bool s_Aim_VisibleCheck;
    extern bool s_Aim_EnableSilent;
    extern bool s_Aim_Nearest;
    extern bool s_Aim_HitboxHead;
    extern bool s_Aim_HitboxNeck;
    extern bool s_Aim_HitboxChest;
    extern bool s_Aim_HitboxPelvis;
    extern float s_Aim_FOV;
    extern bool s_Aim_EnableHitchance;
    extern float s_Aim_Hitchance;
    extern bool s_Aim_LockOnLast;
    extern bool s_Aim_IgnoreDead;
    extern bool s_Aim_IgnoreNpc;
    extern bool s_Aim_IgnoreAdmins;
    extern bool s_Aim_DrawFov;
    extern bool s_Aim_MarkTarget;
    extern int s_Aim_TargetBone;
    extern float s_Aim_FovColor[3];
    extern float s_Aim_MarkColor[3];
    extern float s_Vec_FovColor[3];

    extern bool s_Vec_EnableVector;
    extern bool s_Vec_VisibleCheck;
    extern bool s_Vec_Nearest;
    extern bool s_Vec_HitboxHead;
    extern bool s_Vec_HitboxNeck;
    extern bool s_Vec_HitboxChest;
    extern bool s_Vec_HitboxPelvis;
    extern int s_Vec_ActivationKey;
    extern float s_Vec_FOV;
    extern float s_Vec_Smoothness;
    extern bool s_Vec_RandomSmooth;
    extern float s_Vec_RandomSmoothAmt;
    extern bool s_Vec_EnableSmartSmooth;
    extern bool s_Vec_CameraShake;
    extern float s_Vec_CameraShakeAmt;
    extern bool s_Vec_DrawFov;
}
