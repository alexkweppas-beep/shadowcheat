#include "Offsets.h"
#include "../Main/Hooks.h"
#include "../Memory/Pattern.h"

namespace Offsets
{
    // Live-verified 28.07.2026 against GTA5 size 0x957E000.
    constexpr uintptr_t kGetBoneRva = 0x79EED4;

    static bool LooksLiveCameraCell(uint8_t** cell) noexcept
    {
        if (!cell)
            return false;
        uint8_t* cam = nullptr;
        __try {
            cam = *cell;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        if (!cam)
            return false;
        const uintptr_t p = reinterpret_cast<uintptr_t>(cam);
        if (p < 0x10000 || p >= 0x00007FFFFFFEFFFF || (p & 0xF))
            return false;
        // Reject sentinel -1 / truncated garbage.
        if (p == 0xFFFFFFFFULL || (p >> 32) == 0xFFFFFFFFULL)
            return false;
        return true;
    }

    static bool LooksLikeGetBonePrologue(const uint8_t* fn) noexcept
    {
        if (!fn)
            return false;
        __try {
            // 48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57
            return fn[0] == 0x48 && fn[1] == 0x89 && fn[2] == 0x5C &&
                fn[3] == 0x24 &&
                fn[5] == 0x48 && fn[6] == 0x89 && fn[7] == 0x6C &&
                fn[8] == 0x24 &&
                fn[10] == 0x48 && fn[11] == 0x89 && fn[12] == 0x74 &&
                fn[13] == 0x24 &&
                fn[15] == 0x57;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static void ResolveGetBone(uintptr_t base) noexcept
    {
        GetBone = nullptr;

        const auto* byRva =
            reinterpret_cast<const uint8_t*>(base + kGetBoneRva);
        if (LooksLikeGetBonePrologue(byRva)) {
            GetBone = reinterpret_cast<GetBoneFn>(
                const_cast<uint8_t*>(byRva));
            hooks::HLog("Offsets: GetBone via RVA OK");
            return;
        }

        uint8_t* match = Memory::FindPattern(
            "GTA5.exe", Patterns::GetBone);
        if (match && LooksLikeGetBonePrologue(match)) {
            GetBone = reinterpret_cast<GetBoneFn>(match);
            hooks::HLog("Offsets: GetBone via pattern OK");
            return;
        }

        hooks::HLog("Offsets: GetBone FAILED");
    }

    // Resolve the three global pointer cells to point directly at the .data
    // slot inside GTA5.exe that holds the runtime address of each structure.
    // Cheaper, simpler, and more robust than pattern scanning: after a single
    // GetModuleHandle + RVA arithmetic, we have the right cell forever.
    // All RVAs verified against live game memory on build 1.0.231.0.
    bool Initialize()
    {
        HMODULE hExe = GetModuleHandleW(L"GTA5.exe");
        if (!hExe) {
            hooks::HLog("Offsets: GTA5.exe not loaded yet");
            return false;
        }
        uintptr_t base = reinterpret_cast<uintptr_t>(hExe);

        WorldPtr           = reinterpret_cast<uint8_t**>(base + RVA::World);
        ReplayInterfacePtr = reinterpret_cast<uint8_t**>(base + RVA::ReplayInterface);
        ViewportPtr        = reinterpret_cast<uint8_t**>(base + RVA::Viewport);
        RagePlayerPoolPtr  = reinterpret_cast<uint8_t**>(base + RVA::RagePlayerPool);
        CameraManagerPtr     = reinterpret_cast<uint8_t**>(base + RVA::CameraManager);
        GameplayDirectorPtr  = reinterpret_cast<uint8_t**>(base + RVA::GameplayDirector);

        if (!LooksLiveCameraCell(CameraManagerPtr)) {
            uint8_t* match = Memory::FindPattern(
                "GTA5.exe", Patterns::CameraManager);
            if (match) {
                CameraManagerPtr =
                    Memory::GetAbsoluteAddress<uint8_t**>(match);
                hooks::HLog(
                    LooksLiveCameraCell(CameraManagerPtr)
                        ? "Offsets: CameraManager via pattern OK"
                        : "Offsets: CameraManager pattern cell BAD");
            } else {
                hooks::HLog("Offsets: CameraManager pattern miss");
            }
        }

        // Prefer pattern for director — RVA is a best-effort seed only.
        {
            uint8_t* match = Memory::FindPattern(
                "GTA5.exe", Patterns::GameplayDirector);
            if (!match)
                match = Memory::FindPattern(
                    "GTA5.exe", Patterns::GameplayDirectorAlt);
            if (match) {
                GameplayDirectorPtr =
                    Memory::GetAbsoluteAddress<uint8_t**>(match);
                hooks::HLog(
                    LooksLiveCameraCell(GameplayDirectorPtr)
                        ? "Offsets: GameplayDirector via pattern OK"
                        : "Offsets: GameplayDirector pattern cell BAD");
            } else if (LooksLiveCameraCell(GameplayDirectorPtr)) {
                hooks::HLog("Offsets: GameplayDirector via RVA OK");
            } else {
                GameplayDirectorPtr = nullptr;
                hooks::HLog("Offsets: GameplayDirector FAILED");
            }
        }

        ResolveGetBone(base);

        hooks::HLog("Offsets: initialized via static RVA (GTA5 build 1.0.231.0)");
        hooks::HLog(WorldPtr           ? "Offsets: WorldPtr OK"           : "Offsets: WorldPtr FAILED");
        hooks::HLog(ReplayInterfacePtr ? "Offsets: ReplayInterface OK"    : "Offsets: ReplayInterface FAILED");
        hooks::HLog(ViewportPtr        ? "Offsets: ViewportPtr OK"        : "Offsets: ViewportPtr FAILED");
        hooks::HLog(RagePlayerPoolPtr  ? "Offsets: RagePlayerPool OK"     : "Offsets: RagePlayerPool FAILED");
        hooks::HLog(
            LooksLiveCameraCell(CameraManagerPtr)
                ? "Offsets: CameraManager OK"
                : "Offsets: CameraManager FAILED");
        hooks::HLog(
            LooksLiveCameraCell(GameplayDirectorPtr)
                ? "Offsets: GameplayDirector OK"
                : "Offsets: GameplayDirector FAILED");
        hooks::HLog(GetBone ? "Offsets: GetBone OK" : "Offsets: GetBone FAILED");

        return (WorldPtr != nullptr) && (ReplayInterfacePtr != nullptr);
    }
}
