#pragma once

#include <cstddef>
#include <cstdint>

struct ImGuiStyle;

// Config serialisation for the whole menu: the local slot library on disk and
// the key format shared with feature modules.
namespace Menu
{
    inline constexpr std::size_t kConfigPayloadCapacity = 131072;

    bool AppendConfigLine(
        char* output,
        std::size_t capacity,
        std::size_t& used,
        const char* key,
        double value);

    const char* FindConfigValue(
        const char* content,
        const char* key);

    bool ExportCurrentConfig(char* output, std::size_t capacity);
    void ImportCurrentConfig(const char* content);

    // Local slot storage lives with the serialiser so pages only pass indices.
    // Names live in set->selection_config; everything else is here and is
    // written to disk, so a config survives a restart.
    char* LocalConfigSlot(int slot) noexcept;
    std::size_t LocalConfigSlotCapacity() noexcept;
    int& ConfigSlotCount() noexcept;
    std::int64_t LocalConfigCreatedAt(int slot) noexcept;
    std::int64_t LocalConfigModifiedAt(int slot) noexcept;
    void StampLocalConfig(int slot, bool created) noexcept;
    void MoveLocalConfigSlot(int destination, int source) noexcept;
    int& DefaultConfigSlot() noexcept;

    void EnsureLocalConfigsLoaded() noexcept;
    bool SaveLocalConfigs() noexcept;

    // Applies the slot marked as default, once, on the first menu frame.
    void TickConfigBootstrap() noexcept;
}
