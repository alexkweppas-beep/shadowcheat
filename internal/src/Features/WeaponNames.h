#pragma once

#include <Windows.h>
#include <cstdint>
#include <cstdio>

namespace WeaponNames
{
    inline const char* FromHash(std::uint32_t hash) noexcept
    {
        switch (hash) {
        case 0xA2719263: return "Fist";
        case 0x1B06D571: return "Pistol";
        case 0xBFE256D4: return "Pistol Mk2";
        case 0x5EF9FEC4: return "Combat Pistol";
        case 0x22D8FE39: return "AP Pistol";
        case 0x3656C8C1: return "Stun Gun";
        case 0x99AEEB3B: return "Pistol .50";
        case 0xBFD21232: return "SNS Pistol";
        case 0x88374054: return "SNS Pistol Mk2";
        case 0xD205520E: return "Heavy Pistol";
        case 0x83839C4: return "Vintage Pistol";
        case 0x47757124: return "Flare Gun";
        case 0xDC4DB296: return "Marksman Pistol";
        case 0xC1B3C3D1: return "Revolver";
        case 0xCB96392F: return "Revolver Mk2";
        case 0x97EA20B8: return "Double Action";
        case 0xAF3696A1: return "Up-n-Atomizer";
        case 0x2B5EF5EC: return "Ceramic Pistol";
        case 0x917F6C8C: return "Navy Revolver";
        case 0x57A1366B: return "Perico Pistol";
        case 0x13532244: return "Micro SMG";
        case 0x2BE6766B: return "SMG";
        case 0x78A97CD0: return "SMG Mk2";
        case 0xEFE7E2DF: return "Assault SMG";
        case 0xA3D4D34: return "Combat PDW";
        case 0xDB1AA450: return "Machine Pistol";
        case 0xBD248B55: return "Mini SMG";
        case 0x476BF155: return "Unholy Hellbringer";
        case 0x14E5AFD5: return "Tactical SMG";
        case 0x1D073A89: return "Pump Shotgun";
        case 0x555AF99A: return "Pump Shotgun Mk2";
        case 0x7846A318: return "Sawed-Off";
        case 0xE284C527: return "Assault Shotgun";
        case 0x9D61E50F: return "Bullpup Shotgun";
        case 0xA89CB99E: return "Musket";
        case 0x3AABBBAA: return "Heavy Shotgun";
        case 0xEF951FBB: return "Double Barrel";
        case 0x12E82D3D: return "Sweeper Shotgun";
        case 0x5A96BA4: return "Combat Shotgun";
        case 0xBFEFFF6D: return "Assault Rifle";
        case 0x394F415C: return "Assault Rifle Mk2";
        case 0x83BF0278: return "Carbine Rifle";
        case 0xFAD1F1C9: return "Carbine Rifle Mk2";
        case 0xAF113F99: return "Advanced Rifle";
        case 0xC0A3098D: return "Special Carbine";
        case 0x969C3D67: return "Special Carbine Mk2";
        case 0x7F229F94: return "Bullpup Rifle";
        case 0x84D6FAFD: return "Bullpup Rifle Mk2";
        case 0x624FE830: return "Compact Rifle";
        case 0x9D1F17E6: return "Military Rifle";
        case 0xC78D71B4: return "Heavy Rifle";
        case 0xD1D5F52B: return "Tactical Rifle";
        case 0x9D07F128: return "MG";
        case 0x7FD62962: return "Combat MG";
        case 0xDBBD7280: return "Combat MG Mk2";
        case 0x61012683: return "Gusenberg";
        case 0x5FC3C11: return "Sniper Rifle";
        case 0xC472FE2: return "Heavy Sniper";
        case 0xA914799: return "Heavy Sniper Mk2";
        case 0xC734385A: return "Marksman Rifle";
        case 0x6A6C02E0: return "Marksman Rifle Mk2";
        case 0x6E7DDDEC: return "Precision Rifle";
        case 0xB1CA77B1: return "RPG";
        case 0xA284510B: return "Grenade Launcher";
        case 0x42BF8A85: return "Minigun";
        case 0x7F7497E5: return "Firework";
        case 0x6D544C99: return "Railgun";
        case 0x63AB0442: return "Homing Launcher";
        case 0x781FE4A: return "Compact Launcher";
        case 0xB62D1F67: return "Widowmaker";
        case 0xDB26713A: return "Compact EMP";
        case 0x93E220BD: return "Grenade";
        case 0xA0973D5E: return "BZ Gas";
        case 0x24B17070: return "Molotov";
        case 0x2C3731D9: return "Sticky Bomb";
        case 0xAB564B93: return "Proximity Mine";
        case 0x787F0BB: return "Snowball";
        case 0xBA45E8B8: return "Pipe Bomb";
        case 0x23C9F95C: return "Ball";
        case 0xFDBC8A50: return "Smoke Grenade";
        case 0x497FACC3: return "Flare";
        case 0x34A67B97: return "Petrol Can";
        case 0x60EC506: return "Fire Extinguisher";
        case 0xFBAB5776: return "Parachute";
        case 0x92A27487: return "Dagger";
        case 0x958A4A8F: return "Bat";
        case 0xF9E6AA4B: return "Bottle";
        case 0x84BD7BFD: return "Crowbar";
        case 0x8BB05FD7: return "Flashlight";
        case 0x440E4788: return "Golf Club";
        case 0x4E875F73: return "Hammer";
        case 0xF9DCBF2D: return "Hatchet";
        case 0xD8DF3C3C: return "Knuckle";
        case 0x99B507EA: return "Knife";
        case 0xDD5DF8D9: return "Machete";
        case 0xDFE37640: return "Switchblade";
        case 0x678B81B1: return "Nightstick";
        case 0x19044EE0: return "Wrench";
        case 0xCD274149: return "Battle Axe";
        case 0x94117305: return "Pool Cue";
        case 0x3813FC08: return "Stone Hatchet";
        case 0x1BC4FDB9: return "Candy Cane";
        default: break;
        }
        return nullptr;
    }

    inline void Format(
        std::uint32_t hash,
        char* output,
        int capacity) noexcept
    {
        if (!output || capacity <= 0)
            return;
        output[0] = 0;
        if (hash == 0) {
            lstrcpynA(output, "-", capacity);
            return;
        }
        if (const char* name = FromHash(hash)) {
            lstrcpynA(output, name, capacity);
            return;
        }
        sprintf_s(output, static_cast<size_t>(capacity), "0x%08X", hash);
    }
}
