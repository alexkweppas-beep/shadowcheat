#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "imgui.h"

#include <cstddef>

// Shared Leyawin UI grammar used across Menu pages / Weapon Manager.
namespace MenuUi
{
    // sameLine=true: place after previous item. false: draw at current cursor.
    void HelpIcon(
        const char* tipEn,
        const char* tipRu,
        const char* tipUa,
        bool sameLine = true) noexcept;

    void LabelWithHelp(
        const char* label,
        const char* tipEn,
        const char* tipRu,
        const char* tipUa) noexcept;

    bool CheckWithHelp(
        const char* label,
        bool* value,
        const char* tipEn,
        const char* tipRu,
        const char* tipUa,
        bool bindable = true) noexcept;

    void SliderWithHelp(
        const char* label,
        float* value,
        float mn,
        float mx,
        const char* fmt,
        const char* tipEn,
        const char* tipRu,
        const char* tipUa) noexcept;

    void SectionTitle(const char* label) noexcept;

    bool NavRailItem(
        const char* id,
        const char* icon,
        const char* label,
        bool selected,
        const ImVec2& size,
        float railExpand,
        float iconCenterX = 0.f) noexcept;

    void NavRailGroupLabel(
        const char* label,
        const ImVec2& size,
        float railExpand) noexcept;

    void NavRailAccount(
        const ImVec2& avatarMin,
        float avatarSize,
        const char* name,
        const char* subscription,
        float railExpand) noexcept;

    // Navigation segment. primary=true is a top-row category (selected is a
    // filled cobalt pill); primary=false is a child row segment (selected is
    // a raised pill with an accent glyph).
    bool NavTab(
        const char* id,
        const char* icon,
        const char* label,
        bool selected,
        bool primary,
        const ImVec2& size) noexcept;

    void DrawWorkspaceFrost(
        const ImVec2& min,
        const ImVec2& max,
        float amount) noexcept;

    bool PageChip(
        const char* id,
        const char* label,
        bool selected,
        const ImVec2& size) noexcept;

    struct ChipDesc final
    {
        const char* id = "";
        const char* label = "";
    };

    // Responsive, wrapping chip strip. IDs are explicit and never depend on
    // localized labels. Returns true when the selected value changed.
    bool ChipStrip(
        const char* id,
        const ChipDesc* chips,
        int count,
        int* selected,
        float minimumWidth = 104.f,
        float height = 34.f) noexcept;

    struct IconTabDesc final
    {
        const char* id = "";
        const char* icon = "";
        const char* label = "";
    };

    // Underlined segmented tabs. Falls back to icon-only when cramped.
    bool IconTabStrip(
        const char* id,
        const IconTabDesc* tabs,
        int count,
        int* selected) noexcept;

    inline bool SegmentedTabs(
        const char* id,
        const IconTabDesc* tabs,
        int count,
        int* selected) noexcept
    {
        return IconTabStrip(id, tabs, count, selected);
    }

    // Navigation sub-tab row: one track with equal segments, matching the
    // category sub-row the shell draws under the top bar.
    bool SubNavTabs(
        const char* id,
        const IconTabDesc* tabs,
        int count,
        int* selected,
        float height = 40.f) noexcept;

    // Second-level scope switch. Pills, not underlines, so a page can nest
    // two tab rows without them competing for the same rank.
    bool ChipTabs(
        const char* id,
        const IconTabDesc* tabs,
        int count,
        int* selected) noexcept;

    // Every settings page uses the same layout contract: content starts at the
    // pane's left edge and flows into as many equal columns as the width
    // allows. Pages used to disagree — some filled the width, some sat centred
    // with half the pane empty — which read as if the layout were arbitrary.
    //
    // Returns the column count actually used. Callers may always call
    // NextPageColumn(); with a single column it just adds vertical spacing.
    int BeginPageColumns(
        const char* id,
        int desired = 2,
        float minColumnW = 300.f) noexcept;
    void NextPageColumn() noexcept;
    void EndPageColumns() noexcept;

    void InspectorSection(const char* label) noexcept;

    // Section = title + subtitle card. Pair with EndSection.
    void BeginInspectorIsland(
        const char* id,
        const char* label,
        float topGap = 10.f,
        const char* subtitle = nullptr) noexcept;
    void EndInspectorIsland() noexcept;

    inline void BeginSection(
        const char* id,
        const char* label,
        float topGap = 10.f) noexcept
    {
        BeginInspectorIsland(id, label, topGap);
    }

    inline void EndSection() noexcept
    {
        EndInspectorIsland();
    }

    void DrawLanguageFlag(
        ImDrawList* draw,
        const ImVec2& min,
        const ImVec2& max,
        int language,
        float alpha = 1.f) noexcept;

    struct FeatureRowDesc final
    {
        const char* icon = nullptr;
        const char* label = "";
        bool* enabled = nullptr;
        float* color = nullptr; // RGBA, optional
        bool overflow = false;
        bool bindable = true;
        float iconScale = 1.f;
    };

    struct FeatureRowResult final
    {
        bool overflow = false;
        bool toggled = false;
    };

    // The one setting row: icon, label, optional colour chip and overflow,
    // trailing toggle. The whole row is the hit target.
    FeatureRowResult FeatureRow(
        const char* id,
        const FeatureRowDesc& desc) noexcept;
    using SettingRowDesc = FeatureRowDesc;
    using SettingRowResult = FeatureRowResult;

    inline SettingRowResult SettingRow(
        const char* id,
        const SettingRowDesc& desc) noexcept
    {
        return FeatureRow(id, desc);
    }

    bool BeginOverflowPopup(
        const char* rowId,
        float width = 248.f) noexcept;
    void EndOverflowPopup() noexcept;

    // Dismiss the popup from inside it, for entries that open something else.
    void CloseOverflowPopup() noexcept;

    // One script in a library list: name with a timestamp chip, author line,
    // then favourite, overflow and the run button. Overflow opens the same
    // popup BeginOverflowPopup() reads.
    struct ScriptCardDesc final
    {
        const char* title = "";
        const char* stamp = nullptr;
        const char* subtitle = nullptr;
        const char* runLabel = nullptr;
        bool favorite = false;
        bool canRun = true;
        // Pass the live name buffer to allow renaming by double-clicking it.
        char* editableTitle = nullptr;
        int editableCapacity = 0;
    };

    struct ScriptCardResult final
    {
        bool run = false;
        bool favorite = false;
        bool overflow = false;
        bool renamed = false;
    };

    ScriptCardResult ScriptCard(
        const char* id,
        const ScriptCardDesc& desc) noexcept;

    void PageHeader(
        const char* title,
        const char* subtitle = nullptr) noexcept;

    void StatusChip(
        const char* id,
        const char* text,
        bool positive) noexcept;

    // Premium glass surfaces shared by master-detail pages. The title is
    // display-only; `id` is the complete stable ImGui identity.
    void BeginGlassPanel(
        const char* id,
        const ImVec2& size,
        const char* title = nullptr,
        const char* subtitle = nullptr,
        ImGuiWindowFlags flags = ImGuiWindowFlags_None,
        bool showAccentTick = false) noexcept;

    void EndGlassPanel() noexcept;

    void BeginToolbar(
        const char* id,
        const ImVec2& size = ImVec2(0.f, 44.f)) noexcept;

    void EndToolbar() noexcept;

    bool ActionButton(
        const char* id,
        const char* label,
        const ImVec2& size,
        bool primary = false,
        const char* icon = nullptr) noexcept;

    // Icon with no surface behind it. For destructive affordances that sit
    // inside a row: a filled button there competes with the row itself, and
    // the row is what the user is looking at.
    bool GhostIconButton(
        const char* id,
        const char* icon,
        const ImVec2& size,
        bool danger = false) noexcept;

    bool SearchRow(
        const char* id,
        const char* hint,
        char* buffer,
        std::size_t capacity,
        const ImVec2& size = ImVec2(0.f, 36.f),
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_None) noexcept;

    void EmptyState(
        const char* id,
        const char* icon,
        const char* title,
        const char* detail,
        float height = 132.f) noexcept;

    struct MasterDetailMetrics final
    {
        bool sideBySide = true;
        float masterWidth = 0.f;
        float detailWidth = 0.f;
        float masterHeight = 0.f;
        float detailHeight = 0.f;
        float gap = 0.f;
    };

    MasterDetailMetrics CalculateMasterDetail(
        float availableWidth,
        float availableHeight,
        float preferredMasterWidth = 300.f,
        float minimumDetailWidth = 340.f,
        float compactMasterHeight = 250.f) noexcept;

    // Localized bone name for aim hitboxes (0..15).
    const char* BoneLabel(int index) noexcept;

    // Multi-select dropdown of bones (preferred over vertical list).
    // When `nearestAny` is supplied the list gains a "Nearest" entry that is
    // exclusive with the explicit bones.
    void DrawBoneDropdown(
        bool* bones,
        const char* idSuffix,
        bool* nearestAny = nullptr) noexcept;

    void DrawBoneOptions(
        bool* nearest,
        bool* anyBone,
        bool* visible,
        const char* idSuffix) noexcept;

    struct ListRowDesc final
    {
        const char* title = "";
        const char* meta = nullptr;
        bool selected = false;
        bool accentDot = false;
        ImTextureID icon = nullptr;
        float iconW = 0.f;
        float iconH = 0.f;
        float iconSlotW = 0.f; // 0 = no icon column
        float height = 48.f;
    };

    // Returns true when the row was clicked.
    bool DrawListRow(const char* id, const ListRowDesc& desc) noexcept;
}
