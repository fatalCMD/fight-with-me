#pragma once
namespace FWM::AzurasMoon {
struct Status {float left{},length{},chance{};};
Status On(RE::Actor* actor);
void SetInterception(bool ready);
bool Cast(RE::Actor* caster,int rank,float reachBonus,float durationBonus,bool asked);
bool Negate(RE::Actor* attacker,RE::Actor* target,float damage);
void Tick(float dt);
void Clear();
}
