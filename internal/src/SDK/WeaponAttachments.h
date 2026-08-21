#pragma once
#include <cstdint>

// Mirrors QuantToolsScript.h WPN_COMPAT / WPN_COMPS for Weapon Manager UI.
namespace WeaponAttachments
{
    struct Component final
    {
        std::uint32_t hash;
        const char* name;
        char type; // s g c o m
    };

    struct CompatEntry final
    {
        std::uint32_t weaponHash;
        const std::uint32_t* comps;
        int count;
    };

    inline constexpr Component kComponents[] = {
        {0x6927E1A1u, "Bell-End Muzzle Brake", 'm'},
        {0xC304849Au, "Suppressor", 's'},
        {0x65EA7EBBu, "Suppressor v2", 's'},
        {0x837445AAu, "Suppressor (AR)", 's'},
        {0xA73D4664u, "Suppressor (AR v2)", 's'},
        {0xE608B35Eu, "Suppressor (SR)", 's'},
        {0xAC42DF71u, "Suppressor (SR v3)", 's'},
        {0xC164F53u, "Grip", 'g'},
        {0x9D65907Au, "Grip v2", 'g'},
        {0x420FD713u, "Holo Sight", 'o'},
        {0x9FDB5652u, "Holo Sight (SMG)", 'o'},
        {0xED265A1Cu, "Extended Clip", 'c'},
        {0xD67B4F2Du, "Extended Clip", 'c'},
        {0x249A17D5u, "Extended Clip", 'c'},
        {0xD9D3AC92u, "Extended Clip", 'c'},
        {0x64F9C62Bu, "Extended Clip", 'c'},
        {0x5ED6C128u, "Extended Clip", 'c'},
        {0xCE8C0772u, "Extended Clip", 'c'},
        {0x10E6BA2Bu, "Extended Clip", 'c'},
        {0x350966FBu, "Extended Clip", 'c'},
        {0xBB46E417u, "Extended Clip", 'c'},
        {0x937ED0B7u, "Extended Clip", 'c'},
        {0xB9835B2Eu, "Extended Clip", 'c'},
        {0xB92C6979u, "Extended Clip", 'c'},
        {0x334A5203u, "Extended Clip", 'c'},
        {0x86BD7F72u, "Extended Clip", 'c'},
        {0x971CF6FDu, "Extended Clip", 'c'},
        {0xB1214F9Bu, "Extended Clip", 'c'},
        {0xD12ACA6Fu, "Extended Clip", 'c'},
        {0x91109691u, "Extended Clip", 'c'},
        {0x5DD5DBD5u, "Extended Clip", 'c'},
        {0x8EC1C979u, "Extended Clip", 'c'},
        {0x7C8BD10Eu, "Extended Clip", 'c'},
        {0xDE1FA12Cu, "Extended Clip", 'c'},
        {0xB3688B0Fu, "Extended Clip", 'c'},
        {0x59FF9BF8u, "Extended Clip", 'c'},
        {0x684ACE42u, "Extended Clip", 'c'},
        {0x82158B47u, "Extended Clip", 'c'},
        {0xD6C59CD6u, "Extended Clip", 'c'},
        {0x17DF42E9u, "Extended Clip", 'c'},
        {0xEFB00628u, "Extended Clip", 'c'},
        {0xEAC8C270u, "Extended Clip", 'c'},
        {0xCCFD2AC5u, "Extended Clip", 'c'},
        {0xE6CFD1AAu, "Extended Clip", 'c'},
        {0x2CD8FF9Du, "Extended Clip", 'c'},
    };

    inline constexpr int kComponentCount =
        static_cast<int>(sizeof(kComponents) / sizeof(kComponents[0]));

    inline const Component* FindComponent(std::uint32_t hash) noexcept
    {
        for (int i = 0; i < kComponentCount; ++i)
            if (kComponents[i].hash == hash)
                return &kComponents[i];
        return nullptr;
    }

    // Flat pairs: weapon hash followed by count then comps — use helper tables.
    inline constexpr std::uint32_t kPistol[] = {0x65EA7EBBu, 0xED265A1Cu};
    inline constexpr std::uint32_t kPistolMk2[] = {0x65EA7EBBu, 0x5ED6C128u};
    inline constexpr std::uint32_t kCombatPistol[] = {0xC304849Au, 0xD67B4F2Du};
    inline constexpr std::uint32_t kApPistol[] = {0xC304849Au, 0x249A17D5u};
    inline constexpr std::uint32_t kPistol50[] = {0xA73D4664u, 0xD9D3AC92u};
    inline constexpr std::uint32_t kHeavyPistol[] = {0xC304849Au, 0x64F9C62Bu};
    inline constexpr std::uint32_t kMicroSmg[] = {0xA73D4664u, 0x10E6BA2Bu};
    inline constexpr std::uint32_t kSmg[] = {0xC304849Au, 0x350966FBu};
    inline constexpr std::uint32_t kSmgMk2[] = {0xC304849Au, 0x9FDB5652u, 0xB9835B2Eu};
    inline constexpr std::uint32_t kAssaultSmg[] = {0xA73D4664u, 0xBB46E417u};
    inline constexpr std::uint32_t kCombatPdw[] = {0xC164F53u, 0x334A5203u};
    inline constexpr std::uint32_t kMiniSmg[] = {0x937ED0B7u};
    inline constexpr std::uint32_t kMachinePistol[] = {0xC304849Au, 0xB92C6979u};
    inline constexpr std::uint32_t kPump[] = {0xE608B35Eu};
    inline constexpr std::uint32_t kPumpMk2[] = {0xAC42DF71u, 0x420FD713u};
    inline constexpr std::uint32_t kAssaultSg[] = {0x837445AAu, 0xC164F53u, 0x86BD7F72u};
    inline constexpr std::uint32_t kBullpupSg[] = {0xA73D4664u, 0xC164F53u};
    inline constexpr std::uint32_t kHeavySg[] = {0xA73D4664u, 0xC164F53u, 0x971CF6FDu};
    inline constexpr std::uint32_t kAssaultRifle[] = {0xA73D4664u, 0xC164F53u, 0xB1214F9Bu};
    inline constexpr std::uint32_t kAssaultRifleMk2[] = {0xA73D4664u, 0x9D65907Au, 0x420FD713u, 0xD12ACA6Fu};
    inline constexpr std::uint32_t kCarbine[] = {0x837445AAu, 0xC164F53u, 0x91109691u};
    inline constexpr std::uint32_t kCarbineMk2[] = {0x837445AAu, 0x9D65907Au, 0x420FD713u, 0x5DD5DBD5u};
    inline constexpr std::uint32_t kAdvanced[] = {0x837445AAu, 0x8EC1C979u};
    inline constexpr std::uint32_t kSpecial[] = {0xA73D4664u, 0xC164F53u, 0x7C8BD10Eu};
    inline constexpr std::uint32_t kSpecialMk2[] = {0xA73D4664u, 0x9D65907Au, 0x420FD713u, 0xDE1FA12Cu};
    inline constexpr std::uint32_t kBullpup[] = {0x837445AAu, 0xC164F53u, 0xB3688B0Fu};
    inline constexpr std::uint32_t kBullpupMk2[] = {0x837445AAu, 0x9D65907Au, 0x420FD713u, 0xEFB00628u};
    inline constexpr std::uint32_t kCompact[] = {0x59FF9BF8u};
    inline constexpr std::uint32_t kMilitary[] = {0x837445AAu, 0x684ACE42u};
    inline constexpr std::uint32_t kMg[] = {0x82158B47u};
    inline constexpr std::uint32_t kCombatMg[] = {0xC164F53u, 0xD6C59CD6u};
    inline constexpr std::uint32_t kCombatMgMk2[] = {0x9D65907Au, 0x420FD713u, 0x17DF42E9u};
    inline constexpr std::uint32_t kGusenberg[] = {0xEAC8C270u};
    inline constexpr std::uint32_t kSniper[] = {0xA73D4664u};
    inline constexpr std::uint32_t kHeavySniperMk2[] = {0xAC42DF71u, 0x2CD8FF9Du, 0x6927E1A1u};
    inline constexpr std::uint32_t kMarksman[] = {0x837445AAu, 0xC164F53u, 0xCCFD2AC5u};
    inline constexpr std::uint32_t kMarksmanMk2[] = {0x837445AAu, 0x9D65907Au, 0x420FD713u, 0xE6CFD1AAu};

    inline constexpr CompatEntry kCompat[] = {
        {0x1B06D571u, kPistol, 2},
        {0xBFE256D4u, kPistolMk2, 2},
        {0x5EF9FEC4u, kCombatPistol, 2},
        {0x22D8FE39u, kApPistol, 2},
        {0x99AEEB3Bu, kPistol50, 2},
        {0xD205520Eu, kHeavyPistol, 2},
        {0x13532244u, kMicroSmg, 2},
        {0x2BE6766Bu, kSmg, 2},
        {0x78A97CD0u, kSmgMk2, 3},
        {0xEFE7E2DFu, kAssaultSmg, 2},
        {0x0A3D4D34u, kCombatPdw, 2},
        {0xBD248B55u, kMiniSmg, 1},
        {0xDB1AA450u, kMachinePistol, 2},
        {0x1D073A89u, kPump, 1},
        {0x555AF99Au, kPumpMk2, 2},
        {0xE284C527u, kAssaultSg, 3},
        {0x9D61E50Fu, kBullpupSg, 2},
        {0x3AABBBAAu, kHeavySg, 3},
        {0xBFEFFF6Du, kAssaultRifle, 3},
        {0x394F415Cu, kAssaultRifleMk2, 4},
        {0x83BF0278u, kCarbine, 3},
        {0xFAD1F1C9u, kCarbineMk2, 4},
        {0xAF113F99u, kAdvanced, 2},
        {0xC0A3098Du, kSpecial, 3},
        {0x969C3D67u, kSpecialMk2, 4},
        {0x7F229F94u, kBullpup, 3},
        {0x84D6FAFDu, kBullpupMk2, 4},
        {0x624FE830u, kCompact, 1},
        {0x9D1F17E6u, kMilitary, 2},
        {0x9D07F764u, kMg, 1},
        {0x7FD62962u, kCombatMg, 2},
        {0xDBBD7280u, kCombatMgMk2, 3},
        {0x61012683u, kGusenberg, 1},
        {0x05FC3C11u, kSniper, 1},
        {0x0A914799u, kHeavySniperMk2, 3},
        {0xC734385Au, kMarksman, 3},
        {0x6A6C02E0u, kMarksmanMk2, 4},
    };

    inline constexpr int kCompatCount =
        static_cast<int>(sizeof(kCompat) / sizeof(kCompat[0]));

    inline const CompatEntry* FindCompat(std::uint32_t weaponHash) noexcept
    {
        for (int i = 0; i < kCompatCount; ++i)
            if (kCompat[i].weaponHash == weaponHash)
                return &kCompat[i];
        return nullptr;
    }
}
