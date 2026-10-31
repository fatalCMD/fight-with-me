#include "FWM/PartyBalance.h"
#include "FWM/Ripostes.h"
#include "FWM/RiposteFx.h"
#include "FWM/RiposteVfx.h"
#include "FWM/RiposteGuard.h"
#include "FWM/Freeze.h"
#include "FWM/Abilities.h"
#include "FWM/EffectAudio.h"
#include "FWM/Enemies.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Paths.h"
#include "FWM/Party.h"
#include "FWM/PathsLink.h"
#include "FWM/Settings.h"
#include "FWM/Text.h"
namespace FWM::Ripostes {
namespace {
constexpr auto source=static_cast<std::uint16_t>(Paths::Perk::kRiposte);
constexpr std::uint32_t guardAnchor=1;
struct GuardState {RE::ActorHandle actor;RE::TESObjectCELL* cell{};float left{},retry{};RiposteGuard::Watch watch;Role role{Role::kFighter};RE::FormID id{};};
struct Fall {RE::ActorHandle enemy,by;RE::TESObjectCELL* cell{};RE::NiPoint3 previous{},velocity{};float age{},settled{},weapon{},counter{};int rank{},pushes{};bool launched{},boss{};};
std::vector<GuardState> guards;std::vector<Fall> falls;
std::unordered_map<RE::FormID,int> misses;  // guards in a row that never came up, this fight
RiposteGuard::Hand HandOf(RE::TESForm* held){
 using Hand=RiposteGuard::Hand;
 if(!held)return Hand::Empty;
 if(auto* armor=held->As<RE::TESObjectARMO>())return armor->IsShield()?Hand::Shield:Hand::Empty;
 if(auto* weapon=held->As<RE::TESObjectWEAP>()){
  if(weapon->IsBow()||weapon->IsCrossbow())return Hand::Bow;
  if(weapon->IsStaff())return Hand::Staff;
  return weapon->IsHandToHandMelee()?Hand::Empty:Hand::Melee;
 }
 if(held->Is(RE::FormType::Light))return Hand::Torch;
 if(held->Is(RE::FormType::Spell,RE::FormType::Scroll))return Hand::Spell;
 return Hand::Empty;
}
// Animation events can arrive on AI threads. Never read the main-thread guard
// vector there, or follow a stored actor pointer after an unload.
std::atomic<std::shared_ptr<const std::unordered_set<RE::FormID>>> guarded;
std::atomic<std::uint64_t> dodgeBlocks{},swingBlocks{};
void SetGuarded(RE::FormID id,bool value) {
 auto old=guarded.load(std::memory_order_acquire);
 auto next=std::make_shared<std::unordered_set<RE::FormID>>(old?*old:std::unordered_set<RE::FormID>{});
 if(value)next->insert(id);else next->erase(id);
 guarded.store(std::move(next),std::memory_order_release);
}
struct AnimationEvent {
 static bool Thunk(RE::IAnimationGraphManagerHolder* holder,const RE::BSFixedString& event) {
  if(const auto kind=RiposteGuard::Kind(event.c_str());kind!=RiposteGuard::Held::None) {
   const auto held=guarded.load(std::memory_order_acquire);
   auto* ref=static_cast<RE::TESObjectREFR*>(holder);
   if(held && held->contains(ref->GetFormID())){(kind==RiposteGuard::Held::Dodge?dodgeBlocks:swingBlocks).fetch_add(1,std::memory_order_relaxed);return false;}
  }
  return original(holder,event);
 }
 static inline REL::Relocation<decltype(Thunk)> original;
};
bool Usable(RE::Actor* a){return a&&a->Is3DLoaded()&&Game::Usable(a)&&!a->AsActorState()->IsBleedingOut();}
// The same pressure that called for the guard: an enemy within reach who is after them.
bool Threatened(RE::Actor* a){
 const auto handle=a->GetHandle();
 return std::ranges::any_of(Enemies::All(),[&](const auto& profile){auto enemy=profile.handle.get();
  return enemy&&Game::Usable(enemy.get())&&profile.target==handle&&Game::Distance(a,enemy.get())<450.f;});
}
RE::NiPoint3 Pelvis(RE::Actor* a){auto* root=a->Get3D();auto* node=root?root->GetObjectByName("NPC Pelvis [Pelv]"):nullptr;return node?node->world.translate:root?root->worldBound.center:a->GetPosition();}
void Lower(GuardState& g){
 SetGuarded(g.id,false);
 if(auto a=g.actor.get()){Freeze::ReleaseAnchor(a.get(),guardAnchor);if(a->Is3DLoaded())a->NotifyAnimationGraph("blockStop");}
}
void Damage(RE::Actor* by,RE::Actor* enemy,float value){PartyBalance::Damage(by,enemy,value);}
bool Floor(RE::Actor* a,const RE::NiPoint3& from,RE::NiPoint3& at){
 auto* cell=a->GetParentCell();auto* world=cell?cell->GetbhkWorld():nullptr;if(!world)return false;
 const float scale=RE::bhkWorld::GetWorldScale();RE::bhkPickData pick{};RE::CFilter own{};a->GetCollisionFilterInfo(own);
 const auto top=from+RE::NiPoint3{0,0,5},bottom=from-RE::NiPoint3{0,0,90};
 pick.rayInput.from={top.x*scale,top.y*scale,top.z*scale,0};pick.rayInput.to={bottom.x*scale,bottom.y*scale,bottom.z*scale,0};
 pick.rayInput.filterInfo.filter=static_cast<std::uint32_t>(RE::COL_LAYER::kLOS)|(own.GetSystemGroup()?own.GetSystemGroup()<<16:1u<<16);
 world->PickObject(pick);if(pick.pickFailed||!pick.rayOutput.HasHit())return false;
 at=top+(bottom-top)*pick.rayOutput.hitFraction;return true;
}
void Push(RE::Actor* a,const RE::NiPoint3& v){
 auto* cell=a->GetParentCell();auto* world=cell?cell->GetbhkWorld():nullptr;if(!world)return;
 const float s=RE::bhkWorld::GetWorldScale();const RE::hkVector4 vel(v.x*s,v.y*s,v.z*s,0);
 RE::BSWriteLockGuard lock(world->worldLock);
 RE::BSVisit::TraverseScenegraphCollision(a->Get3D(),[&](RE::bhkNiCollisionObject* o){
  if(auto* b=o&&o->body?netimmerse_cast<RE::bhkRigidBody*>(o->body.get()):nullptr)b->SetLinearVelocity(vel);
  return RE::BSVisit::BSVisitControl::kContinue;
 });
}
}
void Install(){
 static bool installed=false;if(installed)return;installed=true;
 // TESObjectREFR's fourth base is IAnimationGraphManagerHolder (+0x38).
 // Its slot 1 is NotifyAnimationGraph. Chain the existing handler for all
 // other events and all other NPCs; the player vtable is never hooked.
 REL::Relocation<std::uintptr_t> holder{RE::VTABLE_Character[3]};
 AnimationEvent::original=holder.write_vfunc(0x01,AnimationEvent::Thunk);
 Log::Info("[Riposte] Temporary NPC dodge guard installed."sv);
}
bool CanGuard(RE::Actor* fighter){
 return fighter&&RiposteGuard::CanGuard(HandOf(fighter->GetEquippedObject(false)),HandOf(fighter->GetEquippedObject(true)));
}
bool Benched(RE::FormID id){const auto it=misses.find(id);return it!=misses.end()&&it->second>=RiposteGuard::kMissesToBench;}
bool Guard(RE::Actor* fighter,int rank,float seconds){
 (void)rank;
 if(!Usable(fighter)||fighter->IsInRagdollState()||!CanGuard(fighter))return false;
 Stop(fighter->GetFormID());
 if(!fighter->AsActorState()->IsWeaponDrawn())fighter->DrawWeaponMagicHands(true);
 if(!fighter->IsAttacking() && fighter->AsActorState()->IsWeaponDrawn() && !fighter->NotifyAnimationGraph("blockStart"))return false;
 const auto* orders=Party::FindOrders(fighter->GetFormID());
 guards.push_back({fighter->GetHandle(),fighter->GetParentCell(),seconds,.2f,{},orders?orders->role:Role::kFighter,fighter->GetFormID()});
 SetGuarded(fighter->GetFormID(),true);Freeze::Anchor(fighter,guardAnchor);return true;
}
bool Guarding(RE::FormID id){return std::ranges::any_of(guards,[id](const GuardState& g){auto a=g.actor.get();return a&&a->GetFormID()==id;});}
void Stop(RE::FormID id){std::erase_if(guards,[id](GuardState& g){auto a=g.actor.get();if(!a||a->GetFormID()==id){Lower(g);return true;}return false;});}
bool Counter(RE::Actor* fighter,RE::Actor* enemy,int rank,float weaponDamage,float counter,bool boss){
 if(!Usable(fighter)||!Usable(enemy)||!Guarding(fighter->GetFormID())||enemy->GetParentCell()!=fighter->GetParentCell()||enemy->IsInRagdollState())return false;
 Stop(fighter->GetFormID());
 const auto at=(fighter->GetPosition()+enemy->GetPosition())*.5f+RE::NiPoint3{0,0,95};
 RiposteVfx::Hit(fighter,at,rank,false);EffectAudio::Begin(Soundscape::Kind::Riposte,fighter,at,1.4f,rank);
 Game::Stagger(enemy,.35f,fighter);fighter->NotifyAnimationGraph("attackStart");
 std::erase_if(falls,[enemy](const Fall& f){auto a=f.enemy.get();return a&&a->GetFormID()==enemy->GetFormID();});
 Fall f;f.enemy=enemy->GetHandle();f.by=fighter->GetHandle();f.cell=enemy->GetParentCell();f.rank=RiposteFx::Rank(rank);f.weapon=weaponDamage;f.counter=counter;f.boss=boss;
 falls.push_back(f);return true;
}
void Tick(float dt){
 if(!std::isfinite(dt)||dt<=0)return;dt=std::min(dt,.1f);
 if(const auto count=dodgeBlocks.exchange(0,std::memory_order_relaxed);count>0)Log::Info("[Riposte] Suppressed {} dodge attempts while guarding."sv,count);
 if(const auto count=swingBlocks.exchange(0,std::memory_order_relaxed);count>0)Log::Info("[Riposte] Held back {} swing(s) and guard drops while guarding."sv,count);
 std::erase_if(guards,[dt](GuardState& g){auto a=g.actor.get();g.left-=dt;g.retry-=dt;
  const auto* member=a?Party::Find(a->GetFormID()):nullptr;
  const auto* orders=a?Party::FindOrders(a->GetFormID()):nullptr;
  if(!Settings::Get().enabled||!Usable(a.get())||!member||!member->CanTakeRole()||!orders||orders->role!=g.role||a->GetParentCell()!=g.cell||a->IsInRagdollState()||g.left<=0||(!a->IsInCombat()&&!PathsLink::Unlimited(a->GetFormID()))){Lower(g);return true;}
  const bool blocking=a->IsBlocking(),busy=a->IsAttacking()||!a->AsActorState()->IsWeaponDrawn();
  if(!blocking&&!busy&&g.retry<=0){a->NotifyAnimationGraph("blockStart");g.retry=.2f;}
  // Announced once the guard is really up, so a guard that never comes is never called out.
  switch(g.watch.Advance(dt,blocking,busy,Threatened(a.get()))){
  case RiposteGuard::Watch::Step::Raised:
   misses.erase(g.id);
   Notices::Used(Trf("{} sets themselves to riposte.",member->name),Paths::Ability::kRiposte,Notices::Kind::kOrder);
   return false;
  case RiposteGuard::Watch::Step::NeverRaised:{
   const bool benched=++misses[g.id]>=RiposteGuard::kMissesToBench;
   Log::Info("[Riposte] {} never raised the guard; released{}."sv,Game::Name(a.get()),benched?", and no more ripostes on their own this fight":"");
   Lower(g);return true;}
  case RiposteGuard::Watch::Step::Dropped:
   Log::Info("[Riposte] {} cannot maintain the guard; released."sv,Game::Name(a.get()));Lower(g);return true;
  case RiposteGuard::Watch::Step::Alone:
   Log::Info("[Riposte] {} lowers the guard: nobody near is after them."sv,Game::Name(a.get()));Lower(g);return true;
  default:return false;
  }
 });
 std::erase_if(falls,[dt](Fall& f){auto enemy=f.enemy.get(),by=f.by.get();f.age+=dt;
  auto cancel=[&](){if(enemy)Abilities::DropSource(source,enemy->GetFormID());return true;};
  if(!Settings::Get().enabled||!Usable(enemy.get())||!Usable(by.get())||enemy->GetParentCell()!=f.cell||by->GetParentCell()!=f.cell||f.age>4)return cancel();
  if(f.age<RiposteFx::kCounterDelay)return false;
  if(!f.launched){
   if(Game::Distance(by.get(),enemy.get())>450)return cancel();
   Damage(by.get(),enemy.get(),f.weapon*f.counter);
   const auto low=enemy->GetBoundMin(),high=enemy->GetBoundMax();const float breadth=.25f*((high.x-low.x)+(high.y-low.y));
   // Huge and non-ragdollable enemies retain a stagger; they cannot receive a fictional floor impact.
   if(breadth>60||enemy->IsDead()){Game::Stagger(enemy.get(),.8f,by.get());return cancel();}
   auto* process=enemy->GetActorRuntimeData().currentProcess;if(!process)return cancel();
   process->KnockExplosion(enemy.get(),by->GetPosition(),.5f);
   RE::NiPoint3 out=enemy->GetPosition()-by->GetPosition();out.z=0;const float length=out.Length();
   f.velocity=length>1?out*(RiposteFx::PushSpeed(f.rank)/length):RE::NiPoint3{0,180,0};f.velocity.z=110;
   f.launched=true;f.previous=Pelvis(enemy.get());return false;
  }
  if(!enemy->IsInRagdollState()){if(f.age>.95f){Log::Info("[Riposte] {} resists knockdown; counter only."sv,Game::Name(enemy.get()));return cancel();}return false;}
  // Keep native gravity and collisions. Paralysis prevents get-up, not physics.
  Abilities::Keep(enemy->GetFormID(),RE::ActorValue::kParalysis,1,.4f,source);
  if(f.pushes<2){Push(enemy.get(),f.velocity);++f.pushes;f.previous=Pelvis(enemy.get());return false;}
  const auto pelvis=Pelvis(enemy.get());const float vertical=(pelvis.z-f.previous.z)/dt;f.previous=pelvis;
  RE::NiPoint3 ground;const bool nearFloor=Floor(enemy.get(),pelvis,ground)&&pelvis.z-ground.z<45;
  f.settled=nearFloor&&std::abs(vertical)<45?f.settled+dt:0;
  if(!nearFloor||!RiposteFx::Landed(f.age-RiposteFx::kCounterDelay,pelvis.z-ground.z,vertical,f.settled))return false;
  const float hold=RiposteFx::DownSeconds(f.rank)*(f.boss?.5f:1.f),damage=f.weapon*RiposteFx::ImpactMultiplier(f.rank);
  Abilities::Lift(enemy->GetFormID(),RE::ActorValue::kParalysis,1,hold,source);Damage(by.get(),enemy.get(),damage);
  RiposteVfx::Hit(enemy.get(),ground,f.rank,true);EffectAudio::Begin(Soundscape::Kind::RiposteImpact,by.get(),ground,1.3f,f.rank);
  Log::Info("[Riposte] {} hits the floor: {:.1f} damage, {:.1f} s down (rank {})."sv,Game::Name(enemy.get()),damage,hold,f.rank);return true;
 });
}
void Clear(){for(auto& g:guards)Lower(g);guards.clear();falls.clear();misses.clear();Abilities::DropSource(source);RiposteVfx::Clear();}
}
