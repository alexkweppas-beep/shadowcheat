#pragma once

#include "../imgui/imgui.h"

#include <cstdint>

struct WeaponIconEntry
{
    std::uint32_t hash{};
    ImWchar codepoint{};
    const char* title{};
};

// Majestic / noctua glyph map (Private Use Area 0xE000+).
inline constexpr WeaponIconEntry kWeaponIconEntries[] = {
    { 0x92A27487u, static_cast<ImWchar>(0xE000), "Antique Cavalry Dagger" },
    { 0x958A4A8Fu, static_cast<ImWchar>(0xE001), "Baseball Bat" },
    { 0xF9E6AA4Bu, static_cast<ImWchar>(0xE002), "Broken Bottle" },
    { 0x84BD7BFDu, static_cast<ImWchar>(0xE003), "Crowbar" },
    { 0xA2719263u, static_cast<ImWchar>(0xE004), "Fist" },
    { 0x8BB05FD7u, static_cast<ImWchar>(0xE005), "Flashlight" },
    { 0x440E4788u, static_cast<ImWchar>(0xE006), "Golf Club" },
    { 0x4E875F73u, static_cast<ImWchar>(0xE007), "Hammer" },
    { 0xF9DCBF2Du, static_cast<ImWchar>(0xE008), "Hatchet" },
    { 0xD8DF3C3Cu, static_cast<ImWchar>(0xE009), "Brass Knuckle" },
    { 0x99B507EAu, static_cast<ImWchar>(0xE00A), "Knife" },
    { 0xDD5DF8D9u, static_cast<ImWchar>(0xE00B), "Machete" },
    { 0xDFE37640u, static_cast<ImWchar>(0xE00C), "Switchblade" },
    { 0x678B81B1u, static_cast<ImWchar>(0xE00D), "Nightstick" },
    { 0x19044EE0u, static_cast<ImWchar>(0xE00E), "Pipe Wrench" },
    { 0xCD274149u, static_cast<ImWchar>(0xE00F), "Battle Axe" },
    { 0x94117305u, static_cast<ImWchar>(0xE010), "Pool Cue" },
    { 0x3813FC08u, static_cast<ImWchar>(0xE011), "Stone Hatchet" },
    { 0x86589186u, static_cast<ImWchar>(0xE012), "Candy Cane" },
    { 0xDAC00025u, static_cast<ImWchar>(0xE013), "The shocker" },
    { 0x1B06D571u, static_cast<ImWchar>(0xE014), "Pistol" },
    { 0xBFE256D4u, static_cast<ImWchar>(0xE015), "Pistol Mk II" },
    { 0x5EF9FEC4u, static_cast<ImWchar>(0xE016), "Combat Pistol" },
    { 0x22D8FE39u, static_cast<ImWchar>(0xE017), "AP Pistol" },
    { 0x3656C8C1u, static_cast<ImWchar>(0xE018), "Stun Gun" },
    { 0x99AEEB3Bu, static_cast<ImWchar>(0xE019), "Pistol .50" },
    { 0xBFD21232u, static_cast<ImWchar>(0xE01A), "SNS Pistol" },
    { 0x88374054u, static_cast<ImWchar>(0xE01B), "SNS Pistol Mk II" },
    { 0xD205520Eu, static_cast<ImWchar>(0xE01C), "Heavy Pistol" },
    { 0x083839C4u, static_cast<ImWchar>(0xE01D), "Vintage Pistol" },
    { 0x47757124u, static_cast<ImWchar>(0xE01E), "Flare Gun" },
    { 0xDC4DB296u, static_cast<ImWchar>(0xE01F), "Marksman Pistol" },
    { 0xC1B3C3D1u, static_cast<ImWchar>(0xE020), "Heavy Revolver" },
    { 0xCB96392Fu, static_cast<ImWchar>(0xE021), "Heavy Revolver Mk II" },
    { 0x97EA20B8u, static_cast<ImWchar>(0xE022), "Double Action Revolver" },
    { 0xAF3696A1u, static_cast<ImWchar>(0xE023), "Up-n-Atomizer" },
    { 0x2B5EF5ECu, static_cast<ImWchar>(0xE024), "Ceramic Pistol" },
    { 0x917F6C8Cu, static_cast<ImWchar>(0xE025), "Navy Revolver" },
    { 0x57A4368Cu, static_cast<ImWchar>(0xE026), "Perico Pistol" },
    { 0x45CD9CF3u, static_cast<ImWchar>(0xE027), "Stun Gun (MP)" },
    { 0x1BC4FDB9u, static_cast<ImWchar>(0xE028), "WM 29 Pistol" },
    { 0xF7F1E25Eu, static_cast<ImWchar>(0xE029), "Acid Package" },
    { 0x13532244u, static_cast<ImWchar>(0xE02A), "Micro SMG" },
    { 0x2BE6766Bu, static_cast<ImWchar>(0xE02B), "SMG" },
    { 0x78A97CD0u, static_cast<ImWchar>(0xE02C), "SMG Mk II" },
    { 0xEFE7E2DFu, static_cast<ImWchar>(0xE02D), "Assault SMG" },
    { 0x0A3D4D34u, static_cast<ImWchar>(0xE02E), "Combat PDW" },
    { 0xDB1AA450u, static_cast<ImWchar>(0xE02F), "Machine Pistol" },
    { 0xBD248B55u, static_cast<ImWchar>(0xE030), "Mini SMG" },
    { 0x476BF155u, static_cast<ImWchar>(0xE031), "Unholy Hellbringer" },
    { 0x14E56510u, static_cast<ImWchar>(0xE032), "Tactical SMG" },
    { 0x1D073A89u, static_cast<ImWchar>(0xE033), "Pump Shotgun" },
    { 0x555AF99Au, static_cast<ImWchar>(0xE034), "Pump Shotgun Mk II" },
    { 0x7846A318u, static_cast<ImWchar>(0xE035), "Sawed-Off Shotgun" },
    { 0xE284C527u, static_cast<ImWchar>(0xE036), "Assault Shotgun" },
    { 0x9D61E50Fu, static_cast<ImWchar>(0xE037), "Bullpup Shotgun" },
    { 0x3AABBBAAu, static_cast<ImWchar>(0xE038), "Heavy Shotgun" },
    { 0xEF951FBBu, static_cast<ImWchar>(0xE039), "Double Barrel Shotgun" },
    { 0x12E82D3Du, static_cast<ImWchar>(0xE03A), "Sweeper Shotgun" },
    { 0x05A96BA4u, static_cast<ImWchar>(0xE03B), "Combat Shotgun" },
    { 0xBFEFFF6Du, static_cast<ImWchar>(0xE03C), "Assault Rifle" },
    { 0x394F415Cu, static_cast<ImWchar>(0xE03D), "Assault Rifle Mk II" },
    { 0x83BF0278u, static_cast<ImWchar>(0xE03E), "Carbine Rifle" },
    { 0xFAD1F1C9u, static_cast<ImWchar>(0xE03F), "Carbine Rifle Mk II" },
    { 0xAF113F99u, static_cast<ImWchar>(0xE040), "Advanced Rifle" },
    { 0xC0A3098Du, static_cast<ImWchar>(0xE041), "Special Carbine" },
    { 0x969C3D67u, static_cast<ImWchar>(0xE042), "Special Carbine Mk II" },
    { 0x7F229F94u, static_cast<ImWchar>(0xE043), "Bullpup Rifle" },
    { 0x84D6FAFDu, static_cast<ImWchar>(0xE044), "Bullpup Rifle Mk II" },
    { 0x624FE830u, static_cast<ImWchar>(0xE045), "Compact Rifle" },
    { 0x9D1F17E6u, static_cast<ImWchar>(0xE046), "Military Rifle" },
    { 0xC78D71B4u, static_cast<ImWchar>(0xE047), "Heavy Rifle" },
    { 0xD1D5F52Bu, static_cast<ImWchar>(0xE048), "Tactical Rifle" },
    { 0x9D07F764u, static_cast<ImWchar>(0xE049), "MG" },
    { 0x7FD62962u, static_cast<ImWchar>(0xE04A), "Combat MG" },
    { 0xDBBD7280u, static_cast<ImWchar>(0xE04B), "Combat MG Mk II" },
    { 0x61012683u, static_cast<ImWchar>(0xE04C), "Gusenberg Sweeper" },
    { 0x05FC3C11u, static_cast<ImWchar>(0xE04D), "Sniper Rifle" },
    { 0x0C472FE2u, static_cast<ImWchar>(0xE04E), "Heavy Sniper" },
    { 0x0A914799u, static_cast<ImWchar>(0xE04F), "Heavy Sniper Mk II" },
    { 0xC734385Au, static_cast<ImWchar>(0xE050), "Marksman Rifle" },
    { 0x6A6C02E0u, static_cast<ImWchar>(0xE051), "Marksman Rifle Mk II" },
    { 0x6E7DDDECu, static_cast<ImWchar>(0xE052), "Precision Rifle" },
    { 0xA89CB99Eu, static_cast<ImWchar>(0xE053), "Musket" },
    { 0xB1CA77B1u, static_cast<ImWchar>(0xE054), "RPG" },
    { 0xA284510Bu, static_cast<ImWchar>(0xE055), "Grenade Launcher" },
    { 0x4DD2DC56u, static_cast<ImWchar>(0xE056), "Grenade Launcher Smoke" },
    { 0x42BF8A85u, static_cast<ImWchar>(0xE057), "Minigun" },
    { 0x7F7497E5u, static_cast<ImWchar>(0xE058), "Firework Launcher" },
    { 0x6D544C85u, static_cast<ImWchar>(0xE059), "Railgun" },
    { 0x63AB0442u, static_cast<ImWchar>(0xE05A), "Homing Launcher" },
    { 0x0781FE4Au, static_cast<ImWchar>(0xE05B), "Compact Grenade Launcher" },
    { 0xB62D1F67u, static_cast<ImWchar>(0xE05C), "Widowmaker" },
    { 0xDB26713Au, static_cast<ImWchar>(0xE05D), "Compact EMP Launcher" },
    { 0xFEA23564u, static_cast<ImWchar>(0xE05E), "Railgun" },
    { 0x93E220BDu, static_cast<ImWchar>(0xE05F), "Grenade" },
    { 0xA0973D5Eu, static_cast<ImWchar>(0xE060), "BZ Gas" },
    { 0x24B17070u, static_cast<ImWchar>(0xE061), "Molotov Cocktail" },
    { 0x2C3731D9u, static_cast<ImWchar>(0xE062), "Sticky Bomb" },
    { 0xAB564B93u, static_cast<ImWchar>(0xE063), "Proximity Mines" },
    { 0x0787F0BBu, static_cast<ImWchar>(0xE064), "Snowball" },
    { 0xBA45E8B8u, static_cast<ImWchar>(0xE065), "Pipe Bombs" },
    { 0x23C9F95Cu, static_cast<ImWchar>(0xE066), "Baseball" },
    { 0xFDBC8A50u, static_cast<ImWchar>(0xE067), "Tear Gas" },
    { 0x497FACC3u, static_cast<ImWchar>(0xE068), "Flare" },
    { 0x34A67B97u, static_cast<ImWchar>(0xE069), "Jerry Can" },
    { 0xFBAB5776u, static_cast<ImWchar>(0xE06A), "Parachute" },
    { 0x060EC506u, static_cast<ImWchar>(0xE06B), "Fire Extinguisher" },
    { 0xBA536372u, static_cast<ImWchar>(0xE06C), "Hazardous Jerry Can" },
    { 0x184140A1u, static_cast<ImWchar>(0xE06D), "Fertilizer Can" },
};

inline constexpr int kWeaponIconEntryCount =
    static_cast<int>(sizeof(kWeaponIconEntries) / sizeof(kWeaponIconEntries[0]));

inline const WeaponIconEntry* FindWeaponIconEntry(std::uint32_t hash) noexcept
{
    for (int i = 0; i < kWeaponIconEntryCount; ++i)
    {
        if (kWeaponIconEntries[i].hash == hash)
            return &kWeaponIconEntries[i];
    }
    return nullptr;
}

inline bool BuildWeaponIconUtf8(
    std::uint32_t hash,
    ImFont* font,
    char out[8]) noexcept
{
    if (!out)
        return false;
    out[0] = '\0';
    if (!font || hash == 0)
        return false;
    const WeaponIconEntry* entry = FindWeaponIconEntry(hash);
    if (!entry || entry->codepoint == 0)
        return false;
    if (!font->FindGlyphNoFallback(entry->codepoint))
        return false;
    ImTextCharToUtf8(out, static_cast<unsigned int>(entry->codepoint));
    return out[0] != '\0';
}
