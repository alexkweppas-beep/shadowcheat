#pragma once
#include <cstdint>

namespace offsets
{
    // ── Runtime offsets (заполняются в memory::init() в зависимости от платформы) ──
    inline uintptr_t GameWorld       = 0;
    inline uintptr_t ReplayInterface = 0;
    inline uintptr_t ViewPort        = 0;
    inline uintptr_t Camera          = 0;
    inline uintptr_t BlipList        = 0;

    constexpr uintptr_t EntityListPtr  = 0x18;
    constexpr uintptr_t EntityListMax  = 0x110;
    constexpr uintptr_t EntityListBase = 0x100;

    namespace ped
    {
        constexpr uintptr_t PlayerInfo    = 0x10A8;
        constexpr uintptr_t WeaponManager = 0x10B8;
        constexpr uintptr_t Health        = 0x280;
        constexpr uintptr_t MaxHealth     = 0x284;
        constexpr uintptr_t Position      = 0x90;
        constexpr uintptr_t Armor         = 0x150C;
        constexpr uintptr_t VisibleFlag   = 0x145C;
        constexpr uintptr_t BoneMatrix    = 0x60;
        constexpr uintptr_t BoneList      = 0x410;
        constexpr uintptr_t FragInst      = 0x1430;
        constexpr uintptr_t Vehicle       = 0x0D28;
        constexpr uintptr_t IsInVehicle   = 0x146B;
    }

    namespace skeleton
    {
        constexpr uintptr_t FragInstSkel          = 0x68;
        constexpr uintptr_t SkeletonPtr           = 0x178;
        constexpr uintptr_t crSkel_BoneIdTableSlots = 0x18;
        constexpr uintptr_t crSkel_Used           = 0x1A;
        constexpr uintptr_t crSkel_NumBones       = 0x5E;
        constexpr uintptr_t crSkel_BoneIdTable    = 0x10;
        constexpr uintptr_t mat_Arg1Ptr           = 0x8;
        constexpr uintptr_t mat_Arg2Ptr           = 0x18;
    }

    namespace weapon
    {
        constexpr uintptr_t WeaponInfo   = 0x20;
        constexpr uintptr_t Hash         = 0x10;
        constexpr uintptr_t Spread       = 0x84;
        constexpr uintptr_t Recoil       = 0x2F4;
        constexpr uintptr_t Penetration  = 0x110;
        constexpr uintptr_t TimeToShoot  = 0x13C; 
        constexpr uintptr_t AmmoInfo     = 0x60;  
        constexpr uintptr_t AmmoCount    = 0x8;   
        constexpr uintptr_t AmmoValue    = 0x18;  
    }

    namespace viewport
    {
        constexpr uintptr_t ViewMatrix = 0x24C;
    }

    namespace object
    {
        constexpr uintptr_t Position = 0x90;
        constexpr uintptr_t Hash     = 0x18;
    }

    namespace silent {
        inline uintptr_t silent = 0;
    }
    namespace trigger {
        inline uintptr_t trigger = 0;
    }

    namespace vehicle
    {
        constexpr uintptr_t VehicleInterface = 0x10;
        constexpr uintptr_t VehicleList      = 0x180;
        constexpr uintptr_t Position         = 0x90;
        constexpr uintptr_t Health           = 0x280;
        constexpr uintptr_t EngineHealth     = 0x908;
        constexpr uintptr_t LockStatus       = 0x1370;
        constexpr uintptr_t EngineStatus     = 0x92A;
        constexpr uintptr_t TrunkStatus      = 0x5F4;
    }

    // ── Статичные оффсеты RageMP (GTA5 версия под RageMP) ──
    namespace ragemp_static
    {
        constexpr uintptr_t GameWorld       = 0x25B14B0;
        constexpr uintptr_t ReplayInterface = 0x1FBD4F0;
        constexpr uintptr_t ViewPort        = 0x201DBA0;
        constexpr uintptr_t Camera          = 0x201E7D0;

        namespace silent { constexpr uintptr_t silent = 0x103891E; }
        namespace trigger { constexpr uintptr_t trigger = 0x202C8E0; }
    }

    // ── Статичные оффсеты Alt:V (fallback / начальные значения для паттерн-скана) ──
    namespace altv_static
    {
        constexpr uintptr_t GameWorld       = 0x25B14B0;
        constexpr uintptr_t ReplayInterface = 0x1FBD4F0;
        constexpr uintptr_t ViewPort        = 0x201DBA0;
        constexpr uintptr_t Camera          = 0x201E7D0;

        namespace silent { constexpr uintptr_t silent = 0x1044482; }
        namespace trigger { constexpr uintptr_t trigger = 0x203F8F0; }
    }
}
