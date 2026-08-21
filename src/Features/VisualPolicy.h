#pragma once

namespace VisualPolicy
{
    enum class Profile : int
    {
        Players = 0,
        FactionPlayers = 1,
        FamilyPlayers = 2,
        Npc = 3,
        Admin = 4,
        Animal = 5,
        // Must stay aligned with Menu::EspProfileId: callers index the profile
        // array with this value.
        Friend = 6
    };

    struct Point final
    {
        float x{};
        float y{};
        float z{};
        bool isPlayer{};
        bool isAnimal{};
    };

    // Admin outranks friend on purpose: knowing an admin is watching matters
    // more than the reminder that you are on good terms with them.
    constexpr Profile SelectProfile(
        bool isPlayer,
        int adminLevel,
        int adminThreshold,
        bool isSameFaction = false,
        bool isSameFamily = false,
        bool isAnimal = false,
        bool isFriend = false) noexcept
    {
        if (isAnimal)
            return Profile::Animal;
        if (!isPlayer)
            return Profile::Npc;
        if (adminLevel > adminThreshold)
            return Profile::Admin;
        if (isFriend)
            return Profile::Friend;
        if (isSameFamily)
            return Profile::FamilyPlayers;
        if (isSameFaction)
            return Profile::FactionPlayers;
        return Profile::Players;
    }

    constexpr bool UseHiddenPalette(
        bool visibleCheck,
        bool hasSnapshot,
        bool visible) noexcept
    {
        return visibleCheck &&
            (!hasSnapshot || !visible);
    }

    constexpr bool IsBonePlausible(
        const Point& entity,
        const Point& bone,
        float maximumDistanceSquared = 100.0f) noexcept
    {
        const float dx = bone.x - entity.x;
        const float dy = bone.y - entity.y;
        const float dz = bone.z - entity.z;
        return maximumDistanceSquared > 0.0f &&
            dx * dx + dy * dy + dz * dz <=
                maximumDistanceSquared;
    }

    constexpr bool AcceptBoneSnapshot(
        int validBoneCount,
        int minimumBoneCount = 8) noexcept
    {
        return minimumBoneCount > 0 &&
            validBoneCount >= minimumBoneCount;
    }

    inline int AssignNearestOneToOne(
        const Point* snapshots,
        int snapshotCount,
        const Point* peds,
        int pedCount,
        bool* usedSnapshots,
        bool* usedPeds,
        int* assignedPed,
        float playerMaximumDistanceSquared,
        float npcMaximumDistanceSquared) noexcept
    {
        if (!snapshots || !peds ||
            !usedSnapshots || !usedPeds ||
            !assignedPed ||
            snapshotCount <= 0 || pedCount <= 0)
            return 0;
        if (snapshotCount > 256)
            snapshotCount = 256;
        if (pedCount > 256)
            pedCount = 256;

        constexpr float cellSize = 5.f;
        constexpr int bucketCount = 512;
        int bucketHeads[bucketCount]{};
        int nextPed[256]{};
        int pedCellX[256]{};
        int pedCellY[256]{};
        for (int bucket = 0;
             bucket < bucketCount;
             ++bucket)
            bucketHeads[bucket] = -1;

        const auto cellOf = [=](float value) noexcept {
            const float scaled = value / cellSize;
            int cell = static_cast<int>(scaled);
            if (scaled < 0.f &&
                static_cast<float>(cell) != scaled)
                --cell;
            return cell;
        };
        const auto hashCell =
            [=](int x, int y) noexcept {
                const unsigned ux =
                    static_cast<unsigned>(x);
                const unsigned uy =
                    static_cast<unsigned>(y);
                return static_cast<int>(
                    (ux * 73856093u ^
                     uy * 19349663u) &
                    (bucketCount - 1));
            };

        for (int pedIndex = 0;
             pedIndex < pedCount;
             ++pedIndex)
        {
            const int cellX = cellOf(
                peds[pedIndex].x);
            const int cellY = cellOf(
                peds[pedIndex].y);
            pedCellX[pedIndex] = cellX;
            pedCellY[pedIndex] = cellY;
            const int bucket =
                hashCell(cellX, cellY);
            nextPed[pedIndex] =
                bucketHeads[bucket];
            bucketHeads[bucket] = pedIndex;
        }

        int assigned = 0;
        for (int snapshotIndex = 0;
             snapshotIndex < snapshotCount;
             ++snapshotIndex)
        {
            if (usedSnapshots[snapshotIndex])
                continue;
            const Point& snapshot =
                snapshots[snapshotIndex];
            const float limit = snapshot.isPlayer
                ? playerMaximumDistanceSquared
                : npcMaximumDistanceSquared;
            if (limit <= 0.f)
                continue;
            int radius = 0;
            while (radius < 16 &&
                static_cast<float>(
                    radius * radius) *
                    cellSize * cellSize < limit)
                ++radius;
            const int originX =
                cellOf(snapshot.x);
            const int originY =
                cellOf(snapshot.y);
            int bestPed = -1;
            int bestTypePenalty = 2;
            float bestDistanceSquared = limit;
            for (int offsetY = -radius;
                 offsetY <= radius;
                 ++offsetY)
            {
                for (int offsetX = -radius;
                     offsetX <= radius;
                     ++offsetX)
                {
                    const int cellX =
                        originX + offsetX;
                    const int cellY =
                        originY + offsetY;
                    const int bucket =
                        hashCell(cellX, cellY);
                    for (int pedIndex =
                             bucketHeads[bucket];
                         pedIndex >= 0;
                         pedIndex =
                             nextPed[pedIndex])
                    {
                        if (pedCellX[pedIndex] !=
                                cellX ||
                            pedCellY[pedIndex] !=
                                cellY ||
                            usedPeds[pedIndex])
                            continue;
                        if (snapshot.isAnimal !=
                            peds[pedIndex].isAnimal)
                            continue;
                        if (!snapshot.isPlayer &&
                            peds[pedIndex].isPlayer)
                            continue;
                        const float dx =
                            peds[pedIndex].x -
                            snapshot.x;
                        const float dy =
                            peds[pedIndex].y -
                            snapshot.y;
                        const float dz =
                            peds[pedIndex].z -
                            snapshot.z;
                        const float distanceSquared =
                            dx * dx + dy * dy +
                            dz * dz;
                        if (distanceSquared > limit)
                            continue;
                        const int typePenalty =
                            snapshot.isPlayer &&
                            !peds[pedIndex].isPlayer
                                ? 1 : 0;
                        if (typePenalty >
                                bestTypePenalty ||
                            (typePenalty ==
                                 bestTypePenalty &&
                             distanceSquared >=
                                 bestDistanceSquared))
                            continue;
                        bestTypePenalty =
                            typePenalty;
                        bestDistanceSquared =
                            distanceSquared;
                        bestPed = pedIndex;
                    }
                }
            }
            if (bestPed < 0)
                continue;
            assignedPed[snapshotIndex] = bestPed;
            usedSnapshots[snapshotIndex] = true;
            usedPeds[bestPed] = true;
            ++assigned;
        }
        return assigned;
    }
}
