#pragma once

#include <cstddef>
#include <cstdint>

namespace CloudStorage {
struct Item final {
  char id[37]{};
  char name[64]{};
  std::int64_t updatedAt{0};
};

constexpr int kMaxItems = 32;

void Initialize();
// Re-read the launcher's shared auth page if the protected token is absent.
bool EnsureSession();
void Shutdown();
bool IsAvailable();
const char *LastError();
// Unix seconds derived from the launcher's subscription data; 0 if unknown.
std::int64_t SubscriptionEndsAt() noexcept;
const char* GetUsername() noexcept;

// Performs a background ping to the server to verify the session/token is still valid.
bool VerifySubscription();

bool List(const char *kind, Item *items, int capacity, int &count);
bool Get(const char *kind, const char *id, char *content, std::size_t capacity);
bool Upsert(const char *kind, const char *name, const char *content);
bool Delete(const char *kind, const char *id);
} // namespace CloudStorage
