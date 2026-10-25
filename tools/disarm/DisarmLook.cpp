#include "FWM/DisarmFx.h"
#include <cstdio>
using namespace FWM::DisarmFx;
int main(){
 std::printf("{\"fps\":120,\"seconds\":%.3f,\"names\":[\"Flash\",\"Slash0\",\"Slash1\"",kSeconds);
 for(std::size_t i=0;i<kSparks;++i)std::printf(",\"Spark%02zu\"",i);
 std::printf("],\"ranks\":[");
 for(int rank=1;rank<=3;++rank){if(rank>1)std::printf(",");std::printf("[");
 for(int frame=0;frame<=102;++frame){if(frame)std::printf(",");const auto look=At(static_cast<float>(frame)/120,rank);
 std::printf("{\"light\":%.5f,\"parts\":[",look.light);
 for(std::size_t i=0;i<kParts;++i){if(i)std::printf(",");const auto& p=look.parts[i];std::printf("[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f]",p.at[0],p.at[1],p.at[2],p.scale,p.alpha,p.color[0],p.color[1],p.color[2]);}std::printf("]}");}std::printf("]");}std::printf("]}");
}
