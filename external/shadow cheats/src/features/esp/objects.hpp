#pragma once
#include "security/xorstr.hpp"
#include <unordered_map>
#include <string>

namespace features
{
    inline const std::unordered_map<uint32_t, std::string> objectNames = {
        
        {0x92A27487, "Dagger"},
        {0x958A4A8F, "Bat"},
        {0xF9E6AA4B, "Bottle"},
        {0x84BD7BFD, "Crowbar"},
        {0xA2719263, "Unarmed"},
        {0x8BB05FD7, "Flashlight"},
        {0x440E4788, "Golf Club"},
        {0x4E875F73, "Hammer"},
        {0xF9DCBF2D, "Hatchet"},
        {0xD8DF3C3C, "Knuckle Dusters"},
        {0x99B507EA, "Knife"},
        {0xDD5DF8D9, "Machete"},
        {0xDFE37640, "Switchblade"},
        {0x678B81B1, "Nightstick"},
        {0x19044EE0, "Wrench"},
        {0xCD274149, "Battle Axe"},
        {0x94117305, "Pool Cue"},
        {0x3813FC08, "Stone Hatchet"},
        {0x6589186A, "Candy Cane"},
        {0xDAC00025, "Stun Rod"},

        
        {0x1B06D571, "Pistol"},
        {0xBFE256D4, "Pistol Mk II"},
        {0x5EF9FEC4, "Combat Pistol"},
        {0x22D8FE39, "AP Pistol"},
        {0x3656C8C1, "Stun Gun"},
        {0x99AEEB3B, "Pistol .50"},
        {0xBFD21232, "SNS Pistol"},
        {0x88374054, "SNS Pistol Mk II"},
        {0xD205520E, "Heavy Pistol"},
        {0x83839C4,  "Vintage Pistol"},
        {0x47757124, "Flare Gun"},
        {0xDC4DB296, "Marksman Pistol"},
        {0xC1B3C3D1, "Revolver"},
        {0xCB96392F, "Revolver Mk II"},
        {0x97EA20B8, "Double-Action Revolver"},
        {0xAF3696A1, "Ray Pistol"},
        {0x2B5EF5EC, "Ceramic Pistol"},
        {0x917F6C8C, "Navy Revolver"},

        
        {0x13532244, "Micro SMG"},
        {0x2BE6766B, "SMG"},
        {0x78A97CD0, "SMG Mk II"},
        {0xEFE7E2DF, "Assault SMG"},
        {0xA3D4D34,  "Combat PDW"},
        {0xDB1AA450, "Machine Pistol"},
        {0xBD248B55, "Mini SMG"},
        {0x476BF155, "Ray Carbine"},

        
        {0x1D073A89, "Pump Shotgun"},
        {0x555AF99A, "Pump Shotgun Mk II"},
        {0x7846A318, "Sawed-Off Shotgun"},
        {0xE284C527, "Assault Shotgun"},
        {0x9D61E50F, "Bullpup Shotgun"},
        {0x3AABBBAA, "Heavy Shotgun"},
        {0xEF951FBB, "Double Barrel Shotgun"},
        {0x12E82D3D, "Auto Shotgun"},

        
        {0xBFEFFF6D, "Assault Rifle"},
        {0x394F415C, "Assault Rifle Mk II"},
        {0x83BF0278, "Carbine Rifle"},
        {0xFAD1F1C9, "Carbine Rifle Mk II"},
        {0xAF113F99, "Advanced Rifle"},
        {0xC0A3098D, "Special Carbine"},
        {0x969C3D67, "Special Carbine Mk II"},
        {0x7F229F94, "Bullpup Rifle"},
        {0x84D6FAFD, "Bullpup Rifle Mk II"},
        {0x624FE830, "Compact Rifle"},

        
        {0x9D07F764, "MG"},
        {0x7FD62962, "Combat MG"},
        {0xDBBD7280, "Combat MG Mk II"},
        {0x61012683, "Gusenberg"},

        
        {0x5FC3C11,  "Sniper Rifle"},
        {0xC472FE2,  "Heavy Sniper"},
        {0xA914799,  "Heavy Sniper Mk II"},
        {0xC734385A, "Marksman Rifle"},
        {0x6A6C02E0, "Marksman Rifle Mk II"},
        {0xA89CB99E, "Musket"},

        
        {0xB1CA77B1, "RPG"},
        {0xA284510B, "Grenade Launcher"},
        {0x4DD2DC56, "Grenade Launcher Smoke"},
        {0x42BF8A85, "Minigun"},
        {0x7F7497E5, "Firework"},
        {0x6D544C99, "Railgun"},
        {0x63AB0442, "Homing Launcher"},
        {0x781FE4A,  "Compact Launcher"},
        {0xB62D1F67, "Ray Minigun"},

        
        {0x93E220BD, "Grenade"},
        {0xA0973D5E, "BZ Gas"},
        {0xFDBC8A50, "Smoke Grenade"},
        {0x497FACC3, "Flare"},
        {0x24B17070, "Molotov"},
        {0x2C3731D9, "Sticky Bomb"},
        {0xAB564B93, "Prox Mine"},
        {0x787F0BB,  "Snowball"},
        {0xBA45E8B8, "Pipe Bomb"},
        {0x23C9F95C, "Ball"},

        
        {0x34A67B97, "Petrol Can"},
        {0x60EC506,  "Fire Extinguisher"},
        {0xFBAB5776, "Parachute"},
        {0xBA536372, "Hazard Can"},

        
        {0x1053C3FD, "Assault Rifle"},
        {0x3D2E1AE8, "Carbine Rifle"},
        {0x97F39713, "Special Carbine"},
        {0xC603E5F5, "Mini SMG"},
        {0x5311D716, "Revolver"},
        {0x0D42D39D, "Sawed-Off Shotgun"},
        {0x8BF1A0D6, "Ammo 7.62mm"},
        {0x9D2BF7C5, "Ammo"}, 

        
        {0x29CB0F3C, "Armor"},
        {0x4BFB42D1, "Armor"},
        {0x1AFA6A0A, "Weed (Косяк)"},
        {0xA1D23D79, "Weed (Косяк)"},
        {0xE4774712, "Weed (Косяк)"},
        {0x9ED58C86, "Weed (Косяк)"},
        {0xD6B12B86, "Weed (Косяк)"}
    };
}
