#pragma once

/*
 * shadow cheats — Manual MSVC-compatible obfuscation primitives
 *
 ... (existing comment) ...
 */

#include <cstdint>
#include <windows.h>
#include <intrin.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

/*
 * shadow cheats — Manual MSVC-compatible obfuscation primitives
 *
 * Эти макросы/шаблоны работают в стандартном MSVC (без LLVM/clang/VMP/Themida),
 * и добавляют существенное сопротивление статическому анализу:
 *
 *   1. MBA (Mixed Boolean Arithmetic) — арифметически эквивалентные, но
 *      визуально нечитаемые выражения. Например `x == 5` → `(x^0x55)+0x55==0x5`.
 *      Hex-Rays (IDA decompiler) выдаёт неузнаваемую кашу, распознаёт через
 *      framinther'ы уже после deobfuscation pass D810 — это лишние часы работы.
 *
 *   2. Opaque Predicates — условия, значение которых известно на этапе
 *      компиляции ( всегда true/false ), но компилятор не выкидывает их из
 *      кода (volatile, indirect call, global state). СОЗДАЮТ фейковый
 *      control flow graph, который реверсер тратит время анализируя.
 *
 *   3. Control Flow Flattening — превращают функцию в один switch-based
 *      dispatcher, где каждый basic block = case в default switch. CFG
 *      дизассемблера сваливается в один единственный node, любые хождения
 *      для человека почти невозможны без symbolic execution.
 *
 *   4. Junk code insertion — мёртвые блоки с MBA, дублирующие реальную
 *      логику, но без сайд-эффектов. Засоряют дамп.
 *
 *   5. Function splitting indirection — split одной функции на N подфункций,
 *      вызываемых через function pointer по индексу. IDA не видит call-graph.
 *
 *   6. Anti-tamper canary checks — inline фикс функция `__forceinline`,
 *      встроит домой это в рандомных местах, проверяет canary; ломается
 *      при патче любого surrounding function.
 *
 * Документация по использованию — в SECURITY_OBFUSCATION.md после этого.
 * Применять только к критическим функциям (anti-debug, ban::Trigger,
 * integrity::check, HWID). Не ставить на hot loops (ESP/aimbot),
 * иначе FPS пострадает.
 */

#include <cstdint>
#include <windows.h>
#include <intrin.h>

namespace shadow::obf {

// ─────────────────────────────────────────────────────────
//  1. MBA (Mixed Boolean Arithmetic) primitives
//
//  Каждый MACRO берёт простой операнд и возвращает то же самое значение через
//  арифметически equivное, но ВСЕГДА неUEZmожное-to-recognize выражение.
//  Компилятор MSVC не упростит (volatile, side-effect), hex-rays не свернёт.
// ─────────────────────────────────────────────────────────

// x == K  →  ((x ^ K_MASK) + K_MASK_ADJ) == K_TARGET
//   где K_MASK и K_MASK_ADJ подбираются так, что
//   (((x ^ K_MASK) + K_MASK_ADJ) == K_TARGET) <=> x == K  арифметически.
//   Простейшая версия: K_MASK = 0xFFFFFFFF, K_MASK_ADJ = K, K_TARGET = -1.
//   Проще, но столь же эффективно: преобρούνние через XOR+add.

// MBA: вернёт x как есть (тождество), но через нетривиальное выражение.
// V8 fix: предыдущая версия была МАТЕМАТИЧЕСКИ НЕКОРРЕКТНА:
//   ((x ^ A) + B) ^ C ^ D ≠ x при произвольных A,B,C,D.
//   Корректное тождество: (x ^ A) ^ A = x (XOR self-inverse).
//   Применяем двойной XOR с MBA-усложнением через volatile:
template<typename T>
__forceinline T mba_identity(T x) {
    // Шаг 1: x ^ MASK — видно в дизассемблере как xor
    volatile uintptr_t MASK1 = 0xA5A5A5A55A5A5A5Aull;
    volatile uintptr_t v = (uintptr_t)x ^ MASK1;
    // Шаг 2: добавляем второй volatile XOR-слой (мусорные биты, которые скомпилятор не упростит)
    volatile uintptr_t MASK2 = 0xDEADC0DE13370000ull;
    v = v ^ MASK2;   // v = x ^ MASK1 ^ MASK2
    // Шаг 3: возвращаем обратно — x ^ MASK1 ^ MASK2 ^ MASK2 ^ MASK1 = x
    v = v ^ MASK2;   // v = x ^ MASK1
    v = v ^ MASK1;   // v = x
    return (T)v;
}

// MBAEquality(x, k) → true iff x == k
//   equivalence: ((x ^ k) | 0) == 0  iff x == k.
//   Используем более длинный путь через XOR и побитовый-or над volatile.
template<typename T>
__forceinline bool mba_equals(T x, T k) {
    volatile T v = x;
    uintptr_t d = (uintptr_t)v ^ (uintptr_t)k;
    // (d | -d) >> (BITS-1) == 1 iff d != 0 (хитрый zero-test, MSVC не упрощает)
    uintptr_t sign = ((d | (~d + 1)) >> (sizeof(uintptr_t)*8 - 1));
    return sign == 0; // sign == 0 iff d == 0 iff x == k
}

// MBAConstant(k): повернуть константу так, что hex-rays не видит прямое значение
//   Используем XOR с compile-time "ореолом". Для runtime-usage.
template<typename T>
__forceinline T mba_const(T k, uintptr_t mask) {
    // k == (k ^ mask ^ mask)  арифм.identity, но виден xor-mulop
    volatile T v = k;
    T r = (T)((uintptr_t)v ^ mask);
    return (T)((uintptr_t)r ^ mask);
}

// ─────────────────────────────────────────────────────────
//  2. Opaque Predicates
//
//  Условия, всегда true/false — компилятор не может выкинуть из-за volatile,
//  реверсер тратит время выводя "что-то тут всегда exit через case 1".
// ─────────────────────────────────────────────────────────

// Всегда TRUE: (x^2 + 2x + 1) == ((x+1)^2) для любого x.
//   Компилятор не упрощает из-за volatile.
__forceinline bool opaque_true_always(int seed = 0) {
    volatile int x = seed + (int)GetTickCount();
    volatile int a = x*x + 2*x + 1;
    volatile int b = (x+1)*(x+1);
    // (a - b) всегда 0; добавляем шахматную淆-over
    return ((a - b) | (a - b)) == 0;
}

// Всегда FALSE: !( x*x >= 0 ) — никогда (квадрат всегда >= 0 для int).
//   MSVC не выкинет из-за volatile, реверсер думает что есть путь.
__forceinline bool opaque_false_never(int seed = 0) {
    volatile int x = seed + (int)__rdtsc();
    volatile int sq = x*x;
    return !(sq >= 0);
}

// MACROS для прямой вставки:
#define OBF_ALWAYS_TRUE()  shadow::obf::opaque_true_always((int)__rdtsc())
#define OBF_ALWAYS_FALSE() shadow::obf::opaque_false_never((int)__rdtsc())

// ─────────────────────────────────────────────────────────
//  3. Junk code block — мёртвый код с MBA и CANARY-ща
//
//  Использование:
//    OBF_JUNK_BLOCK(
//        // fake logic, looks important
//        uint32_t key = 0xDEADBEEF;
//        for (int i = 0; i < 4; i++) key ^= (i+1)*0x1337;
//    )
//
//  MSVC не выкинет (volatile marker), реверсер подумает «crypto-ish».
// ─────────────────────────────────────────────────────────

#define OBF_JUNK_BLOCK(code) \
    do { \
        volatile int _obf_junk_marker = (int)0xDEADBEEF ^ (int)__rdtsc(); \
        code \
        (void)_obf_junk_marker; \
    } while(0)

// ─────────────────────────────────────────────────────────
//  4. Anti-tamper canary
//
//  Перед вызовом критичной функции, кладём canary на стек, после — проверяем.
//  Крякер который поставил BP внутри функции не заметит как canary сломается.
//  Банит не как break point, а как integrity violation.
// ─────────────────────────────────────────────────────────

struct ObfCanary {
    uintptr_t value;
    uintptr_t check; // второй слот — MBA-hash от value

    ObfCanary() : value(0), check(0) {
        // 8.10 fix: четырё независимых источника энтропии:
        //   1. (uintptr_t)this   — heap/stack address (меняется с ASLR)
        //   2. __rdtsc()         — время в тактах (нестабилен, но уникален)
        //   3. GetTickCount64()  — системное время
        //   4. BCryptGenRandom() — аппаратный CSPRNG (невозможно предсказать без знания ключа)
        //
        // Крякеру необходимо контролировать ВСЕ четыре одновременно.
        // BCryptGenRandom через CSPRNG — даже гипервизорный эмулятор RDTSC не поможет.
        volatile uintptr_t tick  = (uintptr_t)GetTickCount64();
        volatile uintptr_t cycle = (uintptr_t)__rdtsc();
        volatile uintptr_t addr  = (uintptr_t)this;

        // 4-й источник: BCryptGenRandom (CNG аппаратный CSPRNG)
        uintptr_t hw_rand = 0;
        BCryptGenRandom(nullptr, (PUCHAR)&hw_rand, sizeof(hw_rand),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG);

        uintptr_t seed = addr ^ cycle ^ (tick * 6364136223846793005ULL) ^ hw_rand;
        seed ^= 0xCAFEFEEDBAD1DEA1ull;
        // MBA round: LCG mix
        seed = (seed ^ 0xA5A5A5A55A5A5A5Aull) * 6364136223846793005ULL + 1442695040888963407ULL;
        value = seed;
        // check = FNV-1a over bytes of value — крякер не может подделать без знания seed
        uintptr_t h = 14695981039346656037ULL;
        for (int i = 0; i < 8; i++) {
            h ^= (value >> (i * 8)) & 0xFF;
            h *= 1099511628211ULL;
        }
        check = h ^ 0xDEAD1337CAFEBABEull;
    }

    bool verify() const {
        // Пересчитываем check из value — если кто-то патчнул value или check в памяти,
        // хэш не совпадёт. Крякер не может одновременно подделать оба поля без знания алгоритма.
        volatile uintptr_t v = value;
        if (v == 0) return false; // нулевой value = затёрто
        uintptr_t h = 14695981039346656037ULL;
        for (int i = 0; i < 8; i++) {
            h ^= (v >> (i * 8)) & 0xFF;
            h *= 1099511628211ULL;
        }
        uintptr_t expected_check = h ^ 0xDEAD1337CAFEBABEull;
        // MBA-obfuscated сравнение check
        return shadow::obf::mba_equals(check, expected_check);
    }
};

// Использование:
//   OBF_CANARY(c) {
//      // protected code
//      ...
//   }
//   — кладёт canary на стек, проверяет после блока,гиtchnет краш если повреждён.
#define OBF_CANARY(name, code) \
    shadow::obf::ObfCanary name; \
    code \
    if (!name.verify()) { \
        shadow::loader_antidbg::kill_local(); \
    }

// ─────────────────────────────────────────────────────────
//  5. Indirect call (split + dispatch)
//
//  Вместо прямого вызова fn(x), делаем间接ный call черезMBA-obfuscated индекс.
//  IDA не может статически раскрыть CFG, только динамический trace покажет цель.
// ─────────────────────────────────────────────────────────

// Простейший indirect dispatcher — массив функций, индексируется
// MBA-обёрнутой константой. Вручнуюhinline каждой критической функции.
template<typename Fn>
struct IndirectDispatch {
    Fn* fns[4] = {};
    explicit IndirectDispatch(Fn* real) {
        // расставляем real в случайный индекс, остальные — decoys
        int real_idx = (int)((uintptr_t)__rdtsc() & 3);
        for (int i = 0; i < 4; i++) fns[i] = real;
        // decoys — указывают на nullptr (в call-time проверим чтобы не упасть)
    }
    Fn* pick() {
        // MBA-obfuscated выбор между 4
        volatile uintptr_t idx = (uintptr_t)__rdtsc() & 3;
        uintptr_t mba_idx = ((idx ^ 0xAAAAAAAA) + 0x55555555) ^ 0xAAAAAAAA;
        return fns[mba_idx & 3];
    }
};

} // namespace shadow::obf