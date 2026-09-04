#include "MenuMotion.h"

#include <cmath>

#include "../imgui/imgui.h"

namespace MenuMotion
{
    namespace
    {
        bool g_Reduced = false;
        constexpr float kMaxDelta = 1.f / 20.f;
    }

    bool ReducedMotion() noexcept
    {
        return g_Reduced;
    }

    void SetReducedMotion(bool enabled) noexcept
    {
        g_Reduced = enabled;
    }

    float Delta() noexcept
    {
        const float dt = ImGui::GetIO().DeltaTime;
        if (!(dt > 0.f))
            return 0.f;
        return dt < kMaxDelta ? dt : kMaxDelta;
    }

    float Approach(float current, float target, float duration) noexcept
    {
        if (g_Reduced || duration <= 0.0001f)
            return target;
        const float dt = Delta();
        if (dt <= 0.f)
            return current;
        // 5 time constants land within ~1% of the target inside `duration`.
        const float rate = 5.f / duration;
        const float alpha = 1.f - expf(-dt * rate);
        const float next = current + (target - current) * alpha;
        return fabsf(target - next) < 0.0005f ? target : next;
    }

    float Advance(float progress, bool forward, float duration) noexcept
    {
        const float target = forward ? 1.f : 0.f;
        if (g_Reduced || duration <= 0.0001f)
            return target;
        const float dt = Delta();
        if (dt <= 0.f)
            return progress;
        const float step = dt / duration;
        progress += forward ? step : -step;
        return progress < 0.f ? 0.f : (progress > 1.f ? 1.f : progress);
    }

    float EaseStandard(float t) noexcept
    {
        t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
        return t * t * (3.f - 2.f * t);
    }

    float EaseOut(float t) noexcept
    {
        t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
        const float inv = 1.f - t;
        return 1.f - inv * inv * inv;
    }
}
