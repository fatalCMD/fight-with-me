#pragma once
#include <array>
#include <cmath>

namespace FWM::DebugAbility
{
    inline constexpr float kAimDistance = 700.0f; // 70 Skyrim units per metre: ten metres.
    inline std::array<float, 3> Aim(float x, float y, float z, float heading)
    {
        return { x + std::sin(heading) * kAimDistance, y + std::cos(heading) * kAimDistance, z };
    }
}
