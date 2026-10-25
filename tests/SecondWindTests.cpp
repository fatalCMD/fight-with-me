#include "FWM/SecondWindFx.h"
#include <cmath>
#include <cstdio>
#include <limits>
using namespace FWM::SecondWindFx;
int main(){int failed=0;auto check=[&](bool b,const char* why){if(!b){++failed;std::printf("FAIL: %s\n",why);}};
 for(int rank=1;rank<=3;++rank){
    for(float t:{-1.0f,0.0f,kSeconds,std::numeric_limits<float>::quiet_NaN()}){
        const auto f=At(t,rank);check(f.light==0,"no light outside reveal");for(const auto& p:f.parts)check(p.alpha==0,"clean start and finish");
    }
    for(int i=0;i<400;++i){const auto f=At(static_cast<float>(i)/120,rank);for(const auto& p:f.parts)check(std::isfinite(p.at[0])&&std::isfinite(p.at[1])&&std::isfinite(p.at[2])&&p.alpha>=0&&p.alpha<=1&&std::isfinite(p.scale),"finite bounded animation");}
    check(At(.3f,rank).parts[2].alpha>0,"restorative chest flash");
    check(At(1,rank).parts[0].alpha==0,"ground pulse remains brief");
    check(At(1.4f,rank).parts[15].at[2]>At(.6f,rank).parts[15].at[2],"energy motes rise");
 }
 check(At(.6f,1).parts[1].alpha==0&&At(.6f,2).parts[1].alpha==0&&At(.6f,3).parts[1].alpha>0,"rank III adds a second pulse");
 check(At(.5f,3).parts[0].scale>At(.5f,2).parts[0].scale,"rank III larger release");
 check(At(.5f,3).parts[14].alpha>0&&At(.5f,2).parts[14].alpha==0,"rank III outer wisps");
 check(At(.5f,3).parts.back().alpha>0&&At(.5f,2).parts.back().alpha==0,"rank III extra motes");
 std::printf("Second Wind: %d failures\n",failed);return failed?1:0;}
