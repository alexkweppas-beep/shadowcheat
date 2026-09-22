#pragma once

#include <cstdint>

#include "../Security/secure_script_blob.hpp"

namespace FeatureBundles {
enum class Id : std::uint8_t {
    FiveRp,
    Quant,
    Ukraine,
    Policy,
    Count
};

enum class State : std::uint8_t {
    Idle,
    Loading,
    Ready,
    Failed
};

void Request(Id id) noexcept;
bool Consume(
    Id id,
    SecureScriptBlob::Plaintext& output) noexcept;
State GetState(Id id) noexcept;
void Shutdown() noexcept;
}
