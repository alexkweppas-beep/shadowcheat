#pragma once
#include <cstdint>
#include <Windows.h>

namespace Offsets
{
    // ---- RVA constants for GTA5.exe build 1.0.231.0 ----
    // These are STATIC relative-to-module-base offsets. Verified working
    // against live memory via Cheat Engine. Much more robust than pattern
    // scanning because we don't depend on instruction bytes that change
    // across builds — only the data layout.
    namespace RVA {
        constexpr uintptr_t World           = 0x25B14B0;  // CGameWorld*
        constexpr uintptr_t ReplayInterface = 0x1FBD4F0;  // CReplayInterface*
        constexpr uintptr_t Viewport        = 0x201DBA0;  // CViewport* (matrix@+0x110)
        // Live-verified 28.07.2026: old 0x20025B8 was 0xFFFFFFFF.
        // Cell holds CPlayerAngles* (sp¬ CamAddr / amph cam object).
        constexpr uintptr_t CameraManager   = 0x201E7D0;
        // camGameplayDirector** — resolved primarily via pattern (RVA may drift).
        constexpr uintptr_t GameplayDirector = 0x201ED38;
        constexpr uintptr_t RagePlayerPool  = 0x2714DF8; // RAGE MP player pool heap pointer cell

        // Vehicle list: a fixed array of CVehicle* with its capacity beside it.
        //
        // There is a dword further along that looked like a live count and is
        // not: it read 55 once and 8 a minute later while the array actually
        // held 28 vehicles. The array is walked to capacity instead and the
        // entries are filtered on their own type byte, which is the only
        // statement about a slot that the slot itself makes.
        constexpr uintptr_t VehicleList         = 0x2472650; // CVehicle**
        constexpr uintptr_t VehicleListCapacity = 0x2472648; // uint32
        constexpr uintptr_t NetPlayerPedSlots = 0x30047B0; // CPed*[32] by net slot id
    }

    namespace NetPlayer {
        constexpr uint32_t MaxSlots = 32;
    }

    // CReplayInterface holds one interface per entity kind, and each files its
    // own array at its own offset: the ped's is not the vehicle's, which is why
    // asking every interface at the ped's offset finds only peds.
    //
    // Measured on this build by walking the table. Capacities came back 300,
    // 256 and 2300, holding 65 vehicles, 125 peds and 692 objects, and the
    // vehicle interface sees every vehicle around where the flat global list
    // sees roughly half. Array stride is 16 with the pointer in the first 8.
    namespace Replay {
        constexpr uint32_t VehicleInterface = 0x10;
        constexpr uint32_t PedInterface     = 0x18;
        constexpr uint32_t ObjectInterface  = 0x28;
        constexpr uint32_t VehicleList      = 0x180;
        constexpr uint32_t VehicleCount     = 0x188;
        constexpr uint32_t PedList          = 0x100;
        constexpr uint32_t PedCount         = 0x108;
        constexpr uint32_t ObjectList       = 0x158;
        constexpr uint32_t ObjectCount      = 0x160;
        constexpr uint32_t Stride           = 0x10;
    }

    // Shared by every entity: the type discriminator and the transform block
    // that carries the world matrix.
    namespace Entity {
        constexpr uint32_t Type           = 0x28;
        constexpr uint32_t Transform      = 0x30;
        constexpr uint32_t MatrixForward  = 0x30;
        constexpr uint32_t MatrixPosition = 0x50;
        constexpr uint8_t  TypeVehicle    = 3;
        constexpr uint8_t  TypePed        = 4;
        constexpr uint8_t  TypeObject     = 5;
    }

    // ---- Resolved pointers (filled by Initialize()) ----
    // These point to the GLOBAL cell in GTA5.exe .data that holds the
    // heap pointer to the corresponding structure (NOT directly to the
    // heap object — caller must dereference once).
    inline uint8_t** WorldPtr           = nullptr;
    inline uint8_t** ReplayInterfacePtr = nullptr;
    inline uint8_t** ViewportPtr        = nullptr;
    inline uint8_t** RagePlayerPoolPtr  = nullptr;
    inline uint8_t*** VehicleListPtr    = nullptr;
    inline uint32_t*  VehicleListCapacityPtr = nullptr;
    // Cell that holds the active CPlayerAngles* (gameplay camera).
    inline uint8_t** CameraManagerPtr   = nullptr;
    // Cell that holds camGameplayDirector* (FOV metadata source).
    inline uint8_t** GameplayDirectorPtr = nullptr;

    // RAGE MP player entity cell layout (heap, verified via CE 30.06.2026).
    // Entity cell begins with a vtable pointer (multiplayer.dll + EntityVtableRVA).
    // The name is an inline ASCII string at offset EntityNameOff from the cell base.
    // CPed* is stored at EntityPedPtr (used by ped->name reverse lookup).
    namespace RageEntity {
        constexpr uintptr_t EntityVtableRVA = 0x14CF690; // multiplayer.dll RVA of entity vtable (CE verified 30.06.2026)
        constexpr uint32_t  PedPtr          = 0x90;  // CPed* offset within entity cell
        constexpr uint32_t  RpName          = 0x98;  // inline ASCII "FirstName_Surname"
        constexpr uint32_t  RpNameMax       = 32;
        // Backward-compat alias used by legacy heap-scan code paths.
        constexpr uint32_t  PedCellToName   = RpName; // name is read from cell base, not ped ptr
    }

    // ---- RAGE MP multiplayer.dll offsets ----
    // Found via IDA Pro reverse engineering of multiplayer_dump.dll.
    // These are STATIC relative-to-module-base offsets (module found at runtime
    // via GetModuleHandleW(L"multiplayer.dll")).
    namespace RageMpDll {
        // CPed pool: array of pointers to CPed pool entries (off_7FFAEC73E630).
        // Count: dword_7FFAEC73E620.
        constexpr uintptr_t PedPoolPtrArray = 0x1BBE630; // uintptr_t*  (array of entry ptrs)
        constexpr uintptr_t PedPoolCount    = 0x1BBE620; // uint32_t    (active entry count)

        // Per-entry layout (CPed pool entry object):
        constexpr uint32_t EntryEntityPtr  = 0xA78;  // RAGE MP entity* (player entity; NULL for NPCs)
        constexpr uint32_t EntryEntityId   = 0xA80;  // uint32 entity id
        constexpr uint32_t EntryMaxCount   = 256;    // safety cap

        // PlayerEntity layout (deref of EntryEntityPtr):
        constexpr uint32_t EntityPosition  = 0x90;   // Vector3 (matches sub_7FFAEB8EEA40)
        constexpr uint32_t EntityName      = 0x118;  // inline ASCII RP name "Name_Surname"
        constexpr uint32_t EntityNameMax   = 32;
    }

    // RAGE MP player pool (heap object behind RagePlayerPoolPtr in GTA5.exe .data).
    namespace RagePlayerPool {
        constexpr uint32_t MaxCount   = 0x10; // uint32 max slot count (CE: 0xE2 = 226)
        constexpr uint32_t SlotArray  = 0x48; // first slot
        constexpr uint32_t SlotStride = 0x38;
        constexpr uint32_t SlotPed    = 0x00; // CPed*
        constexpr uint32_t SlotId     = 0x0C; // uint32 RAGE player id
        constexpr uint32_t NameMax    = 32;
    }

    // ---- Struct field offsets (relative to entity/struct base) ----
    namespace Entity {
        constexpr uint32_t Position = 0x90; // Vector3 (unused — we use nav+0x50)
    }

    namespace Ped {
        constexpr uint32_t Health      = 0x280;  // float (verified: 180/200)
        constexpr uint32_t MaxHealth   = 0x284;  // float (verified: 200; was 0x2A0 — stale)
        constexpr uint32_t Armor       = 0x150C; // float (from offsets.txt, build 1.0.231.0)
        constexpr uint32_t ArmorMax    = 0x1510; // float (best-guess next slot, verify if needed)
        constexpr uint32_t PlayerInfo  = 0x10C8; // CPlayerInfo*
        // sp¬ CPed::weapon() — CWeaponManager* (build 1.0.231.0 layout).
        constexpr uint32_t WeaponManager = 0x10B8;
        // CEntity velocity (m/s). Ped in a vehicle is often near-zero here —
        // bone-history blend covers that case in silent prediction.
        constexpr uint32_t Velocity = 0x320;
        // GTA ped pose layout: transform-manager pointer plus local
        // 16-byte bone vectors. Do not treat CPed+0x60 as an inline matrix.
        constexpr uint32_t BoneMatrix = 0x180; // __m128[4]* transform manager
        constexpr uint32_t BoneData   = 0x410; // local bone vector, stride 0x10
        // Build-gated skeleton profile (timestamp 0x66746A76 / size 0x957E000):
        // CPed+0x1430 -> fragInst; fragInst+0x68 -> cache; cache+0x178 -> crSkeleton*.
        // crSkeleton: +0x0 data*, +0x8 Mat34V* base, +0x18 Mat34V* bones (stride 0x40).
        // BoneTag table lives in skeleton data as {u32 tag,u32 index,u64 aux} records.
        constexpr uint32_t RagdollInst = 0x1430;
        constexpr uint32_t RagdollInstAlt = 0x1440;
        constexpr uint32_t FragCache = 0x68;
        constexpr uint32_t FragSkeleton = 0x178;
        constexpr uint32_t SkeletonData = 0x0;
        constexpr uint32_t SkeletonBase = 0x8;
        constexpr uint32_t SkeletonBones = 0x18;
        constexpr uint32_t BoneMatrixStride = 0x40;
    }

    namespace PlayerInfo {
        constexpr uint32_t Name = 0xA4; // char[32]
    }

    namespace WeaponManager {
        constexpr uint32_t SelectedHash = 0x18; // uint32 joaat
        constexpr uint32_t WeaponInfo   = 0x20; // CWeaponInfo*
    }

    namespace WeaponInfo {
        constexpr uint32_t Hash = 0x10; // uint64/uint32 joaat
        // CWeaponInfo float layout (alexguirre / Yimura GTAV-Classes).
        constexpr uint32_t AccuracySpread = 0x74;
        constexpr uint32_t RecoilAccuracyMax = 0x84;
        constexpr uint32_t RecoilErrorTime = 0x88;
        constexpr uint32_t RecoilRecoveryRate = 0x8C;
        constexpr uint32_t BatchSpread = 0x124;
        constexpr uint32_t ReloadTimeMP = 0x128;
        constexpr uint32_t ReloadTimeSP = 0x12C;
        constexpr uint32_t VehicleReloadTime = 0x130;
        constexpr uint32_t AnimReloadRate = 0x134;
        constexpr uint32_t RecoilShakeAmplitude = 0x2F4;
    }

    namespace Viewport {
        // CViewport struct matrix layout (build 1.0.231.0, verified via CE):
        //   +0xD0   Camera matrix — rows 0..2 = camera basis in world space:
        //           right, up, backward. Look direction is -row2.
        //           V[12..14] = camera world position.
        //   +0x150  Projection matrix (DirectX row-major perspective,
        //           diag ≈ (1.43, 2.14, -1, 0), P[11]=-1, P[14]=-0.1).
        // WorldToScreen: view = dot(world - camPos, row_i); clip = view * P.
        constexpr uint32_t ViewMatrix       = 0xD0;
        constexpr uint32_t ProjectionMatrix = 0x150;
    }

    // sp¬ CPlayerAngles / amph gameplay cam object layout.
    namespace Camera {
        constexpr uint32_t FpsAngles = 0x40;  // Vector3 look direction (FPS)
        constexpr uint32_t Origin    = 0x60;  // Vector3 camera world position
        constexpr uint32_t TpsAngles = 0x3D0; // Vector3 look direction (TPS)
    }

    // camGameplayDirector → active cam → metadata (GTA5.exe, not RAGE).
    // UC / ExtendedCameraSettings layout (stable across many builds).
    namespace GameplayDirector {
        constexpr uint32_t FollowPedCamera = 0x2C0; // common active cam slot
        constexpr uint32_t CameraMetadata  = 0x10;  // camBaseCamera → metadata*
        constexpr uint32_t FollowFov       = 0x30;  // camFollow*CameraMetadata::Fov
        constexpr uint32_t FollowNearClip  = 0x34;
        constexpr uint32_t FpsMinFov       = 0x24;  // camFirstPersonShooterCameraMetadata
        constexpr uint32_t FpsMaxFov       = 0x22C;
        constexpr uint32_t MountedBaseFov  = 0x54;  // camCinematicMountedCameraMetadata
    }

    // sp¬ bullet spawn hook + engine GetBone (amph/sp¬).
    namespace Patterns {
        inline constexpr const char* CameraManager =
            "48 8B 05 ? ? ? ? 48 8B 98 ? ? ? ? EB";
        // camGameplayDirector* cell (preferred, then fallback).
        inline constexpr const char* GameplayDirector =
            "48 8B 05 ? ? ? ? 38 98 ? ? ? ? 8A C3";
        inline constexpr const char* GameplayDirectorAlt =
            "4C 8B 35 ? ? ? ? 33 FF 32 DB";
        inline constexpr const char* BulletInstance =
            "48 89 5C 24 08 57 48 83 EC 30 41 8B 00 F3";
        // sp¬ GetBoneFunc / amph BonePositionAddress
        inline constexpr const char* GetBone =
            "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC 60 48 8B 01 41 8B E8 48 8B F2 48 8B F9 33 DB";
    }

    // Engine: void* __fastcall GetBone(CPed*, __m128*/Vector4*, int32_t mask)
    using GetBoneFn = void* (__fastcall*)(void* ped, void* outVec4, int32_t mask);
    inline GetBoneFn GetBone = nullptr;

    bool Initialize();
}
