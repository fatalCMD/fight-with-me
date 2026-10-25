#include "FWM/RiposteVfx.h"
#include "FWM/RiposteFx.h"
#include "FWM/Effects.h"
#include "FWM/Posing.h"
#include "FWM/Settings.h"
#include "FWM/Log.h"
namespace FWM::RiposteVfx {
namespace {
struct Part { RE::NiPointer<RE::NiAVObject> node; std::vector<RE::BSShaderProperty*> shaders; };
struct Burst { RE::NiPointer<RE::BSTempEffectParticle> effect; std::array<Part,RiposteFx::kParts> parts; RE::NiPoint3 at; Posing::Lamp lamp; float age{},loading{}; int rank{}; bool ground{}; std::size_t copy{}; bool found{}; };
std::vector<Burst> hits;
bool Find(Burst& hit){
 auto* root=Posing::RootOf(hit.effect.get());if(!root)return false;
 for(std::size_t i=0;i<hit.parts.size();++i){
  const auto name=i==0?std::string("Flash"):i<3?std::format("Ring{}",i-1):std::format("Spark{:02}",i-3);
  hit.parts[i].node.reset(root->GetObjectByName(name));if(!hit.parts[i].node)return false;
 }
 for(auto& p:hit.parts)p.shaders=Posing::ShadersOf(p.node.get());
 hit.found=true;return true;
}
void Pose(Burst& hit){
 auto* root=Posing::RootOf(hit.effect.get());if(!root||!hit.found)return;
 const auto look=RiposteFx::At(hit.age,hit.rank,hit.ground);RE::NiMatrix3 still; if(!hit.ground) still.MakeXRotation(1.57079633f);
 for(std::size_t i=0;i<hit.parts.size();++i){auto& p=hit.parts[i];const auto& v=look.parts[i];
  Posing::PlaceInWorld(root,p.node.get(),hit.at+RE::NiPoint3{v.at[0],v.at[1],v.at[2]},still,std::max(v.scale,.001f));
  Posing::Paint(p.shaders,v.color,v.alpha);Posing::Refresh(p.node.get());
 }
 Posing::Shine(hit.lamp,root,hit.at,look.parts[0].color,look.light,140.0f);
}
void Finish(Burst& hit){Posing::Out(hit.lamp);if(hit.effect)hit.effect->lifetime=0;}
}
void Hit(RE::Actor* target,const RE::NiPoint3& at,int rank,bool ground){
 if(!target||!target->Is3DLoaded()||!Settings::Get().enabled||!Settings::Get().abilityEffects)return;
 Burst hit;hit.rank=std::clamp(rank,1,3);hit.at=at;hit.ground=ground;
 for(std::size_t copy=0;copy<RiposteFx::kCopies;++copy){
  if(std::ranges::any_of(hits,[copy](const Burst& h){return h.copy==copy;}))continue;
  hit.effect.reset(Effects::RiposteStrike(target,copy,hit.at,2.0f));if(!hit.effect)continue;
  hit.copy=copy;if(Find(hit))Pose(hit);hits.push_back(std::move(hit));return;
 }
 Effects::Sparks(target);
}
void Tick(float dt){
 dt=std::isfinite(dt)?std::max(dt,0.0f):0;
 std::erase_if(hits,[dt](Burst& hit){
  if(!Settings::Get().enabled||!Settings::Get().abilityEffects||!hit.effect||!hit.effect->cell||!hit.effect->cell->IsAttached()||hit.effect->age>=hit.effect->lifetime){Finish(hit);return true;}
  // A short impact starts its clock only when loaded, so async model loading cannot eat the flash.
  if(!hit.found&&!Find(hit)){hit.loading+=dt;if(hit.loading>1){Log::Warn("[Look] Riposte strike mesh did not load."sv);Finish(hit);return true;}return false;}
  Pose(hit);hit.age+=dt;if(hit.age>RiposteFx::kSeconds){Finish(hit);return true;}return false;
 });
}
void Clear(){for(auto& hit:hits)Finish(hit);hits.clear();}
}
