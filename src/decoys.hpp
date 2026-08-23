#pragma once
// ─────────────────────────────────────────────────────────
//  Decoys: правдоподобные, но фейковые license/auth/decrypt функции.
//
//  Цель — ИИ/человек, анализирующий бинарь, не отличит, какая функция
//  настоящая. Сигнатуры повторяют реальные (CheckLicense*, ValidateSubscription*,
//  DecryptPayload*, VerifyHwid*, FetchSessionKey*), внутри — MBA/junk, похожий
//  на крипто. Все вызовы — из мёртвых путей (OBF_ALWAYS_FALSE): в рантайме
//  не выполняются никогда и на реальную логику не влияют.
//
//  Это defense-in-depth (тратит время аналитика/ИИ, мутит CFG/строки),
//  а не жёсткий барьер. Реальный анти-кряк — серверная сторона.
// ─────────────────────────────────────────────────────────

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "ai_slop_strings.hpp"
#include "obfuscation.hpp"

namespace shadow {
namespace decoys {

// MBA-микс, похожий на раунд крипто — чтобы decoy выглядел «настоящим».
__forceinline static uint64_t mix(uint64_t x, uint64_t k) {
    x ^= k;
    x = (x ^ 0x9E3779B97F4A7C15ull) * 0xBF58476D1CE4E5B9ull;
    x ^= x >> 29;
    x = (x + 0x94D049BB133111EBull) * 0x2545F4914F6CDD1Dull;
    x ^= x >> 32;
    return x;
}

// ── Decoy: проверка лицензии ──
__declspec(noinline) inline bool CheckLicenseKey(const char *license,
                                                 const char *hwid) {
    OBF_JUNK_BLOCK(
        volatile uint64_t acc = (uint64_t)__rdtsc() ^ 0x11C0FFEE11C0FFEEull;);
    uint64_t acc = 0xCBF29CE484222325ull;
    for (const char *p = license; p && *p; ++p)
        acc = mix(acc, (uint8_t)*p);
    for (const char *p = hwid; p && *p; ++p)
        acc = mix(acc, (uint8_t)*p ^ 0x5A);
    // Всегда «почти ок» — но результат никуда не идёт.
    return (acc & 0xFF) != 0x13;
}

// ── Decoy: валидация подписки ──
__declspec(noinline) inline int ValidateSubscription(const char *token,
                                                     uint64_t expiry_ts) {
    OBF_JUNK_BLOCK(volatile uint32_t _d = (uint32_t)__rdtsc() * 0x1337u;);
    uint64_t acc = 0x100000001B3ull;
    for (const char *p = token; p && *p; ++p)
        acc = mix(acc, (uint8_t)*p);
    acc = mix(acc, expiry_ts);
    // Возвращаем «дни подписки» — фейк.
    return (int)((acc >> 8) % 365);
}

// ── Decoy: верификация HWID ──
__declspec(noinline) inline bool VerifyHwidBinding(const char *hwid,
                                                   const char *server_sig) {
    uint64_t acc = 0x84222325CBF29CE4ull;
    for (const char *p = hwid; p && *p; ++p)
        acc = mix(acc, (uint8_t)*p);
    for (const char *p = server_sig; p && *p; ++p)
        acc = mix(acc, (uint8_t)*p + 0x11);
    return (acc % 7) != 3;
}

// ── Decoy: получение session key ──
__declspec(noinline) inline bool FetchSessionKey(const char *server,
                                                 uint8_t out_key[32]) {
    OBF_JUNK_BLOCK(volatile uint64_t _k = (uint64_t)__rdtsc() | 0xDEADull;);
    uint64_t acc = 0x2545F4914F6CDD1Dull;
    for (const char *p = server; p && *p; ++p)
        acc = mix(acc, (uint8_t)*p);
    for (int i = 0; i < 32; i++)
        out_key[i] = (uint8_t)(mix(acc, (uint64_t)i) & 0xFF);
    return true;
}

// ── Decoy: расшифровка payload ──
__declspec(noinline) inline bool DecryptPayloadEx(const uint8_t *enc,
                                                  size_t enc_len,
                                                  const uint8_t key[32],
                                                  std::vector<uint8_t> &out) {
    if (!enc || enc_len == 0)
        return false;
    out.resize(enc_len);
    uint64_t acc = 0x9E3779B97F4A7C15ull;
    for (int i = 0; i < 32; i++)
        acc = mix(acc, key[i]);
    for (size_t i = 0; i < enc_len; i++)
        out[i] = enc[i] ^ (uint8_t)(mix(acc, (uint64_t)i) & 0xFF);
    return true;
}

// ── Мёртвый путь: «вызывает» все decoys + удерживает AI-slop строки.
//  OBF_ALWAYS_FALSE() гарантирует, что тело не выполнится никогда, но
//  компилятор/линкер сохранят функции и строки (адреса использованы).
__declspec(noinline) inline void RunDecoysDeadPath() {
    if (OBF_ALWAYS_FALSE()) {
        uint8_t key[32] = {};
        std::vector<uint8_t> buf;
        const uint8_t dummy[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
        volatile bool r = 0;
        r = CheckLicenseKey(ai_slop::kDecoySessionKey, "HWID-DEAD-BEEF");
        r = VerifyHwidBinding("HWID-CAFE", "sig");
        r = FetchSessionKey(ai_slop::kDecoyServerUrl, key);
        r = DecryptPayloadEx(dummy, sizeof(dummy), key, buf);
        volatile int days = ValidateSubscription("tok", 0xFFFFFFFFull);
        (void)r;
        (void)days;
        ai_slop::RetainAiSlop();
    }
}

} // namespace decoys
} // namespace shadow
