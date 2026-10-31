#include "FWM/Skald.h"
#include "RE/B/BSSoundInfo.h"
#include "RE/N/NiParticleSystem.h"
#include "RE/N/NiPSysData.h"
#include "FWM/SkaldPolicy.h"
#include "FWM/SkaldMusic.h"
#include "FWM/Abilities.h"
#include "FWM/AbilityLink.h"
#include "FWM/Arts.h"
#include "FWM/Effects.h"
#include "FWM/Enemies.h"
#include "FWM/Freeze.h"
#include "FWM/Game.h"
#include "FWM/Host.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Party.h"
#include "FWM/Posing.h"
#include "FWM/Settings.h"
#include "FWM/Text.h"
#include <bit>

namespace FWM::Skald {
namespace {
using Song=SkaldPolicy::Song;
using Perk=Paths::Perk;
constexpr std::uint32_t anchor=0x534B;
constexpr std::uint16_t speedSource=static_cast<std::uint16_t>(Perk::kFervor);
constexpr std::uint16_t slowSource=static_cast<std::uint16_t>(Perk::kLeadfootVerse);
constexpr std::uint16_t nerveSource=0x534B;
constexpr std::uint16_t noteSource=0x534C;
constexpr std::uint16_t offBeatSource=0x534D;
constexpr std::uint16_t crescendoSource=0x534E;
struct Boon {
    float until{}, strength{}, branchA{}, branchB{}, length{};
    std::uint64_t source{};
    unsigned stacks{},empowered{};
    bool inspirationSpent{};
};
struct Benefits { std::array<Boon,4> songs{}; float restoreAt{}; };
struct Reward { RE::ActorHandle actor,target; float fraction{}; bool magic{}; float amount{}; };
std::mutex gate;
std::unordered_map<RE::FormID,Benefits> benefits;
std::unordered_map<RE::FormID,float> shields;
std::unordered_set<RE::FormID> performing;
std::unordered_set<RE::FormID> interrupted,timid;
std::unordered_set<RE::FormID> tavernReady;
std::unordered_set<std::uint32_t> stanceHolders; // native handles, for the cast hook
float tavernScan{};
std::vector<Reward> rewards;
float now{};
std::uint64_t nextPerformance{};
std::mt19937 rng{std::random_device{}()};
thread_local bool ownEvent{};
struct OwnEvent {bool was{ownEvent};OwnEvent(){ownEvent=true;}~OwnEvent(){ownEvent=was;}};
struct Visual {
    RE::NiPointer<RE::BSTempEffectParticle> effect;
    RE::NiPointer<RE::NiAVObject> node;
    std::vector<RE::BSShaderProperty*> shaders;
    void Drop(){if(effect)effect->lifetime=0;effect.reset();node.reset();shaders.clear();}
    bool Find(const char* name) {
        auto* root=Posing::RootOf(effect.get());
        return FindAt(root,name);
    }
    bool FindAt(RE::NiAVObject* root,const char* name) {
        if(!root)return false;
        if(!node){node.reset(root->GetObjectByName(name));if(node)shaders=Posing::ShadersOf(node.get());}
        return node!=nullptr;
    }
};
// Every lute effect spawned here. Each is drawn only on a tick that placed it, and
// one whose owner let go of it without dropping it is put out (SweepLutes).
std::vector<RE::NiPointer<RE::BSTempEffectParticle>> luteEffects;
std::unordered_set<const RE::BSTempEffectParticle*> placedLutes;
RE::BSTempEffectParticle* Track(RE::BSTempEffectParticle* effect){if(effect)luteEffects.emplace_back(effect);return effect;}
void Placed(RE::BSTempEffectParticle* effect,RE::NiAVObject* root){placedLutes.insert(effect);root->SetAppCulled(false);}
struct Wave {
    float born{},radius{};
    std::size_t slot{};
    RE::NiPoint3 origin;
    std::unordered_map<RE::FormID,SkaldPolicy::WaveContact> contacts;
    bool playerBlocked{};
};
struct HitLight {
    RE::ActorHandle actor;
    RE::TESObjectCELL* cell{};
    float at{};
    Song song{};
    int rank{1};
    bool native{};
    Visual glow;
    std::array<Visual,SkaldPolicy::kHitMotes*SkaldPolicy::kHitBursts> motes;
    std::array<float,SkaldPolicy::kHitBursts> bursts{-100.f,-100.f};
    std::array<int,SkaldPolicy::kHitBursts> burstRanks{1,1};
    unsigned nextBurst{};
    Posing::Lamp lamp,bodyLamp;
    void Drop(){Posing::Out(lamp);Posing::Out(bodyLamp);for(auto& mote:motes)mote.Drop();glow.Drop();bursts.fill(-100.f);nextBurst=0;}
};
std::unordered_map<RE::FormID,HitLight> hitLights;
struct LjodhCloud {
    RE::ActorHandle actor;
    RE::TESObjectCELL* cell{};
    Effects::SkaldCloud kind{};
    RE::NiPointer<RE::BSTempEffectParticle> effect;
    bool primed{},sampled{};
    float age{},attachedAt{};
    void Drop(){if(effect)effect->lifetime=0;effect.reset();}
};
std::vector<LjodhCloud> ljodhClouds;
void LjodhBurst(RE::Actor* actor,Song song,Effects::SkaldCloud kind,int rank,const RE::NiPoint3& at){
    // This is only an artwork budget. It must never gate gameplay waves or buffs.
    if(!actor||!Settings::Get().abilityEffects)return;
    if(ljodhClouds.size()>=128){
        static float warned=-100.f;
        if(now-warned>30.f){warned=now;Log::Warn("[Skald] Ljodh particle budget full: 128 bursts alive; new ones are skipped.");}
        return;
    }
    if(auto* effect=Effects::SkaldParticles(actor,static_cast<std::size_t>(song),kind,rank,at))
        ljodhClouds.push_back({actor->GetHandle(),actor->GetParentCell(),kind,RE::NiPointer<RE::BSTempEffectParticle>(effect)});
}
void TickLjodhClouds(float seconds){
    std::erase_if(ljodhClouds,[seconds](LjodhCloud& cloud){
        cloud.age+=seconds;auto actor=cloud.actor.get();
        if(!actor||!actor->Is3DLoaded()||actor->GetParentCell()!=cloud.cell||!Settings::Get().abilityEffects||
            !cloud.effect||cloud.effect->age>=cloud.effect->lifetime||(!cloud.primed&&cloud.age>2.f)||
            (cloud.primed&&cloud.age-cloud.attachedAt>2.1f)){cloud.Drop();return true;}
        auto* root=Posing::RootOf(cloud.effect.get());if(!root)return false;
        if(!cloud.primed){
            // Start through the engine's animation initializer. Start(0) pins
            // these clamped bursts to application time zero, long before spawn.
            unsigned systems=0;
            RE::BSVisit::TraverseScenegraphGeometries(root,[&](RE::BSGeometry* geometry){
                if(netimmerse_cast<RE::NiParticleSystem*>(geometry)){
                    ++systems;
                    RE::NiTimeController::StartAnimations(geometry);
                }
                return RE::BSVisit::BSVisitControl::kContinue;
            });
            if(systems<3||systems>4){Log::Warn("[Skald] Native Ljodh model failed validation: {} systems.",systems);cloud.Drop();return true;}
            cloud.primed=true;cloud.attachedAt=cloud.age;
            // Async cloning must not consume the visible tail before it starts.
            cloud.effect->lifetime=cloud.effect->age+(cloud.kind==Effects::SkaldCloud::Wave?1.95f:1.65f);
        }
        if(!cloud.sampled&&cloud.age-cloud.attachedAt>=.35f){
            cloud.sampled=true;
            unsigned live=0;
            RE::BSVisit::TraverseScenegraphGeometries(root,[&](RE::BSGeometry* geometry){
                if(auto* system=netimmerse_cast<RE::NiParticleSystem*>(geometry))
                    if(auto data=system->GetParticlesRuntimeData().particleData)live+=data->GetActiveVertexCount();
                return RE::BSVisit::BSVisitControl::kContinue;
            });
            // Log actual emission, not just whether a model cloned. modelName
            // may be cleared after the clone completes; use the scene name.
            static std::array<unsigned,3> reported{};
            const auto kind=static_cast<std::size_t>(cloud.kind);
            const unsigned result=live?1u:2u;
            if(!(reported[kind]&result)){
                reported[kind]|=result;
                Log::Info("[Skald] Native Ljodh emission: kind={}, live particles={}, node={}.",kind,live,root->name.c_str()?root->name.c_str():"unnamed");
            }
        }
        if(cloud.kind!=Effects::SkaldCloud::Wave){
            // Move only the emission volume; world-space particles keep their own trajectories.
            const auto at=actor->GetPosition();cloud.effect->particleEffectTransform.translate=at;
            const auto& parent=root->parent->world;
            if(parent.scale>.0001f)root->local.translate=parent.rotate.Transpose()*(at-parent.translate)*(1.f/parent.scale);
            RE::NiUpdateData update{0.f,RE::NiUpdateData::Flag::kNone};
            root->UpdateWorldData(&update);
            if(auto* node=root->AsNode())for(auto& child:node->GetChildren())if(child)child->UpdateWorldData(&update);
        }
        // Skyrim owns emitter/lifetime updates. Never run Posing::Refresh(time=0) on these systems.
        return false;
    });
}
struct Listener {
    RE::ActorHandle actor;
    SkaldPolicy::WaveStacks stacks;
    float finalePaid{};
};
struct Performance {
    RE::ActorHandle actor;
    RE::ActorHandle target;
    RE::FormID id{};
    RE::TESObjectCELL* cell{};
    RE::TESWorldSpace* worldspace{};
    Song song{};
    Paths::Ability ability{};
    int rank{},copy{};
    float chance{},duration{},a{},b{},shield{},cost{},age{},playing{},lastBeat{-1},goldUntil{},check{};
    std::size_t beat{};
    std::size_t waveBeat{};
    std::uint64_t serial{};
    bool drawn{},idle{},started{},audible{},finished{},debug{},note{},noteFired{},announced{},resuming{},handoff{};
    // fresh: the lute was just called up, so it swings in from behind the back first.
    bool fresh{},lifting{},overlayReported{},respawned{};
    float pluckAt{},startedAt{},liftedAt{-100.f};
    // Lutes already on the bard when this performance began: left-overs, never played.
    std::vector<std::pair<RE::NiPointer<RE::NiAVObject>,RE::NiPoint3>> stale;
    SkaldPolicy::LuteEntry entry;
    SkaldPolicy::PropRecovery recovery;
    float soundWait{},playheadWait{},aim{};
    RE::NiPointer<RE::NiAVObject> prop;
    Visual lute,luteVeil;
    std::vector<Wave> waves;
    std::unordered_map<RE::FormID,Listener> listeners;
    Posing::Lamp lamp;
    RE::BSSoundHandle music;
};
std::vector<Performance> performances;
struct NotePulse {
    RE::ActorHandle actor,target;
    RE::TESObjectCELL* cell{};
    RE::NiPoint3 at,origin;
    float age{},travel{};
    bool primed{};
    Visual art;
    std::array<Visual,24> motes;
    std::array<RE::NiPoint3,24> trail;
    float shed{};
    unsigned emitted{};
    Posing::Lamp light;
    void Drop(){Posing::Out(light);for(auto& mote:motes)mote.Drop();art.Drop();}
};
std::vector<NotePulse> notes;
std::unordered_map<RE::FormID,float> noteReady;
float noteScan{};
// Walking back into the leash around the player; Fight With Me's own leash does
// the walking, as it does for every follower.
struct Rejoin {
    bool active{};
    float since{},best{},movedAt{},unstickAt{},restAt{};
    RE::NiPoint3 from;
};
std::unordered_map<RE::FormID,Rejoin> rejoins;
float leashScan{};
// While the arm layer plays over a walk, the white spectral lute follows the
// recorded prop under the bone Skyrim hangs it from.
struct WalkingLute {
    RE::ActorHandle actor;
    RE::TESObjectCELL* cell{};
    Visual lute,veil;
    Posing::Lamp lamp;
    void Drop(){Posing::Out(lamp);veil.Drop();lute.Drop();}
};
std::unordered_map<RE::FormID,WalkingLute> walkingLutes;
// A spectral lute let go of for a moment (a finished song, a swing of the fists),
// kept hidden briefly so the walk carries it on rather than loading a new one with
// empty hands meanwhile.
struct SpareLute {Visual lute,veil;float at{};};
std::unordered_map<RE::FormID,SpareLute> spares;
void DropSpare(RE::FormID id){
    if(auto it=spares.find(id);it!=spares.end()){it->second.veil.Drop();it->second.lute.Drop();spares.erase(it);}
}
void Spare(RE::FormID id,Visual& lute,Visual& veil){
    DropSpare(id);
    if(!lute.effect){veil.Drop();return;}
    lute.effect->lifetime=lute.effect->age+SkaldPolicy::kSpareFor+1.f;
    spares[id]={std::move(lute),std::move(veil),now};lute={};veil={};
}
// The Single Note played on the move, over the walking lute (TickStrums).
struct Strum {RE::ActorHandle target;float pluckAt{},pluckedAt{-100.f},checkAt{},reportAt{};bool may{};RE::BSSoundHandle sound;};
std::unordered_map<RE::FormID,Strum> strums;
std::unordered_set<RE::FormID> strumming; // under gate, for Statuses
// Skyrim's lutes left on a bard: the exit of a performance, or one a hit left
// behind when the idle was cut short. Kept hidden (TickStrays).
struct StrayLute {RE::NiPointer<RE::NiAVObject> node;RE::NiPoint3 local;float since{};};
struct Strays {std::vector<StrayLute> props;float stopAt{},warned{-100.f};};
std::unordered_map<RE::FormID,Strays> strays;
void StowLute(RE::FormID id){
    if(auto it=walkingLutes.find(id);it!=walkingLutes.end()){it->second.Drop();walkingLutes.erase(it);}
}
// Upper-body layer. The main thread records the arms while the bard plays and
// publishes an immutable clip; the animation update, on any thread, replays it.
// The lute prop is animated too (Skyrim moves it under its bone as the bard
// strums), so its transform is a track of its own beside the arms.
struct Clip {
    RE::NiAVObject* skeleton{};
    std::vector<RE::NiPointer<RE::NiAVObject>> nodes,roots;
    std::vector<std::vector<RE::NiTransform>> frames;
    std::vector<RE::NiTransform> prop;
    std::string parent;
    std::vector<float> times;
    float length{};
};
struct Recording {
    RE::NiAVObject* skeleton{};
    std::vector<RE::NiPointer<RE::NiAVObject>> nodes;
    std::vector<std::vector<RE::NiTransform>> frames;
    std::vector<RE::NiTransform> prop;
    std::string parent;
    std::vector<float> times;
    float lastAt{-100.f},builtAt{-100.f};
    std::shared_ptr<const Clip> best;
    unsigned swings{};
};
std::unordered_map<RE::FormID,Recording> recordings;
// With its playing arms recorded, a bard plays the Single Note on the move.
bool Recorded(RE::FormID id,RE::Actor* actor){
    const auto rec=recordings.find(id);
    return actor&&rec!=recordings.end()&&rec->second.best&&rec->second.best->skeleton==actor->Get3D();
}
struct Layer {std::shared_ptr<const Clip> clip;float started{},stopped{-1.f};};
std::shared_mutex layerLock;
std::unordered_map<RE::FormID,Layer> layers;
std::atomic<std::size_t> layerCount{0};
std::atomic<float> layerClock{0.f};
std::unordered_map<RE::FormID,float> playedUntil;
std::unordered_map<RE::FormID,std::pair<float,bool>> luteSeen;
// Single Note discords on enemies (under gate): stacks per kind, each on its own clock.
struct Discords {
    std::array<unsigned,SkaldPolicy::kDiscords> stacks{};
    std::array<float,SkaldPolicy::kDiscords> until{};
    float jarredAt{-100.f},crescendoAt{-100.f};
};
std::unordered_map<RE::FormID,Discords> discords;
unsigned Stacks(RE::FormID id,SkaldPolicy::Discord kind){
    const auto it=discords.find(id);if(it==discords.end())return 0;
    const auto i=static_cast<std::size_t>(kind);return it->second.until[i]>now?it->second.stacks[i]:0u;
}
// Overhead icons: kept on the main thread, projected each tick for the HUD.
// key tells a discord (its index) from a crescendo (100 + its index) for merging.
struct Mark {RE::ActorHandle target;unsigned key{},count{};float born{},life{SkaldPolicy::kMarkSeconds};std::uint32_t glyph{},color{};bool major{};};
std::vector<Mark> marks;
std::mutex markLock;
std::vector<NoteMark> placedMarks;
std::unordered_map<RE::FormID,unsigned> phrase;
SkaldPolicy::MusicDuck musicDuck;
RE::BGSSoundCategory* MusicCategory(){
    // Skyrim.esm AudioCategoryMUS. Its runtime volume is separate from the saved menu setting.
    return RE::TESForm::LookupByID<RE::BGSSoundCategory>(0x71E64);
}
void DuckMusic(float seconds,bool clear=false){
    auto* category=MusicCategory();if(!category){musicDuck={};return;}
    const float observed=category->GetCategoryVolume();
    if(!std::isfinite(observed)||observed<0.f)return;
    const auto* player=RE::PlayerCharacter::GetSingleton();
    const bool active=!clear&&player&&std::ranges::any_of(performances,[&](const Performance& p){
        auto who=p.actor.get();return p.started&&!p.finished&&!p.note&&who&&
            (who->GetParentCell()==player->GetParentCell()||(who->GetWorldspace()&&who->GetWorldspace()==player->GetWorldspace()))&&
            Game::Distance(who.get(),player)<2000.f;
    });
    const float volume=clear?musicDuck.Restore(observed):musicDuck.Update(observed,active,seconds);
    if(std::abs(volume-observed)>.000001f)category->SetCategoryVolume(volume);
}
std::vector<std::pair<std::uint32_t,float>> resumeCleanup;
constexpr std::array<std::array<float,3>,4> colors{{
    {1.f,.48f,.12f},{.36f,.95f,1.f},{.57f,.18f,1.f},{1.f,.14f,.21f}
}};
Song SongOf(Paths::Ability ability){return static_cast<Song>(static_cast<int>(ability)-static_cast<int>(Paths::Ability::kFervor));}
std::size_t Index(Song song){return static_cast<std::size_t>(song);}
std::array<float,3> MoteColor(Song song,std::size_t index){
    const auto type=index%4;
    if(song==Song::Glory){
        if(type==0)return {1.f,.82f,.46f};
        if(type==1)return {1.f,.67f,.20f};
        if(type==2)return {1.f,.14f,.21f};
        return {1.f,.86f,.58f};
    }
    if(song==Song::Echoes&&type==2)return {.78f,.96f,1.f};
    if(song==Song::Fervor&&type==2)return {1.f,.78f,.32f};
    if(song==Song::Hollow&&type==2)return {.48f,.32f,.60f};
    auto color=colors[Index(song)];if(index%3==0)for(auto& channel:color)channel=.38f+.62f*channel;
    return color;
}
void PlaceMote(RE::NiAVObject* root,Visual& look,const char* name,const RE::NiPoint3& origin,
    const SkaldPolicy::ParticleFrame& frame,const std::array<float,3>& color){
    if(!look.FindAt(root,name))return;
    look.node->SetAppCulled(frame.alpha<=.001f);if(frame.alpha<=.001f)return;
    const RE::NiPoint3 at{origin.x+frame.x,origin.y+frame.y,origin.z+frame.z};
    RE::NiMatrix3 turn;turn.SetEulerAnglesXYZ(0.f,0.f,0.f);
    Posing::PlaceInWorld(root,look.node.get(),at,turn,frame.scale);
    Posing::Paint(look.shaders,color,frame.alpha);Posing::Refresh(look.node.get());
}
bool Standing(RE::Actor* actor){return actor&&Game::Usable(actor)&&actor->Is3DLoaded()&&!actor->AsActorState()->IsBleedingOut();}
// Knocked down, getting up, mid-swing or staggered. Lute and sheathe requests sent
// during a get-up can leave the graph able to play idles but no longer walk.
bool Unsettled(RE::Actor* actor){return Abilities::Busy(actor)||actor->AsActorState()->GetKnockState()!=RE::KNOCK_STATE_ENUM::kNormal;}
// A radial sound wave needs an unobstructed path, not an actor's visual detection cone.
bool ClearPath(RE::Actor* actor,const RE::NiPoint3& from,const RE::NiPoint3& to,RE::Actor* target,bool passActors=false){
    auto* cell=actor?actor->GetParentCell():nullptr;auto* world=cell?cell->GetbhkWorld():nullptr;
    const float scale=RE::bhkWorld::GetWorldScale();if(!world||scale<=0)return false;
    RE::CFilter own{};actor->GetCollisionFilterInfo(own);auto start=from;
    for(unsigned skip=0;skip<24;++skip){
        RE::bhkPickData pick{};
        pick.rayInput.from={start.x*scale,start.y*scale,start.z*scale,0};
        pick.rayInput.to={to.x*scale,to.y*scale,to.z*scale,0};
        pick.rayInput.filterInfo.filter=static_cast<std::uint32_t>(RE::COL_LAYER::kLOS)|(own.GetSystemGroup()?own.GetSystemGroup()<<16:1u<<16);
        world->PickObject(pick);
        if(pick.pickFailed)return false;
        if(!pick.rayOutput.HasHit())return true;
        const auto* body=pick.rayOutput.rootCollidable?pick.rayOutput.rootCollidable->GetOwner<RE::hkpWorldObject>():nullptr;
        auto* hit=body?body->GetUserData():nullptr;
        if(target&&hit==target)return true;
        if(pick.rayOutput.hitFraction>=.999f)return true;
        if(!passActors||!hit||!hit->As<RE::Actor>())return false;
        // Sound passes through the party instead of being swallowed by the
        // first companion's capsule. World geometry still stops the wave.
        const auto delta=to-start;const float length=delta.Length();
        const float distance=length*pick.rayOutput.hitFraction+8.f;
        if(distance>=length)return true;
        start+=delta*(distance/std::max(.001f,length));
    }
    return false;
}
bool Near(RE::Actor* from,RE::Actor* to){
    if(!Standing(from)||!Standing(to)||Game::Distance(from,to)>SkaldPolicy::kReach)return false;
    if(from->GetParentCell()!=to->GetParentCell()&&(!from->GetWorldspace()||from->GetWorldspace()!=to->GetWorldspace()))return false;
    return from==to||ClearPath(from,from->GetPosition()+RE::NiPoint3{0,0,60},to->GetPosition()+RE::NiPoint3{0,0,60},to,true);
}
std::vector<RE::ActorHandle> Allies(RE::Actor* actor){
    std::vector<RE::ActorHandle> result;
    auto add=[&](RE::Actor* ally){if(Near(actor,ally)&&std::find(result.begin(),result.end(),ally->GetHandle())==result.end())result.push_back(ally->GetHandle());};
    add(RE::PlayerCharacter::GetSingleton());
    for(const auto& member:Party::Members())if(auto ally=member.handle.get())add(ally.get());
    return result;
}
Boon Active(RE::FormID id,Song song){
    const auto it=benefits.find(id);
    if(it==benefits.end())return {};
    const auto b=it->second.songs[Index(song)];
    return b.until>now?b:Boon{};
}
void Give(RE::Actor* actor,Song song,float duration,float strength,float a,float b,std::uint64_t source,const SkaldPolicy::WaveStacks& stacks){
    std::scoped_lock lock(gate);
    auto& slot=benefits[actor->GetFormID()].songs[Index(song)];
    // A weaker song cannot prolong a stronger magnitude. Equal strength may refresh its clock.
    if(slot.until<=now||strength>slot.strength||(strength==slot.strength&&a+b>=slot.branchA+slot.branchB)){
        const bool spent=slot.source==source&&slot.inspirationSpent;
        slot={std::max(now+duration,slot.until>now&&slot.strength==strength?slot.until:0.f),strength,a,spent?0.f:b,duration,source,stacks.count,stacks.empowered,spent};
    }
}
float Raise(RE::Actor* actor,RE::ActorValue value,float fraction,std::uint16_t source=speedSource){
    const auto lift=Abilities::BoostOn(actor->GetFormID(),value,source);
    const float without=Game::Current(actor,value)-(lift?lift->amount:0.f);
    const float base=without>0.f?without:1.f;
    return std::round((base*(1.f+fraction)-without)*1000.f)/1000.f;
}
bool Waiting(RE::FormID id){
    if(auto* player=RE::PlayerCharacter::GetSingleton();player&&id==player->GetFormID()&&player->GetVoiceRecoveryTime()>1.f)return true;
    if(const auto* member=Party::Find(id);member&&(member->tauntReady>1||member->healReady>1||member->rejuvenateReady>1||member->specialReady>1))return true;
    for(std::size_t i=1;i<Paths::kAbilityCount;++i){const auto ability=static_cast<Paths::Ability>(i);if(!IsSong(ability)&&Arts::ClockOf(id,ability).ready>1)return true;}
    return false;
}
void LightHit(RE::Actor* who,Song song,int rank,bool ljodh=true){
    if(!Settings::Get().abilityEffects)return;
    auto& hit=hitLights[who->GetFormID()];
    const bool native=ljodh;
    if(hit.cell!=who->GetParentCell()||hit.song!=song||hit.native!=native)hit.Drop();
    hit.native=native;
    hit.actor=who->GetHandle();hit.cell=who->GetParentCell();hit.at=now;hit.song=song;hit.rank=std::clamp(rank,1,3);
    const auto slot=hit.nextBurst++%hit.bursts.size();hit.bursts[slot]=now;hit.burstRanks[slot]=hit.rank;
    if(native)LjodhBurst(who,song,Effects::SkaldCloud::Hit,hit.rank,who->GetPosition());
    else if(!hit.glow.effect)hit.glow.effect.reset(Effects::SkaldHit(who,Index(song),2.5f));
    // Reuse a recipient's light between beats, without accumulating overlapping lights.
    if(hit.glow.effect)hit.glow.effect->lifetime=hit.glow.effect->age+2.5f;
}
void TickHitLights(){
    std::erase_if(hitLights,[](auto& entry){
        auto& hit=entry.second;auto who=hit.actor.get();const float age=now-hit.at;
        if(!Standing(who.get())||who->GetParentCell()!=hit.cell||!Settings::Get().abilityEffects||age>2.f){hit.Drop();return true;}
        auto* root=hit.native?who->Get3D():Posing::RootOf(hit.glow.effect.get());if(!root)return false;
        const auto color=colors[Index(hit.song)];
        float particleLight=hit.native?std::max(0.f,1.f-age/1.4f)*SkaldPolicy::RankGlow(hit.rank):0.f;
        for(std::size_t i=0;!hit.native&&i<hit.motes.size();++i){
            const auto index=i%SkaldPolicy::kHitMotes;
            const auto slot=i/SkaldPolicy::kHitMotes;const int rank=hit.burstRanks[slot];
            const auto frame=index<SkaldPolicy::HitCount(rank)?SkaldPolicy::RankMote(SkaldPolicy::HitMote(static_cast<int>(hit.song),index,now-hit.bursts[slot]),rank):SkaldPolicy::ParticleFrame{};
            PlaceMote(root,hit.motes[i],std::format("HitMote{}",i).c_str(),who->GetPosition(),frame,MoteColor(hit.song,index));
            particleLight=std::max(particleLight,frame.alpha);
        }
        if(particleLight<=.001f)Posing::Out(hit.bodyLamp);
        else{auto at=who->GetPosition();at.z+=75.f;Posing::Shine(hit.bodyLamp,root,at,color,particleLight*.22f,145.f);}
        const float alpha=SkaldPolicy::HitAlpha(age);
        if(alpha<=0.f)Posing::Out(hit.lamp);
        else{auto at=who->GetPosition();at.z+=14.f;Posing::Shine(hit.lamp,root,at,color,alpha*.75f,115.f);}
        if(!hit.native)Posing::Refresh(root);
        return false;
    });
}
void RecoverVoice(RE::Actor* actor,float seconds){
    if(!actor||!actor->IsPlayerRef()||seconds<=0.f)return;
    if(auto* process=actor->GetActorRuntimeData().currentProcess;process&&process->high)
        process->high->voiceRecoveryTime=std::max(0.f,process->high->voiceRecoveryTime-seconds);
}
void ApplyListener(Performance& p,Listener& listener,RE::Actor* who){
    if(!listener.stacks.count)return;
    const float factor=listener.stacks.factor;
    const float base=p.song==Song::Fervor?SkaldPolicy::Speed(p.rank):p.song==Song::Echoes?SkaldPolicy::Recovery(p.rank):
        p.song==Song::Hollow?SkaldPolicy::Weakness(p.rank):SkaldPolicy::Critical(p.rank);
    // Every arriving wave refreshes earned stacks. A consumed Echoes charge stays consumed.
    Give(who,p.song,p.duration,base*factor,p.a*factor,p.song==Song::Hollow?0.f:p.b*factor,p.serial,listener.stacks);
    if(!p.finished)return;
    const float delta=std::max(0.f,factor-listener.finalePaid);
    if(delta<=0)return;
    if(p.song==Song::Echoes&&p.a>0){
        const float cut=p.a*delta;
        Arts::ReduceCooldowns(who->GetFormID(),cut);
        RecoverVoice(who,cut);
        if(auto* member=Party::Find(who->GetFormID())){
            member->tauntReady=std::max(0.f,member->tauntReady-cut);
            member->healReady=std::max(0.f,member->healReady-cut);
            member->rejuvenateReady=std::max(0.f,member->rejuvenateReady-cut);
            member->specialReady=std::max(0.f,member->specialReady-cut);
        }
    }
    if(p.song==Song::Hollow){
        if(listener.finalePaid==0)if(auto actor=p.actor.get())Game::Stagger(who,.35f,actor.get());
        if(p.b>0)who->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kMagicka,Game::Maximum(who,RE::ActorValue::kMagicka)*p.b*delta);
    }
    listener.finalePaid=factor;
}
void AdvanceWaves(Performance& p,RE::Actor* actor){
    std::vector<RE::ActorHandle> targets;
    if(p.song==Song::Hollow){
        for(const auto& enemy:Enemies::All())if(auto who=enemy.handle.get();Standing(who.get())&&!who->IsPlayerRef()&&!Party::IsPartyActor(who.get()))targets.push_back(enemy.handle);
    }else{
        auto add=[&](RE::Actor* who){if(Standing(who)&&std::find(targets.begin(),targets.end(),who->GetHandle())==targets.end())targets.push_back(who->GetHandle());};
        add(RE::PlayerCharacter::GetSingleton());for(const auto& member:Party::Members())if(auto who=member.handle.get())add(who.get());
    }
    for(auto& wave:p.waves){
        const float radius=SkaldPolicy::WaveRadius(p.playing-wave.born);
        for(const auto& target:targets)if(auto who=target.get()){
            const auto offset=who->GetPosition()-wave.origin;
            const float distance=std::hypot(offset.x,offset.y);
            const bool sameSpace=who->GetParentCell()==p.cell||(p.worldspace&&who->GetWorldspace()==p.worldspace);
            const bool eligible=sameSpace && std::abs(offset.z)<=SkaldPolicy::kWaveHeight;
            auto& contact=wave.contacts[who->GetFormID()];
            if(!SkaldPolicy::CrossWave(contact,wave.radius,radius,distance,eligible))continue;
            if(who.get()!=actor&&!ClearPath(actor,wave.origin+RE::NiPoint3{0,0,60},who->GetPosition()+RE::NiPoint3{0,0,60},who.get(),true)){
                if(who->IsPlayerRef()&&!wave.playerBlocked){wave.playerBlocked=true;Log::Info("[Skald] Player wave blocked by world geometry: distance {:.0f}, height {:.0f}.",distance,offset.z);}
                contact.hit=false;continue;
            }
            LightHit(who.get(),p.song,p.rank);
            auto& listener=p.listeners[who->GetFormID()];listener.actor=who->GetHandle();
            bool doubled=false;
            if(listener.stacks.NextIsStack()){
                std::scoped_lock lock(gate);doubled=std::uniform_real_distribution<float>(0.f,1.f)(rng)<p.chance;
            }
            const bool gained=listener.stacks.Hit(doubled);
            if(gained&&doubled)p.goldUntil=p.playing+.45f;
            if(gained||listener.stacks.hits==1)Log::Info("[Skald] {} receives {}: {} waves, {} stacks, {:.2f}x strength.",
                who->IsPlayerRef()?std::string("Player"):Game::Name(who.get()),Paths::Info(p.ability).name,listener.stacks.hits,listener.stacks.count,listener.stacks.factor);
            if(gained&&who->IsPlayerRef())Notices::Used(Trf("You gain stack {} of 3.",listener.stacks.count),p.ability);
            ApplyListener(p,listener,who.get());
        }
        wave.radius=radius;
    }
    std::erase_if(p.waves,[&](const Wave& wave){return p.playing-wave.born>=SkaldPolicy::kWaveSeconds;});
}
// The bone Skyrim hangs the lute from.
RE::NiAVObject* Chest(RE::Actor* actor){auto* root=actor?actor->Get3D():nullptr;return root?root->GetObjectByName("NPC Spine2 [Spn2]"):nullptr;}
// From the parent, since a hidden node's own world transform can be a frame or more old.
RE::NiTransform WorldOf(const RE::NiAVObject* node){return node->parent?node->parent->world*node->local:node->world;}
bool Held(RE::Actor* actor,const RE::NiPoint3& at,std::string_view what){
    auto* chest=Chest(actor);if(!chest)return false;
    const float distance=(at-chest->world.translate).Length();
    if(!SkaldPolicy::Astray(distance))return true;
    static float warned=-100.f;
    if(now-warned>10.f){warned=now;Log::Warn("[Skald] {}'s {} was {:.0f} from the chest holding it; hidden.",Game::Name(actor),what,distance);}
    return false;
}
bool PoseLute(Visual& lute,Visual& veil,Posing::Lamp& lamp,const RE::NiTransform& pose,
    const std::array<float,3>& color,float time,float accent,int rank,float shown=1.f){
    if(!lute.Find("Instrument"))return false;
    auto* root=Posing::RootOf(lute.effect.get());if(!root)return false;
    Placed(lute.effect.get(),root);
    const std::array<float,3> bright{.25f+.75f*color[0],.25f+.75f*color[1],.25f+.75f*color[2]};
    // The core is the wooden lute's own geometry, so it sits where Skyrim's hidden lute is.
    Posing::PlaceInWorld(root,lute.node.get(),pose.translate,pose.rotate,pose.scale*SkaldPolicy::kOverlayScale);
    Posing::Paint(lute.shaders,bright,(1.1f+accent*.45f)*shown);
    Posing::Drift(lute.shaders,time*.035f);
    if(veil.FindAt(root,"LuteVeil")){
        const auto wave=SkaldPolicy::LuteVeil(time,accent);
        RE::NiMatrix3 ripple;ripple.SetEulerAnglesXYZ(wave.angle*.5f,wave.angle,0.f);
        Posing::PlaceInWorld(root,veil.node.get(),pose.translate+pose.rotate*RE::NiPoint3{wave.x,wave.y,wave.z}*pose.scale,
            pose.rotate*ripple,pose.scale*wave.scale);
        Posing::Paint(veil.shaders,bright,wave.glow*shown);Posing::Drift(veil.shaders,wave.scroll);
    }
    if(Settings::Get().abilityEffects)Posing::Shine(lamp,root,pose.translate,bright,(.32f+accent*.28f)*SkaldPolicy::RankGlow(rank)*shown,220.f);
    Posing::Refresh(root);
    return true;
}
// Skeleton bones of both arms, clavicles down to the fingertips. Attach points,
// props and physics bones are left alone: only "NPC " and "CME " nodes.
std::vector<RE::NiPointer<RE::NiAVObject>> ArmNodes(RE::NiAVObject* root){
    std::vector<RE::NiPointer<RE::NiAVObject>> out;
    for(const char* name:{"NPC L Clavicle [LClv]","NPC R Clavicle [RClv]"}){
        auto* top=root?root->GetObjectByName(name):nullptr;if(!top)continue;
        std::vector<RE::NiAVObject*> stack{top};
        while(!stack.empty()){
            auto* node=stack.back();stack.pop_back();
            const std::string_view bone=node->name.c_str()?node->name.c_str():"";
            if(!bone.starts_with("NPC ")&&!bone.starts_with("CME "))continue;
            out.emplace_back(node);
            if(auto* parent=node->AsNode())for(auto& child:parent->GetChildren())if(child)stack.push_back(child.get());
        }
    }
    return out;
}
// Called each tick while the bard plays in a settled lute idle: the arms and the
// lute prop, which Skyrim animates under its bone as the bard strums.
void Record(RE::FormID id,RE::Actor* actor,RE::NiAVObject* prop){
    auto* root=actor->Get3D();if(!root||!prop||!prop->parent||!prop->parent->name.c_str())return;
    auto& rec=recordings[id];
    if(rec.skeleton!=root||rec.nodes.empty()){rec={};rec.skeleton=root;rec.nodes=ArmNodes(root);if(rec.nodes.empty())return;}
    const std::string parent=prop->parent->name.c_str();
    if(!rec.times.empty()&&now-rec.lastAt<1.f/40.f)return;
    // A gap, a prop on another bone or a lute still swinging into place starts a new take.
    const bool swinging=!rec.prop.empty()&&SkaldPolicy::Swinging((prop->local.translate-rec.prop.back().translate).Length(),now-rec.lastAt);
    if(swinging)++rec.swings;
    if(now-rec.lastAt>.2f||parent!=rec.parent||swinging){rec.frames.clear();rec.prop.clear();rec.times.clear();rec.parent=parent;}
    rec.lastAt=now;
    std::vector<RE::NiTransform> frame;frame.reserve(rec.nodes.size());
    for(const auto& node:rec.nodes)frame.push_back(node->local);
    rec.frames.push_back(std::move(frame));rec.prop.push_back(prop->local);rec.times.push_back(now);
    while(rec.times.size()>2&&rec.times.back()-rec.times.front()>SkaldPolicy::kLayerRecord){
        rec.frames.erase(rec.frames.begin());rec.prop.erase(rec.prop.begin());rec.times.erase(rec.times.begin());
    }
    // A long enough take replaces the clip now and then; a short interrupted one never does.
    if(rec.times.back()-rec.times.front()<SkaldPolicy::kLayerMinimum||(rec.best&&now-rec.builtAt<2.f))return;
    auto clip=std::make_shared<Clip>();
    clip->skeleton=root;clip->nodes=rec.nodes;clip->frames=rec.frames;clip->prop=rec.prop;clip->parent=rec.parent;
    for(const auto& node:rec.nodes){const std::string_view bone=node->name.c_str();if(bone=="NPC L Clavicle [LClv]"||bone=="NPC R Clavicle [RClv]")clip->roots.push_back(node);}
    for(const float at:rec.times)clip->times.push_back(at-rec.times.front());
    clip->length=clip->times.back();
    if(!rec.best){
        float reach=0.f;for(const auto& at:clip->prop)reach=std::max(reach,at.translate.Length());
        Log::Info("[Skald] {}'s playing recorded: {} arm bones and the lute on {} (within {:.0f} of it), {:.1f} s, {} swinging takes dropped; the lute stays up on the move.",
            Game::Name(actor),rec.nodes.size(),clip->parent,reach,clip->length,rec.swings);
    }
    rec.best=std::move(clip);rec.builtAt=now;
}
RE::NiTransform Blend(const RE::NiTransform& a,const RE::NiTransform& b,float f){
    RE::NiTransform out;
    const RE::NiQuaternion qa(a.rotate),qb(b.rotate);
    const float side=qa.w*qb.w+qa.x*qb.x+qa.y*qb.y+qa.z*qb.z<0.f?-1.f:1.f;
    RE::NiQuaternion q{qa.w+(qb.w*side-qa.w)*f,qa.x+(qb.x*side-qa.x)*f,qa.y+(qb.y*side-qa.y)*f,qa.z+(qb.z*side-qa.z)*f};
    const float length=std::sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z);
    if(length>1e-6f){q.w/=length;q.x/=length;q.y/=length;q.z/=length;}
    q.ToRotation(out.rotate);
    out.translate=a.translate+(b.translate-a.translate)*f;
    out.scale=a.scale+(b.scale-a.scale)*f;
    return out;
}
// One track of a clip (an arm bone's frames, or the prop's) at a time in seconds.
template<class Pick>
RE::NiTransform Sample(const Clip& clip,float at,Pick pick){
    const auto next=std::upper_bound(clip.times.begin(),clip.times.end(),at);
    if(next==clip.times.begin())return pick(0);
    if(next==clip.times.end())return pick(clip.times.size()-1);
    const auto k=static_cast<std::size_t>(next-clip.times.begin());
    const float from=clip.times[k-1],to=clip.times[k];
    return Blend(pick(k-1),pick(k),to>from?(at-from)/(to-from):0.f);
}
template<class Pick>
RE::NiTransform Looped(const Clip& clip,const SkaldPolicy::LoopSample& at,Pick pick){
    auto pose=Sample(clip,at.time,pick);
    return at.blend>0.f?Blend(pose,Sample(clip,at.other,pick),at.blend):pose;
}
void SetLayer(RE::FormID id,const std::shared_ptr<const Clip>& clip){
    std::unique_lock lock(layerLock);
    const auto it=layers.find(id);
    if(clip){
        // Straight out of a performance the recorded arms already match: no fade.
        const auto played=playedUntil.find(id);
        const bool seamless=played!=playedUntil.end()&&now-played->second<.3f;
        if(it==layers.end())layers[id]={clip,seamless?now-SkaldPolicy::kLayerFade:now,-1.f};
        else if(it->second.stopped>=0.f||it->second.clip!=clip)it->second={clip,seamless||it->second.stopped<0.f?now-SkaldPolicy::kLayerFade:now,-1.f};
    }else if(it!=layers.end()){
        if(it->second.stopped<0.f)it->second.stopped=now;
        else if(now-it->second.stopped>SkaldPolicy::kLayerFade)layers.erase(it);
    }
    layerCount.store(layers.size(),std::memory_order_release);
}
// The arm layer runs for a fighting bard who has a recording and is not in a lute
// idle of its own (or is, but its lute is still swinging in). The white spectral
// lute follows the recorded prop track at the same loop time as the arms, so the
// hands stay on the instrument.
void TickLayers(){
    layerClock.store(now,std::memory_order_relaxed);
    std::unordered_set<RE::FormID> wanted;
    for(const auto& member:Party::Members()){
        const auto* orders=Party::FindOrders(member.id);auto actor=member.handle.get();
        if(!orders||orders->role!=Role::kSkald||!Standing(actor.get())||!actor->IsInCombat()||Unsettled(actor.get()))continue;
        if(std::ranges::any_of(performances,[&](const Performance& p){return p.id==member.id&&p.started&&!p.finished&&!p.lifting;}))continue;
        if(!Recorded(member.id,actor.get()))continue;
        const auto best=recordings[member.id].best;const auto& clip=*best;
        // The inventory is looked at a few times a second, not every frame.
        auto& seen=luteSeen[member.id];
        if(now-seen.first>.5f)seen={now,HasLute(actor.get())};
        if(!seen.second)continue;
        auto* attach=actor->Get3D()->GetObjectByName(clip.parent);if(!attach)continue;
        wanted.insert(member.id);
        SetLayer(member.id,best);
        float started=now;
        {std::shared_lock lock(layerLock);if(const auto it=layers.find(member.id);it!=layers.end())started=it->second.started;}
        const auto at=SkaldPolicy::LoopAt(now-started,clip.length);
        const RE::NiTransform pose=attach->world*Looped(clip,at,[&](std::size_t frame){return clip.prop[frame];});
        auto& walk=walkingLutes[member.id];
        if(walk.cell!=actor->GetParentCell()){walk.Drop();walk={};}
        walk.actor=member.handle;walk.cell=actor->GetParentCell();
        if(!walk.lute.effect){
            if(const auto spare=spares.find(member.id);spare!=spares.end()&&now-spare->second.at<SkaldPolicy::kSpareFor&&
                spare->second.lute.effect&&spare->second.lute.effect->age<spare->second.lute.effect->lifetime){
                walk.lute=std::move(spare->second.lute);walk.veil=std::move(spare->second.veil);spares.erase(spare);
            }else walk.lute.effect.reset(Track(Effects::SkaldModel(actor.get(),static_cast<int>(Song::Echoes),true,2.f)));
        }
        if(walk.lute.effect)walk.lute.effect->lifetime=walk.lute.effect->age+2.f;
        if(!Held(actor.get(),pose.translate,"walking lute"))continue;
        const auto strum=strums.find(member.id);
        const float accent=strum==strums.end()?0.f:std::exp(-std::max(0.f,now-strum->second.pluckedAt)*10.f);
        PoseLute(walk.lute,walk.veil,walk.lamp,pose,SkaldPolicy::kNoteLute,now,accent,1);
    }
    std::erase_if(walkingLutes,[&](auto& entry){
        if(wanted.contains(entry.first))return false;
        // Paused (a swing, a stagger): kept for a moment in case the walk resumes.
        auto& walk=entry.second;Posing::Out(walk.lamp);
        if(auto actor=walk.actor.get();actor&&actor->GetParentCell()==walk.cell)Spare(entry.first,walk.lute,walk.veil);
        walk.Drop();return true;
    });
    std::vector<RE::FormID> stale;
    {std::shared_lock lock(layerLock);for(const auto& [id,layer]:layers)if(!wanted.contains(id))stale.push_back(id);}
    for(const auto id:stale)SetLayer(id,nullptr);
}
void End(Performance& p,bool complete,std::string_view reason){
    if(p.finished){p.waves.clear();return;}
    auto actor=p.actor.get();
    if(!p.started && actor)Log::Warn("[Skald] {} startup failed: age={:.2f}, weapon-state={}, idle-accepted={}, lute-prop={}, spectral-root={}, instrument={}, debug={}",
        Game::Name(actor.get()),p.age,static_cast<int>(actor->AsActorState()->GetWeaponState()),p.idle,static_cast<bool>(p.prop),
        Posing::RootOf(p.lute.effect.get())!=nullptr,static_cast<bool>(p.lute.node),p.debug);
    p.finished=complete;
    if(complete&&actor)for(auto& [id,listener]:p.listeners)if(auto who=listener.actor.get();Standing(who.get())&&Near(actor.get(),who.get()))ApplyListener(p,listener,who.get());
    p.music.FadeOutAndRelease(250);
    Posing::Out(p.lamp);
    // The arm layer can take over straight away, with no dip of the hands.
    if(p.started)playedUntil[p.id]=now;
    if(p.started&&actor)Spare(p.id,p.lute,p.luteVeil);
    else{p.luteVeil.Drop();p.lute.Drop();}
    if(!complete){p.waves.clear();}
    // Skyrim's lute stays hidden through the exit, as do left-overs, until they are
    // put away or an idle plays them again.
    if(p.prop)p.stale.emplace_back(p.prop,p.prop->local.translate);
    p.prop.reset();
    for(auto& [node,local]:p.stale){
        auto& list=strays[p.id].props;
        if(node->parent&&std::ranges::none_of(list,[&](const StrayLute& s){return s.node.get()==node.get();}))list.push_back({node,node->local.translate,now});
    }
    p.stale.clear();
    {
        std::scoped_lock lock(gate);
        performing.erase(p.id);shields.erase(p.id);interrupted.erase(p.id);
    }
    if(actor){
        OwnEvent own;
        // The loose-idle exit removes the animation object as well as stopping the loop.
        if(p.idle&&actor->Is3DLoaded())actor->NotifyAnimationGraph("IdleStop");
        // A hand-over keeps the pose and the anchor for the performance that follows.
        if(!p.handoff)Freeze::ReleaseAnchor(actor.get(),anchor);
        const auto* orders=Party::FindOrders(p.id);
        if(p.drawn&&Standing(actor.get())&&actor->IsInCombat()&&(!orders||orders->role!=Role::kSkald))actor->DrawWeaponMagicHands(true);
        if(!complete&&!p.audible&&!p.note){
            if(!p.debug){
                // Skald uses the stamina ability pool, including on a former mage.
                static_assert(!ChoosesSpells(Role::kSkald));
                actor->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kStamina,p.cost);
                Arts::CancelSong(p.id,p.ability);
            }
            // Startup failures stay in the diagnostic log; automatic retries must
            // not fill the battle HUD with "cannot use Ljodh" warnings.
        }
    }
    Log::Info("[Skald] {:08X} {} {} ({} tempo waves): reason={}, age={:.2f}, audible={}, debug={}.",
        p.id,p.note?"Single Note":Paths::Info(p.ability).name,complete?"completed":"interrupted",p.waveBeat,reason,p.age,p.audible,p.debug);
}
// The leash Fight With Me keeps around the player for this bard's position. With
// Paths alone, its own Decide gives a bard the With Me leash. 0: no leash at all.
float LeashRadius(Position position){
    const auto& settings=Settings::Get();
    if(Host::PathsAlone())return settings.tuning.withMeRadius;
    // Paths turns its own leash setting off when Fight With Me is there; that one
    // still keeps the bard, at the same radii.
    if(!Host::Abilities()&&!settings.guardLeash)return 0.f;
    switch(SupportPosition(position)){
    case Position::kGuard:return settings.tuning.guardRadius;
    case Position::kHoldBack:return settings.holdBackLeash;
    default:return settings.tuning.withMeRadius;
    }
}
// Once the player is well outside a bard's leash, it walks back: a Ljodh stops for
// that, the Single Note plays on the way. It walks while that leash pulls it in, and
// gives up for a while when it cannot get any closer.
void TickLeash(float seconds){
    leashScan-=seconds;if(leashScan>0)return;leashScan=.25f;
    std::erase_if(rejoins,[](const auto& pair){return !Party::Find(pair.first);});
    auto* player=RE::PlayerCharacter::GetSingleton();
    for(const auto& member:Party::Members()){
        const auto* orders=Party::FindOrders(member.id);auto actor=member.handle.get();
        auto& rejoin=rejoins[member.id];
        if(!orders||orders->role!=Role::kSkald||!Standing(actor.get())||!actor->IsInCombat()||!Standing(player)){rejoin={};continue;}
        const float radius=LeashRadius(orders->position);
        const bool together=actor->GetParentCell()==player->GetParentCell()||(actor->GetWorldspace()&&actor->GetWorldspace()==player->GetWorldspace());
        const float distance=Game::Distance(actor.get(),player);
        const auto at=actor->GetPosition();
        if(!rejoin.active){
            if(!together||now<rejoin.restAt||!SkaldPolicy::LeaveToRejoin(distance,radius))continue;
            rejoin.active=true;rejoin.since=rejoin.movedAt=now;rejoin.best=distance;rejoin.from=at;
            std::erase_if(performances,[&](Performance& p){if(p.id!=member.id||p.finished)return false;End(p,false,"walking back to the player");return true;});
            Log::Info("[Skald] {} walks back to you: {:.0f} away, leash {:.0f}.",member.name,distance,radius);
            continue;
        }
        if(!together||SkaldPolicy::BackInLeash(distance,radius)){
            rejoin.active=false;
            Log::Info("[Skald] {} is back within {:.0f} of you ({:.0f} away).",member.name,radius,distance);
            continue;
        }
        // Judged by the bard's own travel: the player may well be moving too.
        if((at-rejoin.from).Length()>SkaldPolicy::kFollowProgress){rejoin.from=at;rejoin.movedAt=now;}
        else if(now-rejoin.movedAt>SkaldPolicy::kFollowStall&&now>=rejoin.unstickAt){
            rejoin.movedAt=now;rejoin.unstickAt=now+SkaldPolicy::kUnstickInterval;
            {OwnEvent own;actor->NotifyAnimationGraph("IdleForceDefaultState");}
            actor->EvaluatePackage(true,false);
            Log::Info("[Skald] {} has not moved for {:.0f} s while walking back; resetting the animation graph.",member.name,SkaldPolicy::kFollowStall);
        }
        if(distance<rejoin.best-SkaldPolicy::kRejoinProgress){rejoin.best=distance;rejoin.since=now;}
        else if(now-rejoin.since>SkaldPolicy::kRejoinPatience){
            // A ledge, a gap or a closed door: play where it stands for a while instead.
            rejoin.active=false;rejoin.restAt=now+SkaldPolicy::kRejoinRest;
            Log::Info("[Skald] {} cannot get closer to you ({:.0f} away); plays where it stands for {:.0f} s.",member.name,distance,SkaldPolicy::kRejoinRest);
        }
    }
}
constexpr const char* kNoteSound=R"(Sound\fx\FightWithMe\Skald\SkaldNote.wav)";
bool PlayLute(RE::BSSoundHandle& sound,RE::Actor* actor,const char* path,float volume){
    auto* audio=RE::BSAudioManager::GetSingleton();if(!audio)return false;
    RE::BSResource::ID file;
    file.GenerateFromPath(path);
    audio->GetSoundHandleByFile(sound,file,0x1A,0);
    if(sound.soundID==RE::BSSoundHandle::kInvalidID)return false;
    sound.SetPosition(actor->GetPosition());sound.SetObjectToFollow(actor->Get3D());sound.SetVolume(volume);
    return sound.Play();
}
bool BeginMusic(Performance& p,RE::Actor* actor){
    return PlayLute(p.music,actor,p.note?kNoteSound:SkaldMusic::tracks[Index(p.song)].file,SkaldPolicy::LuteVolume(p.note));
}
// The spectral lute is placed on Skyrim's hidden one, white for the Single Note and
// in the Ljodh's colour for a song. Called every tick the lute is up: an overlay
// left unplaced for a tick is hidden (SweepLutes).
bool Pose(Performance& p,RE::Actor*){
    if(!p.lute.Find("Instrument"))return false;
    auto* root=Posing::RootOf(p.lute.effect.get());if(!root||!p.prop||!p.prop->parent)return false;
    // While Skyrim's lute swings in from the back, the spectral one waits; a walking
    // lute stays in the hands meanwhile, and is swapped for this one without a fade.
    if(p.lifting){Posing::Out(p.lamp);return true;}
    if(walkingLutes.contains(p.id))p.liftedAt=-100.f;
    StowLute(p.id);DropSpare(p.id);
    const float accent=p.lastBeat<0?0.f:std::exp(-std::max(0.f,p.playing-p.lastBeat)*10.f);
    const auto color=p.playing<p.goldUntil?std::array<float,3>{1.f,.78f,.26f}:p.note?SkaldPolicy::kNoteLute:colors[Index(p.song)];
    return PoseLute(p.lute,p.luteVeil,p.lamp,WorldOf(p.prop.get()),color,now,accent,p.rank,SkaldPolicy::Appear(p.age-p.liftedAt));
}
// Away from every recorded playing frame: the lute is still swinging into place.
bool OffPose(RE::FormID id,RE::NiAVObject* prop){
    const auto rec=recordings.find(id);
    if(rec==recordings.end()||!rec->second.best||!prop||!prop->parent||rec->second.best->parent!=prop->parent->name.c_str())return false;
    const auto& track=rec->second.best->prop;
    return std::ranges::none_of(track,[&](const RE::NiTransform& frame){return (frame.translate-prop->local.translate).Length()<=SkaldPolicy::kOffPose;});
}
// Every lute prop on the actor. A repeated idle request can attach a second
// one; only the first is played.
std::vector<RE::NiAVObject*> LuteProps(RE::NiAVObject* root){
    std::vector<RE::NiAVObject*> out;
    if(!root)return out;
    RE::BSVisit::TraverseScenegraphObjects(root,[&](RE::NiAVObject* object){
        if(object->name=="AnimObjectLute"&&object->parent)out.push_back(object);
        return RE::BSVisit::BSVisitControl::kContinue;
    });
    return out;
}
// Every lute already on the bard, but the one a performance takes over, is a left-over.
void NoteStale(Performance& p,RE::Actor* actor){
    for(auto* prop:LuteProps(actor->Get3D()))if(prop!=p.prop.get())p.stale.emplace_back(prop,prop->local.translate);
}
struct Animation {
    static bool Thunk(RE::IAnimationGraphManagerHolder* holder,const RE::BSFixedString& event){
        if(!ownEvent&&Performing(static_cast<RE::TESObjectREFR*>(holder)->GetFormID())){
            const std::string_view text=event.c_str();
            if(text.starts_with("attackStart")||text.starts_with("AttackStart")||text=="weaponDraw"||text=="bowDraw"||text=="IdleStop"||
               text=="blockStart"||text=="MLh_SpellFire_Event"||text=="MRh_SpellFire_Event")return false;
        }
        return original(holder,event);
    }
    static inline REL::Relocation<decltype(Thunk)> original;
};
// Native attack stamina cost, chained after other mods. Signature/call site documented in Valhalla Combat.
// https://github.com/D7ry/valhallaCombat/blob/master/src/include/Hooks.h
struct AttackCost {
    static float Thunk(RE::ActorValueOwner* owner,RE::BGSAttackData* attack){
        const float cost=original(owner,attack);
        if(!owner||!attack||!attack->data.flags.any(RE::AttackData::AttackFlag::kPowerAttack))return cost;
        auto* actor=static_cast<RE::Actor*>(owner);
        return cost*PowerCost(actor->GetFormID());
    }
    static inline REL::Relocation<decltype(Thunk)> original;
};
// The stance spell is held, never cast: the combat AI's offensive caster never gets
// to start it for a bard holding the stance. Anyone else casts it as usual.
struct StanceCast {
    static bool Thunk(RE::CombatMagicCaster* caster,RE::CombatController* controller){
        if(caster&&controller&&caster->magicItem&&caster->magicItem->GetFormID()==SkaldPolicy::kStanceSpell){
            const auto handle=controller->attackerHandle.native_handle();
            std::scoped_lock lock(gate);if(stanceHolders.contains(handle))return false;
        }
        return original(caster,controller);
    }
    static inline REL::Relocation<decltype(Thunk)> original;
};
struct CastTimer {
    static void Thunk(RE::ActorMagicCaster* caster){
        original(caster);
        if(caster&&caster->actor&&caster->currentSpell&&caster->currentSpell->GetSpellType()==RE::MagicSystem::SpellType::kSpell)
            caster->castingTimer*=1.f-CastingSpeed(caster->actor->GetFormID());
    }
    static inline REL::Relocation<decltype(Thunk)> original;
};
struct InspiredSpell {
    static void Thunk(RE::ActorMagicCaster* caster,bool cast,std::uint32_t arg,RE::MagicItem* spell){
        auto* actor=caster?caster->actor:nullptr;
        // Refund only energy the native caster actually charged. The charge is
        // consumed once; free casts, powers, staves and concentration ticks cannot farm it.
        const float charged=caster?std::max(0.f,std::min(caster->costCharged,caster->currentSpellCost)):0.f;
        const bool eligible=cast&&actor&&actor->IsPlayerRef()&&spell&&charged>0.f&&
            spell->GetSpellType()==RE::MagicSystem::SpellType::kSpell&&spell->GetCastingType()==RE::MagicSystem::CastingType::kFireAndForget;
        original(caster,cast,arg,spell);
        if(!eligible)return;
        std::scoped_lock lock(gate);const auto it=benefits.find(actor->GetFormID());if(it==benefits.end())return;
        auto& boon=it->second.songs[Index(Song::Echoes)];if(boon.until<=now||boon.inspirationSpent||boon.branchB<=0.f)return;
        rewards.push_back({actor->GetHandle(),{},0.f,true,charged*std::clamp(boon.branchB,0.f,1.f)});
        boon.inspirationSpent=true;boon.branchB=0.f;
    }
    static inline REL::Relocation<decltype(Thunk)> original;
};
}

bool HasLute(RE::Actor* actor){
    if(!actor)return false;
    const auto inventory=actor->GetInventoryCounts([](RE::TESBoundObject& item){return item.GetFormID()==SkaldPolicy::kLute;});
    return std::ranges::any_of(inventory,[](const auto& entry){return entry.second>0;});
}
bool IsSong(Paths::Ability ability){return ability>=Paths::Ability::kFervor&&ability<=Paths::Ability::kGlory;}
bool Performing(RE::FormID id){
    if(!Host::Abilities())return AbilityLink::Performing(id);
    std::scoped_lock lock(gate);return performing.contains(id);
}
bool Singing(RE::FormID id){
    return std::ranges::any_of(performances,[id](const Performance& p){return p.id==id&&!p.note&&!p.finished;});
}
bool Relocating(RE::FormID id){
    if(!Host::Abilities())return AbilityLink::SkaldRelocating(id);
    const auto it=rejoins.find(id);return it!=rejoins.end()&&it->second.active;
}
namespace {
// An enemy at arm's length: fists answer it instead of a new note or song.
bool WithinFists(RE::Actor* actor){
    return std::ranges::any_of(Enemies::All(),[&](const auto& enemy){
        auto who=enemy.handle.get();return Standing(who.get())&&Game::Distance(actor,who.get())<=SkaldPolicy::kFistReach;});
}
}
RE::SpellItem* StanceSpell(){static auto* spell=RE::TESForm::LookupByID<RE::SpellItem>(SkaldPolicy::kStanceSpell);return spell;}
bool FistsReady(RE::Actor* actor){
    if(!Standing(actor)||!actor->IsInCombat()||Performing(actor->GetFormID()))return false;
    // Holding its stance spell, the combat AI has something to fight with, and fists
    // only walked it into the fight after its target. A mage gives ground instead.
    if(auto* spell=StanceSpell();spell&&actor->HasSpell(spell))return false;
    // Without it (no lute, or the tree locked) the AI needs something it may use or
    // it runs from the fight: fists while walking back, and for an enemy already at
    // arm's length.
    return Relocating(actor->GetFormID())||WithinFists(actor);
}
bool CasterStance(RE::Actor* actor){
    return Standing(actor)&&actor->IsInCombat()&&!Performing(actor->GetFormID());
}
bool CanTavernLute(RE::FormID actor){
    std::scoped_lock lock(gate);return tavernReady.contains(actor)&&!performing.contains(actor);
}
void OnHit(const RE::TESHitEvent* event){
    if(!event||!event->target||!event->cause||event->target==event->cause)return;
    // Friendly healing and other non-hostile spells are not interruptions.
    auto* source=RE::TESForm::LookupByID(event->source);
    if(auto* spell=source?source->As<RE::MagicItem>():nullptr;spell&&!spell->IsHostile())return;
    std::scoped_lock lock(gate);
    const auto id=event->target->GetFormID();if(performing.contains(id))interrupted.insert(id);
}
bool Useful(RE::Actor* actor,Paths::Ability ability){
    if(!actor||!HasLute(actor)||Singing(actor->GetFormID())||Relocating(actor->GetFormID())||WithinFists(actor))return false;
    if(ability==Paths::Ability::kEchoes){
        for(const auto& ally:Allies(actor))if(auto who=ally.get();who&&Waiting(who->GetFormID()))return true;
        return false;
    }
    int enemies=0;
    for(const auto& enemy:Enemies::All())if(auto who=enemy.handle.get();Near(actor,who.get()))++enemies;
    return enemies>0;
}
bool Cast(RE::Actor* actor,const PathsLink::Kit& kit,Paths::Ability ability){
    if(!Host::Abilities()||!Standing(actor)||actor->IsPlayerRef()||!IsSong(ability)||Singing(actor->GetFormID())||
        !HasLute(actor)||!kit.record||!kit.record->skaldUnlocked||kit.Rank(ability)<1||Unsettled(actor)||Relocating(actor->GetFormID()))return false;
    const auto* orders=Party::FindOrders(actor->GetFormID());
    if(!orders||orders->role!=Role::kSkald||actor->GetCurrentScene()||actor->GetOccupiedFurniture().get()||
        actor->AsActorState()->GetSitSleepState()!=RE::SIT_SLEEP_STATE::kNormal)return false;
    // Animal companions cannot perform the humanoid lute animation.
    auto* root=actor->Get3D();
    if(!root||!root->GetObjectByName("NPC Spine2 [Spn2]"))return false;
    Performance p;
    p.actor=actor->GetHandle();p.id=actor->GetFormID();p.cell=actor->GetParentCell();p.worldspace=actor->GetWorldspace();p.song=SongOf(ability);p.ability=ability;p.rank=kit.Rank(ability);
    p.drawn=actor->AsActorState()->IsWeaponDrawn();
    p.debug=PathsLink::DebugCasting(p.id);
    p.cost=Abilities::ResourceCost(actor,ability);
    p.serial=++nextPerformance;p.chance=SkaldPolicy::Chance(kit.Rank(Perk::kOldTongue));
    p.duration=SkaldPolicy::Duration(p.song,kit.Rank(Perk::kLingeringVerse));
    p.shield=Game::Maximum(actor,RE::ActorValue::kHealth)*SkaldPolicy::Rank(kit.Rank(Perk::kSpectralAegis),.1f,.2f,.3f);
    switch(p.song){
    case Song::Fervor:p.a=SkaldPolicy::Speed(kit.Rank(Perk::kSpellweave));p.b=SkaldPolicy::Rank(kit.Rank(Perk::kRelentlessRhythm),.1f,.2f,.3f);break;
    case Song::Echoes:p.a=SkaldPolicy::Rank(kit.Rank(Perk::kEncore),1.f,2.f,3.f);p.b=SkaldPolicy::Rank(kit.Rank(Perk::kInspiredCasting),.1f,.2f,.3f);break;
    case Song::Hollow:p.a=SkaldPolicy::Weakness(kit.Rank(Perk::kLeadfootVerse));p.b=SkaldPolicy::Rank(kit.Rank(Perk::kBrokenConcentration),.05f,.1f,.15f);break;
    case Song::Glory:p.a=.05f+SkaldPolicy::Chance(kit.Rank(Perk::kKeenChorus));p.b=SkaldPolicy::Rank(kit.Rank(Perk::kVictoriousVerse),.02f,.04f,.06f);break;
    }
    p.copy=static_cast<int>(Index(p.song));
    p.lute.effect.reset(Track(Effects::SkaldModel(actor,p.copy,true,25.f)));
    if(!p.lute.effect)return false;
    bool inheritedIdle=false,followsNote=false;
    std::erase_if(performances,[&](Performance& old){
        if(old.id!=actor->GetFormID()||!old.note)return false;
        inheritedIdle=old.idle;old.idle=false;old.drawn=false;followsNote=true;
        // Keep the same prop and spectral instrument during the note -> song
        // handoff. Re-cloning it here briefly left the hands empty each time.
        if(old.prop)p.prop=std::move(old.prop);
        p.stale=std::move(old.stale);old.stale.clear();
        if(old.lute.effect){p.lute.Drop();p.lute=std::move(old.lute);p.luteVeil=std::move(old.luteVeil);old.lute={};old.luteVeil={};}
        End(old,false,"Ljodh takes priority");return true;
    });
    if(!followsNote)NoteStale(p,actor);
    p.idle=inheritedIdle;
    {
        std::scoped_lock lock(gate);performing.insert(p.id);shields[p.id]=p.shield;
    }
    // A bard playing on the move stops where it is for a Ljodh: left to the combat
    // AI with its stance spell put away, it ran off while the lute idle waited.
    if(!inheritedIdle){OwnEvent own;actor->InterruptCast(true);actor->DrawWeaponMagicHands(false);Freeze::Anchor(actor,anchor);}
    performances.push_back(std::move(p));
    return true;
}
bool NoteTarget(RE::Actor* actor,RE::Actor* target){
    return Standing(target)&&target!=actor&&!Party::IsPartyActor(target)&&!target->IsPlayerRef()&&
        std::ranges::any_of(Enemies::All(),[&](const auto& enemy){return enemy.handle==target->GetHandle();});
}
RE::ActorHandle NoteTargetFor(RE::Actor* actor){
    auto usable=[&](RE::Actor* who){return NoteTarget(actor,who)&&Game::Distance(actor,who)<=SkaldPolicy::kNoteReach&&Near(actor,who);};
    if(const auto* controller=Game::Controller(actor))if(auto who=controller->targetHandle.get();usable(who.get()))return who->GetHandle();
    RE::ActorHandle chosen;float closest=SkaldPolicy::kNoteReach;
    for(const auto& enemy:Enemies::All())if(auto who=enemy.handle.get();usable(who.get())){
        const float distance=Game::Distance(actor,who.get());if(distance<=closest){closest=distance;chosen=enemy.handle;}
    }
    return chosen;
}
void StartNotes(float seconds){
    // Target selection raycasts every enemy; a few checks a second are plenty.
    noteScan-=seconds;if(noteScan>0)return;noteScan=.2f;
    std::erase_if(noteReady,[](const auto& entry){return !Party::Find(entry.first);});
    for(const auto& member:Party::Members()){
        const auto* orders=Party::FindOrders(member.id);if(!orders||orders->role!=Role::kSkald||Performing(member.id)||Relocating(member.id)||noteReady[member.id]>now)continue;
        auto actor=member.handle.get();
        if(!Standing(actor.get())||!actor->IsInCombat()||Unsettled(actor.get())||WithinFists(actor.get())||
            actor->GetCurrentScene()||actor->GetOccupiedFurniture().get()||actor->AsActorState()->GetSitSleepState()!=RE::SIT_SLEEP_STATE::kNormal)continue;
        // Once its arms are recorded the bard plays on the move instead (TickStrums);
        // until then it stands to play, which is what records them.
        if(Recorded(member.id,actor.get()))continue;
        const auto kit=PathsLink::KitOf(member.id,Role::kSkald);
        if(!kit.record||!kit.record->skaldUnlocked||!HasLute(actor.get())||!actor->Get3D()->GetObjectByName("NPC Spine2 [Spn2]"))continue;
        // Settled, it always plays: with nobody in reach it strums, which also keeps
        // the combat AI from walking it toward an enemy.
        const auto target=NoteTargetFor(actor.get());
        noteReady[member.id]=now+3.f; // failed setup is bounded too
        Performance p;p.actor=member.handle;p.target=target;p.id=member.id;p.cell=actor->GetParentCell();p.worldspace=actor->GetWorldspace();
        p.note=true;p.song=Song::Echoes;p.rank=1;p.drawn=actor->AsActorState()->IsWeaponDrawn();p.aim=.3f;
        p.lute.effect.reset(Track(Effects::SkaldModel(actor.get(),static_cast<int>(Song::Echoes),true,12.f)));
        if(!p.lute.effect)continue;
        NoteStale(p,actor.get());
        {std::scoped_lock lock(gate);performing.insert(p.id);}
        // Stopped for it, as for a Ljodh (see Cast).
        {OwnEvent own;actor->InterruptCast(true);actor->DrawWeaponMagicHands(false);Freeze::Anchor(actor.get(),anchor);}
        performances.push_back(std::move(p));
    }
}
// Skyrim's lute is never drawn on a fighting or playing bard: the spectral lute is
// placed on it instead. One on a fighting bard who is not playing is a left-over
// (the exit of a performance, or one a hit left behind) and Skyrim is asked to put
// it away. Out of a fight left-overs stay hidden, unless an idle plays one again.
void TickStrays(){
    std::erase_if(strays,[](const auto& pair){return !Party::Find(pair.first);});
    for(const auto& member:Party::Members()){
        const auto* orders=Party::FindOrders(member.id);auto actor=member.handle.get();
        if(!orders||orders->role!=Role::kSkald||!actor||!actor->Is3DLoaded())continue;
        const auto playing=std::ranges::find_if(performances,[&](const Performance& p){return p.id==member.id&&!p.finished;});
        const bool fighting=actor->IsInCombat()&&playing==performances.end();
        const auto* live=playing!=performances.end()?playing->prop.get():nullptr;
        const auto props=LuteProps(actor->Get3D());
        if(actor->IsInCombat()||playing!=performances.end())for(auto* prop:props)prop->SetAppCulled(true);
        auto& stray=strays[member.id];
        std::erase_if(stray.props,[&](StrayLute& s){
            auto* node=s.node.get();
            if(node==live||std::ranges::find(props,node)==props.end())return true;
            if(now-s.since<SkaldPolicy::kStraySettle){s.local=node->local.translate;return false;}
            if(fighting||!SkaldPolicy::StrayMoving(now-s.since,(node->local.translate-s.local).Length()))return false;
            node->SetAppCulled(false);return true; // an idle plays it again
        });
        if(fighting)for(auto* prop:props)if(std::ranges::none_of(stray.props,[&](const StrayLute& s){return s.node.get()==prop;}))
            stray.props.push_back({RE::NiPointer<RE::NiAVObject>(prop),prop->local.translate,now});
        if(playing==performances.end())for(auto& s:stray.props)s.node->SetAppCulled(true);
        if(!fighting||stray.props.empty()){stray.stopAt=0.f;continue;}
        // The exit of a song puts it away by itself; ask again only if it stays.
        if(stray.stopAt<=0.f){stray.stopAt=now+SkaldPolicy::kStrayStop;continue;}
        if(now<stray.stopAt||!Standing(actor.get())||Unsettled(actor.get()))continue;
        stray.stopAt=now+SkaldPolicy::kStrayStop;
        {OwnEvent own;actor->NotifyAnimationGraph("IdleStop");}
        if(now-stray.warned>10.f){
            stray.warned=now;
            Log::Info("[Skald] {}'s lute from an earlier song is still out ({}); hidden, and Skyrim is asked to put it away.",member.name,stray.props.size());
        }
    }
}
// Every lute effect is drawn only on a tick that placed it; any its owner let go
// of without dropping it is put out.
void SweepLutes(){
    std::erase_if(spares,[](auto& entry){
        if(now-entry.second.at<SkaldPolicy::kSpareFor)return false;
        entry.second.veil.Drop();entry.second.lute.Drop();return true;
    });
    std::unordered_set<const RE::BSTempEffectParticle*> owned;
    for(const auto& p:performances)if(p.lute.effect)owned.insert(p.lute.effect.get());
    for(const auto& [id,walk]:walkingLutes)if(walk.lute.effect)owned.insert(walk.lute.effect.get());
    for(const auto& [id,spare]:spares)if(spare.lute.effect)owned.insert(spare.lute.effect.get());
    std::erase_if(luteEffects,[&](RE::NiPointer<RE::BSTempEffectParticle>& effect){
        auto* root=Posing::RootOf(effect.get());
        // Let go of, it is hidden as well, in case Skyrim keeps it a while.
        if(effect->lifetime<=0.f||effect->age>=effect->lifetime||!owned.contains(effect.get())){
            effect->lifetime=0.f;if(root)root->SetAppCulled(true);return true;
        }
        if(root)root->SetAppCulled(!placedLutes.contains(effect.get()));
        return false;
    });
    placedLutes.clear();
}
bool LaunchNote(RE::Actor* actor,const RE::ActorHandle& aimed){
    auto target=aimed.get();if(!NoteTarget(actor,target.get())||Game::Distance(actor,target.get())>SkaldPolicy::kNoteReach||!Near(actor,target.get()))return false;
    NotePulse pulse;pulse.actor=actor->GetHandle();pulse.target=aimed;pulse.cell=actor->GetParentCell();
    pulse.at=pulse.origin=actor->GetPosition()+RE::NiPoint3{0,0,55};
    if(Settings::Get().abilityEffects)pulse.art.effect.reset(Effects::SkaldHit(actor,static_cast<std::size_t>(Song::Echoes),SkaldPolicy::kNoteLifetime+.5f));
    notes.push_back(std::move(pulse));
    Log::Info("[Skald] {} plucks a single note toward {}.",Game::Name(actor),Game::Name(target.get()));
    return true;
}
bool LaunchNote(Performance& p,RE::Actor* actor){
    if(!LaunchNote(actor,p.target))return false;
    p.noteFired=true;p.lastBeat=p.playing;
    p.pluckAt=now+SkaldPolicy::NoteInterval(std::uniform_real_distribution<float>(0.f,1.f)(rng));
    return true;
}
// The Single Note on the move. While the arm layer plays a bard's lute (TickLayers),
// a note is plucked every one to three seconds at the nearest enemy in reach, and the
// legs go wherever Fight With Me's leash and the combat AI take them. Like a mage it
// keeps playing at an enemy that reaches it; the note's slow helps it get away.
void TickStrums(){
    std::erase_if(strums,[](auto& entry){
        if(walkingLutes.contains(entry.first))return false;
        entry.second.sound.FadeOutAndRelease(150);return true;
    });
    std::unordered_set<RE::FormID> active;
    for(const auto& [id,walk]:walkingLutes){
        auto actor=walk.actor.get();if(!Standing(actor.get()))continue;
        // A Ljodh coming up keeps the walking lute only until its own is in place.
        if(std::ranges::any_of(performances,[id](const Performance& p){return p.id==id&&!p.finished;}))continue;
        auto& strum=strums[id];
        if(now>=strum.checkAt){
            strum.checkAt=now+.5f;
            const auto kit=PathsLink::KitOf(id,Role::kSkald);
            strum.may=kit.record&&kit.record->skaldUnlocked&&HasLute(actor.get());
        }
        if(!strum.may)continue;
        active.insert(id);
        if(now>=strum.reportAt){
            strum.reportAt=now+SkaldPolicy::kStrumReport;
            float nearest=-1.f;
            for(const auto& enemy:Enemies::All())if(auto who=enemy.handle.get();Standing(who.get())){
                const float distance=Game::Distance(actor.get(),who.get());if(nearest<0.f||distance<nearest)nearest=distance;
            }
            Log::Info("[Skald] {} plays on the move: {:.0f} from you, nearest enemy {:.0f}.",Game::Name(actor.get()),
                Game::Distance(actor.get(),RE::PlayerCharacter::GetSingleton()),nearest);
        }
        // A moment for the arms to come in before the first pluck.
        if(strum.pluckAt<=0.f){strum.pluckAt=now+SkaldPolicy::kStrumIn;continue;}
        if(now<strum.pluckAt)continue;
        strum.pluckAt=now+SkaldPolicy::NoteInterval(std::uniform_real_distribution<float>(0.f,1.f)(rng));
        strum.pluckedAt=now;
        // A strum with nobody clear to aim at is still music; only a target takes a note.
        strum.target=NoteTargetFor(actor.get());
        if(strum.target)LaunchNote(actor.get(),strum.target);
        strum.sound.FadeOutAndRelease(30);
        PlayLute(strum.sound,actor.get(),kNoteSound,SkaldPolicy::LuteVolume(true));
    }
    std::scoped_lock lock(gate);strumming=std::move(active);
}
// The stance spell is lent to every playing bard in a fight and taken back when the
// fight ends or the game saves; one that knew it already keeps it. Its hands' art
// hangs from the magic nodes, which stay hidden while it holds the stance.
std::unordered_set<RE::FormID> lent,bareHands;
std::unordered_map<RE::FormID,float> stanceChecked;
void ShowHands(RE::Actor* actor,bool shown){
    auto* root=actor?actor->Get3D():nullptr;if(!root)return;
    for(const auto* name:{"NPC R MagicNode [RMag]","NPC L MagicNode [LMag]"})if(auto* node=root->GetObjectByName(name))node->SetAppCulled(!shown);
}
void TakeBack(RE::FormID id){
    auto* actor=RE::TESForm::LookupByID<RE::Actor>(id);
    if(bareHands.erase(id))ShowHands(actor,true);
    if(lent.erase(id)&&actor&&StanceSpell()){actor->InterruptCast(false);actor->RemoveSpell(StanceSpell());}
}
void TickStance(){
    auto* spell=StanceSpell();if(!spell)return;
    std::unordered_set<RE::FormID> holding;
    for(const auto& member:Party::Members()){
        const auto* orders=Party::FindOrders(member.id);auto actor=member.handle.get();
        // Lent for the whole fight; Fight With Me lets the AI hold it only in CasterStance.
        if(!orders||orders->role!=Role::kSkald||!actor||!actor->Is3DLoaded()||!Standing(actor.get())||!actor->IsInCombat())continue;
        // Only a bard that plays: the tree unlocked and a lute carried. Looked at twice
        // a second; the hands are hidden again then too, in case the 3D was rebuilt.
        auto& checked=stanceChecked[member.id];
        const bool look=now-checked>.5f||!bareHands.contains(member.id);
        if(look){
            checked=now;const auto kit=PathsLink::KitOf(member.id,Role::kSkald);
            if(!kit.record||!kit.record->skaldUnlocked||!HasLute(actor.get()))continue;
        }
        holding.insert(member.id);
        if(!lent.contains(member.id)&&!actor->HasSpell(spell)){
            actor->AddSpell(spell);lent.insert(member.id);
            // A dirty combat inventory visits the actor's spells again, this one now among them.
            if(auto* controller=Game::Controller(actor.get());controller&&controller->inventory)controller->inventory->dirty=true;
            Log::Info("[Skald] {} holds {} like a mage, never cast: the combat AI keeps it at range.",member.name,spell->GetName());
        }
        if(look){bareHands.insert(member.id);ShowHands(actor.get(),false);}
        // The cast hook keeps it from casting; this only catches one that got through.
        for(const auto source:{RE::MagicSystem::CastingSource::kLeftHand,RE::MagicSystem::CastingSource::kRightHand}){
            auto* caster=actor->GetMagicCaster(source);if(!caster||caster->currentSpell!=spell)continue;
            const auto state=caster->state.get();
            if(state==RE::MagicCaster::State::kCharging||state==RE::MagicCaster::State::kCasting||state==RE::MagicCaster::State::kReady){
                actor->InterruptCast(false);
                Log::Warn("[Skald] {} began casting {}; interrupted.",member.name,spell->GetName());
            }
        }
    }
    std::vector<RE::FormID> done;
    for(const auto id:lent)if(!holding.contains(id))done.push_back(id);
    for(const auto id:bareHands)if(!holding.contains(id)&&!lent.contains(id))done.push_back(id);
    for(const auto id:done)TakeBack(id);
    std::erase_if(stanceChecked,[&](const auto& entry){return !holding.contains(entry.first);});
    std::scoped_lock lock(gate);stanceHolders.clear();
    for(const auto id:holding)if(auto* actor=RE::TESForm::LookupByID<RE::Actor>(id))stanceHolders.insert(actor->GetHandle().native_handle());
}
struct DiscordLook {const char* name;std::uint32_t glyph,color;};
constexpr std::uint32_t Rgb(std::uint32_t r,std::uint32_t g,std::uint32_t b){return 0xFF000000u|(b<<16)|(g<<8)|r;}
// Icon-font glyphs, in Discord order.
constexpr std::array<DiscordLook,SkaldPolicy::kDiscords> discordLooks{{
    {"Weakened",0xF6DE,Rgb(236,96,64)},
    {"Off-Beat",0xF2F2,Rgb(244,196,72)},
    {"Exposed",0xF05B,Rgb(232,62,92)},
    {"Winded",0xF72E,Rgb(150,214,96)},
    {"Muddled",0xF5DC,Rgb(96,156,246)},
    {"Jarred",0xF0E7,Rgb(196,126,252)},
}};
// In Crescendo order: snowflake, chains, moon, muted speaker.
constexpr std::array<DiscordLook,SkaldPolicy::kCrescendos> crescendoLooks{{
    {"frozen solid",0xF2DC,Rgb(160,224,255)},
    {"paralyzed",0xF0C1,Rgb(250,206,90)},
    {"lulled out of the fight",0xF186,Rgb(190,170,255)},
    {"silenced",0xF6A9,Rgb(240,110,200)},
}};
void AddMark(RE::Actor* target,unsigned key,unsigned count,const DiscordLook& look,float life,bool major){
    // A repeat of the same kind pops the existing icon with its new count.
    const auto handle=target->GetHandle();
    if(auto it=std::ranges::find_if(marks,[&](const Mark& m){return m.target==handle&&m.key==key;});it!=marks.end()){it->count=count;it->born=now;it->life=life;}
    else marks.push_back({handle,key,count,now,life,look.glyph,look.color,major});
}
// A note rung at the enemy; the next of the bard's phrase unless a pitch is given.
void RingNote(RE::Actor* bard,RE::Actor* target,float pitch=0.f){
    auto* audio=RE::BSAudioManager::GetSingleton();if(!audio)return;
    RE::BSSoundHandle ring;RE::BSResource::ID file;
    file.GenerateFromPath(R"(Sound\fx\FightWithMe\Skald\SkaldNote.wav)");
    audio->GetSoundHandleByFile(ring,file,0x1A,0);
    if(ring.soundID==RE::BSSoundHandle::kInvalidID)return;
    if(pitch<=0.f)pitch=SkaldPolicy::NotePitch(phrase[bard->GetFormID()]++);
    ring.SetPosition(target->GetPosition()+RE::NiPoint3{0,0,60});ring.SetObjectToFollow(target->Get3D());
    ring.SetVolume(SkaldPolicy::kRingVolume);ring.SetFrequency(pitch);
    if(ring.Play())ring.SetFrequency(pitch);
}
// Rarely, a landed note holds its target outright, with a full chord.
void ApplyCrescendo(RE::Actor* bard,RE::Actor* target){
    using SkaldPolicy::Crescendo;
    if(target->AsActorState()->IsFlying())return;
    const auto id=target->GetFormID();
    const bool hasMagicka=Game::Current(target,RE::ActorValue::kMagicka)>=20.f;
    Crescendo kind{};
    {
        std::scoped_lock lock(gate);auto& state=discords[id];
        std::uniform_real_distribution<float> roll(0.f,1.f);
        if(!SkaldPolicy::CrescendoLands(roll(rng),now-state.crescendoAt))return;
        state.crescendoAt=now;kind=SkaldPolicy::PickCrescendo(roll(rng),hasMagicka);
    }
    const float seconds=SkaldPolicy::CrescendoSeconds(kind);
    auto* values=target->AsActorValueOwner();
    switch(kind){
    case Crescendo::Frozen:
        Freeze::Hold(target,0.f,seconds);Effects::Flash(target,Effects::Look::kShaken,seconds);break;
    case Crescendo::Paralyzed:
        Abilities::Lift(id,RE::ActorValue::kParalysis,1.f,seconds,crescendoSource);Effects::Flash(target,Effects::Look::kPinned,seconds);break;
    case Crescendo::Lullaby:
        // Unaggressive and out of the fight until it wears off, or until it is struck.
        Abilities::Lift(id,RE::ActorValue::kAggression,-Game::Current(target,RE::ActorValue::kAggression),seconds,crescendoSource);
        target->StopCombat();Effects::Flash(target,Effects::Look::kMagnus,seconds);break;
    case Crescendo::Silenced:
        target->InterruptCast(true);
        values->DamageActorValue(RE::ActorValue::kMagicka,Game::Current(target,RE::ActorValue::kMagicka));
        Abilities::Lift(id,RE::ActorValue::kMagickaRateMult,-Game::Current(target,RE::ActorValue::kMagickaRateMult),seconds,crescendoSource);
        Effects::Flash(target,Effects::Look::kSurge,seconds);break;
    default:break;
    }
    const auto& look=crescendoLooks[static_cast<std::size_t>(kind)];
    AddMark(target,100u+static_cast<unsigned>(kind),0,look,seconds,true);
    for(const float pitch:{1.f,1.26f,1.5f})RingNote(bard,target,pitch);
    Notices::Push(Trf("{}'s crescendo: {} is {} for {:.0f} s.",Game::Name(bard),Game::Name(target),Tr(look.name),seconds),Notices::Kind::kInfo,look.glyph);
    Log::Info("[Skald] Crescendo from {}: {} is {} for {:.0f} s.",Game::Name(bard),Game::Name(target),look.name,seconds);
}
void ApplyDiscord(RE::Actor* bard,RE::Actor* target){
    using SkaldPolicy::Discord;
    const auto id=target->GetFormID();
    const bool hasMagicka=Game::Maximum(target,RE::ActorValue::kMagicka)>=20.f;
    Discord kind{};unsigned stacks{};
    {
        std::scoped_lock lock(gate);auto& state=discords[id];
        kind=SkaldPolicy::PickDiscord(std::uniform_real_distribution<float>(0.f,1.f)(rng),hasMagicka,now-state.jarredAt>=SkaldPolicy::kJarredCooldown);
        const auto i=static_cast<std::size_t>(kind);
        if(state.until[i]<=now)state.stacks[i]=0;
        state.stacks[i]=std::min(state.stacks[i]+1,SkaldPolicy::kDiscordStacks);state.until[i]=now+SkaldPolicy::kDiscordSeconds;
        if(kind==Discord::Jarred)state.jarredAt=now;
        stacks=state.stacks[i];
    }
    auto* values=target->AsActorValueOwner();
    switch(kind){
    case Discord::OffBeat:
        for(const auto value:{RE::ActorValue::kWeaponSpeedMult,RE::ActorValue::kLeftWeaponSpeedMultiply})
            Abilities::Lift(id,value,Raise(target,value,-SkaldPolicy::DiscordOffBeat(stacks),offBeatSource),SkaldPolicy::kDiscordSeconds,offBeatSource);
        break;
    case Discord::Winded:values->DamageActorValue(RE::ActorValue::kStamina,Game::Maximum(target,RE::ActorValue::kStamina)*SkaldPolicy::DiscordDrain(stacks));break;
    case Discord::Muddled:values->DamageActorValue(RE::ActorValue::kMagicka,Game::Maximum(target,RE::ActorValue::kMagicka)*SkaldPolicy::DiscordDrain(stacks));break;
    case Discord::Jarred:Game::Stagger(target,SkaldPolicy::kJarredStagger,bard);break;
    default:break; // Weakened and Exposed act on damage as it is dealt (Outgoing)
    }
    AddMark(target,static_cast<unsigned>(kind),stacks,discordLooks[static_cast<std::size_t>(kind)],SkaldPolicy::kMarkSeconds,false);
    Log::Info("[Skald] Single note reaches {}: slowed, {} x{}.",Game::Name(target),discordLooks[static_cast<std::size_t>(kind)].name,stacks);
}
void PlaceMarks(){
    std::erase_if(marks,[](const Mark& m){auto who=m.target.get();return now-m.born>=m.life||!who||!who->Is3DLoaded()||who->IsDead();});
    std::vector<NoteMark> placed;
    for(std::size_t i=0;i<marks.size();++i){
        const auto& mark=marks[i];auto who=mark.target.get();
        // Several icons over one enemy sit side by side, in the order they arrived.
        unsigned slot=0,slots=0;
        for(std::size_t j=0;j<marks.size();++j)if(marks[j].target==mark.target){if(j<i)++slot;++slots;}
        if(const auto at=Game::Project(Game::HeadPosition(who.get())+RE::NiPoint3{0,0,30}))
            placed.push_back({at->first,at->second,now-mark.born,mark.life,mark.glyph,mark.color,mark.count,slot,slots,mark.major});
    }
    std::scoped_lock lock(markLock);placedMarks.swap(placed);
}
void TickNotes(float seconds){
    std::erase_if(notes,[seconds](NotePulse& pulse){
        auto actor=pulse.actor.get(),target=pulse.target.get();pulse.age+=seconds;
        const auto drop=[&](){pulse.Drop();return true;};
        if(!Standing(actor.get())||actor->GetParentCell()!=pulse.cell||!NoteTarget(actor.get(),target.get())||pulse.age>SkaldPolicy::kNoteLifetime)return drop();
        if(target->GetParentCell()!=pulse.cell&&(!actor->GetWorldspace()||actor->GetWorldspace()!=target->GetWorldspace()))return drop();
        const auto aim=target->GetPosition()+RE::NiPoint3{0,0,45};const auto delta=aim-pulse.at;const float distance=delta.Length();
        const float step=std::min(distance,SkaldPolicy::kNoteSpeed*seconds);const auto next=pulse.at+delta*(step/std::max(.001f,distance));
        pulse.travel+=step;if(pulse.travel>SkaldPolicy::kNoteReach+100.f||!ClearPath(actor.get(),pulse.at,next,target.get()))return drop();
        pulse.at=next;
        pulse.shed+=seconds;
        if(pulse.shed>=.045f){pulse.shed=0;pulse.trail[pulse.emitted++%pulse.trail.size()]=pulse.at;}
        if(distance-step<=20.f){
            const auto old=Abilities::BoostOn(target->GetFormID(),RE::ActorValue::kSpeedMult,noteSource);
            const float base=std::max(0.f,Game::Current(target.get(),RE::ActorValue::kSpeedMult)-(old?old->amount:0.f));
            Abilities::Lift(target->GetFormID(),RE::ActorValue::kSpeedMult,-base*SkaldPolicy::kNoteSlow,SkaldPolicy::kNoteSlowSeconds,noteSource);
            LightHit(target.get(),Song::Echoes,2,true); // a native burst and a flash on the target
            ApplyDiscord(actor.get(),target.get());
            RingNote(actor.get(),target.get());
            ApplyCrescendo(actor.get(),target.get());
            return drop();
        }
        auto* root=Posing::RootOf(pulse.art.effect.get());if(!root)return false;
        if(!pulse.primed){
            for(std::size_t i=0;i<SkaldPolicy::kHitMotes*SkaldPolicy::kHitBursts;++i)if(auto* mote=root->GetObjectByName(std::format("HitMote{}",i).c_str()))mote->SetAppCulled(true);
            pulse.primed=true;
        }
        for(std::size_t i=0;i<pulse.motes.size();++i){
            const auto count=std::min(pulse.emitted,static_cast<unsigned>(pulse.trail.size()));
            SkaldPolicy::ParticleFrame frame{};auto origin=pulse.at;
            if(i<count){
                const auto slot=(pulse.emitted-1-static_cast<unsigned>(i))%pulse.trail.size();origin=pulse.trail[slot];
                const float age=static_cast<float>(i)*.045f+pulse.shed;
                frame={std::sin(pulse.age*3.f+static_cast<float>(i)*2.4f)*16.f,std::cos(pulse.age*2.f+static_cast<float>(i))*16.f,
                    age*35.f,std::max(0.f,1.f-age/1.2f),(.65f+age*.3f)*1.6f};
            }
            if(i==0){origin=pulse.at;frame={0,0,0,1.f,3.2f};}
            PlaceMote(root,pulse.motes[i],std::format("HitMote{}",i).c_str(),origin,frame,MoteColor(Song::Echoes,i));
        }
        Posing::Shine(pulse.light,root,pulse.at,colors[Index(Song::Echoes)],.6f,170.f);
        Posing::Refresh(root); // include the particle head and floating trail in the bounds
        return false;
    });
}
void Tick(float seconds){
    if(!std::isfinite(seconds)||seconds<=0)return;
    std::erase_if(resumeCleanup,[](const auto& saved){
        auto* actor=RE::TESForm::LookupByID<RE::Actor>(saved.first);if(!actor||!actor->Is3DLoaded())return false;
        OwnEvent own;actor->NotifyAnimationGraph("IdleStop");
        if(saved.second>1.5f && Standing(actor) && actor->IsInCombat())actor->DrawWeaponMagicHands(true);
        return true;
    });
    if(!Settings::Get().enabled){Clear();return;}
    tavernScan-=seconds;
    const bool refreshTavern=tavernScan<=0;
    std::unordered_set<RE::FormID> readyToRelax;
    std::unordered_set<RE::FormID> currentSkalds;
    for(const auto& member:Party::Members()){
        const auto* orders=Party::FindOrders(member.id);if(!orders||orders->role!=Role::kSkald)continue;
        auto actor=member.handle.get();if(!actor||actor->IsPlayerRef())continue;
        currentSkalds.insert(member.id);
        if(refreshTavern&&Standing(actor.get())&&!actor->IsInCombat()&&HasLute(actor.get())){
            const auto kit=PathsLink::KitOf(member.id,Role::kSkald);
            if(kit.record&&kit.record->skaldUnlocked)readyToRelax.insert(member.id);
        }
    }
    // Older builds forced Cautious confidence, which invokes unbounded engine flight.
    // Support spacing is owned by the combat style and the player's leash instead.
    Abilities::DropSource(nerveSource);
    std::vector<std::pair<RE::FormID,Benefits>> current;
    std::vector<Reward> earned;
    {
        std::scoped_lock lock(gate);
        now+=seconds;
        timid=std::move(currentSkalds);
        if(refreshTavern){tavernReady=std::move(readyToRelax);tavernScan=.5f;}
        std::erase_if(benefits,[](const auto& pair){return std::ranges::none_of(pair.second.songs,[](const Boon& b){return b.until>now;});});
        std::erase_if(discords,[](const auto& pair){
            return std::ranges::none_of(pair.second.until,[](float until){return until>now;})&&now-pair.second.jarredAt>=SkaldPolicy::kJarredCooldown;});
        current.assign(benefits.begin(),benefits.end());earned.swap(rewards);
    }
    for(const auto& reward:earned)if(auto actor=reward.actor.get();Standing(actor.get())){
        const auto av=reward.magic?RE::ActorValue::kMagicka:RE::ActorValue::kStamina;
        actor->AsActorValueOwner()->RestoreActorValue(av,Game::Maximum(actor.get(),av)*reward.fraction+reward.amount);
        if(auto target=reward.target.get();Standing(target.get()))Effects::Flash(target.get(),Effects::Look::kLevelUp,.2f);
    }
    for(const auto& [id,buffs]:current)if(auto* actor=RE::TESForm::LookupByID<RE::Actor>(id);Standing(actor)){
        const auto& echoes=buffs.songs[Index(Song::Echoes)];
        if(echoes.until>now)RecoverVoice(actor,seconds*echoes.strength);
        const auto& fervor=buffs.songs[Index(Song::Fervor)];
        if(fervor.until>now)for(const auto value:{RE::ActorValue::kWeaponSpeedMult,RE::ActorValue::kLeftWeaponSpeedMultiply})
            Abilities::Keep(id,value,Raise(actor,value,fervor.strength),std::min(.3f,fervor.until-now),speedSource);
        const auto& hollow=buffs.songs[Index(Song::Hollow)];
        if(hollow.until>now&&hollow.branchA>0)Abilities::Keep(id,RE::ActorValue::kSpeedMult,-100.f*hollow.branchA,std::min(.3f,hollow.until-now),slowSource);
    }
    TickLeash(seconds);
    std::vector<Performance> handed;
    std::erase_if(performances,[seconds,&handed](Performance& p){
        auto actor=p.actor.get();p.age+=seconds;p.check-=seconds;
        if(p.finished){
            if(!Standing(actor.get())||actor->GetParentCell()!=p.cell){End(p,false,"wave cleanup");return true;}
            p.playing+=seconds;AdvanceWaves(p,actor.get());
            if(p.waves.empty()){End(p,false,"waves finished");return true;}
            return false;
        }
        const auto abort=[&](std::string_view reason){End(p,false,reason);return true;};
        if(p.age>45.f&&!p.note)return abort("performance watchdog"); // startup has its own bounded idle timeout
        const auto* orders=Party::FindOrders(p.id);
        bool struck=false;{std::scoped_lock lock(gate);struck=interrupted.erase(p.id)>0;}
        if(struck)return abort("hit received");
        if(!Standing(actor.get()))return abort("actor unavailable or incapacitated");
        if(actor->GetParentCell()!=p.cell)return abort("cell changed");
        if(actor->IsInRagdollState())return abort("ragdoll");
        if(actor->IsInKillMove())return abort("kill move");
        if(actor->AsActorState()->IsStaggered())return abort("stagger");
        if(actor->AsActorState()->GetKnockState()!=RE::KNOCK_STATE_ENUM::kNormal)return abort("knocked down");
        // No spectral lute while Skyrim's swings into place, or anywhere a held lute could not be.
        const auto lift=[&](){
            p.lifting=(p.fresh&&p.age-p.startedAt<SkaldPolicy::kLiftIn&&!recordings.contains(p.id))||OffPose(p.id,p.prop.get())||
                !Held(actor.get(),WorldOf(p.prop.get()).translate,"lute");
            if(p.lifting)p.liftedAt=p.age;
        };
        // Once the idle has settled, record the playing arms for the walk.
        const auto record=[&](){
            if(p.age-p.startedAt>=SkaldPolicy::kLayerSettle&&!p.resuming&&!p.lifting)Record(p.id,actor.get(),p.prop.get());
        };
        if(!orders||orders->role!=Role::kSkald)return abort("Skald role removed");
        if(!actor->IsInCombat()&&!p.debug)return abort("combat ended");
        if(p.check<=0){
            p.check=.3f;const auto kit=PathsLink::KitOf(p.id,Role::kSkald);
            if(!HasLute(actor.get()))return abort("inventory lute missing");
            if(!kit.record||!kit.record->skaldUnlocked)return abort("Skald tree locked");
            if(!p.note&&!p.debug&&!kit.Carries(p.ability))return abort("Ljodh unequipped");
        }
        if(p.note){
            // Keep the current target while it stays in reach; look again for a
            // better one a few times a second rather than raycasting every frame.
            // With nobody in reach the bard keeps strumming and looks again on the timer.
            auto current=p.target.get();p.aim-=seconds;
            const bool keep=current&&NoteTarget(actor.get(),current.get())&&Game::Distance(actor.get(),current.get())<=SkaldPolicy::kNoteReach;
            if(p.aim<=0||(current&&!keep)){p.aim=.3f;p.target=NoteTargetFor(actor.get());}
        }
        if(p.lute.effect)p.lute.effect->lifetime=p.lute.effect->age+12.f;
        // Behavior transitions may replace the prop; never keep using a detached old node.
        // Lutes left on the bard from before are never played, unless an idle moves one again.
        RE::NiAVObject* prop=nullptr;
        for(auto* candidate:LuteProps(actor->Get3D())){
            if(const auto old=std::ranges::find_if(p.stale,[&](const auto& entry){return entry.first.get()==candidate;});old!=p.stale.end()){
                if(!SkaldPolicy::StrayMoving(p.age,(candidate->local.translate-old->second).Length())){
                    if(p.age<SkaldPolicy::kStraySettle)old->second=candidate->local.translate;
                    continue;
                }
                p.stale.erase(old);
            }
            // A repeated idle request can attach a second lute; only the first is played.
            if(!prop)prop=candidate;
        }
        if(prop!=p.prop.get())p.prop.reset(prop);
        if(p.started&&p.prop){
            lift();
            Pose(p,actor.get());
            // Overlay timing, once a performance: a late or missing overlay is what goes unseen.
            if(!p.overlayReported){
                if(Posing::RootOf(p.lute.effect.get())){
                    p.overlayReported=true;
                    if(p.age-p.startedAt>1.f)Log::Info("[Skald] {}'s overlay attached {:.1f} s after the lute came up.",Game::Name(actor.get()),p.age-p.startedAt);
                }else if(p.age-p.startedAt>3.f){
                    p.overlayReported=true;
                    Log::Warn("[Skald] {}'s overlay has not attached 3 s after the lute came up (bursts {}, notes {}, walking lutes {}, lute effects {}).",
                        Game::Name(actor.get()),ljodhClouds.size(),notes.size(),walkingLutes.size(),luteEffects.size());
                }
            }
            // An overlay that never appeared is spawned once more.
            if(!p.respawned&&!Posing::RootOf(p.lute.effect.get())&&p.age-p.startedAt>SkaldPolicy::kOverlayRetry){
                p.respawned=true;p.lute.Drop();p.luteVeil={};
                p.lute.effect.reset(Track(Effects::SkaldModel(actor.get(),static_cast<int>(Index(p.song)),true,12.f)));
                Log::Info("[Skald] {}'s overlay had not appeared {:.1f} s after the lute came up; spawned again.",Game::Name(actor.get()),p.age-p.startedAt);
            }
        }
        if(p.started){
            const auto repair=p.recovery.Next(p.prop&&p.prop->parent,seconds);
            if(repair==SkaldPolicy::PropAction::Abort)return abort("lute prop recovery failed");
            if(repair==SkaldPolicy::PropAction::Retry){
                if(p.note)return abort("note prop detached");
                if(!p.music.Pause())return abort("could not pause for lute recovery");
                auto* idle=RE::TESForm::LookupByID<RE::TESIdleForm>(0x96F8D);OwnEvent own;
                const bool accepted=idle&&actor->NotifyAnimationGraph(idle->animEventName);
                Log::Info("[Skald] {} restores a detached lute: accepted={}; audio and waves paused.",Game::Name(actor.get()),accepted);
                return false;
            }
            if(repair==SkaldPolicy::PropAction::Wait)return false;
            if(repair==SkaldPolicy::PropAction::Resume){
                if(!p.music.Play())return abort("audio resume failed");
                p.resuming=true;p.soundWait=0;p.playheadWait=0;return false;
            }
        }
        if(!p.started){
            const auto weapon=actor->AsActorState()->GetWeaponState();
            const auto next=p.entry.Next(p.age,weapon==RE::WEAPON_STATE::kSheathed,
                weapon==RE::WEAPON_STATE::kSheathing,static_cast<bool>(p.prop));
            {
                OwnEvent own;
                if(next==SkaldPolicy::LuteEntryAction::Sheathe){
                    actor->InterruptCast(true);actor->DrawWeaponMagicHands(false);
                    actor->NotifyAnimationGraph("weaponSheathe");
                }else if(next==SkaldPolicy::LuteEntryAction::Reset){
                    // Clear an idle the AI still counts as current (see LuteEntry);
                    // after an IdleStop the next request brings the lute up at once.
                    actor->NotifyAnimationGraph("IdleStop");
                }else if(next==SkaldPolicy::LuteEntryAction::Idle || next==SkaldPolicy::LuteEntryAction::Graph){
                    auto* idle=RE::TESForm::LookupByID<RE::TESIdleForm>(0x96F8D);
                    const bool accepted=next==SkaldPolicy::LuteEntryAction::Idle ? Abilities::PlayIdle(actor.get(),idle) :
                        idle && actor->NotifyAnimationGraph(idle->animEventName);
                    p.idle=p.idle||accepted;
                    Log::Info("[Skald] {} lute entry: route={}, accepted={}, weapon-state={}, age={:.2f}",
                        Game::Name(actor.get()),next==SkaldPolicy::LuteEntryAction::Idle?"native idle":"graph fallback",accepted,static_cast<int>(weapon),p.age);
                }
            }
            if(p.prop){
                if(!BeginMusic(p,actor.get()))return abort("lute audio could not start");
                Freeze::Anchor(actor.get(),anchor);
                p.started=true;p.playing=0;p.startedAt=p.age;p.fresh=p.entry.requests>0;
                lift();
                // A delayed effect mesh must not cancel a successful native lute idle;
                // the hands play on, and the overlay is spawned again if it never comes.
                Pose(p,actor.get());
                if(!p.note){const auto& track=SkaldMusic::tracks[Index(p.song)];
                    Log::Info("[Skald] {} performs {}; {} BPM, one wave per two beats, {} s music, {:.1f} s linger.",Game::Name(actor.get()),Paths::Info(p.ability).name,track.bpm,track.seconds,p.duration);}
            }else if(next==SkaldPolicy::LuteEntryAction::Timeout)return abort("lute startup timeout");
            return false;
        }
        // A standing Single Note is only there to record the arms; once it has, the bard
        // plays on the move (TickStrums).
        if(p.note&&Recorded(p.id,actor.get()))return abort("recorded; plays on the move");
        // Stay in Skyrim's native lute idle between plucks. Only the audio handle restarts.
        if(p.note&&p.noteFired){
            if(now<p.pluckAt){p.playing+=seconds;record();return false;}
            p.music.FadeOutAndRelease(30);p.playing=0;p.lastBeat=-1;p.noteFired=false;p.audible=false;p.soundWait=0;p.playheadWait=0;
            if(!BeginMusic(p,actor.get()))return abort("note audio could not restart");
        }
        // The main-thread sound-info cache supplies the actual audio playhead, including pauses and pitch changes.
        const float previousPlayhead=p.playing;
        bool heard=false;
        if(auto* audio=RE::BSAudioManager::GetSingleton()){
            const auto found=audio->stateMap.find(p.music.soundID);
            if(found!=audio->stateMap.end()&&found->second){
                p.playing=static_cast<float>(found->second->playbackPositionMilliseconds)/1000.f;
                heard=found->second->isPlaying;p.audible=p.audible||heard;
            }
        }
        if(p.playing+.05f<previousPlayhead)return abort("audio playhead restarted");
        if(p.resuming){
            if(!heard){p.soundWait+=seconds;if(p.soundWait>2.f)return abort("audio never resumed");return false;}
            p.resuming=false;
        }
        if(!p.audible){p.soundWait+=seconds;if(p.soundWait>2.f)return abort("audio never became audible");return false;}
        if(actor->AsActorState()->IsWeaponDrawn())return abort("weapon redrawn");
        if(!p.note&&!p.announced){p.announced=true;Notices::Used(Trf("{} plays {}.",Game::Name(actor.get()),Tr(Paths::Info(p.ability).name)),p.ability);}
        if(p.note&&!p.noteFired&&p.playing>=.05f&&(!p.target||!LaunchNote(p,actor.get()))){
            // A strum with nobody clear to aim at is still music; only a target takes a note.
            p.noteFired=true;p.lastBeat=p.playing;
            p.pluckAt=now+SkaldPolicy::NoteInterval(std::uniform_real_distribution<float>(0.f,1.f)(rng));
        }
        p.playheadWait=p.playing>previousPlayhead?0.f:p.playheadWait+seconds;
        if(p.playheadWait>2.f)return abort("audio playhead stalled");
        if(p.playing>.2f&&!p.music.IsPlaying())return abort("audio stopped");
        if(p.note){record();return false;}
        const auto& track=SkaldMusic::tracks[Index(p.song)];
        if(!p.prop->parent)return abort("lute prop detached");
        AdvanceWaves(p,actor.get());
        const auto previousBeat=p.beat;
        while(p.beat<track.beats.size()&&p.playing>=track.beats[p.beat]){
            p.lastBeat=track.beats[p.beat++];
        }
        // The audio cache can advance in chunks larger than 180 ms. Coalesce
        // overdue artwork to one current beat instead of dropping every burst.
        if(p.beat!=previousBeat&&p.playing-p.lastBeat<60.f/track.bpm)
            LjodhBurst(actor.get(),p.song,Effects::SkaldCloud::Aura,p.rank,actor->GetPosition());
        while(p.waveBeat<track.waves.size()&&p.playing>=track.waves[p.waveBeat]){
            const float born=track.waves[p.waveBeat++];
            std::size_t slot=p.playing-born>=SkaldPolicy::kWaveSeconds?SkaldPolicy::kWaveSlots:0;
            while(slot<SkaldPolicy::kWaveSlots&&std::ranges::any_of(p.waves,[slot](const Wave& w){return w.slot==slot;}))++slot;
            // Slot count only limits the artwork; a frame hitch must never discard a gameplay wave.
            p.waves.push_back({born,0.f,slot,actor->GetPosition(),{}});
            if((p.waveBeat==track.waves.size()||p.playing<track.waves[p.waveBeat])&&p.playing-born<SkaldPolicy::kWaveSeconds)
                LjodhBurst(actor.get(),p.song,Effects::SkaldCloud::Wave,p.rank,actor->GetPosition());
        }
        AdvanceWaves(p,actor.get());
        record();
        if(p.playing>=track.seconds){
            // The lute stays up: a finished Ljodh hands its pose straight to the Single Note,
            // a standing one while the arms are not yet recorded, else the one on the move.
            if(p.prop&&!Recorded(p.id,actor.get())&&!Relocating(p.id)&&!WithinFists(actor.get())&&!Unsettled(actor.get())){
                Performance next;next.actor=p.actor;next.id=p.id;next.cell=p.cell;next.worldspace=p.worldspace;
                next.note=true;next.song=Song::Echoes;next.rank=1;
                next.prop=std::move(p.prop);next.stale=std::move(p.stale);next.idle=p.idle;p.idle=false;p.handoff=true;
                next.lute=std::move(p.lute);next.luteVeil=std::move(p.luteVeil);p.lute={};p.luteVeil={};
                handed.push_back(std::move(next));
            }
            End(p,true,"final chord");return false;
        }
        return false;
    });
    for(auto& next:handed){{std::scoped_lock lock(gate);performing.insert(next.id);}performances.push_back(std::move(next));}
    TickLjodhClouds(seconds);
    TickNotes(seconds);
    StartNotes(seconds);
    TickLayers();
    TickStrums();
    TickStance();
    TickStrays();
    SweepLutes();
    TickHitLights();
    PlaceMarks();
    DuckMusic(seconds);
}
void DropLayers(){
    for(auto& [id,walk]:walkingLutes)walk.Drop();walkingLutes.clear();
    for(auto& [id,strum]:strums)strum.sound.FadeOutAndRelease(150);strums.clear();
    std::unique_lock lock(layerLock);layers.clear();layerCount.store(0,std::memory_order_release);
}
void EndCombat(){
    std::erase_if(performances,[](Performance& p){if(p.finished||p.debug)return false;End(p,false,"combat ended");return true;});
    for(auto& pulse:notes)pulse.Drop();notes.clear();noteReady.clear();
    rejoins.clear();
    DropLayers();
    // Earned buffs and recipient lights finish their remaining duration.
}
void BeforeSave(){
    // The next tick lends it again if the fight goes on.
    std::vector<RE::FormID> held(lent.begin(),lent.end());
    for(const auto id:held)TakeBack(id);
    {std::scoped_lock lock(gate);stanceHolders.clear();}
}
void Clear(){
    for(auto& p:performances)End(p,false,"runtime cleanup");
    performances.clear();resumeCleanup.clear();
    BeforeSave();
    {std::vector<RE::FormID> shown(bareHands.begin(),bareHands.end());for(const auto id:shown)TakeBack(id);}
    stanceChecked.clear();
    for(auto& cloud:ljodhClouds)cloud.Drop();ljodhClouds.clear();
    for(auto& pulse:notes)pulse.Drop();notes.clear();noteReady.clear();noteScan=0;
    rejoins.clear();leashScan=0;
    DropLayers();recordings.clear();playedUntil.clear();luteSeen.clear();
    for(auto& [id,spare]:spares){spare.veil.Drop();spare.lute.Drop();}spares.clear();
    for(auto& effect:luteEffects)effect->lifetime=0.f;luteEffects.clear();placedLutes.clear();strays.clear();
    DuckMusic(0.f,true);
    for(auto& [id,hit]:hitLights)hit.Drop();hitLights.clear();
    marks.clear();phrase.clear();{std::scoped_lock lock(markLock);placedMarks.clear();}
    Abilities::DropSource(speedSource);Abilities::DropSource(slowSource);Abilities::DropSource(nerveSource);Abilities::DropSource(noteSource);Abilities::DropSource(offBeatSource);Abilities::DropSource(crescendoSource);
    std::scoped_lock lock(gate);benefits.clear();shields.clear();performing.clear();strumming.clear();interrupted.clear();timid.clear();tavernReady.clear();tavernScan=0;rewards.clear();discords.clear();now=0;
}
std::vector<NoteMark> Marks(){std::scoped_lock lock(markLock);return placedMarks;}
// After Skyrim writes this frame's pose, on whatever thread animates the actor:
// lay the recorded playing arms over a walking bard.
void AfterAnimation(RE::Actor* actor){
    if(!actor||layerCount.load(std::memory_order_acquire)==0)return;
    std::shared_ptr<const Clip> clip;float started{},stopped{};
    {
        std::shared_lock lock(layerLock);
        const auto it=layers.find(actor->GetFormID());if(it==layers.end())return;
        clip=it->second.clip;started=it->second.started;stopped=it->second.stopped;
    }
    if(!clip||clip->times.size()<2||actor->Get3D()!=clip->skeleton)return;
    const float clock=layerClock.load(std::memory_order_relaxed);
    const float weight=SkaldPolicy::LayerWeight(clock-started,stopped<0.f?-1.f:clock-stopped);
    if(weight<=.001f)return;
    const auto at=SkaldPolicy::LoopAt(clock-started,clip->length);
    for(std::size_t i=0;i<clip->nodes.size();++i){
        const auto target=Looped(*clip,at,[&](std::size_t frame){return clip->frames[frame][i];});
        auto& local=clip->nodes[i]->local;
        local=weight>=.999f?target:Blend(local,target,weight);
    }
    RE::NiUpdateData update{0.f,RE::NiUpdateData::Flag::kNone};
    for(const auto& root:clip->roots)root->UpdateWorldData(&update);
}
float Recovery(RE::FormID id){std::scoped_lock lock(gate);return Active(id,Song::Echoes).strength;}
float CastingSpeed(RE::FormID id){std::scoped_lock lock(gate);return std::clamp(Active(id,Song::Fervor).branchA,0.f,.8f);}
float PowerCost(RE::FormID id){std::scoped_lock lock(gate);return 1.f-std::clamp(Active(id,Song::Fervor).branchB,0.f,1.f);}
float ResourceFactor(RE::FormID id,Paths::Ability ability){
    if(IsSong(ability))return 1;
    std::scoped_lock lock(gate);return 1.f-std::clamp(Active(id,Song::Echoes).branchB,0.f,1.f);
}
void ConsumeInspiration(RE::FormID id,Paths::Ability ability){
    if(IsSong(ability))return;
    std::scoped_lock lock(gate);
    if(const auto it=benefits.find(id);it!=benefits.end()){auto& b=it->second.songs[Index(Song::Echoes)];b.branchB=0;b.inspirationSpent=true;}
}
float Outgoing(RE::Actor* attacker,RE::Actor* target,float damage,bool direct,bool magic){
    if(!attacker||!target||attacker==target||!std::isfinite(damage)||damage<=0)return damage;
    std::scoped_lock lock(gate);
    damage*=1.f-std::clamp(Active(attacker->GetFormID(),Song::Hollow).strength,0.f,.8f);
    damage*=1.f-SkaldPolicy::DiscordWeaken(Stacks(attacker->GetFormID(),SkaldPolicy::Discord::Weakened));
    damage*=1.f+SkaldPolicy::DiscordExposed(Stacks(target->GetFormID(),SkaldPolicy::Discord::Exposed));
    const auto glory=Active(attacker->GetFormID(),Song::Glory);
    if(direct&&glory.strength>0&&std::uniform_real_distribution<float>(0.f,1.f)(rng)<glory.branchA){
        damage*=SkaldPolicy::CriticalMultiplier(glory.strength);
        auto& buffs=benefits[attacker->GetFormID()];
        if(glory.branchB>0&&buffs.restoreAt<=now){buffs.restoreAt=now+1.f;rewards.push_back({attacker->GetHandle(),target->GetHandle(),glory.branchB,magic});}
    }
    return timid.contains(attacker->GetFormID())?SkaldPolicy::TimidDamage(damage):damage;
}
float Absorb(RE::Actor* target,float damage){
    if(!target||damage<=0)return damage;
    std::scoped_lock lock(gate);
    const auto it=shields.find(target->GetFormID());
    return it==shields.end()?damage:SkaldPolicy::Absorb(damage,it->second);
}
std::vector<std::pair<std::uint32_t,float>> SavedPerformers(){
    std::vector<std::pair<std::uint32_t,float>> saved;
    for(const auto& p:performances)if(p.idle&&!p.finished)saved.emplace_back(p.id,p.drawn?2.f:1.f);
    return saved;
}
void LoadPerformers(const std::vector<std::pair<std::uint32_t,float>>& saved){resumeCleanup=saved;}
void Statuses(RE::FormID id,std::vector<View::Status>& out){
    using Tint=View::Status::Tint;
    std::scoped_lock lock(gate);
    if(const auto rejoin=rejoins.find(id);rejoin!=rejoins.end()&&rejoin->second.active)
        out.push_back({std::string(Tr("Walking back to you")),std::string(Tr("Out of reach of the party; plays again once back.")),0xF001u,Tint::kBuff});
    if(performing.contains(id)){
        const auto song=std::ranges::find_if(performances,[id](const Performance& p){return p.id==id&&!p.finished;});
        if(song!=performances.end())out.push_back({std::string(song->note?Tr("Single Note"):Tr(Paths::Info(song->ability).name)),
            song->note?std::string(Tr("A targeted lute pulse slows movement by 30% for 1 second and adds a discord.")):std::string(Tr("Playing a spectral lute; combat resumes after the final chord.")),
            song->note?0xF001u:Paths::Info(song->ability).glyph,Tint::kBuff});
    }else if(strumming.contains(id))
        out.push_back({std::string(Tr("Single Note")),std::string(Tr("A targeted lute pulse slows movement by 30% for 1 second and adds a discord.")),0xF001u,Tint::kBuff});
    for(const auto& song:performances)if(!song.note&&!song.finished){
        const auto listener=song.listeners.find(id);
        if(listener!=song.listeners.end()&&listener->second.stacks.hits>0&&listener->second.stacks.count==0)
            out.push_back({std::string(Tr(Paths::Info(song.ability).name)),Trf("{} of 3 waves toward the first stack.",listener->second.stacks.hits),Paths::Info(song.ability).glyph,Tint::kBuff});
    }
    if(const auto shield=shields.find(id);shield!=shields.end()&&shield->second>0)out.push_back({std::string(Tr("Spectral Aegis")),Trf("{:.0f} damage absorption remaining.",shield->second),0xF3ED,Tint::kBuff});
    for(std::size_t song=0;song<4;++song){const auto buff=Active(id,static_cast<Song>(song));if(buff.until<=now)continue;
        const auto ability=static_cast<Paths::Ability>(static_cast<int>(Paths::Ability::kFervor)+song);
        std::string detail;
        switch(static_cast<Song>(song)){
        case Song::Fervor:detail=Trf("+{:.0f}% attack speed; {:.0f}% shorter spell wind-up.",buff.strength*100,buff.branchA*100);break;
        case Song::Echoes:detail=Trf("{:.0f}% faster non-Ljodh ability cooldown recovery.",buff.strength*100);break;
        case Song::Hollow:detail=Trf("{:.0f}% less outgoing damage.",buff.strength*100);break;
        case Song::Glory:detail=Trf("{:.0f}% critical chance; {:.2f}x critical damage.",buff.branchA*100,SkaldPolicy::CriticalMultiplier(buff.strength));break;
        }
        detail+=Trf(" {} / 3 stacks; {} empowered.",buff.stacks,std::popcount(buff.empowered));
        out.push_back({std::string(Tr(Paths::Info(ability).name)),detail,Paths::Info(ability).glyph,song==2?Tint::kDebuff:Tint::kBuff,buff.until-now,buff.length});
    }
}
void Install(){
    static bool installed=false;if(installed||!Host::Abilities())return;installed=true;
    REL::Relocation<std::uintptr_t> graph{RE::VTABLE_Character[3]};
    Animation::original=graph.write_vfunc(0x01,Animation::Thunk);
    REL::Relocation<std::uintptr_t> caster{RE::VTABLE_ActorMagicCaster[0]};
    CastTimer::original=caster.write_vfunc(0x14,CastTimer::Thunk);
    InspiredSpell::original=caster.write_vfunc(0x09,InspiredSpell::Thunk);
    REL::Relocation<std::uintptr_t> offensive{RE::VTABLE_CombatMagicCasterOffensive[0]};
    StanceCast::original=offensive.write_vfunc(0x06,StanceCast::Thunk);
    const auto version=REL::Module::get().version();
    if(!REL::Module::IsVR() && version<REL::Version(1,7,0,0)){
        const auto site=REL::Relocation<std::uintptr_t>{RELOCATION_ID(37650,38603)}.address()+REL::Relocate<std::uintptr_t>(0x16E,0x171);
        if(*reinterpret_cast<const std::uint8_t*>(site)==0xE8){SKSE::AllocTrampoline(14);AttackCost::original=SKSE::GetTrampoline().write_call<5>(site,AttackCost::Thunk);}
    }
    Log::Info("[Skald] Lute channel and spell wind-up hooks installed; power-attack cost hook {}.",AttackCost::original.address()?"installed":"unavailable on this runtime");
}
}

