#include "FWM/Rejuvenation.h"
#include <cstdio>

int main() {
	using namespace FWM::Rejuvenation;
	std::printf("{\"seconds\":%.3f,\"fps\":30,\"names\":[", kSeconds);
	for (std::size_t i=0;i<kNames.size();++i) std::printf("%s\"%s\"",i?",":"",kNames[i]);
	std::printf("],\"ranks\":[");
	for(int rank=1;rank<=3;++rank){
		const float seconds=std::array{8.0f,10.0f,12.0f}[static_cast<std::size_t>(rank-1)];
		std::printf("%s{\"seconds\":%.3f,\"radius\":%.3f,\"frames\":[",rank>1?",":"",seconds,At(1,rank,seconds).radius);
		for(int i=0;i<=static_cast<int>(seconds*30);++i){
			const auto f=At(static_cast<float>(i)/30.0f,rank,seconds);
			std::printf("%s{\"light\":%.4f,\"lightColor\":[%.3f,%.3f,%.3f],\"parts\":[",i?",":"",f.light,f.lightColor[0],f.lightColor[1],f.lightColor[2]);
			for(std::size_t j=0;j<f.parts.size();++j){const auto& p=f.parts[j];std::printf("%s[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f]",j?",":"",p.scale,p.turn,p.height,p.alpha,p.color[0],p.color[1],p.color[2]);}
			std::printf("],\"motes\":[");
			for(std::size_t j=0;j<f.motes.size();++j){const auto& p=f.motes[j];std::printf("%s[%.3f,%.3f,%.3f,%.4f,%.4f,%.3f,%.3f,%.3f]",j?",":"",p.at[0],p.at[1],p.at[2],p.size,p.alpha,p.color[0],p.color[1],p.color[2]);}
			std::printf("]}");
		}
		std::printf("]}");
	}
	std::printf("]}\n");
}
