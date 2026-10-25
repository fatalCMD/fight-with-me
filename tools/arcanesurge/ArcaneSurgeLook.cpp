#include "FWM/ArcaneSurge.h"
#include <cmath>
#include <cstdio>
int main(){
 using namespace FWM::ArcaneSurge;
 std::printf("{\"fps\":60,\"duration\":12,\"seconds\":%.4f,\"names\":[",12+kFade);
 for(std::size_t i=0;i<kParts;++i)std::printf("%s\"%s\"",i?",":"",Name(i).c_str());
 std::printf("],\"ranks\":[");
 for(int rank=1;rank<=3;++rank){
  std::printf("%s[",rank>1?",":"");
  for(int t=0;t<=static_cast<int>(std::ceil((12+kFade)*60));++t){
   const auto f=At(static_cast<float>(t)/60,12,rank);
   std::printf("%s{\"light\":%.4f,\"parts\":[",t?",":"",f.light);
   for(std::size_t i=0;i<kParts;++i){const auto& p=f.parts[i];
    std::printf("%s[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f]",i?",":"",p.at[0],p.at[1],p.at[2],p.scale,p.turn,p.tilt,p.alpha,p.color[0],p.color[1],p.color[2],p.drift);
   }
   std::printf("]}");
  }
  std::printf("]");
 }
 std::printf("]}\n");
}
