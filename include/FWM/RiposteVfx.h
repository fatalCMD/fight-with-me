#pragma once
namespace FWM::RiposteVfx {
void Hit(RE::Actor* actor,const RE::NiPoint3& at,int rank,bool ground);
void Tick(float dt);
void Clear();
}
