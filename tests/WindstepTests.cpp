#include "FWM/WindstepFx.h"
#include <cstdio>
#include <limits>
int main(){using namespace FWM::WindstepFx;int failed=0;auto check=[&](bool ok,const char* msg){if(!ok){++failed;std::printf("FAIL: %s\n",msg);}};
 check(!SafeSample(false,0,1),"reject missing ground / ledge");check(!SafeSample(true,-50,1),"reject drop");check(!SafeSample(true,35,1),"reject steep step");check(!SafeSample(true,0,.8f),"reject wall");check(SafeSample(true,12,1),"accept gentle slope");check(!SafeSample(true,std::numeric_limits<float>::quiet_NaN(),1),"reject invalid terrain");
 check(Travel(0)==0&&Travel(kTravel)==1,"retreat starts and finishes exactly");check(Speed(0,210)==0&&Speed(kTravel,210)==0,"no abrupt impulse at either end");
 check(Protection(1)==0&&Protection(2)==.2f&&Protection(3)==.3f,"defense rank progression");
 float integral=0;for(int i=0;i<650;++i)integral+=Speed((static_cast<float>(i)+.5f)*.001f,210)*.001f;check(std::abs(integral-210)<.02f,"velocity covers intended distance");
 for(int r=1;r<=3;++r){for(float t=0;t<kVisual+.2f;t+=.017f){const auto f=At(t,r);for(const auto& p:f.parts)check(std::isfinite(p.alpha)&&p.alpha>=0&&p.alpha<=1&&std::isfinite(p.scale),"finite bounded fade");}check(At(0,r).parts[0].alpha==0&&At(kVisual+.1f,r).parts[0].alpha==0,"echo fades in and fully out");}
 check(At(EchoHold(1)+.2f,1).parts[51].alpha==0&&At(EchoHold(2)+.2f,2).parts[51].alpha==0&&At(EchoHold(3)+.2f,3).parts[51].alpha>0,"stagger wave is exclusive to rank III");
 check(EchoHold(1)==4&&EchoHold(2)==5&&EchoHold(3)==6,"echo duration progression");
 for(int r=1;r<=3;++r){
  check(At(EchoHold(r)-.1f,r).parts[0].alpha>.5f,"echo stays visible until its hold ends");
  check(At(EchoHold(r)+kFade*.5f,r).parts[0].alpha>0&&At(EchoHold(r)+kFade*.5f,r).parts[0].alpha<.4f,"soft final fade");
  check(At(VisualSeconds(r),r).parts[0].alpha<.00001f,"target expiry and visual expiry coincide");
  check(At(1,r).parts[51].alpha==0,"no premature final gust");
 }
 check(At(.2f,3).parts[1].alpha>0&&At(.2f,2).parts[1].alpha==0,"rank III adds additional echoes");return failed?1:0;
}
