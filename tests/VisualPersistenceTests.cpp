#include "FWM/HealCircle.h"
#include "FWM/Rejuvenation.h"
#include "FWM/ChaosPulse.h"
#include "FWM/WolfBite.h"
#include <cmath>
#include <cstdio>
#include <limits>

int main()
{
    int failures=0;
    auto check=[&](bool pass,const char* why){if(!pass){++failures;std::printf("FAIL %s\n",why);}};
    // Exercise the actual buff lengths, including the old twelve-second cutoff.
    for (int rank=1;rank<=3;++rank) {
        const float duration=std::array{30.0f,60.0f,120.0f}[rank-1];
        for (float age=1;age<duration-2.8f;age+=.1f) {
            const auto f=FWM::Rejuvenation::At(age,rank,duration);
            check(f.parts[0].alpha>=.83f,"rejuvenation seal stays readable for the whole buff");
            float edges=0,particles=0;
            for(std::size_t i=6;i<9;++i)edges+=f.parts[i].alpha;
            for(const auto& mote:f.motes)particles+=mote.alpha;
            check(edges>.15f&&particles>1,"edges and particles continue throughout long buffs");
        }
        check(FWM::Rejuvenation::At(duration-1,rank,duration).parts[0].alpha < .4f,"gradual ending fade");
        check(FWM::Rejuvenation::At(duration-.02f,rank,duration).parts[0].alpha < .001f,"no visible pop at expiry");
        for(const auto& p:FWM::Rejuvenation::At(duration,rank,duration).parts)check(p.alpha==0,"no seal after buff expiry");
    }
    for(bool grand:{false,true}) {
        for(float age=1;age<9.5f;age+=.1f) {
            const auto f=FWM::HealCircle::At(age,12,grand);
            check(f.ring>.99f,"healing seal holds before final fade");
            check(f.edgeAlpha[0]+f.edgeAlpha[1]>.02f,"healing rim pulses overlap continuously");
        }
        check(FWM::HealCircle::At(10.8f,12,grand).ring>.4f,"healing fade spans multiple seconds");
        check(FWM::HealCircle::At(11.99f,12,grand).ring<.001f,"healing fades smoothly to zero");
        for(float duration:{8.0f,14.0f}) {
            for(float age=1;age<duration-2.4f;age+=.1f) {
                const auto f=FWM::ChaosPulse::At(age,grand,duration);
                check(f.parts[2].alpha>.7f&&f.parts[4].alpha>.45f,"chaos rune persists after the apparition disappears");
                if(age>4.6f)check(f.spirit.alpha==0,"apparition finishes while rune persists");
            }
            check(FWM::ChaosPulse::At(duration-.01f,grand,duration).parts[2].alpha<.001f,"chaos rune fades with slow expiry");
        }
    }
    for(float age=0;age<3.5f;age+=.01f) {
        const auto collision=FWM::WolfBite::At(age,0,{},70,true);
        const auto shown=FWM::WolfBite::Display(collision,0,true);
        check(shown.scale>collision.scale*1.25f,"final wolf silhouette is larger without enlarging its bite collision");
        check(FWM::WolfBite::Lowest(shown)>=FWM::WolfBite::kClearance-.01f,"enlarged wolf clears the ground");
    }
    std::printf("Visual persistence: %s\n",failures?"FAILED":"passed");return failures?1:0;
}
