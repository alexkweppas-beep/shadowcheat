#pragma once
#include <cstdint>
#include <optional>
#include "Math.h"
#include "Offsets.h"

// Forward declarations
class CPed;
class CReplayInterface;

inline bool IsValidPtr(const void* p) {
    if (!p) return false;
    uintptr_t ptr = reinterpret_cast<uintptr_t>(p);
    if (ptr < 0x10000) return false;
    if (ptr >= 0x00007FFFFFFEFFFF) return false;
    // Reject unaligned / garbage pointers (heap objects are 16-byte aligned).
    if (ptr & 0xF) return false;
    return true;
}

class CPlayerInfo {
public:
    const char* GetName() {
        if (!IsValidPtr(this)) return nullptr;
        return (const char*)((uint8_t*)this + Offsets::PlayerInfo::Name);
    }
};

class CBoneManager {
public:
    Vector3 GetBonePos(int boneId) {
        return Vector3(0,0,0);
    }
};

class CPed {
public:
    Vector3 GetPosition() const {
        if (!IsValidPtr(this)) return Vector3(0,0,0);
        uint8_t* nav = nullptr;
        __try {
            nav = *(uint8_t**)((const uint8_t*)this + 0x30);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return Vector3(0,0,0);
        }
        if (!IsValidPtr(nav)) return Vector3(0,0,0);
        Vector3 pos;
        __try {
            pos = *reinterpret_cast<const Vector3*>(nav + 0x50);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return Vector3(0,0,0);
        }
        return pos;
    }

    float GetHealth() const {
        if (!IsValidPtr(this)) return 0.0f;
        float hp = 0.0f;
        __try {
            hp = *(const float*)((const uint8_t*)this + Offsets::Ped::Health);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0.0f;
        }
        return hp;
    }

    float GetArmor() const {
        if (!IsValidPtr(this)) return 0.0f;
        float armor = 0.0f;
        __try {
            armor = *(const float*)((const uint8_t*)this + Offsets::Ped::Armor);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0.0f;
        }
        return armor;
    }

    float GetMaxHealth() const {
        if (!IsValidPtr(this)) return 0.0f;
        float hp = 0.0f;
        __try {
            hp = *(const float*)((const uint8_t*)this + Offsets::Ped::MaxHealth);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0.0f;
        }
        return hp;
    }

    Vector3 GetVelocity() const {
        if (!IsValidPtr(this)) return Vector3(0, 0, 0);
        Vector3 vel{};
        __try {
            vel = *reinterpret_cast<const Vector3*>(
                (const uint8_t*)this + Offsets::Ped::Velocity);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return Vector3(0, 0, 0);
        }
        if (!std::isfinite(vel.x) || !std::isfinite(vel.y) ||
            !std::isfinite(vel.z))
            return Vector3(0, 0, 0);
        return vel;
    }

    CPlayerInfo* GetPlayerInfo() const {
        if (!IsValidPtr(this)) return nullptr;
        CPlayerInfo* pi = nullptr;
        __try {
            pi = *(CPlayerInfo**)((const uint8_t*)this + Offsets::Ped::PlayerInfo);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
        return IsValidPtr(pi) ? pi : nullptr;
    }

    // RAGE MP streamed player with RP name in pool (not GTA account / ped label).
    bool IsRageMpPlayer() const;
    bool GetRageEntityName(char* out, int outSize) const;

    bool GetDisplayName(char* out, int outSize) const
    {
        if (!out || outSize < 2)
            return false;
        out[0] = 0;
        return GetRageEntityName(out, outSize);
    }

};

class CReplayInterface {
public:
    class CPedInterface {
    public:
        CPed* GetPed(int index) {
            if (!IsValidPtr(this)) return nullptr;
            uint8_t* list = nullptr;
            __try {
                list = *(uint8_t**)((uint8_t*)this + 0x100);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return nullptr;
            }
            if (!IsValidPtr(list)) return nullptr;
            CPed* ped = nullptr;
            __try {
                ped = *(CPed**)(list + (index * 16));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return nullptr;
            }
            return IsValidPtr(ped) ? ped : nullptr;
        }

        int GetMaxPeds() {
            if (!IsValidPtr(this)) return 0;
            int count = 0;
            __try {
                count = *(int*)((uint8_t*)this + 0x108);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return 0;
            }
            return (count >= 0 && count < 2000) ? count : 0;
        }
    };

    CPedInterface* GetPedInterface() {
        if (!IsValidPtr(this)) return nullptr;
        CPedInterface* pi = nullptr;
        __try {
            pi = *(CPedInterface**)((uint8_t*)this + 0x18);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
        return IsValidPtr(pi) ? pi : nullptr;
    }
};

namespace Game {
    inline constexpr int kVisualBoneCount = 16;

    struct PedVisualData final {
        bool valid{};
        bool isPlayer{};
        bool isAnimal{};
        bool isLocal{};
        bool visible{};
        bool dead{};
        bool knock{};
        // Roleplay hit points on a 0..100 scale, -1 when unknown.
        float hp{-1.f};
        int adminLevel{};
        int factionId{};
        int factionType{};
        int familyId{};
        int sqlId{};
        bool masked{};
        bool nametagDisabled{};
        uint16_t remoteId{};
        uint32_t rageHandle{};
        uint32_t boneMask{};
        Vector3 position{};
        float heading{};
        Vector3 bones[kVisualBoneCount]{};
        char name[48]{};
        char factionKey[48]{};
        char factionTag[24]{};
        char familyName[48]{};
        char nickname[48]{};
    };

    inline constexpr int kMaxFramePeds = 256;

    struct FramePedEntry final {
        CPed* ped{};
        PedVisualData visual{};
        Vector3 livePosition{};
        Vector3 smoothedPosition{};
        Vector2 coarseScreen{};
        Vector2 projectedBones[kVisualBoneCount]{};
        std::uint32_t projectedBoneMask{};
        // Camera line-of-sight per body zone, owned by Features/Visibility.
        // Kept on the frame entry rather than on the snapshot-derived visual
        // data because it is measured here, keyed on this ped pointer, and has
        // nothing to do with what the RAGE snapshot reported.
        // A zone absent from visibleZoneKnown has no fresh answer, which every
        // consumer must read as "assume visible", not as "hidden".
        std::uint8_t visibleZoneMask{};
        std::uint8_t visibleZoneKnown{};
        float health{};
        float maxHealth{200.f};
        float armor{};
        float distanceSquared{};
        bool valid{};
        bool visualValid{};
        bool coarseVisible{};
        bool boneDemand{};
        bool isPlayer{};
        bool isAnimal{};
        bool dead{};
        // Dev measurement crowd: no game ped behind it, so anything that reads
        // ped memory has to skip these entries.
        bool synthetic{};
    };

    struct FramePedContext final {
        std::uint64_t frameId{};
        std::uint64_t generatedAt{};
        LONG sessionEpoch{};
        CPed* localPed{};
        Vector3 localPosition{};
        bool localPositionValid{};
        int count{};
        std::uint32_t boneRefreshes{};
        std::uint32_t boneCacheHits{};
        std::uint32_t projections{};
        FramePedEntry entries[kMaxFramePeds]{};
    };

    uintptr_t GetModuleBase();

    // Same path as managed ACA59738.E42A89AC(ushort id) -> UTF-8 RP name.
    bool GetRagePlayerNameById(uint16_t ragePlayerId, char* out, int outSize);

    // SEH-wrapped RP name lookup for ESP (avoids crashes on bad pool/ped memory).
    bool TryGetPedRpName(const CPed* ped, char* out, int outSize);

    // Writes name-resolver diagnostics to cheat_log.txt (throttled internally).
    void LogNameDiagnostics();

    // Rebuild pool ped list + periodic full name scan.
    void RefreshPlayerCache();

    // One heap walk: match RP names for streamed peds visible this frame.
    void ResolveRenderedPedNames(const CPed* const* peds, int count);

    // RAGE MP player detection: pool cache + PlayerInfo on streamed peds.
    bool IsPedPlayer(const CPed* ped);
    bool IsPedAnimal(const CPed* ped) noexcept;

    // One-to-one RAGE handle -> CPed bindings for visual snapshots.
    void RefreshVisualCache(
        const CPed* const* peds,
        int count);
    bool TryGetPedVisual(
        const CPed* ped,
        PedVisualData& output);
    bool TryGetDirectPedBones(
        const CPed* ped,
        PedVisualData& output);
    bool TryGetCachedPedBones(
        const CPed* ped,
        const Vector3& currentPosition,
        PedVisualData& output,
        DWORD maximumAgeMs = 500) noexcept;

    // Shared read-only frame snapshot consumed by ESP, radar and Aimbot.
    // Must be built once from Present after RefreshPlayerCache().
    // visibilityDemand arms the line-of-sight prober. Nothing reads the zone
    // masks unless some ESP profile or aim mode has its visible check on, and
    // the probes are real physics queries, so they are not spent otherwise.
    void BuildFramePedContext(
        std::uint64_t frameId,
        bool aimNeedsBones,
        std::uint32_t espBoneProfileMask,
        bool visibilityDemand,
        int adminThreshold) noexcept;
    const FramePedContext& GetFramePedContext() noexcept;
    const FramePedEntry* FindFramePed(
        const CPed* ped) noexcept;

    // Dev measurement aid: appends N fake peds in front of the camera so the
    // ESP path can be profiled at crowd size without a full server. 0 = off.
    int& SyntheticCrowdCount() noexcept;

    // Drop V8-backed name/visual caches after server reconnect.
    void ClearSessionCaches() noexcept;

    // Lengths of the camera matrix rows and the projection scale the ESP
    // actually projects with. A row length that drifts off 1 means the basis
    // is skewed, which rescales every projected point.
    void CameraBasisDiagnostics(
        float& rightLength,
        float& upLength,
        float& backLength,
        float& projectionX,
        float& projectionY) noexcept;

    // ViewProjection matrix captured from D3D11 vertex shader constant buffers.
    extern float g_ViewProjection[16];
    extern bool  g_ViewProjectionValid;

    inline CReplayInterface* GetReplayInterface() {
        if (!Offsets::ReplayInterfacePtr || !IsValidPtr(Offsets::ReplayInterfacePtr)) return nullptr;
        uint8_t* p = nullptr;
        __try {
            p = *Offsets::ReplayInterfacePtr;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
        return IsValidPtr(p) ? reinterpret_cast<CReplayInterface*>(p) : nullptr;
    }

    inline CPed* GetLocalPed() {
        if (!Offsets::WorldPtr || !IsValidPtr(Offsets::WorldPtr)) return nullptr;
        uint8_t* world = nullptr;
        __try {
            world = *Offsets::WorldPtr;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
        if (!IsValidPtr(world)) return nullptr;
        CPed* ped = nullptr;
        __try {
            ped = *reinterpret_cast<CPed**>(world + 0x8);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
        return IsValidPtr(ped) ? ped : nullptr;
    }

    // sp¬ Game.getCam(): *(CameraManagerCell) -> CPlayerAngles*.
    inline uint8_t* GetGameplayCamera() {
        if (!Offsets::CameraManagerPtr ||
            !IsValidPtr(Offsets::CameraManagerPtr))
            return nullptr;
        uint8_t* cam = nullptr;
        __try {
            cam = *Offsets::CameraManagerPtr;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
        return IsValidPtr(cam) ? cam : nullptr;
    }

    // Current ped weapon joaat hash (0 if unavailable).
    inline uint32_t GetPedWeaponHash(CPed* ped) {
        if (!ped)
            return 0;
        uint32_t hash = 0;
        __try {
            uint8_t* wm = *reinterpret_cast<uint8_t**>(
                reinterpret_cast<uint8_t*>(ped) + Offsets::Ped::WeaponManager);
            if (!IsValidPtr(wm))
                return 0;
            hash = *reinterpret_cast<uint32_t*>(
                wm + Offsets::WeaponManager::SelectedHash);
            if (hash == 0) {
                uint8_t* info = *reinterpret_cast<uint8_t**>(
                    wm + Offsets::WeaponManager::WeaponInfo);
                if (IsValidPtr(info))
                    hash = static_cast<uint32_t>(
                        *reinterpret_cast<uint64_t*>(
                            info + Offsets::WeaponInfo::Hash));
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
        return hash;
    }

    inline uint32_t GetLocalWeaponHash() {
        return GetPedWeaponHash(GetLocalPed());
    }

    // Returns screen coords when the point is in front of the camera; empty otherwise.
    std::optional<Vector2> WorldToScreen(const Vector3& world);

    // Range-based ped iteration: for (CPed* ped : range) { ... }
    class PedRange {
    public:
        class Iterator {
        public:
            Iterator(CReplayInterface::CPedInterface* iface, int index, int end)
                : iface_(iface), index_(index), end_(end) {}

            CPed* operator*() const {
                return iface_ ? iface_->GetPed(index_) : nullptr;
            }

            Iterator& operator++() {
                ++index_;
                return *this;
            }

            bool operator!=(const Iterator& other) const {
                return index_ != other.index_;
            }

        private:
            CReplayInterface::CPedInterface* iface_;
            int index_;
            int end_;
        };

        PedRange(CReplayInterface::CPedInterface* iface, int count)
            : iface_(iface), count_(count) {}

        Iterator begin() const { return Iterator{iface_, 0, count_}; }
        Iterator end() const { return Iterator{iface_, count_, count_}; }

        int Count() const { return count_; }

    private:
        CReplayInterface::CPedInterface* iface_;
        int count_;
    };

    inline std::optional<PedRange> GetPedRange() {
        auto* replay = GetReplayInterface();
        if (!replay) return std::nullopt;

        auto* pedIface = replay->GetPedInterface();
        if (!pedIface) return std::nullopt;

        const int count = pedIface->GetMaxPeds();
        if (count <= 0) return std::nullopt;

        return PedRange{pedIface, count};
    }
}
