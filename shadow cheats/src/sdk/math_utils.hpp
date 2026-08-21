#pragma once
#include "../math/SimpleMath.h"

using namespace DirectX::SimpleMath;

bool    Vec3_Empty    (const Vector3& v);
Vector3 Vec3_Transform(Vector3* in, Matrix* m);
void    NormalizeAngles(Vector3& angle);
float   GetDistance   (Vector3 a, Vector3 b);
Vector3 CalcAngle     (Vector3 from, Vector3 to);
Vector3 LerpDynamic(Vector3 start, Vector3 end, float factor);
Vector3 LerpConstant(Vector3 start, Vector3 end, float speed);
