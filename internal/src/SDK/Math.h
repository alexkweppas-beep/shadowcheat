#pragma once
#include <cmath>

struct Vector2 {
    float x, y;
    Vector2() : x(0), y(0) {}
    Vector2(float x, float y) : x(x), y(y) {}
    
    Vector2 operator-(const Vector2& v) const { return Vector2(x - v.x, y - v.y); }
    Vector2 operator+(const Vector2& v) const { return Vector2(x + v.x, y + v.y); }
    Vector2 operator*(float v) const { return Vector2(x * v, y * v); }
    float Length() const { return sqrtf(x * x + y * y); }
};

struct Vector3 {
    float x, y, z;
    Vector3() : x(0), y(0), z(0) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}
    
    Vector3 operator-(const Vector3& v) const { return Vector3(x - v.x, y - v.y, z - v.z); }
    Vector3 operator+(const Vector3& v) const { return Vector3(x + v.x, y + v.y, z + v.z); }
    Vector3 operator*(float v) const { return Vector3(x * v, y * v, z * v); }
    float Length() const { return sqrtf(x * x + y * y + z * z); }
    float Distance(const Vector3& v) const { return (*this - v).Length(); }
};

struct Matrix4x4 {
    float m[4][4];
};

namespace Math {
    static constexpr float PI = 3.14159265358979323846f;
}
