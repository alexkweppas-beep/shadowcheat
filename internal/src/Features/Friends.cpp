#include "Friends.h"

#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Friends
{
namespace
{
    char g_Names[kMaxFriends][kNameCapacity]{};
    float g_Colors[kMaxFriends][4]{};
    int g_Count = 0;

    void SetDefaultColor(int index) noexcept
    {
        g_Colors[index][0] = 51.f / 255.f;  // #33
        g_Colors[index][1] = 217.f / 255.f; // #D9
        g_Colors[index][2] = 64.f / 255.f;  // #40
        g_Colors[index][3] = 1.0f;          // #FF
    }

    void NormalizeName(const char* input, char* output, int capacity) noexcept
    {
        if (!output || capacity <= 0)
            return;
        output[0] = 0;
        if (!input)
            return;

        int write = 0;
        for (const char* p = input; *p && write + 1 < capacity; ++p) {
            const unsigned char c = static_cast<unsigned char>(*p);
            if (c <= 32)
                continue;
            output[write++] = static_cast<char>(
                (c >= 'A' && c <= 'Z') ? (c + 32) : c);
        }
        output[write] = 0;
    }

    bool NamesEqual(const char* a, const char* b) noexcept
    {
        char na[kNameCapacity]{};
        char nb[kNameCapacity]{};
        NormalizeName(a, na, kNameCapacity);
        NormalizeName(b, nb, kNameCapacity);
        return na[0] && lstrcmpiA(na, nb) == 0;
    }

    bool AppendLine(
        char* output,
        std::size_t capacity,
        std::size_t& used,
        const char* key,
        const char* value) noexcept
    {
        if (!output || used >= capacity || !key)
            return false;
        const int written = sprintf_s(
            output + used,
            capacity - used,
            "%s=%s\n",
            key,
            value ? value : "");
        if (written <= 0)
            return false;
        used += static_cast<std::size_t>(written);
        return true;
    }

    bool AppendLineNum(
        char* output,
        std::size_t capacity,
        std::size_t& used,
        const char* key,
        int value) noexcept
    {
        char buf[32]{};
        sprintf_s(buf, "%d", value);
        return AppendLine(output, capacity, used, key, buf);
    }

    bool AppendLineFloat(
        char* output,
        std::size_t capacity,
        std::size_t& used,
        const char* key,
        float value) noexcept
    {
        char buf[32]{};
        sprintf_s(buf, "%.3f", value);
        return AppendLine(output, capacity, used, key, buf);
    }

    const char* FindValue(const char* content, const char* key) noexcept
    {
        if (!content || !key)
            return nullptr;
        const std::size_t keyLength = std::strlen(key);
        const char* line = content;
        while (*line) {
            if (!std::strncmp(line, key, keyLength) &&
                line[keyLength] == '=')
                return line + keyLength + 1;
            const char* next = std::strchr(line, '\n');
            if (!next)
                break;
            line = next + 1;
        }
        return nullptr;
    }

    void CopyValue(const char* src, char* dst, int capacity) noexcept
    {
        if (!dst || capacity <= 0)
            return;
        dst[0] = 0;
        if (!src)
            return;
        int write = 0;
        for (const char* p = src; *p && *p != '\r' && *p != '\n' &&
             write + 1 < capacity;
             ++p)
            dst[write++] = *p;
        dst[write] = 0;
    }
} // namespace

bool UseCustomColors = false;

void Clear() noexcept
{
    SecureZeroMemory(g_Names, sizeof(g_Names));
    for (int i = 0; i < kMaxFriends; ++i)
        SetDefaultColor(i);
    g_Count = 0;
}

int Count() noexcept
{
    return g_Count;
}

bool IsFriend(const char* name) noexcept
{
    if (!name || !name[0])
        return false;
    for (int i = 0; i < g_Count; ++i) {
        if (NamesEqual(g_Names[i], name))
            return true;
    }
    return false;
}

bool Add(const char* name) noexcept
{
    if (!name || !name[0] || g_Count >= kMaxFriends)
        return false;
    char trimmed[kNameCapacity]{};
    int write = 0;
    for (const char* p = name; *p && write + 1 < kNameCapacity; ++p) {
        if (write == 0 && static_cast<unsigned char>(*p) <= 32)
            continue;
        trimmed[write++] = *p;
    }
    while (write > 0 &&
           static_cast<unsigned char>(trimmed[write - 1]) <= 32)
        --write;
    trimmed[write] = 0;
    if (!trimmed[0] || IsFriend(trimmed))
        return false;
    lstrcpynA(g_Names[g_Count], trimmed, kNameCapacity);
    SetDefaultColor(g_Count);
    ++g_Count;
    return true;
}

bool Remove(const char* name) noexcept
{
    if (!name)
        return false;
    for (int i = 0; i < g_Count; ++i) {
        if (NamesEqual(g_Names[i], name))
            return RemoveAt(i);
    }
    return false;
}

bool RemoveAt(int index) noexcept
{
    if (index < 0 || index >= g_Count)
        return false;
    for (int i = index; i + 1 < g_Count; ++i)
    {
        lstrcpynA(g_Names[i], g_Names[i + 1], kNameCapacity);
        for (int c = 0; c < 4; ++c)
            g_Colors[i][c] = g_Colors[i + 1][c];
    }
    SecureZeroMemory(g_Names[g_Count - 1], kNameCapacity);
    SetDefaultColor(g_Count - 1);
    --g_Count;
    return true;
}

const char* Get(int index) noexcept
{
    if (index < 0 || index >= g_Count)
        return "";
    return g_Names[index];
}

void SetColor(int index, const float* color) noexcept
{
    if (index < 0 || index >= g_Count || !color)
        return;
    for (int c = 0; c < 4; ++c)
        g_Colors[index][c] = color[c];
}

void GetColor(int index, float* color) noexcept
{
    if (index < 0 || index >= g_Count || !color)
        return;
    for (int c = 0; c < 4; ++c)
        color[c] = g_Colors[index][c];
}

void GetColorByName(const char* name, float* color) noexcept
{
    if (!name || !color)
        return;
    for (int i = 0; i < g_Count; ++i)
    {
        if (NamesEqual(g_Names[i], name))
        {
            for (int c = 0; c < 4; ++c)
                color[c] = g_Colors[i][c];
            return;
        }
    }
}

bool Export(
    char* output,
    std::size_t capacity,
    std::size_t& used) noexcept
{
    if (!AppendLineNum(output, capacity, used, "friend_count", g_Count))
        return false;
    if (!AppendLineNum(output, capacity, used, "friend_custom_colors", UseCustomColors ? 1 : 0))
        return false;
    for (int i = 0; i < g_Count; ++i) {
        char key[32]{};
        sprintf_s(key, "friend_%d", i);
        if (!AppendLine(output, capacity, used, key, g_Names[i]))
            return false;
        
        char clrKey[48]{};
        sprintf_s(clrKey, "friend_r_%d", i);
        AppendLineFloat(output, capacity, used, clrKey, g_Colors[i][0]);
        sprintf_s(clrKey, "friend_g_%d", i);
        AppendLineFloat(output, capacity, used, clrKey, g_Colors[i][1]);
        sprintf_s(clrKey, "friend_b_%d", i);
        AppendLineFloat(output, capacity, used, clrKey, g_Colors[i][2]);
        sprintf_s(clrKey, "friend_a_%d", i);
        AppendLineFloat(output, capacity, used, clrKey, g_Colors[i][3]);
    }
    return true;
}

void Import(const char* content) noexcept
{
    Clear();
    if (!content)
        return;
    int count = 0;
    if (const char* v = FindValue(content, "friend_count"))
        count = static_cast<int>(std::strtol(v, nullptr, 10));
    if (const char* v = FindValue(content, "friend_custom_colors"))
        UseCustomColors = static_cast<int>(std::strtol(v, nullptr, 10)) != 0;
        
    if (count < 0)
        count = 0;
    if (count > kMaxFriends)
        count = kMaxFriends;
    for (int i = 0; i < count; ++i) {
        char key[32]{};
        sprintf_s(key, "friend_%d", i);
        if (const char* v = FindValue(content, key)) {
            char name[kNameCapacity]{};
            CopyValue(v, name, kNameCapacity);
            if (Add(name)) {
                char clrKey[48]{};
                sprintf_s(clrKey, "friend_r_%d", i);
                if (const char* cv = FindValue(content, clrKey)) g_Colors[g_Count - 1][0] = std::strtof(cv, nullptr);
                sprintf_s(clrKey, "friend_g_%d", i);
                if (const char* cv = FindValue(content, clrKey)) g_Colors[g_Count - 1][1] = std::strtof(cv, nullptr);
                sprintf_s(clrKey, "friend_b_%d", i);
                if (const char* cv = FindValue(content, clrKey)) g_Colors[g_Count - 1][2] = std::strtof(cv, nullptr);
                sprintf_s(clrKey, "friend_a_%d", i);
                if (const char* cv = FindValue(content, clrKey)) g_Colors[g_Count - 1][3] = std::strtof(cv, nullptr);
            }
        }
    }
}
} // namespace Friends
