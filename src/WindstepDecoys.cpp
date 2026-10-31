#include "FWM/WindstepDecoys.h"
#include "FWM/Abilities.h"
#include "FWM/Log.h"
#include "FWM/Game.h"
#include <shared_mutex>
namespace FWM::WindstepDecoys {
namespace {
std::shared_mutex gate;
struct Decoy { RE::NiPointer<RE::Actor> actor; std::vector<RE::ActorHandle> enemies; };
struct Spare { RE::ActorHandle actor; RE::FormID id{}; };
std::unordered_map<RE::FormID,Decoy> active;
// Spent echoes are disabled and reused, never deleted: a deleted actor leaves its cell while
// the engine's movement job still updates it, and that job crashes on the missing cell.
std::vector<Spare> spares;
std::array<RE::TESNPC*,2> bases{}; // Two reusable templates; never alter a stock NPC.
std::array<RE::FormID,2> baseIDs{};
RE::TESNPC* Base(bool female){
 const auto index=female?1u:0u;auto& base=bases[index];
 // A load can purge dynamic forms; never dereference a cached pointer after that.
 if(base&&RE::TESForm::LookupByID<RE::TESNPC>(baseIDs[index])==base)return base;
 base=nullptr;
 auto* data=RE::TESDataHandler::GetSingleton();
 // Stock script-free EncBandit01MissileNordM/F, used only for valid humanoid anatomy.
 auto* stock=data?data->LookupForm<RE::TESNPC>(female?0x37BFF:0x37C00,"Skyrim.esm"):nullptr;
 if(!stock)return nullptr;
 auto* copy=stock->CreateDuplicateForm(false,nullptr);base=copy?copy->As<RE::TESNPC>():nullptr;
 if(!base||base==stock){base=nullptr;return nullptr;}
 baseIDs[index]=base->GetFormID();
 base->formFlags|=RE::TESForm::RecordFlags::kTemporary;
 base->fullName="Windstep Echo";base->shortName="Windstep Echo";
 base->baseTemplateForm=nullptr;base->actorData.templateUseFlags=RE::ACTOR_BASE_DATA::TEMPLATE_USE_FLAG::kNone;
 using F=RE::ACTOR_BASE_DATA::Flag;
 base->actorData.actorBaseFlags=F::kInvulnerable;
 base->actorData.actorBaseFlags.set(F::kDoesntBleed,F::kNoActivation,F::kNoRumors,F::kDoesntAffectStealthMeter);
 if(female)base->actorData.actorBaseFlags.set(F::kFemale);
 base->actorData.healthOffset=10000;base->actorData.level=1;
 base->factions.clear();
 if(auto* faction=data->LookupForm<RE::TESFaction>(0xDB1,"Skyrim.esm"))base->factions.push_back({faction,0}); // PlayerFaction, not CurrentFollowerFaction
 static_cast<RE::TESContainer*>(base)->ClearDataComponent();static_cast<RE::TESSpellList*>(base)->ClearDataComponent();static_cast<RE::TESAIForm*>(base)->ClearDataComponent();
 base->defaultOutfit=nullptr;base->sleepOutfit=nullptr;base->defaultPackList=nullptr;
 base->deathItem=nullptr;base->crimeFaction=nullptr;base->voiceType=nullptr;
 base->SetAggressionLevel(RE::ACTOR_AGGRESSION::kUnaggressive);
 base->SetAssistanceLevel(RE::ACTOR_ASSISTANCE::kHelpsNobody);
 return base;
}
// Moves a spare of this body straight into the live set, so Is() never lapses in between.
RE::NiPointer<RE::Actor> TakeSpare(const RE::TESNPC* base){
 std::unique_lock lock(gate);
 for(auto it=spares.begin();it!=spares.end();){
  auto actor=it->actor.get();
  if(!actor||actor->IsMarkedForDeletion()){it=spares.erase(it);continue;} // a load or a purged cell took it
  if(actor->GetActorBase()!=base){++it;continue;}
  spares.erase(it);active.emplace(actor->GetFormID(),Decoy{actor,{}});return actor;
 }
 return {};
}
}
RE::ActorHandle Create(RE::Actor* caster,const RE::NiPoint3& at){
 if(!caster||!caster->GetParentCell()||!caster->GetParentCell()->IsAttached())return {};
 const auto* npc=caster->GetActorBase();auto* base=Base(npc&&npc->IsFemale());if(!base)return {};
 const RE::NiPoint3 angle{0,0,caster->GetAngleZ()};
 auto actor=TakeSpare(base);const bool reused=static_cast<bool>(actor);
 if(reused){
  // Still disabled where its last echo faded, possibly in another cell: move it before it loads again.
  actor->MoveTo(caster);actor->SetPosition(at,true);actor->Enable(false);
 }else{
  const auto made=RE::TESDataHandler::GetSingleton()->CreateReferenceAtLocation(base,at,angle,caster->GetParentCell(),caster->GetWorldspace(),nullptr,nullptr,RE::ObjectRefHandle(),false,true);
  auto ref=made.get();actor.reset(ref?ref->As<RE::Actor>():nullptr);
  if(!actor){if(ref){ref->Disable();ref->SetDelete(true);}return {};}
  actor->SetTemporary();
  {std::unique_lock lock(gate);active.emplace(actor->GetFormID(),Decoy{actor,{}});}
 }
 actor->SetActivationBlocked(true);actor->EnableAI(true);
 // Keep detection/actor processing alive; hide only rendered geometry. Restraint prevents attacks.
 actor->AsActorState()->actorState1.lifeState=RE::ACTOR_LIFE_STATE::kRestrained;actor->SetAlpha(1);
 if(auto* root=actor->Get3D())root->SetAppCulled(true);
 Log::Info("[Windstep] {} temporary target {:08X} at the departure point."sv,reused?"Reused":"Created",actor->GetFormID());
 return actor->GetHandle();
}
bool Engage(RE::Actor* enemy,RE::Actor* echo){
 if(!Game::Usable(enemy)||!Game::Usable(echo))return false;
 {std::shared_lock lock(gate);if(!active.contains(echo->GetFormID()))return false;} // a spare never takes enemies
 auto* controller=Game::Controller(enemy);if(!controller)return false;
 // StartCombat alone is not a sustained illusion: ordinary detection can forget an
 // invisible dummy and the selector then rejects it as absent from the combat group.
 bool known=false;
 if(auto* existing=controller->combatGroup){RE::BSReadLockGuard lock(existing->lock);known=std::ranges::any_of(existing->targets,[&](const auto& t){return t.targetHandle==echo->GetHandle();});}
 if(!known)enemy->StartCombat(echo);
 controller=Game::Controller(enemy);auto* group=controller?controller->combatGroup:nullptr;if(!group)return false;
 {
  RE::BSWriteLockGuard lock(group->lock);
  auto it=std::find_if(group->targets.begin(),group->targets.end(),[&](const auto& t){return t.targetHandle==echo->GetHandle();});
  if(it==group->targets.end()){
   RE::CombatTarget target{};target.targetHandle=echo->GetHandle();group->targets.push_back(target);it=group->targets.end()-1;
  }
  it->detectLevel=1000;it->stealthPoints=0;
  it->flags.set(RE::CombatTarget::Flags::kTargetKnown);it->flags.reset(RE::CombatTarget::Flags::kTargetLost);
  it->lastKnownLoc.pos=echo->GetPosition();it->lastKnownLoc.space=echo->GetParentCell()->IsInteriorCell()?static_cast<RE::TESForm*>(echo->GetParentCell()):static_cast<RE::TESForm*>(echo->GetWorldspace());
  it->lastKnownTimeStamp.timeStamp=RE::AITimer::QTimer();
 }
 {std::unique_lock lock(gate);auto it=active.find(echo->GetFormID());if(it!=active.end()&&std::find(it->second.enemies.begin(),it->second.enemies.end(),enemy->GetHandle())==it->second.enemies.end())it->second.enemies.push_back(enemy->GetHandle());}
 return true;
}
void Drop(RE::ActorHandle& handle){
 Abilities::DropHoldsOn(handle);
 Decoy owned;
 {std::unique_lock lock(gate);for(auto it=active.begin();it!=active.end();++it)if(it->second.actor&&it->second.actor->GetHandle()==handle){owned=std::move(it->second);active.erase(it);break;}}
 // Remove only our temporary target; original enemies and their battle stay intact.
 for(const auto& victim:owned.enemies)if(auto enemy=victim.get())if(auto* c=Game::Controller(enemy.get());c&&c->combatGroup){
  auto* group=c->combatGroup;RE::BSWriteLockGuard lock(group->lock);
  for(auto it=group->targets.begin();it!=group->targets.end();)if(it->targetHandle==handle)it=group->targets.erase(it);else ++it;
 }
 // Disable only and shelve it for the next Windstep; see spares.
 if(auto actor=handle.get();actor&&owned.actor){actor->StopCombat();actor->Disable();std::unique_lock lock(gate);spares.push_back({handle,actor->GetFormID()});}
 handle={};
}
bool Is(RE::FormID id){std::shared_lock lock(gate);return active.contains(id)||std::ranges::any_of(spares,[id](const Spare& s){return s.id==id;});}
}
