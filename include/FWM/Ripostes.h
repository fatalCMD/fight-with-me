#pragma once
namespace FWM::Ripostes {
void Install();
// Whether what they hold can block at all.
bool CanGuard(RE::Actor* fighter);
// True once their guards keep failing to come up; until the fight ends they only riposte when told.
bool Benched(RE::FormID id);
bool Guard(RE::Actor* fighter,int rank,float seconds);
bool Guarding(RE::FormID id);
void Stop(RE::FormID id);
bool Counter(RE::Actor* fighter,RE::Actor* enemy,int rank,float weaponDamage,float counter,bool boss);
void Tick(float dt);
void Clear();
}
