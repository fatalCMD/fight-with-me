#include "FWM/WindstepFx.h"
#include <cstdio>
int main(){using namespace FWM::WindstepFx;
 std::printf("{\"fps\":60,\"seconds\":%.4f,\"names\":[",kVisual);
 for(std::size_t i=0;i<kParts;++i)std::printf("%s\"%s\"",i?",":"",Name(i).c_str());std::printf("],\"ranks\":[");
 for(int r=1;r<=3;++r){std::printf("%s[",r>1?",":"");
  for(int t=0;t<=static_cast<int>(std::ceil(kVisual*60));++t){const auto frame=At(static_cast<float>(t)/60,r);std::printf("%s{\"light\":0,\"parts\":[",t?",":"");
   for(std::size_t i=0;i<kParts;++i){const auto& p=frame.parts[i];std::printf("%s[%.4f,%.4f,%.4f,%.4f,%.4f,0,%.4f,%.4f,%.4f,%.4f,%.4f]",i?",":"",p.at[0],p.at[1],p.at[2],p.scale,p.turn,p.alpha,p.color[0],p.color[1],p.color[2],p.drift);}std::printf("]}");
  }std::printf("]");
 }std::printf("]}");
}
