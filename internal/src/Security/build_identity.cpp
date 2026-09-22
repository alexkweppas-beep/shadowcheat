#include "build_identity.hpp"

#include <cstring>

#ifndef SHADOW_BUILD_TAG
#define SHADOW_BUILD_TAG "development"
#endif
#ifndef SHADOW_VARIANT_ID
#define SHADOW_VARIANT_ID 0
#endif
#ifndef SHADOW_VARIANT_SEED
#define SHADOW_VARIANT_SEED 0x1ULL
#endif

#define SHADOW_STRINGIFY_INNER(value) #value
#define SHADOW_STRINGIFY(value) SHADOW_STRINGIFY_INNER(value)

#pragma section(".shid", read, write)
#pragma comment(linker, "/SECTION:.shid,RW")

extern "C" __declspec(allocate(".shid"))
volatile char g_ShadowCustomerTag[BuildIdentity::kCustomerTagCapacity] =
    "SHADOW_CUSTOMER_TAG_00000000000000000000000000000000";

extern "C" __declspec(allocate(".shid"))
const char g_ShadowBuildTag[] = "SHADOW_BUILD_TAG:" SHADOW_BUILD_TAG;

extern "C" __declspec(allocate(".shid"))
volatile char g_ShadowVariantTag[] =
    "SHADOW_VARIANT_ID:" SHADOW_STRINGIFY(SHADOW_VARIANT_ID);

namespace BuildIdentity {
const char* BuildTag() noexcept {
    return g_ShadowBuildTag;
}

const char* CustomerTag() noexcept {
    return const_cast<const char*>(g_ShadowCustomerTag);
}

bool MatchesCustomerTag(const char* value) noexcept {
    if (!value)
        return false;
    return std::strncmp(
        CustomerTag(),
        value,
        kCustomerTagCapacity - 1) == 0;
}

std::uint32_t VariantId() noexcept {
    return g_ShadowVariantTag[0] == 'S'
        ? SHADOW_VARIANT_ID
        : UINT32_MAX;
}

bool MatchesVariant(std::uint32_t value) noexcept {
    return value == VariantId();
}

__declspec(noinline) bool ValidateVariantNonce(
    std::uint64_t nonce) noexcept {
    volatile std::uint64_t seed = SHADOW_VARIANT_SEED;
#if (SHADOW_VARIANT_ID % 4) == 0
    const std::uint64_t mixed = (nonce ^ seed) + 0x9E3779B97F4A7C15ULL;
    return ((mixed - 0x9E3779B97F4A7C15ULL) ^ seed) == nonce;
#elif (SHADOW_VARIANT_ID % 4) == 1
    const std::uint64_t mixed = (nonce + seed) ^ 0xD6E8FEB86659FD93ULL;
    return (mixed ^ 0xD6E8FEB86659FD93ULL) - seed == nonce;
#elif (SHADOW_VARIANT_ID % 4) == 2
    const std::uint64_t mixed = (nonce ^ 0xA0761D6478BD642FULL) - seed;
    const std::uint64_t restored =
        (mixed + seed) ^ 0xA0761D6478BD642FULL;
    return restored == nonce;
#else
    const std::uint64_t mixed = (nonce - seed) + 0xE7037ED1A0B428DBULL;
    return mixed - 0xE7037ED1A0B428DBULL + seed == nonce;
#endif
}
}
