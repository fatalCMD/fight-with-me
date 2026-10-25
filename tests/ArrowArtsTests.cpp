#include "FWM/ArrowArtsFx.h"
#include "FWM/Paths.h"
#include <cstdio>
#include <limits>
using namespace FWM::ArrowArtsFx;
int main(){int fails=0;auto check=[&](bool v,const char* why){if(!v){++fails;std::printf("FAIL: %s\n",why);}};
 ImpactGate gate;check(!gate.Resolve(false),"no physical collision does not explode");check(gate.Resolve(true),"first collision explodes, regardless of enemy or terrain");check(!gate.Resolve(true),"duplicate impact does not explode again");
 PrimeWindow prime;check(!prime.Claim(.05f),"already airborne arrow cannot take a newly activated charge");
 prime.Advance(.2f);check(prime.Claim(0),"next native bow projectile consumes charge");check(!prime.Claim(0),"second projectile cannot consume charge again");
 PrimeWindow late;late.Advance(kPrimeSeconds);check(late.Expired()&&!late.Claim(0),"unfired charge expires");
 PrimeWindow later;later.Advance(3);check(later.Claim(.02f),"natural shot several seconds later is still empowered");
 PrimeWindow invalid;check(!invalid.Claim(std::numeric_limits<float>::quiet_NaN()),"invalid projectile age rejected");
 for(int surface=0;surface<4;++surface){ImpactGate contact;check(contact.Resolve(true),"actor, blocked shot, terrain and object contacts all detonate");check(!contact.Resolve(true),"hit event plus collision backup cannot double detonate");}
 check(Hurt(30,301,300,0)==0,"outside blast receives no damage");check(Hurt(30,0,300,0)==30,"direct hit gets full damage");check(Hurt(30,150,300,0)<30,"blast falls off with distance");check(Hurt(30,0,300,50)==15,"magic resistance applies");check(Hurt(30,0,0,0)==0,"invalid radius cannot hurt");
 check(Damage(1)<Damage(2)&&Damage(2)<Damage(3)&&Radius(1)<Radius(3),"rank increases explosion and radius");
 check(At(Kind::Rune,1,1).parts[0].alpha>0&&At(Kind::Rune,1,1).parts[1].alpha==0,"ranks one and two use emerald artwork");
 auto rune=At(Kind::Rune,1,3);check(rune.parts[0].alpha==0&&rune.parts[1].alpha>0,"rank three selects generated emerald/crimson artwork");
 check(rune.parts[1].color==std::array<float,3>{1,1,1},"artwork colours are not overwritten by tinting");
 check(At(Kind::Rune,2,3,0).light>At(Kind::Rune,1,3).light,"impact flashes the rune");check(At(Kind::Rune,2,3,kFlash).parts[1].alpha==0,"rune fades after impact");
 check(At(Kind::Wind,kWindSeconds,1).parts[4].alpha==0,"gust is brief");check(At(Kind::Wind,.16f,1).parts[4].at[1]>At(Kind::Wind,.04f,1).parts[4].at[1],"wind travels out from arrow rest");
 for(int kind=0;kind<2;++kind)for(int rank=1;rank<=3;++rank)for(int i=0;i<240;++i)for(const auto& part:At(static_cast<Kind>(kind),static_cast<float>(i)/60,rank).parts)check(std::isfinite(part.scale)&&part.alpha>=0&&part.alpha<=1,"finite bounded animation");
 check(FWM::Paths::Info(FWM::Paths::Ability::kDeadeye).name=="Explosive Arrow","stable save ID exposes new name");
 std::printf("Arrow arts: %d failures\n",fails);return fails?1:0;
}
