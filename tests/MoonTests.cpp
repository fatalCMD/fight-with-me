#include "FWM/MoonFx.h"
#include <cstdio>
#include <limits>
using namespace FWM::MoonFx;
int main(){int failed=0;auto check=[&](bool value,const char* why){if(!value){++failed;std::printf("FAIL: %s\n",why);}};
 for(int rank=1;rank<=3;++rank){
  check(Radius(rank)>=840&&Duration(rank)>=10,"large initial blessing");
  int proc=0;for(int i=0;i<10000;++i)proc+=Negates(Chance(rank),5,true,30,static_cast<float>(i)/10000)?1:0;
  check(std::abs(static_cast<float>(proc)/10000-Chance(rank))<.0002f,"ranked chance is exact");
  check(!Negates(Chance(rank),0,true,30,0),"expired blessing does not protect");
  check(!Negates(Chance(rank),5,false,30,0),"environment and friendly damage unaffected");
  check(!Negates(Chance(rank),5,true,-30,0),"healing is not suppressed");
  check(!Negates(Chance(rank),5,true,std::numeric_limits<float>::quiet_NaN(),0),"invalid damage rejected");
  check(Wave(1.3f,Radius(rank))==0&&Wave(2.4f,Radius(rank))==Radius(rank),"wave release and arrival");
  for(int i=0;i<1800;++i){const auto f=At(static_cast<float>(i)/100,rank);for(const auto& p:f.parts)check(std::isfinite(p.at[2])&&p.alpha>=0&&p.alpha<=1&&std::isfinite(p.scale),"finite bounded frames");}
  const auto f=At(2,rank);check((f.parts[1].alpha>0)==(rank<3),"moon only in first two ranks");check((f.parts[2].alpha>0)==(rank==3),"Azura Star only in final rank");
  const auto aura=Aura(5,5,rank);check(aura.parts[6].alpha>.1f,"aura persists through blessing");check(Aura(9,.2f,rank).parts[6].alpha<aura.parts[6].alpha,"aura fades near expiry");
  check(Aura(10,0,rank).parts[6].alpha==0,"aura ends with blessing");
 }
 const auto prayer=At(3,3),end=At(6.8f,3);
 check(prayer.parts[0].at[1]<-100,"mother behind caster");
 check(end.parts[0].at[2]>25&&end.parts[0].alpha<prayer.parts[0].alpha,"fades above ground without sinking");
 check(At(.2f,3).parts[0].alpha<At(1.5f,3).parts[0].alpha,"gradual appearance while rising");
 check(At(2,3).parts[75].at!=At(3,3).parts[75].at,"wraps flow over time");
 for(std::size_t i=66;i<113;++i){if((i-66)%8==7)continue;const auto& a=prayer.parts[i];const auto& b=prayer.parts[i+1];
  const std::array<float,3> direction{std::sin(a.turn)*std::sin(a.tilt),-std::cos(a.turn)*std::sin(a.tilt),std::cos(a.tilt)};
  for(std::size_t j=0;j<3;++j)check(std::abs(a.at[j]+direction[j]*a.scale-b.at[j])<.01f,"fabric segments remain connected");
 }
 check(At(2,1).parts[0].color[2]>At(2,1).parts[0].color[0]*3,"blue early ranks");
 check(At(2,3).parts[0].color[0]>At(2,3).parts[0].color[1]*2,"purple final rank");
 check(At(2,3).parts[0].scale>At(2,2).parts[0].scale,"larger final apparition");
 for(const auto& p:At(std::numeric_limits<float>::quiet_NaN(),3).parts)check(p.alpha==0,"invalid age remains hidden");
 std::printf("Azura's Moon: %d failures\n",failed);return failed?1:0;
}
