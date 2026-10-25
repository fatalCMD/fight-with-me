#pragma once
namespace FWM::Windsteps {
bool Call(RE::Actor* actor,int rank,float extraSeconds,bool fleet,bool debug);
void Tick(float dt);
void Clear();
// Published snapshots: safe in physics/damage callbacks without actor lookup.
RE::hkVector4 Velocity(RE::bhkCharacterController* controller,const RE::hkVector4& incoming);
float DamageFactor(RE::FormID actor);
}
