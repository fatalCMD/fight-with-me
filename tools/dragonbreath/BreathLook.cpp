#include "FWM/DragonBreath.h"
#include <cstdio>
int main(int argc, char**) {
    const bool compact=argc>1;
    using namespace FWM::DragonBreath;
    std::printf("{\"fps\":60,\"release\":%.4f,\"ranks\":[",kRelease);
    for(int rank=1;rank<=3;rank++) {
        const float duration=Duration(rank), total=kRelease+duration+kFade;
        std::printf("%s{\"duration\":%.4f,\"seconds\":%.4f,\"reach\":%.4f,\"dps\":%.4f,\"radius\":%.4f,\"frames\":[",rank>1?",":"",duration,total,Reach(rank),kDPS[Tier(rank)],Radius(rank,Reach(rank))*StreamWidth(rank,compact));
        for(int i=0;i<=int(std::ceil(total*60));i++) {
            const float t=float(i)/60;const auto p=At(t,rank,duration,compact);
            std::printf("%s{\"t\":%.4f,\"a\":%.4f,\"jaw\":%.4f,\"stream\":%.4f,\"front\":%.4f,\"s\":%.4f,\"height\":%.4f,\"forward\":%.4f,\"muzzleY\":%.4f,\"muzzleZ\":%.4f,\"pitch\":%.4f,\"runeAngle\":%.4f,\"runeRadius\":%.4f,\"runeAlpha\":%.4f,\"arcs\":[",i?",":"",t,p.alpha,p.jaw,p.stream,p.front,p.scale,p.height,p.forward,p.muzzleY,p.muzzleZ,p.pitch,p.runeAngle,p.runeRadius,p.runeAlpha);
            for(int arc=0;arc<64;arc++){const auto q=ArcAt(t,arc);std::printf("%s[%.4f,%.4f,%.4f,%.4f]",arc?",":"",q.angle,q.x,q.z,q.alpha);}
            std::printf("],\"ground\":[");
            for(int g=0;g<4;g++){const auto q=GroundAt(t,g,duration);std::printf("%s[%.4f,%.4f,%.4f,%d]",g?",":"",q.alpha,q.fraction,q.side,q.event);}
            std::printf("]}");
        }
        std::printf("]}");
    }
    std::printf("]}");
}
