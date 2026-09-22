#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

namespace SecureScriptBlob {
struct Blob {
    const std::uint8_t* ciphertext;
    std::size_t size;
    const std::uint8_t* nonce;
    const std::uint8_t* tag;
    const std::uint8_t* maskedKey;
    const std::uint8_t* keyMask;
};

class Plaintext final {
public:
    ~Plaintext();

    Plaintext(const Plaintext&) = delete;
    Plaintext& operator=(const Plaintext&) = delete;
    Plaintext() = default;
    Plaintext(Plaintext&& other) noexcept;
    Plaintext& operator=(Plaintext&& other) noexcept;

    char* Resize(std::size_t size);
    const char* Data() const noexcept;
    std::size_t Size() const noexcept;
    void Clear() noexcept;

private:
    std::vector<char> bytes_;
};

bool Decrypt(const Blob& blob, Plaintext& output) noexcept;
bool DecryptEnvelope(
    const std::vector<std::uint8_t>& payload,
    const std::array<std::uint8_t, 32>& key,
    Plaintext& output) noexcept;
}
