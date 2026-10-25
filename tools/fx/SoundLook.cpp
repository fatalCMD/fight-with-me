#include "FWM/Soundscape.h"
#include <cstdio>
int main()
{
	using namespace FWM::Soundscape;
	std::printf("{");
	for(int kind=0;kind<static_cast<int>(Kind::Count);++kind){
		std::printf("%s\"%s\":[",kind?",":"",kNames[kind].data());
		for(int rank=1;rank<=3;++rank){
			std::printf("%s[",rank>1?",":"");bool first=true;
			for(const auto& cue:Plan(static_cast<Kind>(kind),rank)){
				std::printf("%s{\"form\":%u,\"at\":%.4f,\"seconds\":%.4f,\"volume\":%.4f,\"loop\":%s,\"pitch\":%.4f,\"channel\":%s}",first?"":",",cue.form,cue.at,cue.seconds,cue.volume,cue.loop?"true":"false",cue.pitch,cue.channel?"true":"false");first=false;
			}
			std::printf("]");
		}
		std::printf("]");
	}
	std::printf("}\n");
}
