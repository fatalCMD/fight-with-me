#include "FWM/Disarms.h"
#include "FWM/DisarmFx.h"
#include "FWM/Effects.h"
#include "FWM/Posing.h"
#include "FWM/Settings.h"
#include "FWM/Log.h"
namespace FWM::Disarms {
namespace {
struct Part { RE::NiPointer<RE::NiAVObject> node; std::vector<RE::BSShaderProperty*> shaders; };
struct Hit { RE::NiPointer<RE::BSTempEffectParticle> effect; std::array<Part,DisarmFx::kParts> parts; RE::NiPoint3 at; Posing::Lamp lamp; float age{},loading{}; int rank{}; std::size_t copy{}; bool found{}; };
std::vector<Hit> hits;
bool Find(Hit& hit){
 auto* root=Posing::RootOf(hit.effect.get());if(!root)return false;
 for(std::size_t i=0;i<hit.parts.size();++i){
  const auto name=i==0?std::string("Flash"):i<3?std::format("Slash{}",i-1):std::format("Spark{:02}",i-3);
  hit.parts[i].node.reset(root->GetObjectByName(name));if(!hit.parts[i].node)return false;
 }
 for(auto& p:hit.parts)p.shaders=Posing::ShadersOf(p.node.get());
 hit.found=true;return true;
}
void Pose(Hit& hit){
 auto* root=Posing::RootOf(hit.effect.get());if(!root||!hit.found)return;
 const auto look=DisarmFx::At(hit.age,hit.rank);const RE::NiMatrix3 still;
 for(std::size_t i=0;i<hit.parts.size();++i){auto& p=hit.parts[i];const auto& v=look.parts[i];
  Posing::PlaceInWorld(root,p.node.get(),hit.at+RE::NiPoint3{v.at[0],v.at[1],v.at[2]},still,std::max(v.scale,.001f));
  Posing::Paint(p.shaders,v.color,v.alpha);Posing::Refresh(p.node.get());
 }
 Posing::Shine(hit.lamp,root,hit.at,look.parts[0].color,look.light,140.0f);
}
void Finish(Hit& hit){Posing::Out(hit.lamp);if(hit.effect)hit.effect->lifetime=0;}
}
bool Strike(RE::Actor* target,int rank){
 if(!target||!target->Is3DLoaded()||!Settings::Get().enabled||!Settings::Get().abilityEffects)return false;
 auto* model=target->Get3D();auto* weapon=model->GetObjectByName("WEAPON");
 auto* hand=model->GetObjectByName("NPC R Hand [RHnd]");
 Hit hit;hit.rank=std::clamp(rank,1,3);
 hit.at=weapon?weapon->world.translate:hand?hand->world.translate:target->GetPosition()+RE::NiPoint3{0,0,85};
 // A drawn weapon's bound locates its visible blade, including modded weapon lengths.
 if(weapon&&weapon->worldBound.radius>1&&weapon->worldBound.radius<180&&
  (weapon->worldBound.center-hit.at).Length()<150)hit.at=weapon->worldBound.center;
 for(std::size_t copy=0;copy<DisarmFx::kCopies;++copy){
  if(std::ranges::any_of(hits,[copy](const Hit& h){return h.copy==copy;}))continue;
  hit.effect.reset(Effects::DisarmStrike(target,copy,hit.at,2.0f));if(!hit.effect)continue;
  hit.copy=copy;if(Find(hit))Pose(hit);hits.push_back(std::move(hit));return true;
 }
 Effects::Sparks(target);return false;
}
void Tick(float dt){
 dt=std::isfinite(dt)?std::max(dt,0.0f):0;
 std::erase_if(hits,[dt](Hit& hit){
  if(!Settings::Get().enabled||!Settings::Get().abilityEffects||!hit.effect||!hit.effect->cell||!hit.effect->cell->IsAttached()||hit.effect->age>=hit.effect->lifetime){Finish(hit);return true;}
  // A short impact starts its clock only when loaded, so async model loading cannot eat the flash.
  if(!hit.found&&!Find(hit)){hit.loading+=dt;if(hit.loading>1){Log::Warn("[Look] Disarm strike mesh did not load."sv);Finish(hit);return true;}return false;}
  Pose(hit);hit.age+=dt;if(hit.age>DisarmFx::kSeconds){Finish(hit);return true;}return false;
 });
}
void Clear(){for(auto& hit:hits)Finish(hit);hits.clear();}
}
