#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

struct ShadowLaunchAuth;

namespace CloudStorage {
enum class AuthorizationState : std::uint8_t {
  Pending,
  Authorized,
  Denied
};

enum class VerificationResult : std::uint8_t {
  Authorized,
  Retry,
  Denied
};

struct Item final {
  char id[37]{};
  char name[64]{};
  std::int64_t updatedAt{0};
};

constexpr int kMaxItems = 32;

struct EncryptedFeatureBundle final {
  std::vector<std::uint8_t> payload;
  std::array<std::uint8_t, 32> key{};
  char canary[65]{};
};

bool Initialize(const ShadowLaunchAuth* auth);
bool EnsureSession();
void Shutdown();
bool IsAvailable();
const char *LastError();
// Unix seconds derived from the launcher's subscription data; 0 if unknown.
std::int64_t SubscriptionEndsAt() noexcept;
const char* GetUsername() noexcept;

AuthorizationState GetAuthorizationState() noexcept;
bool IsAuthorized() noexcept;
VerificationResult VerifySubscription();

bool PostJson(const wchar_t *path, const char *body, unsigned long &status);
bool DownloadFeatureBundle(
    const char* featureId,
    EncryptedFeatureBundle& bundle);

bool List(const char *kind, Item *items, int capacity, int &count);
bool Get(const char *kind, const char *id, char *content, std::size_t capacity);
bool Upsert(const char *kind, const char *name, const char *content);
bool Delete(const char *kind, const char *id);
} // namespace CloudStorage
