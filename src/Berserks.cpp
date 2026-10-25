#include "FWM/Berserks.h"
#include "FWM/Berserk.h"
#include "FWM/Effects.h"
#include "FWM/Posing.h"
#include "FWM/Settings.h"
#include "FWM/Log.h"

namespace FWM::Berserks {
namespace {
struct Part { RE::NiPointer<RE::NiAVObject> node; std::vector<RE::BSShaderProperty*> shaders; };
struct Rage {
    RE::ActorHandle actor;
    RE::NiPointer<RE::BSTempEffectParticle> effect;
    std::array<Part,Berserk::kParts> parts;
    Posing::Lamp lamp;
    RE::NiPoint3 origin;
    float age{},duration{},retry{},loading{},bodyScale{1};
    std::size_t copy{Berserk::kCopies};
    int rank{1}; bool found{};
};
std::vector<Rage> rages;
void Drop(Rage& r){
    Posing::Out(r.lamp); if(r.effect)r.effect->lifetime=0;
    r.effect.reset();for(auto& p:r.parts){p.node.reset();p.shaders.clear();}
    r.copy=Berserk::kCopies;r.found=false;r.loading=0;
}
bool Find(Rage& r){
    auto* root=Posing::RootOf(r.effect.get());if(!root)return false;
    for(std::size_t i=0;i<r.parts.size();++i){r.parts[i].node.reset(root->GetObjectByName(Berserk::Name(i)));if(!r.parts[i].node)return false;}
    for(auto& p:r.parts)p.shaders=Posing::ShadersOf(p.node.get());
    r.found=true;return true;
}
void Spawn(Rage& r,RE::Actor* actor){
    if(r.effect||r.retry>0)return;
    for(std::size_t copy=0;copy<Berserk::kCopies;++copy){
        if(std::ranges::any_of(rages,[copy](const Rage& other){return other.effect&&other.copy==copy;}))continue;
        r.effect.reset(Effects::BerserkMantle(actor,copy,actor->GetPosition(),r.duration-r.age+Berserk::kFade+1.5f));
        if(!r.effect)continue;
        r.copy=copy;
        const float height=actor->GetHeight();r.bodyScale=std::isfinite(height)?std::clamp(height/128.f,.7f,1.45f):1;
        return;
    }
    r.retry=1;
}
void Pose(Rage& r,RE::Actor* actor){
    auto* root=Posing::RootOf(r.effect.get());if(!root||!r.found)return;
    const auto f=Berserk::At(r.age,r.duration,r.rank);
    RE::NiMatrix3 facing;facing.MakeZRotation(actor->GetAngleZ());
    for(std::size_t i=0;i<r.parts.size();++i){
        auto& part=r.parts[i];const auto& p=f.parts[i];
        RE::NiMatrix3 yaw,tilt;yaw.MakeZRotation(-p.turn);tilt.MakeXRotation(-p.tilt);
        const RE::NiPoint3 offset{p.at[0]*r.bodyScale,p.at[1]*r.bodyScale,p.at[2]*r.bodyScale};
        const auto at=i<2?r.origin+offset:actor->GetPosition()+facing*offset;
        Posing::PlaceInWorld(root,part.node.get(),at,i<2?yaw:facing*yaw*tilt,p.scale*r.bodyScale);
        Posing::Paint(part.shaders,p.color,p.alpha);Posing::Drift(part.shaders,p.drift);
        part.node->SetAppCulled(p.alpha<=0);
    }
    Posing::Shine(r.lamp,root,actor->GetPosition()+RE::NiPoint3{0,0,70*r.bodyScale},
        r.rank>=3?std::array<float,3>{1,.075f,.02f}:std::array<float,3>{1,.32f,.04f},f.light,180*r.bodyScale);
    Posing::Refresh(root);
}
}
void Call(RE::Actor* actor,float seconds,int rank){
    if(!actor||actor->IsDead()||!std::isfinite(seconds)||seconds<=0)return;
    for(auto& r:rages)if(r.actor==actor->GetHandle()){
        Drop(r);r.age=0;r.duration=seconds;r.rank=std::clamp(rank,1,3);r.origin=actor->GetPosition();return;
    }
    Rage r;r.actor=actor->GetHandle();r.duration=seconds;r.rank=std::clamp(rank,1,3);r.origin=actor->GetPosition();
    rages.push_back(std::move(r));
    Log::Info("[Look] Berserk rank {} rage mantle started for {:08X} ({:.1f}s)."sv,rank,actor->GetFormID(),seconds);
}
void Extend(RE::Actor* actor,float seconds){
    if(!actor||!std::isfinite(seconds)||seconds<=0)return;
    for(auto& r:rages)if(r.actor==actor->GetHandle()){
        r.duration=std::max(r.duration,r.age+seconds);
        if(r.effect)r.effect->lifetime=r.effect->age+(r.duration-r.age)+Berserk::kFade+1.5f;
    }
}
void End(){for(auto& r:rages)r.duration=std::min(r.duration,r.age);}
void Tick(float dt){
    dt=std::isfinite(dt)?std::max(dt,0.f):0;
    std::erase_if(rages,[dt](Rage& r){
        r.age+=dt;r.retry=std::max(0.f,r.retry-dt);auto actor=r.actor.get();
        if(!Settings::Get().enabled||!actor||actor->IsDead()||actor->IsDisabled()||r.age>=r.duration+Berserk::kFade){Drop(r);return true;}
        return false;
    });
    for(auto& r:rages){
        auto actor=r.actor.get();if(!actor)continue;
        if(!Settings::Get().abilityEffects||!actor->Is3DLoaded()){Drop(r);continue;}
        if(r.effect&&(!r.effect->cell||!r.effect->cell->IsAttached()||r.effect->cell!=actor->GetParentCell()||r.effect->age>=r.effect->lifetime))Drop(r);
        Spawn(r,actor.get());if(!r.effect)continue;
        if(!r.found&&!Find(r)){r.loading+=dt;if(r.loading>1){Drop(r);r.retry=1;}continue;}
        Pose(r,actor.get());
    }
}
void Clear(){for(auto& r:rages)Drop(r);rages.clear();}
}
