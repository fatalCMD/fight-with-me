#include "FWM/RiposteFx.h"
#include "FWM/RiposteGuard.h"
#include <cstdio>
#include <limits>
using namespace FWM::RiposteFx;
int main(){int failures=0;auto check=[&](bool ok,const char* message){if(!ok){++failures;std::printf("FAIL: %s\n",message);}};
 for(auto event:{"DodgeBack","DodgeLeft","DodgeRight","dodgeStart","TKDodgeForward","TKDodgeBack","TKDodgeLeft","TKDodgeRight","StepDodgeBack","tKdOdGeLeFt"}) {
  check(FWM::RiposteGuard::Block(true,event),"guard rejects vanilla/TK dodge entry");
  check(!FWM::RiposteGuard::Block(false,event),"dodge resumes immediately after guard releases");
 }
 for(auto event:{"TKDodgeStop","TKDR_DodgeEnd","TKDR_DodgeStart","DodgeStop","DodgeEnd","blockStart","blockStop","attackStart","staggerStart","Death",""})
  check(!FWM::RiposteGuard::Block(true,event),"cleanup, annotations, counter and hit reactions remain available");
 check(!Landed(.1f,15,0,.2f),"spawned ragdoll is not yet a landing");
 check(!Landed(.7f,80,0,.2f),"apex cannot trigger impact");
 check(!Landed(.7f,15,-150,.2f),"falling rapidly cannot trigger impact");
 check(!Landed(.7f,15,0,.02f),"single jitter frame cannot trigger impact");
 check(!Landed(.7f,-40,0,.2f),"below-floor sample rejected");
 check(Landed(.7f,22,18,.12f),"settled body near floor lands");
 check(!Landed(.7f,std::numeric_limits<float>::quiet_NaN(),0,.2f),"invalid geometry cannot deal damage");
 check(DownSeconds(1)<DownSeconds(2)&&DownSeconds(2)<DownSeconds(3),"hold grows every rank");
 check(ImpactMultiplier(1)<ImpactMultiplier(2)&&ImpactMultiplier(2)<ImpactMultiplier(3),"impact damage grows every rank");
 for(int rank=1;rank<=3;++rank)for(bool ground:{false,true}){
  for(float age:{-1.f,0.f,kSeconds,std::numeric_limits<float>::quiet_NaN()})for(const auto& p:At(age,rank,ground).parts)check(p.alpha==0,"hidden outside lifetime");
  for(int i=0;i<180;++i)for(const auto& p:At(static_cast<float>(i)/120,rank,ground).parts)check(std::isfinite(p.at[2])&&p.alpha>=0&&p.alpha<=1,"finite bounded poses");
 }
 check(At(.15f,1,false).parts[2].alpha==0&&At(.15f,3,false).parts[2].alpha>0,"rank three adds a second shockwave");
 check(At(.6f,3,true).parts[1].alpha>At(.6f,3,false).parts[1].alpha,"floor ripple outlasts short parry flash");
 std::printf("Riposte: %d failures\n",failures);return failures?1:0;
}
