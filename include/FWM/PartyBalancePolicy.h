#pragma once
#include <algorithm>
#include <cmath>

namespace FWM::PartyBalancePolicy
{
    inline float Share(int followers, int enemies, float extra = 5.0f, float advantage = 10.0f, float minimum = 50.0f)
    {
        if (followers <= 1) return 1.0f;
        if (!std::isfinite(extra) || !std::isfinite(advantage) || !std::isfinite(minimum)) return 1.0f;
        const float floor = std::clamp(minimum, 10.0f, 100.0f) / 100.0f;
        const int excess = std::max(0, followers - std::max(1, enemies));
        return std::clamp(1.0f - (std::clamp(extra, 0.0f, 25.0f) * static_cast<float>(followers - 1) +
            std::clamp(advantage, 0.0f, 25.0f) * static_cast<float>(excess)) / 100.0f, floor, 1.0f);
    }

    inline float Ease(float current, float target, float dt)
    {
        if (!std::isfinite(current) || !std::isfinite(target)) return 1.0f;
        const float step = std::isfinite(dt) ? std::clamp(dt, 0.0f, 1.0f) * 0.15f : 0.0f;
        return std::clamp(current + std::clamp(target - current, -step, step), 0.1f, 1.0f);
    }
}
