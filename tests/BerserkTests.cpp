#include "FWM/Berserk.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
int main(){
 using namespace FWM::Berserk;int failures=0;
 auto check=[&](bool ok,const char* why){if(!ok){std::printf("FAIL: %s\n",why);++failures;}};
 std::set<std::string> names;for(std::size_t i=0;i<kParts;++i)names.insert(Name(i));
 check(names.size()==kParts,"every animated part has an independent name");
 for(int rank=1;rank<=3;++rank){
  for(float t:{-1.f,0.f,10+kFade,40.f,std::numeric_limits<float>::quiet_NaN()}){
   const auto f=At(t,10,rank);for(const auto& p:f.parts)check(p.alpha==0,"hidden before activation and after fade");check(f.light==0,"no stale light");
  }
  for(int i=0;i<1800;++i){const auto f=At(static_cast<float>(i)/144,10,rank);
   for(const auto& p:f.parts){check(std::isfinite(p.alpha)&&p.alpha>=0&&p.alpha<=1&&std::isfinite(p.scale)&&p.scale>0,"bounded opacity and scale");
    for(float v:p.at)check(std::isfinite(v),"finite positions");}
  }
  check(At(.5f,10,rank).parts[0].alpha>0,"activation shockwave appears");
  check(At(3,10,rank).parts[0].alpha==0,"shockwave does not stay as a floating ring");
  check(At(9.9f,10,rank).parts[2].alpha>0,"mantle persists through whole buff");
  check(At(10.7f,10,rank).parts[2].alpha<At(10.7f,13,rank).parts[2].alpha,"bloodlust extends the mantle without replaying activation");
  check(At(10.7f,13,rank).parts[0].alpha==0,"extension never replays impact");
  check(At(1.2f,.4f,rank).parts[2].alpha<At(1.2f,10,rank).parts[2].alpha,"early cancellation fades promptly");
 }
 check(At(4,10,3).parts[2].scale>At(4,10,1).parts[2].scale*1.3f,"rank III substantially larger");
 check(At(4,10,1).parts[10].alpha==0&&At(4,10,3).parts[10].alpha>0,"rank III adds extra flame plumes");
 const auto red=At(4,10,3).parts[3].color;check(red[0]>red[1]*8,"rank III preserves crimson colour");
 std::printf("Berserk: %s\n",failures?"FAILED":"passed");return failures?1:0;
}
