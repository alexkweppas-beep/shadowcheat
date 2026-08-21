#pragma once
#include <cstdint>

// Weapon hashes from https://gtahash.ru/weapons/ (joaat / signed hash cast to uint32).
namespace WeaponGroups
{
    enum class Group : int
    {
        General = 0, // unknown / melee / fallback
        Pistol,
        SMG,
        Rifle,
        Shotgun,
        Sniper,
        MG,
        Count
    };

    inline const char* GroupLabel(Group group, int lang) noexcept
    {
        switch (group)
        {
        case Group::General:
            return lang == 1 ? "Общий" : (lang == 2 ? "Загальний" : "General");
        case Group::Pistol:
            return lang == 1 ? "Пистолеты" : (lang == 2 ? "Пістолети" : "Pistol");
        case Group::SMG:
            return "SMG";
        case Group::Rifle:
            return lang == 1 ? "Винтовки" : (lang == 2 ? "Гвинтівки" : "Rifle");
        case Group::Shotgun:
            return lang == 1 ? "Дробовики" : (lang == 2 ? "Дробовики" : "Shotgun");
        case Group::Sniper:
            return lang == 1 ? "Снайперки" : (lang == 2 ? "Снайперки" : "Sniper");
        case Group::MG:
            return "MG";
        default:
            return "General";
        }
    }

    inline Group Classify(uint32_t hash) noexcept
    {
        switch (hash)
        {
        // Pistols
        case 453432689u:      // pistol
        case 3219281620u:     // pistol_mk2 (-1075685676)
        case 1593441988u:     // combatpistol
        case 584646201u:      // appistol
        case 911657153u:      // stungun
        case 2578377531u:     // pistol50 (0x99AEEB3B)
        case 3218215474u:     // snspistol (-1076751822)
        case 2285322324u:     // snspistol_mk2 (-2009644972)
        case 3523564046u:     // heavypistol (-771403250)
        case 137902532u:      // vintagepistol
        case 1198879012u:     // flaregun
        case 3696079510u:     // marksmanpistol (-598887786)
        case 3249783761u:     // revolver (-1045183535)
        case 3415619887u:     // revolver_mk2 (-879347409)
        case 2548703416u:     // doubleaction (-1746263880)
        case 2939590305u:     // raypistol (-1355376991)
        case 727643628u:      // ceramicpistol
        case 2441047180u:     // navyrevolver (-1853920116)
        case 1470379660u:     // gadgetpistol (Perico)
            return Group::Pistol;

        // SMG
        case 324215364u:      // microsmg
        case 736523883u:      // smg
        case 2024373456u:     // smg_mk2
        case 4024951519u:     // assaultsmg (-270015777)
        case 171789620u:      // combatpdw
        case 3675956304u:     // machinepistol (-619010992)
        case 3173288789u:     // minismg (-1121678507)
        case 1198256469u:     // raycarbine
            return Group::SMG;

        // Rifles
        case 3220176749u:     // assaultrifle (-1074790547)
        case 961495388u:      // assaultrifle_mk2
        case 2210333304u:     // carbinerifle (-2084633992)
        case 4208062921u:     // carbinerifle_mk2 (-86904375)
        case 2937143193u:     // advancedrifle (-1357824103)
        case 3231910285u:     // specialcarbine (-1063057011)
        case 2526821735u:     // specialcarbine_mk2 (-1768145561)
        case 2132975508u:     // bullpuprifle
        case 2228681469u:     // bullpuprifle_mk2 (-2066285827)
        case 1649403952u:     // compactrifle
        case 0xC78D71B4u:     // heavyrifle
        case 0x9D1F17E6u:     // militaryrifle
        case 0xD1D5F52Bu:     // tacticalrifle
            return Group::Rifle;

        // Shotguns
        case 487013001u:      // pumpshotgun
        case 1432025498u:     // pumpshotgun_mk2
        case 2017895192u:     // sawnoffshotgun
        case 3800352039u:     // assaultshotgun (-494615257)
        case 2640438543u:     // bullpupshotgun (-1654528753)
        case 2828843422u:     // musket (-1466123874)
        case 984333226u:      // heavyshotgun
        case 4019527611u:     // dbshotgun (-275439685)
        case 317205821u:      // autoshotgun
            return Group::Shotgun;

        // Snipers
        case 100416529u:      // sniperrifle
        case 205991906u:      // heavysniper
        case 177293209u:      // heavysniper_mk2
        case 3342088282u:     // marksmanrifle (-952879014)
        case 1785463520u:     // marksmanrifle_mk2
        case 0x6E7DDDECu:     // precisionrifle
            return Group::Sniper;

        // MG
        case 2634544996u:     // mg (0x9D07F128)
        case 2144741730u:     // combatmg
        case 3686625920u:     // combatmg_mk2 (-608341376)
        case 1627465347u:     // gusenberg
            return Group::MG;

        default:
            return Group::General;
        }
    }
}
