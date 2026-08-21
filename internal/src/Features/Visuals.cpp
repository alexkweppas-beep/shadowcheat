#include "Visuals.h"
#include "Aimbot.h"
#include "EspSkeleton.h"
#include "FiveRpMetadataResolver.h"
#include "Friends.h"
#include "MinimapAnchor.h"
#include "QuantTools.h"
#include "UkraineTools.h"
#include "RagePlayerNames.h"
#include "RageWorldEntities.h"
#include "RuntimeProfiler.h"
#include "VisualPolicy.h"
#include "WeaponNames.h"
#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"
#include "../SDK/Game.h"
#include "../SDK/Offsets.h"
#include "../SDK/RageBuildFlavor.h"
#include "../Menu/EspDrawHelpers.h"
#include "../Menu/EspProjectedOverlay.h"
#include "../Menu/FactionColors.h"
#include "../Menu/Menu.h"
#include "../Main/Hooks.h"
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <Windows.h>

namespace Menu {
    extern float s_Aim_FOV;
    extern float s_Vec_FOV;
    extern bool s_Aim_DrawFov;
    extern bool s_Vec_DrawFov;
    extern bool s_Aim_MarkTarget;
    extern float s_Aim_FovColor[3];
    extern float s_Vec_FovColor[3];
    extern float s_Aim_MarkColor[3];
}

namespace Features {

    // SEH guard flag (defined here, toggled false by hooks on crash).
    bool g_EspEnabled = true;

    // Periodic diagnostic logging for Viewport matrix dump
    static int s_EspDiagCounter = 0;
    static bool s_LogThisFrame = false;

    struct SmoothedVisual final
    {
        std::uint32_t handle{};
        const CPed* ped{};
        std::uint32_t boneMask{};
        Vector3 bones[Game::kVisualBoneCount]{};
        Vector3 targets[Game::kVisualBoneCount]{};
        Vector3 velocity[Game::kVisualBoneCount]{};
        ULONGLONG targetUpdated{};
        ULONGLONG lastSeen{};
        bool initialized{};
    };

    SmoothedVisual s_SmoothedVisuals[256]{};

    struct SmoothedPosition final
    {
        const CPed* ped{};
        Vector3 current{};
        Vector3 target{};
        Vector3 velocity{};
        ULONGLONG targetUpdated{};
        ULONGLONG lastSeen{};
        bool initialized{};
    };

    SmoothedPosition s_SmoothedPositions[256]{};

    struct SmoothedBox final
    {
        const CPed* ped{};
        ImVec2 min{};
        ImVec2 max{};
        bool initialized{};
        ULONGLONG lastSeen{};
    };

    SmoothedBox s_SmoothedBoxes[256]{};

    struct WorldBoxProjection final
    {
        ImVec2 points[8]{};
        bool valid[8]{};
        ImVec2 minimum{};
        ImVec2 maximum{};
        int validCount{};
    };

    bool ModelFilterContains(
        const std::uint32_t* values,
        int count,
        std::uint32_t model) noexcept
    {
        if (!values || count <= 0)
            return false;
        if (count > Menu::kMaxVisualModelFilters)
            count = Menu::kMaxVisualModelFilters;
        for (int index = 0; index < count; ++index)
        {
            if (values[index] == model)
                return true;
        }
        return false;
    }

    bool PassesModelFilter(
        std::uint32_t model,
        const std::uint32_t* include,
        int includeCount,
        const std::uint32_t* exclude,
        int excludeCount) noexcept
    {
        if (ModelFilterContains(exclude, excludeCount, model))
            return false;
        return includeCount <= 0 ||
            ModelFilterContains(include, includeCount, model);
    }

    bool VehicleClassEnabled(
        RageWorldEntities::VehicleClass vehicleClass,
        const Menu::VehicleVisualSettings& settings) noexcept
    {
        switch (vehicleClass)
        {
        case RageWorldEntities::VehicleClass::Car:
            return settings.showCars;
        case RageWorldEntities::VehicleClass::Bike:
            return settings.showBikes;
        case RageWorldEntities::VehicleClass::Boat:
            return settings.showBoats;
        case RageWorldEntities::VehicleClass::Aircraft:
            return settings.showAircraft;
        case RageWorldEntities::VehicleClass::Other:
        default:
            return settings.showOther;
        }
    }

    bool ProjectWorldEntityBox(
        const RageWorldEntities::Entry& entry,
        WorldBoxProjection& output) noexcept
    {
        output = {};
        const float hx = ImClamp(entry.halfExtentX, 0.05f, 100.f);
        const float hy = ImClamp(entry.halfExtentY, 0.05f, 100.f);
        const float hz = ImClamp(entry.halfExtentZ, 0.05f, 100.f);
        const float yaw =
            entry.heading * 0.01745329251994329577f;
        const float rightX = cosf(yaw);
        const float rightY = sinf(yaw);
        const float forwardX = -sinf(yaw);
        const float forwardY = cosf(yaw);

        output.minimum = ImVec2(FLT_MAX, FLT_MAX);
        output.maximum = ImVec2(-FLT_MAX, -FLT_MAX);
        for (int corner = 0; corner < 8; ++corner)
        {
            const float localX = (corner & 1) ? hx : -hx;
            const float localY = (corner & 2) ? hy : -hy;
            const float localZ = (corner & 4) ? hz : -hz;
            const Vector3 world{
                entry.x + rightX * localX + forwardX * localY,
                entry.y + rightY * localX + forwardY * localY,
                entry.z + localZ
            };
            const auto screen = Game::WorldToScreen(world);
            if (!screen)
                continue;
            output.valid[corner] = true;
            output.points[corner] =
                ImVec2(screen->x, screen->y);
            output.minimum.x = ImMin(
                output.minimum.x,
                output.points[corner].x);
            output.minimum.y = ImMin(
                output.minimum.y,
                output.points[corner].y);
            output.maximum.x = ImMax(
                output.maximum.x,
                output.points[corner].x);
            output.maximum.y = ImMax(
                output.maximum.y,
                output.points[corner].y);
            ++output.validCount;
        }
        return output.validCount >= 4 &&
            output.maximum.x - output.minimum.x >= 2.f &&
            output.maximum.y - output.minimum.y >= 2.f;
    }

    ImU32 WorldColor(const float color[4]) noexcept
    {
        return IM_COL32(
            static_cast<int>(ImSaturate(color[0]) * 255.f),
            static_cast<int>(ImSaturate(color[1]) * 255.f),
            static_cast<int>(ImSaturate(color[2]) * 255.f),
            static_cast<int>(ImSaturate(color[3]) * 255.f));
    }

    void DrawProjectedWorldBox(
        ImDrawList* draw,
        const WorldBoxProjection& box,
        ImU32 color) noexcept
    {
        if (!draw)
            return;
        static constexpr int edges[][2] = {
            {0, 1}, {0, 2}, {0, 4},
            {1, 3}, {1, 5},
            {2, 3}, {2, 6},
            {3, 7},
            {4, 5}, {4, 6},
            {5, 7}, {6, 7}
        };
        for (const auto& edge : edges)
        {
            if (!box.valid[edge[0]] || !box.valid[edge[1]])
                continue;
            draw->AddLine(
                box.points[edge[0]],
                box.points[edge[1]],
                IM_COL32(0, 0, 0, 180),
                3.f);
            draw->AddLine(
                box.points[edge[0]],
                box.points[edge[1]],
                color,
                1.35f);
        }
    }

    const char* VehicleClassLabel(
        RageWorldEntities::VehicleClass vehicleClass) noexcept
    {
        switch (vehicleClass)
        {
        case RageWorldEntities::VehicleClass::Car:
            return "Vehicle";
        case RageWorldEntities::VehicleClass::Bike:
            return "Bike";
        case RageWorldEntities::VehicleClass::Boat:
            return "Boat";
        case RageWorldEntities::VehicleClass::Aircraft:
            return "Aircraft";
        case RageWorldEntities::VehicleClass::Other:
        default:
            return "Vehicle";
        }
    }

    void DrawWorldEntityLabel(
        ImDrawList* draw,
        const WorldBoxProjection& box,
        ImU32 color,
        const char* text,
        int line) noexcept
    {
        if (!draw || !text || !text[0])
            return;
        ImFont* font = ImGui::GetFont();
        const float fontSize = ImGui::GetFontSize();
        const ImVec2 size =
            font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, text);
        EspDrawOutlinedText(
            draw,
            font,
            fontSize,
            ImVec2(
                (box.minimum.x + box.maximum.x - size.x) * 0.5f,
                box.maximum.y + 4.f + line * (fontSize + 1.f)),
            color,
            text,
            1.f);
    }

    void RenderWorldEntityEsp(
        ImDrawList* draw,
        const RageWorldEntities::Entry* entries,
        int count,
        const Vector3& localPosition,
        bool localPositionValid) noexcept
    {
        if (!draw || !entries || count <= 0 || !localPositionValid)
            return;

        const auto& vehicles = Menu::s_VehicleVisualSettings;
        const auto& objects = Menu::s_ObjectVisualSettings;
        for (int index = 0; index < count; ++index)
        {
            const RageWorldEntities::Entry& entry = entries[index];
            if (!entry.valid)
                continue;
            const float dx = entry.x - localPosition.x;
            const float dy = entry.y - localPosition.y;
            const float dz = entry.z - localPosition.z;
            const float distance =
                sqrtf(dx * dx + dy * dy + dz * dz);

            bool drawBox = false;
            bool drawName = false;
            bool drawDistance = false;
            bool drawRemoteId = false;
            bool drawHealth = false;
            const float* boxColor = nullptr;
            const float* textColor = nullptr;

            if (entry.kind ==
                RageWorldEntities::EntityKind::Vehicle)
            {
                if (!vehicles.enabled ||
                    distance > vehicles.maxDistance ||
                    (entry.occupied && !vehicles.showOccupied) ||
                    (!entry.occupied && !vehicles.showEmpty) ||
                    !VehicleClassEnabled(
                        entry.vehicleClass,
                        vehicles) ||
                    !PassesModelFilter(
                        entry.modelHash,
                        vehicles.includeModels,
                        vehicles.includeModelCount,
                        vehicles.excludeModels,
                        vehicles.excludeModelCount))
                    continue;
                drawBox = vehicles.box;
                drawName = vehicles.name;
                drawDistance = vehicles.distance;
                drawRemoteId = vehicles.remoteId;
                drawHealth = vehicles.health;
                boxColor = vehicles.boxColor;
                textColor = vehicles.textColor;
            }
            else
            {
                const bool pickup =
                    entry.kind ==
                    RageWorldEntities::EntityKind::Pickup;
                if (!objects.enabled ||
                    distance > objects.maxDistance ||
                    (pickup && !objects.showPickups) ||
                    (!pickup && !objects.showObjects) ||
                    !PassesModelFilter(
                        entry.modelHash,
                        objects.includeModels,
                        objects.includeModelCount,
                        objects.excludeModels,
                        objects.excludeModelCount))
                    continue;
                drawBox = objects.box;
                drawName = objects.name;
                drawDistance = objects.distance;
                drawRemoteId = objects.remoteId;
                boxColor = pickup
                    ? objects.pickupColor
                    : objects.objectColor;
                textColor = objects.textColor;
            }

            WorldBoxProjection projection{};
            if (!ProjectWorldEntityBox(entry, projection))
                continue;
            const ImU32 boxCol = WorldColor(boxColor);
            const ImU32 textCol = WorldColor(textColor);
            if (drawBox)
                DrawProjectedWorldBox(draw, projection, boxCol);

            int line = 0;
            char label[96]{};
            if (drawName)
            {
                if (entry.kind ==
                    RageWorldEntities::EntityKind::Vehicle)
                {
                    _snprintf(
                        label,
                        sizeof(label) - 1,
                        "%s 0x%08X",
                        VehicleClassLabel(entry.vehicleClass),
                        static_cast<unsigned>(entry.modelHash));
                }
                else
                {
                    _snprintf(
                        label,
                        sizeof(label) - 1,
                        "%s 0x%08X",
                        entry.kind ==
                                RageWorldEntities::EntityKind::Pickup
                            ? "Pickup"
                            : "Object",
                        static_cast<unsigned>(entry.modelHash));
                }
                DrawWorldEntityLabel(
                    draw,
                    projection,
                    textCol,
                    label,
                    line++);
            }
            if (drawRemoteId &&
                entry.remoteId !=
                    RageWorldEntities::kInvalidRemoteId)
            {
                _snprintf(
                    label,
                    sizeof(label) - 1,
                    "ID: %u",
                    static_cast<unsigned>(entry.remoteId));
                DrawWorldEntityLabel(
                    draw,
                    projection,
                    textCol,
                    label,
                    line++);
            }
            if (drawHealth)
            {
                const float health = entry.bodyHealth != 0.f
                    ? entry.bodyHealth
                    : entry.health;
                _snprintf(
                    label,
                    sizeof(label) - 1,
                    "HP: %.0f",
                    health);
                DrawWorldEntityLabel(
                    draw,
                    projection,
                    textCol,
                    label,
                    line++);
            }
            if (drawDistance)
            {
                _snprintf(
                    label,
                    sizeof(label) - 1,
                    "%.0f m",
                    distance);
                DrawWorldEntityLabel(
                    draw,
                    projection,
                    textCol,
                    label,
                    line);
            }
        }
    }

    // The box top and bottom come from the rig when bones are available and
    // from fixed offsets around the ped root when they are not, and the two
    // disagreed by about a fifth of the height. So a ped that entered the bone
    // budget — which is what happens when the skeleton is switched on, or when
    // the aim pulls a target to screen centre — visibly resized. Measuring the
    // real extent while the rig is visible keeps both paths the same size.
    // Distance in world metres from the head bone, which sits inside the
    // skull, up to the crown, and from the foot bone, which sits at the ankle,
    // down to the sole. Ped models are all near human scale, so constants hold;
    // world metres rather than pixels so the correction survives distance.
    constexpr float kHeadBoneToCrown = 0.13f;
    constexpr float kFootBoneToSole = 0.09f;

    struct PedExtent final
    {
        const CPed* ped{};
        float headAbove{};
        float feetBelow{};
        ULONGLONG lastSeen{};
    };

    PedExtent s_PedExtents[96]{};
    // Seeded with what standing peds actually measure, so boxes are the right
    // size from the first frame. Bones are only fetched when something asks for
    // them, so with the skeleton off there is nothing to measure and this seed
    // is what every box uses for the whole session.
    float s_HeadAboveTypical = 0.76f;
    float s_FeetBelowTypical = 1.00f;

    void RememberPedExtent(
        const CPed* ped,
        float headAbove,
        float feetBelow) noexcept
    {
        if (!ped)
            return;
        if (!std::isfinite(headAbove) ||
            !std::isfinite(feetBelow))
            return;
        // Wide on purpose: the ped root sits near the middle on foot but the
        // offsets differ in vehicles and while prone, and clamping to a
        // narrow band would silently reintroduce a wrong height.
        headAbove = ImClamp(headAbove, 0.15f, 2.10f);
        feetBelow = ImClamp(feetBelow, -0.40f, 1.80f);

        const ULONGLONG now = GetTickCount64();
        PedExtent* slot = nullptr;
        PedExtent* oldest = &s_PedExtents[0];
        for (auto& entry : s_PedExtents)
        {
            if (entry.ped == ped)
            {
                slot = &entry;
                break;
            }
            if (!entry.ped)
            {
                slot = &entry;
                break;
            }
            if (entry.lastSeen < oldest->lastSeen)
                oldest = &entry;
        }
        if (!slot)
            slot = oldest;
        slot->ped = ped;
        slot->headAbove = headAbove;
        slot->feetBelow = feetBelow;
        slot->lastSeen = now;

        // Only upright poses feed the shared default: mixing in crouched and
        // seated extents, which measure around 0.65, would drag it towards a
        // height nobody has.
        if (headAbove > 0.70f)
        {
            s_HeadAboveTypical +=
                (headAbove - s_HeadAboveTypical) * 0.02f;
            s_FeetBelowTypical +=
                (feetBelow - s_FeetBelowTypical) * 0.02f;
        }
    }

    void LookupPedExtent(
        const CPed* ped,
        float& headAbove,
        float& feetBelow) noexcept
    {
        headAbove = s_HeadAboveTypical;
        feetBelow = s_FeetBelowTypical;
        if (!ped)
            return;
        for (const auto& entry : s_PedExtents)
        {
            if (entry.ped == ped)
            {
                headAbove = entry.headAbove;
                feetBelow = entry.feetBelow;
                return;
            }
        }
    }

    // Aim target marker: one glowing dot that breathes. A ring or a reticle
    // covers the part of the ped you are aiming at, which is the one place you
    // are looking; a dot marks the spot and stays out of the way. The pulse is
    // what makes it readable against a moving background.
    void DrawAimTargetReticle(
        ImDrawList* draw,
        const Features::AimTargetSnapshot& mark,
        const float color[3]) noexcept
    {
        if (!draw)
            return;

        const ImVec2 at(mark.screen.x, mark.screen.y);

        // Sized from a fixed world height projected at the target, so the dot
        // shrinks with distance like the ped instead of sitting at a constant
        // pixel size that is a speck up close and a blob far away.
        float scale = 1.f;
        {
            Vector3 above = mark.world;
            above.z += 0.30f;
            const auto atScreen = Game::WorldToScreen(mark.world);
            const auto aboveScreen = Game::WorldToScreen(above);
            if (atScreen && aboveScreen)
            {
                const float span =
                    fabsf(aboveScreen->y - atScreen->y);
                if (std::isfinite(span))
                    scale = ImClamp(span / 22.f, 0.55f, 2.2f);
            }
        }

        // A sine, not a sawtooth: the brightness has to arrive and leave at the
        // same rate or the pulse reads as a flicker.
        static float s_Phase = 0.f;
        s_Phase += ImClamp(ImGui::GetIO().DeltaTime, 0.f, 0.05f) * 3.4f;
        if (s_Phase > 6.28318f)
            s_Phase -= 6.28318f;
        const float pulse = 0.5f + 0.5f * sinf(s_Phase);

        const float dim = mark.visible ? 1.f : 0.5f;
        const auto rgba =
            [&](float tint, float opacity) noexcept {
                return IM_COL32(
                    (int)(ImClamp(color[0] * tint, 0.f, 1.f) * 255.f),
                    (int)(ImClamp(color[1] * tint, 0.f, 1.f) * 255.f),
                    (int)(ImClamp(color[2] * tint, 0.f, 1.f) * 255.f),
                    (int)(ImClamp(opacity * dim, 0.f, 1.f) * 255.f));
            };

        const float core = 2.3f * scale;
        // Three widening passes at low alpha: cheap, and it reads as a glow
        // because the falloff is what the eye picks up, not the blur itself.
        const float halo = core * (3.4f + 1.1f * pulse);
        draw->AddCircleFilled(
            at, halo, rgba(1.f, 0.10f + 0.10f * pulse), 20);
        draw->AddCircleFilled(
            at, halo * 0.62f, rgba(1.f, 0.16f + 0.16f * pulse), 16);
        draw->AddCircleFilled(
            at, core, rgba(1.f, 0.75f + 0.25f * pulse), 14);
        // The centre stays near white so the dot keeps its position when the
        // glow around it is at its dimmest.
        draw->AddCircleFilled(
            at, core * 0.45f, rgba(1.8f, 0.85f + 0.15f * pulse), 10);
    }

    void SmoothScreenBox(
        const CPed* ped,
        ImVec2& boxMin,
        ImVec2& boxMax) noexcept
    {
        if (!ped)
            return;
        const ULONGLONG now = GetTickCount64();
        SmoothedBox* slot = nullptr;
        SmoothedBox* oldest = &s_SmoothedBoxes[0];
        for (auto& entry : s_SmoothedBoxes)
        {
            if (entry.initialized && entry.ped == ped)
            {
                slot = &entry;
                break;
            }
            if (!entry.initialized)
            {
                slot = &entry;
                break;
            }
            if (entry.lastSeen < oldest->lastSeen)
                oldest = &entry;
        }
        if (!slot)
            slot = oldest;

        const float rawW = boxMax.x - boxMin.x;
        const float rawH = boxMax.y - boxMin.y;
        if (rawW < 4.f || rawH < 8.f)
            return;

        if (!slot->initialized || slot->ped != ped)
        {
            slot->ped = ped;
            slot->min = boxMin;
            slot->max = boxMax;
            slot->initialized = true;
            slot->lastSeen = now;
            return;
        }

        // Fast catch-up on large jumps (teleport / first frames),
        // otherwise damp accordion jitter while sprinting.
        const float cx =
            (boxMin.x + boxMax.x) * 0.5f;
        const float prevCx =
            (slot->min.x + slot->max.x) * 0.5f;
        const float jump =
            fabsf(cx - prevCx) +
            fabsf(boxMin.y - slot->min.y) +
            fabsf(boxMax.y - slot->max.y);
        const float blend = jump > 120.f ? 0.85f : 0.28f;
        slot->min.x += (boxMin.x - slot->min.x) * blend;
        slot->min.y += (boxMin.y - slot->min.y) * blend;
        slot->max.x += (boxMax.x - slot->max.x) * blend;
        slot->max.y += (boxMax.y - slot->max.y) * blend;
        slot->lastSeen = now;
        boxMin = slot->min;
        boxMax = slot->max;
    }

    // crown and sole are the projected top of the skull and bottom of the
    // shoes. They are needed because the head bone sits in the middle of the
    // head and the foot bone at the ankle, so a box drawn straight onto those
    // bones cuts through both ends of the ped. Null when the rig is missing.
    void BuildStableEspBox(
        const ImVec2* bonePts,
        const bool* boneOk,
        const ImVec2& screenHead,
        const ImVec2& screenFeet,
        const ImVec2* crown,
        const ImVec2* sole,
        ImVec2& boxMin,
        ImVec2& boxMax) noexcept
    {
        using BS = RagePlayerNames::BoneSlot;
        const int head = static_cast<int>(BS::Head);
        const int neck = static_cast<int>(BS::Neck);
        const int spine = static_cast<int>(BS::Spine);
        const int pelvis = static_cast<int>(BS::Pelvis);
        const int lFoot = static_cast<int>(BS::LeftFoot);
        const int rFoot = static_cast<int>(BS::RightFoot);
        const int lArm = static_cast<int>(BS::LeftUpperArm);
        const int rArm = static_cast<int>(BS::RightUpperArm);

        float topY = screenHead.y;
        float botY = screenFeet.y;
        float centerX = (screenHead.x + screenFeet.x) * 0.5f;

        if (boneOk && bonePts)
        {
            if (crown)
                topY = crown->y;
            else if (boneOk[head])
                topY = bonePts[head].y;
            else if (boneOk[neck])
                topY = bonePts[neck].y;

            if (sole)
                botY = sole->y;
            else
            {
                float footY = 0.f;
                int footN = 0;
                if (boneOk[lFoot]) { footY += bonePts[lFoot].y; ++footN; }
                if (boneOk[rFoot]) { footY += bonePts[rFoot].y; ++footN; }
                if (footN > 0)
                    botY = footY / static_cast<float>(footN);
            }
            // Without feet the bottom stays on the projected estimate: the old
            // "thigh plus 28 pixels" guess was a screen-space constant, so it
            // was roughly right at a few metres and wildly wrong further out.

            if (boneOk[pelvis])
                centerX = bonePts[pelvis].x;
            else if (boneOk[spine])
                centerX = bonePts[spine].x;
            else if (boneOk[neck])
                centerX = bonePts[neck].x;
        }

        if (botY < topY)
        {
            const float tmp = botY;
            botY = topY;
            topY = tmp;
        }

        // Vertical pad proportional to box height so skull/feet are covered closely without huge margins.
        {
            const float h = ImMax(botY - topY, 12.f);
            topY -= h * 0.03f;
            botY += h * 0.03f;
        }

        float height = botY - topY;
        if (height < 12.f)
            height = 12.f;

        // Width from torso aspect — sleek and proportional.
        float width = height * 0.36f;
        if (boneOk && bonePts && boneOk[lArm] && boneOk[rArm])
        {
            const float shoulder =
                fabsf(bonePts[lArm].x - bonePts[rArm].x) + 6.f;
            // Only widen if shoulders are clearly wider; never shrink below aspect.
            if (shoulder > width)
                width = width + (shoulder - width) * 0.35f;
        }
        if (width < height * 0.28f)
            width = height * 0.28f;
        if (width > height * 0.48f)
            width = height * 0.48f;

        boxMin = ImVec2(centerX - width * 0.5f, topY);
        boxMax = ImVec2(centerX + width * 0.5f, botY);
    }

    Vector3 SmoothWorldPosition(
        const CPed* ped,
        const Vector3& input)
    {
        const ULONGLONG now = GetTickCount64();
        SmoothedPosition* slot = nullptr;
        SmoothedPosition* oldest =
            &s_SmoothedPositions[0];
        for (auto& entry : s_SmoothedPositions)
        {
            if (entry.initialized && entry.ped == ped)
            {
                slot = &entry;
                break;
            }
            if (!entry.initialized)
            {
                slot = &entry;
                break;
            }
            if (entry.lastSeen < oldest->lastSeen)
                oldest = &entry;
        }
        if (!slot)
            slot = oldest;
        if (!slot->initialized ||
            slot->ped != ped ||
            now - slot->lastSeen > 2000)
        {
            *slot = {};
            slot->initialized = true;
            slot->ped = ped;
            slot->current = input;
            slot->target = input;
            slot->targetUpdated = now;
        }
        else
        {
            const float tx = input.x - slot->target.x;
            const float ty = input.y - slot->target.y;
            const float tz = input.z - slot->target.z;
            const float targetDelta =
                tx * tx + ty * ty + tz * tz;
            if (targetDelta > 0.000001f)
            {
                float sampleDt =
                    static_cast<float>(
                        now - slot->targetUpdated) /
                    1000.f;
                if (sampleDt < 0.016f)
                    sampleDt = 0.016f;
                if (sampleDt > 0.25f)
                    sampleDt = 0.25f;
                if (targetDelta > 100.f)
                {
                    slot->current = input;
                    slot->velocity = {};
                }
                else
                {
                    slot->velocity = {
                        tx / sampleDt,
                        ty / sampleDt,
                        tz / sampleDt
                    };
                }
                slot->target = input;
                slot->targetUpdated = now;
            }
            float lead =
                static_cast<float>(
                    now - slot->targetUpdated) /
                1000.f;
            if (lead > 0.08f)
                lead = 0.08f;
            const Vector3 predicted{
                slot->target.x + slot->velocity.x * lead,
                slot->target.y + slot->velocity.y * lead,
                slot->target.z + slot->velocity.z * lead
            };
            float dt = ImGui::GetIO().DeltaTime;
            if (dt <= 0.f || dt > 0.1f)
                dt = 1.f / 60.f;
            const float blend =
                1.f - std::exp(-24.f * dt);
            slot->current.x +=
                (predicted.x - slot->current.x) * blend;
            slot->current.y +=
                (predicted.y - slot->current.y) * blend;
            slot->current.z +=
                (predicted.z - slot->current.z) * blend;
        }
        slot->lastSeen = now;
        return slot->current;
    }

    void SmoothVisualData(
        const CPed* ped,
        Game::PedVisualData& visual)
    {
        if (!ped || !visual.valid ||
            !visual.rageHandle ||
            !visual.boneMask)
            return;
        const ULONGLONG now = GetTickCount64();
        SmoothedVisual* slot = nullptr;
        SmoothedVisual* oldest =
            &s_SmoothedVisuals[0];
        for (auto& entry : s_SmoothedVisuals)
        {
            if (entry.initialized &&
                entry.handle == visual.rageHandle &&
                entry.ped == ped)
            {
                slot = &entry;
                break;
            }
            if (!entry.initialized)
            {
                slot = &entry;
                break;
            }
            if (entry.lastSeen < oldest->lastSeen)
                oldest = &entry;
        }
        if (!slot)
            slot = oldest;

        if (!slot->initialized ||
            slot->handle != visual.rageHandle ||
            slot->ped != ped ||
            now - slot->lastSeen > 2000)
        {
            *slot = {};
            slot->initialized = true;
            slot->handle = visual.rageHandle;
            slot->ped = ped;
            slot->boneMask = visual.boneMask;
            for (int bone = 0;
                 bone < Game::kVisualBoneCount;
                 ++bone)
            {
                slot->bones[bone] = visual.bones[bone];
                slot->targets[bone] = visual.bones[bone];
            }
            slot->targetUpdated = now;
        }
        else
        {
            float dt = ImGui::GetIO().DeltaTime;
            if (dt <= 0.f || dt > 0.1f)
                dt = 1.f / 60.f;
            const float blend =
                1.f - std::exp(-28.f * dt);
            float sampleDt =
                static_cast<float>(
                    now - slot->targetUpdated) /
                1000.f;
            if (sampleDt < 0.03f)
                sampleDt = 0.03f;
            if (sampleDt > 0.25f)
                sampleDt = 0.25f;
            bool targetChanged = false;
            for (int bone = 0;
                 bone < Game::kVisualBoneCount;
                 ++bone)
            {
                const std::uint32_t bit = 1u << bone;
                if (!(visual.boneMask & bit))
                    continue;
                if (!(slot->boneMask & bit))
                {
                    slot->bones[bone] =
                        visual.bones[bone];
                    slot->targets[bone] =
                        visual.bones[bone];
                    slot->velocity[bone] = {};
                    targetChanged = true;
                    continue;
                }
                const float targetDx =
                    visual.bones[bone].x -
                    slot->targets[bone].x;
                const float targetDy =
                    visual.bones[bone].y -
                    slot->targets[bone].y;
                const float targetDz =
                    visual.bones[bone].z -
                    slot->targets[bone].z;
                const float targetDeltaSquared =
                    targetDx * targetDx +
                    targetDy * targetDy +
                    targetDz * targetDz;
                if (targetDeltaSquared > 0.000001f)
                {
                    if (targetDeltaSquared > 25.f)
                    {
                        slot->bones[bone] =
                            visual.bones[bone];
                        slot->velocity[bone] = {};
                    }
                    else
                    {
                        slot->velocity[bone] = {
                            targetDx / sampleDt,
                            targetDy / sampleDt,
                            targetDz / sampleDt
                        };
                    }
                    slot->targets[bone] =
                        visual.bones[bone];
                    targetChanged = true;
                }
            }
            if (targetChanged)
                slot->targetUpdated = now;

            float lead =
                static_cast<float>(
                    now - slot->targetUpdated) /
                1000.f;
            if (lead < 0.f)
                lead = 0.f;
            if (lead > 0.12f)
                lead = 0.12f;
            for (int bone = 0;
                 bone < Game::kVisualBoneCount;
                 ++bone)
            {
                const std::uint32_t bit = 1u << bone;
                if (!(visual.boneMask & bit))
                    continue;
                const Vector3 predicted{
                    slot->targets[bone].x +
                        slot->velocity[bone].x * lead,
                    slot->targets[bone].y +
                        slot->velocity[bone].y * lead,
                    slot->targets[bone].z +
                        slot->velocity[bone].z * lead
                };
                const float dx =
                    predicted.x - slot->bones[bone].x;
                const float dy =
                    predicted.y - slot->bones[bone].y;
                const float dz =
                    predicted.z - slot->bones[bone].z;
                if (dx * dx + dy * dy + dz * dz >
                    25.f)
                {
                    slot->bones[bone] =
                        predicted;
                    continue;
                }
                slot->bones[bone].x += dx * blend;
                slot->bones[bone].y += dy * blend;
                slot->bones[bone].z += dz * blend;
            }
            slot->boneMask = visual.boneMask;
        }
        slot->lastSeen = now;
        for (int bone = 0;
             bone < Game::kVisualBoneCount;
             ++bone)
        {
            if (visual.boneMask & (1u << bone))
                visual.bones[bone] =
                    slot->bones[bone];
        }
    }

    // Helper: format float as raw hex (wsprintfA doesn't support %f)
    static void LogFloat(const char* label, float val)
    {
        char buf[128];
        int* p = (int*)&val;
        wsprintfA(buf, "[ESP-DIAG] %s = raw=0x%08X int=%d", label, *p, *p);
        hooks::HLog(buf);
    }

    void RenderESP(ImDrawList* drawList)
    {
        // ESP-DIAG was left on for viewport debugging and floods cheat_log
        // every ~5s. Keep off unless actively diagnosing a matrix crash.
        constexpr bool kEspDiagLog = false;
        s_LogThisFrame = kEspDiagLog && (++s_EspDiagCounter >= 300);
        if (s_LogThisFrame) s_EspDiagCounter = 0;

        // --- Safety: skip ESP entirely while game is in loading/menu state.
        // Camera position == (0,0,0) means viewport is identity (not in world).
        // In this state replay interface returns garbage peds that AV on read.
        if (Offsets::ViewportPtr && IsValidPtr(Offsets::ViewportPtr)) {
            uint8_t* vp = *Offsets::ViewportPtr;
            if (IsValidPtr(vp)) {
                const float* V = (const float*)(vp + Offsets::Viewport::ViewMatrix);
                if (V[12] == 0.0f && V[13] == 0.0f && V[14] == 0.0f) {
                    return; // loading/menu — do not touch ped memory
                }
            } else {
                return;
            }
        } else {
            return;
        }

        // --- Periodic diagnostic: dump Viewport matrices as raw hex ---
        // wsprintfA doesn't support %f or %llu, so we log float bits as hex
        if (s_LogThisFrame) {
            char buf[256];
            if (Offsets::ViewportPtr && IsValidPtr(Offsets::ViewportPtr)) {
                uint8_t* vp = *Offsets::ViewportPtr;
                if (IsValidPtr(vp)) {
                    const float* V = (const float*)(vp + Offsets::Viewport::ViewMatrix);
                    const float* P = (const float*)(vp + Offsets::Viewport::ProjectionMatrix);

                    // Log View matrix translation (camera pos) as raw bits
                    int* vx = (int*)&V[12];
                    int* vy = (int*)&V[13];
                    int* vz = (int*)&V[14];
                    int* p0 = (int*)&P[0];
                    int* p5 = (int*)&P[5];
                    int* p10 = (int*)&P[10];
                    int* p11 = (int*)&P[11];
                    wsprintfA(buf, "[ESP-DIAG] Vtx=0x%08X Vty=0x%08X Vtz=0x%08X", *vx, *vy, *vz);
                    hooks::HLog(buf);
                    wsprintfA(buf, "[ESP-DIAG] P0=0x%08X P5=0x%08X P10=0x%08X P11=0x%08X", *p0, *p5, *p10, *p11);
                    hooks::HLog(buf);

                    // Also log first row of V to check if it's a rotation
                    int* vr0 = (int*)&V[0];
                    int* vr1 = (int*)&V[1];
                    int* vr2 = (int*)&V[2];
                    wsprintfA(buf, "[ESP-DIAG] Vrow0=0x%08X 0x%08X 0x%08X", *vr0, *vr1, *vr2);
                    hooks::HLog(buf);
                } else {
                    hooks::HLog("[ESP-DIAG] *ViewportPtr is NULL");
                }
            } else {
                hooks::HLog("[ESP-DIAG] ViewportPtr is NULL");
            }
        }

        const ImVec2 center = ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);

        const Menu::AimWeaponProfile& aimProf = Menu::GetActiveAimProfile();
        if (aimProf.silentDrawFov) {
            const ImU32 silentCol = IM_COL32(
                (int)(aimProf.silentFovColor[0] * 255.f),
                (int)(aimProf.silentFovColor[1] * 255.f),
                (int)(aimProf.silentFovColor[2] * 255.f),
                180);
            drawList->AddCircle(
                center,
                aimProf.silentFov,
                silentCol,
                aimProf.silentFov < 120.f ? 32 : 48,
                1.2f);
        }

        if (aimProf.vecDrawFov) {
            const ImU32 vecCol = IM_COL32(
                (int)(aimProf.vecFovColor[0] * 255.f),
                (int)(aimProf.vecFovColor[1] * 255.f),
                (int)(aimProf.vecFovColor[2] * 255.f),
                180);
            drawList->AddCircle(
                center,
                aimProf.vecFov,
                vecCol,
                aimProf.vecFov < 120.f ? 32 : 48,
                1.0f);
        }

        if (aimProf.silentMark) {
            AimTargetSnapshot mark{};
            if (CopyDisplayTarget(mark) && mark.valid) {
                DrawAimTargetReticle(
                    drawList,
                    mark,
                    Menu::s_Aim_MarkColor);
            }
        }

        bool anyProfileEnabled = false;
        for (int index = 0;
             index < static_cast<int>(
                 Menu::EspProfileId::Count);
             ++index)
        {
            if (Menu::s_EspProfiles[index].enabled)
            {
                anyProfileEnabled = true;
                break;
            }
        }
        const bool worldVisualDemand =
            Menu::s_RadarSettings.enabled ||
            Menu::s_VehicleVisualSettings.enabled ||
            Menu::s_ObjectVisualSettings.enabled;
        if (!anyProfileEnabled && !worldVisualDemand)
            return;

        const Game::FramePedContext& frame =
            Game::GetFramePedContext();
        auto* localPed = frame.localPed;
        const Vector3 localPosition =
            frame.localPosition;
        const bool hasLocalPosition =
            frame.localPositionValid;
        static RageWorldEntities::Entry
            worldEntries[RageWorldEntities::kMaxEntries]{};
        int worldEntryCount = 0;
        if (Menu::s_VehicleVisualSettings.enabled ||
            Menu::s_ObjectVisualSettings.enabled ||
            (Menu::s_RadarSettings.enabled &&
             Menu::s_RadarSettings.showVehicles))
        {
            worldEntryCount =
                RageWorldEntities::CopySnapshot(
                    worldEntries,
                    IM_ARRAYSIZE(worldEntries));
        }

        int pedCount = 0;
        int w2sSuccess = 0;
        int nameMiss = 0;
        int nameHit = 0;
        const CPed* samplePedForName = nullptr;
        int profileCounts[
            static_cast<int>(
                Menu::EspProfileId::Count)]{};
        int visualMatches = 0;
        int visibleMatches = 0;
        int hiddenMatches = 0;
        int validSkeletons = 0;
        int skeletonSegments = 0;
        // Flicker metric: how many peds that asked for a skeleton actually got
        // one drawn this frame.
        std::uint32_t skeletonExpected = 0;
        std::uint32_t skeletonDrawn = 0;

        // Only the local player's affiliation is needed here, so the snapshot
        // is queried for that one row instead of copying 128 fat entries with
        // their bones and strings every frame.
        RUNTIME_PROFILE_BEGIN(localSnapTimer);
        const RagePlayerNames::LocalIdentity localIdentity =
            RagePlayerNames::GetLocalIdentity();
        RUNTIME_PROFILE_ACCUM(SnapshotCopy, localSnapTimer);
        const int localFactionId = localIdentity.factionId;
        const int localFamilyId = localIdentity.familyId;
        const char* const localFactionKey =
            localIdentity.factionKey;
        const char* const localFamilyName =
            localIdentity.familyName;
        const bool friendScopeActive =
            Menu::s_EspProfiles[static_cast<int>(
                Menu::EspProfileId::Friends)].enabled &&
            Friends::Count() > 0;

        for (int activeIndex = 0;
             activeIndex < frame.count;
             ++activeIndex) {
            const Game::FramePedEntry& frameEntry =
                frame.entries[activeIndex];
            const CPed* ped = frameEntry.ped;
            if (!frameEntry.valid ||
                (!ped && !frameEntry.synthetic) ||
                (ped && ped == localPed))
                continue;
            Vector3 pos =
                frameEntry.smoothedPosition;
            float hp = frameEntry.health;
            float maxHp = frameEntry.maxHealth;
            bool isPlayer = frameEntry.isPlayer;
            Game::PedVisualData visual =
                frameEntry.visual;
            const bool hasVisual =
                frameEntry.visualValid;
            if (hasVisual && visual.isLocal)
                continue;
            const bool isAnimal =
                frameEntry.isAnimal;
            if (isAnimal)
                isPlayer = false;

            // A player is only called dead when the game itself says so,
            // through the native flag the snapshot reports. The raw health
            // baseline differs between the local ped and streamed remotes,
            // so a low reading proves nothing for players. Ordinary peds do
            // die at zero.
            const bool isDeadOrKnock =
                (hasVisual && (visual.dead || visual.knock)) ||
                (!isPlayer && hp <= 0.0f);

            bool isSameFaction = false;
            bool isSameFamily = false;
            if (hasVisual && isPlayer) {
                if (localFactionId > 0 && visual.factionId == localFactionId)
                    isSameFaction = true;
                if (!isSameFaction &&
                    localFactionKey[0] &&
                    visual.factionKey[0] &&
                    lstrcmpiA(
                        localFactionKey,
                        visual.factionKey) == 0)
                    isSameFaction = true;
                if ((localFamilyId > 0 &&
                     visual.familyId == localFamilyId) ||
                    (localFamilyId <= 0 &&
                     localFamilyName[0] != 0 &&
                     visual.familyName[0] != 0 &&
                     lstrcmpiA(
                         localFamilyName,
                         visual.familyName) == 0))
                    isSameFamily = true;
            }

            // Only asked when the friend scope is switched on and somebody is
            // actually on the list, so the name comparison never runs for
            // players who could not be routed there anyway.
            bool isFriend = false;
            if (friendScopeActive && isPlayer && hasVisual)
            {
                if (visual.nickname[0])
                    isFriend = Friends::IsFriend(visual.nickname);
                if (!isFriend && visual.name[0])
                    isFriend = Friends::IsFriend(visual.name);
            }

            const VisualPolicy::Profile selectedProfile =
                VisualPolicy::SelectProfile(
                    isPlayer,
                    hasVisual
                        ? visual.adminLevel
                        : 0,
                    Menu::s_AdminLevelThreshold,
                    isSameFaction,
                    isSameFamily,
                    isAnimal,
                    isFriend);
            const int profileIndex =
                static_cast<int>(selectedProfile);
            const Menu::EspProfileSettings& profile =
                Menu::s_EspProfiles[profileIndex];
            if (!profile.enabled)
                continue;
            if (isDeadOrKnock && !profile.showDead)
                continue;

            if (!frameEntry.coarseVisible)
                continue;

            ++profileCounts[profileIndex];
            if (hasVisual)
            {
                ++visualMatches;
                if (visual.visible)
                    ++visibleMatches;
                else
                    ++hiddenMatches;
            }
            if (frameEntry.projectedBoneMask)
                ++validSkeletons;
            const bool hidden =
                VisualPolicy::UseHiddenPalette(
                    profile.visibleCheck,
                    hasVisual,
                    hasVisual && visual.visible);

            ++pedCount;

            // Project bones once for box AABB + silhouette glow.
            ImVec2 bonePts[16]{};
            bool boneOk[16]{};
            if (frameEntry.projectedBoneMask)
            {
                for (int b = 0; b < 16; ++b)
                {
                    if (!(frameEntry.projectedBoneMask &
                          (1u << b)))
                        continue;
                    bonePts[b] = ImVec2(
                        frameEntry.projectedBones[b].x,
                        frameEntry.projectedBones[b].y);
                    boneOk[b] = true;
                }
            }

            // The rig gives joint centres, not the silhouette: the head bone is
            // inside the skull and the foot bone at the ankle. Both ends are
            // pushed out in world metres so the correction keeps its scale at
            // any distance, and the same corrected extent is what gets recorded
            // for use when the bones drop out.
            using BS = RagePlayerNames::BoneSlot;
            Vector3 crownWorld{};
            Vector3 soleWorld{};
            bool crownReady = false;
            bool soleReady = false;
            {
                const int headSlot = static_cast<int>(BS::Head);
                const int lFootSlot = static_cast<int>(BS::LeftFoot);
                const int rFootSlot = static_cast<int>(BS::RightFoot);
                if (boneOk[headSlot])
                {
                    crownWorld = visual.bones[headSlot];
                    crownWorld.z += kHeadBoneToCrown;
                    crownReady = true;
                }
                if (boneOk[lFootSlot] || boneOk[rFootSlot])
                {
                    Vector3 accum{};
                    int footCount = 0;
                    if (boneOk[lFootSlot])
                    {
                        accum = visual.bones[lFootSlot];
                        ++footCount;
                    }
                    if (boneOk[rFootSlot])
                    {
                        const Vector3& right =
                            visual.bones[rFootSlot];
                        if (footCount)
                        {
                            accum.x = (accum.x + right.x) * 0.5f;
                            accum.y = (accum.y + right.y) * 0.5f;
                            accum.z = (accum.z + right.z) * 0.5f;
                        }
                        else
                        {
                            accum = right;
                        }
                        ++footCount;
                    }
                    soleWorld = accum;
                    soleWorld.z -= kFootBoneToSole;
                    soleReady = true;
                }
                if (crownReady && soleReady)
                {
                    RememberPedExtent(
                        ped,
                        crownWorld.z - pos.z,
                        pos.z - soleWorld.z);
                }
            }

            float headAbove = 0.f;
            float feetBelow = 0.f;
            LookupPedExtent(ped, headAbove, feetBelow);

            Vector3 feetPos = pos;
            Vector3 headPos = pos;
            headPos.z += headAbove;
            feetPos.z -= feetBelow;

            // BuildStableEspBox only falls back to these two projections when
            // the bones that define the top, bottom and centre are missing.
            // With a full rig they were computed and thrown away every frame.
            const bool boneTop = crownReady;
            const bool boneBottom = soleReady;
            const bool boneCenter =
                boneOk[static_cast<int>(BS::Pelvis)] ||
                boneOk[static_cast<int>(BS::Spine)] ||
                boneOk[static_cast<int>(BS::Neck)];
            const bool boxFromBones =
                boneTop && boneBottom && boneCenter;

            RUNTIME_PROFILE_BEGIN(projectTimer);
            ImVec2 headPoint{};
            ImVec2 feetPoint{};
            ImVec2 crownPoint{};
            ImVec2 solePoint{};
            bool crownProjected = false;
            bool soleProjected = false;
            if (crownReady)
            {
                const auto projected =
                    Game::WorldToScreen(crownWorld);
                if (projected)
                {
                    crownPoint =
                        ImVec2(projected->x, projected->y);
                    crownProjected = true;
                }
            }
            if (soleReady)
            {
                const auto projected =
                    Game::WorldToScreen(soleWorld);
                if (projected)
                {
                    solePoint =
                        ImVec2(projected->x, projected->y);
                    soleProjected = true;
                }
            }
            bool anchorsReady =
                boxFromBones && crownProjected && soleProjected;
            if (!anchorsReady)
            {
                const auto screenPosOpt =
                    Game::WorldToScreen(feetPos);
                const auto screenHeadOpt =
                    Game::WorldToScreen(headPos);
                if (screenPosOpt && screenHeadOpt)
                {
                    feetPoint = ImVec2(
                        screenPosOpt->x,
                        screenPosOpt->y);
                    headPoint = ImVec2(
                        screenHeadOpt->x,
                        screenHeadOpt->y);
                    anchorsReady = true;
                }
            }
            RUNTIME_PROFILE_ACCUM(EspProject, projectTimer);
            if (anchorsReady) {
                ++w2sSuccess;

                ImVec2 boxMin{};
                ImVec2 boxMax{};
                BuildStableEspBox(
                    bonePts,
                    boneOk,
                    headPoint,
                    feetPoint,
                    crownProjected ? &crownPoint : nullptr,
                    soleProjected ? &solePoint : nullptr,
                    boxMin,
                    boxMax);

#if RAGEMP_DEV_BUILD
                // Bones are only refreshed for peds near screen centre, so
                // holding the aim bind pulls the target into the budget and its
                // box switches from the padded head/feet estimate to the tight
                // bone box. Measure both heights for one ped per interval: if
                // they differ, the two definitions are the whole bug.
                {
                    static ULONGLONG s_LastBoxLog = 0;
                    const ULONGLONG nowBox = GetTickCount64();
                    if (nowBox - s_LastBoxLog > 500)
                    {
                        s_LastBoxLog = nowBox;
                        int resolved = 0;
                        for (int b = 0;
                             b < static_cast<int>(
                                     RagePlayerNames::BoneSlot::Count);
                             ++b)
                        {
                            if (boneOk[b])
                                ++resolved;
                        }

                        float fallbackHeight = -1.f;
                        const auto feetOpt =
                            Game::WorldToScreen(feetPos);
                        const auto headOpt =
                            Game::WorldToScreen(headPos);
                        if (feetOpt && headOpt)
                        {
                            ImVec2 altMin{};
                            ImVec2 altMax{};
                            BuildStableEspBox(
                                nullptr,
                                nullptr,
                                ImVec2(headOpt->x, headOpt->y),
                                ImVec2(feetOpt->x, feetOpt->y),
                                nullptr,
                                nullptr,
                                altMin,
                                altMax);
                            fallbackHeight = altMax.y - altMin.y;
                        }

                        char msg[224]{};
                        sprintf_s(
                            msg,
                            "EspBox: used=%.1f fallback=%.1f fromBones=%d "
                            "bones=%d extent=%.2f/%.2f typ=%.2f/%.2f",
                            boxMax.y - boxMin.y,
                            fallbackHeight,
                            boxFromBones ? 1 : 0,
                            resolved,
                            headAbove,
                            feetBelow,
                            s_HeadAboveTypical,
                            s_FeetBelowTypical);
                        hooks::HLog(msg);
                    }
                }
#endif

                // Projected ESP overlay (same path as menu preview editor).
                EspProjected::EnsureLayoutsMigrated(
                    const_cast<Menu::EspProfileSettings&>(profile));

                char nameBuf[64]{};
                const char* nameLabel = isAnimal ? "Animal" : (isPlayer ? "\xD0\x93\xD1\x80\xD0\xB0\xD0\xB6\xD0\xB4\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xBD" : "NPC");
                auto isRealPersonStr = [](const char* str) -> bool {
                    if (!str || !str[0]) return false;
                    if (lstrcmpiA(str, "Player") == 0 ||
                        lstrcmpiA(str, "Unknown") == 0 ||
                        lstrcmpiA(str, "Citizen") == 0 ||
                        lstrcmpiA(str, "NPC") == 0 ||
                        lstrcmpiA(str, "Animal") == 0) return false;
                    if (strncmp(str, "\xD0\x93\xD1\x80\xD0\xB0\xD0\xB6\xD0\xB4\xD0\xB0\xD0\xBD", 12) == 0) return false;
                    if (strncmp(str, "\xD0\x9D\xD0\xB5\xD0\xB7\xD0\xBD\xD0\xB0\xD0\xBA", 10) == 0) return false;
                    return true;
                };

                __try {
                    if (isAnimal) {
                        if (hasVisual && visual.isAnimal && visual.name[0] &&
                            lstrcmpiA(visual.name, "Player") != 0 &&
                            lstrcmpiA(visual.name, "NPC") != 0) {
                            lstrcpynA(nameBuf, visual.name, (int)sizeof(nameBuf));
                            nameLabel = nameBuf;
                        }
                        ++nameHit;
                    } else if (hasVisual && visual.nickname[0] && isRealPersonStr(visual.nickname)) {
                        // 1. Confirmed character real name from V8 snapshot
                        lstrcpynA(nameBuf, visual.nickname, (int)sizeof(nameBuf));
                        nameLabel = nameBuf;
                        ++nameHit;
                    } else if (ped &&
                               Game::TryGetPedRpName(ped, nameBuf, (int)sizeof(nameBuf)) &&
                               nameBuf[0] && isRealPersonStr(nameBuf)) {
                        // 2. Native multiplayer.dll network name
                        nameLabel = nameBuf;
                        ++nameHit;
                    } else if (hasVisual && visual.name[0] && isRealPersonStr(visual.name)) {
                        // 3. Visual name if real person
                        lstrcpynA(nameBuf, visual.name, (int)sizeof(nameBuf));
                        nameLabel = nameBuf;
                        ++nameHit;
                    } else if (hasVisual && visual.name[0]) {
                        // 4. 5RP Citizen tag (Гражданин / Гражданка)
                        lstrcpynA(nameBuf, visual.name, (int)sizeof(nameBuf));
                        nameLabel = nameBuf;
                        ++nameHit;
                    } else {
                        nameLabel = isPlayer ? "\xD0\x93\xD1\x80\xD0\xB0\xD0\xB6\xD0\xB4\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xBD" : "NPC";
                        ++nameMiss;
                    }
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    nameLabel = isPlayer ? "\xD0\x93\xD1\x80\xD0\xB0\xD0\xB6\xD0\xB4\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xBD" : "NPC";
                    ++nameMiss;
                }
                if (isDeadOrKnock) {
                    char tagged[72]{};
                    const char* baseName = (nameLabel && nameLabel[0]) ? nameLabel : (isAnimal ? "Animal" : "\xD0\x93\xD1\x80\xD0\xB0\xD0\xB6\xD0\xB4\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xBD");
                    if (hasVisual && visual.knock && !visual.dead)
                        sprintf_s(tagged, "%s [KD]", baseName);
                    else
                        sprintf_s(tagged, "%s [DEAD]", baseName);
                    lstrcpynA(nameBuf, tagged, (int)sizeof(nameBuf));
                    nameLabel = nameBuf;
                }

                float readMax = maxHp;
                float armor = frameEntry.armor;
                if (readMax <= 0.f) readMax = 200.f;
                maxHp = readMax;
                // A player ped keeps the GTA baseline of 100 in memory, so it
                // has to go before scaling. That read is live, unlike the
                // snapshot, which stands in when the read is unusable.
                float hpPercent;
                if (isPlayer) {
                    const float span = maxHp - 100.f;
                    if (hp > 100.f && span > 0.f)
                        hpPercent = (hp - 100.f) / span;
                    else if (hasVisual && visual.hp >= 0.f)
                        hpPercent = visual.hp / 100.f;
                    else
                        hpPercent = 0.f;
                } else {
                    hpPercent = hp / maxHp;
                }
                if (hpPercent > 1.f) hpPercent = 1.f;
                if (hpPercent < 0.f) hpPercent = 0.f;
                float armorPercent = armor / 100.f;
                if (armorPercent < 0.f) armorPercent = 0.f;
                if (armorPercent > 1.f) armorPercent = 1.f;

                float rawDist = 0.f;
                char distBuf[16] = "0m";
                if (hasLocalPosition) {
                    float dx = pos.x - localPosition.x;
                    float dy = pos.y - localPosition.y;
                    float dz = pos.z - localPosition.z;
                    rawDist = sqrtf(dx * dx + dy * dy + dz * dz);
                    wsprintfA(distBuf, "%dm", (int)rawDist);
                }

                char weaponBuf[40]{};
                const char* weaponLabel = nullptr;
                uint32_t weaponHash = 0;
                if (profile.weaponTag) {
                    // Synthetic crowd carries a fixed weapon so the tag path is
                    // measured without reading ped memory that is not there.
                    weaponHash = frameEntry.synthetic
                        ? 3220176749u
                        : Game::GetPedWeaponHash(
                            const_cast<CPed*>(ped));
                    if (weaponHash != 0 && weaponHash != 0xA2719263u) {
                        WeaponNames::Format(
                            weaponHash, weaponBuf, (int)sizeof(weaponBuf));
                        weaponLabel = weaponBuf;
                    } else {
                        weaponHash = 0;
                    }
                }

                char dynIdBuf[24]{};
                char statIdBuf[24]{};
                const char* dynIdLabel = nullptr;
                const char* statIdLabel = nullptr;
                if (profile.dynamicIdTag && hasVisual && isPlayer &&
                    visual.remoteId != 0xFFFFu) {
                    sprintf_s(dynIdBuf, "ID:%u", (unsigned)visual.remoteId);
                    dynIdLabel = dynIdBuf;
                }
                if (profile.staticIdTag && hasVisual &&
                    isPlayer && visual.sqlId > 0) {
                    sprintf_s(statIdBuf, "#%d", visual.sqlId);
                    statIdLabel = statIdBuf;
                }

                char factionBuf[40]{};
                const char* factionLabel = nullptr;
                if (profile.factionTag && isPlayer &&
                    hasVisual &&
                    (visual.factionTag[0] ||
                     visual.factionId > 0)) {
                    if (visual.factionTag[0])
                        lstrcpynA(
                            factionBuf,
                            visual.factionTag,
                            (int)sizeof(factionBuf));
                    else
                        wsprintfA(factionBuf, "F%d", visual.factionId);
                    factionLabel = factionBuf;
                }

                char familyBuf[48]{};
                const char* familyLabel = nullptr;
                if (profile.familyTag && isPlayer &&
                    hasVisual && visual.familyName[0] != 0) {
                    lstrcpynA(
                        familyBuf,
                        visual.familyName,
                        (int)sizeof(familyBuf));
                    familyLabel = familyBuf;
                }

                EspProjected::DrawOptions drawOptions{};
                drawOptions.colorOverride =
                    hidden ? profile.hiddenColor : nullptr;
                // Colour the faction tag by the faction itself. Only asked for
                // when the tag is actually drawn, and the key is the server's
                // own identifier, so it survives a rename of the visible tag.
                if (profile.factionTag && hasVisual &&
                    visual.factionKey[0])
                {
                    drawOptions.factionColorOverride =
                        Menu::FactionColors::Resolve(
                            visual.factionKey,
                            visual.factionTag);
                }
                auto disableElement =
                    [&](EspProjected::ElementId id) {
                        const int index =
                            EspProjected::IndexOf(id);
                        if (index >= 0)
                            drawOptions.disabledMask |=
                                1u << index;
                    };
                if (!(profile.armorBar &&
                      armorPercent > 0.01f))
                    disableElement(
                        EspProjected::ElementId::Armor);
                if (!profile.nameTag)
                    disableElement(
                        EspProjected::ElementId::Name);
                if (!factionLabel)
                    disableElement(
                        EspProjected::ElementId::Faction);
                if (!familyLabel)
                    disableElement(
                        EspProjected::ElementId::Family);
                if (!weaponLabel &&
                    !(profile.weaponTag &&
                      profile.weaponDisplay != 0 &&
                      weaponHash != 0))
                    disableElement(
                        EspProjected::ElementId::Weapon);
                if (!dynIdLabel)
                    disableElement(
                        EspProjected::ElementId::DynamicId);
                if (!statIdLabel)
                    disableElement(
                        EspProjected::ElementId::StaticId);
                // 5RP disables the native RAGE nametag while rendering its
                // own client-visible label. That flag must not suppress ESP.

                EspProjected::PlayerData pdata{};
                pdata.name = nameLabel;
                pdata.faction = factionLabel ? factionLabel : "";
                pdata.family = familyLabel ? familyLabel : "";
                pdata.distance = distBuf;
                pdata.weapon = weaponLabel ? weaponLabel : "";
                pdata.weaponHash = weaponHash;
                pdata.dynamicId = dynIdLabel ? dynIdLabel : "";
                pdata.staticId = statIdLabel ? statIdLabel : "";
                pdata.health = hpPercent;
                pdata.armor = armorPercent;
                pdata.rawDistance = rawDist;
                pdata.dead = isDeadOrKnock &&
                    !(hasVisual && visual.knock && !visual.dead);
                pdata.knock = hasVisual && visual.knock;

                EspProjected::DrawPlayerOverlay(
                    drawList,
                    profile,
                    pdata,
                    ImRect(boxMin, boxMax),
                    1.f,
                    &drawOptions);

                if (profile.skeleton) {
                    RUNTIME_PROFILE_BEGIN(skeletonTimer);
                    ++skeletonExpected;
                    const float* skColSrc = hidden
                        ? profile.hiddenColor
                        : profile.skeletonColor;
                    const ImU32 skCol = IM_COL32(
                        (int)(skColSrc[0] * 255.f),
                        (int)(skColSrc[1] * 255.f),
                        (int)(skColSrc[2] * 255.f),
                        (int)((skColSrc[3] < 0.f
                            ? 0.f
                            : (skColSrc[3] > 1.f ? 1.f : skColSrc[3])) *
                            255.f +
                            0.5f));

                    if (EspSkeleton::ValidateProjected(
                            bonePts,
                            boneOk,
                            false))
                    {
                        const int segments =
                            EspSkeleton::DrawProjected(
                                drawList,
                                bonePts,
                                boneOk,
                                skCol,
                                1.5f);
                        skeletonSegments += segments;
                        if (segments > 0)
                            ++skeletonDrawn;
                    }
                    RUNTIME_PROFILE_ACCUM(
                        EspSkeleton,
                        skeletonTimer);
                }
            }
        }

        RenderWorldEntityEsp(
            drawList,
            worldEntries,
            worldEntryCount,
            localPosition,
            hasLocalPosition);

        RuntimeProfiler::SetDrawnPeds(
            static_cast<std::uint32_t>(
                pedCount));
        {
            unsigned int labels = 0;
            unsigned int glyphPasses = 0;
            EspProjected::ConsumeDrawCounters(
                labels,
                glyphPasses);
            RuntimeProfiler::SetDrawWork(labels, glyphPasses);
            RuntimeProfiler::SetSkeletonWork(
                skeletonDrawn,
                skeletonExpected);
        }

        // Standalone radar on the ESP drawlist (no ImGui window/render submit).
        {
            Menu::RadarSettings& radar = Menu::s_RadarSettings;
            if (radar.enabled && hasLocalPosition && drawList) {
                const ImGuiIO& io = ImGui::GetIO();
                MinimapAnchor::Rect anchor = MinimapAnchor::Compute(
                    io.DisplaySize.x, io.DisplaySize.y);
                float size = radar.size;
                if (size < 64.f) {
                    size = (anchor.width < anchor.height)
                        ? anchor.width
                        : anchor.height;
                    if (size < 80.f)
                        size = 140.f;
                }
                if (size < 80.f)
                    size = 80.f;
                if (size > 360.f)
                    size = 360.f;

                float posX = radar.posX;
                float posY = radar.posY;
                if (posX < 0.f || posY < 0.f) {
                    posX = anchor.left;
                    posY = anchor.top;
                }
                if (posX + size > io.DisplaySize.x)
                    posX = io.DisplaySize.x - size;
                if (posY + size > io.DisplaySize.y)
                    posY = io.DisplaySize.y - size;
                if (posX < 0.f)
                    posX = 0.f;
                if (posY < 0.f)
                    posY = 0.f;

                static bool s_RadarDragging = false;
                static bool s_RadarResizing = false;
                const ImVec2 winPos(posX, posY);
                const ImVec2 winSize(size, size);
                const ImVec2 mouse = io.MousePos;
                const bool hovered =
                    mouse.x >= winPos.x && mouse.y >= winPos.y &&
                    mouse.x <= winPos.x + winSize.x &&
                    mouse.y <= winPos.y + winSize.y;
                const float grip = 14.f;
                const bool onGrip =
                    mouse.x >= winPos.x + winSize.x - grip &&
                    mouse.y >= winPos.y + winSize.y - grip &&
                    mouse.x <= winPos.x + winSize.x &&
                    mouse.y <= winPos.y + winSize.y;

                if (io.MouseClicked[0] && hovered) {
                    s_RadarResizing = onGrip;
                    s_RadarDragging = !onGrip;
                }
                if (!io.MouseDown[0]) {
                    s_RadarDragging = false;
                    s_RadarResizing = false;
                }
                if (s_RadarDragging) {
                    radar.posX = winPos.x + io.MouseDelta.x;
                    radar.posY = winPos.y + io.MouseDelta.y;
                    radar.size = size;
                } else if (s_RadarResizing) {
                    float ns = mouse.x - winPos.x;
                    const float nsy = mouse.y - winPos.y;
                    if (nsy > ns)
                        ns = nsy;
                    if (ns < 80.f)
                        ns = 80.f;
                    if (ns > 360.f)
                        ns = 360.f;
                    radar.size = ns;
                    radar.posX = winPos.x;
                    radar.posY = winPos.y;
                    size = ns;
                }

                const ImVec2 center(
                    winPos.x + size * 0.5f,
                    winPos.y + size * 0.5f);
                const float half = size * 0.5f - 4.f;

                float camYaw = 0.f;
                if (Offsets::ViewportPtr &&
                    IsValidPtr(Offsets::ViewportPtr)) {
                    uint8_t* vp = nullptr;
                    __try {
                        vp = *Offsets::ViewportPtr;
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                        vp = nullptr;
                    }
                    if (IsValidPtr(vp)) {
                        __try {
                            const float* V = reinterpret_cast<const float*>(
                                vp + Offsets::Viewport::ViewMatrix);
                            camYaw = atan2f(-V[8], -V[9]);
                        } __except (EXCEPTION_EXECUTE_HANDLER) {
                        }
                    }
                }
                const float cosY = cosf(camYaw);
                const float sinY = sinf(camYaw);
                float range = radar.range;
                if (range < 10.f)
                    range = 10.f;

                drawList->AddRectFilled(
                    winPos,
                    ImVec2(winPos.x + size, winPos.y + size),
                    IM_COL32(
                        8,
                        10,
                        14,
                        static_cast<int>(
                            ImClamp(
                                radar.backgroundOpacity,
                                0.f,
                                1.f) * 255.f)),
                    8.f);
                drawList->AddRect(
                    winPos,
                    ImVec2(winPos.x + size, winPos.y + size),
                    IM_COL32(220, 220, 220, 72),
                    8.f,
                    0,
                    1.f);
                drawList->AddLine(
                    ImVec2(center.x, winPos.y + 8.f),
                    ImVec2(center.x, winPos.y + size - 8.f),
                    IM_COL32(255, 255, 255, 18),
                    1.f);
                drawList->AddLine(
                    ImVec2(winPos.x + 8.f, center.y),
                    ImVec2(winPos.x + size - 8.f, center.y),
                    IM_COL32(255, 255, 255, 18),
                    1.f);
                drawList->AddCircle(
                    center,
                    half * 0.5f,
                    IM_COL32(255, 255, 255, 14),
                    48,
                    1.f);
                const float localMarker =
                    ImClamp(radar.markerSize, 2.f, 9.f);
                drawList->AddTriangleFilled(
                    ImVec2(center.x, center.y - localMarker * 1.8f),
                    ImVec2(
                        center.x - localMarker,
                        center.y + localMarker),
                    ImVec2(
                        center.x + localMarker,
                        center.y + localMarker),
                    IM_COL32(80, 220, 120, 255));
                drawList->AddTriangleFilled(
                    ImVec2(winPos.x + size - 2.f, winPos.y + size - grip),
                    ImVec2(winPos.x + size - 2.f, winPos.y + size - 2.f),
                    ImVec2(winPos.x + size - grip, winPos.y + size - 2.f),
                    IM_COL32(255, 255, 255, 120));

                const auto radarColor = [](const float color[4]) {
                    return IM_COL32(
                        static_cast<int>(ImSaturate(color[0]) * 255.f),
                        static_cast<int>(ImSaturate(color[1]) * 255.f),
                        static_cast<int>(ImSaturate(color[2]) * 255.f),
                        static_cast<int>(ImSaturate(color[3]) * 255.f));
                };
                const ImU32 enemyCol = radarColor(radar.enemyColor);
                const ImU32 friendCol = radarColor(radar.friendColor);
                const ImU32 npcCol = radarColor(radar.npcColor);
                const ImU32 vehicleCol =
                    radarColor(radar.vehicleColor);

                for (int i = 0;
                     i < frame.count;
                     ++i) {
                    const Game::FramePedEntry&
                        radarEntry = frame.entries[i];
                    if (!radarEntry.valid ||
                        (!radarEntry.ped &&
                         !radarEntry.synthetic) ||
                        (radarEntry.ped &&
                         radarEntry.ped == localPed))
                        continue;
                    const Game::PedVisualData& visual =
                        radarEntry.visual;
                    if (!radarEntry.visualValid ||
                        visual.isLocal)
                        continue;
                    const char* identity = visual.nickname[0]
                        ? visual.nickname
                        : visual.name;
                    const bool isFriend =
                        visual.isPlayer &&
                        identity[0] &&
                        Friends::IsFriend(identity);
                    if (visual.isPlayer)
                    {
                        if (isFriend && !radar.showFriends)
                            continue;
                        if (!isFriend && !radar.showPlayers)
                            continue;
                    }
                    else if (!radar.showNpcs)
                    {
                        continue;
                    }

                    const float dx =
                        radarEntry.smoothedPosition.x -
                        localPosition.x;
                    const float dy =
                        radarEntry.smoothedPosition.y -
                        localPosition.y;
                    const float dist = sqrtf(dx * dx + dy * dy);
                    if (dist > range)
                        continue;

                    const float rx = dx * cosY - dy * sinY;
                    const float ry = dx * sinY + dy * cosY;
                    const float scale = half / range;
                    ImVec2 dot(
                        center.x + rx * scale,
                        center.y - ry * scale);
                    if (dot.x < winPos.x + 3.f) dot.x = winPos.x + 3.f;
                    if (dot.y < winPos.y + 3.f) dot.y = winPos.y + 3.f;
                    if (dot.x > winPos.x + size - 3.f) dot.x = winPos.x + size - 3.f;
                    if (dot.y > winPos.y + size - 3.f) dot.y = winPos.y + size - 3.f;

                    const ImU32 col = visual.isPlayer
                        ? (isFriend ? friendCol : enemyCol)
                        : npcCol;
                    const float marker =
                        ImClamp(radar.markerSize, 2.f, 9.f);
                    drawList->AddCircleFilled(dot, marker, col, 16);

                    // RAGE heading is network-synchronised body direction.
                    // Remote private camera direction is not available.
                    const float facingYaw =
                        visual.heading * 0.01745329251994329577f;
                    const float rel = facingYaw - camYaw;
                    const float ax = sinf(rel);
                    const float ay = -cosf(rel);
                    const float directionLength = ImClamp(
                        radar.directionLength,
                        4.f,
                        22.f);
                    const ImVec2 tip(
                        dot.x + ax * directionLength,
                        dot.y + ay * directionLength);
                    drawList->AddLine(dot, tip, col, 1.8f);
                    drawList->AddTriangleFilled(
                        tip,
                        ImVec2(
                            tip.x - ax * marker - ay * marker * 0.62f,
                            tip.y - ay * marker + ax * marker * 0.62f),
                        ImVec2(
                            tip.x - ax * marker + ay * marker * 0.62f,
                            tip.y - ay * marker - ax * marker * 0.62f),
                        col);
                }

                if (radar.showVehicles)
                {
                    for (int i = 0; i < worldEntryCount; ++i)
                    {
                        const auto& vehicle = worldEntries[i];
                        if (!vehicle.valid ||
                            vehicle.kind !=
                                RageWorldEntities::EntityKind::Vehicle)
                            continue;
                        const float dx =
                            vehicle.x - localPosition.x;
                        const float dy =
                            vehicle.y - localPosition.y;
                        const float distance =
                            sqrtf(dx * dx + dy * dy);
                        if (distance > range)
                            continue;

                        const float rx =
                            dx * cosY - dy * sinY;
                        const float ry =
                            dx * sinY + dy * cosY;
                        const float radarScale = half / range;
                        ImVec2 markerPos(
                            center.x + rx * radarScale,
                            center.y - ry * radarScale);
                        markerPos.x = ImClamp(
                            markerPos.x,
                            winPos.x + 4.f,
                            winPos.x + size - 4.f);
                        markerPos.y = ImClamp(
                            markerPos.y,
                            winPos.y + 4.f,
                            winPos.y + size - 4.f);

                        const float marker = ImClamp(
                            radar.markerSize,
                            2.f,
                            9.f);
                        drawList->AddRectFilled(
                            ImVec2(
                                markerPos.x - marker,
                                markerPos.y - marker),
                            ImVec2(
                                markerPos.x + marker,
                                markerPos.y + marker),
                            vehicleCol,
                            1.5f);

                        const float relative =
                            vehicle.heading *
                                0.01745329251994329577f -
                            camYaw;
                        const float ax = sinf(relative);
                        const float ay = -cosf(relative);
                        const float length = ImClamp(
                            radar.directionLength,
                            4.f,
                            22.f);
                        drawList->AddLine(
                            markerPos,
                            ImVec2(
                                markerPos.x + ax * length,
                                markerPos.y + ay * length),
                            vehicleCol,
                            1.8f);
                    }
                }
            }
        }

        // Log ped count + W2S success rate every diagnostic interval
        if (s_LogThisFrame) {
            char buf[128];
            wsprintfA(buf, "[ESP] peds=%d w2s_ok=%d names_hit=%d names_miss=%d",
                      pedCount, w2sSuccess, nameHit, nameMiss);
            hooks::HLog(buf);
            wsprintfA(
                buf,
                "[ESP] profiles P=%d N=%d A=%d visual=%d "
                "visible=%d hidden=%d skeletons=%d segments=%d",
                profileCounts[0],
                profileCounts[1],
                profileCounts[2],
                visualMatches,
                visibleMatches,
                hiddenMatches,
                validSkeletons,
                skeletonSegments);
            hooks::HLog(buf);
            if (samplePedForName) {
                wsprintfA(buf, "[ESP] sample ped for name=%p", samplePedForName);
                hooks::HLog(buf);
            }
            Game::LogNameDiagnostics();
        }
    }
}
