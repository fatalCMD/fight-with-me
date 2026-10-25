#pragma once
namespace FWM::Berserks {
void Call(RE::Actor* actor,float seconds,int rank);
void Extend(RE::Actor* actor,float seconds);
void End();
void Tick(float dt);
void Clear();
}
