#include "FWM/Windsteps.h"
#include "FWM/WindstepFx.h"
#include "FWM/WindstepDecoys.h"
#include "FWM/Abilities.h"
#include "FWM/Effects.h"
#include "FWM/EffectAudio.h"
#include "FWM/Enemies.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/Party.h"
#include "FWM/Overrides.h"
#include "FWM/Posing.h"
#include "FWM/Settings.h"
#include <shared_mutex>
namespace FWM::Windsteps {
namespace {
struct Part {RE::NiPointer<RE::NiAVObject> node;std::vector<RE::BSShaderProperty*> shaders;};
struct Step {
 RE::ActorHandle actor,decoy;std::vector<RE::ActorHandle> attackers;RE::FormID id{};RE::TESObjectCELL* cell{};
 RE::NiPoint3 origin,direction;RE::NiMatrix3 facing;
 RE::NiPointer<RE::BSTempEffectParticle> effect;
 std::array<Part,WindstepFx::kParts> parts;
 float age{},duration{},reach{},retargetIn{},scale{1};int targetSamples{},onEcho{};bool spawned{};int rank{},copy{-1};bool found{},burst{},stopped{},debug{};
};
struct Motion {RE::NiPointer<RE::bhkCharacterController> controller;RE::NiPoint3 velocity;};
std::vector<Step> steps;
std::unordered_map<RE::FormID,float> retry;
std::shared_mutex gate;
std::unordered_map<const RE::bhkCharacterController*,Motion> motion;
std::unordered_map<RE::FormID,float> protection;
constexpr std::uint16_t kSpeedSource=static_cast<std::uint16_t>(Paths::Perk::kPinningShot);
bool Usable(RE::Actor* a){return Game::Usable(a)&&a->GetCharController()&&!a->AsActorState()->IsBleedingOut();}
bool CanMove(RE::Actor* a){return Usable(a)&&!a->IsInRagdollState()&&!a->AsActorState()->IsStaggered()&&!a->AsActorState()->IsFlying()&&!a->AsActorState()->IsSwimming();}
// These rays are conservative corridor/ground probes, followed by the engine's normal
// colliding character movement. Never warp an actor or disable gravity/collision.
std::optional<float> Ray(RE::Actor* a,const RE::NiPoint3& from,const RE::NiPoint3& to,RE::TESObjectREFR* target=nullptr){
 auto* cell=a->GetParentCell();auto* world=cell?cell->GetbhkWorld():nullptr;if(!world)return {};
 const float scale=RE::bhkWorld::GetWorldScale();if(scale<=0)return {};
 RE::bhkPickData p{};RE::CFilter own{};a->GetCollisionFilterInfo(own);
 p.rayInput.from={from.x*scale,from.y*scale,from.z*scale,0};p.rayInput.to={to.x*scale,to.y*scale,to.z*scale,0};
 p.rayInput.filterInfo.filter=static_cast<std::uint32_t>(RE::COL_LAYER::kLOS)|(own.GetSystemGroup()?own.GetSystemGroup()<<16:1u<<16);
 world->PickObject(p);
 if(target&&!p.pickFailed&&p.rayOutput.rootCollidable){const auto* hit=p.rayOutput.rootCollidable->GetOwner<RE::hkpWorldObject>();if(hit&&hit->GetUserData()==target)return 1.f;}
 return !p.pickFailed&&p.rayOutput.HasHit()?std::clamp(p.rayOutput.hitFraction,0.f,1.f):1.f;
}
float Corridor(RE::Actor* a,const RE::NiPoint3& origin,const RE::NiPoint3& dir,float reach){
 const RE::NiPoint3 side{-dir.y,dir.x,0};float safe=0,ground=origin.z;
 const float width=std::clamp(a->GetHeight()*.22f,24.f,40.f);
 for(float distance=28;distance<=reach+1;distance+=28){
  const auto at=origin+dir*distance;float nextGround=ground;
  for(float offset:{-width,0.f,width}){
   const auto lane=at+side*offset;
   const auto floor=Ray(a,lane+RE::NiPoint3{0,0,ground-at.z+26},lane+RE::NiPoint3{0,0,ground-at.z-50});
   if(!floor||*floor>=.999f)return safe;
   const float z=ground+26-*floor*76;
   if(!WindstepFx::SafeSample(true,z-ground,1))return safe;
   if(offset==0)nextGround=z;
   for(float height:{22.f,65.f,110.f}){
    const auto from=origin+dir*safe+side*offset+RE::NiPoint3{0,0,ground-origin.z+height};
    const auto hit=Ray(a,from,lane+RE::NiPoint3{0,0,z-at.z+height});
    if(!hit||*hit<.999f)return safe;
   }
  }
  // Avoid occupied destinations, including allies whom LOS rays can ignore.
  for(const auto& m:Party::Members())if(auto other=m.handle.get();other&&other.get()!=a&&(other->GetPosition()-at).Length()<width*2)return safe;
  if(auto* player=RE::PlayerCharacter::GetSingleton();player&&(player->GetPosition()-at).Length()<width*2)return safe;
  for(const auto& e:Enemies::All())if(auto enemy=e.handle.get();enemy&&(enemy->GetPosition()-at).Length()<width*2)return safe;
  safe=distance;ground=nextGround;
 }return safe;
}
void DropDecoy(Step& s){
 if(s.decoy){Log::Info("[Windstep] Echo ended for {:08X}; observed {}/{} attacker target samples on its echo."sv,s.id,s.onEcho,s.targetSamples);WindstepDecoys::Drop(s.decoy);}
}
void Finish(Step& s){DropDecoy(s);if(s.effect)s.effect->lifetime=0;Abilities::DropSource(kSpeedSource,s.id,RE::ActorValue::kSpeedMult);}
// Keep only the enemies that were pursuing this archer when it stepped away.
void Decoy(Step& s,RE::Actor* caster,float dt){
 if((s.debug&&!caster->IsInCombat())||s.age>=WindstepFx::VisualSeconds(s.rank)){DropDecoy(s);return;}
 if(s.age<WindstepFx::kTravel)return; // let the caster clear the physical target first
 if(!s.spawned){s.spawned=true;if(!s.attackers.empty())s.decoy=WindstepDecoys::Create(caster,s.origin);
  if(s.decoy)Log::Info("[Windstep] Holding {} original attackers on the echo for {:.1f} s."sv,s.attackers.size(),WindstepFx::VisualSeconds(s.rank)-s.age);
  if(!s.attackers.empty()&&!s.decoy)Log::Warn("[Windstep] Could not create the decoy: retreat remains active, enemy targeting is unchanged."sv);
 }
 auto echo=s.decoy.get();if(!echo)return;
 echo->SetAlpha(1);if(auto* root=echo->Get3D())root->SetAppCulled(true);
 if((echo->GetPosition()-s.origin).Length()>3)echo->SetPosition(s.origin,true);
 if(!Game::Usable(echo.get()))return; // wait for the actor's combat geometry to load
 s.retargetIn-=dt;if(s.retargetIn>0)return;s.retargetIn=.2f;
 for(const auto& handle:s.attackers){auto enemy=handle.get();
  if(!Game::Usable(enemy.get())||Party::IsPartyActor(enemy.get())||enemy->GetParentCell()!=s.cell||Game::Distance(enemy.get(),echo.get())>1400)continue;
  ++s.targetSamples;const auto target=Game::CombatTarget(enemy.get());if(target==echo)++s.onEcho;
  // A later explicit tank taunt may take priority, but the ordinary AI may not break the illusion.
  const auto held=Abilities::HolderOf(handle);if(held&&held!=s.decoy&&held!=s.actor)continue;
  if(WindstepDecoys::Engage(enemy.get(),echo.get()))Abilities::AddHold(handle,s.decoy,std::max(.05f,WindstepFx::VisualSeconds(s.rank)-s.age));
 }
}
bool Find(Step& s){auto* root=Posing::RootOf(s.effect.get());if(!root)return false;
 for(std::size_t i=0;i<s.parts.size();++i){auto& p=s.parts[i];p.node.reset(root->GetObjectByName(WindstepFx::Name(i)));if(!p.node)return false;p.shaders=Posing::ShadersOf(p.node.get());}s.found=true;return true;
}
void Pose(Step& s){if(!s.effect||(!s.found&&!Find(s)))return;auto* root=Posing::RootOf(s.effect.get());if(!root)return;
 const auto frame=WindstepFx::At(s.age,s.rank);
 RE::NiMatrix3 trail;trail.MakeZRotation(std::atan2(-s.direction.x,-s.direction.y));
 for(std::size_t i=0;i<s.parts.size();++i){auto& n=s.parts[i];const auto& p=frame.parts[i];RE::NiMatrix3 spin;spin.MakeZRotation(p.turn);
  auto local=RE::NiPoint3{p.at[0],p.at[1],p.at[2]};if(i>=3&&i<51)local.y*=s.reach/(WindstepFx::Reach(s.rank)*s.scale);
  const auto basis=i<3?s.facing:trail;Posing::PlaceInWorld(root,n.node.get(),s.origin+basis*(local*s.scale),basis*spin,p.scale*s.scale);
  Posing::Paint(n.shaders,p.color,p.alpha);if(i>=3)Posing::Drift(n.shaders,p.drift);n.node->SetAppCulled(p.alpha<=0);
 }Posing::Refresh(root);
}
void Burst(Step& s,RE::Actor* actor){s.burst=true;if(s.rank<3)return;
 EffectAudio::Begin(Soundscape::Kind::WindstepBurst,actor,s.origin,.9f,3);
 if(s.debug)return;
 int hit=0;for(const auto& profile:Enemies::All()){auto enemy=profile.handle.get();if(!Game::Usable(enemy.get())||Party::IsPartyActor(enemy.get())||(enemy->GetPosition()-s.origin).Length()>210)continue;
  const bool same=enemy->GetParentCell()==s.cell||(actor->GetWorldspace()&&actor->GetWorldspace()==enemy->GetWorldspace());if(!same)continue;
  const auto visible=Ray(actor,s.origin+RE::NiPoint3{0,0,60},enemy->GetPosition()+RE::NiPoint3{0,0,60},enemy.get());
  if(visible&&*visible>=.98f&&Game::Stagger(enemy.get(),.65f,actor))++hit;
 }Log::Info("[Windstep] Rank III echo scattered {} nearby enemies."sv,hit);
}
}
bool Call(RE::Actor* actor,int rank,float extraSeconds,bool fleet,bool debug){
 if(!CanMove(actor)||std::ranges::any_of(steps,[actor](const Step& s){return s.id==actor->GetFormID();}))return false;
 if(retry.contains(actor->GetFormID()))return false;
 Step s;s.actor=actor->GetHandle();s.id=actor->GetFormID();s.cell=actor->GetParentCell();s.origin=actor->GetPosition();s.rank=WindstepFx::Rank(rank);s.debug=debug;s.duration=WindstepFx::Duration(rank)+std::clamp(extraSeconds,0.f,3.f);
 s.facing.MakeZRotation(actor->GetAngleZ());s.scale=std::clamp(actor->GetHeight()/128.f,.7f,1.4f);
 RE::NiPoint3 away{-std::sin(actor->GetAngleZ()),-std::cos(actor->GetAngleZ()),0};float nearest=500;RE::NiPoint3 threat{};bool hasThreat=false;
 for(const auto& e:Enemies::All())if(auto enemy=e.handle.get();Game::Usable(enemy.get())&&!Party::IsPartyActor(enemy.get())){const float d=Game::Distance(actor,enemy.get());if(d<nearest){nearest=d;threat=enemy->GetPosition();hasThreat=true;}}
 if(hasThreat){away=s.origin-threat;away.z=0;away=away/std::max(away.Length(),1.f);}
 float best=-1;
 for(float angle:{0.f,.55f,-.55f,1.05f,-1.05f}){RE::NiMatrix3 turn;turn.MakeZRotation(angle);const auto direction=turn*away;const float reach=Corridor(actor,s.origin,direction,WindstepFx::Reach(rank));if(reach<112)continue;
  const auto end=s.origin+direction*reach;float clearance=hasThreat?(end-threat).Length():reach;
  for(const auto& e:Enemies::All())if(auto enemy=e.handle.get();Game::Usable(enemy.get()))clearance=std::min(clearance,(enemy->GetPosition()-end).Length());
  if(hasThreat&&clearance<nearest+56)continue;
  const float score=clearance+reach*.25f-std::abs(angle)*15;if(score>best){best=score;s.direction=direction;s.reach=reach;}
 }
 if(best<0){retry[s.id]=1.f;Log::Info("[Windstep] {} has no safe retreat corridor."sv,Game::Name(actor));return false;}
 if(!debug||actor->IsInCombat())for(const auto& profile:Enemies::All()){auto enemy=profile.handle.get();
  if(Game::Usable(enemy.get())&&!Party::IsPartyActor(enemy.get())&&enemy->GetParentCell()==s.cell&&Game::CombatTarget(enemy.get()).get()==actor&&Game::Distance(enemy.get(),actor)<=1400){
   bool unused=false;if(enemy->HasLineOfSight(actor,unused))s.attackers.push_back(profile.handle);
  }
 }
 for(std::size_t i=0;i<WindstepFx::kCopies;++i){if(std::ranges::any_of(steps,[i](const Step& v){return v.copy==static_cast<int>(i);}))continue;s.effect.reset(Effects::Windstep(actor,i,s.origin,WindstepFx::VisualSeconds(rank)+1));if(s.effect){s.copy=static_cast<int>(i);break;}}
 Abilities::Lift(s.id,RE::ActorValue::kSpeedMult,fleet?40.f:30.f,s.duration,kSpeedSource);
 actor->NotifyAnimationGraph("attackStop");
 EffectAudio::Begin(Soundscape::Kind::Windstep,actor,s.origin,1.2f,s.rank);
 Log::Info("[Windstep] {} retreats {:.1f} m; rank {}, {:.1f} s buff, {:.0f}% protection."sv,Game::Name(actor),s.reach/70,s.rank,s.duration,WindstepFx::Protection(rank)*100);
 {std::unique_lock lock(gate);protection[s.id]=1-WindstepFx::Protection(rank);}
 Pose(s);steps.push_back(std::move(s));return true;
}
void Tick(float dt){
 dt=std::isfinite(dt)?std::max(0.f,dt):0;
 for(auto& entry:retry)entry.second-=dt;
 std::erase_if(retry,[](const auto& pair){return pair.second<=0;});
 std::unordered_map<const RE::bhkCharacterController*,Motion> next;std::unordered_map<RE::FormID,float> protectedNow;
 std::erase_if(steps,[&](Step& s){auto actor=s.actor.get();s.age+=dt;
  if(!Settings::Get().enabled||!Settings::Get().specials||!Usable(actor.get())||actor->GetParentCell()!=s.cell||!s.cell->IsAttached()||s.age>=std::max(s.duration,WindstepFx::VisualSeconds(s.rank))){Finish(s);return true;}
  if(!s.debug){const auto* orders=Party::FindOrders(s.id);if(!orders||orders->role!=Role::kArcher||!PathsLink::KitOf(s.id,Role::kArcher).Carries(Paths::Ability::kPinningShot)){Finish(s);return true;}}
  if(s.age<WindstepFx::kTravel+.08f){
   if(!CanMove(actor.get())||s.age>=WindstepFx::kTravel)s.stopped=true;
   const float travelled=(actor->GetPosition()-s.origin).Dot(s.direction);const float remaining=s.reach-travelled;
   if(!s.stopped&&(remaining<5||Corridor(actor.get(),actor->GetPosition(),s.direction,56)<28))s.stopped=true;
   const float speed=s.stopped?0.f:std::min(WindstepFx::Speed(s.age,s.reach),std::max(0.f,remaining)/std::max(dt,.016f));
   auto* controller=actor->GetCharController();next.emplace(controller,Motion{RE::NiPointer<RE::bhkCharacterController>(controller),s.direction*(speed*RE::bhkWorld::GetWorldScale())});
  }
  if(s.age<s.duration)protectedNow[s.id]=1-WindstepFx::Protection(s.rank);
  Decoy(s,actor.get(),dt);
  if(auto echo=s.decoy.get();echo&&echo->GetCharController()){
   auto* body=echo->GetCharController();next.emplace(body,Motion{RE::NiPointer<RE::bhkCharacterController>(body),{0,0,0}});
  }
  if(!s.burst&&s.age>=WindstepFx::EchoHold(s.rank))Burst(s,actor.get());
  if(Settings::Get().abilityEffects&&s.age<WindstepFx::VisualSeconds(s.rank))Pose(s);else if(s.effect)s.effect->lifetime=0;
  return false;
 });
 {std::unique_lock lock(gate);motion.swap(next);protection.swap(protectedNow);}
}
RE::hkVector4 Velocity(RE::bhkCharacterController* c,const RE::hkVector4& incoming){std::shared_lock lock(gate);const auto it=motion.find(c);if(it==motion.end())return incoming;
 std::array<float,4> v;_mm_storeu_ps(v.data(),incoming.quad);return {it->second.velocity.x,it->second.velocity.y,v[2],v[3]};
}
float DamageFactor(RE::FormID id){if(WindstepDecoys::Is(id))return 0;std::shared_lock lock(gate);const auto i=protection.find(id);return i==protection.end()?1.f:i->second;}
void Clear(){for(auto& s:steps)Finish(s);steps.clear();retry.clear();std::unique_lock lock(gate);motion.clear();protection.clear();}
}
