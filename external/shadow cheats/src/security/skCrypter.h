#pragma once

// skCrypter - Compile-time string encryption
// Reference implementation

#include <intrin.h>
#include <array>
#include <cstdint>
#include <utility>

namespace skc
{
    template<int X> struct EnsureCompileTime {
        enum : int {
            Value = X
        };
    };

    // Seed generator
    #define SKC_SEED ((__TIME__[7] - '0') * 1ull    + (__TIME__[6] - '0') * 10ull  + \
                      (__TIME__[4] - '0') * 60ull   + (__TIME__[3] - '0') * 600ull + \
                      (__TIME__[1] - '0') * 3600ull + (__TIME__[0] - '0') * 36000ull)

    constexpr uint64_t LinearCongruentGenerator(uint64_t Rounds) {
        return 1013904223ull + (1664525ull * ((Rounds > 0) ? LinearCongruentGenerator(Rounds - 1) : SKC_SEED)) % 0xFFFFFFFF;
    }

    #define Random() EnsureCompileTime<LinearCongruentGenerator(10)>::Value
    #define RandomNumber(Min, Max) (Min + (Random() % (Max - Min + 1)))

    template <int X> struct RandomInt {
        enum : int {
            Value = RandomNumber(0, X)
        };
    };

    template <typename T, int size>
    class skCrypter
    {
    public:
        __forceinline skCrypter(T* data, T* key)
        {
            for (int i = 0; i < size; i++)
            {
                storage[i] = data[i];
                storage_key[i] = key[i];
            }
        }

        __forceinline ~skCrypter()
        {
            for (int i = 0; i < size; i++)
            {
                storage[i] = 0;
            }
        }

        __forceinline T* decrypt()
        {
            for (int i = 0; i < size; i++)
            {
                storage[i] ^= storage_key[i];
            }
            return storage;
        }

        __forceinline void encrypt()
        {
            for (int i = 0; i < size; i++)
            {
                storage[i] ^= storage_key[i];
            }
        }

        __forceinline void clear()
        {
            for (int i = 0; i < size; i++)
            {
                storage[i] = 0;
            }
        }

    private:
        T storage[size];
        T storage_key[size];
    };

} // namespace skc

#define skCrypt(str) \
    []() { \
        constexpr auto size = sizeof(str) / sizeof(str[0]); \
        constexpr auto key = skc::RandomInt<255>::Value; \
        auto sk_key = new char[size]; \
        for(int i = 0; i < size; i++) sk_key[i] = key ^ i; \
        auto sk_str = new char[size]; \
        for(int i = 0; i < size; i++) sk_str[i] = str[i] ^ sk_key[i]; \
        static skc::skCrypter<char, size> result(sk_str, sk_key); \
        delete[] sk_key; \
        delete[] sk_str; \
        return result.decrypt(); \
    }()
