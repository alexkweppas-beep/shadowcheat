#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <Windows.h>
#include <Psapi.h>

#pragma comment(lib, "psapi.lib")

// Minimal GTA V native invoker (noctua-style pattern scan).
namespace NativeInvoke
{
    struct ScrNativeCallContext
    {
        void* m_return{};
        std::uint32_t m_arg_count{};
        void* m_args{};
        std::uint32_t m_data_count{};
        alignas(16) std::uint8_t m_vector_space[192]{};
    };

    using ScrNativeHash = std::uint64_t;
    using ScrNativeHandler = void (*)(ScrNativeCallContext*);
    using GetNativeHandlerFn = ScrNativeHandler (*)(void* table, ScrNativeHash hash);

    inline void* g_nativeTable = nullptr;
    inline GetNativeHandlerFn g_getHandler = nullptr;
    inline void (*g_fixVectors)(ScrNativeCallContext*) = nullptr;
    inline bool g_initialized = false;
    // Resolution is attempted exactly once. Without this a failed attempt left
    // g_initialized false, and Lookup calls Init whenever that is false, so
    // every single native call re-scanned the whole game module for the
    // pattern. That is tens of megabytes per call, several calls a frame.
    inline bool g_initTried = false;
    inline uintptr_t g_moduleBase = 0;
    inline size_t g_moduleSize = 0;

    // CBlipList* — array of blip object pointers (Menyoo / SHVDN style).
    inline void** g_blipList = nullptr;
    inline bool g_blipListTried = false;

    // Set by the diagnostics build so resolution can report what it found.
    // A function pointer rather than an include so this header keeps no
    // dependency on the logging side.
    using DiagFn = void (*)(const char*);
    inline DiagFn g_diag = nullptr;

    inline void Diag(const char* message) noexcept
    {
        if (g_diag && message)
            g_diag(message);
    }

    inline uintptr_t FindPattern(uintptr_t base, size_t size, const char* pattern)
    {
        auto pat2bytes = [](const char* pat, std::uint8_t* bytes, bool* mask, int& len) {
            len = 0;
            for (const char* p = pat; *p; ++p)
            {
                if (*p == ' ')
                    continue;
                if (*p == '?')
                {
                    bytes[len] = 0;
                    mask[len] = true;
                    ++len;
                    if (p[1] == '?')
                        ++p;
                }
                else
                {
                    char hex[3] = { p[0], p[1], 0 };
                    bytes[len] = static_cast<std::uint8_t>(strtoul(hex, nullptr, 16));
                    mask[len] = false;
                    ++len;
                    ++p;
                }
            }
        };

        std::uint8_t bytes[256]{};
        bool mask[256]{};
        int len = 0;
        pat2bytes(pattern, bytes, mask, len);

        const auto* mem = reinterpret_cast<const std::uint8_t*>(base);
        for (size_t i = 0; i + static_cast<size_t>(len) <= size; ++i)
        {
            bool found = true;
            for (int j = 0; j < len; ++j)
            {
                if (!mask[j] && mem[i + j] != bytes[j])
                {
                    found = false;
                    break;
                }
            }
            if (found)
                return base + i;
        }
        return 0;
    }

    inline uintptr_t RipRel(uintptr_t addr, int offset)
    {
        const std::int32_t disp = *reinterpret_cast<std::int32_t*>(addr + offset);
        return addr + offset + 4 + disp;
    }

    struct NativeContext : ScrNativeCallContext
    {
        std::uint64_t m_storage[32]{};

        NativeContext()
        {
            m_return = &m_storage[0];
            m_args = &m_storage[0];
        }

        template <typename T>
        void Push(T value)
        {
            static_assert(sizeof(T) <= 8);
            *reinterpret_cast<T*>(static_cast<std::uint64_t*>(m_args) + m_arg_count) = value;
            ++m_arg_count;
        }

        template <typename T>
        T Result() const
        {
            return *reinterpret_cast<const T*>(m_storage);
        }
    };

    inline bool Init() noexcept
    {
        if (g_initialized)
            return true;
        if (g_initTried)
            return false;
        g_initTried = true;

        HMODULE gta = GetModuleHandleA(nullptr);
        if (!gta)
        {
            Diag("NativeInvoke: GetModuleHandle(nullptr) failed");
            return false;
        }

        MODULEINFO mi{};
        if (!GetModuleInformation(GetCurrentProcess(), gta, &mi, sizeof(mi)))
        {
            Diag("NativeInvoke: GetModuleInformation failed");
            return false;
        }

        const uintptr_t base = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
        const size_t size = mi.SizeOfImage;
        g_moduleBase = base;
        g_moduleSize = size;

        {
            char line[128]{};
            sprintf_s(
                line,
                "NativeInvoke: module %016llX size 0x%zX",
                static_cast<unsigned long long>(base),
                size);
            Diag(line);
        }

        const uintptr_t addr = FindPattern(
            base,
            size,
            "48 8D 0D ? ? ? ? 48 8B 14 FA E8 ? ? ? ? 48 85 C0 75 0A");
        if (!addr)
        {
            // Used to return false without a word, which is why every native
            // silently did nothing: callers only ever saw a zero result and
            // read it as "the game ignored us on this thread".
            Diag("NativeInvoke: GetNativeHandler pattern NOT FOUND");
            return false;
        }

        g_nativeTable = reinterpret_cast<void*>(RipRel(addr, 3));
        g_getHandler = reinterpret_cast<GetNativeHandlerFn>(RipRel(addr, 12));

        static const char* kFixVectorPatterns[] = {
            "83 79 18 00 48 8B D1 74 4A FF 4A 18 48 63 4A 18 48 8D 41 04 48 8B 4C CA",
            // Slightly looser — size byte can vary across builds.
            "83 79 18 00 48 8B D1 74 ? FF 4A 18 48 63 4A 18",
        };
        for (const char* pat : kFixVectorPatterns)
        {
            const uintptr_t fv = FindPattern(base, size, pat);
            if (fv)
            {
                g_fixVectors = reinterpret_cast<decltype(g_fixVectors)>(fv);
                break;
            }
        }

        g_initialized = (g_nativeTable != nullptr && g_getHandler != nullptr);
        {
            char line[160]{};
            sprintf_s(
                line,
                "NativeInvoke: pattern at %016llX table=%016llX "
                "getHandler=%016llX fixVectors=%016llX ready=%d",
                static_cast<unsigned long long>(addr),
                static_cast<unsigned long long>(
                    reinterpret_cast<uintptr_t>(g_nativeTable)),
                static_cast<unsigned long long>(
                    reinterpret_cast<uintptr_t>(g_getHandler)),
                static_cast<unsigned long long>(
                    reinterpret_cast<uintptr_t>(g_fixVectors)),
                g_initialized ? 1 : 0);
            Diag(line);
        }
        return g_initialized;
    }

    inline bool CallHandler(ScrNativeHandler handler, NativeContext& ctx) noexcept
    {
        if (!handler)
            return false;
        __try
        {
            handler(&ctx);
            if (g_fixVectors)
                g_fixVectors(&ctx);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Every native call on this target resolves to null, and the reason is not
    // the pattern above: it matches, and the pointers it yields look sane.
    //
    // Measured in game on RAGE MP's GTA5.exe (build 1.0.231.0, module
    // 0x950C000 bytes). The bucket array the pattern resolves to holds values
    // like 000000003C7F4E40 rather than 00007FF6-style addresses, walking it
    // faults immediately, and not one of four hashes taken from the wrappers in
    // this very file appears anywhere in the module's committed data. Both the
    // registration pointers and the hashes are encrypted in this build, so
    // GetNativeHandler reads ciphertext and a plaintext scan cannot find
    // anything either.
    //
    // Consequence: no C++ native works here, and none can until the
    // build-specific decryption is implemented. Anything that needs a native
    // has to go through the RAGE script runtime, which is why the trigger,
    // waypoint teleport, ammo, super jump and anti-AFK all ended up in V8. That
    // was read as "natives are ignored on the Present thread"; the thread was
    // never the problem.
    inline ScrNativeHandler Lookup(ScrNativeHash hash) noexcept
    {
        if ((!g_initialized && !Init()) || !g_nativeTable || !g_getHandler)
            return nullptr;
        __try
        {
            return g_getHandler(g_nativeTable, hash);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    inline bool NativesAvailable() noexcept
    {
        // PLAYER_PED_ID: cheapest thing to ask for, and a real handler for it
        // would mean the table became readable.
        return Lookup(0xD80958FC74E988A6ull) != nullptr;
    }

    template <typename Ret>
    inline Ret DefaultResult()
    {
        if constexpr (!std::is_void_v<Ret>)
            return Ret{};
    }

    template <typename Ret, typename... Args>
    inline Ret Invoke(ScrNativeHash hash, Args... args)
    {
        NativeContext ctx;
        (ctx.Push(args), ...);
        const ScrNativeHandler handler = Lookup(hash);
        if (!CallHandler(handler, ctx))
            return DefaultResult<Ret>();
        if constexpr (!std::is_void_v<Ret>)
            return ctx.Result<Ret>();
    }

    inline int PlayerPedId() noexcept
    {
        return Invoke<int>(0xD80958FC74E988A6ull);
    }

    inline int GetVehiclePedIsIn(int ped, bool lastVehicle) noexcept
    {
        return Invoke<int>(0x9A9112A0FE9A4713ull, ped, lastVehicle ? 1 : 0);
    }

    inline int GetPedInVehicleSeat(int vehicle, int seatIndex) noexcept
    {
        return Invoke<int>(
            0xBB40DD2270B65366ull,
            vehicle,
            seatIndex,
            0);
    }

    // MODIFY_VEHICLE_TOP_SPEED (formerly named
    // _SET_VEHICLE_ENGINE_POWER_MULTIPLIER). Stock multiplier is 1.0.
    inline void ModifyVehicleTopSpeed(int vehicle, float multiplier) noexcept
    {
        Invoke<void>(0x93A3996368C94158ull, vehicle, multiplier);
    }

    inline void SetVehicleEnginePowerMultiplier(
        int vehicle,
        float multiplier) noexcept
    {
        ModifyVehicleTopSpeed(vehicle, multiplier);
    }

    // Frame-driven engine torque modifier. Stock multiplier is 1.0.
    inline void SetVehicleEngineTorqueMultiplier(
        int vehicle,
        float multiplier) noexcept
    {
        Invoke<void>(0xB59E4BD37AE292DBull, vehicle, multiplier);
    }

    inline bool DoesEntityExist(int entity) noexcept
    {
        return entity != 0 &&
            Invoke<int>(0x7239B21A38F536BAull, entity) != 0;
    }

    inline void ApplyForceToEntity(
        int entity,
        int forceFlags,
        float x,
        float y,
        float z,
        float offX,
        float offY,
        float offZ,
        int boneIndex,
        bool isDirectionRel,
        bool ignoreUpVec,
        bool isForceRel,
        bool p12,
        bool p13) noexcept
    {
        Invoke<void>(
            0xC5F68BE9613E2D18ull,
            entity,
            forceFlags,
            x,
            y,
            z,
            offX,
            offY,
            offZ,
            boneIndex,
            isDirectionRel ? 1 : 0,
            ignoreUpVec ? 1 : 0,
            isForceRel ? 1 : 0,
            p12 ? 1 : 0,
            p13 ? 1 : 0);
    }

    inline void SetEntityMaxSpeed(int entity, float speed) noexcept
    {
        Invoke<void>(0x0E46A3FCBDE2A1B1ull, entity, speed);
    }

    // Blip sprite 8 = map waypoint (GPS mark).
    //
    // The hash here used to be 0x1BEDE295E52CAD49, which is neither the legacy
    // nor the current GET_FIRST_BLIP_INFO_ID. The iterator's first call always
    // failed, so the native blip walk below never started.
    inline int GetFirstBlipInfoId(int blipSprite) noexcept
    {
        if (const int first =
                Invoke<int>(0x1BEDE233E52CFA47ull, blipSprite);
            first != 0)
            return first;
        return Invoke<int>(0x1BEDE233E6CD2A1Full, blipSprite);
    }

    inline int GetNextBlipInfoId(int blipSprite) noexcept
    {
        return Invoke<int>(0x14F96AA50D6FBEA7ull, blipSprite);
    }

    inline int GetBlipSprite(int blip) noexcept
    {
        return Invoke<int>(0x1FC877464A04FC4Full, blip);
    }

    // 1=veh 2=ped 3=obj 4=coord(waypoint) 6=pickup 7=radius
    inline int GetBlipInfoIdType(int blip) noexcept
    {
        return Invoke<int>(0xBE9B0959FFD0779Bull, blip);
    }

    inline bool DoesBlipExist(int blip) noexcept
    {
        return Invoke<int>(0xA6DB27D19ECBB7DAull, blip) != 0;
    }

    inline bool IsWaypointActive() noexcept
    {
        return Invoke<int>(0x1DD1F58F493F1DA5ull) != 0;
    }

    // Alias: _GET_BLIP_INFO_ID_ITERATOR — returns waypoint sprite (8).
    inline int GetWaypointBlipEnumId() noexcept
    {
        return Invoke<int>(0x186E5D252FA50E7Dull);
    }

    // Classic menu finder: iterate sprite-8 blips, prefer type==4 (coord).
    inline int FindWaypointBlip() noexcept
    {
        int iterator = GetWaypointBlipEnumId();
        if (iterator <= 0)
            iterator = 8;

        int firstAny = 0;
        for (int blip = GetFirstBlipInfoId(iterator);
             DoesBlipExist(blip);
             blip = GetNextBlipInfoId(iterator))
        {
            if (!firstAny)
                firstAny = blip;
            // Type 4 = coordinate blip (GPS waypoint).
            if (GetBlipInfoIdType(blip) == 4)
                return blip;
        }

        if (firstAny)
            return firstAny;

        // Hard fallback: sprite 8 even if enum id looked weird.
        if (iterator != 8)
        {
            for (int blip = GetFirstBlipInfoId(8);
                 DoesBlipExist(blip);
                 blip = GetNextBlipInfoId(8))
            {
                if (GetBlipInfoIdType(blip) == 4)
                    return blip;
                if (!firstAny)
                    firstAny = blip;
            }
            if (firstAny)
                return firstAny;
        }

        // Last resort: any existing blip with sprite 8 via first-id only.
        const int raw = GetFirstBlipInfoId(8);
        if (DoesBlipExist(raw))
            return raw;
        return 0;
    }

    // Unpack Vector3 return from native ctx (FixVectors + fallbacks).
    inline bool ReadVector3Result(
        NativeContext& ctx,
        float& outX,
        float& outY,
        float& outZ) noexcept
    {
        // 1) Normal path after game FixVectors: 3× float in 8-byte slots.
        outX = *reinterpret_cast<float*>(&ctx.m_storage[0]);
        outY = *reinterpret_cast<float*>(&ctx.m_storage[1]);
        outZ = *reinterpret_cast<float*>(&ctx.m_storage[2]);
        const auto sane = [](float x, float y) noexcept {
            return x > -6000.f && x < 6000.f && y > -6000.f && y < 10000.f &&
                   !(x == 0.f && y == 0.f);
        };
        if (sane(outX, outY))
            return true;

        // 2) Contiguous float3 in vector space (no FixVectors).
        auto* vs = reinterpret_cast<float*>(ctx.m_vector_space);
        outX = vs[0];
        outY = vs[1];
        outZ = vs[2];
        if (sane(outX, outY))
            return true;

        // 3) Padded Vector3 {f32,pad,f32,pad,f32,pad} in vector space.
        struct PaddedVec3
        {
            float x;
            std::uint32_t _px;
            float y;
            std::uint32_t _py;
            float z;
            std::uint32_t _pz;
        };
        const auto* pv = reinterpret_cast<const PaddedVec3*>(ctx.m_vector_space);
        outX = pv->x;
        outY = pv->y;
        outZ = pv->z;
        if (sane(outX, outY))
            return true;

        // 4) Same padded layout in return storage.
        const auto* ps = reinterpret_cast<const PaddedVec3*>(ctx.m_storage);
        outX = ps->x;
        outY = ps->y;
        outZ = ps->z;
        return sane(outX, outY);
    }

    inline bool InvokeVector3Blip(
        ScrNativeHash hash,
        int blip,
        float& outX,
        float& outY,
        float& outZ) noexcept
    {
        NativeContext ctx;
        ctx.Push(blip);
        const ScrNativeHandler handler = Lookup(hash);
        if (!CallHandler(handler, ctx))
            return false;
        return ReadVector3Result(ctx, outX, outY, outZ);
    }

    // GET_BLIP_INFO_ID_COORD
    inline bool GetBlipInfoIdCoord(
        int blip,
        float& outX,
        float& outY,
        float& outZ) noexcept
    {
        return InvokeVector3Blip(0xFA7C7F0AADF25D09ull, blip, outX, outY, outZ);
    }

    // GET_BLIP_COORDS — alternate Vector3 native (some builds prefer this).
    inline bool GetBlipCoords(
        int blip,
        float& outX,
        float& outY,
        float& outZ) noexcept
    {
        return InvokeVector3Blip(0x586AFE3FF72D996Eull, blip, outX, outY, outZ);
    }

    // Menyoo Blipx layout (stable across many builds).
    struct BlipObject
    {
        std::int32_t id;           // 0x00
        std::uint8_t id2;          // 0x04
        char pad05[3];
        std::uint8_t flag08;       // 0x08
        char pad09[7];
        float x;                   // 0x10
        float y;                   // 0x14
        float z;                   // 0x18
        char pad1C[6];
        std::uint8_t focused;      // 0x22
        char pad23[5];
        char* message;             // 0x28
        char pad30[16];
        std::int32_t icon;         // 0x40  (sprite; waypoint = 8)
        char pad44[4];
        std::uint32_t color;       // 0x48  (waypoint yellow = 84)
        char pad4C[4];
        float scale;               // 0x50
        std::int16_t rotation;     // 0x54
        std::uint8_t infoIdType;  // 0x56  (4 = coord)
        std::uint8_t zIndex;       // 0x57
        std::uint8_t display;      // 0x58
        std::uint8_t alpha;        // 0x59
    };

    inline bool InitBlipList() noexcept
    {
        if (g_blipList)
            return true;
        if (g_blipListTried)
            return false;
        g_blipListTried = true;

        HMODULE gta = GetModuleHandleA(nullptr);
        if (!gta)
            return false;
        MODULEINFO mi{};
        if (!GetModuleInformation(GetCurrentProcess(), gta, &mi, sizeof(mi)))
            return false;

        const uintptr_t base = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
        const size_t size = mi.SizeOfImage;

        // lea r8, [BlipList]; movzx eax, cx  (Menyoo / common)
        static const char* kBlipPatterns[] = {
            "4C 8D 05 ? ? ? ? 0F B7 C1",
            "4C 8D 05 ? ? ? ? 0F B7 D1",
            "4C 8D 05 ? ? ? ? 0F B7 C2",
        };
        for (const char* pat : kBlipPatterns)
        {
            const uintptr_t addr = FindPattern(base, size, pat);
            if (!addr)
                continue;
            g_blipList = reinterpret_cast<void**>(RipRel(addr, 3));
            if (g_blipList)
                return true;
        }
        return false;
    }

    inline bool SaneWaypointXY(float x, float y) noexcept
    {
        if (x == 0.f && y == 0.f)
            return false;
        return x > -6000.f && x < 6000.f && y > -6000.f && y < 10000.f;
    }

    // Direct memory scan — works from Present when HUD natives return empty.
    inline bool FindWaypointCoordsMemory(
        float& outX,
        float& outY,
        float& outZ) noexcept
    {
        if (!InitBlipList() || !g_blipList)
            return false;

        float fallbackX = 0.f;
        float fallbackY = 0.f;
        float fallbackZ = 0.f;
        bool haveFallback = false;

        __try
        {
            // BlipList::m_Blips[1500]
            for (int i = 0; i < 1500; ++i)
            {
                auto* blip = reinterpret_cast<BlipObject*>(g_blipList[i]);
                if (!blip)
                    continue;

                const int icon = blip->icon;
                const unsigned color = blip->color;
                const unsigned type = blip->infoIdType;
                const float x = blip->x;
                const float y = blip->y;
                const float z = blip->z;
                if (!SaneWaypointXY(x, y))
                    continue;

                // GPS waypoint: sprite 8 + color 84 (classic).
                if (icon == 8 && color == 84)
                {
                    outX = x;
                    outY = y;
                    outZ = z;
                    return true;
                }

                // Sprite 8 + coord type, or plain sprite 8 as soft fallback.
                if (icon == 8 && (type == 4 || !haveFallback))
                {
                    fallbackX = x;
                    fallbackY = y;
                    fallbackZ = z;
                    haveFallback = true;
                    if (type == 4)
                    {
                        outX = x;
                        outY = y;
                        outZ = z;
                        return true;
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }

        if (!haveFallback)
            return false;
        outX = fallbackX;
        outY = fallbackY;
        outZ = fallbackZ;
        return true;
    }

    // Resolve waypoint world XY (Z often junk — caller must ground-probe).
    inline bool FindWaypointCoords(float& outX, float& outY, float& outZ) noexcept
    {
        // Memory first — HUD natives often fail outside script thread / Present.
        if (FindWaypointCoordsMemory(outX, outY, outZ))
            return true;

        if (!Init())
            return false;

        const int blip = FindWaypointBlip();
        if (!blip)
            return false;

        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
        if (!GetBlipInfoIdCoord(blip, x, y, z))
        {
            if (!GetBlipCoords(blip, x, y, z))
                return false;
        }
        if (!SaneWaypointXY(x, y))
        {
            if (!GetBlipCoords(blip, x, y, z) || !SaneWaypointXY(x, y))
                return false;
        }
        outX = x;
        outY = y;
        outZ = z;
        return true;
    }

    // includeWater: true = water surface counts as ground (FiveM naming).
    // p5: NativeDB-added 6th arg on modern builds — always push false.
    inline bool GetGroundZFor3dCoord(
        float x,
        float y,
        float z,
        float& outGroundZ,
        bool includeWater = false) noexcept
    {
        float ground = 0.f;
        const int ok = Invoke<int>(
            0xC906A7DAB05C8D2Bull,
            x,
            y,
            z,
            &ground,
            includeWater ? 1 : 0,
            0);
        if (!ok)
            return false;
        outGroundZ = ground;
        return true;
    }

    // Pull streaming focus to XY so collision can load for ground probes.
    inline void SetFocusPosAndVel(
        float x,
        float y,
        float z,
        float vx = 0.f,
        float vy = 0.f,
        float vz = 0.f) noexcept
    {
        Invoke<void>(0xBB7454BAFF08FE25ull, x, y, z, vx, vy, vz);
    }

    inline void ClearFocus() noexcept
    {
        Invoke<void>(0x31B73D1EA9F01DA2ull);
    }

    inline bool HasCollisionLoadedAroundEntity(int entity) noexcept
    {
        return Invoke<int>(0xE9676F61BC0B3321ull, entity) != 0;
    }

    // PAD::_SET_CONTROL_NORMAL. The value is consumed for this frame only,
    // so Trigger cannot leave INPUT_ATTACK latched after a fault/menu open.
    // Reports whether the native was dispatched, not what it returned: it
    // returns nothing, and NativeContext aliases the return slot onto the first
    // argument, so reading a result back yielded inputGroup. With group 0 that
    // read as failure on every call, and the caller's shot cooldown, which only
    // arms on success, therefore never armed.
    inline bool SetControlValueNextFrame(
        int inputGroup,
        int control,
        float value) noexcept
    {
        NativeContext ctx;
        ctx.Push(inputGroup);
        ctx.Push(control);
        ctx.Push(value);
        return CallHandler(
            Lookup(0xE8A25867FBA3B05Eull),
            ctx);
    }

    // Synchronous LOS probe. flags: 1 = world/map geometry.
    inline int StartExpensiveSynchronousShapeTestLosProbe(
        float x1,
        float y1,
        float z1,
        float x2,
        float y2,
        float z2,
        int flags,
        int entity,
        int p8) noexcept
    {
        return Invoke<int>(
            0x377906D8A31E5586ull,
            x1,
            y1,
            z1,
            x2,
            y2,
            z2,
            flags,
            entity,
            p8);
    }

    // Returns true on solid hit; writes hit position into outX/Y/Z.
    inline bool GetShapeTestHitCoords(
        int rayHandle,
        float& outX,
        float& outY,
        float& outZ) noexcept
    {
        if (!rayHandle)
            return false;
        int hit = 0;
        // Vector3 returns use 3×8-byte slots.
        alignas(16) std::uint64_t endSlot[3]{};
        alignas(16) std::uint64_t normalSlot[3]{};
        int entityHit = 0;
        NativeContext ctx;
        ctx.Push(rayHandle);
        ctx.Push(&hit);
        ctx.Push(endSlot);
        ctx.Push(normalSlot);
        ctx.Push(&entityHit);
        const ScrNativeHandler handler = Lookup(0x3D87450E45143FDCull);
        if (!CallHandler(handler, ctx))
            return false;
        if (!hit)
            return false;
        outX = *reinterpret_cast<float*>(&endSlot[0]);
        outY = *reinterpret_cast<float*>(&endSlot[1]);
        outZ = *reinterpret_cast<float*>(&endSlot[2]);
        return true;
    }

    // START_SHAPE_TEST_LOS_PROBE (0x7EE9F5D83DD4F90E) is deliberately absent.
    // It returns a null handle on every call from the Present thread, measured
    // over hundreds of attempts in game: the shape test manager will not
    // reserve a cross-frame slot for a thread that does not own the script
    // runtime. Only the expensive synchronous variant above is usable here,
    // because it completes inside the call.

    enum class ShapeTestStatus : int
    {
        // Handle was never valid, or has already been consumed.
        Invalid = 0,
        // Physics has not answered yet; poll again next frame.
        Pending = 1,
        // Result is filled in and the handle is now spent.
        Ready = 2
    };

    struct ShapeTestResult
    {
        bool hit = false;
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
        int entity = 0;
        // Whatever the native left in the return slot. Recorded for
        // diagnostics only; see below for why it cannot gate success.
        int status = 0;
    };

    // GET_SHAPE_TEST_RESULT. Returns false only when the native could not be
    // dispatched at all, and hands back entityHit instead of discarding it.
    //
    // Success is deliberately not gated on the returned status. NativeContext
    // aliases the return slot onto the first argument, so a native that writes
    // no return value leaves the ray handle sitting in it, and a handle is
    // never 2. Gating on status == Ready therefore rejected every completed
    // probe, which is measurable: 3 probes a frame, 3 dropped, none resolved.
    // GetShapeTestHitCoords above has always trusted only the hit flag, for
    // the same reason.
    inline bool GetShapeTestResult(
        int rayHandle,
        ShapeTestResult& out) noexcept
    {
        out = ShapeTestResult{};
        if (!rayHandle)
            return false;
        int hit = 0;
        // Vector3 returns use 3x8-byte slots.
        alignas(16) std::uint64_t endSlot[3]{};
        alignas(16) std::uint64_t normalSlot[3]{};
        int entityHit = 0;
        NativeContext ctx;
        ctx.Push(rayHandle);
        ctx.Push(&hit);
        ctx.Push(endSlot);
        ctx.Push(normalSlot);
        ctx.Push(&entityHit);
        const ScrNativeHandler handler = Lookup(0x3D87450E45143FDCull);
        if (!CallHandler(handler, ctx))
            return false;
        out.status = ctx.Result<int>();
        out.hit = hit != 0;
        out.entity = entityHit;
        out.x = *reinterpret_cast<float*>(&endSlot[0]);
        out.y = *reinterpret_cast<float*>(&endSlot[1]);
        out.z = *reinterpret_cast<float*>(&endSlot[2]);
        return true;
    }

    // Raycast straight down onto map collision. Rejects absurd sky hits.
    inline bool RaycastGroundZ(
        float x,
        float y,
        float& outGroundZ,
        float fromZ = 900.f,
        float toZ = -200.f) noexcept
    {
        // 1=world, 16=objects, 256=foliage — map often needs more than world bit.
        const int ray = StartExpensiveSynchronousShapeTestLosProbe(
            x,
            y,
            fromZ,
            x,
            y,
            toZ,
            1 | 16 | 256,
            0,
            7);
        float hx = 0.f;
        float hy = 0.f;
        float hz = 0.f;
        if (!GetShapeTestHitCoords(ray, hx, hy, hz))
            return false;
        // Keep real seabed/Chiliad terrain while rejecting probe/sky hits.
        // The conjunctive form also rejects NaN.
        if (!(hz >= -200.f && hz <= 900.f))
            return false;
        outGroundZ = hz;
        return true;
    }

    inline void RequestCollisionAtCoord(float x, float y, float z) noexcept
    {
        Invoke<void>(0x07503F7948F491F7ull, x, y, z);
    }

    inline void RequestAdditionalCollisionAtCoord(
        float x,
        float y,
        float z) noexcept
    {
        Invoke<void>(0xC9156DC11411A9EAULL, x, y, z);
    }

    inline void LoadScene(float x, float y, float z) noexcept
    {
        Invoke<void>(0x4448EB75B4904BDBULL, x, y, z);
    }

    inline void SetEntityCoordsNoOffset(
        int entity,
        float x,
        float y,
        float z,
        bool keepTasks = false,
        bool keepIk = false,
        bool doWarp = true) noexcept
    {
        Invoke<void>(
            0x239A3351AC1DA385ull,
            entity,
            x,
            y,
            z,
            keepTasks ? 1 : 0,
            keepIk ? 1 : 0,
            doWarp ? 1 : 0);
    }

    inline void SetEntityVelocity(int entity, float x, float y, float z) noexcept
    {
        Invoke<void>(0x1C99BB7B6E96D16Full, entity, x, y, z);
    }

    inline void SetEntityCollision(
        int entity,
        bool enabled,
        bool keepPhysics = true) noexcept
    {
        Invoke<void>(
            0x1A9205C1B9EE827Full,
            entity,
            enabled ? 1 : 0,
            keepPhysics ? 1 : 0);
    }

    inline void FreezeEntityPosition(int entity, bool frozen) noexcept
    {
        Invoke<void>(
            0x428CA6DBD1094446ull,
            entity,
            frozen ? 1 : 0);
    }

    inline void SetEntityHasGravity(int entity, bool enabled) noexcept
    {
        Invoke<void>(
            0x4A4722448F18EEF5ull,
            entity,
            enabled ? 1 : 0);
    }

    inline bool GetGameplayCamRot(
        float& outPitch,
        float& outRoll,
        float& outYaw) noexcept
    {
        NativeContext context;
        context.Push(2);
        const ScrNativeHandler handler =
            Lookup(0x837765A25378F0BBull);
        if (!CallHandler(handler, context))
            return false;

        const auto valid = [](float pitch, float roll, float yaw) noexcept {
            return
                pitch >= -180.f && pitch <= 180.f &&
                roll >= -180.f && roll <= 180.f &&
                yaw >= -720.f && yaw <= 720.f;
        };

        const float* contiguous =
            reinterpret_cast<const float*>(context.m_vector_space);
        if (g_fixVectors)
        {
            outPitch = *reinterpret_cast<float*>(&context.m_storage[0]);
            outRoll = *reinterpret_cast<float*>(&context.m_storage[1]);
            outYaw = *reinterpret_cast<float*>(&context.m_storage[2]);
            if (valid(outPitch, outRoll, outYaw))
                return true;
        }
        else
        {
            outPitch = contiguous[0];
            outRoll = contiguous[1];
            outYaw = contiguous[2];
            if (valid(outPitch, outRoll, outYaw) &&
                (outPitch != 0.f ||
                 outRoll != 0.f ||
                 outYaw != 0.f))
            {
                return true;
            }
        }

        struct PaddedRotation final
        {
            float pitch;
            std::uint32_t padPitch;
            float roll;
            std::uint32_t padRoll;
            float yaw;
            std::uint32_t padYaw;
        };
        const auto* padded =
            reinterpret_cast<const PaddedRotation*>(
                context.m_vector_space);
        outPitch = padded->pitch;
        outRoll = padded->roll;
        outYaw = padded->yaw;
        return valid(outPitch, outRoll, outYaw);
    }

    inline bool GetGameplayCamCoord(
        float& outX,
        float& outY,
        float& outZ) noexcept
    {
        NativeContext ctx;
        const ScrNativeHandler handler =
            Lookup(0xA200EB1EE790F448ull);
        if (!CallHandler(handler, ctx))
            return false;
        return ReadVector3Result(ctx, outX, outY, outZ);
    }

    inline float GetGameplayCamFov() noexcept
    {
        return Invoke<float>(0x65019750A0324133ull);
    }

    inline int PlayerId() noexcept
    {
        return Invoke<int>(0x4F8644AF03D0E0D6ull);
    }

    inline void RestorePlayerStamina(int player, float value = 1.f) noexcept
    {
        Invoke<void>(0xA352C1B864CAFD33ull, player, value);
    }

    inline void SetSuperJumpThisFrame(int player) noexcept
    {
        Invoke<void>(0x57FFF03E423A4C0Bull, player);
    }

    inline void ClearPedTasksImmediately(int ped) noexcept
    {
        Invoke<void>(0xAAA34F8A7CB32098ull, ped);
    }

    inline void ClearPedTasks(int ped) noexcept
    {
        Invoke<void>(0xE1EF3C1216AFF2CDull, ped);
    }

    inline float GetDisabledControlNormal(int pad, int control) noexcept
    {
        return Invoke<float>(0x11E65974A982637Cull, pad, control);
    }

    inline float GetControlNormal(int pad, int control) noexcept
    {
        return Invoke<float>(0xEC3C9B8D5327B563ull, pad, control);
    }

    inline int CreateCam(const char* name, bool activate) noexcept
    {
        return Invoke<int>(
            0xC3981DCE61D9E13Full,
            name,
            activate ? 1 : 0);
    }

    inline int CreateCamWithParams(
        const char* name,
        float x,
        float y,
        float z,
        float pitch,
        float roll,
        float yaw,
        float fov,
        bool activate,
        int rotationOrder) noexcept
    {
        return Invoke<int>(
            0xB51194800B257161ull,
            name,
            x,
            y,
            z,
            pitch,
            roll,
            yaw,
            fov,
            activate ? 1 : 0,
            rotationOrder);
    }

    inline void DestroyCam(int cam, bool thisScriptCheck = false) noexcept
    {
        Invoke<void>(0x865908C81A2C22E9ull, cam, thisScriptCheck ? 1 : 0);
    }

    inline bool DoesCamExist(int cam) noexcept
    {
        return cam != 0 && Invoke<int>(0xA7A932170592B50Eull, cam) != 0;
    }

    inline void SetCamActive(int cam, bool active) noexcept
    {
        Invoke<void>(0x026FB97D0A425F84ull, cam, active ? 1 : 0);
    }

    inline void RenderScriptCams(
        bool render,
        bool ease = false,
        int easeTime = 0,
        bool easyOut = true,
        bool p4 = false) noexcept
    {
        Invoke<void>(
            0x07E5B515DB0636FCull,
            render ? 1 : 0,
            ease ? 1 : 0,
            easeTime,
            easyOut ? 1 : 0,
            p4 ? 1 : 0,
            0);
    }

    inline void SetCamCoord(int cam, float x, float y, float z) noexcept
    {
        Invoke<void>(0x4D41783FB745E42Eull, cam, x, y, z);
    }

    inline void SetCamRot(
        int cam,
        float pitch,
        float roll,
        float yaw,
        int rotationOrder = 2) noexcept
    {
        Invoke<void>(
            0x85973643155D0B07ull,
            cam,
            pitch,
            roll,
            yaw,
            rotationOrder);
    }

    inline void SetCamFov(int cam, float fov) noexcept
    {
        Invoke<void>(0xB13C14F66A00D047ull, cam, fov);
    }

    inline int GetSelectedPedWeapon(int ped) noexcept
    {
        return Invoke<int>(0x0A6DB4965674D243ull, ped);
    }

    inline void SetPedInfiniteAmmo(int ped, bool toggle, int weaponHash) noexcept
    {
        Invoke<void>(
            0x3EDCB0505123623Bull,
            ped,
            toggle ? 1 : 0,
            weaponHash);
    }

    inline void SetPedInfiniteAmmoClip(int ped, bool toggle) noexcept
    {
        Invoke<void>(0x183DADC6AA953186ull, ped, toggle ? 1 : 0);
    }

    inline void SetAmmoInClip(int ped, int weaponHash, int ammo) noexcept
    {
        Invoke<void>(0xDCD2A934D65CB497ull, ped, weaponHash, ammo);
    }

    inline int GetMaxAmmoInClip(int ped, int weaponHash, bool isPed = true) noexcept
    {
        return Invoke<int>(
            0xA38DCFFCEA8962FAull,
            ped,
            weaponHash,
            isPed ? 1 : 0);
    }

    // Was 0x7A653569DA385A54, which is not this native: it shares only the top
    // half of the real hash and RAGE rejects it outright as unknown.
    inline void SetPedCanBeKnockedOffVehicle(int ped, int state) noexcept
    {
        Invoke<void>(0x7A6535691B477C48ull, ped, state);
    }

    inline void SetPedConfigFlag(int ped, int flag, bool value) noexcept
    {
        Invoke<void>(
            0x1913FE4CBF41C463ull,
            ped,
            flag,
            value ? 1 : 0);
    }

    inline void SetVehicleEngineOn(
        int vehicle,
        bool on,
        bool instantly,
        bool disableAutoStart = false) noexcept
    {
        Invoke<void>(
            0x2497C4717C8B881Eull,
            vehicle,
            on ? 1 : 0,
            instantly ? 1 : 0,
            disableAutoStart ? 1 : 0);
    }

    inline void SetVehicleGravity(int vehicle, bool enabled) noexcept
    {
        Invoke<void>(0x89F149B6131E57DAull, vehicle, enabled ? 1 : 0);
    }

    inline bool IsPedGettingIntoAVehicle(int ped) noexcept
    {
        return Invoke<int>(0xBB062B2B5722478Eull, ped) != 0;
    }

    inline bool IsPedJumpingOutOfVehicle(int ped) noexcept
    {
        return Invoke<int>(0x433DDFFE37D2BA7Eull, ped) != 0;
    }

    inline int GetVehiclePedIsEntering(int ped) noexcept
    {
        return Invoke<int>(0x814FA8BE84158326ull, ped);
    }

    inline void SetPedIntoVehicle(int ped, int vehicle, int seat) noexcept
    {
        Invoke<void>(0xF75B0D629E1C063Dull, ped, vehicle, seat);
    }

    inline void TaskLeaveVehicle(int ped, int vehicle, int flags) noexcept
    {
        Invoke<void>(0xD3DBCE61A490BE02ull, ped, vehicle, flags);
    }

    inline void TaskVehicleDriveToCoordLongrange(
        int ped,
        int vehicle,
        float x,
        float y,
        float z,
        float speed,
        int drivingFlags,
        float stopRange) noexcept
    {
        Invoke<void>(
            0x158BB33F920D360Cull,
            ped,
            vehicle,
            x,
            y,
            z,
            speed,
            drivingFlags,
            stopRange);
    }

    inline float GetEntityHeading(int entity) noexcept
    {
        return Invoke<float>(0xE83D4F9BA2A38914ull, entity);
    }

    inline bool GetEntityCoords(
        int entity,
        float& outX,
        float& outY,
        float& outZ) noexcept
    {
        NativeContext ctx;
        ctx.Push(entity);
        ctx.Push(0);
        const ScrNativeHandler handler =
            Lookup(0x3FEF770D40960D2Aull);
        if (!CallHandler(handler, ctx))
            return false;
        return ReadVector3Result(ctx, outX, outY, outZ);
    }

    inline void SetEntityRotation(
        int entity,
        float pitch,
        float roll,
        float yaw,
        int rotationOrder = 2,
        bool p5 = true) noexcept
    {
        Invoke<void>(
            0x8524A8B0171D5E07ull,
            entity,
            pitch,
            roll,
            yaw,
            rotationOrder,
            p5 ? 1 : 0);
    }

    inline void NetworkExplodeVehicle(
        int vehicle,
        bool isAudible,
        bool isInvisible,
        int p3 = 0) noexcept
    {
        Invoke<void>(
            0x301A42153C9AD707ull,
            vehicle,
            isAudible ? 1 : 0,
            isInvisible ? 1 : 0,
            p3);
    }

    inline void SetWeatherTypeNowPersist(const char* weather) noexcept
    {
        Invoke<void>(0xED712CA327900C8Aull, weather);
    }

    inline void ClearWeatherTypePersist() noexcept
    {
        Invoke<void>(0xCCC39339BEF76CF5ull);
    }

    inline void ClearOverrideWeather() noexcept
    {
        Invoke<void>(0x338D2E3477711050ull);
    }

    inline void NetworkOverrideClockTime(int hour, int minute, int second) noexcept
    {
        Invoke<void>(0xE679E3E06E363892ull, hour, minute, second);
    }

    inline void NetworkClearClockTimeOverride() noexcept
    {
        Invoke<void>(0xD972DF67326F966Eull);
    }

    inline void PauseClock(bool paused) noexcept
    {
        Invoke<void>(0x4055E40BD2DBEC1Dull, paused ? 1 : 0);
    }

    inline void SetTimecycleModifier(const char* name) noexcept
    {
        Invoke<void>(0x2C933ABF17A1DF41ull, name);
    }

    inline void ClearTimecycleModifier() noexcept
    {
        Invoke<void>(0x0F07E7745A236711ull);
    }

    inline void SetTimecycleModifierStrength(float strength) noexcept
    {
        Invoke<void>(0x82E7FFCD5B2326B3ull, strength);
    }

    inline void SetFarShadowsSuppressed(bool suppressed) noexcept
    {
        Invoke<void>(0x0AE73D8DF3A762B2ull, suppressed ? 1 : 0);
    }

    inline void DrawMarker(
        int type,
        float x,
        float y,
        float z,
        float scaleX,
        float scaleY,
        float scaleZ,
        int r,
        int g,
        int b,
        int a) noexcept
    {
        Invoke<void>(
            0x28477EC23D892089ull,
            type,
            x,
            y,
            z,
            0.f,
            0.f,
            0.f,
            0.f,
            0.f,
            0.f,
            scaleX,
            scaleY,
            scaleZ,
            r,
            g,
            b,
            a,
            0,
            0,
            2,
            0,
            0,
            0,
            0);
    }
}
