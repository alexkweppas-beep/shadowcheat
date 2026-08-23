#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdio>
#include <cstring>

#include "FeatureBinds.h"

extern HMODULE g_ThisModule;
// Captured in DllMain before the PE header wipe; StableId must not read the
// header afterwards.
extern uint32_t g_ThisModuleSize;

namespace FeatureBinds
{
    namespace
    {
        constexpr int kCapacity = 128;
        Binding g_Bindings[kCapacity]{};
        int g_Count = 0;
        volatile LONG g_CaptureActive = 0;

        struct Pending final
        {
            std::uint64_t stableId{};
            int key{};
            int mode{};
            bool used{};
        };
        Pending g_Pending[kCapacity]{};

        std::uint64_t HashLabel(const char* label) noexcept
        {
            std::uint64_t hash = 1469598103934665603ull;
            if (label)
            {
                for (const unsigned char* cursor =
                        reinterpret_cast<const unsigned char*>(label);
                     *cursor;
                     ++cursor)
                {
                    hash ^= *cursor;
                    hash *= 1099511628211ull;
                }
            }
            return hash | (1ull << 63);
        }

        std::uint64_t StableId(
            bool* value,
            const char* label) noexcept
        {
            const auto address =
                reinterpret_cast<std::uintptr_t>(value);
            const auto base =
                reinterpret_cast<std::uintptr_t>(g_ThisModule);
            // g_ThisModuleSize was captured before the header wipe; the
            // header itself is zeroed and must not be read here.
            if (base && g_ThisModuleSize && address >= base &&
                address < base + g_ThisModuleSize)
            {
                return address - base;
            }
            return HashLabel(label);
        }

        void CopyLabel(
            char* destination,
            std::size_t capacity,
            const char* source) noexcept
        {
            if (!destination || !capacity)
                return;
            if (!source)
                source = "";
            std::size_t index = 0;
            while (index + 1 < capacity && source[index])
            {
                if (source[index] == '#' &&
                    source[index + 1] == '#')
                    break;
                destination[index] = source[index];
                ++index;
            }
            destination[index] = 0;
        }

        void ApplyPending(Binding& binding) noexcept
        {
            for (Pending& pending : g_Pending)
            {
                if (!pending.used ||
                    pending.stableId != binding.stableId)
                    continue;
                binding.key = pending.key;
                binding.mode =
                    pending.mode == Hold ? Hold : Toggle;
                binding.previousDown =
                    binding.key > 0 &&
                    (GetAsyncKeyState(binding.key) & 0x8000) != 0;
                pending = {};
                return;
            }
        }
    }

    Binding* Register(
        bool* value,
        const char* label) noexcept
    {
        if (!value)
            return nullptr;
        for (int index = 0; index < g_Count; ++index)
        {
            if (g_Bindings[index].value != value)
                continue;
            CopyLabel(
                g_Bindings[index].label,
                sizeof(g_Bindings[index].label),
                label);
            return &g_Bindings[index];
        }
        if (g_Count >= kCapacity)
            return nullptr;
        Binding& binding = g_Bindings[g_Count++];
        binding = {};
        binding.value = value;
        binding.stableId = StableId(value, label);
        binding.mode = Toggle;
        CopyLabel(
            binding.label,
            sizeof(binding.label),
            label);
        ApplyPending(binding);
        return &binding;
    }

    Binding* RegisterAction(
        const char* id,
        const char* label,
        void (*action)()) noexcept
    {
        if (!action || !id || !*id)
            return nullptr;
        const std::uint64_t stableId = HashLabel(id);
        for (int index = 0; index < g_Count; ++index)
        {
            if (g_Bindings[index].value ||
                g_Bindings[index].stableId != stableId)
                continue;
            // The same action can sit on more than one page under different
            // wording; the last one drawn names the bind.
            CopyLabel(
                g_Bindings[index].label,
                sizeof(g_Bindings[index].label),
                label);
            g_Bindings[index].action = action;
            return &g_Bindings[index];
        }
        if (g_Count >= kCapacity)
            return nullptr;
        Binding& binding = g_Bindings[g_Count++];
        binding = {};
        binding.action = action;
        binding.stableId = stableId;
        binding.mode = Toggle;
        CopyLabel(
            binding.label,
            sizeof(binding.label),
            label);
        ApplyPending(binding);
        return &binding;
    }

    void Tick(bool suppressActivation) noexcept
    {
        const bool suppressed =
            suppressActivation ||
            IsCaptureActive();
        for (int index = 0; index < g_Count; ++index)
        {
            Binding& binding = g_Bindings[index];
            if ((!binding.value && !binding.action) ||
                binding.key <= 0 ||
                binding.key >= 256)
                continue;
            const bool down =
                (GetAsyncKeyState(binding.key) & 0x8000) != 0;
            if (!suppressed)
            {
                if (binding.action)
                {
                    if (down && !binding.previousDown)
                        binding.action();
                }
                else if (binding.mode == Hold)
                {
                    *binding.value = down;
                }
                else if (down && !binding.previousDown)
                {
                    *binding.value = !*binding.value;
                }
            }
            binding.previousDown = down;
        }
    }

    void SetCaptureActive(bool active) noexcept
    {
        InterlockedExchange(
            &g_CaptureActive,
            active ? 1 : 0);
    }

    bool IsCaptureActive() noexcept
    {
        return InterlockedCompareExchange(
            &g_CaptureActive, 0, 0) != 0;
    }

    void ResetEdge(Binding* binding) noexcept
    {
        if (!binding)
            return;
        binding->previousDown =
            binding->key > 0 &&
            binding->key < 256 &&
            (GetAsyncKeyState(binding->key) & 0x8000) != 0;
    }

    bool Export(
        char* output,
        std::size_t capacity,
        std::size_t& used) noexcept
    {
        if (!output || used >= capacity)
            return false;
        for (int index = 0; index < g_Count; ++index)
        {
            const Binding& binding = g_Bindings[index];
            if (!binding.key)
                continue;
            const int written = sprintf_s(
                output + used,
                capacity - used,
                "bind_%016llX=%d,%d\n",
                static_cast<unsigned long long>(
                    binding.stableId),
                binding.key,
                binding.mode == Hold ? Hold : Toggle);
            if (written <= 0)
                return false;
            used += static_cast<std::size_t>(written);
        }
        return true;
    }

    void Import(const char* content) noexcept
    {
        if (!content)
            return;
        const char* line = content;
        while (*line)
        {
            unsigned long long stableId = 0;
            int key = 0;
            int mode = Toggle;
            if (sscanf_s(
                    line,
                    "bind_%16llX=%d,%d",
                    &stableId,
                    &key,
                    &mode) == 3 &&
                key >= 0 && key < 256)
            {
                bool applied = false;
                for (int index = 0; index < g_Count; ++index)
                {
                    Binding& binding = g_Bindings[index];
                    if (binding.stableId != stableId)
                        continue;
                    binding.key = key;
                    binding.mode =
                        mode == Hold ? Hold : Toggle;
                    ResetEdge(&binding);
                    applied = true;
                    break;
                }
                if (!applied)
                {
                    for (Pending& pending : g_Pending)
                    {
                        if (pending.used &&
                            pending.stableId != stableId)
                            continue;
                        pending.used = true;
                        pending.stableId = stableId;
                        pending.key = key;
                        pending.mode =
                            mode == Hold ? Hold : Toggle;
                        break;
                    }
                }
            }
            const char* next = std::strchr(line, '\n');
            if (!next)
                break;
            line = next + 1;
        }
    }
}
