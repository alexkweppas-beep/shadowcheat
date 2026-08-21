#include "world_to_screen.hpp"
#include "../core/globals.hpp"
#include "../security/vmp_markers.hpp"

bool WorldToScreen(const Matrix& ViewMatrix, const Vector3& vWorld, Vector2& vOut)
{
    const Matrix v = ViewMatrix.Transpose();
    const Vector4 row1 (v._21, v._22, v._23, v._24);
    const Vector4 row2 (v._31, v._32, v._33, v._34);
    const Vector4 row3 (v._41, v._42, v._43, v._44);

    Vector3 vec_out(
        row1.x*vWorld.x + row1.y*vWorld.y + row1.z*vWorld.z + row1.w,
        row2.x*vWorld.x + row2.y*vWorld.y + row2.z*vWorld.z + row2.w,
        row3.x*vWorld.x + row3.y*vWorld.y + row3.z*vWorld.z + row3.w);

    if (vec_out.z <= 0.001f) {
        return false;
    }

    vec_out.z = 1.f / vec_out.z;
    vec_out.x *= vec_out.z;
    vec_out.y *= vec_out.z;

    const float w = static_cast<float>(g.GameRect.right - g.GameRect.left);
    const float h = static_cast<float>(g.GameRect.bottom - g.GameRect.top);

    vOut.x  = w * 0.5f + (0.5f * vec_out.x * w + 0.5f);
    vOut.y  = h * 0.5f - (0.5f * vec_out.y * h + 0.5f);
    return true;
}


