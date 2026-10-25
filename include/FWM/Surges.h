#pragma once
namespace FWM::Surges {
void Call(RE::Actor* actor,float seconds,int rank);
void End();
void Tick(float dt);
void Clear();
}
