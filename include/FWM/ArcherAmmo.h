#pragma once

namespace FWM::ArcherAmmo
{
    [[nodiscard]] float ElementalChance(RE::FormID a_actor);
    void Install();
    void Refresh(float a_dt);
    void Clear();
    [[nodiscard]] bool Unlimited(RE::Actor* a_actor);
}
