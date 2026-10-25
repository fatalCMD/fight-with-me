#pragma once

#include <algorithm>
#include <cmath>

namespace FWM::Archery
{
    struct Shot
    {
        int element{ -1 };
        bool resolved{ false };
        // One projectile, one outcome. Blocking consumes the imbuement too.
        [[nodiscard]] int Resolve(bool landedUnblocked)
        {
            if (resolved) return -1;
            resolved = true;
            return landedUnblocked ? element : -1;
        }
    };
    inline constexpr float kElementalDamage = 8.0f;

    // A projectile rolls once before impact. Non-finite input never grants an imbuement.
    [[nodiscard]] inline bool Imbued(float chance, float roll)
    {
        return std::isfinite(chance) && std::isfinite(roll) && roll >= 0 && roll < std::clamp(chance, 0.0f, 100.0f);
    }

    [[nodiscard]] inline float ElementalDamage(float magicResistance, float elementResistance)
    {
        if (!std::isfinite(magicResistance) || !std::isfinite(elementResistance)) return 0;
        const float magic = 1.0f - std::clamp(magicResistance, -100.0f, 100.0f) / 100.0f;
        const float element = 1.0f - std::clamp(elementResistance, -100.0f, 100.0f) / 100.0f;
        return kElementalDamage * magic * element;
    }
}
