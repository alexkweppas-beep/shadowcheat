#pragma once

/*
 * shadow cheats — fibstr: Fibonacci-based compile-time stream cipher
 *
 * Третий обфускатор строк (после xorstr_ и skCrypt).
 *
 * Принцип работы:
 *   1. Базовый ключ берётся из __COUNTER__ XOR __LINE__ XOR seed от __TIME__
 *   2. Для каждого байта строки ключ вычисляется как fib(i) % 251 XOR base_key
 *      где fib(i) — i-е число Фибоначчи. Ключ не константный — для каждого
 *      байта своё значение. В отличие от xorstr_ (один xor на весь блок)
 *      и skCrypt (один XOR-байт на всю строку), здесь КАЖДЫЙ байт шифруется
 *      другим ключом из псевдо-случайного потока Фибоначчи.
 *   3. Дешифрование: та же операция (XOR симметрична), fib пересчитывается runtime
 *
 * IDA-скрипт для xorstr_ не сработает: ключ не константный, нет фиксированной
 * маски. Скрипт для fibstr не сработает на xorstr_. Реверсеру нужно разбирать
 * оба отдельно.
 *
 * Ограничения:
 *   - Только narrow (char) строки. Для wchar_t — конвертировать после decode.
 *   - Строка должна быть < 255 символов.
 *   - MSVC constexpr — нет рекурсии глубже 1024 (Фибоначчи итеративно).
 *
 * Использование:
 *   const char* str = fibstr_("Hello Server");
 *   // str — plaintext, доступен до конца scope
 */

#include <cstdint>
#include <cstddef>

// ─── Compile-time seed из __TIME__ ───
// Превращает "HH:MM:SS" в uint32_t. Уникален для каждой компиляции.
namespace fibstr_detail {

constexpr uint32_t time_seed() {
    const char* t = __TIME__; // "HH:MM:SS"
    return (uint32_t)(
        ((uint8_t)(t[0]-'0') * 36000u) +
        ((uint8_t)(t[1]-'0') * 3600u)  +
        ((uint8_t)(t[3]-'0') * 600u)   +
        ((uint8_t)(t[4]-'0') * 60u)    +
        ((uint8_t)(t[6]-'0') * 10u)    +
        ((uint8_t)(t[7]-'0') * 1u)
    );
}

// Компиляционный counter (инкрементируется каждый раз при использовании макроса)
// Используется как часть per-string ключа.

// ─── Compile-time Fibonacci ───
// fib_table[i] = i-е число Фибоначчи (mod 251 чтобы влезало в uint8_t)
// Первые 64 значений:
constexpr uint8_t fib_lut[64] = {
    0, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233,
    126, 107, 233, 84, 61, 145, 206, 95, 45, 140, 185, 69, 3,
    72, 71, 143, 214, 101, 64, 165, 229, 138, 111, 249, 104, 97,
    201, 42, 243, 29, 21, 50, 71, 121, 192, 57, 249, 50, 43,
    93, 136, 229, 109, 82, 191, 17, 208, 225, 177, 146
};

// Runtime: генерируем fib для индексов > 64 на лету
inline uint8_t fib_at(size_t n) {
    if (n < 64) return fib_lut[n];
    // Итеративно (не рекурсивно — стек безопасен)
    uint32_t a = fib_lut[62], b = fib_lut[63];
    for (size_t i = 64; i <= n; i++) {
        uint32_t c = (a + b) % 251u;
        a = b; b = (uint8_t)c;
    }
    return (uint8_t)b;
}

// ─── Compile-time строка (MSVC constexpr) ───
template<size_t N, uint32_t Key>
struct FibEncrypted {
    char buf[N];
    uint32_t key;

    // Шифруем compile-time в constexpr конструкторе
    constexpr FibEncrypted(const char(&src)[N]) : buf{}, key(Key) {
        // base_key = Key & 0xFF (per-string уникальный)
        uint8_t bk = (uint8_t)(Key & 0xFFu);
        for (size_t i = 0; i < N; i++) {
            // ключ для i-го байта = fib_lut[i % 64] XOR bk XOR (i * 7)
            uint8_t k = fib_lut[i % 64] ^ bk ^ (uint8_t)(i * 7u);
            buf[i] = (char)((uint8_t)src[i] ^ k);
        }
    }

    // Дешифруем runtime (та же XOR операция)
    __forceinline const char* decrypt() const {
        static thread_local char out[N];
        uint8_t bk = (uint8_t)(key & 0xFFu);
        for (size_t i = 0; i < N; i++) {
            uint8_t k = fib_at(i % 64) ^ bk ^ (uint8_t)(i * 7u);
            out[i] = (char)((uint8_t)buf[i] ^ k);
        }
        return out;
    }
};

} // namespace fibstr_detail

// ─── Per-use уникальный ключ: Time seed XOR counter XOR line ───
// __COUNTER__ инкрементируется для каждого использования макроса в TU.
// Результат: каждый fibstr_("...") имеет свой ключ, даже если строки одинаковые.
#define FIBSTR_KEY(ctr) \
    ((fibstr_detail::time_seed() ^ (uint32_t)(ctr) ^ (uint32_t)(__LINE__)) | 1u)

// ─── Основной макрос ───
// Использование: fibstr_("string literal")
// Возвращает const char* — дешифрованная строка (thread_local static буфер)
#define fibstr_(str) \
    (fibstr_detail::FibEncrypted<sizeof(str), FIBSTR_KEY(__COUNTER__)>(str).decrypt())

// ─── Широкая строка (обёртка) ───
// fibstrw_(L"string") → std::wstring (runtime конвертация из fibstr_)
// Используй только там где нужен wchar_t (WinHTTP заголовки, реестр).
#include <string>
#define fibstrw_(str) \
    ([&]() -> std::wstring { \
        const char* n = fibstr_(str); \
        int wlen = MultiByteToWideChar(CP_UTF8, 0, n, -1, nullptr, 0); \
        std::wstring w(wlen, 0); \
        MultiByteToWideChar(CP_UTF8, 0, n, -1, w.data(), wlen); \
        if (!w.empty() && w.back() == L'\0') w.pop_back(); \
        return w; \
    }())
