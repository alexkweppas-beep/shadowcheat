#pragma once

#include "../imgui/imgui.h"

#include <cstddef>
#include <d3d11.h>
#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace ModelPreview
{
    bool Init(ID3D11Device* device, ID3D11DeviceContext* ctx);
    void Shutdown();

    // Loads embedded nekomiya mesh + bones (noctua-style, synchronous).
    bool LoadDefaultAsset();
    bool LoadGlb(const std::string& path);
    bool LoadGlbFromMemory(const void* data, size_t size);
    bool LoadBonesGlb(const std::string& path);
    bool LoadBonesGlbFromMemory(const void* data, size_t size);

    // Kept for PedPreview call sites; no-op (sync embed load).
    void TickApplyCloudAsset();
    void RetryCloudDownload();
    bool IsLoading();
    bool IsLoadFailed();

    void Render(
        float rotationY = 0.f,
        float rotationX = 0.f,
        float viewScale = 1.f);

    ImTextureID GetTexture();
    bool IsLoaded();
    bool IsReady();
    int GetTriangleCount();
    int GetBoneCount();

    bool ProjectCanonicalBones(
        ImVec2 imgMin,
        ImVec2 imgMax,
        float rotationY,
        float rotationX,
        float viewScale,
        ImVec2* output,
        int capacity) noexcept;

    // Projects every finite canonical point and reports per-slot availability.
    // Unlike the compatibility overload above, coincident 2D points are valid.
    int ProjectCanonicalBones(
        ImVec2 imgMin,
        ImVec2 imgMax,
        float rotationY,
        float rotationX,
        float viewScale,
        ImVec2* output,
        bool* available,
        int capacity) noexcept;

    void DrawSkeletonOverlay(
        ImDrawList* drawList,
        ImVec2 imgMin,
        ImVec2 imgMax,
        float rotationY,
        float rotationX,
        ImU32 color,
        float thickness,
        float viewScale = 1.f);

    std::string ResolveDefaultModelPath();
}
