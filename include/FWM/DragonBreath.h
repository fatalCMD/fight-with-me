#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// Shared by the in-game channel, regression tests and the animated preview.
namespace FWM::DragonBreath
{
    inline constexpr float kRelease = 0.90f, kFade = 0.8f, kMinDistance = 140.f; // two metres
    inline constexpr std::array<float,3> kReach{600,850,1100}, kDPS{8,11,14}, kDuration{2.4f,2.8f,3.2f};
    constexpr int Tier(int rank) { return std::clamp(rank,1,3)-1; }
    inline float Reach(int rank, int branch=0) { return kReach[Tier(rank)]*(1+0.1f*std::clamp(branch,0,3)); }
    inline constexpr std::array<float,3> kRadius{90,125,180};
    inline float Radius(int rank, float reach) {return kRadius[Tier(rank)]*reach/kReach[Tier(rank)];}
    inline float Duration(int rank, int branch=0) { return kDuration[Tier(rank)]+0.5f*std::clamp(branch,0,3); }
    inline float Ease(float t) { t=std::clamp(t,0.f,1.f); return t*t*(3-2*t); }
    inline bool SafeDistance(float distance) { return std::isfinite(distance) && distance >= kMinDistance; }
    struct Pose { float alpha{}, jaw{}, stream{}, front{}, scale{}, height{}, forward{}, muzzleY{}, muzzleZ{}, pitch{}, runeAngle{}, runeRadius{}, runeAlpha{}; };
    inline Pose At(float age, int rank, float duration)
    {
        const bool grand=rank>=3;
        const float stop=kRelease+duration;
        Pose p;
        p.alpha=Ease(age/.4f)*(1-Ease((age-stop)/kFade));
        const float open=Ease((age-.38f)/.44f)*(1-Ease((age-stop)/.60f));
        const float pull=Ease(age/.38f)*(1-Ease((age-.38f)/.30f));
        const float since=std::max(0.f,age-.65f);
        const float settle=std::sin(since*13.f)*std::exp(-since*4.f)*open;
        p.pitch=-.09f*pull+.16f*open+.055f*settle+.010f*std::sin(age*8.6f)*open;
        // The skull lifts as the chin drops; the final lower jaw angle stays near -0.5 radians.
        p.jaw=(grand?.32f:.52f)*(1-open)-.50f*open-p.pitch;
        p.stream=Ease((age-kRelease)/.12f)*(1-Ease((age-stop)/.25f));
        p.front=Ease((age-kRelease)/.30f);
        p.scale=grand?7.2f:rank==2?4.4f:3.8f;
        // Base jaw bounds plus clearance for the hinge's travel as the skull lifts.
        p.height=26.f+(grand?32.22402f:56.69074f)*p.scale+3.f*open;
        p.forward=(grand?80.f:60.f)+(grand?44.02418f:57.97861f)*p.scale-12.f*pull+9.f*settle;
        // Throat anchor inside the open mouth, transformed with the skull rather than a point ahead.
        const float throatY=grand?-10.f:-2.f,throatZ=grand?-6.f:-18.f;
        p.muzzleY=std::cos(p.pitch)*throatY-std::sin(p.pitch)*throatZ;
        p.muzzleZ=std::sin(p.pitch)*throatY+std::cos(p.pitch)*throatZ;
        p.runeAngle=age*.30f;
        p.runeRadius=(grand?320.f:rank==2?245.f:220.f)*(.88f+.12f*Ease(age/.65f));
        p.runeAlpha=p.alpha*.55f*(.92f+.08f*std::sin(age*4));
        return p;
    }
    // Exact active overlap prevents frame rate and the fade tail adding damage.
    inline float ActiveTime(float before, float after, float duration)
    {
        if (!std::isfinite(before)||!std::isfinite(after)||after<=before) return 0;
        return std::max(0.f,std::min(after,kRelease+duration)-std::max(before,kRelease));
    }
    inline float BeamProfile(float fraction) { return .16f+.84f*Ease(fraction/.12f); }
    inline bool Contains(float along, float radial, float reach, float front=1, int rank=1)
    {
        return std::isfinite(along)&&std::isfinite(radial)&&reach>0 && along>=0 && along<=reach*front && radial>=0 && radial<=Radius(rank,reach)*BeamProfile(along/reach)+25.f;
    }
    struct ArcPose { float angle{}, x{}, z{}, alpha{}; };
    inline ArcPose ArcAt(float age,int index) {
        const float frame=age*22.f+index*.173f, step=std::floor(frame), blend=Ease((frame-step)*4.f);
        const auto noise=[index](float tick,float offset){const float v=std::sin(tick*12.9898f+index*78.233f+offset)*4375.8545f;return v-std::floor(v);};
        const auto mix=[&](float offset){return noise(step,offset)*(1-blend)+noise(step+1,offset)*blend;};
        return {mix(0)*6.283185f,index<8?0.f:(mix(11)-.5f)*12,index<8?0.f:(mix(27)-.5f)*12,index<8?.65f+.35f*mix(51):std::max(0.f,(mix(51)-.60f)*2.5f)};
    }
    inline float TargetReach(int rank,int branch=0) {const auto p=At(0,rank,Duration(rank));return p.forward+p.muzzleY*p.scale+Reach(rank,branch);}
    inline std::array<float,3> JetColor(int rank,int element,int jet) {
        if(element==0)return rank>=3&&jet<3?std::array<float,3>{.06f,.40f,1.f}:std::array<float,3>{1,.28f,.035f};
        if(element==1)return rank>=3?std::array<float,3>{.58f,.88f,1.f}:std::array<float,3>{.32f,.72f,1.f};
        return {.45f,.42f,1.f};
    }
    struct GroundPose {float alpha{},fraction{},side{};int event{-1};};
    inline GroundPose GroundAt(float age,int index,float duration) {
        const float local=age-kRelease-.22f-index*.42f;
        if(local<0||age>=kRelease+duration)return {};
        const int event=int(local/1.68f);const float t=local-event*1.68f;
        const float seed=std::sin(event*13.f+index*7.f+1.f)*431.f,noise=seed-std::floor(seed);
        return {t<.20f?std::sin(t/.20f*3.1415926f):0.f,.24f+.55f*noise,(index%2?1.f:-1.f)*(1.1f+.5f*noise),event};
    }
    inline float Damage(float dps,float dt,float magic,float element)
    {
        if (!std::isfinite(dps)||!std::isfinite(dt)||!std::isfinite(magic)||!std::isfinite(element)) return 0;
        return std::max(dps,0.f)*std::max(dt,0.f)*(1-std::clamp(magic,-100.f,100.f)/100)*(1-std::clamp(element,-100.f,100.f)/100);
    }
}
