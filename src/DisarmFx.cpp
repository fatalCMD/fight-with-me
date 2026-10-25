#include "FWM/DisarmFx.h"
#include <algorithm>
#include <cmath>
namespace FWM::DisarmFx {
namespace { float Ease(float t){t=std::clamp(t,0.0f,1.0f);return t*t*(3-2*t);}
float Pulse(float t,float start,float rise,float hold,float fall){return Ease((t-start)/rise)*(1-Ease((t-start-rise-hold)/fall));} }
Look At(float t,int rank){
 Look out;if(!std::isfinite(t)||t<0||t>=kSeconds)return out;
 rank=std::clamp(rank,1,3);const bool grand=rank==3;
 const std::array<float,3> color=grand?std::array<float,3>{1,.025f,.045f}:std::array<float,3>{.045f,.35f,1};
 const float size=rank==1?1.0f:rank==2?1.15f:1.4f;
 out.parts[0]={{0,0,0},size*(.85f+.15f*Ease(t/.12f)),Pulse(t,0,.045f,.025f,.19f)*.8f,grand?std::array<float,3>{1,1,1}:color};
 for(std::size_t i=1;i<=2;++i){
  const float delay=i==1?.015f:.065f;
  out.parts[i]={{0,0,0},size*(.82f+.18f*Ease((t-delay)/.10f)),(i==1||grand)?Pulse(t,delay,.06f,.035f,.23f):0,color};
 }
 out.light=Pulse(t,0,.035f,.025f,.22f)*.75f;
 const std::size_t count=grand?kSparks:rank==2?24:18;
 for(std::size_t i=0;i<count;++i){
  const float n=static_cast<float>(i),delay=.025f+std::fmod(n*.017f,.055f),u=t-delay;
  const float life=.32f+std::fmod(n*.073f,.29f);if(u<=0||u>=life)continue;
  const float az=n*2.39996323f,up=.22f+std::fmod(n*.317f,.9f),speed=(100+std::fmod(n*41.0f,115.0f))*size;
  auto& p=out.parts[i+3];p.at={std::cos(az)*speed*u,std::sin(az)*speed*u,up*speed*u-210*u*u};
  p.scale=(.65f+std::fmod(n*.23f,.65f))*(grand?1.2f:1);
  p.alpha=Pulse(u,0,.015f,.025f,life-.04f);
  p.color=i%4==0?std::array<float,3>{1,.94f,.8f}:color;
 }
 return out;
}
}

