#pragma once

#include "imgui.h"
#include <cstdint>

// Manual-mapped DLL: avoid std::map (pulls CRT/locale paths that may crash without CRT init).
// Fixed-slot animation table keyed by ImGuiID; lazy singleton via operator new -> GetProcessHeap().
// Keep Capacity small: each table lives in .bss and SizeOfImage must stay
// map-friendly for the kernel injector (~512 blew the image to ~10MB).
template<typename V, int Tag, int Capacity = 64>
struct LeyawinAnimTable
{
    struct Entry
    {
        ImGuiID key = 0;
        bool used = false;
        int lastFrame = -1;
        V value{};
    };

    Entry entries[Capacity] = {};

    V& get(ImGuiID id)
    {
        const int frame = ImGui::GetFrameCount();
        int free = -1;
        int oldest = 0;
        int oldestFrame = INT32_MAX;

        for (int i = 0; i < Capacity; ++i) {
            if (entries[i].used && entries[i].key == id) {
                entries[i].lastFrame = frame;
                return entries[i].value;
            }
            if (!entries[i].used) {
                if (free < 0)
                    free = i;
            } else if (entries[i].lastFrame < oldestFrame) {
                oldestFrame = entries[i].lastFrame;
                oldest = i;
            }
        }

        // Widgets come and go (script tiles, friend rows), so a full table has
        // to reclaim the entry nobody touched most recently. The old code always
        // overwrote slot 0, which made two live widgets fight over one state.
        const int slot = free >= 0 ? free : oldest;
        entries[slot].key = id;
        entries[slot].used = true;
        entries[slot].lastFrame = frame;
        entries[slot].value = V{};
        return entries[slot].value;
    }
};

template<typename V, int Tag, int Capacity = 64>
inline LeyawinAnimTable<V, Tag, Capacity>& LeyawinAnim()
{
    // BSS-backed singleton — no heap alloc on first widget use (manual-map safe).
    static LeyawinAnimTable<V, Tag, Capacity> s_table;
    return s_table;
}

#define LEYAWIN_ANIM(K, V) LeyawinAnim<V, __LINE__>()

template<typename Table, typename Key>
inline auto& MapGet(Table& table, const Key& key)
{
    return table.get(key);
}

// Sidebar / tabs: stable numeric ids — no ImGuiID hash table needed.
template<typename V, int Count>
struct LeyawinAnimArray
{
    V values[Count] = {};

    V& get(int index)
    {
        if (index < 0) index = 0;
        if (index >= Count) index = Count - 1;
        return values[index];
    }
};

template<typename Table>
inline auto& AnimGetIdx(Table& table, int index)
{
    return table.get(index);
}
