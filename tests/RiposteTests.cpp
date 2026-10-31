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
 for(auto event:{"TKDodgeStop","TKDR_DodgeEnd","TKDR_DodgeStart","DodgeStop","DodgeEnd","blockStart","attackStop","attackRelease","bashRelease","blockStopped","staggerStart","Death",""})
  check(!FWM::RiposteGuard::Block(true,event),"cleanup, raising the guard and hit reactions remain available");
 for(auto event:{"attackStart","AttackStartLeftHand","attackStartDualWield","attackStartSprint","attackPowerStartForward","attackPowerStartInPlace","bashStart","bashPowerStart","BeginCastLeft","BeginCastRight","blockStop","BlockStop"}) {
  check(FWM::RiposteGuard::Block(true,event),"the AI cannot swing, cast or drop the guard behind it");
  check(!FWM::RiposteGuard::Block(false,event),"swings resume once the guard is lowered, so the counter goes through");
 }
 check(FWM::RiposteGuard::Kind("TKDodgeBack")==FWM::RiposteGuard::Held::Dodge&&FWM::RiposteGuard::Kind("attackStart")==FWM::RiposteGuard::Held::Swing&&FWM::RiposteGuard::Kind("blockStop")==FWM::RiposteGuard::Held::Lower,"held events are told apart for the log");
 {
  using FWM::RiposteGuard::CanGuard;using H=FWM::RiposteGuard::Hand;
  check(CanGuard(H::Melee,H::Empty)&&CanGuard(H::Melee,H::Shield)&&CanGuard(H::Melee,H::Melee)&&CanGuard(H::Empty,H::Empty)&&CanGuard(H::Melee,H::Torch),"a blade, a shield, two blades or fists can block");
  check(!CanGuard(H::Melee,H::Spell)&&!CanGuard(H::Melee,H::Staff),"a spell or staff in the off hand cannot");
  check(!CanGuard(H::Spell,H::Empty)&&!CanGuard(H::Spell,H::Spell)&&CanGuard(H::Spell,H::Shield),"spells block only behind a shield");
  check(!CanGuard(H::Bow,H::Empty)&&!CanGuard(H::Staff,H::Empty)&&!CanGuard(H::Staff,H::Shield),"bows and staves never guard");
  using FWM::RiposteGuard::Watch;using S=Watch::Step;
  auto run=[](Watch& w,float seconds,bool blocking,bool busy){S last=S::Holding;for(float t=0;t<seconds;t+=1.f/60){last=w.Advance(1.f/60,blocking,busy);if(last!=S::Holding)return last;}return last;};
  Watch idle;check(run(idle,3,false,false)==S::NeverRaised&&idle.age<FWM::RiposteGuard::kSlack+.05f,"an idle fighter who never blocks is let go after the slack");
  Watch swinging;check(run(swinging,FWM::RiposteGuard::kRaiseWithin-.1f,false,true)==S::Holding,"a swing in progress may finish before the guard comes up");
  check(run(swinging,1,false,true)==S::NeverRaised,"swinging alone never holds a guard open");
  Watch blocker;check(blocker.Advance(.1f,false,true)==S::Holding&&blocker.Advance(.1f,true,false)==S::Raised,"the first block raises it, once");
  check(run(blocker,8,true,false)==S::Holding,"a held block lasts");
  check(run(blocker,.6f,false,true)==S::Holding&&run(blocker,.6f,false,false)==S::Holding,"swings between blocks do not count against it");
  check(run(blocker,.6f,false,false)==S::Dropped,"a raised guard goes after the slack spent idle");
  check(FWM::RiposteGuard::kMissesToBench>=2,"one failed guard is not enough to bench it");
  auto alone=[](Watch& w,float seconds,bool threatened){S last=S::Holding;for(float t=0;t<seconds;t+=1.f/60){last=w.Advance(1.f/60,true,false,threatened);if(last==S::Alone)return last;}return last;};
  Watch waiting;check(alone(waiting,FWM::RiposteGuard::kAloneFor-.1f,false)!=S::Alone&&alone(waiting,1,true)!=S::Alone,"a moment without an attacker does not lower it");
  check(alone(waiting,FWM::RiposteGuard::kAloneFor-.1f,false)!=S::Alone,"a new attacker starts the count again");
  check(alone(waiting,.3f,false)==S::Alone,"with nobody near after them, the guard goes");
 }
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
