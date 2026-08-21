#include "security/xorstr.hpp"
#include <unordered_map>
namespace weapons {

    static const std::unordered_map<uint32_t, const char*> weapon_names = {
            {0x92A27487, "Dagger"},
            {0x958A4A8F, "Bat"},
            {0xF9E6AA4B, "Bottle"},
            {0x84BD7BFD, "Crowbar"},
            
            {0x8BB05FD7, "Flashlight"},
            {0x440E4788, "Golf club"},
            {0x4E875F73, "Hammer"},
            {0xF9DCBF2D, "Hatchet"},
            {0xD8DF3C3C, "Knuckle"},
            {0x99B507EA, "Knife"},
            {0xDD5DF8D9, "Machete"},
            {0xDFE37640, "Switch Blade"},
            {0x678B81B1, "Night Stick"},
            {0x19044EE0, "Wrench"},
            {0xCD274149, "Battle Axe"},
            {0x94117305, "Pool Cue"},
            {0x1B06D571, "Pistol"},
            {0xBFE256D4, "Pistol MK2"},
            {0x5EF9FEC4, "Combat Pistol"},
            {0x22D8FE39, "AP Pistol"},
            {0x3656C8C1, "Stungun"},
            {0x99AEEB3B, "Pistol 50"},
            {0xBFD21232, "SNS Pistol"},
            {0x88374054, "SNS Pistol MK2"},
            {0xD205520E, "Heavy Pistol"},
            {0x83839C4, "Vintage Pisol"},
            {0x47757124, "Flare Gun"},
            {0xDC4DB296, "Marksman Pistol"},
            {0xC1B3C3D1, "Revolver"},
            {0xCB96392F, "Revolver MK2"},
            {0x97EA20B8, "Double Action"},
            {0x13532244, "Micro SMG"},
            {0x2BE6766B, "SMG"},
            {0x78A97CD0, "SMG MK2"},
            {0xEFE7E2DF, "Assault SMG"},
            {0xA3D4D34, "Combat PDW"},
            {0xDB1AA450, "Machine Pistol"},
            {0xBD248B55, "Mini SMG"},
            {0x1D073A89, "Pump Shotgun"},
            {0x555AF99A, "Pump Shotgun MK2"},
            {0x7846A318, "Sawnoff Shotgun"},
            {0xE284C527, "Assault Shotgun"},
            {0x9D61E50F, "Bullpup Shotgun"},
            {0xA89CB99E, "Musket"},
            {0x3AABBBAA, "Heavy Shotgun"},
            {0xEF951FBB, "DB Shotgun"},
            {0x12E82D3D, "Auto Shotgun"},
            {0xBFEFFF6D, "Assault Rifle"},
            {0x394F415C, "Assault Rifle MK2"},
            {0x83BF0278, "Carbine Rifle"},
            {0xFAD1F1C9, "Carbine Rifle MK2"},
            {0xAF113F99, "Advanced Rifle"},
            {0xC0A3098D, "Special Carbine"},
            {0x969C3D67, "Special Carbine MK2"},
            {0x7F229F94, "Bullpup Rifle"},
            {0x84D6FAFD, "Bullpup Rifle MK2"},
            {0x624FE830, "Compact Rifle"},
            {0x9D07F764, "Machine Gun"},
            {0x7FD62962, "Combat MG"},
            {0xDBBD7280, "Combat MG MK2"},
            {0x61012683, "Gusenberg"},
            {0x5FC3C11, "Sniper Rifle"},
            {0xC472FE2, "Heavy Sniper"},
            {0xA914799, "Heavy Sniper MK2"},
            {0xC734385A, "Marksman Rifle"},
            {0x6A6C02E0, "Marksman Rifle MK2"},
            {0xB1CA77B1, "RPG"},
            {0xA284510B, "Grenade Launcher"},
            {0x4DD2DC56, "Grenade Launcher Smoke"},
            {0x42BF8A85, "Minigun"},
            {0x7F7497E5, "Firework Launcher"},
            {0x6D544C99, "Railgun"},
            {0x63AB0442, "Homing Launcher"},
            {0x781FE4A, "Compact Launcher"},
            {0x93E220BD, "Grenade"},
            {0xA0973D5E, "BZGAS"},
            {0xFDBC8A50, "Smoke Grenade"},
            {0x497FACC3, "Flare"},
            {0x24B17070, "Molotov"},
            {0x2C3731D9, "Sticky BOMB"},
            {0xAB564B93, "Prox Mine"},
            {0x787F0BB, "Snowball"},
            {0xBA45E8B8, "Pipe Bomb"},
            {0x23C9F95C, "Ball"},
            {0x34A67B97, "Petrol Can"},
            {0x60EC506, "Fire Extinguisher"},
            {0xFBAB5776, "Parachute"},
            {0xAF3696A1, "Ray Pistol"},
            {0x2B5EF5EC, "Ceramic Pistol"},
            {0x476BF155, "Ray Carbine"},
            {0x3813FC08, "Stone Hatchet"},
            {0x6589186A, "Candy Cane"},
            {0xDAC00025, "Stun Rod"},
            {0xB62D1F67, "Ray Minigun"},
            {0xBA536372, "Hazard Can"},
            {0xC78D71B4, "Heavy Rifle" },
            {0x917F6C8C, "Navy Revolver"},
            {0x6E7DDDEC, "Precision Rifle"}

    };

    inline const char* get_weapon_name(uint32_t hash)
    {
        auto it = weapon_names.find(hash);
        if (it != weapon_names.end()) {
            return it->second;
        }

        return "";
    }

    inline int get_weapon_category(uint32_t hash) {
        auto it = weapon_names.find(hash);
        if (it == weapon_names.end()) return 0;
        std::string name = it->second;
        for(auto& c : name) c = tolower(c);
        
        if (name.find(xorstr_("pistol")) != std::string::npos || name.find(xorstr_("revolver")) != std::string::npos || name.find(xorstr_("flare")) != std::string::npos || name.find(xorstr_("stun")) != std::string::npos) return 1; 
        if (name.find(xorstr_("smg")) != std::string::npos || name.find(xorstr_("pdw")) != std::string::npos || name.find(xorstr_("gusenberg")) != std::string::npos) return 2; 
        if (name.find(xorstr_("sniper")) != std::string::npos || name.find(xorstr_("marksman")) != std::string::npos || name.find(xorstr_("precision")) != std::string::npos) return 5; 
        if (name.find(xorstr_("rifle")) != std::string::npos || name.find(xorstr_("carbine")) != std::string::npos || name.find(xorstr_("heavy rifle")) != std::string::npos) return 3; 
        if (name.find(xorstr_("shotgun")) != std::string::npos || name.find(xorstr_("musket")) != std::string::npos || name.find(xorstr_("sssault shotgun")) != std::string::npos) return 4; 
        if (name.find(xorstr_("rpg")) != std::string::npos || name.find(xorstr_("launcher")) != std::string::npos || name.find(xorstr_("minigun")) != std::string::npos || name.find(xorstr_("railgun")) != std::string::npos || name.find(xorstr_("machine gun")) != std::string::npos || name.find(xorstr_("combat mg")) != std::string::npos) return 6; 
        
        return 0; 
    }
}
