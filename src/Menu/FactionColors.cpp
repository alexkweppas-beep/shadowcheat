#include "FactionColors.h"

#include <Windows.h>
#include <cstdint>

namespace Menu::FactionColors
{
namespace
{
    struct Preset final
    {
        const char* key;
        const char* label;
        float r;
        float g;
        float b;
    };

    // Colours follow what the faction is, not a palette ramp: the emergency
    // services read as red, police as blue, gangs keep their own colours from
    // the game, so a tag can be recognised without reading it.
    constexpr Preset kPresets[] = {
        { "F_LSPD",            "LSPD",     0.23f, 0.51f, 0.96f },
        { "F_LSSD",            "LSSD",     0.79f, 0.64f, 0.15f },
        { "F_FIB",             "FIB",      0.39f, 0.40f, 0.95f },
        { "F_LSARMY",          "Army",     0.42f, 0.56f, 0.14f },
        { "F_LSCITYHALL",      "Gov",      0.65f, 0.55f, 0.98f },
        { "F_EMS",             "EMS",      0.94f, 0.27f, 0.27f },
        { "F_NEWS",            "News",     0.98f, 0.80f, 0.08f },
        { "F_PRISON",          "Prison",   0.58f, 0.64f, 0.72f },
        { "F_GANG_BALLAS",     "Ballas",   0.66f, 0.33f, 0.97f },
        { "F_GANG_BLOODS",     "Bloods",   0.86f, 0.15f, 0.15f },
        { "F_GANG_GROVE",      "Grove",    0.13f, 0.77f, 0.37f },
        { "F_GANG_MARABUNTA",  "Marabunta",0.02f, 0.71f, 0.83f },
        { "F_GANG_VAGOS",      "Vagos",    0.92f, 0.70f, 0.03f },
        { "F_RUSSIANMAFIA",    "RM",       0.85f, 0.85f, 0.88f },
        { "F_ARMENIAMAFIA",    "Armenian", 0.91f, 0.45f, 0.20f },
        { "F_MEXICOMAFIA",     "Mexican",  0.55f, 0.78f, 0.36f }
    };

    struct SavedColor final
    {
        std::uint32_t hash{};
        float color[4]{};
        bool used{};
    };

    Entry g_Entries[kCapacity]{};
    // Colours read from config for factions that have not been seen yet.
    SavedColor g_Saved[kCapacity]{};
    bool g_Enabled = true;
    bool g_Initialized = false;

    std::uint32_t HashKey(const char* key) noexcept
    {
        // FNV-1a over the upper-cased key, so the generated colour does not
        // depend on how the server happened to case it.
        std::uint32_t hash = 2166136261u;
        for (const char* c = key; *c; ++c)
        {
            char upper = *c;
            if (upper >= 'a' && upper <= 'z')
                upper = static_cast<char>(upper - 32);
            hash ^= static_cast<std::uint8_t>(upper);
            hash *= 16777619u;
        }
        return hash;
    }

    void HueToRgb(
        float hue,
        float saturation,
        float value,
        float* out) noexcept
    {
        const float h = hue * 6.f;
        const int sector = static_cast<int>(h) % 6;
        const float f = h - static_cast<float>(static_cast<int>(h));
        const float p = value * (1.f - saturation);
        const float q = value * (1.f - saturation * f);
        const float t = value * (1.f - saturation * (1.f - f));
        switch (sector)
        {
        case 0: out[0] = value; out[1] = t; out[2] = p; break;
        case 1: out[0] = q; out[1] = value; out[2] = p; break;
        case 2: out[0] = p; out[1] = value; out[2] = t; break;
        case 3: out[0] = p; out[1] = q; out[2] = value; break;
        case 4: out[0] = t; out[1] = p; out[2] = value; break;
        default: out[0] = value; out[1] = p; out[2] = q; break;
        }
    }

    void FillGeneratedColor(
        const char* key,
        float* out) noexcept
    {
        // Golden-ratio stepping spreads consecutive hashes across the wheel,
        // so two factions added back to back do not come out near-identical.
        const std::uint32_t hash = HashKey(key);
        const float scaled =
            static_cast<float>(hash & 0xFFFFFFu) /
            static_cast<float>(0x1000000u);
        float hue = scaled * 0.61803398875f + 0.61803398875f;
        hue -= static_cast<float>(static_cast<int>(hue));
        HueToRgb(hue, 0.68f, 1.f, out);
        out[3] = 1.f;
    }

    void ApplyDefault(Entry& entry) noexcept
    {
        for (const Preset& preset : kPresets)
        {
            if (lstrcmpiA(entry.key, preset.key) != 0)
                continue;
            entry.color[0] = preset.r;
            entry.color[1] = preset.g;
            entry.color[2] = preset.b;
            entry.color[3] = 1.f;
            if (!entry.label[0])
                lstrcpynA(entry.label, preset.label, kLabelLength);
            return;
        }
        FillGeneratedColor(entry.key, entry.color);
    }

    void EnsureInitialized() noexcept
    {
        if (g_Initialized)
            return;
        g_Initialized = true;
        // The known factions are seeded so the settings list is populated
        // before anyone from them is seen.
        int index = 0;
        for (const Preset& preset : kPresets)
        {
            if (index >= kCapacity)
                break;
            Entry& entry = g_Entries[index++];
            entry = Entry{};
            lstrcpynA(entry.key, preset.key, kKeyLength);
            lstrcpynA(entry.label, preset.label, kLabelLength);
            entry.color[0] = preset.r;
            entry.color[1] = preset.g;
            entry.color[2] = preset.b;
            entry.color[3] = 1.f;
            entry.used = true;
        }
    }
}

bool& Enabled() noexcept
{
    return g_Enabled;
}

Entry* EnsureSlot(
    const char* key,
    const char* label) noexcept
{
    EnsureInitialized();
    if (!key || !key[0])
        return nullptr;

    Entry* free = nullptr;
    for (auto& entry : g_Entries)
    {
        if (entry.used && lstrcmpiA(entry.key, key) == 0)
        {
            if (label && label[0] && !entry.label[0])
                lstrcpynA(entry.label, label, kLabelLength);
            return &entry;
        }
        if (!entry.used && !free)
            free = &entry;
    }
    if (!free)
        return nullptr;

    *free = Entry{};
    lstrcpynA(free->key, key, kKeyLength);
    if (label && label[0])
        lstrcpynA(free->label, label, kLabelLength);
    free->used = true;
    ApplyDefault(*free);

    // A colour saved for this faction before this session knew it existed.
    const std::uint32_t hash = HashKey(free->key);
    for (const auto& saved : g_Saved)
    {
        if (!saved.used || saved.hash != hash)
            continue;
        for (int c = 0; c < 4; ++c)
            free->color[c] = saved.color[c];
        free->custom = true;
        break;
    }
    return free;
}

const float* Resolve(
    const char* key,
    const char* label) noexcept
{
    if (!g_Enabled)
        return nullptr;
    const Entry* entry = EnsureSlot(key, label);
    return entry ? entry->color : nullptr;
}

int Count() noexcept
{
    EnsureInitialized();
    int count = 0;
    for (const auto& entry : g_Entries)
    {
        if (entry.used)
            ++count;
    }
    return count;
}

Entry* At(int index) noexcept
{
    EnsureInitialized();
    int seen = 0;
    for (auto& entry : g_Entries)
    {
        if (!entry.used)
            continue;
        if (seen == index)
            return &entry;
        ++seen;
    }
    return nullptr;
}

void ResetToDefaults() noexcept
{
    EnsureInitialized();
    for (auto& entry : g_Entries)
    {
        if (!entry.used)
            continue;
        entry.custom = false;
        ApplyDefault(entry);
    }
}

unsigned int KeyHash(const char* key) noexcept
{
    return key ? HashKey(key) : 0u;
}

int CustomCount() noexcept
{
    EnsureInitialized();
    int count = 0;
    for (const auto& entry : g_Entries)
    {
        if (entry.used && entry.custom)
            ++count;
    }
    return count;
}

bool CustomRecord(
    int index,
    unsigned int& hash,
    float* color) noexcept
{
    EnsureInitialized();
    if (!color || index < 0)
        return false;
    int seen = 0;
    for (const auto& entry : g_Entries)
    {
        if (!entry.used || !entry.custom)
            continue;
        if (seen == index)
        {
            hash = HashKey(entry.key);
            for (int c = 0; c < 4; ++c)
                color[c] = entry.color[c];
            return true;
        }
        ++seen;
    }
    return false;
}

void ApplySavedColor(
    unsigned int hash,
    const float* color) noexcept
{
    EnsureInitialized();
    if (!hash || !color)
        return;

    // Already registered: apply straight away.
    for (auto& entry : g_Entries)
    {
        if (!entry.used || HashKey(entry.key) != hash)
            continue;
        for (int c = 0; c < 4; ++c)
            entry.color[c] = color[c];
        entry.custom = true;
        return;
    }

    for (auto& saved : g_Saved)
    {
        if (saved.used && saved.hash != hash)
            continue;
        saved.used = true;
        saved.hash = hash;
        for (int c = 0; c < 4; ++c)
            saved.color[c] = color[c];
        return;
    }
}
}
