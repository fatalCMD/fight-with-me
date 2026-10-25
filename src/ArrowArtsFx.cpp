#include "FWM/ArrowArtsFx.h"
namespace FWM::ArrowArtsFx {
Look At(Kind kind,float age,int rank,float impact){
 Look out;if(!std::isfinite(age)||!std::isfinite(impact)||age<0)return out;const int r=Rank(rank);const float s=1+.14f*static_cast<float>(r-1);
 const std::array<float,3> white{.8f,1,.92f};
 auto set=[&](std::size_t i,float alpha,float scale,const std::array<float,3>& c,float turn=0){auto& p=out.parts[i];p.alpha=std::clamp(alpha,0.f,1.f);p.scale=scale;p.color=c;p.turn=turn;};
 if(kind==Kind::Rune){
  const float fade=std::clamp(age/.22f,0.f,1.f)*(impact<0?1:std::clamp(1-impact/kFlash,0.f,1.f));
  const float hit=impact>=0?std::max(0.f,1-impact/.25f):0;
  // Generated artwork supplies the emerald/crimson gradient; white preserves its colours.
  set(r==3?1:0,fade*(.58f+.35f*hit),s*(1+.06f*hit),{1,1,1},age*.6f);
  set(7,fade*hit*.65f,s*1.3f,white);out.light=fade*hit;return out;
 }
 if(kind==Kind::Wind){
  if(age>=kWindSeconds)return out;
  const float t=age/kWindSeconds,fade=std::sin(std::min(1.f,t*4)*1.5707963f)*std::pow(1-t,1.5f);
  for(std::size_t i=4;i<7;++i){set(i,fade*(i==4?.65f:.38f),s*(.35f+1.3f*t),white,age*10+static_cast<float>(i));out.parts[i].at[1]=t*(35+12*static_cast<float>(i-4));}
  for(std::size_t i=8;i<20;++i){const float a=static_cast<float>(i)*2.399963f;set(i,fade*.45f,.5f,white);out.parts[i].at={std::cos(a)*t*22,t*65,std::sin(a)*t*22};}
  return out;
 }
 return out;
}
}
