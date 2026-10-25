#include "FWM/SupportArt.h"
#include <cstdio>
#include <limits>
int main()
{
    int failures=0;
    auto check=[&](bool ok,const char* why){if(!ok){++failures;std::printf("FAIL %s\n",why);}};
    using namespace FWM::SupportArt;
    for(int k=0;k<=static_cast<int>(Kind::Spirit);++k) {
        const auto kind=static_cast<Kind>(k);
        check(At(0,10,kind).alpha==0,"starts dark");
        check(At(10,0,kind).alpha==0,"expires dark");
        check(At(-1,1,kind).alpha==0,"reject negative age");
        check(At(std::numeric_limits<float>::quiet_NaN(),1,kind).alpha==0,"reject invalid age");
        for(float age=.3f;age<119.4f;age+=.013f) {
            const auto f=At(age,120-age,kind),next=At(age+.001f,120-age-.001f,kind);
            check(std::isfinite(f.alpha)&&f.alpha>=0&&f.alpha<=1.001f,"finite bounded light");
            check(std::abs(next.alpha-f.alpha)<.01f,"smooth light");
            if(kind!=Kind::Impact&&kind!=Kind::Wind)check(f.alpha>.60f,"buff persists for actual duration");
            if(Weapon(kind))check(f.turn==0&&f.coreTurn==0&&f.scale==1&&f.coreScale==1,"weapon energy stays aligned");
            for(std::size_t i=0;i<MoteCount(kind);++i) {
                const auto m=Particle(age,120-age,kind,i),n=Particle(age+.001f,120-age-.001f,kind,i);
                check(m.alpha>=0&&m.alpha<=1.001f,"particle light bounded");
                if(std::abs(n.rise-m.rise)>1)check(m.alpha<.001f&&n.alpha<.001f,"particle wraps only while invisible");
                check(std::abs(n.alpha-m.alpha)<.02f,"particle fade continuous");
            }
        }
        check(At(9.999f,.001f,kind).alpha<.0001f,"smooth final fade");
    }
    check(At(.4f,1,Kind::Wind).scale>At(.1f,1,Kind::Wind).scale,"wind expands");
    check(At(.7f,1,Kind::Binding).coreScale<At(.1f,1,Kind::Binding).coreScale,"bindings tighten");
    check(At(1,1,Kind::Surge).coreTurn*At(1,1,Kind::Surge).turn<0,"surge layers counterrotate");
    check(MoteCount(Kind::Fire)==24,"fire has embers and independently rising flame puffs");
    std::printf("Support motion, persistence and weapon alignment: %s\n",failures?"FAILED":"passed");
    return failures?1:0;
}
