#include "MenuConfig.h"
#include "FactionColors.h"

#define IMGUI_DEFINE_MATH_OPERATORS

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"
#include "Menu.h"
#include "MenuState.h"
#include "MenuTheme.h"
#include "MenuMotion.h"
#include "EspDrawHelpers.h"
#include "EspProjectedOverlay.h"
#include "Leyawin/custom_widgets.hpp"
#include "Leyawin/leyawin_globals.h"
#include "Leyawin/leyawin_init.h"
#include "../Features/FeatureBinds.h"
#include "../Features/Friends.h"
#include "../Features/MeshSilhouette.h"
#include "../Features/MiscFeatures.h"
#include "../Features/ObsBypass.h"
#include "../SDK/WeaponCatalog.h"
#include "../SDK/WeaponGroups.h"

namespace Menu
{
    namespace
    {
        constexpr std::uint32_t kConfigStoreMagic = 0x47464331U;
        constexpr std::uint32_t kConfigStoreVersion = 1;
        constexpr wchar_t kConfigStorePath[] =
            L"C:\\RAGEMP\\user_configs.bin";

        char g_LocalConfigData[
            c_settings::kConfigSlots][kConfigPayloadCapacity]{};
        std::int64_t g_LocalConfigCreated[c_settings::kConfigSlots]{};
        std::int64_t g_LocalConfigModified[c_settings::kConfigSlots]{};
        int g_ConfigSlotCount = 0;
        int g_DefaultConfigSlot = -1;
        bool g_ConfigsLoaded = false;

        bool ReadExact(HANDLE file, void* target, DWORD size) noexcept
        {
            DWORD read = 0;
            return ReadFile(file, target, size, &read, nullptr) != FALSE &&
                read == size;
        }

        bool WriteExact(HANDLE file, const void* data, DWORD size) noexcept
        {
            DWORD written = 0;
            return WriteFile(file, data, size, &written, nullptr) != FALSE &&
                written == size;
        }
    }

    char* LocalConfigSlot(int slot) noexcept
    {
        if (slot < 0 || slot >= c_settings::kConfigSlots)
            return nullptr;
        return g_LocalConfigData[slot];
    }

    std::size_t LocalConfigSlotCapacity() noexcept
    {
        return kConfigPayloadCapacity;
    }

    int& ConfigSlotCount() noexcept
    {
        return g_ConfigSlotCount;
    }

    std::int64_t LocalConfigCreatedAt(int slot) noexcept
    {
        if (slot < 0 || slot >= c_settings::kConfigSlots)
            return 0;
        return g_LocalConfigCreated[slot];
    }

    std::int64_t LocalConfigModifiedAt(int slot) noexcept
    {
        if (slot < 0 || slot >= c_settings::kConfigSlots)
            return 0;
        return g_LocalConfigModified[slot];
    }

    void StampLocalConfig(int slot, bool created) noexcept
    {
        if (slot < 0 || slot >= c_settings::kConfigSlots)
            return;
        const std::int64_t now =
            static_cast<std::int64_t>(std::time(nullptr));
        if (created)
            g_LocalConfigCreated[slot] = now;
        g_LocalConfigModified[slot] = now;
    }

    void MoveLocalConfigSlot(int destination, int source) noexcept
    {
        if (destination < 0 || destination >= c_settings::kConfigSlots ||
            source < 0 || source >= c_settings::kConfigSlots)
            return;
        memcpy(
            g_LocalConfigData[destination],
            g_LocalConfigData[source],
            kConfigPayloadCapacity);
        g_LocalConfigCreated[destination] = g_LocalConfigCreated[source];
        g_LocalConfigModified[destination] = g_LocalConfigModified[source];
    }

    int& DefaultConfigSlot() noexcept
    {
        return g_DefaultConfigSlot;
    }

    void EnsureLocalConfigsLoaded() noexcept
    {
        if (g_ConfigsLoaded)
            return;
        g_ConfigsLoaded = true;

#ifdef RAGEMP_MENU_PREVIEW
        // The preview shares the machine with the real client; it must not read
        // or write the player's config store.
        return;
#endif
        HANDLE file = CreateFileW(
            kConfigStorePath,
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return;

        std::uint32_t magic = 0;
        std::uint32_t version = 0;
        std::int32_t count = 0;
        std::int32_t defaultSlot = -1;
        bool ok =
            ReadExact(file, &magic, sizeof(magic)) &&
            ReadExact(file, &version, sizeof(version)) &&
            ReadExact(file, &count, sizeof(count)) &&
            ReadExact(file, &defaultSlot, sizeof(defaultSlot)) &&
            magic == kConfigStoreMagic &&
            version == kConfigStoreVersion &&
            count >= 0 &&
            count <= c_settings::kConfigSlots;

        int loaded = 0;
        for (int slot = 0; ok && slot < count; ++slot)
        {
            std::uint32_t nameLength = 0;
            std::uint32_t payloadLength = 0;
            char name[sizeof(set->selection_config[0])]{};
            ok = ReadExact(file, &nameLength, sizeof(nameLength)) &&
                nameLength < sizeof(name);
            if (ok && nameLength > 0)
                ok = ReadExact(file, name, nameLength);
            if (ok)
            {
                ok = ReadExact(
                        file,
                        &g_LocalConfigCreated[slot],
                        sizeof(g_LocalConfigCreated[slot])) &&
                    ReadExact(
                        file,
                        &g_LocalConfigModified[slot],
                        sizeof(g_LocalConfigModified[slot])) &&
                    ReadExact(file, &payloadLength, sizeof(payloadLength)) &&
                    payloadLength < kConfigPayloadCapacity;
            }
            if (ok && payloadLength > 0)
                ok = ReadExact(file, g_LocalConfigData[slot], payloadLength);
            if (!ok)
                break;
            g_LocalConfigData[slot][payloadLength] = 0;
            lstrcpynA(
                set->selection_config[slot],
                name,
                static_cast<int>(sizeof(set->selection_config[slot])));
            ++loaded;
        }
        CloseHandle(file);

        if (!ok)
        {
            // A truncated store is discarded rather than half-applied.
            for (int slot = 0; slot < c_settings::kConfigSlots; ++slot)
            {
                g_LocalConfigData[slot][0] = 0;
                g_LocalConfigCreated[slot] = 0;
                g_LocalConfigModified[slot] = 0;
                set->selection_config[slot][0] = 0;
            }
            g_ConfigSlotCount = 0;
            g_DefaultConfigSlot = -1;
            return;
        }
        g_ConfigSlotCount = loaded;
        g_DefaultConfigSlot =
            defaultSlot >= 0 && defaultSlot < loaded ? defaultSlot : -1;
    }

    bool SaveLocalConfigs() noexcept
    {
#ifdef RAGEMP_MENU_PREVIEW
        return true;
#endif
        CreateDirectoryW(L"C:\\RAGEMP", nullptr);
        HANDLE file = CreateFileW(
            kConfigStorePath,
            GENERIC_WRITE,
            FILE_SHARE_READ,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;

        const std::int32_t count = ImClamp(
            g_ConfigSlotCount,
            0,
            static_cast<int>(c_settings::kConfigSlots));
        const std::int32_t defaultSlot =
            g_DefaultConfigSlot >= 0 && g_DefaultConfigSlot < count
                ? g_DefaultConfigSlot
                : -1;
        bool ok =
            WriteExact(file, &kConfigStoreMagic, sizeof(kConfigStoreMagic)) &&
            WriteExact(
                file,
                &kConfigStoreVersion,
                sizeof(kConfigStoreVersion)) &&
            WriteExact(file, &count, sizeof(count)) &&
            WriteExact(file, &defaultSlot, sizeof(defaultSlot));
        for (int slot = 0; ok && slot < count; ++slot)
        {
            const std::uint32_t nameLength = static_cast<std::uint32_t>(
                strnlen(
                    set->selection_config[slot],
                    sizeof(set->selection_config[slot])));
            const std::uint32_t payloadLength = static_cast<std::uint32_t>(
                strnlen(
                    g_LocalConfigData[slot],
                    kConfigPayloadCapacity - 1));
            ok = WriteExact(file, &nameLength, sizeof(nameLength));
            if (ok && nameLength > 0)
                ok = WriteExact(file, set->selection_config[slot], nameLength);
            if (ok)
            {
                ok = WriteExact(
                        file,
                        &g_LocalConfigCreated[slot],
                        sizeof(g_LocalConfigCreated[slot])) &&
                    WriteExact(
                        file,
                        &g_LocalConfigModified[slot],
                        sizeof(g_LocalConfigModified[slot])) &&
                    WriteExact(file, &payloadLength, sizeof(payloadLength));
            }
            if (ok && payloadLength > 0)
                ok = WriteExact(file, g_LocalConfigData[slot], payloadLength);
        }
        CloseHandle(file);
        return ok;
    }

    bool AppendConfigLine(
        char* output,
        std::size_t capacity,
        std::size_t& used,
        const char* key,
        double value)
    {
        if (!output || used >= capacity)
            return false;
        const int written = sprintf_s(
            output + used,
            capacity - used,
            "%s=%.9g\n",
            key,
            value);
        if (written <= 0)
            return false;
        used += static_cast<std::size_t>(written);
        return true;
    }

    const char* FindConfigValue(
        const char* content,
        const char* key)
    {
        if (!content || !key)
            return nullptr;
        const std::size_t keyLength = std::strlen(key);
        const char* line = content;
        while (*line) {
            if (!std::strncmp(line, key, keyLength) &&
                line[keyLength] == '=') {
                return line + keyLength + 1;
            }
            const char* next = std::strchr(line, '\n');
            if (!next)
                break;
            line = next + 1;
        }
        return nullptr;
    }

    bool ExportCurrentConfig(
        char* output,
        std::size_t capacity)
    {
        if (!output || capacity < 2)
            return false;
        output[0] = 0;
        std::size_t used = 0;
#define CFG_WRITE(name, value) \
        if (!AppendConfigLine(output, capacity, used, name, value)) return false
        EnsureAimProfilesInitialized();
        PushGeneralToLegacy();
        CFG_WRITE("version", 2);
        CFG_WRITE("language", static_cast<int>(s_Language));
        CFG_WRITE("ui_scale", s_UIScalePercent);
        CFG_WRITE("menu_toggle_key", s_MenuToggleKey);
        CFG_WRITE("menu_unload_key", s_UnloadKey);
        CFG_WRITE("menu_light_theme", s_LightTheme);
        CFG_WRITE("menu_window_gradient", set->window_gradient);
        CFG_WRITE("menu_watermark", set->watermark);
        CFG_WRITE("menu_reduced_motion", MenuMotion::ReducedMotion());
        CFG_WRITE("menu_particles", s_Particles);
        CFG_WRITE("menu_cursor_particles", set->particle_cursor);
        CFG_WRITE("menu_obs_bypass", set->obs_bypass);
        CFG_WRITE("menu_window_rounding", set->window_rounding);
        CFG_WRITE("menu_child_rounding", set->child_rounding);
        CFG_WRITE("menu_selection_rounding", set->selection_rounding);
        CFG_WRITE("menu_input_rounding", set->input_rounding);
        CFG_WRITE("menu_combo_rounding", set->combo_rounding);
        CFG_WRITE("menu_scrollbar_size", set->window_scrollbar_size);
        CFG_WRITE("menu_particle_count", set->particle_num);
        CFG_WRITE("menu_particle_gravity", set->particle_gravity);
        for (int c = 0; c < 4; ++c)
        {
            char key[48]{};
            sprintf_s(key, "menu_accent_%d", c);
            CFG_WRITE(key, func->accent_color[c]);
            sprintf_s(key, "menu_window_bg_%d", c);
            CFG_WRITE(key, (&clr->window_background.x)[c]);
            sprintf_s(key, "menu_panel_%d", c);
            CFG_WRITE(key, (&clr->child_bottom_bar.x)[c]);
            sprintf_s(key, "menu_element_%d", c);
            CFG_WRITE(key, (&clr->element_background.x)[c]);
            sprintf_s(key, "menu_text_active_%d", c);
            CFG_WRITE(key, (&clr->text_active.x)[c]);
            sprintf_s(key, "menu_text_%d", c);
            CFG_WRITE(key, (&clr->text.x)[c]);
            sprintf_s(key, "menu_particle_color_%d", c);
            CFG_WRITE(key, set->particle_color[c]);
        }
        CFG_WRITE("aim_silent", s_Aim_EnableSilent);
        CFG_WRITE("aim_visible", s_Aim_VisibleCheck);
        CFG_WRITE("aim_nearest", s_Aim_Nearest);
        CFG_WRITE("aim_head", s_Aim_HitboxHead);
        CFG_WRITE("aim_neck", s_Aim_HitboxNeck);
        CFG_WRITE("aim_chest", s_Aim_HitboxChest);
        CFG_WRITE("aim_pelvis", s_Aim_HitboxPelvis);
        CFG_WRITE("aim_draw_fov", s_Aim_DrawFov);
        CFG_WRITE("aim_fov", s_Aim_FOV);
        CFG_WRITE("aim_mark", s_Aim_MarkTarget);
        // Saved now that the colour has a picker: it used to be a constant
        // nobody could reach, so nothing carried it between sessions.
        for (int c = 0; c < 3; ++c) {
            char key[32]{};
            sprintf_s(key, "aim_mark_color%d", c);
            CFG_WRITE(key, s_Aim_MarkColor[c]);
        }
        CFG_WRITE("aim_lock", s_Aim_LockOnLast);
        CFG_WRITE("aim_hitchance_enabled", s_Aim_EnableHitchance);
        CFG_WRITE("aim_hitchance", s_Aim_Hitchance);
        CFG_WRITE("aim_ignore_dead", s_Aim_IgnoreDead);
        CFG_WRITE("aim_ignore_npc", s_Aim_IgnoreNpc);
        CFG_WRITE("aim_ignore_admins", s_Aim_IgnoreAdmins);
        CFG_WRITE("aim_ignore_friends", s_Aim_IgnoreFriends);
        CFG_WRITE("vec_enabled", s_Vec_EnableVector);
        CFG_WRITE("vec_visible", s_Vec_VisibleCheck);
        CFG_WRITE("vec_nearest", s_Vec_Nearest);
        CFG_WRITE("vec_head", s_Vec_HitboxHead);
        CFG_WRITE("vec_neck", s_Vec_HitboxNeck);
        CFG_WRITE("vec_chest", s_Vec_HitboxChest);
        CFG_WRITE("vec_pelvis", s_Vec_HitboxPelvis);
        CFG_WRITE("vec_key", s_Vec_ActivationKey);
        CFG_WRITE("vec_draw_fov", s_Vec_DrawFov);
        CFG_WRITE("vec_fov", s_Vec_FOV);
        CFG_WRITE("vec_smoothness", s_Vec_Smoothness);
        CFG_WRITE("vec_random_smooth", s_Vec_RandomSmooth);
        CFG_WRITE("vec_random_amt", s_Vec_RandomSmoothAmt);
        CFG_WRITE("vec_smart_smooth", s_Vec_EnableSmartSmooth);
        CFG_WRITE("vec_cam_shake", s_Vec_CameraShake);
        CFG_WRITE("vec_cam_shake_amt", s_Vec_CameraShakeAmt);
        for (int g = 0; g < kAimWeaponGroupCount; ++g) {
            const AimWeaponProfile& p = s_AimWeaponProfiles[g];
            char key[64]{};
#define WG_WRITE(suffix, value) \
            sprintf_s(key, "aim_wg%d_" suffix, g); CFG_WRITE(key, value)
            WG_WRITE("custom", p.useCustom);
            WG_WRITE("vec_on", p.vecEnabled);
            WG_WRITE("vec_key", p.vecKey);
            WG_WRITE("vec_pred", p.vecPrediction);
            WG_WRITE("vec_pred_scale", p.vecPredictionScale);
            WG_WRITE("vec_fov", p.vecFov);
            WG_WRITE("vec_draw", p.vecDrawFov);
            WG_WRITE("vec_near", p.vecNearest);
            WG_WRITE("vec_sticky", p.vecSticky);
            WG_WRITE("vec_bone_any", p.vecBoneNearestAny);
            WG_WRITE("vec_vis", p.vecVisible);
            {
                char key[64]{};
                sprintf_s(key, "aim_wg%d_vec_bones", g);
                const uint32_t mask = BonesToMask(p.vecBones);
                CFG_WRITE(key, static_cast<int>(mask));
            }
            WG_WRITE("vec_smooth", p.vecSmoothness);
            WG_WRITE("vec_rand", p.vecRandomSmooth);
            WG_WRITE("vec_rand_amt", p.vecRandomAmt);
            WG_WRITE("vec_smart", p.vecSmartSmooth);
            WG_WRITE("vec_shake", p.vecCamShake);
            WG_WRITE("vec_shake_amt", p.vecShakeAmt);
            WG_WRITE("dyn_fov", p.dynamicFov);
            WG_WRITE("dyn_ads", p.dynamicFovAdsMul);
            WG_WRITE("dyn_speed", p.dynamicFovSpeedExpand);
            WG_WRITE("fov_curve_on", p.useFovCurve);
            WG_WRITE("fov_dist_mul", p.fovDistanceMul);
            WG_WRITE("smooth_curve_on", p.useSmoothCurve);
            WG_WRITE("smooth_dist_mul", p.smoothDistanceMul);
            WG_WRITE("humanize", p.humanize);
            WG_WRITE("react_ms", p.reactionDelayMs);
            WG_WRITE("jitter", p.microJitter);
            WG_WRITE("overshoot", p.overshoot);
            WG_WRITE("switch_ms", p.targetSwitchDelayMs);
            WG_WRITE("fatigue", p.fatigue);
            for (int ci = 0; ci < kAimCurvePoints; ++ci) {
                sprintf_s(key, "aim_wg%d_fov_curve_%d", g, ci);
                CFG_WRITE(key, p.fovDistanceCurve.y[ci]);
                sprintf_s(key, "aim_wg%d_smooth_curve_%d", g, ci);
                CFG_WRITE(key, p.smoothDistanceCurve.y[ci]);
            }
            for (int c = 0; c < 3; ++c) {
                sprintf_s(key, "aim_wg%d_vec_color_%d", g, c);
                CFG_WRITE(key, p.vecFovColor[c]);
            }
            WG_WRITE("sil_on", p.silentEnabled);
            WG_WRITE("sil_key", p.silentKey);
            WG_WRITE("sil_fov", p.silentFov);
            WG_WRITE("sil_draw", p.silentDrawFov);
            WG_WRITE("sil_mark", p.silentMark);
            WG_WRITE("sil_mark_style", p.silentMarkStyle);
            WG_WRITE("sil_lock", p.silentLock);
            WG_WRITE("sil_near", p.silentNearest);
            WG_WRITE("sil_bone_any", p.silentBoneNearestAny);
            WG_WRITE("sil_vis", p.silentVisible);
            WG_WRITE("sil_wall", p.silentWallshot);
            {
                char key[64]{};
                sprintf_s(key, "aim_wg%d_sil_bones", g);
                const uint32_t mask = BonesToMask(p.silentBones);
                CFG_WRITE(key, static_cast<int>(mask));
            }
            WG_WRITE("sil_hc", p.silentHitchance);
            WG_WRITE("sil_hc_pct", p.silentHitchancePct);
            WG_WRITE("sil_pred", p.silentPrediction);
            WG_WRITE("sil_lead", p.silentLeadScale);
            WG_WRITE("sil_react", p.silentReactionMs);
            WG_WRITE("sil_miss", p.silentMissBias);
            WG_WRITE("sil_hc_curve_on", p.useSilentHcCurve);
            WG_WRITE("sil_hc_dist_mul", p.silentHcDistanceMul);
            for (int ci = 0; ci < kAimCurvePoints; ++ci) {
                sprintf_s(key, "aim_wg%d_sil_hc_curve_%d", g, ci);
                CFG_WRITE(key, p.silentHitchanceCurve.y[ci]);
            }
            for (int c = 0; c < 3; ++c) {
                sprintf_s(key, "aim_wg%d_sil_color_%d", g, c);
                CFG_WRITE(key, p.silentFovColor[c]);
            }
            WG_WRITE("trig_on", p.triggerEnabled);
            WG_WRITE("trig_key", p.triggerKey);
            WG_WRITE("trig_delay", p.triggerDelayMs);
            WG_WRITE("trig_cooldown", p.triggerCooldownMs);
            WG_WRITE("trig_vis", p.triggerVisible);
            {
                char key[64]{};
                sprintf_s(key, "aim_wg%d_trig_bones", g);
                CFG_WRITE(
                    key,
                    static_cast<int>(BonesToMask(p.triggerBones)));
            }
            WG_WRITE("ignore_dead", p.ignoreDead);
            WG_WRITE("ignore_npc", p.ignoreNpc);
            WG_WRITE("ignore_admins", p.ignoreAdmins);
            WG_WRITE("ignore_friends", p.ignoreFriends);
            WG_WRITE("no_spread", p.noSpread);
            WG_WRITE("no_spread_pct", p.noSpreadPct);
            WG_WRITE("no_recoil", p.noRecoil);
            WG_WRITE("no_recoil_pct", p.noRecoilPct);
            WG_WRITE("reload_spd", p.reloadSpeed);
            WG_WRITE("reload_pct", p.reloadSpeedPct);
            WG_WRITE("sil_dead", p.ignoreDead); // legacy
#undef WG_WRITE
        }
        CFG_WRITE("wpn_override_count", s_WeaponOverrideCount);
        for (int i = 0; i < s_WeaponOverrideCount; ++i) {
            const WeaponOverrideSlot& slot = s_WeaponOverrides[i];
            const AimWeaponProfile& p = slot.profile;
            char key[72]{};
#define WO_WRITE(suffix, value) \
            sprintf_s(key, "aim_wpn_%u_" suffix, slot.hash); CFG_WRITE(key, value)
            WO_WRITE("active", slot.active);
            WO_WRITE("vec_on", p.vecEnabled);
            WO_WRITE("vec_key", p.vecKey);
            WO_WRITE("vec_pred", p.vecPrediction);
            WO_WRITE("vec_pred_scale", p.vecPredictionScale);
            WO_WRITE("vec_draw", p.vecDrawFov);
            WO_WRITE("vec_fov", p.vecFov);
            WO_WRITE("vec_near", p.vecNearest);
            WO_WRITE("vec_sticky", p.vecSticky);
            WO_WRITE("vec_bone_any", p.vecBoneNearestAny);
            WO_WRITE("vec_vis", p.vecVisible);
            {
                sprintf_s(key, "aim_wpn_%u_vec_bones", slot.hash);
                CFG_WRITE(
                    key,
                    static_cast<int>(BonesToMask(p.vecBones)));
            }
            WO_WRITE("dyn_fov", p.dynamicFov);
            WO_WRITE("dyn_ads", p.dynamicFovAdsMul);
            WO_WRITE("dyn_speed", p.dynamicFovSpeedExpand);
            WO_WRITE("fov_curve_on", p.useFovCurve);
            WO_WRITE("fov_dist_mul", p.fovDistanceMul);
            WO_WRITE("smooth_curve_on", p.useSmoothCurve);
            WO_WRITE("smooth_dist_mul", p.smoothDistanceMul);
            WO_WRITE("vec_smooth", p.vecSmoothness);
            WO_WRITE("vec_rand", p.vecRandomSmooth);
            WO_WRITE("vec_rand_amt", p.vecRandomAmt);
            WO_WRITE("vec_smart", p.vecSmartSmooth);
            WO_WRITE("vec_shake", p.vecCamShake);
            WO_WRITE("vec_shake_amt", p.vecShakeAmt);
            WO_WRITE("humanize", p.humanize);
            WO_WRITE("react_ms", p.reactionDelayMs);
            WO_WRITE("jitter", p.microJitter);
            WO_WRITE("overshoot", p.overshoot);
            WO_WRITE("switch_ms", p.targetSwitchDelayMs);
            WO_WRITE("fatigue", p.fatigue);
            for (int ci = 0; ci < kAimCurvePoints; ++ci) {
                sprintf_s(
                    key,
                    "aim_wpn_%u_fov_curve_%d",
                    slot.hash,
                    ci);
                CFG_WRITE(key, p.fovDistanceCurve.y[ci]);
                sprintf_s(
                    key,
                    "aim_wpn_%u_smooth_curve_%d",
                    slot.hash,
                    ci);
                CFG_WRITE(key, p.smoothDistanceCurve.y[ci]);
            }
            for (int c = 0; c < 3; ++c) {
                sprintf_s(
                    key,
                    "aim_wpn_%u_vec_color_%d",
                    slot.hash,
                    c);
                CFG_WRITE(key, p.vecFovColor[c]);
            }
            WO_WRITE("sil_on", p.silentEnabled);
            WO_WRITE("sil_key", p.silentKey);
            WO_WRITE("sil_draw", p.silentDrawFov);
            WO_WRITE("sil_fov", p.silentFov);
            WO_WRITE("sil_mark", p.silentMark);
            WO_WRITE("sil_mark_style", p.silentMarkStyle);
            WO_WRITE("sil_lock", p.silentLock);
            WO_WRITE("sil_near", p.silentNearest);
            WO_WRITE("sil_bone_any", p.silentBoneNearestAny);
            WO_WRITE("sil_vis", p.silentVisible);
            WO_WRITE("sil_wall", p.silentWallshot);
            {
                sprintf_s(key, "aim_wpn_%u_sil_bones", slot.hash);
                CFG_WRITE(
                    key,
                    static_cast<int>(BonesToMask(p.silentBones)));
            }
            WO_WRITE("sil_hc_on", p.silentHitchance);
            WO_WRITE("sil_hc", p.silentHitchancePct);
            WO_WRITE("sil_pred", p.silentPrediction);
            WO_WRITE("sil_lead", p.silentLeadScale);
            WO_WRITE("sil_react", p.silentReactionMs);
            WO_WRITE("sil_miss", p.silentMissBias);
            WO_WRITE("sil_hc_curve_on", p.useSilentHcCurve);
            WO_WRITE("sil_hc_dist_mul", p.silentHcDistanceMul);
            for (int ci = 0; ci < kAimCurvePoints; ++ci) {
                sprintf_s(
                    key,
                    "aim_wpn_%u_sil_hc_curve_%d",
                    slot.hash,
                    ci);
                CFG_WRITE(key, p.silentHitchanceCurve.y[ci]);
            }
            for (int c = 0; c < 3; ++c) {
                sprintf_s(
                    key,
                    "aim_wpn_%u_sil_color_%d",
                    slot.hash,
                    c);
                CFG_WRITE(key, p.silentFovColor[c]);
            }
            WO_WRITE("trig_on", p.triggerEnabled);
            WO_WRITE("trig_key", p.triggerKey);
            WO_WRITE("trig_delay", p.triggerDelayMs);
            WO_WRITE("trig_cooldown", p.triggerCooldownMs);
            WO_WRITE("trig_vis", p.triggerVisible);
            {
                sprintf_s(key, "aim_wpn_%u_trig_bones", slot.hash);
                CFG_WRITE(
                    key,
                    static_cast<int>(BonesToMask(p.triggerBones)));
            }
            WO_WRITE("ignore_dead", p.ignoreDead);
            WO_WRITE("ignore_npc", p.ignoreNpc);
            WO_WRITE("ignore_admins", p.ignoreAdmins);
            WO_WRITE("ignore_friends", p.ignoreFriends);
            WO_WRITE("no_spread", p.noSpread);
            WO_WRITE("no_spread_pct", p.noSpreadPct);
            WO_WRITE("no_recoil", p.noRecoil);
            WO_WRITE("no_recoil_pct", p.noRecoilPct);
            WO_WRITE("reload_spd", p.reloadSpeed);
            WO_WRITE("reload_pct", p.reloadSpeedPct);
            {
                sprintf_s(key, "wpn_att_%u", slot.hash);
                CFG_WRITE(key, slot.attachmentCount);
                for (int a = 0; a < slot.attachmentCount; ++a) {
                    sprintf_s(key, "wpn_att_%u_%d", slot.hash, a);
                    CFG_WRITE(key, static_cast<int>(slot.attachments[a]));
                }
            }
#undef WO_WRITE
        }
        for (int p = 0;
             p < static_cast<int>(EspProfileId::Count);
             ++p) {
            char key[48]{};
            EspProfileSettings& profile = s_EspProfiles[p];
#define PROFILE_WRITE(suffix, value) \
            sprintf_s(key, "esp%d_" suffix, p); CFG_WRITE(key, value)
            PROFILE_WRITE("enabled", profile.enabled);
            PROFILE_WRITE("box", profile.box);
            PROFILE_WRITE("health", profile.healthBar);
            PROFILE_WRITE("armor", profile.armorBar);
            PROFILE_WRITE("name", profile.nameTag);
            PROFILE_WRITE("faction", profile.factionTag);
            PROFILE_WRITE("family", profile.familyTag);
            PROFILE_WRITE("skeleton", profile.skeleton);
            PROFILE_WRITE("snapline", profile.snapLine);
            PROFILE_WRITE("snapline_origin", profile.snapLineOrigin);
            PROFILE_WRITE("distance", profile.distance);
            PROFILE_WRITE("weapon", profile.weaponTag);
            PROFILE_WRITE("dyn_id", profile.dynamicIdTag);
            PROFILE_WRITE("stat_id", profile.staticIdTag);
            PROFILE_WRITE("show_dead", profile.showDead);
            PROFILE_WRITE("visible", profile.visibleCheck);
            PROFILE_WRITE("hidden_style", profile.hiddenStyle);
            PROFILE_WRITE("radar", profile.radarOverlay);
            PROFILE_WRITE("health_pos", profile.healthBarPos);
            PROFILE_WRITE("armor_pos", profile.armorBarPos);
            PROFILE_WRITE("name_pos", profile.nameTagPos);
            PROFILE_WRITE("faction_pos", profile.factionTagPos);
            PROFILE_WRITE("distance_pos", profile.distancePos);
            PROFILE_WRITE("weapon_pos", profile.weaponTagPos);
            PROFILE_WRITE("box_style", profile.boxStyle);
            PROFILE_WRITE("health_style", profile.healthStyle);
            PROFILE_WRITE("armor_style", profile.armorStyle);
            PROFILE_WRITE("weapon_disp", profile.weaponDisplay);
            PROFILE_WRITE("radar_range", profile.radarRange);
            PROFILE_WRITE("radar_pos_x", profile.radarPosX);
            PROFILE_WRITE("radar_pos_y", profile.radarPosY);
            PROFILE_WRITE("radar_size", profile.radarSize);
            PROFILE_WRITE("layouts_mig", profile.layoutsMigrated ? 1 : 0);
            PROFILE_WRITE("hp_show_val", profile.healthShowValue ? 1 : 0);
            PROFILE_WRITE("ar_show_val", profile.armorShowValue ? 1 : 0);
            for (int li = 0; li < 9; ++li) {
                sprintf_s(key, "esp%d_lay%d_side", p, li);
                CFG_WRITE(key, profile.layouts[li].side);
                sprintf_s(key, "esp%d_lay%d_ord", p, li);
                CFG_WRITE(key, profile.layouts[li].order);
                sprintf_s(key, "esp%d_lay%d_sz", p, li);
                CFG_WRITE(key, profile.layouts[li].size);
            }
            for (int c = 0; c < 4; ++c) {
                sprintf_s(key, "esp%d_box_color%d", p, c);
                CFG_WRITE(key, profile.boxColor[c]);
                sprintf_s(key, "esp%d_box_fill%d", p, c);
                CFG_WRITE(key, profile.boxFillColor[c]);
                sprintf_s(key, "esp%d_name_color%d", p, c);
                CFG_WRITE(key, profile.nameColor[c]);
                sprintf_s(key, "esp%d_faction_color%d", p, c);
                CFG_WRITE(key, profile.factionColor[c]);
                sprintf_s(key, "esp%d_family_color%d", p, c);
                CFG_WRITE(key, profile.familyColor[c]);
                sprintf_s(key, "esp%d_health_color%d", p, c);
                CFG_WRITE(key, profile.healthColor[c]);
                sprintf_s(key, "esp%d_armor_color%d", p, c);
                CFG_WRITE(key, profile.armorColor[c]);
                sprintf_s(key, "esp%d_skeleton_color%d", p, c);
                CFG_WRITE(key, profile.skeletonColor[c]);
                sprintf_s(key, "esp%d_snapline_color%d", p, c);
                CFG_WRITE(key, profile.snapLineColor[c]);
                sprintf_s(key, "esp%d_distance_color%d", p, c);
                CFG_WRITE(key, profile.distanceColor[c]);
                sprintf_s(key, "esp%d_weapon_color%d", p, c);
                CFG_WRITE(key, profile.weaponColor[c]);
                sprintf_s(key, "esp%d_dynid_color%d", p, c);
                CFG_WRITE(key, profile.dynamicIdColor[c]);
                sprintf_s(key, "esp%d_statid_color%d", p, c);
                CFG_WRITE(key, profile.staticIdColor[c]);
                sprintf_s(key, "esp%d_dead_color%d", p, c);
                CFG_WRITE(key, profile.deadColor[c]);
                sprintf_s(key, "esp%d_radar_color%d", p, c);
                CFG_WRITE(key, profile.radarColor[c]);
                sprintf_s(key, "esp%d_hidden_color%d", p, c);
                CFG_WRITE(key, profile.hiddenColor[c]);
            }
#undef PROFILE_WRITE
        }
        CFG_WRITE(
            "faction_colors_on",
            FactionColors::Enabled() ? 1 : 0);
        {
            const int factionRecords = FactionColors::CustomCount();
            CFG_WRITE("faction_colors_count", factionRecords);
            for (int i = 0; i < factionRecords; ++i) {
                unsigned int hash = 0;
                float color[4]{};
                if (!FactionColors::CustomRecord(i, hash, color))
                    continue;
                char key[48]{};
                sprintf_s(key, "faction_col%d_hash", i);
                // Written signed so it round-trips through the integer reader
                // instead of saturating at the signed maximum.
                CFG_WRITE(key, static_cast<int>(hash));
                for (int c = 0; c < 4; ++c) {
                    sprintf_s(key, "faction_col%d_%d", i, c);
                    CFG_WRITE(key, color[c]);
                }
            }
        }

        CFG_WRITE("radar_enabled", s_RadarSettings.enabled);
        CFG_WRITE("radar_show_players", s_RadarSettings.showPlayers);
        CFG_WRITE("radar_show_friends", s_RadarSettings.showFriends);
        CFG_WRITE("radar_show_npcs", s_RadarSettings.showNpcs);
        CFG_WRITE("radar_show_vehicles", s_RadarSettings.showVehicles);
        CFG_WRITE("radar_range", s_RadarSettings.range);
        CFG_WRITE("radar_pos_x", s_RadarSettings.posX);
        CFG_WRITE("radar_pos_y", s_RadarSettings.posY);
        CFG_WRITE("radar_size", s_RadarSettings.size);
        CFG_WRITE("radar_bg_opacity", s_RadarSettings.backgroundOpacity);
        CFG_WRITE("radar_marker_size", s_RadarSettings.markerSize);
        CFG_WRITE("radar_direction_len", s_RadarSettings.directionLength);

        CFG_WRITE("vehicle_vis_enabled", s_VehicleVisualSettings.enabled);
        CFG_WRITE("vehicle_vis_box", s_VehicleVisualSettings.box);
        CFG_WRITE("vehicle_vis_name", s_VehicleVisualSettings.name);
        CFG_WRITE("vehicle_vis_distance", s_VehicleVisualSettings.distance);
        CFG_WRITE("vehicle_vis_remote_id", s_VehicleVisualSettings.remoteId);
        CFG_WRITE("vehicle_vis_health", s_VehicleVisualSettings.health);
        CFG_WRITE("vehicle_vis_trunk", s_VehicleVisualSettings.trunk);
        CFG_WRITE("vehicle_vis_occupied", s_VehicleVisualSettings.showOccupied);
        CFG_WRITE("vehicle_vis_empty", s_VehicleVisualSettings.showEmpty);
        CFG_WRITE("vehicle_vis_cars", s_VehicleVisualSettings.showCars);
        CFG_WRITE("vehicle_vis_bikes", s_VehicleVisualSettings.showBikes);
        CFG_WRITE("vehicle_vis_boats", s_VehicleVisualSettings.showBoats);
        CFG_WRITE("vehicle_vis_aircraft", s_VehicleVisualSettings.showAircraft);
        CFG_WRITE("vehicle_vis_other", s_VehicleVisualSettings.showOther);
        CFG_WRITE("vehicle_vis_snapline", s_VehicleVisualSettings.snapLine);
        CFG_WRITE(
            "vehicle_vis_snapline_origin",
            s_VehicleVisualSettings.snapLineOrigin);
        CFG_WRITE("vehicle_vis_range", s_VehicleVisualSettings.maxDistance);
        CFG_WRITE(
            "vehicle_vis_include_count",
            s_VehicleVisualSettings.includeModelCount);
        CFG_WRITE(
            "vehicle_vis_exclude_count",
            s_VehicleVisualSettings.excludeModelCount);

        CFG_WRITE("object_vis_enabled", s_ObjectVisualSettings.enabled);
        CFG_WRITE("object_vis_box", s_ObjectVisualSettings.box);
        CFG_WRITE("object_vis_name", s_ObjectVisualSettings.name);
        CFG_WRITE("object_vis_distance", s_ObjectVisualSettings.distance);
        CFG_WRITE("object_vis_remote_id", s_ObjectVisualSettings.remoteId);
        CFG_WRITE("object_vis_objects", s_ObjectVisualSettings.showObjects);
        CFG_WRITE("object_vis_pickups", s_ObjectVisualSettings.showPickups);
        CFG_WRITE("object_vis_snapline", s_ObjectVisualSettings.snapLine);
        CFG_WRITE(
            "object_vis_snapline_origin",
            s_ObjectVisualSettings.snapLineOrigin);
        CFG_WRITE("object_vis_range", s_ObjectVisualSettings.maxDistance);
        CFG_WRITE(
            "object_vis_include_count",
            s_ObjectVisualSettings.includeModelCount);
        CFG_WRITE(
            "object_vis_exclude_count",
            s_ObjectVisualSettings.excludeModelCount);

        for (int c = 0; c < 4; ++c)
        {
            char key[64]{};
            sprintf_s(key, "radar_enemy_color%d", c);
            CFG_WRITE(key, s_RadarSettings.enemyColor[c]);
            sprintf_s(key, "radar_friend_color%d", c);
            CFG_WRITE(key, s_RadarSettings.friendColor[c]);
            sprintf_s(key, "radar_npc_color%d", c);
            CFG_WRITE(key, s_RadarSettings.npcColor[c]);
            sprintf_s(key, "radar_vehicle_color%d", c);
            CFG_WRITE(key, s_RadarSettings.vehicleColor[c]);
            sprintf_s(key, "vehicle_vis_box_color%d", c);
            CFG_WRITE(key, s_VehicleVisualSettings.boxColor[c]);
            sprintf_s(key, "vehicle_vis_text_color%d", c);
            CFG_WRITE(key, s_VehicleVisualSettings.textColor[c]);
            sprintf_s(key, "object_vis_object_color%d", c);
            CFG_WRITE(key, s_ObjectVisualSettings.objectColor[c]);
            sprintf_s(key, "object_vis_pickup_color%d", c);
            CFG_WRITE(key, s_ObjectVisualSettings.pickupColor[c]);
            sprintf_s(key, "object_vis_text_color%d", c);
            CFG_WRITE(key, s_ObjectVisualSettings.textColor[c]);
            sprintf_s(key, "vehicle_vis_snapline_color%d", c);
            CFG_WRITE(key, s_VehicleVisualSettings.snapLineColor[c]);
            sprintf_s(key, "object_vis_snapline_color%d", c);
            CFG_WRITE(key, s_ObjectVisualSettings.snapLineColor[c]);
        }
        for (int i = 0; i < kMaxVisualModelFilters; ++i)
        {
            char key[64]{};
            sprintf_s(key, "vehicle_vis_include_%d", i);
            CFG_WRITE(
                key,
                static_cast<unsigned>(
                    s_VehicleVisualSettings.includeModels[i]));
            sprintf_s(key, "vehicle_vis_exclude_%d", i);
            CFG_WRITE(
                key,
                static_cast<unsigned>(
                    s_VehicleVisualSettings.excludeModels[i]));
            sprintf_s(key, "object_vis_include_%d", i);
            CFG_WRITE(
                key,
                static_cast<unsigned>(
                    s_ObjectVisualSettings.includeModels[i]));
            sprintf_s(key, "object_vis_exclude_%d", i);
            CFG_WRITE(
                key,
                static_cast<unsigned>(
                    s_ObjectVisualSettings.excludeModels[i]));
        }
        {
            const auto& mesh = MeshSilhouette::GetSettings();
            CFG_WRITE("mesh_chams_enabled", mesh.enabled);
            CFG_WRITE("mesh_chams_fill", mesh.fill);
            CFG_WRITE("mesh_chams_edge", mesh.edge);
            CFG_WRITE("mesh_chams_opacity", mesh.opacity);
            CFG_WRITE("mesh_chams_edge_width", mesh.edgeWidth);
            CFG_WRITE("mesh_chams_softness", mesh.softness);
            for (int c = 0; c < 4; ++c)
            {
                char key[64]{};
                sprintf_s(key, "mesh_chams_visible_color%d", c);
                CFG_WRITE(key, (&mesh.visibleColor.r)[c]);
                sprintf_s(key, "mesh_chams_occluded_color%d", c);
                CFG_WRITE(key, (&mesh.occludedColor.r)[c]);
            }
        }
#undef CFG_WRITE
        if (!MiscFeatures::Export(output, capacity, used))
            return false;
        if (!Friends::Export(output, capacity, used))
            return false;
        return FeatureBinds::Export(
            output,
            capacity,
            used);
    }

    void ImportCurrentConfig(const char* content)
    {
        InitDefaultEspProfiles();
        EnsureAimProfilesInitialized();
        for (int group = 0; group < kAimWeaponGroupCount; ++group)
            s_AimWeaponProfiles[group] = MakeDefaultAimProfile();
        s_AimWeaponProfiles[0].useCustom = true;
        s_RadarSettings = {};
        s_VehicleVisualSettings = {};
        s_ObjectVisualSettings = {};
        MeshSilhouette::GetSettings() = {};
        PushGeneralToLegacy();
#define CFG_READ_BOOL(name, target) \
        if (const char* v = FindConfigValue(content, name)) target = std::strtol(v, nullptr, 10) != 0
#define CFG_READ_INT(name, target) \
        if (const char* v = FindConfigValue(content, name)) target = static_cast<int>(std::strtol(v, nullptr, 10))
#define CFG_READ_FLOAT(name, target) \
        if (const char* v = FindConfigValue(content, name)) target = std::strtof(v, nullptr)
        int language = static_cast<int>(s_Language);
        CFG_READ_INT("language", language);
        if (language >= 0 && language <= 2)
            s_Language = static_cast<Language>(language);
        CFG_READ_INT("ui_scale", s_UIScalePercent);
        CFG_READ_INT("menu_toggle_key", s_MenuToggleKey);
        CFG_READ_INT("menu_unload_key", s_UnloadKey);
        if (s_MenuToggleKey <= 0 || s_MenuToggleKey > 0xA5)
            s_MenuToggleKey = VK_F4;
        if (s_UnloadKey < 0 || s_UnloadKey > 0xA5)
            s_UnloadKey = 0;
        CFG_READ_BOOL("menu_light_theme", s_LightTheme);
        CFG_READ_BOOL("menu_window_gradient", set->window_gradient);
        CFG_READ_BOOL("menu_watermark", set->watermark);
        {
            bool reducedMotion = MenuMotion::ReducedMotion();
            CFG_READ_BOOL("menu_reduced_motion", reducedMotion);
            MenuMotion::SetReducedMotion(reducedMotion);
        }
        CFG_READ_BOOL("menu_particles", s_Particles);
        CFG_READ_BOOL("menu_cursor_particles", set->particle_cursor);
        CFG_READ_BOOL("menu_obs_bypass", set->obs_bypass);
        CFG_READ_FLOAT("menu_window_rounding", set->window_rounding);
        CFG_READ_FLOAT("menu_child_rounding", set->child_rounding);
        CFG_READ_FLOAT("menu_selection_rounding", set->selection_rounding);
        CFG_READ_FLOAT("menu_input_rounding", set->input_rounding);
        CFG_READ_FLOAT("menu_combo_rounding", set->combo_rounding);
        CFG_READ_FLOAT("menu_scrollbar_size", set->window_scrollbar_size);
        CFG_READ_INT("menu_particle_count", set->particle_num);
        CFG_READ_INT("menu_particle_gravity", set->particle_gravity);
        for (int c = 0; c < 4; ++c)
        {
            char key[48]{};
            sprintf_s(key, "menu_accent_%d", c);
            CFG_READ_FLOAT(key, func->accent_color[c]);
            sprintf_s(key, "menu_window_bg_%d", c);
            CFG_READ_FLOAT(key, (&clr->window_background.x)[c]);
            sprintf_s(key, "menu_panel_%d", c);
            CFG_READ_FLOAT(key, (&clr->child_bottom_bar.x)[c]);
            sprintf_s(key, "menu_element_%d", c);
            CFG_READ_FLOAT(key, (&clr->element_background.x)[c]);
            sprintf_s(key, "menu_text_active_%d", c);
            CFG_READ_FLOAT(key, (&clr->text_active.x)[c]);
            sprintf_s(key, "menu_text_%d", c);
            CFG_READ_FLOAT(key, (&clr->text.x)[c]);
            sprintf_s(key, "menu_particle_color_%d", c);
            CFG_READ_FLOAT(key, set->particle_color[c]);
        }
        MenuTheme::SetAccentBase(
            ImVec4(
                func->accent_color[0],
                func->accent_color[1],
                func->accent_color[2],
                1.f));
        // window_background is the stored canvas, so feed it back as the base
        // the rest of the neutral ramp is derived from.
        MenuTheme::SetCanvasBase(clr->window_background);
        ObsBypass::SetEnabled(set->obs_bypass);
        ApplyRawImGuiTheme(ImGui::GetStyle());
        CFG_READ_BOOL("aim_silent", s_Aim_EnableSilent);
        CFG_READ_BOOL("aim_visible", s_Aim_VisibleCheck);
        CFG_READ_BOOL("aim_nearest", s_Aim_Nearest);
        CFG_READ_BOOL("aim_head", s_Aim_HitboxHead);
        CFG_READ_BOOL("aim_neck", s_Aim_HitboxNeck);
        CFG_READ_BOOL("aim_chest", s_Aim_HitboxChest);
        CFG_READ_BOOL("aim_pelvis", s_Aim_HitboxPelvis);
        CFG_READ_BOOL("aim_draw_fov", s_Aim_DrawFov);
        CFG_READ_FLOAT("aim_fov", s_Aim_FOV);
        CFG_READ_BOOL("aim_mark", s_Aim_MarkTarget);
        for (int c = 0; c < 3; ++c) {
            char key[32]{};
            sprintf_s(key, "aim_mark_color%d", c);
            CFG_READ_FLOAT(key, s_Aim_MarkColor[c]);
        }
        CFG_READ_BOOL("aim_lock", s_Aim_LockOnLast);
        CFG_READ_BOOL("aim_hitchance_enabled", s_Aim_EnableHitchance);
        CFG_READ_FLOAT("aim_hitchance", s_Aim_Hitchance);
        CFG_READ_BOOL("aim_ignore_dead", s_Aim_IgnoreDead);
        CFG_READ_BOOL("aim_ignore_npc", s_Aim_IgnoreNpc);
        CFG_READ_BOOL("aim_ignore_admins", s_Aim_IgnoreAdmins);
        CFG_READ_BOOL("aim_ignore_friends", s_Aim_IgnoreFriends);
        CFG_READ_BOOL("vec_enabled", s_Vec_EnableVector);
        CFG_READ_BOOL("vec_visible", s_Vec_VisibleCheck);
        CFG_READ_BOOL("vec_nearest", s_Vec_Nearest);
        CFG_READ_BOOL("vec_head", s_Vec_HitboxHead);
        CFG_READ_BOOL("vec_neck", s_Vec_HitboxNeck);
        CFG_READ_BOOL("vec_chest", s_Vec_HitboxChest);
        CFG_READ_BOOL("vec_pelvis", s_Vec_HitboxPelvis);
        CFG_READ_INT("vec_key", s_Vec_ActivationKey);
        CFG_READ_BOOL("vec_draw_fov", s_Vec_DrawFov);
        CFG_READ_FLOAT("vec_fov", s_Vec_FOV);
        CFG_READ_FLOAT("vec_smoothness", s_Vec_Smoothness);
        CFG_READ_BOOL("vec_random_smooth", s_Vec_RandomSmooth);
        CFG_READ_FLOAT("vec_random_amt", s_Vec_RandomSmoothAmt);
        CFG_READ_BOOL("vec_smart_smooth", s_Vec_EnableSmartSmooth);
        CFG_READ_BOOL("vec_cam_shake", s_Vec_CameraShake);
        CFG_READ_FLOAT("vec_cam_shake_amt", s_Vec_CameraShakeAmt);
        PullLegacyIntoGeneral();
        for (int g = 0; g < kAimWeaponGroupCount; ++g) {
            AimWeaponProfile& p = s_AimWeaponProfiles[g];
            char key[64]{};
#define WG_READ_BOOL(suffix, target) \
            sprintf_s(key, "aim_wg%d_" suffix, g); CFG_READ_BOOL(key, target)
#define WG_READ_FLOAT(suffix, target) \
            sprintf_s(key, "aim_wg%d_" suffix, g); CFG_READ_FLOAT(key, target)
#define WG_READ_INT(suffix, target) \
            sprintf_s(key, "aim_wg%d_" suffix, g); CFG_READ_INT(key, target)
            WG_READ_BOOL("custom", p.useCustom);
            WG_READ_BOOL("vec_on", p.vecEnabled);
            WG_READ_INT("vec_key", p.vecKey);
            WG_READ_BOOL("vec_pred", p.vecPrediction);
            WG_READ_FLOAT("vec_pred_scale", p.vecPredictionScale);
            WG_READ_FLOAT("vec_fov", p.vecFov);
            WG_READ_BOOL("vec_draw", p.vecDrawFov);
            WG_READ_BOOL("vec_near", p.vecNearest);
            WG_READ_BOOL("vec_sticky", p.vecSticky);
            WG_READ_BOOL("vec_bone_any", p.vecBoneNearestAny);
            WG_READ_BOOL("vec_vis", p.vecVisible);
            {
                char key[64]{};
                int mask = static_cast<int>(BonesToMask(p.vecBones));
                sprintf_s(key, "aim_wg%d_vec_bones", g);
                CFG_READ_INT(key, mask);
                MaskToBones(static_cast<uint32_t>(mask), p.vecBones);
                // Legacy per-bone keys
                bool head = p.vecBones[0], neck = p.vecBones[1];
                bool chest = p.vecBones[2], pelvis = p.vecBones[3];
                sprintf_s(key, "aim_wg%d_vec_head", g); CFG_READ_BOOL(key, head);
                sprintf_s(key, "aim_wg%d_vec_neck", g); CFG_READ_BOOL(key, neck);
                sprintf_s(key, "aim_wg%d_vec_chest", g); CFG_READ_BOOL(key, chest);
                sprintf_s(key, "aim_wg%d_vec_pelvis", g); CFG_READ_BOOL(key, pelvis);
                p.vecBones[0] = head; p.vecBones[1] = neck;
                p.vecBones[2] = chest; p.vecBones[3] = pelvis;
            }
            WG_READ_FLOAT("vec_smooth", p.vecSmoothness);
            WG_READ_BOOL("vec_rand", p.vecRandomSmooth);
            WG_READ_FLOAT("vec_rand_amt", p.vecRandomAmt);
            WG_READ_BOOL("vec_smart", p.vecSmartSmooth);
            WG_READ_BOOL("vec_shake", p.vecCamShake);
            WG_READ_FLOAT("vec_shake_amt", p.vecShakeAmt);
            WG_READ_BOOL("dyn_fov", p.dynamicFov);
            WG_READ_FLOAT("dyn_ads", p.dynamicFovAdsMul);
            WG_READ_FLOAT("dyn_speed", p.dynamicFovSpeedExpand);
            WG_READ_BOOL("fov_curve_on", p.useFovCurve);
            WG_READ_FLOAT("fov_dist_mul", p.fovDistanceMul);
            WG_READ_BOOL("smooth_curve_on", p.useSmoothCurve);
            WG_READ_FLOAT("smooth_dist_mul", p.smoothDistanceMul);
            WG_READ_BOOL("humanize", p.humanize);
            WG_READ_FLOAT("react_ms", p.reactionDelayMs);
            WG_READ_FLOAT("jitter", p.microJitter);
            WG_READ_FLOAT("overshoot", p.overshoot);
            WG_READ_FLOAT("switch_ms", p.targetSwitchDelayMs);
            WG_READ_FLOAT("fatigue", p.fatigue);
            for (int ci = 0; ci < kAimCurvePoints; ++ci) {
                sprintf_s(key, "aim_wg%d_fov_curve_%d", g, ci);
                CFG_READ_FLOAT(key, p.fovDistanceCurve.y[ci]);
                sprintf_s(key, "aim_wg%d_smooth_curve_%d", g, ci);
                CFG_READ_FLOAT(key, p.smoothDistanceCurve.y[ci]);
            }
            for (int c = 0; c < 3; ++c) {
                sprintf_s(key, "aim_wg%d_vec_color_%d", g, c);
                CFG_READ_FLOAT(key, p.vecFovColor[c]);
            }
            WG_READ_BOOL("sil_on", p.silentEnabled);
            WG_READ_INT("sil_key", p.silentKey);
            WG_READ_FLOAT("sil_fov", p.silentFov);
            WG_READ_BOOL("sil_draw", p.silentDrawFov);
            WG_READ_BOOL("sil_mark", p.silentMark);
            WG_READ_INT("sil_mark_style", p.silentMarkStyle);
            WG_READ_BOOL("sil_lock", p.silentLock);
            WG_READ_BOOL("sil_near", p.silentNearest);
            WG_READ_BOOL("sil_bone_any", p.silentBoneNearestAny);
            WG_READ_BOOL("sil_vis", p.silentVisible);
            WG_READ_BOOL("sil_wall", p.silentWallshot);
            {
                char key[64]{};
                int mask = static_cast<int>(BonesToMask(p.silentBones));
                sprintf_s(key, "aim_wg%d_sil_bones", g);
                CFG_READ_INT(key, mask);
                MaskToBones(static_cast<uint32_t>(mask), p.silentBones);
                bool head = p.silentBones[0], neck = p.silentBones[1];
                bool chest = p.silentBones[2], pelvis = p.silentBones[3];
                sprintf_s(key, "aim_wg%d_sil_head", g); CFG_READ_BOOL(key, head);
                sprintf_s(key, "aim_wg%d_sil_neck", g); CFG_READ_BOOL(key, neck);
                sprintf_s(key, "aim_wg%d_sil_chest", g); CFG_READ_BOOL(key, chest);
                sprintf_s(key, "aim_wg%d_sil_pelvis", g); CFG_READ_BOOL(key, pelvis);
                p.silentBones[0] = head; p.silentBones[1] = neck;
                p.silentBones[2] = chest; p.silentBones[3] = pelvis;
            }
            WG_READ_BOOL("sil_hc", p.silentHitchance);
            WG_READ_FLOAT("sil_hc_pct", p.silentHitchancePct);
            WG_READ_BOOL("sil_pred", p.silentPrediction);
            WG_READ_FLOAT("sil_lead", p.silentLeadScale);
            WG_READ_FLOAT("sil_react", p.silentReactionMs);
            WG_READ_FLOAT("sil_miss", p.silentMissBias);
            WG_READ_BOOL("sil_hc_curve_on", p.useSilentHcCurve);
            WG_READ_FLOAT("sil_hc_dist_mul", p.silentHcDistanceMul);
            for (int ci = 0; ci < kAimCurvePoints; ++ci) {
                sprintf_s(key, "aim_wg%d_sil_hc_curve_%d", g, ci);
                CFG_READ_FLOAT(key, p.silentHitchanceCurve.y[ci]);
            }
            for (int c = 0; c < 3; ++c) {
                sprintf_s(key, "aim_wg%d_sil_color_%d", g, c);
                CFG_READ_FLOAT(key, p.silentFovColor[c]);
            }
            WG_READ_BOOL("trig_on", p.triggerEnabled);
            WG_READ_INT("trig_key", p.triggerKey);
            WG_READ_FLOAT("trig_delay", p.triggerDelayMs);
            WG_READ_FLOAT("trig_cooldown", p.triggerCooldownMs);
            WG_READ_BOOL("trig_vis", p.triggerVisible);
            {
                int mask = static_cast<int>(
                    BonesToMask(p.triggerBones));
                sprintf_s(key, "aim_wg%d_trig_bones", g);
                CFG_READ_INT(key, mask);
                MaskToBones(
                    static_cast<std::uint32_t>(mask),
                    p.triggerBones);
            }
            WG_READ_BOOL("sil_dead", p.ignoreDead); // legacy
            WG_READ_BOOL("ignore_dead", p.ignoreDead);
            WG_READ_BOOL("ignore_npc", p.ignoreNpc);
            WG_READ_BOOL("ignore_admins", p.ignoreAdmins);
            WG_READ_BOOL("ignore_friends", p.ignoreFriends);
            WG_READ_BOOL("no_spread", p.noSpread);
            WG_READ_FLOAT("no_spread_pct", p.noSpreadPct);
            WG_READ_BOOL("no_recoil", p.noRecoil);
            WG_READ_FLOAT("no_recoil_pct", p.noRecoilPct);
            WG_READ_BOOL("reload_spd", p.reloadSpeed);
            WG_READ_FLOAT("reload_pct", p.reloadSpeedPct);
#undef WG_READ_BOOL
#undef WG_READ_FLOAT
#undef WG_READ_INT
        }
        s_AimWeaponProfiles[0].useCustom = true;
        PushGeneralToLegacy();
        // Restore Weapon Manager per-weapon overrides.
        s_WeaponOverrideCount = 0;
        for (int wi = 0; wi < WeaponCatalog::kWeaponCount; ++wi) {
            const std::uint32_t hash = WeaponCatalog::kWeapons[wi].hash;
            char key[72]{};
            sprintf_s(key, "aim_wpn_%u_active", hash);
            if (!FindConfigValue(content, key))
                continue;
            bool active = false;
            CFG_READ_BOOL(key, active);
            WeaponOverrideSlot* slotPtr =
                GetOrCreateWeaponOverride(hash);
            if (!slotPtr)
                continue;
            WeaponOverrideSlot& slot = *slotPtr;
            AimWeaponProfile& p = slot.profile;
            slot.active = active;
#define WO_READ_BOOL(suffix, target) \
            sprintf_s(key, "aim_wpn_%u_" suffix, hash); CFG_READ_BOOL(key, target)
#define WO_READ_FLOAT(suffix, target) \
            sprintf_s(key, "aim_wpn_%u_" suffix, hash); CFG_READ_FLOAT(key, target)
#define WO_READ_INT(suffix, target) \
            sprintf_s(key, "aim_wpn_%u_" suffix, hash); CFG_READ_INT(key, target)
            WO_READ_BOOL("active", slot.active);
            WO_READ_BOOL("vec_on", p.vecEnabled);
            {
                int keyValue = p.vecKey;
                sprintf_s(key, "aim_wpn_%u_vec_key", hash);
                CFG_READ_INT(key, keyValue);
                p.vecKey = keyValue;
            }
            WO_READ_BOOL("vec_pred", p.vecPrediction);
            WO_READ_FLOAT("vec_pred_scale", p.vecPredictionScale);
            WO_READ_BOOL("vec_draw", p.vecDrawFov);
            WO_READ_FLOAT("vec_fov", p.vecFov);
            WO_READ_BOOL("vec_near", p.vecNearest);
            WO_READ_BOOL("vec_sticky", p.vecSticky);
            WO_READ_BOOL("vec_bone_any", p.vecBoneNearestAny);
            WO_READ_BOOL("vec_vis", p.vecVisible);
            {
                int mask = static_cast<int>(BonesToMask(p.vecBones));
                sprintf_s(key, "aim_wpn_%u_vec_bones", hash);
                CFG_READ_INT(key, mask);
                MaskToBones(
                    static_cast<std::uint32_t>(mask),
                    p.vecBones);
            }
            WO_READ_BOOL("dyn_fov", p.dynamicFov);
            WO_READ_FLOAT("dyn_ads", p.dynamicFovAdsMul);
            WO_READ_FLOAT("dyn_speed", p.dynamicFovSpeedExpand);
            WO_READ_BOOL("fov_curve_on", p.useFovCurve);
            WO_READ_FLOAT("fov_dist_mul", p.fovDistanceMul);
            WO_READ_BOOL("smooth_curve_on", p.useSmoothCurve);
            WO_READ_FLOAT("smooth_dist_mul", p.smoothDistanceMul);
            WO_READ_FLOAT("vec_smooth", p.vecSmoothness);
            WO_READ_BOOL("vec_rand", p.vecRandomSmooth);
            WO_READ_FLOAT("vec_rand_amt", p.vecRandomAmt);
            WO_READ_BOOL("vec_smart", p.vecSmartSmooth);
            WO_READ_BOOL("vec_shake", p.vecCamShake);
            WO_READ_FLOAT("vec_shake_amt", p.vecShakeAmt);
            WO_READ_BOOL("humanize", p.humanize);
            WO_READ_FLOAT("react_ms", p.reactionDelayMs);
            WO_READ_FLOAT("jitter", p.microJitter);
            WO_READ_FLOAT("overshoot", p.overshoot);
            WO_READ_FLOAT("switch_ms", p.targetSwitchDelayMs);
            WO_READ_FLOAT("fatigue", p.fatigue);
            for (int ci = 0; ci < kAimCurvePoints; ++ci) {
                sprintf_s(
                    key,
                    "aim_wpn_%u_fov_curve_%d",
                    hash,
                    ci);
                CFG_READ_FLOAT(key, p.fovDistanceCurve.y[ci]);
                sprintf_s(
                    key,
                    "aim_wpn_%u_smooth_curve_%d",
                    hash,
                    ci);
                CFG_READ_FLOAT(key, p.smoothDistanceCurve.y[ci]);
            }
            for (int c = 0; c < 3; ++c) {
                sprintf_s(
                    key,
                    "aim_wpn_%u_vec_color_%d",
                    hash,
                    c);
                CFG_READ_FLOAT(key, p.vecFovColor[c]);
            }
            WO_READ_BOOL("sil_on", p.silentEnabled);
            {
                int keyValue = p.silentKey;
                sprintf_s(key, "aim_wpn_%u_sil_key", hash);
                CFG_READ_INT(key, keyValue);
                p.silentKey = keyValue;
            }
            WO_READ_BOOL("sil_draw", p.silentDrawFov);
            WO_READ_FLOAT("sil_fov", p.silentFov);
            WO_READ_BOOL("sil_mark", p.silentMark);
            WO_READ_INT("sil_mark_style", p.silentMarkStyle);
            WO_READ_BOOL("sil_lock", p.silentLock);
            WO_READ_BOOL("sil_near", p.silentNearest);
            WO_READ_BOOL("sil_bone_any", p.silentBoneNearestAny);
            WO_READ_BOOL("sil_vis", p.silentVisible);
            WO_READ_BOOL("sil_wall", p.silentWallshot);
            {
                int mask = static_cast<int>(
                    BonesToMask(p.silentBones));
                sprintf_s(key, "aim_wpn_%u_sil_bones", hash);
                CFG_READ_INT(key, mask);
                MaskToBones(
                    static_cast<std::uint32_t>(mask),
                    p.silentBones);
            }
            WO_READ_BOOL("sil_hc_on", p.silentHitchance);
            WO_READ_FLOAT("sil_hc", p.silentHitchancePct);
            WO_READ_BOOL("sil_pred", p.silentPrediction);
            WO_READ_FLOAT("sil_lead", p.silentLeadScale);
            WO_READ_FLOAT("sil_react", p.silentReactionMs);
            WO_READ_FLOAT("sil_miss", p.silentMissBias);
            WO_READ_BOOL("sil_hc_curve_on", p.useSilentHcCurve);
            WO_READ_FLOAT("sil_hc_dist_mul", p.silentHcDistanceMul);
            for (int ci = 0; ci < kAimCurvePoints; ++ci) {
                sprintf_s(
                    key,
                    "aim_wpn_%u_sil_hc_curve_%d",
                    hash,
                    ci);
                CFG_READ_FLOAT(key, p.silentHitchanceCurve.y[ci]);
            }
            for (int c = 0; c < 3; ++c) {
                sprintf_s(
                    key,
                    "aim_wpn_%u_sil_color_%d",
                    hash,
                    c);
                CFG_READ_FLOAT(key, p.silentFovColor[c]);
            }
            WO_READ_BOOL("trig_on", p.triggerEnabled);
            {
                int keyValue = p.triggerKey;
                sprintf_s(key, "aim_wpn_%u_trig_key", hash);
                CFG_READ_INT(key, keyValue);
                p.triggerKey = keyValue;
            }
            WO_READ_FLOAT("trig_delay", p.triggerDelayMs);
            WO_READ_FLOAT("trig_cooldown", p.triggerCooldownMs);
            WO_READ_BOOL("trig_vis", p.triggerVisible);
            {
                int mask = static_cast<int>(
                    BonesToMask(p.triggerBones));
                sprintf_s(key, "aim_wpn_%u_trig_bones", hash);
                CFG_READ_INT(key, mask);
                MaskToBones(
                    static_cast<std::uint32_t>(mask),
                    p.triggerBones);
            }
            WO_READ_BOOL("ignore_dead", p.ignoreDead);
            WO_READ_BOOL("ignore_npc", p.ignoreNpc);
            WO_READ_BOOL("ignore_admins", p.ignoreAdmins);
            WO_READ_BOOL("ignore_friends", p.ignoreFriends);
            WO_READ_BOOL("no_spread", p.noSpread);
            WO_READ_FLOAT("no_spread_pct", p.noSpreadPct);
            WO_READ_BOOL("no_recoil", p.noRecoil);
            WO_READ_FLOAT("no_recoil_pct", p.noRecoilPct);
            WO_READ_BOOL("reload_spd", p.reloadSpeed);
            WO_READ_FLOAT("reload_pct", p.reloadSpeedPct);
            {
                int count = 0;
                sprintf_s(key, "wpn_att_%u", hash);
                CFG_READ_INT(key, count);
                if (count < 0) count = 0;
                if (count > kMaxWeaponAttachments) count = kMaxWeaponAttachments;
                slot.attachmentCount = count;
                for (int a = 0; a < count; ++a) {
                    int comp = 0;
                    sprintf_s(key, "wpn_att_%u_%d", hash, a);
                    CFG_READ_INT(key, comp);
                    slot.attachments[a] = static_cast<std::uint32_t>(comp);
                }
            }
#undef WO_READ_BOOL
#undef WO_READ_FLOAT
        }
        // Legacy configs used 0..1 Smooth X/Y — map average into sp¬ 1..100.
        if (const char* legacyX = FindConfigValue(content, "vec_smooth_x")) {
            const float sx = std::strtof(legacyX, nullptr);
            float sy = sx;
            if (const char* legacyY = FindConfigValue(content, "vec_smooth_y"))
                sy = std::strtof(legacyY, nullptr);
            const float avg = (sx + sy) * 0.5f;
            if (avg > 0.0f && avg <= 1.0f)
                s_Vec_Smoothness = 1.0f / (avg < 0.05f ? 0.05f : avg);
        }
        for (int p = 0;
             p < static_cast<int>(EspProfileId::Count);
             ++p) {
            char key[48]{};
            EspProfileSettings& profile = s_EspProfiles[p];
#define PROFILE_READ(suffix, macro, target) \
            sprintf_s(key, "esp%d_" suffix, p); macro(key, target)
            PROFILE_READ("enabled", CFG_READ_BOOL, profile.enabled);
            PROFILE_READ("box", CFG_READ_BOOL, profile.box);
            PROFILE_READ("health", CFG_READ_BOOL, profile.healthBar);
            PROFILE_READ("armor", CFG_READ_BOOL, profile.armorBar);
            PROFILE_READ("name", CFG_READ_BOOL, profile.nameTag);
            PROFILE_READ("faction", CFG_READ_BOOL, profile.factionTag);
            PROFILE_READ("family", CFG_READ_BOOL, profile.familyTag);
            PROFILE_READ("skeleton", CFG_READ_BOOL, profile.skeleton);
            PROFILE_READ("snapline", CFG_READ_BOOL, profile.snapLine);
            PROFILE_READ(
                "snapline_origin",
                CFG_READ_INT,
                profile.snapLineOrigin);
            PROFILE_READ("distance", CFG_READ_BOOL, profile.distance);
            PROFILE_READ("weapon", CFG_READ_BOOL, profile.weaponTag);
            PROFILE_READ("dyn_id", CFG_READ_BOOL, profile.dynamicIdTag);
            PROFILE_READ("stat_id", CFG_READ_BOOL, profile.staticIdTag);
            PROFILE_READ("show_dead", CFG_READ_BOOL, profile.showDead);
            PROFILE_READ("visible", CFG_READ_BOOL, profile.visibleCheck);
            PROFILE_READ("hidden_style", CFG_READ_INT, profile.hiddenStyle);
            PROFILE_READ("radar", CFG_READ_BOOL, profile.radarOverlay);
            PROFILE_READ("health_pos", CFG_READ_INT, profile.healthBarPos);
            PROFILE_READ("armor_pos", CFG_READ_INT, profile.armorBarPos);
            PROFILE_READ("name_pos", CFG_READ_INT, profile.nameTagPos);
            PROFILE_READ("faction_pos", CFG_READ_INT, profile.factionTagPos);
            PROFILE_READ("distance_pos", CFG_READ_INT, profile.distancePos);
            PROFILE_READ("weapon_pos", CFG_READ_INT, profile.weaponTagPos);
            PROFILE_READ("box_style", CFG_READ_INT, profile.boxStyle);
            PROFILE_READ("health_style", CFG_READ_INT, profile.healthStyle);
            PROFILE_READ("armor_style", CFG_READ_INT, profile.armorStyle);
            PROFILE_READ("weapon_disp", CFG_READ_INT, profile.weaponDisplay);
            PROFILE_READ("radar_range", CFG_READ_FLOAT, profile.radarRange);
            PROFILE_READ("radar_pos_x", CFG_READ_FLOAT, profile.radarPosX);
            PROFILE_READ("radar_pos_y", CFG_READ_FLOAT, profile.radarPosY);
            PROFILE_READ("radar_size", CFG_READ_FLOAT, profile.radarSize);
            {
                int mig = 0;
                sprintf_s(key, "esp%d_layouts_mig", p);
                CFG_READ_INT(key, mig);
                profile.layoutsMigrated = mig != 0;
                int showHp = 1, showAr = 1;
                sprintf_s(key, "esp%d_hp_show_val", p);
                CFG_READ_INT(key, showHp);
                sprintf_s(key, "esp%d_ar_show_val", p);
                CFG_READ_INT(key, showAr);
                profile.healthShowValue = showHp != 0;
                profile.armorShowValue = showAr != 0;
                for (int li = 0; li < 9; ++li) {
                    sprintf_s(key, "esp%d_lay%d_side", p, li);
                    CFG_READ_INT(key, profile.layouts[li].side);
                    sprintf_s(key, "esp%d_lay%d_ord", p, li);
                    CFG_READ_INT(key, profile.layouts[li].order);
                    sprintf_s(key, "esp%d_lay%d_sz", p, li);
                    CFG_READ_FLOAT(key, profile.layouts[li].size);
                }
            }
            if (profile.healthStyle < 0 ||
                profile.healthStyle >= EspBarStyleCount)
                profile.healthStyle = EspBarSolid;
            if (profile.armorStyle < 0 ||
                profile.armorStyle >= EspBarStyleCount)
                profile.armorStyle = EspBarSolid;
            if (profile.hiddenStyle < 0 ||
                profile.hiddenStyle >= EspHiddenStyleCount)
                profile.hiddenStyle = EspHiddenTintAll;
            if (profile.weaponDisplay < 0 ||
                profile.weaponDisplay >= EspWeaponDisplayCount)
                profile.weaponDisplay = EspWeaponText;
            if (profile.nameTagPos != EspTagAbove &&
                profile.nameTagPos != EspTagBelow)
                profile.nameTagPos = EspTagAbove;
            if (profile.factionTagPos != EspTagAbove &&
                profile.factionTagPos != EspTagBelow)
                profile.factionTagPos = EspTagAbove;
            if (profile.distancePos != EspTagAbove &&
                profile.distancePos != EspTagBelow)
                profile.distancePos = EspTagBelow;
            if (profile.weaponTagPos != EspTagAbove &&
                profile.weaponTagPos != EspTagBelow)
                profile.weaponTagPos = EspTagBelow;
            for (int c = 0; c < 4; ++c) {
                sprintf_s(key, "esp%d_box_color%d", p, c);
                CFG_READ_FLOAT(key, profile.boxColor[c]);
                sprintf_s(key, "esp%d_box_fill%d", p, c);
                CFG_READ_FLOAT(key, profile.boxFillColor[c]);
                sprintf_s(key, "esp%d_name_color%d", p, c);
                CFG_READ_FLOAT(key, profile.nameColor[c]);
                sprintf_s(key, "esp%d_faction_color%d", p, c);
                CFG_READ_FLOAT(key, profile.factionColor[c]);
                sprintf_s(key, "esp%d_family_color%d", p, c);
                CFG_READ_FLOAT(key, profile.familyColor[c]);
                sprintf_s(key, "esp%d_health_color%d", p, c);
                CFG_READ_FLOAT(key, profile.healthColor[c]);
                sprintf_s(key, "esp%d_armor_color%d", p, c);
                CFG_READ_FLOAT(key, profile.armorColor[c]);
                sprintf_s(key, "esp%d_skeleton_color%d", p, c);
                CFG_READ_FLOAT(key, profile.skeletonColor[c]);
                sprintf_s(key, "esp%d_snapline_color%d", p, c);
                CFG_READ_FLOAT(key, profile.snapLineColor[c]);
                sprintf_s(key, "esp%d_distance_color%d", p, c);
                CFG_READ_FLOAT(key, profile.distanceColor[c]);
                sprintf_s(key, "esp%d_weapon_color%d", p, c);
                CFG_READ_FLOAT(key, profile.weaponColor[c]);
                sprintf_s(key, "esp%d_dynid_color%d", p, c);
                CFG_READ_FLOAT(key, profile.dynamicIdColor[c]);
                sprintf_s(key, "esp%d_statid_color%d", p, c);
                CFG_READ_FLOAT(key, profile.staticIdColor[c]);
                sprintf_s(key, "esp%d_dead_color%d", p, c);
                CFG_READ_FLOAT(key, profile.deadColor[c]);
                sprintf_s(key, "esp%d_radar_color%d", p, c);
                CFG_READ_FLOAT(key, profile.radarColor[c]);
                sprintf_s(key, "esp%d_hidden_color%d", p, c);
                CFG_READ_FLOAT(key, profile.hiddenColor[c]);
            }
            EspProjected::EnsureLayoutsMigrated(profile);
#undef PROFILE_READ
        }
        const bool hasStandaloneRadar =
            FindConfigValue(content, "radar_enabled") != nullptr;
        if (!hasStandaloneRadar)
        {
            const EspProfileSettings& legacy =
                s_EspProfiles[static_cast<int>(
                    EspProfileId::Players)];
            s_RadarSettings.enabled = legacy.radarOverlay;
            s_RadarSettings.range = legacy.radarRange;
            s_RadarSettings.posX = legacy.radarPosX;
            s_RadarSettings.posY = legacy.radarPosY;
            s_RadarSettings.size = legacy.radarSize;
            for (int c = 0; c < 4; ++c)
                s_RadarSettings.enemyColor[c] =
                    legacy.radarColor[c];
        }

        CFG_READ_BOOL("faction_colors_on", FactionColors::Enabled());
        {
            int factionRecords = 0;
            CFG_READ_INT("faction_colors_count", factionRecords);
            if (factionRecords > 64)
                factionRecords = 64;
            for (int i = 0; i < factionRecords; ++i) {
                char key[48]{};
                sprintf_s(key, "faction_col%d_hash", i);
                int hash = 0;
                CFG_READ_INT(key, hash);
                if (!hash)
                    continue;
                float color[4] = { 1.f, 1.f, 1.f, 1.f };
                for (int c = 0; c < 4; ++c) {
                    sprintf_s(key, "faction_col%d_%d", i, c);
                    CFG_READ_FLOAT(key, color[c]);
                }
                FactionColors::ApplySavedColor(
                    static_cast<unsigned int>(hash),
                    color);
            }
        }

        CFG_READ_BOOL("radar_enabled", s_RadarSettings.enabled);
        CFG_READ_BOOL("radar_show_players", s_RadarSettings.showPlayers);
        CFG_READ_BOOL("radar_show_friends", s_RadarSettings.showFriends);
        CFG_READ_BOOL("radar_show_npcs", s_RadarSettings.showNpcs);
        CFG_READ_BOOL("radar_show_vehicles", s_RadarSettings.showVehicles);
        CFG_READ_FLOAT("radar_range", s_RadarSettings.range);
        CFG_READ_FLOAT("radar_pos_x", s_RadarSettings.posX);
        CFG_READ_FLOAT("radar_pos_y", s_RadarSettings.posY);
        CFG_READ_FLOAT("radar_size", s_RadarSettings.size);
        CFG_READ_FLOAT("radar_bg_opacity", s_RadarSettings.backgroundOpacity);
        CFG_READ_FLOAT("radar_marker_size", s_RadarSettings.markerSize);
        CFG_READ_FLOAT("radar_direction_len", s_RadarSettings.directionLength);

        CFG_READ_BOOL("vehicle_vis_enabled", s_VehicleVisualSettings.enabled);
        CFG_READ_BOOL("vehicle_vis_box", s_VehicleVisualSettings.box);
        CFG_READ_BOOL("vehicle_vis_name", s_VehicleVisualSettings.name);
        CFG_READ_BOOL("vehicle_vis_distance", s_VehicleVisualSettings.distance);
        CFG_READ_BOOL("vehicle_vis_remote_id", s_VehicleVisualSettings.remoteId);
        CFG_READ_BOOL("vehicle_vis_health", s_VehicleVisualSettings.health);
        CFG_READ_BOOL("vehicle_vis_trunk", s_VehicleVisualSettings.trunk);
        CFG_READ_BOOL("vehicle_vis_occupied", s_VehicleVisualSettings.showOccupied);
        CFG_READ_BOOL("vehicle_vis_empty", s_VehicleVisualSettings.showEmpty);
        CFG_READ_BOOL("vehicle_vis_cars", s_VehicleVisualSettings.showCars);
        CFG_READ_BOOL("vehicle_vis_bikes", s_VehicleVisualSettings.showBikes);
        CFG_READ_BOOL("vehicle_vis_boats", s_VehicleVisualSettings.showBoats);
        CFG_READ_BOOL("vehicle_vis_aircraft", s_VehicleVisualSettings.showAircraft);
        CFG_READ_BOOL("vehicle_vis_other", s_VehicleVisualSettings.showOther);
        CFG_READ_BOOL(
            "vehicle_vis_snapline",
            s_VehicleVisualSettings.snapLine);
        CFG_READ_INT(
            "vehicle_vis_snapline_origin",
            s_VehicleVisualSettings.snapLineOrigin);
        CFG_READ_FLOAT("vehicle_vis_range", s_VehicleVisualSettings.maxDistance);
        CFG_READ_INT(
            "vehicle_vis_include_count",
            s_VehicleVisualSettings.includeModelCount);
        CFG_READ_INT(
            "vehicle_vis_exclude_count",
            s_VehicleVisualSettings.excludeModelCount);

        CFG_READ_BOOL("object_vis_enabled", s_ObjectVisualSettings.enabled);
        CFG_READ_BOOL("object_vis_box", s_ObjectVisualSettings.box);
        CFG_READ_BOOL("object_vis_name", s_ObjectVisualSettings.name);
        CFG_READ_BOOL("object_vis_distance", s_ObjectVisualSettings.distance);
        CFG_READ_BOOL("object_vis_remote_id", s_ObjectVisualSettings.remoteId);
        CFG_READ_BOOL("object_vis_objects", s_ObjectVisualSettings.showObjects);
        CFG_READ_BOOL("object_vis_pickups", s_ObjectVisualSettings.showPickups);
        CFG_READ_BOOL(
            "object_vis_snapline",
            s_ObjectVisualSettings.snapLine);
        CFG_READ_INT(
            "object_vis_snapline_origin",
            s_ObjectVisualSettings.snapLineOrigin);
        CFG_READ_FLOAT("object_vis_range", s_ObjectVisualSettings.maxDistance);
        CFG_READ_INT(
            "object_vis_include_count",
            s_ObjectVisualSettings.includeModelCount);
        CFG_READ_INT(
            "object_vis_exclude_count",
            s_ObjectVisualSettings.excludeModelCount);

        for (int c = 0; c < 4; ++c)
        {
            char key[64]{};
            sprintf_s(key, "radar_enemy_color%d", c);
            CFG_READ_FLOAT(key, s_RadarSettings.enemyColor[c]);
            sprintf_s(key, "radar_friend_color%d", c);
            CFG_READ_FLOAT(key, s_RadarSettings.friendColor[c]);
            sprintf_s(key, "radar_npc_color%d", c);
            CFG_READ_FLOAT(key, s_RadarSettings.npcColor[c]);
            sprintf_s(key, "radar_vehicle_color%d", c);
            CFG_READ_FLOAT(key, s_RadarSettings.vehicleColor[c]);
            sprintf_s(key, "vehicle_vis_box_color%d", c);
            CFG_READ_FLOAT(key, s_VehicleVisualSettings.boxColor[c]);
            sprintf_s(key, "vehicle_vis_text_color%d", c);
            CFG_READ_FLOAT(key, s_VehicleVisualSettings.textColor[c]);
            sprintf_s(key, "object_vis_object_color%d", c);
            CFG_READ_FLOAT(key, s_ObjectVisualSettings.objectColor[c]);
            sprintf_s(key, "object_vis_pickup_color%d", c);
            CFG_READ_FLOAT(key, s_ObjectVisualSettings.pickupColor[c]);
            sprintf_s(key, "object_vis_text_color%d", c);
            CFG_READ_FLOAT(key, s_ObjectVisualSettings.textColor[c]);
            sprintf_s(key, "vehicle_vis_snapline_color%d", c);
            CFG_READ_FLOAT(key, s_VehicleVisualSettings.snapLineColor[c]);
            sprintf_s(key, "object_vis_snapline_color%d", c);
            CFG_READ_FLOAT(key, s_ObjectVisualSettings.snapLineColor[c]);
        }
        for (int i = 0; i < kMaxVisualModelFilters; ++i)
        {
            char key[64]{};
            sprintf_s(key, "vehicle_vis_include_%d", i);
            if (const char* value = FindConfigValue(content, key))
                s_VehicleVisualSettings.includeModels[i] =
                    static_cast<std::uint32_t>(
                        std::strtoul(value, nullptr, 10));
            sprintf_s(key, "vehicle_vis_exclude_%d", i);
            if (const char* value = FindConfigValue(content, key))
                s_VehicleVisualSettings.excludeModels[i] =
                    static_cast<std::uint32_t>(
                        std::strtoul(value, nullptr, 10));
            sprintf_s(key, "object_vis_include_%d", i);
            if (const char* value = FindConfigValue(content, key))
                s_ObjectVisualSettings.includeModels[i] =
                    static_cast<std::uint32_t>(
                        std::strtoul(value, nullptr, 10));
            sprintf_s(key, "object_vis_exclude_%d", i);
            if (const char* value = FindConfigValue(content, key))
                s_ObjectVisualSettings.excludeModels[i] =
                    static_cast<std::uint32_t>(
                        std::strtoul(value, nullptr, 10));
        }

        s_VehicleVisualSettings.includeModelCount = ImClamp(
            s_VehicleVisualSettings.includeModelCount,
            0,
            kMaxVisualModelFilters);
        s_VehicleVisualSettings.excludeModelCount = ImClamp(
            s_VehicleVisualSettings.excludeModelCount,
            0,
            kMaxVisualModelFilters);
        s_ObjectVisualSettings.includeModelCount = ImClamp(
            s_ObjectVisualSettings.includeModelCount,
            0,
            kMaxVisualModelFilters);
        s_ObjectVisualSettings.excludeModelCount = ImClamp(
            s_ObjectVisualSettings.excludeModelCount,
            0,
            kMaxVisualModelFilters);
        s_RadarSettings.range =
            ImClamp(s_RadarSettings.range, 25.f, 800.f);
        s_RadarSettings.backgroundOpacity =
            ImClamp(s_RadarSettings.backgroundOpacity, 0.15f, 0.95f);
        s_RadarSettings.markerSize =
            ImClamp(s_RadarSettings.markerSize, 2.f, 9.f);
        s_RadarSettings.directionLength =
            ImClamp(s_RadarSettings.directionLength, 4.f, 22.f);
        s_VehicleVisualSettings.maxDistance =
            ImClamp(s_VehicleVisualSettings.maxDistance, 25.f, 1200.f);
        s_ObjectVisualSettings.maxDistance =
            ImClamp(s_ObjectVisualSettings.maxDistance, 10.f, 600.f);
        {
            auto& mesh = MeshSilhouette::GetSettings();
            CFG_READ_BOOL("mesh_chams_enabled", mesh.enabled);
            CFG_READ_BOOL("mesh_chams_fill", mesh.fill);
            CFG_READ_BOOL("mesh_chams_edge", mesh.edge);
            CFG_READ_FLOAT("mesh_chams_opacity", mesh.opacity);
            CFG_READ_FLOAT("mesh_chams_edge_width", mesh.edgeWidth);
            CFG_READ_FLOAT("mesh_chams_softness", mesh.softness);
            for (int c = 0; c < 4; ++c)
            {
                char key[64]{};
                sprintf_s(key, "mesh_chams_visible_color%d", c);
                CFG_READ_FLOAT(key, (&mesh.visibleColor.r)[c]);
                sprintf_s(key, "mesh_chams_occluded_color%d", c);
                CFG_READ_FLOAT(key, (&mesh.occludedColor.r)[c]);
            }
            mesh.opacity = ImClamp(mesh.opacity, 0.05f, 1.f);
            mesh.edgeWidth = ImClamp(mesh.edgeWidth, 0.5f, 8.f);
            mesh.softness = ImClamp(mesh.softness, 0.f, 4.f);
        }
#undef CFG_READ_FLOAT
#undef CFG_READ_INT
#undef CFG_READ_BOOL
        MiscFeatures::Import(content);
        Friends::Import(content);
        FeatureBinds::Import(content);
    }

    void TickConfigBootstrap() noexcept
    {
        static bool s_Bootstrapped = false;
        if (s_Bootstrapped)
            return;
        s_Bootstrapped = true;
        EnsureLocalConfigsLoaded();
        const char* payload = LocalConfigSlot(g_DefaultConfigSlot);
        if (payload && payload[0])
            ImportCurrentConfig(payload);
    }
}
