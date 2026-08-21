#include "math_utils.hpp"
#include <cmath>

bool Vec3_Empty(const Vector3& v)
{
    return v == Vector3(0.f, 0.f, 0.f);
}

Vector3 Vec3_Transform(Vector3* in, Matrix* m)
{
    Vector3 out{};
    out.x = in->x * m->_11 + in->y * m->_21 + in->z * m->_31 + m->_41;
    out.y = in->x * m->_12 + in->y * m->_22 + in->z * m->_32 + m->_42;
    out.z = in->x * m->_13 + in->y * m->_23 + in->z * m->_33 + m->_43;
    return out;
}

void NormalizeAngles(Vector3& a)
{
    while (a.x >  89.f) a.x -= 180.f;
    while (a.x < -89.f) a.x += 180.f;
    while (a.y >  180.f) a.y -= 360.f;
    while (a.y < -180.f) a.y += 360.f;
}

float GetDistance(Vector3 a, Vector3 b)
{
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

Vector3 CalcAngle(Vector3 from, Vector3 to)
{
    const float d = GetDistance(from, to);
    if (d == 0.f) return { 0.f, 0.f, 0.f };
    return { (to.x - from.x)/d, (to.y - from.y)/d, (to.z - from.z)/d };
}
Vector3 LerpDynamic(Vector3 start, Vector3 end, float factor)
{
    Vector3 diff = end - start;
    NormalizeAngles(diff);
    
    return start + (diff / factor);
}

Vector3 LerpConstant(Vector3 start, Vector3 end, float factor)
{
    Vector3 diff = end - start;
    NormalizeAngles(diff); 
    float dist = diff.Length();
    
    
    
    float speed = 10.0f / factor;
    
    if (dist <= speed) {
        return end;
    }
    return start + (diff * (speed / dist));
}
