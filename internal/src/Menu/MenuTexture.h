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

    // Soft radial falloff, white with the shape in alpha. Generated rather than
    // shipped: it is a formula, and a formula is smaller and sharper at any size
    // than a compressed image of one.
    //
    // Meant to be drawn additively, which is what turns overlapping sprites into
    // a glow instead of a pile of translucent discs.
    void* GetGlowImGui() noexcept;

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
