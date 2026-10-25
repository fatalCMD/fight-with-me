#include "FWM/RiposteFx.h"
namespace FWM::RiposteFx {
Look At(float age,int rank,bool ground){
 Look out;if(!std::isfinite(age)||age<=0||age>=kSeconds)return out;
 const int r=Rank(rank);const float strength=1+.18f*static_cast<float>(r-1);
 const std::array<float,3> tint=r==3?std::array<float,3>{1,.12f,.045f}:std::array<float,3>{.12f,.52f,1};
 auto envelope=[age](float start,float attack,float end){return std::clamp((age-start)/attack,0.f,1.f)*std::clamp((end-age)/(end-start-attack),0.f,1.f);};
 auto& flash=out.parts[0];flash.scale=strength*(.55f+age*2);flash.alpha=envelope(0,.018f,ground?.20f:.24f);flash.color={1,1,1};
 out.light=flash.alpha*(ground?.6f:1);
 for(int j=0;j<2;++j){auto& p=out.parts[static_cast<std::size_t>(j+1)];const float start=static_cast<float>(j)*.10f;
  p.alpha=(j==0||r==3)?envelope(start,.04f,ground?.75f:.44f)*.85f:0;p.color=tint;
  p.scale=strength*(.25f+std::max(0.f,age-start)*(ground?2.3f:1.8f));p.at[2]=ground?2.5f+static_cast<float>(j):0;
 }
 for(std::size_t i=3;i<kParts;++i){auto& p=out.parts[i];const float n=static_cast<float>(i-3),theta=n*2.39996323f;
  const float life=.32f+.014f*static_cast<float>((i*7)%40),t=age;
  p.alpha=envelope(.012f,.025f,life)*(i<27||r==3?1.f:0.f);
  const float speed=(65+static_cast<float>((i*37)%105))*strength,radial=speed*t;
  p.at={std::cos(theta)*radial,std::sin(theta)*radial,ground?std::max(1.f, (30+static_cast<float>((i*13)%60))*t-100*t*t):std::sin(n*1.71f)*radial-90*t*t};
  p.scale=(.55f+.11f*static_cast<float>(i%5))*strength*(1-.5f*t);p.color=i%3==0?std::array<float,3>{1,.88f,.63f}:tint;
 }
 return out;
}
}
