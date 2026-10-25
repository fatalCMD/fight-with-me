#include "FWM/Berserk.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace FWM::Berserk {
namespace {
constexpr float pi=3.14159265359f;
float Ease(float x){x=std::clamp(x,0.f,1.f);return x*x*(3-2*x);}
float Fract(float x){return x-std::floor(x);}
}
std::string Name(std::size_t i){
    if(i<2)return i?"Aftershock":"Shockwave";
    char name[24];
    if(i<2+kPlumes)std::snprintf(name,sizeof(name),"Plume%02u",static_cast<unsigned>(i-2));
    else std::snprintf(name,sizeof(name),"Ember%02u",static_cast<unsigned>(i-2-kPlumes));
    return name;
}
Frame At(float age,float duration,int rank){
    Frame out;
    if(!std::isfinite(age)||!std::isfinite(duration)||age<0||duration<=0||age>=duration+kFade)return out;
    rank=std::clamp(rank,1,3); const bool grand=rank==3;
    const float fade=1-Ease((age-duration)/kFade), appear=Ease(age/.32f);
    const float envelope=fade*appear;
    const float size=grand?1.35f:(rank==2?1.12f:1.f);
    const float surge=std::exp(-std::pow((age-.4f)/.27f,2.f));
    for(std::size_t i=0;i<2;++i){
        auto& p=out.parts[i];const float t=age-kImpact-static_cast<float>(i)*.18f;
        const float f=std::clamp(t/(grand?.9f:.72f),0.f,1.f);
        p.scale=(.4f+Ease(f)*(grand?3.4f:2.15f))*size;
        p.at[2]=3+f*6;p.turn=.12f*static_cast<float>(i);
        p.alpha=(t>=0?(1-Ease(f))*Ease(t/.07f):0)*fade*(i==1?(grand?.58f:.15f):.82f);
        p.color=grand?std::array<float,3>{1,.08f,.018f}:std::array<float,3>{1,.36f,.045f};p.drift=-age*.18f;
    }
    for(std::size_t i=0;i<kPlumes;++i){
        auto& p=out.parts[2+i];const float n=static_cast<float>(i);
        // The front remains open: the fighter and their weapon stay readable.
        const float a=(i<8?(n/8)*2*pi:((n-8)/4)*2*pi+.3f);
        const float sway=.08f*std::sin(age*2.1f+n*1.9f)+.04f*std::sin(age*3.7f+n);
        const float r=(i<8?29.f:43.f)*size;
        p.at={std::cos(a+sway)*r,std::sin(a+sway)*r,8+3*std::sin(age*2+n)};
        p.turn=a+pi/2+sway;
        p.tilt=.04f*std::sin(age*2.7f+n*2.1f);
        p.scale=size*(.83f+.18f*std::sin(n*2.3f)+.16f*surge+.035f*std::sin(age*4.3f+n));
        const float breath=.81f+.12f*std::sin(age*5.1f+n*2.4f)+.07f*std::sin(age*8.7f+n);
        p.alpha=envelope*breath*(i>=8?(grand?.72f:0):grand?.88f:.66f);
        // Crimson edges, hot amber inner tongues: no white overexposure at rank III.
        p.color=grand?std::array<float,3>{1,i%3==0?.25f:.055f,.018f}:std::array<float,3>{1,.38f,.055f};
        p.drift=-age*(.48f+.025f*n)+n*.173f;
    }
    for(std::size_t i=0;i<kEmbers;++i){
        auto& p=out.parts[2+kPlumes+i];const float n=static_cast<float>(i);
        const float f=Fract(age*(.37f+.013f*static_cast<float>(i%7))+n*.618034f);
        const float a=n*2.39996f+age*(i%2?.28f:-.24f);
        const float r=(25+f*(grand?57.f:30.f)+8*std::sin(n))*size;
        p.at={std::cos(a)*r+4*std::sin(age*2+n),std::sin(a)*r,12+f*(grand?208.f:155.f)};
        p.scale=(.65f+static_cast<float>(i%4)*.24f)*(grand?1.2f:1);
        p.alpha=envelope*std::pow(std::sin(pi*f),.65f)*(i<static_cast<std::size_t>(rank==1?24:rank==2?36:48)?.85f:0);
        p.color=grand&&i%3?std::array<float,3>{1,.13f,.025f}:std::array<float,3>{1,.62f,.16f};
    }
    out.light=envelope*((grand?.2f:.11f)+surge*.24f);
    return out;
}
}
