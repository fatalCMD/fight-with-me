#include "FWM/PartyBalance.h"
#include "FWM/DragonBreaths.h"
#include "FWM/DragonBreath.h"
#include "FWM/Effects.h"
#include "FWM/EffectAudio.h"
#include "FWM/Enemies.h"
#include "FWM/Forms.h"
#include "FWM/Freeze.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/Party.h"
#include "FWM/Posing.h"
#include "FWM/Settings.h"

namespace FWM::DragonBreaths
{
    namespace
    {
        struct Part { RE::NiPointer<RE::NiAVObject> node; std::vector<RE::BSShaderProperty*> shaders; RE::NiPoint3 rest; };
        struct Live {
            RE::ActorHandle caster;
            RE::TESObjectCELL* cell{};
            RE::NiPoint3 origin, headAt, mouth, direction;
            RE::NiMatrix3 heading, aim;
            RE::NiPointer<RE::BSTempEffectParticle> headFX, streamFX;
            Part head, stream, rune;
            RE::NiPointer<RE::NiAVObject> jaw;
            std::array<Part,8> jets;
            std::array<Part,32> puffs;
            std::array<Part,64> arcs;
            std::array<Part,16> ice;
            std::array<Part,4> ground;
            std::array<int,4> groundEvent{-1,-1,-1,-1};
            std::array<bool,4> groundValid{};
            std::array<RE::NiPoint3,4> groundFrom,groundTo;
            int rank{},element{},copy{};
            float age{},loading{},duration{},reach{},dps{},dealt{},markClock{};
            bool found{},released{};
            EffectAudio::ID audio{};
            Posing::Lamp lamp;
        };
        std::vector<Live> live;
        constexpr std::array<std::array<float,3>,3> colors{{{1,.28f,.035f},{.32f,.72f,1},{.45f,.42f,1}}};
        constexpr std::array<RE::ActorValue,3> resists{RE::ActorValue::kResistFire,RE::ActorValue::kResistFrost,RE::ActorValue::kResistShock};
        RE::NiMatrix3 Facing(RE::NiPoint3 forward) {
            forward=forward/std::max(forward.Length(),.001f);
            auto right=forward.Cross({0,0,1}); right=right/std::max(right.Length(),.001f);
            const auto up=right.Cross(forward);
            RE::NiMatrix3 m;
            m.entry[0][0]=right.x; m.entry[1][0]=right.y; m.entry[2][0]=right.z;
            m.entry[0][1]=forward.x; m.entry[1][1]=forward.y; m.entry[2][1]=forward.z;
            m.entry[0][2]=up.x; m.entry[1][2]=up.y; m.entry[2][2]=up.z;
            return m;
        }
        float ClearReach(Live& v, RE::Actor* caster) {
            auto* world=v.cell->GetbhkWorld(); const float scale=RE::bhkWorld::GetWorldScale();
            if (!world || scale<=0) return v.reach;
            const auto to=v.mouth+v.direction*v.reach;
            RE::bhkPickData pick{}; RE::CFilter own{}; caster->GetCollisionFilterInfo(own);
            pick.rayInput.from={v.mouth.x*scale,v.mouth.y*scale,v.mouth.z*scale,0};
            pick.rayInput.to={to.x*scale,to.y*scale,to.z*scale,0};
            pick.rayInput.filterInfo.filter=static_cast<std::uint32_t>(RE::COL_LAYER::kLOS)|(own.GetSystemGroup()?own.GetSystemGroup()<<16:1u<<16);
            world->PickObject(pick);
            return !pick.pickFailed && pick.rayOutput.HasHit() ? v.reach*std::clamp(pick.rayOutput.hitFraction,0.f,1.f) : v.reach;
        }
        Part Find(RE::NiAVObject* root,const std::string& name) {
            Part p; if(root) p.node.reset(root->GetObjectByName(name));
            if(p.node) {p.shaders=Posing::ShadersOf(p.node.get());p.rest=p.node->local.translate;} return p;
        }
        bool Find(Live& v) {
            auto* root=Posing::RootOf(v.headFX.get()); auto* flow=Posing::RootOf(v.streamFX.get());
            if(!root||!flow) return false;
            v.head=Find(root,"Head"); v.jaw.reset(root->GetObjectByName("Jaw")); v.stream=Find(flow,"Stream");
            v.rune=Find(root,"SummoningRune");
            if(!v.head.node||!v.jaw||!v.stream.node) return false;
            for(int i=0;i<8;i++) v.jets[i]=Find(flow,std::format("Jet{}",i));
            for(int i=0;i<32;i++) v.puffs[i]=Find(flow,std::format("Puff{}",i));
            if(v.element==2) for(int i=0;i<64;i++) v.arcs[i]=Find(flow,std::format("Arc{}",i));
            if(v.rank==3&&v.element==1)for(int i=0;i<16;i++)v.ice[i]=Find(flow,std::format("Ice{}",i));
            if(v.rank==3&&v.element==2)for(int i=0;i<4;i++)v.ground[i]=Find(flow,std::format("Ground{}",i));
            v.found=true; return true;
        }
        void Finish(Live& v) {
            if(auto caster=v.caster.get()) Freeze::ReleaseAnchor(caster.get());
            if(v.headFX) v.headFX->lifetime=0; if(v.streamFX) v.streamFX->lifetime=0;
            Posing::Out(v.lamp); EffectAudio::End(v.audio);
        }
        void Show(Live& v,float clear) {
            if(!v.found) return;
            const auto p=DragonBreath::At(v.age,v.rank,v.duration);
            auto* root=Posing::RootOf(v.headFX.get()); auto* flow=Posing::RootOf(v.streamFX.get());
            if(!root||!flow) return;
            const auto headAim=Facing(v.direction*std::cos(p.pitch)+RE::NiPoint3{0,0,std::sin(p.pitch)});
            Posing::PlaceInWorld(root,v.head.node.get(),v.headAt,headAim,p.scale);
            v.jaw->local.rotate=Facing({0,std::cos(p.jaw),std::sin(p.jaw)});
            Posing::PaintBlended(v.head.shaders,v.rank>=3?std::array<float,3>{.9f,.13f,.1f}:std::array<float,3>{.65f,.85f,1},p.alpha);
            if(v.rune.node) {
                const float c=std::cos(p.runeAngle),s=std::sin(p.runeAngle);
                RE::NiMatrix3 spin;spin.entry[0][0]=c;spin.entry[0][1]=0;spin.entry[0][2]=s;
                spin.entry[1][0]=0;spin.entry[1][1]=1;spin.entry[1][2]=0;
                spin.entry[2][0]=-s;spin.entry[2][1]=0;spin.entry[2][2]=c;
                Posing::PlaceInWorld(root,v.rune.node.get(),v.origin+v.direction*15.f+RE::NiPoint3{0,0,p.runeRadius+12.f},v.heading*spin,p.runeRadius);
                Posing::PaintBlended(v.rune.shaders,{1,1,1},p.runeAlpha);
            }
            const float length=std::min(clear,v.reach*p.front);
            Posing::PlaceInWorld(flow,v.stream.node.get(),v.mouth,v.aim,std::max(length/1000,.001f));
            for(int i=0;i<8;i++) {
                const float flicker=v.element==2?.35f+.65f*std::abs(std::sin(v.age*39+i*1.7f)):.8f+.2f*std::sin(v.age*11+i);
                Posing::Paint(v.jets[i].shaders,DragonBreath::JetColor(v.rank,v.element,i),p.stream*flicker);
                Posing::Drift(v.jets[i].shaders,-v.age*(v.element==0?3.5f:1.5f)+i*.17f);
                if(v.jets[i].node) {
                    const float angle=.10f*std::sin(v.age*6.f-i*.7f),c=std::cos(angle),s=std::sin(angle);
                    auto& r=v.jets[i].node->local.rotate;
                    r.entry[0][0]=c;r.entry[0][1]=0;r.entry[0][2]=s;r.entry[1][0]=0;r.entry[1][1]=1;r.entry[1][2]=0;r.entry[2][0]=-s;r.entry[2][1]=0;r.entry[2][2]=c;
                }
            }
            for(int i=0;i<32;i++) {
                auto& part=v.puffs[i]; if(!part.node) continue;
                const float f=std::fmod(v.age*(v.element==2?2.1f:1.25f)+i/32.f,1.f), angle=i*2.399f+v.age;
                const float normalized=DragonBreath::Radius(v.rank,v.reach)*1000/v.reach;
                const float radius=normalized*DragonBreath::BeamProfile(f)*(v.rank==3&&v.element==0?.78f+.12f*(i%7)/6:.15f+.55f*(i%7)/6);
                part.node->local.translate={std::cos(angle)*radius,1000*f,std::sin(angle)*radius};
                part.node->local.scale=.7f+.5f*std::sin(f*3.14159f);
                Posing::Paint(part.shaders,DragonBreath::JetColor(v.rank,v.element,7),p.stream*std::sin(f*3.14159f)*(v.rank>=3?1.f:.65f));
                Posing::Drift(part.shaders,-v.age*.35f);
            }
            if(v.element==2) for(int i=0;i<64;i++) {
                auto& arc=v.arcs[i]; if(!arc.node) continue;
                const auto pose=DragonBreath::ArcAt(v.age,i);
                const float c=std::cos(pose.angle),s=std::sin(pose.angle);
                RE::NiMatrix3 spin; spin.entry[0][0]=c;spin.entry[0][1]=0;spin.entry[0][2]=s;
                spin.entry[1][0]=0;spin.entry[1][1]=1;spin.entry[1][2]=0;
                spin.entry[2][0]=-s;spin.entry[2][1]=0;spin.entry[2][2]=c;
                arc.node->local.rotate=spin;arc.node->local.translate=arc.rest+RE::NiPoint3{pose.x,0,pose.z};
                Posing::Paint(arc.shaders,colors[2],p.stream*pose.alpha);
            }
            if(v.rank==3&&v.element==1)for(int i=0;i<16;i++) {
                auto& shard=v.ice[i];if(!shard.node)continue;
                const float f=std::fmod(v.age*1.65f+i/16.f,1.f),a=i*2.399f+v.age*2;
                const float r=DragonBreath::Radius(v.rank,v.reach)*1000/v.reach*DragonBreath::BeamProfile(f)*.72f;
                shard.node->local.translate={std::cos(a)*r,1000*f,std::sin(a)*r};
                Posing::Paint(shard.shaders,{.65f,.9f,1},p.stream*std::sin(f*3.14159f));
            }
            if(v.rank==3&&v.element==2)for(int i=0;i<4;i++) {
                auto& bolt=v.ground[i];if(!bolt.node)continue;
                const auto pulse=DragonBreath::GroundAt(v.age,i,v.duration);
                if(pulse.alpha>0&&pulse.event!=v.groundEvent[i]) {
                    v.groundEvent[i]=pulse.event;v.groundValid[i]=false;
                    const float along=v.reach*pulse.fraction;
                    if(along<clear) {
                        v.groundFrom[i]=v.mouth+v.direction*along;
                        const auto side=v.direction.Cross({0,0,1});
                        const auto start=v.groundFrom[i]+side*(DragonBreath::Radius(v.rank,v.reach)*pulse.side)+RE::NiPoint3{0,0,80};
                        const auto end=start-RE::NiPoint3{0,0,800};
                        const float scale=RE::bhkWorld::GetWorldScale();auto* world=v.cell->GetbhkWorld();
                        if(world&&scale>0) {
                            RE::bhkPickData pick{};
                            pick.rayInput.from={start.x*scale,start.y*scale,start.z*scale,0};
                            pick.rayInput.to={end.x*scale,end.y*scale,end.z*scale,0};
                            pick.rayInput.filterInfo.filter=static_cast<std::uint32_t>(RE::COL_LAYER::kLOS)|(1u<<16);
                            world->PickObject(pick);
                            if(!pick.pickFailed&&pick.rayOutput.HasHit()) {v.groundValid[i]=true;v.groundTo[i]=start+(end-start)*pick.rayOutput.hitFraction;}
                        }
                    }
                }
                if(v.groundValid[i]&&pulse.alpha>0) {
                    const auto travel=v.groundTo[i]-v.groundFrom[i];
                    Posing::PlaceInWorld(flow,bolt.node.get(),v.groundFrom[i],Facing(travel),travel.Length()/100);
                }
                Posing::Paint(bolt.shaders,{.7f,.72f,1},v.groundValid[i]?pulse.alpha:0);
            }
            Posing::Shine(v.lamp,flow,v.mouth,colors[v.element],p.stream*.7f,200);
            Posing::Refresh(root); Posing::Refresh(flow);
        }
    }
    bool Cast(RE::Actor* caster,RE::Actor* target,int rank,int element,int reachRank,int durationRank,const RE::NiPoint3* point) {
        if(!caster||(!target&&!point)||!caster->Is3DLoaded()||caster->IsDead()||(target&&target->IsDead())||element<0||element>2) return false;
        const auto targetPoint=point?*point:target->GetPosition();
        if(!DragonBreath::SafeDistance(caster->GetPosition().GetDistance(targetPoint)))return false;
        // Never commit to a stationary channel with a hostile already within two metres.
        for(const auto& e:Enemies::All())if(auto enemy=e.handle.get();enemy&&Game::Usable(enemy.get())&&!Party::IsPartyActor(enemy.get())&&enemy->GetParentCell()==caster->GetParentCell())
            if(!DragonBreath::SafeDistance(caster->GetPosition().GetDistance(enemy->GetPosition())))return false;
        if(std::ranges::any_of(live,[&](const auto& v){return v.caster==caster->GetHandle();})) return false;
        int copy=0; for(;copy<4;copy++) if(std::ranges::none_of(live,[&](const auto& v){return v.copy==copy;})) break;
        if(copy==4) return false;
        Live v; v.caster=caster->GetHandle();v.cell=caster->GetParentCell();v.rank=std::clamp(rank,1,3);v.element=element;v.copy=copy;
        if(!v.cell||!v.cell->IsAttached()) return false;
        v.duration=DragonBreath::Duration(v.rank,durationRank);v.reach=DragonBreath::Reach(v.rank,reachRank);v.dps=DragonBreath::kDPS[DragonBreath::Tier(v.rank)];
        const auto p=DragonBreath::At(0,v.rank,v.duration);
        v.origin=caster->GetPosition();
        auto flat=targetPoint-caster->GetPosition(); flat.z=0;
        if(flat.Length()<1) flat={std::sin(caster->GetAngleZ()),std::cos(caster->GetAngleZ()),0};
        flat=flat/std::max(flat.Length(),.001f);v.heading=Facing(flat);
        v.headAt=caster->GetPosition()+flat*p.forward+RE::NiPoint3{0,0,p.height};
        v.mouth=v.headAt+flat*(p.muzzleY*p.scale)+RE::NiPoint3{0,0,p.muzzleZ*p.scale};
        auto aim=targetPoint-v.mouth;
        if(aim.Dot(flat)<30) return false; // no backwards breath at an enemy inside the head
        v.direction=flat;v.aim=v.heading; // level beam, never tilted down toward a target
        bool visible=false;
        if(!point && (!caster->HasLineOfSight(target,visible)||ClearReach(v,caster)<(targetPoint-v.mouth).Dot(flat)-55))return false;
        caster->SetHeading(std::atan2(flat.x,flat.y));
        Freeze::Anchor(caster);
        if(Settings::Get().abilityEffects) {
            const float life=DragonBreath::kRelease+v.duration+DragonBreath::kFade+2;
            v.headFX.reset(Effects::BreathModel(caster,copy,v.headAt,life,v.rank>=3?1:0));
            v.streamFX.reset(Effects::BreathModel(caster,copy,v.mouth,life,element+2,v.rank));
        }
        Log::Info("[Dragon's Breath] {} begins rank {} element {}: {:.0f}/s for {:.1f}s, reach {:.0f}."sv,Game::Name(caster),v.rank,element,v.dps,v.duration,v.reach);
        live.push_back(std::move(v));return true;
    }
    void Tick(float dt) {
        if(!std::isfinite(dt)||dt<=0) return;
        std::erase_if(live,[dt](Live& v){
            auto caster=v.caster.get();
            if(!Settings::Get().enabled||!caster||caster->IsDead()||!caster->Is3DLoaded()||caster->GetParentCell()!=v.cell||!v.cell->IsAttached()||caster->IsInRagdollState()||caster->AsActorState()->IsBleedingOut()||caster->AsActorState()->IsStaggered()) {Finish(v);return true;}
            if(!v.found&&v.headFX&&v.streamFX&&!Find(v)&&v.loading<1) {v.loading+=dt;return false;}
            const float before=v.age;v.age+=dt;
            const auto pose=DragonBreath::At(v.age,v.rank,v.duration);
            v.headAt=v.origin+v.direction*pose.forward+RE::NiPoint3{0,0,pose.height};
            v.mouth=v.headAt+v.direction*(pose.muzzleY*pose.scale)+RE::NiPoint3{0,0,pose.muzzleZ*pose.scale};
            caster->SetHeading(std::atan2(v.direction.x,v.direction.y));
            if(!v.released&&v.age>=DragonBreath::kRelease) {
                v.released=true;
                v.audio=EffectAudio::Begin(Soundscape::Kind::DragonBreath,caster.get(),v.mouth,v.duration+.25f,v.rank,false,v.element);
            }
            EffectAudio::Move(v.audio,v.mouth);
            const float clear=ClearReach(v,caster.get());
            if(Settings::Get().abilityEffects) Show(v,clear);
            else { if(v.headFX) v.headFX->lifetime=0; if(v.streamFX) v.streamFX->lifetime=0; Posing::Out(v.lamp); }
            const float active=DragonBreath::ActiveTime(before,v.age,v.duration);
            v.markClock-=dt;const bool mark=v.markClock<=0;if(mark)v.markClock=.65f;
            if(active>0) for(const auto& entry:Enemies::All()) {
                auto enemy=entry.handle.get();
                if(!enemy||!Game::Usable(enemy.get())||!enemy->Is3DLoaded()||Party::IsPartyActor(enemy.get())||enemy->GetParentCell()!=v.cell) continue;
                const auto relative=enemy->GetPosition()+RE::NiPoint3{0,0,70}-v.mouth;
                const float along=relative.Dot(v.direction),radial=(relative-v.direction*along).Length();
                const float front=DragonBreath::At(v.age,v.rank,v.duration).front;
                if(!DragonBreath::Contains(along,radial,v.reach,front,v.rank)||along>clear+55) continue;
                bool unused=false; if(!caster->HasLineOfSight(enemy.get(),unused)) continue;
                auto* values=enemy->AsActorValueOwner();
                const float amount=DragonBreath::Damage(v.dps,active,values->GetActorValue(RE::ActorValue::kResistMagic),values->GetActorValue(resists[v.element]));
                if(amount<=0) continue;
                v.dealt+=PartyBalance::Damage(caster.get(),enemy.get(),amount);
                if(v.element==1) values->DamageActorValue(RE::ActorValue::kStamina,amount);
                if(v.element==2) values->DamageActorValue(RE::ActorValue::kMagicka,amount*.5f);
                if(mark&&Settings::Get().abilityEffects) Effects::Struck(enemy.get(),v.element,.8f);
            }
            if(v.age>=DragonBreath::kRelease+v.duration+DragonBreath::kFade) {
                Log::Info("[Dragon's Breath] {} completes: {:.1f} total damage to enemies in the stream."sv,Game::Name(caster.get()),v.dealt);
                Finish(v);return true;
            }
            return false;
        });
    }
    void Clear() {for(auto& v:live) Finish(v);live.clear();}
    bool Channeling(RE::Actor* caster) {return caster&&std::ranges::any_of(live,[&](const auto& v){return v.caster==caster->GetHandle();});}
}
