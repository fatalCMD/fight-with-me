#pragma once
namespace FWM::DragonBreaths
{
    bool Cast(RE::Actor* caster, RE::Actor* target, int rank, int element, int reachRank, int durationRank, const RE::NiPoint3* point = nullptr);
    void Tick(float dt);
    void Clear();
    bool Channeling(RE::Actor* caster);
}
