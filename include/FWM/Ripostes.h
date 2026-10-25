#pragma once
namespace FWM::Ripostes {
void Install();
bool Guard(RE::Actor* fighter,int rank,float seconds);
bool Guarding(RE::FormID id);
void Stop(RE::FormID id);
bool Counter(RE::Actor* fighter,RE::Actor* enemy,int rank,float weaponDamage,float counter,bool boss);
void Tick(float dt);
void Clear();
}
