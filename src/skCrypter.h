#pragma once

// Simplified, MSVC-compatible compile-time string encryption
// Works around metaprogramming limits in older/strict MSVC setups

#define SEED ((__TIME__[7] - '0') * 1  + (__TIME__[6] - '0') * 10  + \
              (__TIME__[4] - '0') * 60 + (__TIME__[3] - '0') * 600 + \
              (__TIME__[1] - '0') * 3600 + (__TIME__[0] - '0') * 36000)

constexpr int LCG(int rounds) {
    return 1013904223 + 1664525 * ((rounds > 0) ? LCG(rounds - 1) : SEED & 0xFFFFFFFF);
}

#define RANDOM() LCG(10)
#define RANDOM_NUMBER(Min, Max) (Min + (RANDOM() % (Max - Min + 1)))

template <size_t Size>
class cryptor {
private:
    char buffer[Size];
    char key;
public:
    constexpr cryptor(const char* str, char key) : buffer{}, key(key) {
        for (size_t i = 0; i < Size; ++i) {
            buffer[i] = str[i] ^ key;
        }
    }

    __forceinline char* decrypt() {
        for (size_t i = 0; i < Size; ++i) {
            buffer[i] ^= key;
        }
        buffer[Size - 1] = '\0';
        return buffer;
    }
};

#define skCrypt(str) (cryptor<sizeof(str)>(str, RANDOM_NUMBER(1, 255)).decrypt())

