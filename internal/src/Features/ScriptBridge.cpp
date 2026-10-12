#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "ScriptBridge.h"

#include "../Helpers/LocalTime.h"
#include "../Menu/IconsFontAwesome6.h"
#include "../Menu/Leyawin/custom_widgets.hpp"
#include "../Menu/Leyawin/leyawin_globals.h"
#include "../Menu/Menu.h"
#include "../Menu/MenuUiHelpers.h"
#include "../SDK/Game.h"
#include "../SDK/RageBuildFlavor.h"
#include "../SDK/RageMpBuildProfile.h"
#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"
#include "Aimbot.h"
#include "EventLogger.h"
#include "FiveRpAdminScan.h"
#include "FiveRpMetadataResolver.h"
#include "MiscFeatures.h"
#include "RagePlayerNames.h"
#include "RageV8Runtime.h"
#include "RageWorldEntities.h"
#include "RuntimeProfiler.h"
#include "Visuals.h"
#include "CloudStorage.h"
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>

namespace {
constexpr int kMaxPending = 16;
constexpr int kNameCapacity = 64;
// The runtime wraps every source in a try/catch before handing it to V8,
// so the editor ceiling stays below the runtime limit by that margin.
constexpr int kMaxScriptBytes =
    static_cast<int>(RageV8Runtime::kMaxScriptLength) - 128;
// A corrupt header must not turn into an unbounded read loop.
constexpr std::uint32_t kMaxStoredScripts = 100000;
constexpr std::uint32_t kStoreMagic = 0x53543851U;
constexpr std::uint32_t kStoreVersion = 4;
// Versions 1-3 were fixed-stride: twelve slots of one fixed-size record.
constexpr int kLegacySlotCount = 12;
constexpr int kLegacySourceCapacity = 16384;
constexpr wchar_t kStorePath[] = L"C:\\RAGEMP\\user_scripts.bin";

// Live record. The source lives on the heap so a slot can hold a whole
// file instead of a fixed 16 KB, and the array only ever holds real
// scripts, so there is no "used" flag and no free-slot search.
struct ScriptEntry final {
  char name[kNameCapacity];
  char *source;
  int sourceLength;
  int sourceCapacity;
  bool favorite;
  std::int64_t createdAt;
  std::int64_t modifiedAt;
};

// Legacy on-disk records. The three versions share the leading
// used/name/source layout, which is what the migration reads.
struct ScriptEntryV3 final {
  bool used;
  char name[kNameCapacity];
  char source[kLegacySourceCapacity];
  bool favorite;
  std::int64_t createdAt;
  std::int64_t modifiedAt;
};

struct ScriptEntryV2 final {
  bool used;
  char name[kNameCapacity];
  char source[kLegacySourceCapacity];
  std::int64_t createdAt;
  std::int64_t modifiedAt;
};

struct ScriptEntryV1 final {
  bool used;
  char name[kNameCapacity];
  char source[kLegacySourceCapacity];
};

struct StoreHeader final {
  std::uint32_t magic{};
  std::uint32_t version{};
  // Fixed stride for versions 1-3, unused (0) from version 4 on.
  std::uint32_t entrySize{};
  std::uint32_t entryCount{};
};

// Version 4 record prefix. Name and source bytes follow it inline, so the
// stride is per-entry instead of global. Field order avoids implicit
// padding, which keeps the layout stable across compilers.
struct StoreRecord final {
  std::uint32_t sourceLength{};
  std::uint32_t flags{};
  std::uint16_t nameLength{};
  std::uint16_t reserved0{};
  std::uint32_t reserved1{};
  std::int64_t createdAt{};
  std::int64_t modifiedAt{};
};

constexpr std::uint32_t kRecordFlagFavorite = 1u << 0;

struct PendingCommand final {
  std::uint64_t id{};
  char scriptName[64]{};
};

// Growable text buffer for the editor drafts. ImGui resizes it through
// the input-text callback, so pasting a large file is not truncated.
struct TextBuffer final {
  char *data;
  int capacity;
};

ScriptEntry *g_Scripts = nullptr;
int g_ScriptCount = 0;
int g_ScriptCapacity = 0;
TextBuffer g_DraftSource{};
char g_ImportPath[512]{};
PendingCommand g_Pending[kMaxPending]{};
int g_PendingCount = 0;
std::uint64_t g_RuntimeGeneration = 0;
int g_SelectedScript = -1;
bool g_LibraryLoaded = false;
bool g_ReconnectKeyDown = false;
ScriptBridge::Page g_Page = ScriptBridge::Page::Scripts;

// The payload is manual-mapped and the CRT never initializes, so the heap
// is addressed directly instead of through malloc.
void *HeapAllocate(std::size_t bytes) noexcept {
  return HeapAlloc(GetProcessHeap(), 0, bytes ? bytes : 1);
}

void HeapRelease(void *memory) noexcept {
  if (memory)
    HeapFree(GetProcessHeap(), 0, memory);
}

char LowerAscii(char value) noexcept {
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A'))
                                      : value;
}

bool ContainsInsensitive(const char *text, const char *query) noexcept {
  if (!query || !query[0])
    return true;
  if (!text)
    return false;
  for (int start = 0; text[start]; ++start) {
    int offset = 0;
    while (query[offset] && text[start + offset] &&
           LowerAscii(query[offset]) == LowerAscii(text[start + offset]))
      ++offset;
    if (!query[offset])
      return true;
  }
  return false;
}

const char *StateName(RageV8Runtime::State state) {
  switch (state) {
  case RageV8Runtime::State::Uninitialized:
    return "Idle";
  case RageV8Runtime::State::Resolving:
    return "Connecting";
  case RageV8Runtime::State::WaitingForApi:
#if RAGEMP_DEV_BUILD
    return "Waiting for RAGE API";
#else
    return "Connecting";
#endif
  case RageV8Runtime::State::Ready:
    return "Ready";
  case RageV8Runtime::State::Error:
    return "Error";
  default:
    return "Unknown";
  }
}

const char *SourceText(const ScriptEntry &entry) noexcept {
  return entry.source ? entry.source : "";
}

bool EnsureSourceCapacity(ScriptEntry &entry, int bytes) noexcept {
  if (bytes < 1)
    bytes = 1;
  if (entry.sourceCapacity >= bytes)
    return true;
  char *grown =
      static_cast<char *>(HeapAllocate(static_cast<std::size_t>(bytes)));
  if (!grown)
    return false;
  grown[0] = 0;
  HeapRelease(entry.source);
  entry.source = grown;
  entry.sourceCapacity = bytes;
  return true;
}

bool SetSource(ScriptEntry &entry, const char *source, int length) noexcept {
  if (!source)
    length = 0;
  if (length < 0)
    length = 0;
  if (length > kMaxScriptBytes)
    length = kMaxScriptBytes;
  if (!EnsureSourceCapacity(entry, length + 1))
    return false;
  if (length) {
    CopyMemory(entry.source, source, static_cast<std::size_t>(length));
  }
  entry.source[length] = 0;
  entry.sourceLength = length;
  return true;
}

bool ReserveScripts(int wanted) noexcept {
  if (wanted <= g_ScriptCapacity)
    return true;
  int capacity = g_ScriptCapacity > 0 ? g_ScriptCapacity : 16;
  while (capacity < wanted)
    capacity *= 2;
  auto *grown = static_cast<ScriptEntry *>(
      HeapAllocate(sizeof(ScriptEntry) * static_cast<std::size_t>(capacity)));
  if (!grown)
    return false;
  ZeroMemory(grown, sizeof(ScriptEntry) * static_cast<std::size_t>(capacity));
  // Source ownership moves with the record, so the old array is freed
  // without touching the buffers it pointed at.
  for (int index = 0; index < g_ScriptCount; ++index)
    grown[index] = g_Scripts[index];
  HeapRelease(g_Scripts);
  g_Scripts = grown;
  g_ScriptCapacity = capacity;
  return true;
}

void SetEntry(int index, const char *name, const char *source,
              int sourceLength) noexcept {
  if (index < 0 || index >= g_ScriptCount)
    return;
  ScriptEntry &entry = g_Scripts[index];
  const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
  if (entry.createdAt <= 0)
    entry.createdAt = now;
  entry.modifiedAt = now;
  lstrcpynA(entry.name, name && name[0] ? name : "Script", kNameCapacity);
  SetSource(entry, source, sourceLength);
}

void SetEntry(int index, const char *name, const char *source) noexcept {
  SetEntry(index, name, source, source ? lstrlenA(source) : 0);
}

int AppendEntry(const char *name, const char *source) noexcept {
  if (!ReserveScripts(g_ScriptCount + 1))
    return -1;
  g_Scripts[g_ScriptCount] = ScriptEntry{};
  ++g_ScriptCount;
  SetEntry(g_ScriptCount - 1, name, source);
  return g_ScriptCount - 1;
}

void ClearLibrary() noexcept {
  for (int index = 0; index < g_ScriptCount; ++index) {
    HeapRelease(g_Scripts[index].source);
    g_Scripts[index] = ScriptEntry{};
  }
  g_ScriptCount = 0;
  g_SelectedScript = -1;
}

void CreateDefaults() noexcept {
  ClearLibrary();
}

bool WriteAll(HANDLE file, const void *data, DWORD bytes) noexcept {
  if (!bytes)
    return true;
  DWORD written = 0;
  return WriteFile(file, data, bytes, &written, nullptr) != FALSE &&
         written == bytes;
}

bool ReadAll(HANDLE file, void *data, DWORD bytes) noexcept {
  if (!bytes)
    return true;
  DWORD read = 0;
  return ReadFile(file, data, bytes, &read, nullptr) != FALSE && read == bytes;
}

bool SaveLibrary() noexcept {
  CloudStorage::Item cloudItems[CloudStorage::kMaxItems];
  int cloudCount = 0;
  if (CloudStorage::List("script", cloudItems, CloudStorage::kMaxItems, cloudCount)) {
    for (int c = 0; c < cloudCount; ++c) {
      bool found = false;
      for (int l = 0; l < g_ScriptCount; ++l) {
        if (_stricmp(cloudItems[c].name, g_Scripts[l].name) == 0) {
          found = true;
          break;
        }
      }
      if (!found) {
        CloudStorage::Delete("script", cloudItems[c].id);
      }
    }
  }
  for (int index = 0; index < g_ScriptCount; ++index) {
    CloudStorage::Upsert("script", g_Scripts[index].name, SourceText(g_Scripts[index]));
  }
  return true;
}

void EnsureLibraryLoaded() noexcept {
  if (g_LibraryLoaded)
    return;

  g_LibraryLoaded = true;

  if (!CloudStorage::IsAvailable() && !CloudStorage::EnsureSession())
      return;
  CloudStorage::Item items[CloudStorage::kMaxItems];
  int count = 0;
  if (CloudStorage::List("script", items, CloudStorage::kMaxItems, count)) {
      ClearLibrary();
      char *buf = new (std::nothrow) char[kMaxScriptBytes + 1];
      if (buf) {
          for (int i = 0; i < count; ++i) {
              buf[0] = 0;
              if (CloudStorage::Get("script", items[i].id, buf, kMaxScriptBytes + 1)) {
                  AppendEntry(items[i].name, buf);
              }
          }
          delete[] buf;
      }
  }

  for (int index = 0; index < g_ScriptCount; ++index) {
    ScriptEntry &script = g_Scripts[index];
    script.name[kNameCapacity - 1] = 0;
    if (!script.name[0])
      lstrcpynA(script.name, "Untitled", kNameCapacity);
    if (script.createdAt <= 0 || script.modifiedAt <= 0) {
      const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
      if (script.createdAt <= 0)
        script.createdAt = now;
      if (script.modifiedAt <= 0)
        script.modifiedAt = script.createdAt;
    }
  }
  g_SelectedScript = 0;
}

int AddScript() noexcept {
  char name[kNameCapacity]{};
  _snprintf(name, sizeof(name) - 1, "New Script %d", g_ScriptCount + 1);
  const int index = AppendEntry(name, "// Write synchronous JavaScript here.\n"
                                      "return 'ready';");
  if (index < 0) {
    Menu::PushNotification("Script Library", "Out of memory for a new script.",
                           false);
    return -1;
  }
  g_SelectedScript = index;
  SaveLibrary();
  return index;
}

void RemoveScript(int index) noexcept {
  if (index < 0 || index >= g_ScriptCount)
    return;
  HeapRelease(g_Scripts[index].source);
  for (int move = index; move + 1 < g_ScriptCount; ++move)
    g_Scripts[move] = g_Scripts[move + 1];
  --g_ScriptCount;
  g_Scripts[g_ScriptCount] = ScriptEntry{};
  if (g_SelectedScript >= g_ScriptCount)
    g_SelectedScript = g_ScriptCount - 1;
  if (g_ScriptCount == 0) {
    AddScript();
    return;
  }
  SaveLibrary();
}

bool TrackCommand(std::uint64_t id, const char *scriptName) noexcept {
  if (!id || g_PendingCount >= kMaxPending)
    return false;
  PendingCommand &command = g_Pending[g_PendingCount++];
  command = {};
  command.id = id;
  lstrcpynA(command.scriptName, scriptName ? scriptName : "Script",
            static_cast<int>(sizeof(command.scriptName)));
  return true;
}

void ExecuteSource(const char *source, const char *name) noexcept {
  std::uint64_t commandId = 0;
  if (!RageV8Runtime::Enqueue(source, &commandId)) {
    Menu::PushNotification("Scripts",
                           "Unable to queue script. Runtime is not ready, "
                           "the queue is full, or the script is too large.",
                           false, 6000);
    return;
  }
  if (!TrackCommand(commandId, name)) {
    Menu::PushNotification(
        "Scripts", "Too many pending commands are awaiting results.", false);
  }
}

bool EnsureTextCapacity(TextBuffer &buffer, int bytes) noexcept {
  if (bytes < 1)
    bytes = 1;
  if (bytes > kMaxScriptBytes + 1)
    bytes = kMaxScriptBytes + 1;
  if (buffer.capacity >= bytes)
    return true;
  int capacity = buffer.capacity > 0 ? buffer.capacity : 4096;
  while (capacity < bytes)
    capacity *= 2;
  if (capacity > kMaxScriptBytes + 1)
    capacity = kMaxScriptBytes + 1;
  char *grown =
      static_cast<char *>(HeapAllocate(static_cast<std::size_t>(capacity)));
  if (!grown)
    return false;
  if (buffer.data) {
    const int copy = buffer.capacity < capacity ? buffer.capacity : capacity;
    CopyMemory(grown, buffer.data, static_cast<std::size_t>(copy));
    grown[copy - 1] = 0;
  } else {
    grown[0] = 0;
  }
  HeapRelease(buffer.data);
  buffer.data = grown;
  buffer.capacity = capacity;
  return true;
}

void AssignText(TextBuffer &buffer, const char *text) noexcept {
  const int length = text ? lstrlenA(text) : 0;
  if (!EnsureTextCapacity(buffer, length + 1))
    return;
  const int copy = length < buffer.capacity - 1 ? length : buffer.capacity - 1;
  if (copy > 0) {
    CopyMemory(buffer.data, text, static_cast<std::size_t>(copy));
  }
  buffer.data[copy] = 0;
}

// ImGui hands us the length it wants to store; growing here is what lets
// a paste of a whole file survive instead of being clipped.
int SourceResizeCallback(ImGuiInputTextCallbackData *data) noexcept {
  if (data->EventFlag != ImGuiInputTextFlags_CallbackResize)
    return 0;
  auto *buffer = static_cast<TextBuffer *>(data->UserData);
  EnsureTextCapacity(*buffer, data->BufTextLen + 1);
  // Reporting the real capacity is also how a refused growth is
  // communicated: ImGui truncates instead of overrunning.
  data->Buf = buffer->data;
  data->BufSize = buffer->capacity;
  return 0;
}

bool ImportSourceFromFile(const char *path, TextBuffer &buffer) noexcept {
  if (!path || !path[0])
    return false;
  HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    return false;
  LARGE_INTEGER size{};
  bool success = GetFileSizeEx(file, &size) != FALSE && size.QuadPart >= 0 &&
                 size.QuadPart <= static_cast<LONGLONG>(kMaxScriptBytes);
  if (success) {
    const int length = static_cast<int>(size.QuadPart);
    success = EnsureTextCapacity(buffer, length + 1) &&
              ReadAll(file, buffer.data, static_cast<DWORD>(length));
    if (success) {
      buffer.data[length] = 0;
      // A UTF-8 BOM would reach eval() as a stray character.
      if (length >= 3 && static_cast<unsigned char>(buffer.data[0]) == 0xEF &&
          static_cast<unsigned char>(buffer.data[1]) == 0xBB &&
          static_cast<unsigned char>(buffer.data[2]) == 0xBF) {
        MoveMemory(buffer.data, buffer.data + 3,
                   static_cast<std::size_t>(length) - 2);
      }
    }
  }
  CloseHandle(file);
  return success;
}

int RenderScriptEditorModal(bool &targetOpen, float &animation, bool &focusName,
                            char *name, int nameCapacity, TextBuffer &source,
                            bool isNew) noexcept {
  if (!EnsureTextCapacity(source, 1))
    return 0;
  const float dt = ImClamp(ImGui::GetIO().DeltaTime, 0.f, 0.05f);
  animation = ImClamp(animation + (targetOpen ? 1.f : -1.f) * dt *
                                      (targetOpen ? 5.f : 7.f),
                      0.f, 1.f);
  if (!targetOpen && animation <= 0.001f)
    return 0;
  const float eased = 0.5f - 0.5f * cosf(animation * 3.14159265358979323846f);
  const float scale =
      ImClamp(static_cast<float>(Menu::s_UIScalePercent) / 100.f, 1.f, 2.f);
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const ImVec2 modalSize(ImMin(760.f * scale, viewport->WorkSize.x - 32.f),
                         ImMin(570.f * scale, viewport->WorkSize.y - 32.f));
  const ImVec2 center = viewport->WorkPos + viewport->WorkSize * 0.5f +
                        ImVec2(0.f, 14.f * (1.f - eased) * scale);

  ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.f));
  ImGuiWindowFlags backdropFlags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
  // It now sits over the menu rather than under it, so while the sheet
  // fades out it must stop swallowing the clicks meant for the page.
  if (!targetOpen)
    backdropFlags |= ImGuiWindowFlags_NoInputs;
  ImGui::Begin("##script_editor_backdrop", nullptr, backdropFlags);
  // Behind the editor rather than around it: the dim was four strips
  // leaving a square hole, so the rounded corners sat in undimmed
  // wedges and the whole sheet read as a lit frame around the window.
  ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
  ImGui::InvisibleButton("##dismiss", viewport->WorkSize);
  if (targetOpen && ImGui::IsItemClicked())
    targetOpen = false;
  if (ImDrawList *backdropDraw = ImGui::GetWindowDrawList()) {
    backdropDraw->AddRectFilled(
        viewport->WorkPos, viewport->WorkPos + viewport->WorkSize,
        IM_COL32(0, 0, 0, static_cast<int>(158.f * eased)));
  }
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(3);

  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(modalSize, ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * eased);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.f, 14.f) * scale);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.f, 0.f, 0.f, 0.f));
  ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
      // The editor already shrinks to fit, so a scrollbar can only ever
      // be an artifact along the right edge.
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  if (!targetOpen)
    flags |= ImGuiWindowFlags_NoInputs;

  int result = 0;
  if (ImGui::Begin("##script_editor_modal", nullptr, flags)) {
    // Ahead of the backdrop, which was just put in front of the menu.
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImDrawList *draw = ImGui::GetWindowDrawList();
    const ImVec2 windowPos = ImGui::GetWindowPos();
    const ImVec2 windowMax = windowPos + ImGui::GetWindowSize();
    const float headerH = 58.f * scale;
    draw->AddRectFilled(windowPos, windowMax,
                        gui->get_clr(clr->window_background, 0.995f),
                        12.f * scale);
    draw->AddRect(windowPos, windowMax,
                  gui->get_clr(clr->child_stroke_bar, 0.92f), 12.f * scale, 0,
                  1.f);
    draw->AddRectFilled(windowPos, ImVec2(windowMax.x, windowPos.y + headerH),
                        gui->get_clr(clr->child_top_bar, 0.68f), 12.f * scale,
                        ImDrawFlags_RoundCornersTop);
    // Icon and title share one baseline instead of two hand-picked
    // offsets: each is centred on the middle of the header band and the
    // title starts a fixed gap after the glyph, so the pair reads as
    // one line whatever the font metrics happen to be.
    ImFont *iconFont = set->icon ? set->icon : ImGui::GetFont();
    ImFont *titleFont = ImGui::GetFont();
    const char *headerTitle =
        isNew ? Menu::Translate("New script", "Новый скрипт", "Новий скрипт")
              : Menu::Translate("Edit script", "Редактирование", "Редагування");
    const float iconSize = 15.f * scale;
    const float titleSize = titleFont->FontSize;
    const ImVec2 iconExtent =
        iconFont->CalcTextSizeA(iconSize, FLT_MAX, 0.f, ICON_FA_FILE_CODE);
    const float headerMid = windowPos.y + headerH * 0.5f;
    const float iconX = windowPos.x + 18.f * scale;
    draw->AddText(
        iconFont, iconSize,
        ImVec2(IM_ROUND(iconX), IM_ROUND(headerMid - iconExtent.y * 0.5f)),
        gui->get_clr(clr->accent_color), ICON_FA_FILE_CODE);
    draw->AddText(titleFont, titleSize,
                  ImVec2(IM_ROUND(iconX + iconExtent.x + 10.f * scale),
                         IM_ROUND(headerMid - titleSize * 0.5f)),
                  gui->get_clr(clr->text_active), headerTitle);
    ImGui::SetCursorScreenPos(windowPos +
                              ImVec2(16.f, headerH / scale + 12.f) * scale);
    if (focusName) {
      ImGui::SetKeyboardFocusHere();
      focusName = false;
    }
    gui->text_field(
        "##script_modal_name",
        Menu::Translate("Script name", "Имя скрипта", "Ім'я скрипта"),
        ImVec2(ImGui::GetContentRegionAvail().x, 34.f * scale), name,
        static_cast<std::size_t>(nameCapacity));
    ImGui::Dummy(ImVec2(0.f, 5.f * scale));
    ImGui::TextDisabled("JavaScript · %d B", lstrlenA(source.data));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, clr->element_background);
    ImGui::PushStyleColor(ImGuiCol_Border, clr->child_stroke_bar);
    ImGui::InputTextMultiline(
        "##script_modal_source", source.data,
        static_cast<std::size_t>(source.capacity),
        // Reserves the import row and the button row below it, so the
        // editor gives up height instead of pushing them out of view.
        ImVec2(-1.f, -92.f * scale),
        ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize,
        SourceResizeCallback, &source);
    ImGui::PopStyleColor(2);
    const float gap = 8.f * scale;
    // Pasting a megabyte through the clipboard is painful, so a path
    // plus Import reads the file straight into the draft.
    //
    // It sits on the button row rather than beside the header, where a
    // raw input field floating at the right edge read as a stray frame
    // in the corner of the window.
    const float importWidth = 104.f * scale;
    ImGui::SetNextItemWidth(ImMax(
        80.f * scale, ImGui::GetContentRegionAvail().x - importWidth - gap));
    ImGui::InputTextWithHint("##script_modal_path",
                             Menu::Translate("C:\\path\\script.js",
                                             "C:\\путь\\script.js",
                                             "C:\\шлях\\script.js"),
                             g_ImportPath, sizeof(g_ImportPath));
    ImGui::SameLine(0.f, gap);
    if (MenuUi::ActionButton("script_modal_import",
                             Menu::Translate("Import", "Импорт", "Імпорт"),
                             ImVec2(importWidth, 26.f * scale), false,
                             ICON_FA_FILE_IMPORT)) {
      const bool imported = ImportSourceFromFile(g_ImportPath, source);
      Menu::PushNotification("Script Library",
                             imported ? "Source imported from file."
                                      : "Unable to read that file, or it is "
                                        "larger than the script limit.",
                             imported, imported ? 2500 : 6000);
    }
    ImGui::Dummy(ImVec2(0.f, 4.f * scale));
    const float buttonW = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
    if (MenuUi::ActionButton("script_modal_cancel",
                             Menu::Translate("Cancel", "Отмена", "Скасувати"),
                             ImVec2(buttonW, 32.f * scale))) {
      targetOpen = false;
      result = 2;
    }
    ImGui::SameLine(0.f, gap);
    if (MenuUi::ActionButton("script_modal_save",
                             Menu::Translate("Save", "Сохранить", "Зберегти"),
                             ImVec2(buttonW, 32.f * scale), true,
                             ICON_FA_FLOPPY_DISK)) {
      result = 1;
    }
  }
  ImGui::End();

  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(4);
  return result;
}

} // namespace

namespace ScriptBridge {
bool Initialize() {
  EnsureLibraryLoaded();
  return RageV8Runtime::Initialize();
}

void Update() {
  // F1 is where a soft reconnect usually starts, so it is worth stopping
  // work briefly, but it is not evidence that one is happening: the same
  // key is pressed by accident, and the runtime detects a real reconnect
  // from the isolate context regardless of any key. So the press only
  // pauses, and the pause expires on its own.
  const bool reconnectKeyDown = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
  if (reconnectKeyDown && !g_ReconnectKeyDown)
    RageV8Runtime::PauseForSuspectedReconnect();
  g_ReconnectKeyDown = reconnectKeyDown;

  RageV8Runtime::Update();
  const auto runtime = RageV8Runtime::GetStatus();
  if (runtime.generation != g_RuntimeGeneration) {
    const bool hadPending = g_RuntimeGeneration != 0 && g_PendingCount != 0;
    g_RuntimeGeneration = runtime.generation;
    g_PendingCount = 0;
    for (auto &pending : g_Pending)
      pending = {};
    Game::ClearSessionCaches();
    RageWorldEntities::Clear();
    if (hadPending) {
      Menu::PushNotification(
          "Scripts", "Pending scripts canceled after reconnect", false, 5000);
    }
  }
  bool metadataActive = Menu::NeedsPlayerCache() || Features::g_EspEnabled ||
                        Features::g_AimbotEnabled;
  for (int index = 0; index < static_cast<int>(Menu::EspProfileId::Count);
       ++index) {
    metadataActive |= Menu::s_EspProfiles[index].enabled;
  }
  // Live eval of __ctx__.api still faults at multiplayer.dll+0xCC4F3
  // even after a 10s generation delay. Keep overlay TryGet, do not eval.
  constexpr bool kEnableFiveRpMetadata = false;
  if (kEnableFiveRpMetadata) {
    FiveRpMetadataResolver::Update(metadataActive);
  }
  // Sweeping the whole player pool is only worth it while the roster is
  // on: the nearby count comes from the nametag snapshot and needs none of
  // it. Keyed on the count toggle, that sweep ran for people who only
  // wanted to know whether anyone was watching.
  FiveRpAdminScan::Update(
      MiscFeatures::GetSettings().showAdminList ||
      MiscFeatures::GetSettings().specDetect);
  const auto namesProfile = RuntimeProfiler::Begin();
  RagePlayerNames::Update();
  RageWorldEntities::Update();
  RuntimeProfiler::End(RuntimeProfiler::Scope::Names, namesProfile);

  int write = 0;
  for (int index = 0; index < g_PendingCount; ++index) {
    // Probe first: a Result carries 32 KB, and this loop runs every
    // frame for every pending script.
    if (RageV8Runtime::HasResultFor(g_Pending[index].id)) {
      RageV8Runtime::Result result{};
      if (!RageV8Runtime::PollResultFor(g_Pending[index].id, result))
        continue;
      char title[96]{};
      _snprintf(title, sizeof(title) - 1, "%s: %s", g_Pending[index].scriptName,
                result.success ? "completed" : "failed");
      Menu::PushNotification(title,
                             result.text[0] ? result.text : "<empty result>",
                             result.success, result.success ? 5000 : 8000);
      continue;
    }
    if (write != index)
      g_Pending[write] = g_Pending[index];
    ++write;
  }
  g_PendingCount = write;
}

void RunDirect() {
  EnsureLibraryLoaded();
  if (g_SelectedScript < 0 || g_SelectedScript >= g_ScriptCount)
    return;
  ExecuteSource(SourceText(g_Scripts[g_SelectedScript]),
                g_Scripts[g_SelectedScript].name);
}

// One library for every build: scripts live in a local store, never in the
// cloud.
void RenderLibraryUI() {
  EnsureLibraryLoaded();
  const auto status = RageV8Runtime::GetStatus();
  const bool ready = status.state == RageV8Runtime::State::Ready;
  const float scale =
      ImClamp(static_cast<float>(Menu::s_UIScalePercent) / 100.f, 1.f, 2.f);
  static char search[64]{};
  static char draftName[kNameCapacity]{};
  static int editIndex = -2;
  static int pendingEdit = -2;
  static int pendingDelete = -1;
  static bool editorOpen = false;
  static bool focusName = false;
  static float editorAnimation = 0.f;

  auto CreateBlank = [&]() {
    char generated[kNameCapacity]{};
    for (int number = 1;; ++number) {
      _snprintf(generated, sizeof(generated) - 1, "New Script %d", number);
      bool exists = false;
      for (int index = 0; index < g_ScriptCount; ++index) {
        if (_stricmp(g_Scripts[index].name, generated) == 0) {
          exists = true;
          break;
        }
      }
      if (!exists)
        break;
    }
    const int created = AppendEntry(generated, "");
    if (created < 0) {
      Menu::PushNotification("Script Library",
                             "Out of memory for a new script.", false);
      return;
    }
    g_SelectedScript = created;
    SaveLibrary();
  };
  auto OpenEdit = [&](int index) {
    if (index < 0 || index >= g_ScriptCount)
      return;
    editIndex = index;
    lstrcpynA(draftName, g_Scripts[index].name, sizeof(draftName));
    AssignText(g_DraftSource, SourceText(g_Scripts[index]));
    focusName = false;
    editorOpen = true;
  };
  if (pendingEdit >= 0) {
    OpenEdit(pendingEdit);
    pendingEdit = -2;
  }
  Menu::SetRenderPhase("scripts:header");
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 6.f * scale));
  MenuUi::BeginInspectorIsland("scripts_library_header", nullptr, -1.f);
  const float actionGap = 8.f * scale;
  const float newWidth = 138.f * scale;
  const float searchWidth = ImMax(
      150.f * scale, ImGui::GetContentRegionAvail().x - newWidth - actionGap);
  ImGui::BeginGroup();
  MenuUi::SearchRow("scripts_redesign_search",
                    Menu::Translate("Search scripts...", "Поиск скриптов...",
                                    "Пошук скриптів..."),
                    search, sizeof(search), ImVec2(searchWidth, 34.f * scale));
  ImGui::SameLine(0.f, actionGap);
  if (MenuUi::ActionButton(
          "scripts_redesign_new",
          Menu::Translate("New script", "Новый скрипт", "Новий скрипт"),
          ImVec2(newWidth, 34.f * scale), true, ICON_FA_FILE_CIRCLE_PLUS)) {
    CreateBlank();
  }
  ImGui::EndGroup();
  MenuUi::EndInspectorIsland();

  int shown = 0;
  static char s_CardPhase[32]{};
  for (int index = 0; index < g_ScriptCount; ++index) {
    _snprintf(s_CardPhase, sizeof(s_CardPhase) - 1, "scripts:card %d", index);
    Menu::SetRenderPhase(s_CardPhase);
    ScriptEntry &entry = g_Scripts[index];
    if (!ContainsInsensitive(entry.name, search))
      continue;

    ImGui::PushID(index);
    char stamp[64]{};
    SYSTEMTIME local{};
    if (TimeUtil::LocalTimeFromUnix(entry.modifiedAt, local)) {
      _snprintf(stamp, sizeof(stamp) - 1, "%s %02d.%02d.%02d %02d:%02d",
                entry.modifiedAt > entry.createdAt
                    ? Menu::Translate("Edited", "Изменён", "Змінено")
                    : Menu::Translate("Created", "Создан", "Створено"),
                local.wDay, local.wMonth, local.wYear % 100, local.wHour,
                local.wMinute);
    }
    MenuUi::ScriptCardDesc card{};
    card.title = entry.name[0] ? entry.name : "Untitled";
    card.stamp = stamp[0] ? stamp : nullptr;
    card.subtitle = "JavaScript";
    card.runLabel = Menu::Translate("Load", "Загрузить", "Завантажити");
    card.favorite = entry.favorite;
    card.canRun = ready;
    card.editableTitle = entry.name;
    card.editableCapacity = kNameCapacity;
    const MenuUi::ScriptCardResult action =
        MenuUi::ScriptCard("script_card", card);
    if (action.run) {
      g_SelectedScript = index;
      ExecuteSource(SourceText(entry), entry.name);
    }
    if (action.renamed) {
      entry.modifiedAt = static_cast<std::int64_t>(std::time(nullptr));
      SaveLibrary();
    }
    if (MenuUi::BeginOverflowPopup("script_card", 180.f)) {
      if (MenuUi::ActionButton(
              "edit", Menu::Translate("Edit", "Изменить", "Змінити"),
              ImVec2(-1.f, 28.f * scale), false, ICON_FA_PEN_TO_SQUARE)) {
        pendingEdit = index;
        MenuUi::CloseOverflowPopup();
      }
      if (MenuUi::ActionButton(
              "delete", Menu::Translate("Delete", "Удалить", "Видалити"),
              ImVec2(-1.f, 28.f * scale), false, ICON_FA_TRASH_CAN)) {
        // Deferred: entries are dense now, so removing one here
        // would shift the rest mid-iteration.
        pendingDelete = index;
        MenuUi::CloseOverflowPopup();
      }
      MenuUi::EndOverflowPopup();
    }
    ImGui::PopID();
    ++shown;
  }
  if (pendingDelete >= 0) {
    RemoveScript(pendingDelete);
    pendingDelete = -1;
  }
  if (shown == 0) {
    Menu::SetRenderPhase("scripts:empty");
    MenuUi::EmptyState(
        "scripts_redesign_empty",
        search[0] ? ICON_FA_MAGNIFYING_GLASS : ICON_FA_FILE_CODE,
        Menu::Translate("No scripts", "Нет скриптов", "Немає скриптів"),
        Menu::Translate("Create one or change the filter",
                        "Создайте скрипт или смените фильтр",
                        "Створіть скрипт або змініть фільтр"),
        150.f);
  }
  ImGui::PopStyleVar();

  Menu::SetRenderPhase("scripts:editor");
  const int editorAction =
      RenderScriptEditorModal(editorOpen, editorAnimation, focusName, draftName,
                              sizeof(draftName), g_DraftSource, editIndex < 0);
  if (editorAction == 1) {
    if (!draftName[0]) {
      Menu::PushNotification("Script Library", "Enter a script name.", false);
    } else {
      int destination = editIndex;
      if (destination < 0 || destination >= g_ScriptCount)
        destination = AppendEntry(draftName, "");
      if (destination >= 0) {
        SetEntry(destination, draftName, g_DraftSource.data,
                 g_DraftSource.data ? lstrlenA(g_DraftSource.data) : 0);
        g_SelectedScript = destination;
        SaveLibrary();
        editorOpen = false;
        Menu::PushNotification("Script Library", "Script saved.", true, 2500);
      } else {
        Menu::PushNotification("Script Library",
                               "Out of memory for a new script.", false);
      }
    }
  }
}

void OpenPage(Page page) noexcept {
#if RAGEMP_EVENT_LOGGER
  g_Page = page;
#else
  // Scripts is the only page a user build has, so the request is honoured
  // by ignoring it rather than by trusting a caller that cannot know.
  (void)page;
  g_Page = Page::Scripts;
#endif
}

Page CurrentPage() noexcept { return g_Page; }

void RenderUI() {
#if RAGEMP_EVENT_LOGGER
  // The two share a page and the category row above chooses between them.
  // Drawing both, which is what calling the logger unconditionally did,
  // put it on top of the script library instead of on its own tab.
  static Page s_LastPage = Page::Scripts;
  if (s_LastPage != g_Page) {
    s_LastPage = g_Page;
    ImGui::SetScrollY(0.f);
  }
  if (g_Page == Page::EventLogger) {
    EventLogger::RenderUI();
    return;
  }
#endif
  RenderLibraryUI();
}

} // namespace ScriptBridge
