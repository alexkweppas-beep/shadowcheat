#pragma once
#include "../math/SimpleMath.h"

using namespace DirectX::SimpleMath;

bool WorldToScreen(const Matrix& view, const Vector3& world, Vector2& outScreen);
