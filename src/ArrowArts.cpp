#include "FWM/PartyBalance.h"
#include "FWM/ArrowArts.h"
#include "FWM/ArrowArtsFx.h"
#include "FWM/Effects.h"
#include "FWM/EffectAudio.h"
#include "FWM/Enemies.h"
#include "FWM/Game.h"
#include "FWM/Loadouts.h"
#include "FWM/Log.h"
#include "FWM/Party.h"
#include "FWM/Posing.h"
#include "FWM/Settings.h"
#include "FWM/Specials.h"
#include <mutex>
namespace FWM::ArrowArts {
namespace {
using Kind=ArrowArtsFx::Kind;
struct Part {RE::NiPointer<RE::NiAVObject> node;std::vector<RE::BSShaderProperty*> shaders;};
struct Visual {std::uint32_t id{};RE::NiPointer<RE::BSTempEffectParticle> effect;std::array<Part,ArrowArtsFx::kParts> parts;RE::ActorHandle owner;RE::NiPoint3 at;RE::NiMatrix3 basis;Posing::Lamp lamp;Kind kind{};float age{},impact{-1},loading{};int rank{};std::size_t copy{};bool found{};};
struct Cast {RE::ActorHandle actor,target;RE::TESObjectCELL* cell{};RE::NiPoint3 aim;int rank{};float radius{},damage{};ArrowArtsFx::PrimeWindow prime;std::uint32_t visual{};bool debug{};};
struct Shot {RE::ProjectileHandle arrow;RE::ActorHandle actor;RE::TESObjectCELL* cell{};RE::NiPoint3 origin,aim;int rank{};float radius{},damage{},age{};std::uint32_t visual{};bool debug{};ArrowArtsFx::ImpactGate gate;};
struct Hit {Shot shot;RE::ActorHandle target;RE::NiPoint3 at;};
struct Window {int rank{};float left{};};
struct Gust {RE::ActorHandle actor;RE::NiPoint3 at,direction;int rank{};};
std::unordered_map<RE::FormID,Window> windows;std::unordered_set<RE::FormID> windSeen;std::vector<Gust> gusts;
std::vector<Visual> visuals;std::unordered_map<RE::FormID,Cast> casts;std::unordered_map<RE::FormID,Shot> shots;std::vector<Hit> hits;std::mutex shotLock;std::uint32_t next=1;
RE::NiMatrix3 Along(RE::NiPoint3 forward){
 RE::NiMatrix3 m;if(forward.Length()<1)return m;forward/=forward.Length();const RE::NiPoint3 up=std::abs(forward.z)>.95f?RE::NiPoint3{1,0,0}:RE::NiPoint3{0,0,1};auto right=forward.Cross(up);right/=right.Length();const auto vertical=right.Cross(forward);
 for(int i=0;i<3;++i){m.entry[i][0]=right[i];m.entry[i][1]=forward[i];m.entry[i][2]=vertical[i];}return m;
}
void Finish(Visual& v){Posing::Out(v.lamp);if(v.effect)v.effect->lifetime=0;}
void EndRune(std::uint32_t id,bool struck){for(auto& v:visuals)if(v.id==id&&v.impact<0)v.impact=struck?0:.25f;}
std::uint32_t Spawn(RE::Actor* actor,const RE::NiPoint3& at,const RE::NiMatrix3& basis,int rank,Kind kind){
 if(!actor||!Settings::Get().enabled||!Settings::Get().abilityEffects)return 0;
 Visual v;v.owner=actor->GetHandle();v.at=at;v.basis=basis;v.rank=rank;v.kind=kind;v.id=next++;
 for(std::size_t i=0;i<ArrowArtsFx::kCopies;++i){if(std::ranges::any_of(visuals,[i](const auto& x){return x.copy==i;}))continue;
  v.effect.reset(Effects::ArrowArt(actor,i,at,ArrowArtsFx::kPrimeSeconds+ArrowArtsFx::kFlightLimit+2));if(!v.effect)continue;v.copy=i;visuals.push_back(std::move(v));return visuals.back().id;
 }return 0;
}
bool SameSpace(RE::Actor* actor,RE::Actor* target){auto* a=actor->GetParentCell();auto* b=target->GetParentCell();return a&&b&&(a==b||(!a->IsInteriorCell()&&!b->IsInteriorCell()&&actor->GetWorldspace()==target->GetWorldspace()));}
bool Enemy(RE::Actor* actor,RE::Actor* target){return actor&&target&&!Party::IsPartyActor(target)&&!target->IsPlayerRef()&&(Enemies::Find(target->GetFormID())||actor->IsHostileToActor(target));}
// Trace from the actual collision point, so a wall impact cannot hurt actors behind it.
bool BlastVisible(RE::Actor* actor,const RE::NiPoint3& at,RE::Actor* target){
 auto* cell=actor->GetParentCell();auto* world=cell?cell->GetbhkWorld():nullptr;if(!world)return false;
 const float scale=RE::bhkWorld::GetWorldScale();if(scale<=0)return false;
 auto end=Game::HeadPosition(target);auto direction=end-at;const float length=direction.Length();if(length<2)return true;
 // Small offset clears rounding at the struck surface, not a wall's thickness.
 const auto from=at+direction*(1.f/length);
 RE::bhkPickData pick{};pick.rayInput.from={from.x*scale,from.y*scale,from.z*scale,0};pick.rayInput.to={end.x*scale,end.y*scale,end.z*scale,0};
 pick.rayInput.filterInfo.filter=static_cast<std::uint32_t>(RE::COL_LAYER::kLOS);world->PickObject(pick);
 if(pick.pickFailed||!pick.rayOutput.HasHit())return true;
 auto* body=pick.rayOutput.rootCollidable?pick.rayOutput.rootCollidable->GetOwner<RE::hkpWorldObject>():nullptr;
 return body&&body->GetUserData()==target;
}
void Explode(const Hit& hit){
 auto actor=hit.shot.actor.get(),centre=hit.target.get();if(!Game::Usable(actor.get())||actor->GetParentCell()!=hit.shot.cell)return;
 Log::Info("[Explosive Arrow] Impact at {:.0f}, {:.0f}, {:.0f} ({})."sv,hit.at.x,hit.at.y,hit.at.z,centre?"actor":"world");
 EndRune(hit.shot.visual,true);Effects::ArrowExplosion(centre?centre.get():actor.get(),hit.at,hit.shot.rank);
 EffectAudio::Begin(Soundscape::Kind::ExplosiveImpact,actor.get(),hit.at,1.5f,hit.shot.rank);
 if(hit.shot.debug)return;
 std::vector<RE::NiPointer<RE::Actor>> targets;if(centre)targets.push_back(centre);
 for(const auto& profile:Enemies::All()){auto a=profile.handle.get();if(a&&a!=centre)targets.push_back(std::move(a));}
 int count=0;
 for(const auto& target:targets){
  if(!Game::Usable(target.get())||!Enemy(actor.get(),target.get())||!SameSpace(actor.get(),target.get()))continue;
  const float distance=(Game::HeadPosition(target.get())-RE::NiPoint3{0,0,25}-hit.at).Length();if(distance>hit.shot.radius)continue;
  if(target!=centre&&!BlastVisible(actor.get(),hit.at,target.get()))continue;
  const float damage=ArrowArtsFx::Hurt(hit.shot.damage,distance,hit.shot.radius,Game::Current(target.get(),RE::ActorValue::kResistMagic));
  Game::Stagger(target.get(),.4f+.15f*static_cast<float>(hit.shot.rank),actor.get());
  if(damage>0)PartyBalance::Damage(actor.get(),target.get(),damage);++count;
 }
 Log::Info("[Explosive Arrow] {} detonates rank {}: {} enemies in {:.1f} m, {:.1f} base damage."sv,Game::Name(actor.get()),hit.shot.rank,count,hit.shot.radius/70.f,hit.shot.damage);
}
void Pose(Visual& v,float dt){
 auto* root=Posing::RootOf(v.effect.get());if(!root){v.loading+=dt;return;}
 if(!v.found){for(std::size_t i=0;i<v.parts.size();++i){auto& p=v.parts[i];p.node.reset(root->GetObjectByName(std::format("Part{:02}",i)));if(!p.node){v.loading+=dt;return;}p.shaders=Posing::ShadersOf(p.node.get());}v.found=true;}
 if(v.kind==Kind::Rune){auto actor=v.owner.get();if(!actor||!Game::Usable(actor.get())||actor->GetParentCell()!=v.effect->cell){v.impact=.7f;return;}
  const float a=actor->GetAngleZ();const RE::NiPoint3 forward{std::sin(a),std::cos(a),0};v.at=actor->GetPosition()-forward*48+RE::NiPoint3{0,0,100};v.basis=Along(forward);
 }
 const auto frame=ArrowArtsFx::At(v.kind,v.age,v.rank,v.impact);
 for(std::size_t i=0;i<v.parts.size();++i){const auto& p=frame.parts[i];auto& node=v.parts[i];RE::NiMatrix3 turn,tilt;
  // Rune and wind geometry are authored in XZ, with their face along the arrow direction.
  turn.MakeYRotation(-p.turn);
  Posing::PlaceInWorld(root,node.node.get(),v.at+v.basis*RE::NiPoint3{p.at[0],p.at[1],p.at[2]},v.basis*tilt*turn,std::max(.001f,p.scale));
  Posing::Paint(node.shaders,p.color,p.alpha);Posing::Refresh(node.node.get());
 }
 Posing::Shine(v.lamp,root,v.at,{.2f,1,.45f},frame.light,220);
 v.age+=dt;if(v.impact>=0)v.impact+=dt;
}
}
void VolleyWindow(RE::Actor* actor,int rank,float seconds){if(!actor)return;const std::scoped_lock guard(shotLock);windows[actor->GetFormID()]={rank,seconds};}
void OnFlight(RE::ArrowProjectile* arrow){
 if(!arrow||arrow->GetProjectileRuntimeData().livingTime>.1f)return;
 const auto& data=arrow->GetProjectileRuntimeData();auto ref=data.shooter.get();auto* actor=ref?ref->As<RE::Actor>():nullptr;if(!actor)return;
 const std::scoped_lock guard(shotLock);
 if(auto ready=casts.find(actor->GetFormID());ready!=casts.end()&&!shots.contains(arrow->GetFormID())){
  auto& c=ready->second;
  if(actor->GetParentCell()==c.cell&&c.prime.Claim(data.livingTime)){
   Shot shot;shot.arrow=RE::ProjectileHandle{arrow};shot.actor=c.actor;shot.cell=c.cell;shot.origin=arrow->GetPosition();shot.rank=c.rank;shot.radius=c.radius;shot.damage=c.damage;shot.visual=c.visual;shot.debug=c.debug;
   shots.emplace(arrow->GetFormID(),shot);casts.erase(ready);
   Log::Info("[Explosive Arrow] Primed native projectile {:08X} from {:08X}."sv,arrow->GetFormID(),actor->GetFormID());
  }
 }
 auto it=windows.find(actor->GetFormID());if(it==windows.end()||it->second.left<=0||windSeen.size()>1024||windSeen.contains(arrow->GetFormID()))return;
 windSeen.insert(arrow->GetFormID());gusts.push_back({actor->GetHandle(),arrow->GetPosition(),data.velocity,it->second.rank});
}
// Runs before native impact dispatch deletes projectile/terrain collision data.
void OnImpact(RE::ArrowProjectile* arrow){
 if(!arrow)return;
 const std::scoped_lock guard(shotLock);auto it=shots.find(arrow->GetFormID());if(it==shots.end())return;
 for(const auto* impact:arrow->GetProjectileRuntimeData().impacts){
  if(!impact||!it->second.gate.Resolve(true))continue;
  auto ref=impact->collidee.get();auto* target=ref?ref->As<RE::Actor>():nullptr;
  hits.push_back({it->second,target?target->GetHandle():RE::ActorHandle{},impact->desiredTargetLoc});break;
 }
}
void Wind(RE::Actor* actor,const RE::NiPoint3& origin,const RE::NiPoint3& direction,int rank){Spawn(actor,origin,Along(direction),rank,Kind::Wind);EffectAudio::Begin(Soundscape::Kind::Volley,actor,origin,.5f,rank);}
bool Explosive(RE::Actor* actor,RE::Actor* target,const RE::NiPoint3& aim,int rank,float extraRadius,float extraDamage,bool debug){
 if(!Game::Usable(actor)||(!debug&&!Game::Usable(target)))return false;
 const auto [bow,ammo]=debug?Specials::DebugRanged(actor):Loadouts::Ranged(actor);if(!bow||!ammo)return false;
 {const std::scoped_lock guard(shotLock);if(casts.contains(actor->GetFormID())||std::ranges::any_of(shots,[actor](const auto& p){return p.second.actor==actor->GetHandle();}))return false;}
 Cast c;c.actor=actor->GetHandle();c.target=target?target->GetHandle():RE::ActorHandle{};c.cell=actor->GetParentCell();c.aim=aim;c.rank=ArrowArtsFx::Rank(rank);c.radius=ArrowArtsFx::Radius(rank)+extraRadius;c.damage=ArrowArtsFx::Damage(rank)*(1+extraDamage/100);c.visual=Spawn(actor,actor->GetPosition(),{},rank,Kind::Rune);c.debug=debug;
 {const std::scoped_lock guard(shotLock);casts.emplace(actor->GetFormID(),c);}
 EffectAudio::Begin(Soundscape::Kind::Arrow,actor,actor->GetPosition(),.7f,c.rank);
 Log::Info("[Explosive Arrow] {} primes their next natural bow shot for {:.0f} s (rank {})."sv,Game::Name(actor),ArrowArtsFx::kPrimeSeconds,c.rank);
 return true;
}
void OnHit(RE::FormID projectile,const RE::TESHitEvent* event){
 if(!projectile||!event||!event->target||!event->cause||!event->projectile)return;
 auto* target=event->target->As<RE::Actor>();if(!target)return;
 const std::scoped_lock guard(shotLock);auto it=shots.find(projectile);if(it==shots.end())return;
 auto actor=it->second.actor.get();if(!actor||event->cause->GetFormID()!=actor->GetFormID())return;
 // Hostility and friendly-fire filtering run on the main thread. Only queue this projectile once.
 if(!it->second.gate.Resolve(true))return;
 auto arrow=it->second.arrow.get();auto at=arrow?arrow->GetPosition():target->GetPosition()+RE::NiPoint3{0,0,70};
 hits.push_back({it->second,target->GetHandle(),at});
}
void Tick(float dt){
 if(!std::isfinite(dt)||dt<=0)return;if(!Settings::Get().enabled){Clear();return;}
 std::vector<Hit> pending;std::vector<Gust> wind;std::vector<std::uint32_t> ended;
 {const std::scoped_lock guard(shotLock);pending.swap(hits);wind.swap(gusts);
  for(auto& [id,c]:casts)c.prime.Advance(dt);
  std::erase_if(casts,[&](const auto& pair){const auto& c=pair.second;auto actor=c.actor.get();
   if(!Game::Usable(actor.get())||actor->GetParentCell()!=c.cell||c.prime.Expired()||(!c.debug&&!actor->IsInCombat())){ended.push_back(c.visual);return true;}return false;
  });
  for(auto& [id,window]:windows)window.left-=dt;
  std::erase_if(windows,[](const auto& pair){return pair.second.left<=0;});if(windows.empty())windSeen.clear();
  for(auto& [id,shot]:shots){shot.age+=dt;if(shot.gate.spent)continue;auto arrow=shot.arrow.get();
   // Backup for world collisions which did not dispatch a TESHitEvent.
   if(arrow)for(const auto* impact:arrow->GetProjectileRuntimeData().impacts){if(!impact)continue;
    if(shot.gate.Resolve(true)){auto ref=impact->collidee.get();auto* target=ref?ref->As<RE::Actor>():nullptr;pending.push_back({shot,target?target->GetHandle():RE::ActorHandle{},impact->desiredTargetLoc});}break;
   }
  }
  std::erase_if(shots,[&](const auto& pair){const auto& shot=pair.second;auto arrow=shot.arrow.get();auto actor=shot.actor.get();
   if(shot.gate.spent)return true;
   if(!arrow||!actor||actor->GetParentCell()!=shot.cell||shot.age>ArrowArtsFx::kFlightLimit){ended.push_back(shot.visual);return true;}return false;
  });}
 for(const auto& g:wind)if(auto a=g.actor.get())Wind(a.get(),g.at,g.direction,g.rank);
 for(const auto id:ended)EndRune(id,false);for(const auto& hit:pending)Explode(hit);
 std::erase_if(visuals,[dt](Visual& v){if(!v.effect||!v.effect->cell||!v.effect->cell->IsAttached()||!Settings::Get().abilityEffects){Finish(v);return true;}Pose(v,dt);
  const bool done=v.kind==Kind::Rune?(v.impact>=ArrowArtsFx::kFlash||v.age>ArrowArtsFx::kPrimeSeconds+ArrowArtsFx::kFlightLimit):v.age>=ArrowArtsFx::kWindSeconds;
  if(done||v.loading>1){Finish(v);return true;}return false;
 });
}
void Clear(){for(auto& v:visuals)Finish(v);visuals.clear();const std::scoped_lock guard(shotLock);casts.clear();shots.clear();hits.clear();windows.clear();windSeen.clear();gusts.clear();}
}
