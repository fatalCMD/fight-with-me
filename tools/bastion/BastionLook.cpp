#include "FWM/Bastion.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv)
{
	using namespace FWM::Bastion;
	const float input = argc > 1 ? std::strtof(argv[1], nullptr) : 8;
	const float duration = std::isfinite(input) ? std::clamp(input, 2.0f, 60.0f) : 8;
	std::printf("{\"fps\":60,\"duration\":%.4f,\"seconds\":%.4f,\"impact\":%.4f,\"stoneGone\":%.4f,", duration, duration + kFade, kImpact, kStoneGone);
	std::printf("\"shake\":{\"seconds\":%.4f,\"strength\":[%.4f,%.4f,%.4f]},\"audio\":[", kShakeSeconds, Shake(0,1), Shake(0,2), Shake(0,3));
	for (std::size_t i=0;i<kSounds.size();++i) {
		const auto& s=kSounds[i];
		std::printf("%s{\"form\":%u,\"grandForm\":%u,\"at\":%.4f,\"seconds\":%.4f,\"volume\":[%.4f,%.4f,%.4f]}",i?",":"",s.form,s.grandForm,s.at,s.seconds,s.volume[0],s.volume[1],s.volume[2]);
	}
	std::printf("],\"names\":[");
	for (std::size_t i=0;i<kNames.size();++i) std::printf("%s\"%s\"",i?",":"",kNames[i]);
	std::printf("],\"ranks\":[");
	for(int rank=1;rank<=3;++rank) {
		std::printf("%s[",rank>1?",":"");
		for(int i=0;i<=static_cast<int>(std::ceil((duration+kFade)*60));++i) {
			const auto f=At(i/60.0f,duration,rank,argc>2);
			std::printf("%s{\"light\":%.4f,\"parts\":[",i?",":"",f.light);
			for(std::size_t j=0;j<f.parts.size();++j) { const auto& p=f.parts[j]; std::printf("%s[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f]",j?",":"",p.scale,p.height,p.turn,p.alpha,p.color[0],p.color[1],p.color[2]); }
			std::printf("]}");
		}
		std::printf("]");
	}
	std::printf("]}\n");
}
