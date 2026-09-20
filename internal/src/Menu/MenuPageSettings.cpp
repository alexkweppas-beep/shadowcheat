#include "MenuPages.h"
#include <Windows.h>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define IMGUI_DEFINE_MATH_OPERATORS

#include "../Features/Aimbot.h"
#include "../Features/CloudStorage.h"
#include "../Features/FeatureBinds.h"
#include "../Features/FiveRpFarmBot.h"
#include "../Features/FiveRpMetadataResolver.h"
#include "../Features/Friends.h"
#include "../Features/MeshSilhouette.h"
#include "../Features/MiscFeatures.h"
#include "../Features/ObsBypass.h"
#include "../Features/PedPreview.h"
#include "../Features/QuantTools.h"
#include "../Features/RagePlayerNames.h"
#include "../Features/RageV8Runtime.h"
#include "../Features/RuntimeProfiler.h"
#include "../Features/ScriptBridge.h"
#include "../Features/SilentAim.h"
#include "../Features/UkraineTools.h"
#include "../Features/VehicleMovement.h"
#include "../Helpers/LocalTime.h"
#include "../Main/Hooks.h"
#include "../SDK/Game.h"
#include "../SDK/RageBuildFlavor.h"
#include "../SDK/WeaponCatalog.h"
#include "../SDK/WeaponGroups.h"
#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"
#include "EspDrawHelpers.h"
#include "EspProjectedOverlay.h"
#include "IconsFontAwesome6.h"
#include "Leyawin/custom_widgets.hpp"
#include "Leyawin/leyawin_globals.h"
#include "Leyawin/leyawin_init.h"
#include "Leyawin/leyawin_safe.h"
#include "MenuConfig.h"
#include "MenuMotion.h"
#include "MenuSearch.h"
#include "MenuState.h"
#include "MenuTexture.h"
#include "MenuTheme.h"
#include "MenuType.h"
#include "MenuUiHelpers.h"
#include "WeaponIconMap.h"
#include "WeaponManager.h"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace Menu {
namespace {
// The gauge has no plan length to divide by, so it reads remaining time
// against a month: a full bar means "plenty", and the last days visibly
// drain it.
constexpr std::int64_t kSubscriptionGaugeSpan = 30 * 86400;

void DrawSubscriptionCard(float scale) {
  const MenuTheme::Palette &tokens = MenuTheme::Colors();
  const MenuTheme::Metrics &metrics = MenuTheme::Sizes();

  std::int64_t expiresAt = s_AccountExpiresAt;
  if (expiresAt <= 0)
    expiresAt = CloudStorage::SubscriptionEndsAt();
  const std::int64_t now = static_cast<std::int64_t>(time(nullptr));
  const std::int64_t left = expiresAt > 0 ? expiresAt - now : 0;
  const bool expired = expiresAt <= 0 || left <= 0;
  // Under three days the card switches to amber: this panel is the
  // only place the expiry shows, so it has to raise the alarm itself.
  const bool ending = !expired && left < 3 * 86400;
  const ImVec4 statusColor =
      expired ? tokens.error : (ending ? tokens.warning : tokens.success);

  MenuUi::BeginInspectorIsland("flyout_account_island", nullptr, 0.f);
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const float width = ImMax(ImGui::GetContentRegionAvail().x, 1.f);

  // --- Header: avatar, nickname, status pill -------------------
  // railExpand 0 keeps NavRailAccount to the avatar disc so the
  // nickname can be clipped against the status pill below.
  const float avatar = 34.f * scale;
  const ImVec2 headerMin = ImGui::GetCursorScreenPos();
  MenuUi::NavRailAccount(headerMin, avatar, s_AccountName, nullptr, 0.f);

  const char *statusText =
      expired ? T("Expired", "Истёк", "Вичерпано")
              : (ending ? T("Ending", "Заканчивается", "Закінчується")
                        : T("Active", "Активен", "Активний"));
  ImFont *microFont = MenuType::Micro();
  const float microSize = MenuType::MicroSize();
  const ImVec2 statusSz =
      microFont->CalcTextSizeA(microSize, FLT_MAX, 0.f, statusText);
  const float dot = 4.f * scale;
  const float pillH = microSize + 8.f * scale;
  const float pillW = statusSz.x + dot * 2.f + 16.f * scale;
  // Whole pixels everywhere below: this card centres text against
  // odd heights, and the half-pixel positions that fell out of that
  // were what made the glyphs look soft.
  const ImVec2 pillMin(IM_ROUND(headerMin.x + width - pillW),
                       IM_ROUND(headerMin.y + (avatar - pillH) * 0.5f));
  draw->AddRectFilled(pillMin, pillMin + ImVec2(pillW, pillH),
                      gui->get_clr(statusColor, 0.14f), pillH * 0.5f);
  draw->AddCircleFilled(
      ImVec2(pillMin.x + 8.f * scale, pillMin.y + pillH * 0.5f), dot,
      gui->get_clr(statusColor, 1.f), 12);
  draw->AddText(microFont, microSize,
                ImVec2(IM_ROUND(pillMin.x + 8.f * scale + dot + 5.f * scale),
                       IM_ROUND(pillMin.y + (pillH - microSize) * 0.5f)),
                gui->get_clr(statusColor, 1.f), statusText);

  const char *cloudUser = CloudStorage::GetUsername();
  if (!cloudUser || !cloudUser[0])
    cloudUser = s_AccountName;
  const char *nick = cloudUser[0] ? cloudUser : "User";
  ImFont *nickFont = MenuType::Title();
  const float nickSize = MenuType::TitleSize();
  const float nickX = IM_ROUND(headerMin.x + avatar + 10.f * scale);
  draw->PushClipRect(ImVec2(nickX, headerMin.y),
                     ImVec2(pillMin.x - 8.f * scale, headerMin.y + avatar),
                     true);
  draw->AddText(
      nickFont, nickSize,
      ImVec2(nickX, IM_ROUND(headerMin.y + (avatar - nickSize) * 0.5f)),
      gui->get_clr(tokens.textPrimary, 1.f), nick);
  draw->PopClipRect();
  ImGui::Dummy(ImVec2(0.f, avatar + metrics.space3 * scale));

  // --- Remaining time -----------------------------------------
  char value[32]{};
  const char *caption = nullptr;
  if (expired) {
    lstrcpynA(value, T("No subscription", "Нет подписки", "Немає підписки"),
              static_cast<int>(sizeof(value)));
  } else {
    FormatSubscriptionRemaining(value, static_cast<int>(sizeof(value)),
                                expiresAt);
    caption = T("left", "осталось", "залишилось");
  }

  // Display() is the only role rasterised at DisplaySize. Mono() is
  // built at 14.5 px, so asking for 23 px upscaled its glyphs by
  // 1.6x and that is what read as blur.
  ImFont *valueFont = expired ? MenuType::Title() : MenuType::Display();
  const float valueSize =
      expired ? MenuType::TitleSize() : MenuType::DisplaySize();
  const ImVec2 valueOrigin = ImGui::GetCursorScreenPos();
  const ImVec2 valuePos(IM_ROUND(valueOrigin.x), IM_ROUND(valueOrigin.y));
  const ImVec2 valueSz =
      valueFont->CalcTextSizeA(valueSize, FLT_MAX, 0.f, value);
  draw->AddText(
      valueFont, valueSize, valuePos,
      gui->get_clr(expired ? tokens.textMuted : tokens.textPrimary, 1.f),
      value);
  if (caption) {
    draw->AddText(
        microFont, microSize,
        ImVec2(IM_ROUND(valuePos.x + valueSz.x + 6.f * scale),
               IM_ROUND(valuePos.y + valueSz.y - microSize - 1.f * scale)),
        gui->get_clr(tokens.textFaint, 1.f), caption);
  }
  ImGui::Dummy(ImVec2(0.f, valueSz.y + metrics.space2 * scale));

  // --- Gauge ---------------------------------------------------
  // One tick per day of the span the gauge measures against, so a lit
  // tick is a real remaining day and not a decorative notch.
  constexpr int kGaugeTicks = static_cast<int>(kSubscriptionGaugeSpan / 86400);
  const float trackH = IM_ROUND(5.f * scale);
  const ImVec2 trackOrigin = ImGui::GetCursorScreenPos();
  const float trackLeft = IM_ROUND(trackOrigin.x);
  const float trackTop = IM_ROUND(trackOrigin.y);
  const float trackRight = IM_ROUND(trackOrigin.x + width);
  const float tickGap = ImMax(IM_ROUND(2.f * scale), 1.f);
  const float tickPitch =
      (trackRight - trackLeft + tickGap) / static_cast<float>(kGaugeTicks);
  const float tickW = ImMax(IM_ROUND(tickPitch - tickGap), 1.f);
  const float tickRadius = ImMin(trackH, tickW) * 0.5f;
  // Round up: the hours left over past the last whole day still hold
  // their tick, so the gauge only empties when the time really has.
  const int litTicks =
      expired
          ? 0
          : ImClamp(static_cast<int>(ceilf(static_cast<float>(left) / 86400.f)),
                    1, kGaugeTicks);
  for (int tick = 0; tick < kGaugeTicks; ++tick) {
    const float x = IM_ROUND(trackLeft + tickPitch * static_cast<float>(tick));
    draw->AddRectFilled(
        ImVec2(x, trackTop), ImVec2(x + tickW, trackTop + trackH),
        gui->get_clr(tick < litTicks ? statusColor : tokens.raised, 1.f),
        tickRadius);
  }
  ImGui::Dummy(ImVec2(0.f, trackH + metrics.space2 * scale));

  // --- Expiry date --------------------------------------------
  char expiry[64]{};
  SYSTEMTIME parts{};
  if (TimeUtil::LocalTimeFromUnix(expiresAt, parts)) {
    _snprintf(expiry, sizeof(expiry) - 1, "%s %02d.%02d.%04d",
              T("Until", "До", "До"), parts.wDay, parts.wMonth, parts.wYear);
  } else {
    lstrcpynA(expiry, T("Not activated", "Не активирован", "Не активовано"),
              static_cast<int>(sizeof(expiry)));
  }
  const ImVec2 expiryOrigin = ImGui::GetCursorScreenPos();
  const ImVec2 expiryPos(IM_ROUND(expiryOrigin.x), IM_ROUND(expiryOrigin.y));
  ImFont *iconFont = set->icon ? set->icon : microFont;
  const float iconSize = microSize + 1.f * scale;
  draw->AddText(iconFont, iconSize, expiryPos,
                gui->get_clr(tokens.textFaint, 1.f), ICON_FA_CALENDAR_DAY);
  draw->AddText(
      microFont, microSize,
      ImVec2(IM_ROUND(expiryPos.x + iconSize + 6.f * scale), expiryPos.y),
      gui->get_clr(tokens.textMuted, 1.f), expiry);
  ImGui::Dummy(ImVec2(0.f, microSize + 1.f * scale));
  MenuUi::EndInspectorIsland();
}

void DrawGlobalKeysCard() {
  MenuUi::BeginInspectorIsland("flyout_keys_island",
                               T("Keys", "Клавиши", "Клавіші"), 0.f, nullptr,
                               true);

  gui->keybind(T("Open menu", "Открыть меню", "Відкрити меню"),
               &s_MenuToggleKey, ICON_FA_KEYBOARD);
  // Capturing must not let the pressed key also reach the toggle or
  // the unload handler in the same frame.
  if (ImGui::IsItemActive())
    FeatureBinds::SetCaptureActive(true);
  // Clearing this bind would lock the user out of their own menu.
  if (s_MenuToggleKey <= 0 && !ImGui::IsItemActive())
    s_MenuToggleKey = VK_F4;

  gui->keybind(T("Unload", "Выгрузить", "Вивантажити"), &s_UnloadKey,
               ICON_FA_POWER_OFF);
  if (ImGui::IsItemActive())
    FeatureBinds::SetCaptureActive(true);
  MenuUi::EndInspectorIsland();

  // Capture bypass belongs with the other global switches rather
  // than on a feature page: it changes how the overlay is drawn,
  // not what it draws.
  MenuUi::BeginInspectorIsland("flyout_capture_island",
                               T("Capture", "Захват", "Захоплення"), 10.f,
                               ObsBypass::StatusText(), true);
  ImGui::TextDisabled("%s", ObsBypass::StatusDetail());
  MenuUi::SettingRowDesc obs{};
  obs.icon = ICON_FA_VIDEO;
  obs.label = T("OBS bypass", "Обход OBS", "Обхід OBS");
  obs.enabled = &set->obs_bypass;
  // Not a feature bind: this changes how the overlay is presented,
  // so a hotkey for it would only cause confusion.
  obs.bindable = false;
  if (MenuUi::SettingRow("flyout_obs_bypass", obs).toggled)
    ObsBypass::SetEnabled(set->obs_bypass);
  MenuUi::EndInspectorIsland();
}
} // namespace

void RenderMenuSettingsFlyout() {
  const float scale =
      ImClamp(static_cast<float>(s_UIScalePercent) / 100.f, 1.f, 2.f);
  SetRenderPhase("flyout:account");
  DrawSubscriptionCard(scale);

  // Two columns are what keep every setting above the fold. The shared
  // BeginPageColumns() cannot be used here: it sizes its children from
  // the available height, and this panel's height comes from its content.
  const MenuTheme::Metrics &metrics = MenuTheme::Sizes();
  const float columnGap = metrics.space3 * scale;
  const float columnWidth =
      (ImMax(ImGui::GetContentRegionAvail().x, 1.f) - columnGap) * 0.5f;

  ImGui::BeginChild("##flyout_column_look", ImVec2(columnWidth, 0.f),
                    ImGuiChildFlags_AutoResizeY,
                    ImGuiWindowFlags_NoSavedSettings);

  SetRenderPhase("flyout:appearance");
  MenuUi::BeginInspectorIsland(
      "flyout_look_island", T("Appearance", "Внешний вид", "Зовнішній вигляд"),
      0.f, nullptr, true);

  // Only the rasterised font variants are valid, so the value snaps to
  // the nearest 25%. Six steps from three quarters to double.
  int scalePercent = s_UIScalePercent;
  if (gui->slider_int(T("Size###flyout_scale", "Размер###flyout_scale", "Розмір###flyout_scale"),
                      &scalePercent, 75, 200, "%d%%")) {
    s_UIScalePercent = 75 + ImClamp((scalePercent - 75 + 12) / 25, 0, 5) * 25;
  }
  gui->color_edit(T("Accent###flyout_accent", "Акцент###flyout_accent", "Акцент###flyout_accent"),
                  func->accent_color);
  MenuTheme::SetAccentBase(ImVec4(func->accent_color[0], func->accent_color[1],
                                  func->accent_color[2], 1.f));

  // House accents. These only move the accent ramp; surfaces and text
  // keep their own tokens.
  {
    const float swatch = 20.f * scale;
    const float swatchGap = 7.f * scale;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImVec4 current = MenuTheme::AccentBase();
    ImGui::PushID("flyout_accent_presets");
    for (int i = 0; i < static_cast<int>(MenuTheme::Preset::Count); ++i) {
      const auto preset = static_cast<MenuTheme::Preset>(i);
      const ImVec4 color = MenuTheme::PresetColor(preset);
      if (i > 0)
        ImGui::SameLine(0.f, swatchGap);
      ImGui::PushID(i);
      ImGui::InvisibleButton("##sw", ImVec2(swatch, swatch));
      const bool hovered = ImGui::IsItemHovered();
      const bool active = fabsf(color.x - current.x) < 0.01f &&
                          fabsf(color.y - current.y) < 0.01f &&
                          fabsf(color.z - current.z) < 0.01f;
      const ImVec2 center =
          ImGui::GetItemRectMin() + ImVec2(swatch * 0.5f, swatch * 0.5f);
      draw->AddCircleFilled(
          center, swatch * 0.42f,
          gui->get_clr(color, hovered || active ? 1.f : 0.82f), 20);
      if (active) {
        draw->AddCircle(center, swatch * 0.5f,
                        gui->get_clr(MenuTheme::Colors().textPrimary, 0.85f),
                        20, 1.6f * scale);
      }
      if (hovered) {
        ImGui::SetTooltip("%s", MenuTheme::PresetName(preset));
      }
      if (ImGui::IsItemClicked()) {
        MenuTheme::SetPreset(preset);
        const ImVec4 picked = MenuTheme::AccentBase();
        func->accent_color[0] = picked.x;
        func->accent_color[1] = picked.y;
        func->accent_color[2] = picked.z;
      }
      ImGui::PopID();
    }
    ImGui::PopID();
  }

  // The canvas drives every neutral surface, so editing it retints the
  // panels and controls with it.
  static float s_CanvasEdit[4]{};
  static bool s_CanvasEditReady = false;
  if (!s_CanvasEditReady) {
    const ImVec4 canvas = MenuTheme::CanvasBase();
    s_CanvasEdit[0] = canvas.x;
    s_CanvasEdit[1] = canvas.y;
    s_CanvasEdit[2] = canvas.z;
    s_CanvasEdit[3] = 1.f;
    s_CanvasEditReady = true;
  }
  if (gui->color_edit(T("Background###flyout_canvas", "Фон###flyout_canvas", "Тло###flyout_canvas"),
                      s_CanvasEdit)) {
    MenuTheme::SetCanvasBase(
        ImVec4(s_CanvasEdit[0], s_CanvasEdit[1], s_CanvasEdit[2], 1.f));
  }

  int theme = s_LightTheme ? 1 : 0;
  const MenuUi::IconTabDesc themes[] = {
      {"dark", ICON_FA_MOON, T("Dark", "Темная", "Темна")},
      {"light", ICON_FA_SUN, T("Light", "Светлая", "Світла")}};
  if (MenuUi::ChipTabs("flyout_theme", themes, IM_ARRAYSIZE(themes), &theme)) {
    s_LightTheme = theme == 1;
    ApplyLeyawinTheme(s_LightTheme);
  }
  if (MenuUi::ActionButton("flyout_theme_reset",
                           T("Reset to default", "Сбросить по умолчанию", "Скинути за замовчуванням"),
                           ImVec2(-1.f, 30.f * scale), false,
                           ICON_FA_ARROW_ROTATE_LEFT)) {
    s_LightTheme = false;
    ApplyLeyawinTheme(s_LightTheme);
    MenuTheme::ResetToDefault();
    // The accent picker is pushed into the theme every frame, so the
    // edit buffers have to follow the reset or they would undo it.
    const ImVec4 accent = MenuTheme::AccentBase();
    func->accent_color[0] = accent.x;
    func->accent_color[1] = accent.y;
    func->accent_color[2] = accent.z;
    const ImVec4 canvas = MenuTheme::CanvasBase();
    s_CanvasEdit[0] = canvas.x;
    s_CanvasEdit[1] = canvas.y;
    s_CanvasEdit[2] = canvas.z;
    s_CanvasEdit[3] = 1.f;
  }
  MenuUi::EndInspectorIsland();
  ImGui::EndChild();

  ImGui::SameLine(0.f, columnGap);

  ImGui::BeginChild("##flyout_column_input", ImVec2(columnWidth, 0.f),
                    ImGuiChildFlags_AutoResizeY,
                    ImGuiWindowFlags_NoSavedSettings);

  SetRenderPhase("flyout:keys");
  DrawGlobalKeysCard();
  ImGui::EndChild();

  // Full width: the three chips fit on one row here, and it leaves the
  // two columns above ending at the same height.
  SetRenderPhase("flyout:language");
  MenuUi::BeginInspectorIsland("flyout_language_island",
                               T("Language", "Язык", "Мова"), 10.f, nullptr,
                               true);
  const MenuUi::ChipDesc langChips[] = {
      {"en", "English"}, {"ru", "Русский"}, {"ua", "Українська"}};
  int selectedLang = static_cast<int>(s_Language);
  if (MenuUi::ChipStrip("flyout_language", langChips, IM_ARRAYSIZE(langChips),
                        &selectedLang, 92.f, 30.f)) {
    s_Language = static_cast<Language>(ImClamp(selectedLang, 0, 2));
  }
  MenuUi::EndInspectorIsland();

  ApplyRawImGuiTheme(ImGui::GetStyle());
}

enum class ConfigTileAction : int {
  None = 0,
  Load,
  Save,
  SetDefault,
  Rename,
  Delete
};

static bool ConfigNameContains(const char *text, const char *query) noexcept {
  if (!query || !query[0])
    return true;
  if (!text)
    return false;
  for (int start = 0; text[start]; ++start) {
    int offset = 0;
    while (query[offset] && text[start + offset]) {
      char a = text[start + offset];
      char b = query[offset];
      if (a >= 'A' && a <= 'Z')
        a += 'a' - 'A';
      if (b >= 'A' && b <= 'Z')
        b += 'a' - 'A';
      if (a != b)
        break;
      ++offset;
    }
    if (!query[offset])
      return true;
  }
  return false;
}

static ConfigTileAction DrawConfigTile(const char *id, const char *title,
                                       const char *meta, float width,
                                       bool loaded, bool isDefault,
                                       char *editableTitle = nullptr,
                                       int editableCapacity = 0) noexcept {
  struct TileAnim final {
    float hover = 0.f;
  };
  const float scale =
      ImClamp(static_cast<float>(s_UIScalePercent) / 100.f, 1.f, 2.f);
  const ImVec2 min = ImGui::GetCursorScreenPos();
  const ImVec2 size(width, 88.f * scale);
  ImGui::BeginGroup();
  ImGui::Dummy(size);
  const ImVec2 after = ImGui::GetCursorScreenPos();
  const ImVec2 max = min + size;
  const bool hovered = ImGui::IsMouseHoveringRect(min, max, false);
  ImGui::PushID(id ? id : "config_tile");
  TileAnim &anim = LeyawinAnim<TileAnim, 7601>().get(ImGui::GetID("##state"));
  anim.hover = ImLerp(anim.hover, hovered ? 1.f : 0.f, gui->fixed_speed(16.f));
  ImDrawList *draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(
      min, max,
      gui->get_clr(ImLerp(clr->child_bottom_bar, clr->element_background,
                          anim.hover * 0.55f),
                   0.76f),
      14.f * scale);
  draw->AddRect(min, max, gui->get_clr(clr->child_stroke_bar, 0.38f),
                14.f * scale);
  // Double-clicking the name turns it into a text field; only local slots
  // hand over a buffer, cloud entries stay read-only.
  static ImGuiID s_RenamingTile = 0;
  static bool s_RenameFocus = false;
  const ImGuiID tileId = ImGui::GetID("##name");
  const bool canRename = editableTitle != nullptr && editableCapacity > 1;
  const bool renaming = canRename && s_RenamingTile == tileId;
  ConfigTileAction action = ConfigTileAction::None;
  // Icon and name share one line: the icon is centred on the middle of
  // the name's own box and the name starts a fixed gap after the glyph.
  // They used to be placed by two hand-picked offsets, so the icon sat a
  // few pixels above the text and the name looked shifted against it, by
  // a different amount for the star and the file glyph.
  ImFont *iconFont = set->icon ? set->icon : ImGui::GetFont();
  const char *tileIcon = isDefault ? ICON_FA_STAR : ICON_FA_FILE_LINES;
  const float tileIconSize = 12.f * scale;
  const ImVec2 tileIconExtent =
      iconFont->CalcTextSizeA(tileIconSize, FLT_MAX, 0.f, tileIcon);
  const float tileIconX = min.x + 12.f * scale;
  const float titleTop = min.y + 10.f * scale;
  const float titleBottom = min.y + 36.f * scale;
  const float titleMid = (titleTop + titleBottom) * 0.5f;
  draw->AddText(
      iconFont, tileIconSize,
      ImVec2(IM_ROUND(tileIconX), IM_ROUND(titleMid - tileIconExtent.y * 0.5f)),
      gui->get_clr(clr->accent_color), tileIcon);
  // Both buttons are as wide as their own label needs.
  //
  // They used to be a fixed seventy pixels, which fits "Save" and "Load"
  // and nothing else: in Russian the word ran past the pill and the icon
  // sat on top of the text. The row is laid out from the right edge, and
  // the line above it is clipped to whatever is left.
  const char *saveLabel = T("Save", "Сохранить", "Зберегти");
  const char *loadLabel = T("Load", "Загрузить", "Завантажити");
  const float actionsGap = 6.f * scale;
  const float buttonPad = 44.f * scale;
  const float saveW =
      ImMax(70.f * scale, ImGui::CalcTextSize(saveLabel).x + buttonPad);
  const float loadW =
      ImMax(70.f * scale, ImGui::CalcTextSize(loadLabel).x + buttonPad);
  const float actionsW = saveW + actionsGap + loadW;
  const float actionsX = max.x - 12.f * scale - actionsW;

  const ImVec2 titleMin(IM_ROUND(tileIconX + tileIconExtent.x + 9.f * scale),
                        titleTop);
  // The title row only has to clear the overflow dots.
  const ImVec2 titleMax(max.x - 44.f * scale, titleBottom);
  if (!renaming) {
    gui->render_text_clipped(set->poppins_widget, titleMin, titleMax,
                             gui->get_clr(clr->text_active),
                             title ? title : "Config", {0.f, 0.5f});
    if (canRename && ImGui::IsMouseHoveringRect(titleMin, titleMax, false) &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      s_RenamingTile = tileId;
      s_RenameFocus = true;
    }
  } else {
    ImGui::SetCursorScreenPos(ImVec2(titleMin.x - 4.f * scale, titleMin.y));
    ImGui::SetNextItemWidth(ImMax(80.f * scale, titleMax.x - titleMin.x));
    if (s_RenameFocus) {
      ImGui::SetKeyboardFocusHere();
      s_RenameFocus = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(5.f * scale, 3.f * scale));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,
                          gui->get_clr(clr->element_background, 1.f));
    const bool submitted = ImGui::InputText(
        "##rename", editableTitle, static_cast<size_t>(editableCapacity),
        ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_AutoSelectAll);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (submitted || ImGui::IsItemDeactivated()) {
      s_RenamingTile = 0;
      action = ConfigTileAction::Rename;
    }
  }
  gui->render_text_clipped(
      set->poppins_widget, min + ImVec2(12.f, 40.f) * scale,
      ImVec2(actionsX - actionsGap, max.y - 10.f * scale),
      gui->get_clr(clr->text, 0.72f), meta ? meta : "Config", {0.f, 0.5f});

  const ImVec2 moreMin(max.x - 36.f * scale, min.y + 10.f * scale);
  ImGui::SetCursorScreenPos(moreMin);
  ImGui::InvisibleButton("##more", ImVec2(28.f, 28.f) * scale);
  const bool moreHovered = ImGui::IsItemHovered();
  if (ImGui::IsItemClicked()) {
    gui->open_popup_w("##config_actions", ImGui::GetItemRectMin(),
                      ImGui::GetItemRectMax());
  }
  gui->render_text_clipped(
      iconFont, moreMin, moreMin + ImVec2(28.f, 28.f) * scale,
      gui->get_clr(moreHovered ? clr->text_active : clr->text,
                   moreHovered ? 1.f : 0.65f),
      ICON_FA_ELLIPSIS, {0.5f, 0.5f});

  if (gui->begin_popup_w("##config_actions", 206.f * scale, -1)) {
    ImGui::TextDisabled("%s", T("CONFIG ACTIONS", "ДЕЙСТВИЯ", "ДІЇ"));
    ImGui::Dummy(ImVec2(0.f, 2.f * scale));
    if (MenuUi::ActionButton(
            "config_set_default",
            T("Set as default", "Сделать по умолчанию", "Зробити типовим"),
            ImVec2(ImGui::GetContentRegionAvail().x, 31.f * scale), false,
            ICON_FA_STAR)) {
      action = ConfigTileAction::SetDefault;
      gui->dismiss_popup_w("##config_actions");
    }
    if (MenuUi::ActionButton(
            "config_delete", T("Delete", "Удалить", "Видалити"),
            ImVec2(ImGui::GetContentRegionAvail().x, 31.f * scale), false,
            ICON_FA_TRASH_CAN)) {
      action = ConfigTileAction::Delete;
      gui->dismiss_popup_w("##config_actions");
    }
    gui->end_popup_w();
  }

  ImGui::SetCursorScreenPos(ImVec2(actionsX, min.y + 46.f * scale));
  if (MenuUi::ActionButton("config_save", saveLabel,
                           ImVec2(saveW, 28.f * scale), true,
                           ICON_FA_FLOPPY_DISK)) {
    action = ConfigTileAction::Save;
  }
  ImGui::SameLine(0.f, actionsGap);
  if (MenuUi::ActionButton("config_load", loadLabel,
                           ImVec2(loadW, 28.f * scale), false,
                           ICON_FA_DOWNLOAD)) {
    action = ConfigTileAction::Load;
  }
  (void)loaded;
  ImGui::SetCursorScreenPos(after);
  ImGui::EndGroup();
  ImGui::PopID();
  return action;
}

static CloudStorage::Item s_CloudConfigs[CloudStorage::kMaxItems]{};
static int s_CloudConfigCount = 0;
static bool s_CloudFetched = false;
static char s_CloudStatus[128]{};
int s_CloudLoadedIdx = -1;
static int s_CloudDefaultIdx = -1;

static void RefreshCloudConfigs() noexcept {
  s_CloudConfigCount = 0;
  if (!CloudStorage::List("config", s_CloudConfigs, CloudStorage::kMaxItems,
                          s_CloudConfigCount)) {
    lstrcpynA(s_CloudStatus, CloudStorage::LastError(), sizeof(s_CloudStatus));
  } else {
    s_CloudStatus[0] = 0;
  }
}

static void RenderConfigSlotsPageRedesign(ImGuiStyle *style) {
  (void)style;
  static char search[64]{};
  static char s_RenameBuf[64]{};
  static int  s_RenamingIdx = -1;
  static bool s_RenameFocusSet = false;
  static bool s_RenameWasActive = false;
  const float scale =
      ImClamp(static_cast<float>(s_UIScalePercent) / 100.f, 1.f, 2.f);

  // ?�??�? Ensure session & fetch once ?�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�?
  if (!s_CloudFetched) {
    if (CloudStorage::IsAvailable() || CloudStorage::EnsureSession()) {
      s_CloudFetched = true;
      RefreshCloudConfigs();
    }
  }

  // ?�??�? Header row: Search + New config ?�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�?
  MenuUi::BeginInspectorIsland(
      "config_library_header", nullptr, 0.f,
      T("Cloud configs", "Облачные конфиги", "Хмарні конфіги"), true);

  const float gap      = 8.f * scale;
  const float newWidth = 132.f * scale;
  const float searchWidth =
      ImMax(140.f * scale, ImGui::GetContentRegionAvail().x - newWidth - gap);

  MenuUi::SearchRow(
      "config_library_search",
      T("Search configs...", "Поиск конфигов...", "Пошук конфігів..."),
      search, sizeof(search), ImVec2(searchWidth, 34.f * scale));
  ImGui::SameLine(0.f, gap);

  const bool canCreate =
      (CloudStorage::IsAvailable() || CloudStorage::EnsureSession()) &&
      s_CloudConfigCount < CloudStorage::kMaxItems;

  if (MenuUi::ActionButton(
          "config_library_new",
          T("New config", "Новый конфиг", "Новий конфіг"),
          ImVec2(newWidth, 34.f * scale), true,
          ICON_FA_FILE_CIRCLE_PLUS) && canCreate) {
    // Generate unique name
    char generated[64]{};
    for (int n = 1; n <= 99; ++n) {
      _snprintf(generated, sizeof(generated) - 1, "New Config %d", n);
      bool exists = false;
      for (int i = 0; i < s_CloudConfigCount; ++i)
        if (_stricmp(s_CloudConfigs[i].name, generated) == 0) { exists = true; break; }
      if (!exists) break;
    }
    // Export current settings and upload
    char *payload = new (std::nothrow) char[Menu::kConfigPayloadCapacity]{};
    if (payload) {
      if (Menu::ExportCurrentConfig(payload, Menu::kConfigPayloadCapacity) &&
          CloudStorage::Upsert("config", generated, payload)) {
        RefreshCloudConfigs();
        // find created index
        for (int i = 0; i < s_CloudConfigCount; ++i)
          if (_stricmp(s_CloudConfigs[i].name, generated) == 0) {
            s_CloudLoadedIdx = i;
            break;
          }
        lstrcpynA(s_CloudStatus, T("Created", "Создан", "Створено"), sizeof(s_CloudStatus));
      } else {
        lstrcpynA(s_CloudStatus, CloudStorage::LastError(), sizeof(s_CloudStatus));
      }
      delete[] payload;
    }
  }
  MenuUi::EndInspectorIsland();

  // ?�??�? Status line ?�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�?
  if (s_CloudStatus[0]) {
    ImGui::Dummy(ImVec2(0.f, 2.f * scale));
    ImGui::TextColored(ImVec4(0.62f, 0.66f, 0.74f, 1.f), "%s", s_CloudStatus);
  }

  // ?�??�? Cloud unavailable ?�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�?
  if (!CloudStorage::IsAvailable()) {
    ImGui::Dummy(ImVec2(0.f, 8.f * scale));
    MenuUi::EmptyState(
        "config_no_session", ICON_FA_CLOUD,
        T("Not signed in", "Нет авторизации", "Немає авторизації"),
        T("Launch through the loader to sync configs", "Запусти через лоадер для синхронизации", "Запусти через лоадер для синхронізації"),
        150.f);
    return;
  }

  // ?�??�? Config tile grid ?�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�?
  const float gridHeight = ImGui::GetContentRegionAvail().y;
  ImGui::BeginChild("##config_tile_grid", ImVec2(0.f, gridHeight),
                    ImGuiChildFlags_None, ImGuiWindowFlags_NoSavedSettings);

  const float tileGap   = 9.f * scale;
  const float tileWidth = (ImGui::GetContentRegionAvail().x -
                           ImGui::GetStyle().ScrollbarSize - tileGap) * 0.5f;

  int  shown = 0;
  bool stop  = false;

  for (int i = 0; i < s_CloudConfigCount && !stop; ++i) {
    if (!ConfigNameContains(s_CloudConfigs[i].name, search))
      continue;

    if ((shown % 2) == 1)
      ImGui::SameLine(0.f, tileGap);

    // ?�??�? Draw tile ?�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�??�?
    char id[32]{};
    _snprintf(id, sizeof(id) - 1, "cloud_cfg_%d", i);

    char metaBuf[128]{};
    const char *metaLabel = "";

    if (s_CloudConfigs[i].updatedAt > 0) {
      SYSTEMTIME local{};
      if (TimeUtil::LocalTimeFromUnix(s_CloudConfigs[i].updatedAt, local)) {
        _snprintf(metaBuf, sizeof(metaBuf) - 1, "%s %02d.%02d.%02d %02d:%02d",
                  T("Created", "Создан", "Створено"),
                  local.wDay, local.wMonth, local.wYear % 100, local.wHour, local.wMinute);
        metaLabel = metaBuf;
      }
    }

    // -- tile background & name
    const MenuTheme::Palette &tokens = MenuTheme::Colors();
    const float scale2 = scale;
    struct TileAnim { float hover = 0.f; };
    const ImVec2 tileMin = ImGui::GetCursorScreenPos();
    const ImVec2 tileSize(tileWidth, 88.f * scale2);
    ImGui::PushID(id);
    ImGui::BeginGroup();
    ImGui::Dummy(tileSize);
    const ImVec2 tileAfter = ImGui::GetCursorScreenPos();
    const ImVec2 tileMax   = tileMin + tileSize;
    const bool   hovered   = ImGui::IsMouseHoveringRect(tileMin, tileMax, false);
    TileAnim &anim = LeyawinAnim<TileAnim, 7610>().get(ImGui::GetID("##ts"));
    anim.hover = ImLerp(anim.hover, hovered ? 1.f : 0.f, gui->fixed_speed(16.f));

    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(tileMin, tileMax,
        gui->get_clr(ImLerp(clr->child_bottom_bar, clr->element_background,
                            anim.hover * 0.55f), 0.76f),
        14.f * scale2);
    draw->AddRect(tileMin, tileMax,
        gui->get_clr(clr->child_stroke_bar, 0.38f), 14.f * scale2);

    // icon
    ImFont *iconFont = set->icon ? set->icon : ImGui::GetFont();
    const char *ico  = (i == s_CloudDefaultIdx) ? ICON_FA_STAR : ICON_FA_FILE_LINES;
    const float icoSz = 12.f * scale2;
    const ImVec2 icoExt = iconFont->CalcTextSizeA(icoSz, FLT_MAX, 0.f, ico);
    const float  icoX   = tileMin.x + 12.f * scale2;
    const float  titleTop    = tileMin.y + 10.f * scale2;
    const float  titleBottom = tileMin.y + 36.f * scale2;
    const float  titleMid    = (titleTop + titleBottom) * 0.5f;
    draw->AddText(iconFont, icoSz,
        ImVec2(IM_ROUND(icoX), IM_ROUND(titleMid - icoExt.y * 0.5f)),
        gui->get_clr(clr->accent_color), ico);

    // buttons
    const char *saveLabel = T("Save", "Сохранить", "Зберегти");
    const char *loadLabel = T("Load", "Загрузить", "Завантажити");
    const float btnGap  = 6.f * scale2;
    const float btnPad  = 44.f * scale2;
    const float saveW   = ImMax(70.f * scale2, ImGui::CalcTextSize(saveLabel).x + btnPad);
    const float loadW   = ImMax(70.f * scale2, ImGui::CalcTextSize(loadLabel).x + btnPad);
    const float actW    = saveW + btnGap + loadW;
    const float actX    = tileMax.x - 12.f * scale2 - actW;

    // name / rename field
    const ImVec2 nameMin(IM_ROUND(icoX + icoExt.x + 9.f * scale2), titleTop);
    const ImVec2 nameMax(tileMax.x - 44.f * scale2, titleBottom);

    const bool renaming = (s_RenamingIdx == i);
    if (!renaming) {
      gui->render_text_clipped(set->poppins_widget, nameMin, nameMax,
          gui->get_clr(clr->text_active), s_CloudConfigs[i].name, {0.f, 0.5f});
      if (ImGui::IsMouseHoveringRect(nameMin, nameMax, false) &&
          ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        s_RenamingIdx = i;
        s_RenameFocusSet = false;
        lstrcpynA(s_RenameBuf, s_CloudConfigs[i].name, sizeof(s_RenameBuf));
      }
    } else {
      ImGui::SetCursorScreenPos(ImVec2(nameMin.x - 4.f * scale2, nameMin.y));
      ImGui::SetNextItemWidth(ImMax(80.f * scale2, nameMax.x - nameMin.x));
      if (!s_RenameFocusSet) {
        ImGui::SetKeyboardFocusHere();
        s_RenameFocusSet = true;
        s_RenameWasActive = false;
      }
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.f * scale2, 3.f * scale2));
      ImGui::PushStyleColor(ImGuiCol_FrameBg, gui->get_clr(clr->element_background, 1.f));
      const bool submitted = ImGui::InputText("##rename", s_RenameBuf, sizeof(s_RenameBuf),
          ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
      ImGui::PopStyleColor();
      ImGui::PopStyleVar();
      if (ImGui::IsItemActive())
        s_RenameWasActive = true;
      const bool cancelled = ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Escape);
      if (cancelled) {
        s_RenamingIdx = -1;
        s_RenameFocusSet = false;
        s_RenameWasActive = false;
      } else if (submitted || (s_RenameWasActive && !ImGui::IsItemActive() && s_RenameFocusSet)) {
        s_RenamingIdx = -1;
        s_RenameFocusSet = false;
        s_RenameWasActive = false;
        if (s_RenameBuf[0] && _stricmp(s_RenameBuf, s_CloudConfigs[i].name) != 0) {
          char *payload = new (std::nothrow) char[Menu::kConfigPayloadCapacity]{};
          if (payload) {
            if (CloudStorage::Get("config", s_CloudConfigs[i].id, payload,
                                  Menu::kConfigPayloadCapacity)) {
              CloudStorage::Delete("config", s_CloudConfigs[i].id);
              if (CloudStorage::Upsert("config", s_RenameBuf, payload)) {
                RefreshCloudConfigs();
                lstrcpynA(s_CloudStatus, T("Renamed", "Переименован", "Перейменовано"),
                          sizeof(s_CloudStatus));
              } else {
                lstrcpynA(s_CloudStatus, CloudStorage::LastError(), sizeof(s_CloudStatus));
              }
            } else {
              lstrcpynA(s_CloudStatus, CloudStorage::LastError(), sizeof(s_CloudStatus));
            }
            delete[] payload;
          }
        }
      }
    }

    // meta line
    gui->render_text_clipped(set->poppins_widget,
        tileMin + ImVec2(12.f, 40.f) * scale2,
        ImVec2(actX - btnGap, tileMax.y - 10.f * scale2),
        gui->get_clr(clr->text, 0.72f), metaLabel, {0.f, 0.5f});

    // ?�? popup button
    const float moreBtnW = 28.f * scale2;
    const float moreBtnH = titleBottom - titleTop;
    const ImVec2 moreMin(tileMax.x - 36.f * scale2, titleTop);
    ImGui::SetCursorScreenPos(moreMin);
    ImGui::InvisibleButton("##more", ImVec2(moreBtnW, moreBtnH));
    const bool moreHov = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked())
      gui->open_popup_w("##cfg_actions", ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    gui->render_text_clipped(iconFont, moreMin, ImVec2(moreMin.x + moreBtnW, titleBottom),
        gui->get_clr(moreHov ? clr->text_active : clr->text, moreHov ? 1.f : 0.65f),
        ICON_FA_ELLIPSIS, {0.5f, 0.5f});

    if (gui->begin_popup_w("##cfg_actions", 206.f * scale2, -1)) {
      ImGui::TextDisabled("%s", T("CONFIG ACTIONS", "ДЕЙСТВИЯ", "ДІЇ"));
      ImGui::Dummy(ImVec2(0.f, 2.f * scale2));
      if (MenuUi::ActionButton("cfg_default",
              T("Set as default", "Сделать по умолчанию", "Зробити типовим"),
              ImVec2(ImGui::GetContentRegionAvail().x, 31.f * scale2), false,
              ICON_FA_STAR)) {
        s_CloudDefaultIdx = (s_CloudDefaultIdx == i) ? -1 : i;
        gui->dismiss_popup_w("##cfg_actions");
      }
      if (MenuUi::ActionButton("cfg_delete",
              T("Delete", "Удалить", "Видалити"),
              ImVec2(ImGui::GetContentRegionAvail().x, 31.f * scale2), false,
              ICON_FA_TRASH_CAN)) {
        if (CloudStorage::Delete("config", s_CloudConfigs[i].id)) {
          if (s_CloudLoadedIdx == i) s_CloudLoadedIdx = -1;
          else if (s_CloudLoadedIdx > i) --s_CloudLoadedIdx;
          if (s_CloudDefaultIdx == i) s_CloudDefaultIdx = -1;
          else if (s_CloudDefaultIdx > i) --s_CloudDefaultIdx;
          RefreshCloudConfigs();
          stop = true;
        } else {
          lstrcpynA(s_CloudStatus, CloudStorage::LastError(), sizeof(s_CloudStatus));
        }
        gui->dismiss_popup_w("##cfg_actions");
      }
      gui->end_popup_w();
    }

    // Save / Load buttons
    ImGui::SetCursorScreenPos(ImVec2(actX, tileMin.y + 46.f * scale2));
    if (MenuUi::ActionButton("cfg_save", saveLabel,
            ImVec2(saveW, 28.f * scale2), true, ICON_FA_FLOPPY_DISK)) {
      char *payload = new (std::nothrow) char[Menu::kConfigPayloadCapacity]{};
      if (payload) {
        bool exported = Menu::ExportCurrentConfig(payload, Menu::kConfigPayloadCapacity);
        if (exported) {
            CloudStorage::Delete("config", s_CloudConfigs[i].name);
        }
        if (exported && CloudStorage::Upsert("config", s_CloudConfigs[i].name, payload)) {
          s_CloudLoadedIdx = i;
          RefreshCloudConfigs();
          lstrcpynA(s_CloudStatus, T("Saved", "Сохранён", "Збережено"), sizeof(s_CloudStatus));
        } else {
          lstrcpynA(s_CloudStatus, CloudStorage::LastError(), sizeof(s_CloudStatus));
        }
        delete[] payload;
      }
    }
    ImGui::SameLine(0.f, btnGap);
    if (MenuUi::ActionButton("cfg_load", loadLabel,
            ImVec2(loadW, 28.f * scale2), false, ICON_FA_DOWNLOAD)) {
      char *payload = new (std::nothrow) char[Menu::kConfigPayloadCapacity]{};
      if (payload) {
        if (CloudStorage::Get("config", s_CloudConfigs[i].id, payload,
                              Menu::kConfigPayloadCapacity)) {
          Menu::ImportCurrentConfig(payload);
          s_CloudLoadedIdx = i;
          lstrcpynA(s_CloudStatus, T("Loaded", "Загружен", "Завантажено"), sizeof(s_CloudStatus));
        } else {
          lstrcpynA(s_CloudStatus, CloudStorage::LastError(), sizeof(s_CloudStatus));
        }
        delete[] payload;
      }
    }

    ImGui::SetCursorScreenPos(tileAfter);
    ImGui::EndGroup();
    ImGui::PopID();

    ++shown;
  }

  if (shown == 0) {
    MenuUi::EmptyState(
        "config_library_empty", ICON_FA_FILE_LINES,
        T("No configs", "Конфигов нет", "Конфігів немає"),
        T("Create a config or change the filter", "Создай конфиг или измени фильтр", "Створи конфіг або зміни фильтр"),
        150.f);
  }

  ImGui::EndChild();
}

// Session shows only authoritative account state; no invented status.
void RenderConfigPage(ImGuiStyle *style) {
  s_ConfigSubTab = 0;
  RenderConfigSlotsPageRedesign(style);
}

// ── Cloud config public accessors (top-bar dropdown) ──
int CloudConfigCount() noexcept {
  return s_CloudConfigCount;
}

const char* CloudConfigName(int index) noexcept {
  if (index < 0 || index >= s_CloudConfigCount)
    return nullptr;
  return s_CloudConfigs[index].name;
}

bool LoadCloudConfig(int index) noexcept {
  if (index < 0 || index >= s_CloudConfigCount)
    return false;
  char *payload = new (std::nothrow) char[Menu::kConfigPayloadCapacity]{};
  if (!payload)
    return false;
  bool ok = false;
  if (CloudStorage::Get("config", s_CloudConfigs[index].id, payload,
                        Menu::kConfigPayloadCapacity)) {
    Menu::ImportCurrentConfig(payload);
    s_CloudLoadedIdx = index;
    ok = true;
  }
  delete[] payload;
  return ok;
}

void RefreshCloudConfigList() noexcept {
  s_CloudFetched = false; // triggers re-fetch on next render
}

} // namespace Menu
