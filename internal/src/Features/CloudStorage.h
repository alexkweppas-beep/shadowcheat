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
// Re-read C:\RAGEMP\cloud.session if the token was not loaded yet.
bool EnsureSession();
void Shutdown();
bool IsAvailable();
const char *LastError();
// Unix seconds from cloud.session trailer; 0 if unknown.
std::int64_t SubscriptionEndsAt() noexcept;
const char* GetUsername() noexcept;

bool List(const char *kind, Item *items, int capacity, int &count);
bool Get(const char *kind, const char *id, char *content, std::size_t capacity);
bool Upsert(const char *kind, const char *name, const char *content);
bool Delete(const char *kind, const char *id);
} // namespace CloudStorage
