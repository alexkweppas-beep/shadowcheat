#include "secure_script_blob.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <utility>

namespace SecureScriptBlob {
namespace {
struct KeyContext {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_KEY_HANDLE key{};
    std::vector<std::uint8_t> object;

    ~KeyContext() {
        if (key)
            BCryptDestroyKey(key);
        if (algorithm)
            BCryptCloseAlgorithmProvider(algorithm, 0);
    }
};

bool OpenAlgorithm(KeyContext& context) {
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
            &context.algorithm, BCRYPT_AES_ALGORITHM, nullptr, 0)))
        return false;
    if (!BCRYPT_SUCCESS(BCryptSetProperty(
            context.algorithm,
            BCRYPT_CHAINING_MODE,
            reinterpret_cast<PUCHAR>(BCRYPT_CHAIN_MODE_GCM),
            sizeof(BCRYPT_CHAIN_MODE_GCM),
            0)))
        return false;
    DWORD objectSize = 0;
    DWORD copied = 0;
    BCryptGetProperty(
        context.algorithm,
        BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectSize),
        sizeof(objectSize),
        &copied,
        0);
    context.object.resize(objectSize);
    return objectSize != 0;
}

bool OpenKey(
    const Blob& blob,
    KeyContext& context,
    std::array<std::uint8_t, 32>& keyBytes) {
    for (std::size_t index = 0; index < keyBytes.size(); ++index)
        keyBytes[index] = blob.maskedKey[index] ^ blob.keyMask[index];
    if (!OpenAlgorithm(context))
        return false;
    return BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(
        context.algorithm,
        &context.key,
        context.object.data(),
        static_cast<ULONG>(context.object.size()),
        keyBytes.data(),
        static_cast<ULONG>(keyBytes.size()),
        0));
}

BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO Authentication(
    const Blob& blob) {
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth{};
    BCRYPT_INIT_AUTH_MODE_INFO(auth);
    auth.pbNonce = const_cast<PUCHAR>(blob.nonce);
    auth.cbNonce = 12;
    auth.pbTag = const_cast<PUCHAR>(blob.tag);
    auth.cbTag = 16;
    return auth;
}
}

Plaintext::~Plaintext() {
    Clear();
}

Plaintext::Plaintext(Plaintext&& other) noexcept
    : bytes_(std::move(other.bytes_)) {}

Plaintext& Plaintext::operator=(Plaintext&& other) noexcept {
    if (this == &other)
        return *this;
    Clear();
    bytes_ = std::move(other.bytes_);
    return *this;
}

char* Plaintext::Resize(std::size_t size) {
    Clear();
    bytes_.resize(size + 1, 0);
    return bytes_.data();
}

const char* Plaintext::Data() const noexcept {
    return bytes_.empty() ? nullptr : bytes_.data();
}

std::size_t Plaintext::Size() const noexcept {
    return bytes_.empty() ? 0 : bytes_.size() - 1;
}

void Plaintext::Clear() noexcept {
    if (!bytes_.empty())
        SecureZeroMemory(bytes_.data(), bytes_.size());
    bytes_.clear();
    bytes_.shrink_to_fit();
}

bool Decrypt(const Blob& blob, Plaintext& output) noexcept {
    KeyContext context;
    std::array<std::uint8_t, 32> keyBytes{};
    if (!OpenKey(blob, context, keyBytes)) {
        SecureZeroMemory(keyBytes.data(), keyBytes.size());
        return false;
    }
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth =
        Authentication(blob);
    ULONG written = 0;
    char* plaintext = output.Resize(blob.size);
    const NTSTATUS status = BCryptDecrypt(
        context.key,
        const_cast<PUCHAR>(blob.ciphertext),
        static_cast<ULONG>(blob.size),
        &auth,
        nullptr,
        0,
        reinterpret_cast<PUCHAR>(plaintext),
        static_cast<ULONG>(blob.size),
        &written,
        0);
    SecureZeroMemory(keyBytes.data(), keyBytes.size());
    if (!BCRYPT_SUCCESS(status) || written != blob.size) {
        output.Clear();
        return false;
    }
    plaintext[written] = 0;
    return true;
}

bool DecryptEnvelope(
    const std::vector<std::uint8_t>& payload,
    const std::array<std::uint8_t, 32>& key,
    Plaintext& output) noexcept {
    constexpr std::size_t nonceSize = 12;
    constexpr std::size_t tagSize = 16;
    if (payload.size() <= nonceSize + tagSize)
        return false;
    const std::array<std::uint8_t, 32> zeroMask{};
    const Blob blob{
        payload.data() + nonceSize,
        payload.size() - nonceSize - tagSize,
        payload.data(),
        payload.data() + payload.size() - tagSize,
        key.data(),
        zeroMask.data()};
    return Decrypt(blob, output);
}
}
