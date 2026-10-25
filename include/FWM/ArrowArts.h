#pragma once
namespace FWM::ArrowArts {
void VolleyWindow(RE::Actor* actor,int rank,float seconds);
void OnFlight(RE::ArrowProjectile* arrow);
void OnImpact(RE::ArrowProjectile* arrow);
void Wind(RE::Actor* actor,const RE::NiPoint3& origin,const RE::NiPoint3& direction,int rank);
bool Explosive(RE::Actor* actor,RE::Actor* target,const RE::NiPoint3& aim,int rank,float extraRadius,float extraDamage,bool debug);
// Called inside the existing native arrow-impact hook; queues work for the main thread.
void OnHit(RE::FormID projectile,const RE::TESHitEvent* event);
void Tick(float dt);
void Clear();
}
