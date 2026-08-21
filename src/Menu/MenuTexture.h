#pragma once

#include <cstdint>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace MenuTexture
{
    void Initialize(ID3D11Device* device) noexcept;
    void Shutdown() noexcept;

    // Body silhouette for the ESP preview stage. White mask with the shape in
    // alpha, so the caller picks the colour through the ImGui tint.
    void* GetSilhouetteImGui(
        unsigned* outWidth,
        unsigned* outHeight) noexcept;

    // Icons downloaded once from cloud pack (public-assets).
    ID3D11ShaderResourceView* GetWeaponIcon(const char* iconFile) noexcept;
    void* GetWeaponIconImGui(const char* iconFile) noexcept;
    // Natural pixel size of a cached icon (false if unknown / not loaded).
    bool GetWeaponIconSize(
        const char* iconFile,
        unsigned* outWidth,
        unsigned* outHeight) noexcept;
    // Fit a weapon icon into maxW×maxH keeping aspect (GTA icons are wide).
    void FitWeaponIconSize(
        const char* iconFile,
        float maxW,
        float maxH,
        float* outW,
        float* outH) noexcept;
}
