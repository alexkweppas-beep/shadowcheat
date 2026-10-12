#pragma once

#include <cstddef>

namespace Friends
{
    static constexpr int kMaxFriends = 64;
    static constexpr int kNameCapacity = 48;

    void Clear() noexcept;
    int Count() noexcept;
    bool IsFriend(const char* name) noexcept;
    bool Add(const char* name) noexcept;
    bool Remove(const char* name) noexcept;
    bool RemoveAt(int index) noexcept;
    const char* Get(int index) noexcept;

    extern bool UseCustomColors;

    void SetColor(int index, const float* color) noexcept;
    void GetColor(int index, float* color) noexcept;
    void GetColorByName(const char* name, float* color) noexcept;

    bool Export(
        char* output,
        std::size_t capacity,
        std::size_t& used) noexcept;
    void Import(const char* content) noexcept;
}
