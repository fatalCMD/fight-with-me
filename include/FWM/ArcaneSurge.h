#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace FWM::ArcaneSurge {
inline constexpr std::size_t kCopies=4, kParts=30;
inline constexpr float kFade=1.0f;
struct Part {
    std::array<float,3> at{},color{.56f,.40f,1.0f};
    float scale{1},turn{},tilt{},alpha{},drift{};
};
struct Frame { std::array<Part,kParts> parts{}; float light{}; };
inline std::string Name(std::size_t i) { return "Surge"+(i<10?std::string("0"):std::string())+std::to_string(i); }
inline float Smooth(float x) { x=std::clamp(x,0.f,1.f);return x*x*(3-2*x); }
inline Frame At(float age,float duration,int rank) {
    Frame f;
    if(!std::isfinite(age)||!std::isfinite(duration)||age<0||duration<=0||age>=duration+kFade)return f;
    rank=std::clamp(rank,1,3);
    const bool grand=rank==3;
    const float envelope=Smooth(age/.6f)*(1-Smooth((age-duration)/kFade));
    const float radius=grand?1.35f:rank==2?1.12f:1;
    const float breath=.92f+.08f*std::sin(age*(grand?1.65f:2.1f));
    for(auto& p:f.parts) {p.scale=radius;p.alpha=envelope*breath;p.at[2]=2;}
    auto& seal=f.parts[0];seal.turn=age*.09f;seal.color={1,1,1};seal.alpha*=grand?0:.72f;
    f.parts[1].alpha=0; // Slot retained for mesh compatibility; generated artwork includes its inner seal.
    auto& rim=f.parts[2];rim.turn=-age*.065f;rim.drift=-age*.16f;rim.alpha*=grand?.56f:.37f;
    auto& outer=f.parts[3];outer.turn=-age*.045f;outer.alpha*=grand?.82f:0;outer.color={1,1,1};
    auto& rim2=f.parts[4];rim2.turn=age*.055f;rim2.drift=-age*.11f;rim2.alpha*=grand?.32f:0;
    auto& glow=f.parts[5];glow.alpha*=grand?.23f:.12f;glow.scale*=1+.025f*std::sin(age*1.65f);glow.at[2]=1;
    for(std::size_t i=6;i<kParts;++i){
        auto& p=f.parts[i];const float n=static_cast<float>(i-6),period=3.5f+.17f*static_cast<float>(i%5);
        const float phase=std::fmod(age/period+n*.381966f,1.f),angle=n*2.399963f+age*.09f;
        const float r=(grand?79.f:61.f)*radius;
        p.at={std::cos(angle)*r,std::sin(angle)*r,4+phase*(grand?62:37)};
        p.scale=(grand?1.1f:.75f)*(.75f+.25f*std::sin(phase*3.141593f));
        p.color={.79f,.69f,1};
        p.alpha=envelope*Smooth(phase/.18f)*Smooth((1-phase)/.35f)*(grand?.55f:.32f);
        if(!grand&&i>=18)p.alpha=0;
    }
    f.light=envelope*breath*(grand?.12f:.055f);
    return f;
}
}
