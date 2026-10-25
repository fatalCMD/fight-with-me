#include "FWM/SecondWinds.h"
#include "FWM/SecondWindFx.h"
#include "FWM/Effects.h"
#include "FWM/Posing.h"
#include "FWM/Settings.h"
#include "FWM/Log.h"
namespace FWM::SecondWinds {
namespace {
struct Part { RE::NiPointer<RE::NiAVObject> node; std::vector<RE::BSShaderProperty*> shaders; };
struct Wind {
    RE::ActorHandle actor; RE::NiPointer<RE::BSTempEffectParticle> effect;
    std::array<Part,SecondWindFx::kParts> parts;
    RE::NiPoint3 origin; Posing::Lamp lamp;
    float age{},loading{},scale{1}; int rank{1}; std::size_t copy{}; bool found{};
};
std::vector<Wind> winds;
void Finish(Wind& wind){Posing::Out(wind.lamp);if(wind.effect)wind.effect->lifetime=0;}
bool Find(Wind& wind){
    auto* root=Posing::RootOf(wind.effect.get());if(!root)return false;
    for(std::size_t i=0;i<wind.parts.size();++i){wind.parts[i].node.reset(root->GetObjectByName(SecondWindFx::Name(i)));if(!wind.parts[i].node)return false;}
    for(auto& p:wind.parts)p.shaders=Posing::ShadersOf(p.node.get());
    wind.found=true;return true;
}
void Pose(Wind& wind,RE::Actor* actor){
    auto* root=Posing::RootOf(wind.effect.get());if(!root||!wind.found)return;
    const auto f=SecondWindFx::At(wind.age,wind.rank);
    RE::NiMatrix3 facing;facing.MakeZRotation(actor->GetAngleZ());
    for(std::size_t i=0;i<wind.parts.size();++i){
        auto& part=wind.parts[i];const auto& p=f.parts[i];
        RE::NiMatrix3 yaw,tilt;yaw.MakeZRotation(-p.turn);tilt.MakeXRotation(-p.tilt);
        const RE::NiPoint3 offset{p.at[0]*wind.scale,p.at[1]*wind.scale,p.at[2]*wind.scale};
        const auto at=i<2?wind.origin+offset:actor->GetPosition()+facing*offset;
        Posing::PlaceInWorld(root,part.node.get(),at,i<2?yaw:facing*yaw*tilt,p.scale*wind.scale);
        Posing::Paint(part.shaders,p.color,p.alpha);Posing::Drift(part.shaders,p.drift);
        part.node->SetAppCulled(p.alpha<=0);
    }
    Posing::Shine(wind.lamp,root,actor->GetPosition()+RE::NiPoint3{0,0,85*wind.scale},{.6f,1,.72f},f.light,180*wind.scale);
    Posing::Refresh(root);
}
}
void Call(RE::Actor* actor,int rank){
    if(!actor||actor->IsDead()||!actor->Is3DLoaded()||!Settings::Get().enabled||!Settings::Get().abilityEffects)return;
    std::erase_if(winds,[actor](Wind& w){if(w.actor==actor->GetHandle()){Finish(w);return true;}return false;});
    Wind wind;wind.actor=actor->GetHandle();wind.origin=actor->GetPosition();wind.rank=std::clamp(rank,1,3);
    const float height=actor->GetHeight();wind.scale=std::isfinite(height)?std::clamp(height/128.0f,.7f,1.45f):1;
    for(std::size_t copy=0;copy<SecondWindFx::kCopies;++copy){
        if(std::ranges::any_of(winds,[copy](const Wind& w){return w.copy==copy;}))continue;
        wind.effect.reset(Effects::SecondWindBurst(actor,copy,wind.origin,SecondWindFx::kSeconds+1.5f));if(!wind.effect)continue;
        wind.copy=copy;if(Find(wind))Pose(wind,actor);
        winds.push_back(std::move(wind));return;
    }
    Effects::Flash(actor,Effects::Look::kSecondWind);
}
void Tick(float dt){
    dt=std::isfinite(dt)?std::max(dt,0.0f):0;
    std::erase_if(winds,[dt](Wind& wind){
        auto actor=wind.actor.get();
        if(!Settings::Get().enabled||!Settings::Get().abilityEffects||!actor||actor->IsDead()||actor->IsDisabled()||!actor->Is3DLoaded()||
            !wind.effect||!wind.effect->cell||!wind.effect->cell->IsAttached()||actor->GetParentCell()!=wind.effect->cell||wind.effect->age>=wind.effect->lifetime){Finish(wind);return true;}
        if(!wind.found&&!Find(wind)){
            wind.loading+=dt;if(wind.loading>1){Effects::Flash(actor.get(),Effects::Look::kSecondWind);Log::Warn("[Look] Second Wind mesh did not load; using restorative aura."sv);Finish(wind);return true;}
            return false;
        }
        Pose(wind,actor.get());wind.age+=dt;
        if(wind.age>SecondWindFx::kSeconds){Finish(wind);return true;}return false;
    });
}
void Clear(){for(auto& wind:winds)Finish(wind);winds.clear();}
}
