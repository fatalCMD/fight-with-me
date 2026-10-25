#pragma once

namespace FWM::PartyBalance
{
    // Paths owns this feature in paired and standalone setups. No persistent actor-value changes.
    void Update(float dt);
    void Clear();
    [[nodiscard]] float Multiplier(RE::Actor* attacker, RE::Actor* target);
    // Direct ability damage carries its source explicitly; normal attacks use the engine hooks.
    float Damage(RE::Actor* attacker, RE::Actor* target, float amount);
}
