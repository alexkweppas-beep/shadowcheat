#include "EspSkeleton.h"

#include <cmath>

namespace
{
    using Bone = RagePlayerNames::BoneSlot;
    constexpr EspSkeleton::BonePair kPairs[] = {
        {Bone::Head, Bone::Neck},
        {Bone::Neck, Bone::Spine},
        {Bone::Spine, Bone::Pelvis},
        {Bone::Neck, Bone::LeftUpperArm},
        {Bone::LeftUpperArm, Bone::LeftForearm},
        {Bone::LeftForearm, Bone::LeftHand},
        {Bone::Neck, Bone::RightUpperArm},
        {Bone::RightUpperArm, Bone::RightForearm},
        {Bone::RightForearm, Bone::RightHand},
        {Bone::Pelvis, Bone::LeftThigh},
        {Bone::LeftThigh, Bone::LeftCalf},
        {Bone::LeftCalf, Bone::LeftFoot},
        {Bone::Pelvis, Bone::RightThigh},
        {Bone::RightThigh, Bone::RightCalf},
        {Bone::RightCalf, Bone::RightFoot}
    };

    constexpr int kBoneCount =
        static_cast<int>(Bone::Count);
}

namespace EspSkeleton
{
    const BonePair* CanonicalPairs(
        int& count) noexcept
    {
        count = static_cast<int>(
            sizeof(kPairs) / sizeof(kPairs[0]));
        return kPairs;
    }

    bool ValidateProjected(
        const ImVec2* points,
        const bool* valid,
        bool requireAll) noexcept
    {
        if (!points || !valid)
            return false;
        // Thresholds used to be absolute pixels: a player far enough away has a
        // rig only tens of pixels tall, so a wrist landing within a fifth of a
        // pixel of a forearm threw the whole skeleton away. As the distance
        // wobbled, the skeleton blinked. Now the extent of the rig decides what
        // counts as degenerate, and single degenerate pairs are skipped at draw
        // time instead of rejecting everything.
        int validCount = 0;
        float minX = 0.f;
        float minY = 0.f;
        float maxX = 0.f;
        float maxY = 0.f;
        for (int index = 0;
             index < kBoneCount;
             ++index)
        {
            if (!valid[index])
            {
                if (requireAll)
                    return false;
                continue;
            }
            if (!std::isfinite(points[index].x) ||
                !std::isfinite(points[index].y))
                return false;
            if (validCount == 0)
            {
                minX = maxX = points[index].x;
                minY = maxY = points[index].y;
            }
            else
            {
                if (points[index].x < minX)
                    minX = points[index].x;
                if (points[index].x > maxX)
                    maxX = points[index].x;
                if (points[index].y < minY)
                    minY = points[index].y;
                if (points[index].y > maxY)
                    maxY = points[index].y;
            }
            ++validCount;
        }
        if (validCount < (requireAll ? kBoneCount : 8))
            return false;

        // A rig collapsed into a couple of pixels carries no shape worth
        // drawing; anything taller is honest data even at long range.
        const float extentX = maxX - minX;
        const float extentY = maxY - minY;
        if (extentY < 2.f && extentX < 2.f)
            return false;
        const float diagonalSquared =
            extentX * extentX + extentY * extentY;
        if (diagonalSquared > 4000000.f)
            return false;

        int pairCount = 0;
        const BonePair* pairs =
            CanonicalPairs(pairCount);
        int validPairs = 0;
        for (int index = 0;
             index < pairCount;
             ++index)
        {
            const int from =
                static_cast<int>(pairs[index].from);
            const int to =
                static_cast<int>(pairs[index].to);
            if (valid[from] && valid[to])
                ++validPairs;
        }
        return validPairs >= (requireAll ? pairCount : 6);
    }

    int DrawProjected(
        ImDrawList* draw,
        const ImVec2* points,
        const bool* valid,
        ImU32 color,
        float thickness) noexcept
    {
        if (!draw || !points || !valid)
            return 0;
        int pairCount = 0;
        const BonePair* pairs =
            CanonicalPairs(pairCount);
        int drawn = 0;
        for (int index = 0;
             index < pairCount;
             ++index)
        {
            const int from =
                static_cast<int>(pairs[index].from);
            const int to =
                static_cast<int>(pairs[index].to);
            if (!valid[from] || !valid[to])
                continue;
            const float dx =
                points[from].x - points[to].x;
            const float dy =
                points[from].y - points[to].y;
            const float lengthSquared =
                dx * dx + dy * dy;
            // Sub-pixel and absurd segments are skipped one by one; the rest of
            // the rig still draws.
            if (lengthSquared < 0.02f ||
                lengthSquared > 4000000.f)
                continue;
            draw->AddLine(
                points[from],
                points[to],
                color,
                thickness);
            ++drawn;
        }
        return drawn;
    }
}
