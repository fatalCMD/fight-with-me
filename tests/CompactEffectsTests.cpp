#include "FWM/BearRoar.h"
#include "FWM/Bastion.h"
#include "FWM/DragonBreath.h"
#include "FWM/MoonFx.h"
#include "FWM/WolfBite.h"
#include "FWM/ArrowRain.h"
#include <cmath>
#include <cstdio>
int main() {
    int failures=0;
    const auto check=[&](bool ok,const char* message){if(!ok){++failures;std::printf("FAIL %s\n",message);}};
    using namespace FWM;
    for(int rank=1;rank<=3;++rank) {
        const bool grand=rank==3;
        for(int i=0;i<240;++i) {
            const float age=static_cast<float>(i)/60;
            const auto d=DragonBreath::At(age,rank,DragonBreath::Duration(rank),true);
            const auto old=DragonBreath::At(age,rank,DragonBreath::Duration(rank));
            check(d.scale<old.scale*.4f&&d.forward<160&&d.height<130,"compact dragon fits directly ahead at actor height");
            check(d.alpha==old.alpha&&d.stream==old.stream&&d.jaw==old.jaw,"sizing preserves dragon timing and mouth articulation");
            const float openFloor=d.height-(grand?32.22402f:56.69074f)*d.scale;
            check(openFloor>=25,"open jaw retains ground clearance");
            const auto b=BearRoar::At(age,grand,true),cat=BearRoar::WarCryAt(age,grand,true);
            check(b.scale<=BearRoar::At(age,grand).scale*.54f,"bear is substantially smaller");
            check(cat.scale<=BearRoar::WarCryAt(age,grand).scale*.54f,"sabercat is substantially smaller");
            const auto original=WolfBite::At(age,0,{},70,grand),wolf=WolfBite::Display(original,0,grand,true);
            check(WolfBite::Lowest(wolf)>=WolfBite::kClearance-.01f,"compact wolf remains above the floor");
            const auto bite=WolfBite::Place(original,WolfBite::kGrip),small=WolfBite::Place(wolf,WolfBite::kGrip);
            check(std::hypot(bite.x-small.x,bite.y-small.y)<.01f,"wolf keeps its bite aligned with target");
            const auto castle=Bastion::At(age,8,rank,true),large=Bastion::At(age,8,rank);
            check(castle.parts[0].scale<=large.parts[0].scale*.46f,"castle reduced");
            check(castle.parts[4].scale==large.parts[4].scale&&castle.parts[4].alpha==large.parts[4].alpha,"guardian ward not shrunk");
            const auto moon=MoonFx::At(age,rank,0,true),full=MoonFx::At(age,rank);
            check(moon.parts[0].scale<full.parts[0].scale*.43f,"moon apparition reduced");
            check(moon.parts[4].scale==full.parts[4].scale&&moon.parts[6].scale==full.parts[6].scale,"blessing wave and recipient aura unchanged");
        }
    }
    check(DragonBreath::At(1,3,3,true).scale>DragonBreath::At(1,2,3,true).scale*1.5f,"Alduin remains distinct from blue dragon");
    for(int i=0;i<72;++i){const auto a=ArrowRain::DemoArrow(i,72,1.5f);
        check(a.from.y==ArrowRain::kPortalForward&&a.from.z>60&&a.from.z<140,"arrows originate within forward portal");
        check(a.to.y>a.from.y&&a.lands-a.loosed<.1f,"fast forward flight, no overhead rain");
        if(i)check(a.loosed-ArrowRain::DemoArrow(i-1,72,1.5f).loosed<.022f,"dense arrow cadence");
    }
    std::printf("Compact summons and portal barrage: %d failures\n",failures);return failures?1:0;
}
