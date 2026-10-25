#include "FWM/SupportArt.h"
#include <array>
#include <cstdio>
int main()
{
    using namespace FWM::SupportArt;
    constexpr std::array names{"Guard","Vitality","Focus","Mark","Binding","Surge","Rally","Wind","Impact","LevelUp","FireSword","SteelSword","SpiritSword"};
    std::printf("{\"fps\":30,\"effects\":{");
    for(std::size_t k=0;k<names.size();++k) {
        const auto kind=static_cast<Kind>(k);
        const float duration=kind==Kind::Impact?.8f:(kind==Kind::Wind?1.4f:(kind==Kind::LevelUp?3.2f:(kind==Kind::Surge?12.0f:8.0f)));
        std::printf("%s\"%s\":{\"duration\":%.3f,\"frames\":[",k?",":"",names[k],duration);
        const int count=static_cast<int>(std::round(duration*30));
        for(int n=0;n<=count;++n) {
            const float age=static_cast<float>(n)/30;
            const auto f=At(age,duration-age,kind);
            std::printf("%s{\"alpha\":%.5f,\"turn\":%.5f,\"drift\":%.5f,\"scale\":%.5f,\"coreTurn\":%.5f,\"coreScale\":%.5f,\"coreDrift\":%.5f,\"motes\":[",
                n?",":"",f.alpha,f.turn,f.drift,f.scale,f.coreTurn,f.coreScale,f.coreDrift);
            for(std::size_t i=0;i<MoteCount(kind);++i) {
                const auto m=Particle(age,duration-age,kind,i);
                std::printf("%s[%.5f,%.5f,%.5f,%.5f]",i?",":"",m.turn,m.rise,m.alpha,m.scale);
            }
            std::printf("]}");
        }
        std::printf("]}");
    }
    std::printf("}}\n");
}
