#include "FWM/AzurasMoon.h"
#include "FWM/MoonFx.h"
#include "FWM/Effects.h"
#include "FWM/Posing.h"
#include "FWM/Party.h"
#include "FWM/AbilityAuto.h"
#include "FWM/Settings.h"
#include "FWM/Log.h"
namespace FWM::AzurasMoon {
namespace {
struct Blessing {RE::ActorHandle actor;float left{},length{},chance{},age{};int rank{};std::uint32_t prevented{};};
std::mutex lock;
std::unordered_map<RE::FormID,Blessing> blessings;
struct Roll {double at{};bool negate{};};
std::unordered_map<std::uint64_t,Roll> rolls;
double now{};bool interception{};
std::mt19937 random{std::random_device{}()};
struct Part {RE::NiPointer<RE::NiAVObject> node;std::vector<RE::BSShaderProperty*> shaders;};
struct Look {
 RE::ActorHandle actor;RE::NiPointer<RE::BSTempEffectParticle> effect;
 std::array<Part,MoonFx::kParts> parts;RE::NiPoint3 origin;RE::NiMatrix3 facing;
 Posing::Lamp lamp;float age{},radius{};int rank{};std::size_t copy{};bool aura{},found{};
};
std::vector<Look> looks;
struct Pulse {RE::ActorHandle caster;RE::NiPoint3 origin;RE::TESObjectCELL* cell{};float age{},radius{},duration{};int rank{};std::vector<RE::ActorHandle> reached;};
std::vector<Pulse> pulses;
bool Ally(RE::Actor* actor){return actor&&(actor->IsPlayerRef()||Party::IsPartyActor(actor));}
bool Valid(RE::Actor* actor){return Ally(actor)&&!actor->IsDead()&&!actor->IsDisabled()&&actor->Is3DLoaded();}
void Finish(Look& look){Posing::Out(look.lamp);if(look.effect)look.effect->lifetime=0;}
void Visual(RE::Actor* actor,int rank,bool aura,float radius,float seconds){
 if(!Settings::Get().enabled||!Settings::Get().abilityEffects)return;
 if(aura&&std::ranges::any_of(looks,[&](const Look& l){return l.aura&&l.actor==actor->GetHandle();}))return;
 for(std::size_t copy=0;copy<MoonFx::kCopies;++copy){
  if(std::ranges::any_of(looks,[copy](const Look& l){return l.copy==copy;}))continue;
  Look l;l.actor=actor->GetHandle();l.origin=actor->GetPosition();l.facing.MakeZRotation(actor->GetAngleZ());l.rank=rank;l.aura=aura;l.radius=radius;l.copy=copy;
  l.effect.reset(Effects::Moon(actor,copy,l.origin,seconds+2));if(!l.effect)continue;
  looks.push_back(std::move(l));return;
 }
 Log::Warn("[Azura] No free blessing mesh; protection still applies."sv);
}
void Grant(RE::Actor* actor,const Pulse& pulse){
 {const std::scoped_lock guard(lock);auto& b=blessings[actor->GetFormID()];b.actor=actor->GetHandle();
  // Refresh strongest protection and longest remaining duration; never add probabilities.
  b.left=std::max(b.left,pulse.duration);b.length=b.left;b.chance=std::max(b.chance,MoonFx::Chance(pulse.rank));b.rank=std::max(b.rank,pulse.rank);
 }
 Visual(actor,pulse.rank,true,0,pulse.duration);
 Log::Info("[Azura] Blessed {}: rank {}, {:.0f}% damage-negation chance for {:.0f} s."sv,actor->GetName(),pulse.rank,MoonFx::Chance(pulse.rank)*100,pulse.duration);
}
}
Status On(RE::Actor* actor){
 if(!actor)return {};const std::scoped_lock guard(lock);const auto it=blessings.find(actor->GetFormID());
 if(it==blessings.end())return {};return {it->second.left,it->second.length,it->second.chance};
}
void SetInterception(bool ready){interception=ready;}
bool Cast(RE::Actor* caster,int rank,float reachBonus,float durationBonus,bool asked){
 if(!interception||!Valid(caster)||!Settings::Get().enabled)return false;
 if(std::ranges::any_of(pulses,[&](const Pulse& p){return p.caster==caster->GetHandle();}))return false;
 if(!asked){const std::scoped_lock guard(lock);const auto it=blessings.find(caster->GetFormID());if(it!=blessings.end()&&it->second.left>3)return false;}
 rank=std::clamp(rank,1,3);Pulse p;p.caster=caster->GetHandle();RE::NiMatrix3 facing;facing.MakeZRotation(caster->GetAngleZ());p.origin=caster->GetPosition()+facing*RE::NiPoint3{0,-150,0};p.cell=caster->GetParentCell();p.rank=rank;
 p.radius=MoonFx::Radius(rank)+std::clamp(reachBonus,0.0f,420.0f);p.duration=MoonFx::Duration(rank)+std::clamp(durationBonus,0.0f,6.0f);
 Visual(caster,rank,false,p.radius,MoonFx::kApparition);pulses.push_back(std::move(p));return true;
}
bool Negate(RE::Actor* attacker,RE::Actor* target,float damage){
 if(!attacker||!target||attacker==target||!std::isfinite(damage)||damage<=0||!Settings::Get().enabled)return false;
 const std::scoped_lock guard(lock);const auto it=blessings.find(target->GetFormID());
 if(it==blessings.end()||it->second.left<=0||it->second.actor!=target->GetHandle()||!attacker->IsHostileToActor(target))return false;
 // Components of one hit (weapon + enchantment/dual effect) share a roll in the same 50 ms window.
 const auto key=(static_cast<std::uint64_t>(attacker->GetFormID())<<32)|target->GetFormID();
 const auto prior=rolls.find(key);if(prior!=rolls.end()&&now-prior->second.at<.05)return prior->second.negate;
 const bool ignore=MoonFx::Negates(it->second.chance,it->second.left,true,damage,std::generate_canonical<float,24>(random));
 rolls[key]={now,ignore};if(ignore)++it->second.prevented;return ignore;
}
void Tick(float dt){
 dt=std::isfinite(dt)?std::clamp(dt,0.0f,.25f):0;
 if(!Settings::Get().enabled){Clear();return;}
 {const std::scoped_lock guard(lock);now+=dt;
  std::erase_if(rolls,[](const auto& pair){return now-pair.second.at>1;});
  for(auto& [id,b]:blessings){b.left-=dt;b.age+=dt;}
  std::erase_if(blessings,[](const auto& pair){const auto& b=pair.second;auto actor=b.actor.get();
   const bool gone=b.left<=0||!Valid(actor.get());if(gone)Log::Info("[Azura] Blessing ended for {:08X}: {} hostile hits negated."sv,pair.first,b.prevented);return gone;
  });
 }
 std::erase_if(pulses,[dt](Pulse& p){
  p.age+=dt;auto caster=p.caster.get();if(!caster||!p.cell||!p.cell->IsAttached()||caster->GetParentCell()!=p.cell)return true;
  if(p.age>=MoonFx::kRelease){
   std::vector<RE::Actor*> allies;auto* player=RE::PlayerCharacter::GetSingleton();if(player)allies.push_back(player);
   std::vector<RE::NiPointer<RE::Actor>> held;
   for(const auto& member:Party::Members()){auto a=member.handle.get();if(a){allies.push_back(a.get());held.push_back(std::move(a));}}
   for(auto* actor:allies){if(!Valid(actor)||actor->GetParentCell()!=p.cell||std::ranges::find(p.reached,actor->GetHandle())!=p.reached.end())continue;
    const float distance=(actor->GetPosition()-p.origin).Length();
    if(distance<=MoonFx::Wave(p.age,p.radius)&&AbilityAuto::Visible(caster.get(),actor,p.radius+160)){
     Grant(actor,p);p.reached.push_back(actor->GetHandle());
    }
   }
  }
  return p.age>=MoonFx::kRelease+MoonFx::kTravel;
 });
 std::erase_if(looks,[dt](Look& l){
  l.age+=dt;auto actor=l.actor.get();
  if(!Valid(actor.get())||!Settings::Get().abilityEffects||!l.effect||!l.effect->cell||!l.effect->cell->IsAttached()||actor->GetParentCell()!=l.effect->cell){Finish(l);return true;}
  MoonFx::Frame frame;
  if(l.aura){const std::scoped_lock guard(lock);const auto it=blessings.find(actor->GetFormID());if(it==blessings.end()){Finish(l);return true;}
   const auto& b=it->second;frame=MoonFx::Aura(b.age,b.left,b.rank);l.effect->lifetime=l.effect->age+b.left+1;
  }else{if(l.age>MoonFx::kApparition){Finish(l);return true;}frame=MoonFx::At(l.age,l.rank,l.radius);}
  auto* root=Posing::RootOf(l.effect.get());if(!root)return false;
  if(!l.found){for(std::size_t i=0;i<l.parts.size();++i){auto& part=l.parts[i];part.node.reset(root->GetObjectByName(MoonFx::Name(i)));if(!part.node){Finish(l);return true;}part.shaders=Posing::ShadersOf(part.node.get());}l.found=true;}
  for(std::size_t i=0;i<l.parts.size();++i){auto& part=l.parts[i];const auto& p=frame.parts[i];const bool visible=l.aura?(i>=6&&i<66):(i<6||i>=66);
   RE::NiMatrix3 yaw,tilt;yaw.MakeZRotation(-p.turn);tilt.MakeXRotation(-p.tilt);
   const RE::NiPoint3 offset{p.at[0],p.at[1],p.at[2]};const auto origin=l.aura?actor->GetPosition():l.origin;
   Posing::PlaceInWorld(root,part.node.get(),origin+l.facing*offset,l.facing*yaw*tilt,p.scale);
   Posing::Paint(part.shaders,p.color,visible?p.alpha:0);Posing::Drift(part.shaders,p.drift);part.node->SetAppCulled(!visible||p.alpha<=0);
  }
  if(!l.aura)Posing::Shine(l.lamp,root,l.origin+RE::NiPoint3{0,0,170},frame.parts[0].color,frame.light,300);
  Posing::Refresh(root);return false;
 });
}
void Clear(){for(auto& l:looks)Finish(l);looks.clear();pulses.clear();const std::scoped_lock guard(lock);blessings.clear();rolls.clear();now=0;}
}
