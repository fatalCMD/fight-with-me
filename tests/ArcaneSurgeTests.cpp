#include "FWM/ArcaneSurge.h"
#include <limits>
int main(){
 using namespace FWM::ArcaneSurge;
 int failures=0;auto check=[&](bool ok,const char* label){if(!ok){++failures;std::printf("FAIL: %s\n",label);}};
 for(int rank=1;rank<=3;++rank){
  check(At(0,12,rank).parts[0].alpha==0,"fades in");
  check(At(13,12,rank).parts[0].alpha==0,"fades out");
  for(float t=.7f;t<12;t+=.017f){
   const auto f=At(t,12,rank),n=At(t+.001f,12,rank);
   const auto& seal=f.parts[rank==3?3:0];
   check(seal.alpha>.55f,"seal persists through whole buff");
   check(f.parts[2].alpha>.2f,"rim persists through whole buff");
   check(f.parts[0].at[0]==0&&f.parts[0].at[1]==0,"seal remains centered on caster");
   for(std::size_t i=0;i<kParts;++i){
    check(std::isfinite(f.parts[i].alpha)&&f.parts[i].alpha>=0&&f.parts[i].alpha<=1,"bounded opacity");
    check(std::abs(n.parts[i].alpha-f.parts[i].alpha)<.01f,"smooth glow");
    if(i>=6&&std::abs(n.parts[i].at[2]-f.parts[i].at[2])>1)check(f.parts[i].alpha<.001f&&n.parts[i].alpha<.001f,"motes respawn invisibly");
   }
   check(rank==3?f.parts[3].alpha>.6f:f.parts[3].alpha==0,"outer seal reserved for rank III");
  }
  check(At(12.8f,12,rank).parts[rank==3?3:0].alpha<At(12.2f,12,rank).parts[rank==3?3:0].alpha,"ending dims progressively");
 }
 check(At(2,12,3).parts[0].scale>At(2,12,2).parts[0].scale,"rank III larger");
 check(std::abs(At(2,12,3).parts[3].turn)<std::abs(At(2,12,1).parts[0].turn),"rank III weightier rotation");
 check(At(std::numeric_limits<float>::quiet_NaN(),12,1).parts[0].alpha==0,"invalid time stays dark");
 std::printf("Arcane Surge persistence, rank distinction and fading: %s\n",failures?"FAILED":"passed");return failures?1:0;
}
