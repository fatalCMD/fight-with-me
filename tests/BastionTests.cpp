#include "FWM/Bastion.h"
#include <cmath>
#include <cstdio>
#include <limits>
int main()
{
	using namespace FWM::Bastion;
	int failures=0;
	auto check=[&](bool good,const char* why){if(!good){++failures;std::printf("FAIL: %s\n",why);}};
	for(int rank=1;rank<=3;++rank) {
		for(float age:{-1.0f,0.0f,8+kFade,20.0f,std::numeric_limits<float>::quiet_NaN()}) {
			const auto f=At(age,8,rank);for(const auto& p:f.parts)check(p.alpha==0,"no effect outside lifetime");
		}
		check(At(.6f,8,rank).parts[0].height>100,"tower forms above ground");
		check(std::abs(At(kImpact,8,rank).parts[0].height)<.01f,"tower strikes ground at impact");
		check(At(kImpact-.01f,8,rank).parts[2].alpha==0,"shockwave waits for impact");
		check(At(kImpact,8,rank).parts[2].alpha>.5f,"impact launches shockwave");
		check(At(kStoneGone,8,rank).parts[0].alpha==0,"architecture disappears before ward expires");
		check(At(6,8,rank).parts[4].alpha>.1f,"ward remains after architecture");
		check(At(8.4f,8,rank).parts[4].alpha<At(7,8,rank).parts[4].alpha,"ward fades at expiry");
		check(Shake(0,rank)>0 && Shake(0,rank)<=.20f,"landing shake stays small at closest range");
		check(Shake(800,rank)<Shake(400,rank) && Shake(400,rank)<Shake(0,rank),"shake weakens with distance");
		for(float distance:{-1.0f,kShakeReach,5000.0f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
			check(Shake(distance,rank)==0,"distant or invalid locations cannot shake the camera");
		for(int i=0;i<1300;++i)for(const auto& p:At(i/144.0f,8,rank).parts)check(std::isfinite(p.scale)&&p.scale>0&&std::isfinite(p.height)&&std::isfinite(p.alpha)&&p.alpha>=0&&p.alpha<=1,"finite bounded pose");
	}
	const auto red=At(2,8,3).parts[0].color;
	check(red[0]>red[1]*8&&red[0]>red[2]*8,"rank III remains saturated red");
	std::printf("Last Bastion: %s\n",failures?"FAILED":"passed");return failures?1:0;
}
