#pragma once

// Ranked global search over menu destinations. The shell only needs the
// ranking plus display strings; the destination table stays private.
namespace Menu
{
    struct RankedSearchResult final
    {
        int destination = -1;
        int score = 0x7FFFFFFF;
    };

    int BuildRankedSearchResults(
        const char* query,
        bool quantAvailable,
        bool ukraineAvailable,
        bool fiveRpAvailable,
        RankedSearchResult* output,
        int capacity) noexcept;

    const char* SearchDestinationId(int destination) noexcept;
    const char* SearchDestinationTitle(int destination) noexcept;
    const char* SearchDestinationCategory(int destination) noexcept;

    void NavigateToSearchDestination(int destination) noexcept;
}
