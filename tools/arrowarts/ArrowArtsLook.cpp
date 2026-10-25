#include "FWM/ArrowArtsFx.h"
#include <cstdio>
using namespace FWM::ArrowArtsFx;
int main(){
 std::printf("{\"fps\":60,\"names\":[");for(std::size_t i=0;i<kParts;++i)std::printf("%s\"Part%02zu\"",i?",":"",i);std::printf("],\"kinds\":[");
 for(int kind=0;kind<2;++kind){if(kind)std::printf(",");std::printf("[");
 for(int rank=1;rank<=3;++rank){if(rank>1)std::printf(",");std::printf("[");
 for(int frame=0;frame<=180;++frame){if(frame)std::printf(",");const float age=static_cast<float>(frame)/60;const auto look=At(static_cast<Kind>(kind),age,rank,age>=1.65f?age-1.65f:-1);
 std::printf("{\"light\":%.5f,\"parts\":[",look.light);for(std::size_t i=0;i<kParts;++i){if(i)std::printf(",");const auto& p=look.parts[i];std::printf("[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f]",p.at[0],p.at[1],p.at[2],p.scale,p.alpha,p.turn,p.color[0],p.color[1],p.color[2]);}std::printf("]}");}std::printf("]");}std::printf("]");}std::printf("]}");
}
