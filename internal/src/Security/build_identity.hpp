#pragma once

#include <cstddef>
#include <cstdint>

namespace BuildIdentity {
constexpr std::size_t kCustomerTagCapacity = 53;

const char* BuildTag() noexcept;
const char* CustomerTag() noexcept;
bool MatchesCustomerTag(const char* value) noexcept;
std::uint32_t VariantId() noexcept;
bool MatchesVariant(std::uint32_t value) noexcept;
__declspec(noinline) bool ValidateVariantNonce(
    std::uint64_t nonce) noexcept;
}
