#include "FWM/DragonBreath.h"
#include <cstdio>
#include <limits>
int main() {
    using namespace FWM::DragonBreath;
    int failures=0;
    const auto check=[&](bool good,const char* message){if(!good){++failures;std::printf("FAIL: %s\n",message);}};
    for(int rank=1;rank<=3;++rank) {
        const float duration=Duration(rank);
        check(At(.35f,rank,duration).jaw>0 && At(.35f,rank,duration).stream==0,"jaw is shut before the release");
        const auto release=At(kRelease,rank,duration);
        check(release.pitch>.05f && release.jaw+release.pitch<-.49f,"skull lifts and lower jaw drops together before release");
        check(release.muzzleY<5,"emitter remains inside mouth rather than ahead of snout");
        check(At(.3f,rank,duration).forward<At(0,rank,duration).forward,"head draws back before release");
        check(At(kRelease+duration+kFade,rank,duration).alpha==0,"head fades completely");
        check(At(kRelease+duration+kFade,rank,duration).runeAlpha==0,"summoning rune fades with the head");
        check(At(2,rank,duration).runeAngle>At(1,rank,duration).runeAngle,"spell rune rotates during the cast");
        check(At(kRelease+duration+.26f,rank,duration).stream==0,"stream ends before head fades");
        for(int fps:{30,60,144}) {
            float seconds=0;
            for(int frame=0;frame<fps*8;frame++) seconds+=ActiveTime(float(frame)/fps,float(frame+1)/fps,duration);
            check(std::abs(seconds-duration)<.0001f,"frame rate cannot change channel damage");
        }
        check(ActiveTime(0,kRelease,duration)==0 && ActiveTime(kRelease+duration,10,duration)==0,"no windup or fade damage");
        check(Contains(Reach(rank),30,Reach(rank)) && !Contains(Reach(rank)+1,0,Reach(rank)),"reach boundary matches rank");
        check(Contains(300,50,Reach(rank),1,rank) && !Contains(300,Radius(rank,Reach(rank))+26,Reach(rank),1,rank),"beam hit width follows the visible rank");
        check(!Contains(-1,0,Reach(rank)) && !Contains(Reach(rank)*.8f,0,Reach(rank),.5f),"cannot hit behind caster or beyond growing stream");
        check(Reach(rank,3)>Reach(rank) && Duration(rank,3)==duration+1.5f,"branches extend reach and duration");
    }
    check(Radius(3,Reach(3))>=2*Radius(1,Reach(1)),"rank III beam diameter at least doubles rank I");
    check(!SafeDistance(139.99f)&&SafeDistance(140)&&SafeDistance(300),"no stationary cast within two metres");
    check(BeamProfile(.25f)==BeamProfile(.9f),"beam stays broad throughout its reach");
    check(JetColor(3,0,0)[2]>JetColor(3,0,0)[0]&&JetColor(3,0,7)[0]>JetColor(3,0,7)[2],"final fire has blue centre and orange exterior");
    check(JetColor(3,1,0)[0]>JetColor(2,1,0)[0],"final frost gains a colder white core");
    for(int i=0;i<4;i++) {
        check(GroundAt(0,i,3.2f).alpha==0&&GroundAt(kRelease+3.2f,i,3.2f).alpha==0,"ground forks only occur during channel");
        const auto g=GroundAt(kRelease+.22f+i*.42f+.1f,i,3.2f);
        check(g.alpha>.9f&&g.fraction>.2f&&g.fraction<.8f&&std::abs(g.side)>1,"occasional forks reach outside stream toward terrain");
    }
    for(int i=0;i<64;i++) {
        const auto a=ArcAt(1.f,i),b=ArcAt(1.08f,i);
        check(std::abs(a.angle-b.angle)+std::abs(a.x-b.x)+std::abs(a.z-b.z)>.01f,"electrical arcs reshape instead of only flickering");
        check(a.alpha>=0 && a.alpha<=1 && std::isfinite(a.angle),"bounded arc motion");
    }
    check(Reach(3)>Reach(2) && Reach(2)>Reach(1),"each rank reaches farther");
    check(std::abs(Damage(10,1,0,100)-1.5f)<.0001f && std::abs(Damage(10,1,100,0)-1.5f)<.0001f,"resistances count up to 85%, as for Elemental Burst");
    check(std::abs(Damage(10,1,100,100)-.225f)<.0001f && Damage(10,1,85,0)==Damage(10,1,300,0),"both resistances together still let some through");
    for(int rank=1;rank<=3;++rank) {
        const float reach=Reach(rank,3), mouth=TargetReach(rank,3,true)-reach;
        check(UnderHead(mouth*.5f,0,mouth,rank,reach) && UnderHead(0,Radius(rank,reach)*.5f,mouth,rank,reach),"enemies under the head take the stream");
        check(!UnderHead(-1,0,mouth,rank,reach) && !UnderHead(mouth+1,0,mouth,rank,reach),"the head covers only the ground between caster and mouth");
        check(!UnderHead(mouth*.5f,Radius(rank,reach)*.5f+26,mouth,rank,reach),"the head is narrower than the stream");
    }
    check(std::abs(Turn(0,1,.5f)-kTurnRate*.5f)<.0001f && Turn(0,.1f,1)==.1f,"the head turns at most its rate and stops on its target");
    check(Turn(3.0f,-3.0f,1)>3.0f,"the head turns the short way round");
    check(Turn(.5f,2,0)==.5f && Turn(.5f,std::numeric_limits<float>::quiet_NaN(),1)==.5f,"no turn without time or a heading");
    check(Damage(10,1,50,50)==2.5f && Damage(10,1,0,-50)==15,"resistance and weakness affect damage");
    check(Damage(10,0,0,0)==0 && Damage(10,-1,0,0)==0,"nonpositive time cannot deal damage");
    const float nan=std::numeric_limits<float>::quiet_NaN();
    check(Damage(10,1,nan,0)==0 && ActiveTime(nan,1,3)==0 && !Contains(nan,0,600),"invalid inputs cannot deal damage");
    std::printf("Dragon breath timing, reach and resistance: %d failures\n",failures);
    return failures?1:0;
}
