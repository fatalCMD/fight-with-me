#include "FWM/Soundscape.h"
#include "FWM/Bastion.h"
#include <cmath>
#include <cstdio>
#include <limits>
int main()
{
	using namespace FWM::Soundscape;int failures=0;
	auto check=[&](bool pass,const char* why){if(!pass){++failures;std::printf("FAIL %s\n",why);}};
	for(int kind=0;kind<static_cast<int>(Kind::Count);++kind)for(int rank=1;rank<=3;++rank){
		const auto plan=Plan(static_cast<Kind>(kind),rank);check(!plan.empty(),"each sound family has cues");
		for(const auto& cue:plan){
			check(cue.form&&cue.volume>0&&cue.volume<=1,"valid bounded cue");
			check(std::isfinite(cue.pitch)&&cue.pitch>=.6f&&cue.pitch<=1.2f,"bounded pitch");
			if(cue.loop&&!cue.channel)check(cue.volume<=.06f,"persistent beds stay quiet");
			for(float duration:{2.5f,8.0f,18.0f}){
				check(Gain(cue,-1,duration)==0&&Gain(cue,duration,duration)==0,"no audio outside lifetime");
				check(Gain(cue,std::numeric_limits<float>::quiet_NaN(),duration)==0,"invalid time is silent");
				for(int step=0;step<400;++step){const float gain=Gain(cue,step*.05f,duration);check(std::isfinite(gain)&&gain>=0&&gain<=cue.volume,"envelope remains bounded");}
			}
			if(cue.loop){check(Gain(cue,Start(cue,8),8)==0,"loop fades in");check(Gain(cue,7.95f,8)<Gain(cue,6,8),"loop fades out");}
		}
	}
    for(int element=0;element<3;element++) {
        const auto plan=Plan(Kind::DragonBreath,3,element);
        int channels=0;
        for(const auto& cue:plan)if(cue.channel) {
            channels++;
            check(cue.loop&&cue.seconds==0,"breath sound persists through full channel");
            check(Gain(cue,4.7f,5.f)>.3f,"extended breath keeps full sound until ending fade");
            check(cue.form==(element==0?0x03EF56u:element==1?0x0DAB87u:0x03F205u),"element selects matching native loop");
        }
        check(channels==1,"one sustained elemental sound per breath");
    }
    for(int rank=1;rank<=3;++rank){
        const auto wind=Plan(Kind::SecondWind,rank);
        for(const auto& cue:wind)check(cue.form==0x0A0D90&&!cue.loop,"Second Wind uses short wind gusts only");
        check(wind.size()==static_cast<std::size_t>(rank==3?2:1),"rank III has a second wind gust");
        if(rank==3)check(std::abs(wind[1].at-.49f)<.001f,"wind echo matches the visual pulse");
    }
	for(bool loop:{false,true}){
		check(DistanceGain(0,loop)==1&&DistanceGain(4000,loop)==0,"near and distant mix");
		check(DistanceGain(800,loop)<DistanceGain(300,loop),"distance reduces sound");
	}
	for(const auto& cue:FWM::Bastion::kSounds)check(cue.form!=cue.grandForm,"crimson fortress uses different recordings");
	for(const auto& cue:Plan(Kind::Wolf,3))check(cue.form!=0x0D8D4B,"wolf uses a threatening voice instead of the old thin howl");
	for(int k=0;k<static_cast<int>(Kind::Count);++k){
		const auto kind=static_cast<Kind>(k);
		if(kind==Kind::Arrow||kind==Kind::Impact||kind==Kind::Volley||kind==Kind::WindstepBurst)continue;
        if(kind==Kind::Windstep){
            check(Plan(kind,3)[0].volume>Plan(kind,1)[0].volume,"Windstep III uses a stronger retreat gust");
            for(const auto& cue:Plan(Kind::WindstepBurst,3))check(cue.form==0x0A0D90&&!cue.loop,"Windstep III burst is wind, not a magic tone");continue;
        }
        if(kind==Kind::Riposte||kind==Kind::RiposteImpact){
            check(Plan(kind,3)[0].volume>Plan(kind,1)[0].volume,"final parry and landing are stronger physical sounds");continue;
        }
		check(Plan(kind,3).size()>Plan(kind,1).size(),"final ranks have an additional short sound accent");
	}
	std::printf("Soundscape: %s\n",failures?"FAILED":"passed");return failures?1:0;
}
