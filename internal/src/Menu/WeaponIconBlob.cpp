#include "WeaponIconBlob.h"

namespace WeaponIconBlob
{
namespace
{
    const unsigned char* g_Data = nullptr;
    std::size_t g_DataSize = 0;
    const Entry* g_Index = nullptr;
    int g_IndexCount = 0;
}

void SetPack(
    const unsigned char* data,
    std::size_t size,
    const Entry* index,
    int indexCount) noexcept
{
    g_Data = data;
    g_DataSize = size;
    g_Index = index;
    g_IndexCount = indexCount;
}

void ClearPack() noexcept
{
    g_Data = nullptr;
    g_DataSize = 0;
    g_Index = nullptr;
    g_IndexCount = 0;
}

bool IsReady() noexcept
{
    return g_Data != nullptr && g_Index != nullptr && g_IndexCount > 0;
}

const Entry* Find(const char* name) noexcept
{
    if (!IsReady() || !name || !name[0])
        return nullptr;
    for (int i = 0; i < g_IndexCount; ++i)
    {
        const char* a = g_Index[i].name;
        const char* b = name;
        while (*a && *b)
        {
            const char ca =
                (*a >= 'A' && *a <= 'Z')
                    ? static_cast<char>(*a + 32)
                    : *a;
            const char cb =
                (*b >= 'A' && *b <= 'Z')
                    ? static_cast<char>(*b + 32)
                    : *b;
            if (ca != cb)
                break;
            ++a;
            ++b;
        }
        if (*a == 0 && *b == 0)
            return &g_Index[i];
    }
    return nullptr;
}

const unsigned char* Bytes(const Entry& e) noexcept
{
    if (!g_Data || e.offset + e.size > g_DataSize)
        return nullptr;
    return g_Data + e.offset;
}
}
