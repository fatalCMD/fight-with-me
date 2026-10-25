#include "FWM/Surges.h"
#include "FWM/ArcaneSurge.h"
#include "FWM/Effects.h"
#include "FWM/Posing.h"
#include "FWM/Settings.h"
#include "FWM/Log.h"

namespace FWM::Surges {
namespace {
struct Part { RE::NiPointer<RE::NiAVObject> node; std::vector<RE::BSShaderProperty*> shaders; };
struct Surge {
    RE::ActorHandle actor;
    RE::NiPointer<RE::BSTempEffectParticle> effect;
    std::array<Part,ArcaneSurge::kParts> parts;
    Posing::Lamp lamp;
    float age{},duration{},retry{},loading{},bodyScale{1};
    std::size_t copy{ArcaneSurge::kCopies};
    int rank{1}; bool found{};
};
std::vector<Surge> surges;
void Drop(Surge& r){
    Posing::Out(r.lamp); if(r.effect)r.effect->lifetime=0;
    r.effect.reset();for(auto& p:r.parts){p.node.reset();p.shaders.clear();}
    r.copy=ArcaneSurge::kCopies;r.found=false;r.loading=0;
}
bool Find(Surge& r){
    auto* root=Posing::RootOf(r.effect.get());if(!root)return false;
    for(std::size_t i=0;i<r.parts.size();++i){r.parts[i].node.reset(root->GetObjectByName(ArcaneSurge::Name(i)));if(!r.parts[i].node)return false;}
    for(auto& p:r.parts)p.shaders=Posing::ShadersOf(p.node.get());
    r.found=true;return true;
}
void Spawn(Surge& r,RE::Actor* actor){
    if(r.effect||r.retry>0)return;
    for(std::size_t copy=0;copy<ArcaneSurge::kCopies;++copy){
        if(std::ranges::any_of(surges,[copy](const Surge& other){return other.effect&&other.copy==copy;}))continue;
        r.effect.reset(Effects::ArcaneSurgeCircle(actor,copy,actor->GetPosition(),r.duration-r.age+ArcaneSurge::kFade+1.5f));
        if(!r.effect)continue;
        r.copy=copy;
        const float height=actor->GetHeight();r.bodyScale=std::isfinite(height)?std::clamp(height/128.f,.7f,1.45f):1;
        return;
    }
    r.retry=1;
}
void Pose(Surge& r,RE::Actor* actor){
    auto* root=Posing::RootOf(r.effect.get());if(!root||!r.found)return;
    const auto f=ArcaneSurge::At(r.age,r.duration,r.rank);
    for(std::size_t i=0;i<r.parts.size();++i){
        auto& part=r.parts[i];const auto& p=f.parts[i];
        RE::NiMatrix3 yaw,tilt;yaw.MakeZRotation(-p.turn);tilt.MakeXRotation(-p.tilt);
        const RE::NiPoint3 offset{p.at[0]*r.bodyScale,p.at[1]*r.bodyScale,p.at[2]*r.bodyScale};
        const auto at=actor->GetPosition()+offset;
        Posing::PlaceInWorld(root,part.node.get(),at,yaw*tilt,p.scale*r.bodyScale);
        Posing::Paint(part.shaders,p.color,p.alpha);
        if(i==2||i==4)Posing::Drift(part.shaders,p.drift);
        part.node->SetAppCulled(p.alpha<=0);
    }
    Posing::Shine(r.lamp,root,actor->GetPosition()+RE::NiPoint3{0,0,18*r.bodyScale},
        std::array<float,3>{.56f,.4f,1},f.light,180*r.bodyScale);
    Posing::Refresh(root);
}
}
void Call(RE::Actor* actor,float seconds,int rank){
    if(!actor||actor->IsDead()||!std::isfinite(seconds)||seconds<=0)return;
    for(auto& r:surges)if(r.actor==actor->GetHandle()){
        Drop(r);r.age=0;r.duration=seconds;r.rank=std::clamp(rank,1,3);return;
    }
    Surge r;r.actor=actor->GetHandle();r.duration=seconds;r.rank=std::clamp(rank,1,3);
    surges.push_back(std::move(r));
    Log::Info("[Look] ArcaneSurge rank {} following seal started for {:08X} ({:.1f}s)."sv,rank,actor->GetFormID(),seconds);
}
void End(){for(auto& r:surges)r.duration=std::min(r.duration,r.age);}
void Tick(float dt){
    dt=std::isfinite(dt)?std::max(dt,0.f):0;
    std::erase_if(surges,[dt](Surge& r){
        r.age+=dt;r.retry=std::max(0.f,r.retry-dt);auto actor=r.actor.get();
        if(!Settings::Get().enabled||!actor||actor->IsDead()||actor->IsDisabled()||r.age>=r.duration+ArcaneSurge::kFade){Drop(r);return true;}
        return false;
    });
    for(auto& r:surges){
        auto actor=r.actor.get();if(!actor)continue;
        if(!Settings::Get().abilityEffects||!actor->Is3DLoaded()){Drop(r);continue;}
        if(r.effect&&(!r.effect->cell||!r.effect->cell->IsAttached()||r.effect->cell!=actor->GetParentCell()||r.effect->age>=r.effect->lifetime))Drop(r);
        Spawn(r,actor.get());if(!r.effect)continue;
        if(!r.found&&!Find(r)){r.loading+=dt;if(r.loading>1){Drop(r);r.retry=1;}continue;}
        Pose(r,actor.get());
    }
}
void Clear(){for(auto& r:surges)Drop(r);surges.clear();}
}
