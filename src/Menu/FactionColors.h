#pragma once

namespace Menu
{
    // Faction text used to take one colour from the ESP profile, so every
    // faction on screen read the same and the tag only told you that somebody
    // was in one. Colours are keyed by the server's faction key (F_LSPD,
    // F_GANG_BALLAS and so on), which is stable across sessions, and any
    // faction the built-in table does not know still gets a colour of its own
    // derived from that key.
    namespace FactionColors
    {
        // The live server has sixteen factions; the rest of the room is for
        // ones added later and for the numeric legacy keys.
        constexpr int kCapacity = 40;
        constexpr int kKeyLength = 48;
        constexpr int kLabelLength = 24;

        struct Entry final
        {
            char key[kKeyLength]{};
            char label[kLabelLength]{};
            float color[4]{ 1.f, 1.f, 1.f, 1.f };
            // Set once the user picks a colour, so a later default change or a
            // config merge does not overwrite their choice.
            bool custom{};
            bool used{};
        };

        bool& Enabled() noexcept;

        // Colour for a faction, registering it on first sight so the settings
        // list shows the factions actually present on the server. Returns null
        // when there is no faction to colour.
        const float* Resolve(
            const char* key,
            const char* label) noexcept;

        int Count() noexcept;
        Entry* At(int index) noexcept;
        void ResetToDefaults() noexcept;

        Entry* EnsureSlot(
            const char* key,
            const char* label) noexcept;

        // Config plumbing. The config format carries numbers only, so a colour
        // is stored against a hash of the faction key instead of the key
        // itself. That still restores factions this build has never heard of:
        // the record waits until one of their members is seen.
        unsigned int KeyHash(const char* key) noexcept;
        int CustomCount() noexcept;
        bool CustomRecord(
            int index,
            unsigned int& hash,
            float* color) noexcept;
        void ApplySavedColor(
            unsigned int hash,
            const float* color) noexcept;
    }
}
