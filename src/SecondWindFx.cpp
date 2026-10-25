#include "FWM/SecondWindFx.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace FWM::SecondWindFx {
namespace {
constexpr float pi=3.14159265f;
float Ease(float t){t=std::clamp(t,0.0f,1.0f);return t*t*(3-2*t);}
float Pulse(float t,float start,float rise,float hold,float fall){return Ease((t-start)/rise)*(1-Ease((t-start-rise-hold)/fall));}
}
std::string Name(std::size_t i){
    if(i<3)return i==0?"Pulse":i==1?"Echo":"Heart";
    char name[24];
    std::snprintf(name,sizeof(name),i<3+kWisps?"Wisp%02u":"Mote%02u",static_cast<unsigned>(i<3+kWisps?i-3:i-3-kWisps));
    return name;
}
Frame At(float t,int rank){
    Frame out;if(!std::isfinite(t)||t<0||t>=kSeconds)return out;
    rank=std::clamp(rank,1,3);const bool grand=rank==3;
    const float size=grand?1.28f:rank==2?1.1f:1;
    const std::array<float,3> jade{.15f,1,.55f},gold{1,.78f,.22f},pale{.82f,1,.88f};
    // A tiny inward breath precedes a single outward release, plus a delayed echo at III.
    for(std::size_t i=0;i<2;++i){
        auto& p=out.parts[i];const float delay=i==0?.20f:.49f,u=t-delay;
        p.scale=(.48f+1.95f*Ease(u/.72f))*size;p.at[2]=3+12*Ease(u/.8f);
        p.alpha=(i==0||grand)?Pulse(t,delay,.065f,.02f,.7f)*(i==0?.55f:.4f):0;
        p.turn=t*.14f;p.drift=-t*.12f;p.color=grand&&i==1?gold:jade;
    }
    auto& heart=out.parts[2];heart.at={0,0,85};heart.scale=size*(.7f+.5f*Ease((t-.18f)/.25f));
    heart.alpha=Pulse(t,.12f,.14f,.02f,.38f)*(grand?.54f:.36f);heart.color=pale;
    for(std::size_t i=0;i<kWisps;++i){
        auto& p=out.parts[3+i];const float n=static_cast<float>(i);
        const float delay=.045f*static_cast<float>(i%4),u=std::max(0.0f,t-delay);
        p.turn=n*2*pi/8+u*(i%2?.95f:-.72f);
        p.at={0,0,5+46*Ease((u-.24f)/1.5f)};
        p.scale=size*(.72f+.3f*Ease(u/.22f)+.14f*Ease((u-.3f)/1.4f))*(i>=8?1.18f:1);
        p.tilt=.045f*std::sin(u*2.8f+n);
        p.alpha=(i<static_cast<std::size_t>(rank==1?6:rank==2?8:12))?Pulse(t,delay,.24f,.17f,1.65f)*(i>=8?.48f:.58f):0;
        p.color=grand&&i%3==0?gold:jade;p.drift=-t*.45f+n*.13f;
    }
    for(std::size_t i=0;i<kMotes;++i){
        auto& p=out.parts[3+kWisps+i];const float n=static_cast<float>(i),delay=std::fmod(n*.027f,.18f),u=std::max(0.0f,t-delay);
        const float angle=n*2.39996323f+u*(i%2?.7f:-.6f);
        const float breath=1-Ease(u/.23f),release=Ease((u-.23f)/1.05f);
        const float radius=(23+23*breath+(24+std::fmod(n*7.0f,40.0f))*release)*size;
        p.at={std::cos(angle)*radius,std::sin(angle)*radius,18+std::fmod(n*19.0f,97.0f)+u*(grand?56.0f:40.0f)};
        p.scale=(.5f+std::fmod(n*.17f,.8f))*(grand?1.25f:1);
        p.alpha=i<static_cast<std::size_t>(rank==1?28:rank==2?40:56)?Pulse(t,delay,.18f,.35f,1.35f)*.85f:0;
        p.color=grand&&i%3==0?gold:i%4==0?pale:jade;
    }
    out.light=Pulse(t,.12f,.14f,.035f,.5f)*(grand?.65f:.38f)+(grand?Pulse(t,.49f,.08f,0,.45f)*.2f:0);
    return out;
}
}
