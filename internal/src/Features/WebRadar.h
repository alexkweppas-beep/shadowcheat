#pragma once

namespace WebRadar {

struct Settings final {
    bool enabled{false};
    float hz{5.f};
    float radius{400.f};
};

Settings& GetSettings() noexcept;
void Update() noexcept;
void Shutdown() noexcept;
const char* StatusText() noexcept;
const char* PageUrl() noexcept;
bool CopyPageUrl() noexcept;
unsigned LastStatus() noexcept;
unsigned LastSendAgeMs() noexcept;

} // namespace WebRadar
