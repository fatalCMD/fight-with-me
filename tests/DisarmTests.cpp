#include "FWM/DisarmFx.h"
#include <cstdio>
#include <cmath>
#include <limits>
using namespace FWM::DisarmFx;
int main(){int failed=0;auto check=[&](bool b,const char* text){if(!b){std::printf("FAIL: %s\n",text);++failed;}};
 for(int rank=1;rank<=3;++rank){
 for(float t:{-1.0f,0.0f,kSeconds,std::numeric_limits<float>::quiet_NaN()})for(const auto& p:At(t,rank).parts)check(p.alpha==0,"hidden before impact and after expiry");
 for(int i=0;i<200;++i){const auto f=At(static_cast<float>(i)/200,rank);for(const auto& p:f.parts)check(std::isfinite(p.at[0])&&std::isfinite(p.at[1])&&std::isfinite(p.at[2])&&p.alpha>=0&&p.alpha<=1,"bounded finite poses");}
 check(At(.09f,rank).parts[0].alpha>0,"fast contact flash");check(At(.55f,rank).parts[1].alpha==0,"slash fades quickly");}
 check(At(.15f,1).parts[2].alpha==0&&At(.15f,2).parts[2].alpha==0,"lower ranks single slash");
 const auto three=At(.15f,3);check(three.parts[1].alpha>0&&three.parts[2].alpha>0,"rank III crossed slashes overlap");
 check(three.parts[1].color[0]>.9f&&three.parts[1].color[2]<.1f,"rank III red cut");check(three.parts[0].color[0]==three.parts[0].color[2],"rank III white flash");
 check(At(.1f,1).parts[1].color[2]>.9f,"lower ranks blue");
 check(At(.3f,3).parts[3].at!=At(.2f,3).parts[3].at,"sparks fly through space");
 std::printf("Disarm: %d failures\n",failed);return failed?1:0;}
