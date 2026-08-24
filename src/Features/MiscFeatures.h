#pragma once

#include <cstddef>

namespace MiscFeatures
{
    struct Settings final
    {
        // Combat
        bool noSpread{};
        float noSpreadPct{100.f};
        bool noRecoil{};
        float noRecoilPct{100.f};
        bool reloadSpeed{};
        float reloadSpeedPct{100.f};

        // Self
        bool godMode{};
        bool fixedHp{};
        float fixedHpValue{100.f};
        bool fixedAp{};
        float fixedApValue{100.f};
        bool antiAfk{};

        // Move
        bool speedhack{};
        float speedhackValue{1.5f};
        bool noclip{};
        float noclipSpeed{1.5f};
        int noclipKey{0x77}; // F8
        bool freecam{};
        float freecamSpeed{1.5f};
        int freecamKey{0x78}; // F9
        int tpWaypointKey{0}; // 0 = unbound
        float cameraFov{50.f};
        bool cameraFovEnabled{};
        float aspectRatio{1.777f};
        bool aspectEnabled{};

        // Player extras
        bool unlimitedStamina{};
        bool superJump{};
        bool clickWarp{};
        int clickWarpKey{0x02}; // RMB
        bool greenzoneBypass{};
        bool noReload{};
        bool infiniteAmmo{};
        bool infiniteClip{};

        // World
        bool weatherOverride{};
        int weatherIndex{};
        bool timeOverride{};
        int timeHour{12};
        int timeMinute{};
        bool freezeTime{};
        bool noFog{};

        // Vehicle
        bool seatbelt{};
        // Where Respawn puts the character: the spot he fell on, or wherever
        // the server would have sent him.
        bool respawnInPlace{true};

        // Collision is a property of a pair, not of an entity, so each class
        // is switched off against us separately.
        bool disablePlayerCollision{};
        bool disableCarCollision{};
        bool disableObjectCollision{};

        // Players panel
        bool showPlayers{};
        // Two separate things, because they answer different questions. The
        // count is "is anyone watching me right now", which is a glance. The
        // roster is "who is on duty at all", which is a read, and it is the only
        // place an admin outside render range appears.
        bool showAdmins{};
        bool showAdminList{};
        float playersDistance{300.f};
        // Negative means "never moved": the panel then uses its default spot.
        // ImGui's own ini is disabled in this build, so the position has to be
        // carried in our config or it would reset every launch.
        float aroundPosX{-1.f};
        float aroundPosY{-1.f};
    };

    Settings& GetSettings() noexcept;

    void Initialize() noexcept;
    void Shutdown() noexcept;
    void Update() noexcept;
    void RenderPlayersOverlay() noexcept;

    void RequestSuicide() noexcept;
    void RequestResetAnimations() noexcept;
    void RequestHeal() noexcept;
    void RequestArmor() noexcept;
    void RequestRespawn() noexcept;
    // Queued once and executed by the RAGE script thread.
    void RequestRepairVehicle() noexcept;

    // Opens the inventory of the vehicle the camera is pointing at, preferring
    // one with its trunk open. Server-specific: GTA5RP carries this over its
    // own RPC bridge.
    void RequestOpenTrunkInventory() noexcept;
    // Queue an asynchronous ground-resolved GPS waypoint teleport.
    bool RequestTeleportWaypoint() noexcept;

    // Assert INPUT_ATTACK on the RAGE script thread for a short window.
    // Control values submitted from the D3D Present hook are never consumed,
    // so the triggerbot routes its shot through here instead.
    bool RequestAttackBurst(int milliseconds) noexcept;

    bool Export(
        char* output,
        std::size_t capacity,
        std::size_t& used) noexcept;
    void Import(const char* content) noexcept;
}
