#include "FWM/MoonFx.h"
#include <numbers>
namespace FWM::MoonFx {
namespace {
constexpr float pi=std::numbers::pi_v<float>;
float ease(float a,float b,float x){const float t=std::clamp((x-a)/(b-a),0.0f,1.0f);return t*t*(3-2*t);}
std::array<float,3> tint(int rank){return rank>=3?std::array<float,3>{.48f,.14f,1}:std::array<float,3>{.15f,.52f,1};}
}
std::string Name(std::size_t i){
 if(i<6)return std::array<std::string,6>{"Mother","Moon","Star","Halo","Wave","Mist"}[i];
 if(i==114)return "MotherAura";
 const auto n=i<18?i-6:i<66?i-18:i-66;
 return std::string(i<18?"Lunar":i<66?"Mote":"Wrap")+(n<10?"0":"")+std::to_string(n);
}
Frame Aura(float age,float left,int rank){
 Frame f;if(!std::isfinite(age)||!std::isfinite(left)||age<0||left<=0)return f;
 const float visible=ease(0,.65f,age)*ease(0,1,left);const auto color=tint(rank);
 for(auto& p:f.parts)p.color=color;
 // Slow suspended crescent wards and a translucent lunar veil, rather than rising spirals.
 for(std::size_t i=6;i<18;++i){auto& p=f.parts[i];const float n=static_cast<float>(i-6),angle=n*pi/3+age*.12f;
  p.at={std::cos(angle)*36,std::sin(angle)*36,60+n*15+3*std::sin(age*.8f+n)};
  p.turn=angle+pi*.5f;p.tilt=.12f*std::sin(age*.6f+n);p.scale=rank>=3?1.25f:1;
  p.alpha=visible*(rank>=3?.82f:.64f)*(.87f+.13f*std::sin(age*1.4f+n));
  if(i>=12){p.at={0,0,12};p.turn=n*pi/3+age*.05f;p.alpha*=.5f;p.drift=age*.025f;}
 }
 for(std::size_t i=18;i<66;++i){auto& p=f.parts[i];const float n=static_cast<float>(i-18),angle=n*2.39996f+age*.045f,rad=27+19*std::abs(std::sin(n*7.1f));
  p.at={std::cos(angle)*rad,std::sin(angle)*rad,15+std::fmod(n*37.3f,135.0f)+4*std::sin(age*.5f+n)};
  p.alpha=visible*(rank>=3?.8f:.5f)*(.65f+.35f*std::pow(std::sin(age*.7f+n),2.0f));p.scale=.45f+.4f*std::abs(std::sin(n));
 }
 return f;
}
Frame At(float age,int rank,float radius,bool compact){
 if(!std::isfinite(age)||age<0||age>=kSeconds)return {};
 rank=std::clamp(rank,1,3);if(radius<=0)radius=Radius(rank);
 const float casterArrival=kRelease+150/radius*kTravel;
 Frame f=Aura(std::max(0.0f,age-casterArrival),Duration(rank)-(age-casterArrival),rank);
 const auto color=tint(rank);for(auto& p:f.parts)p.color=color;
 const float rise=ease(0,1.7f,age),fade=1-ease(4.6f,kApparition,age),alpha=ease(.15f,1.4f,age)*fade;
 const float size=(rank==3?3.15f:rank==2?2.8f:2.5f)*(1+.004f*std::sin(age*1.2f));
 auto& mother=f.parts[0];mother.at={3*std::sin(age*.55f),-150, -size*151*(1-rise)+38+4*std::sin(age*1.1f)};mother.scale=size;
 mother.tilt=.018f*std::sin(age*1.3f);mother.turn=.027f*std::sin(age*.8f);mother.alpha=alpha*.7f;
 const float celestial=ease(.75f,1.85f,age)*fade;
 for(std::size_t i=1;i<=3;++i){auto& p=f.parts[i];p.at={mother.at[0],-150,162*size+mother.at[2]+36};p.scale=rank==3?1.65f:1.35f;
  p.alpha=celestial;p.turn=.1f*std::sin(age*.45f);
 }
 f.parts[1].alpha*=rank<3?1.0f:0.0f;f.parts[2].alpha*=rank==3?1.0f:0.0f;
 f.parts[3].alpha*=.38f*(1+.12f*std::sin(age*2));f.parts[3].scale*=1+.03f*std::sin(age*2);
 auto& wave=f.parts[4];wave.at={0,-150,5};wave.scale=std::max(.001f,Wave(age,radius)/50);
 wave.alpha=ease(kRelease,kRelease+.08f,age)*(1-ease(kRelease+.65f,kRelease+kTravel,age))*.8f;
 auto& mist=f.parts[5];mist.at={0,-150,4};mist.scale=1.5f+.25f*rise;mist.alpha=alpha*.4f;mist.turn=age*.1f;mist.drift=age*.04f;
 // Linked fabric strips: every end is exactly the next segment's start, with delayed wind waves.
 const auto clothPoint=[&](int strand,float t){
  const float sign=strand%2?1.0f:-1.0f;const int pair=strand/2;
  const float height=pair==0?107.0f:pair==1?70.0f:119.0f;
  float x=sign*(pair==2?8.0f:13.0f)+sign*std::sin(t*pi*.8f)*19+t*5*std::sin(age*1.4f-t*5+static_cast<float>(strand));
  float y=-3-t*26+t*7*std::sin(age*1.1f-t*4+static_cast<float>(strand)*.8f);
  float z=height-t*(pair==1?64.0f:85.0f)+t*3*std::sin(age*1.2f-t*3);
  const float yy=y*std::cos(mother.tilt)-z*std::sin(mother.tilt);z=y*std::sin(mother.tilt)+z*std::cos(mother.tilt);y=yy;
  const float xx=x*std::cos(mother.turn)-y*std::sin(mother.turn);y=x*std::sin(mother.turn)+y*std::cos(mother.turn);x=xx;
  return std::array<float,3>{mother.at[0]+x*size,mother.at[1]+y*size,mother.at[2]+z*size};
 };
 for(std::size_t i=66;i<114;++i){auto& p=f.parts[i];const int index=static_cast<int>(i-66),strand=index/8,segment=index%8;
  const auto a=clothPoint(strand,static_cast<float>(segment)/8),b=clothPoint(strand,static_cast<float>(segment+1)/8);
  const float x=b[0]-a[0],y=b[1]-a[1],z=b[2]-a[2],length=std::sqrt(x*x+y*y+z*z);
  p.at=a;p.scale=length;p.turn=std::atan2(x,-y);p.tilt=std::acos(std::clamp(z/length,-1.0f,1.0f));p.alpha=alpha*.7f;
 }
 auto& aura=f.parts[114];aura.at=mother.at;aura.scale=size;aura.alpha=alpha*.24f;aura.drift=age*.025f;
 if(compact)for(std::size_t i=0;i<kParts;++i){
  if(i==4 || (i>=6 && i<66))continue; // The blessing radius and recipients retain their original effect.
  auto& p=f.parts[i];const float factor=rank>=3?.42f:.38f;
  p.at[0]*=factor;p.at[1]=-150+(p.at[1]+150)*factor;p.at[2]*=factor;p.scale*=factor;
 }
 f.light=alpha*.3f+wave.alpha*.5f;
 return f;
}
}

