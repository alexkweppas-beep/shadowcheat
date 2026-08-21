#pragma once
#include <cstdint>
#include <cstddef>

// Runtime pack loaded from cloud (see MenuTexture). No embedded PNG blob.
namespace WeaponIconBlob
{
    struct Entry final
    {
        const char* name;
        std::uint32_t offset;
        std::uint32_t size;
    };

    // Backed by the downloaded pack in MenuTexture.
    const Entry* Find(const char* name) noexcept;
    const unsigned char* Bytes(const Entry& e) noexcept;
    bool IsReady() noexcept;
    void SetPack(
        const unsigned char* data,
        std::size_t size,
        const Entry* index,
        int indexCount) noexcept;
    void ClearPack() noexcept;
}
