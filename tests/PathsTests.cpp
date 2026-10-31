#include "FWM/SkaldPolicy.h"
#include "FWM/SkaldMusic.h"
#include "FWM/Paths.h"
#include "FWM/DebugAbility.h"
#include "FWM/Archery.h"
#include "FWM/ChaosDown.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <set>

namespace
{
	int checks = 0;
	int failures = 0;

	void Check(bool a_ok, const char* a_what, int a_line)
	{
		++checks;
		if (!a_ok) {
			++failures;
			std::printf("FAIL line %d: %s\n", a_line, a_what);
		}
	}

	bool Near(float a, float b, float eps = 0.001f) { return std::fabs(a - b) <= eps; }

#define CHECK(x) Check((x), #x, __LINE__)

	using namespace FWM;
	using namespace FWM::Paths;

	std::size_t At(Role a_role, Perk a_perk) { return static_cast<std::size_t>(IndexOf(a_role, a_perk)); }

    void SkaldProgression()
    {
        CHECK(SkaldLack(false,false)==Lack::kLute);
        CHECK(SkaldLack(false,true)==Lack::kLute);
        CHECK(SkaldLack(true,false)==Lack::kSkaldLocked);
        CHECK(SkaldLack(true,true)==Lack::kNothing);
        static_assert(static_cast<int>(Role::kSkald)==6); // Existing six saved role numbers stay fixed.
        static_assert(kAbilityCount<32); // The manual-use bit mask still holds all songs.
        Book book;
        book.Meet(42,19); // Nine points above level ten.
        CHECK(book.UnlockSkald(42)==Block::kNoPoints);
        CHECK(!book.Find(42)->skaldUnlocked && book.Unspent(42)==9);
        CHECK(book.Learn(42,Role::kSkald,0)==Block::kSkaldLocked);
        book.Meet(42,20);
        CHECK(book.UnlockSkald(42)==Block::kNone);
        CHECK(book.Find(42)->skaldUnlocked && book.Unspent(42)==0);
        CHECK(Spent(*book.Find(42),Role::kSkald)==10);
        CHECK(book.UnlockSkald(42)==Block::kMaxed);
        CHECK(book.Learn(42,Role::kSkald,0)==Block::kNoPoints);
        book.Meet(42,50);
        CHECK(book.Learn(42,Role::kSkald,1)==Block::kParent);
        CHECK(book.Learn(42,Role::kSkald,0)==Block::kNone);
        CHECK(book.Learn(42,Role::kSkald,1)==Block::kNone);
        CHECK(book.Learn(42,Role::kSkald,1)==Block::kParent);
        CHECK(book.Learn(42,Role::kSkald,3)==Block::kNone);
        CHECK(book.Learn(42,Role::kSkald,6)==Block::kNone);
        CHECK(book.Learn(42,Role::kSkald,9)==Block::kNone);
        CHECK(book.Equip(42,Role::kSkald,1,Ability::kGlory)==Block::kNone);
        CHECK(book.Find(42)->slots[6][1]==Ability::kGlory);
        const auto bytes=Encode(book.Records());
        const auto loaded=Decode(bytes,kRecordVersion);
        CHECK(loaded.size()==1 && loaded[0].second==*book.Find(42));
        Book after;after.Load(loaded,{});
        CHECK(after.Unspent(42)==book.Unspent(42));
        CHECK(after.SetUnlimited(42,true)==Block::kNone);
        CHECK(after.Reset(42,Role::kSkald)==Block::kNone);
        CHECK(!after.Find(42)->skaldUnlocked);
        CHECK(after.SetUnlimited(42,false)==Block::kNone);
        CHECK(after.Find(42)->skaldUnlocked && after.Unspent(42)==book.Unspent(42));
        CHECK(after.Reset(42,Role::kSkald)==Block::kNone);
        CHECK(!after.Find(42)->skaldUnlocked && after.Unspent(42)==40);
        CHECK(after.Find(42)->slots[6][0]==Ability::kNone);
        // A corrupt unlock tail must never invent a free unlock, or discard earlier valid trees.
        auto cut=bytes;cut.pop_back();
        auto partial=Decode(cut,kRecordVersion);
        CHECK(partial.size()==1);
        CHECK(!partial[0].second.skaldUnlocked && Spent(partial[0].second,Role::kSkald)==0);
        auto insufficient=*book.Find(42);
        CHECK(Repair(insufficient,9));
        CHECK(!insufficient.skaldUnlocked && Spent(insufficient)==0);
        // Actual v4 shape, with six roles and a nonzero existing investment.
        std::vector<std::uint8_t> old;
        const auto put=[&](std::uint32_t n,int count){for(int i=0;i<count;++i)old.push_back(static_cast<std::uint8_t>(n>>(8*i)));};
        put(1,4);put(66,4);put(40,2);put(6,1);
        for(int role=0;role<6;++role){
            put(role==0?static_cast<std::uint32_t>(Ability::kDisarm):0,1);put(0,1);put(role==0?1:0,1);
            if(role==0){put(static_cast<std::uint32_t>(Perk::kDisarm),1);put(2,1);}
        }
        put(0,4);
        const auto legacy=Decode(old,4);
        CHECK(legacy.size()==1 && legacy[0].second.peak==40);
        CHECK(!legacy[0].second.skaldUnlocked && Rank(legacy[0].second,Role::kFighter,Ability::kDisarm)==2);
        CHECK(Spent(legacy[0].second)==3);
        const auto migrated=Decode(Encode(legacy),kRecordVersion);
        CHECK(migrated==legacy);
        // Debug unlocks are free, survive saves during debug, and disappear with debug.
        Book guarded;guarded.Meet(91,10);
        CHECK(guarded.SetUnlimited(91,true)==Block::kNone);
        CHECK(guarded.UnlockSkald(91)==Block::kNone);
        CHECK(guarded.UnlockSkald(91)==Block::kMaxed);
        CHECK(guarded.Learn(91,Role::kSkald,0)==Block::kNone);
        CHECK(guarded.Learn(91,Role::kFighter,0)==Block::kNone);
        CHECK(!guarded.KeptAside()[0].second.skaldUnlocked);
        Book repaired;repaired.Load(Decode(Encode(guarded.Records()),kRecordVersion),Decode(Encode(guarded.KeptAside()),kRecordVersion));
        CHECK(repaired.Unlimited(91) && repaired.Find(91)->skaldUnlocked);
        CHECK(repaired.Find(91)->ranks[6][0]==1);
        CHECK(repaired.SetUnlimited(91,false)==Block::kNone);
        CHECK(!repaired.Find(91)->skaldUnlocked && Spent(*repaired.Find(91),Role::kSkald)==0);
        CHECK(repaired.Unspent(91)==0);
        CHECK(repaired.Learn(91,Role::kSkald,0)==Block::kSkaldLocked);
        CHECK(repaired.UnlockSkald(91)==Block::kNoPoints);
        CHECK(repaired.SetUnlimited(91,true)==Block::kNone);
        CHECK(!repaired.Find(91)->skaldUnlocked); // Enabling debug still needs an explicit unlock.
        CHECK(repaired.SetUnlimited(91,false)==Block::kNone);
        repaired.Meet(91,50);
        CHECK(repaired.Unspent(91)==40);
        CHECK(repaired.UnlockSkald(91)==Block::kNone && repaired.Unspent(91)==30);
        CHECK(repaired.SetUnlimited(91,true)==Block::kNone);
        Book paid;paid.Load(repaired.Records(),repaired.KeptAside());
        CHECK(paid.Find(91)->skaldUnlocked);
        CHECK(paid.Learn(91,Role::kSkald,0)==Block::kNone);
        CHECK(paid.SetUnlimited(91,false)==Block::kNone);
        CHECK(paid.Find(91)->skaldUnlocked && paid.Unspent(91)==30);
        CHECK(paid.Find(91)->ranks[6][0]==0); // Only the earned unlock survives.
    }
    void SkaldLuteEntry()
    {
        using A=SkaldPolicy::LuteEntryAction;
        SkaldPolicy::LuteEntry entry;
        CHECK(entry.Next(0,false,false,false)==A::Sheathe);
        CHECK(entry.Next(.2f,false,true,false)==A::Wait);
        CHECK(entry.Next(3.f,false,true,false)==A::Wait); // slow sheathing is allowed to finish
        CHECK(entry.Next(3.1f,true,false,false)==A::Reset); // clear a stale idle before every request
        CHECK(entry.Next(3.15f,true,false,false)==A::Wait);
        CHECK(entry.Next(3.25f,true,false,false)==A::Idle);
        CHECK(entry.Next(3.8f,true,false,false)==A::Wait); // a request gets about a second to produce its prop
        CHECK(entry.Next(4.3f,true,false,false)==A::Reset);
        CHECK(entry.Next(4.45f,true,false,false)==A::Graph); // attempts alternate the AI idle and the graph event
        CHECK(entry.Next(4.9f,true,false,true)==A::Wait); // never restart an animation that produced its prop
        CHECK(entry.Next(5.f,false,false,false)==A::Sheathe); // the combat AI drew the fists meanwhile
        CHECK(entry.Next(5.2f,true,false,false)==A::Reset);   // a redraw restarts the attempt at once
        CHECK(entry.Next(5.35f,true,false,false)==A::Idle);
        CHECK(entry.Next(SkaldPolicy::kLuteStartup,true,false,false)==A::Timeout);
        // Replay the logged stall: an idle accepted mid-walk stayed current for
        // eight seconds and repeating the request never cleared it.
        SkaldPolicy::LuteEntry stalled;unsigned resets=0,requests=0;
        for(float age=0;age<SkaldPolicy::kLuteStartup;age+=1.f/60.f){
            const auto next=stalled.Next(age,true,false,false);
            resets+=next==A::Reset;requests+=next==A::Idle||next==A::Graph;
            CHECK(next!=A::Timeout);
        }
        CHECK(requests>=4 && resets>=requests && resets<=requests+1); // each attempt cleared first, none every frame
        SkaldPolicy::LuteEntry fast;
        CHECK(fast.Next(0,true,false,false)==A::Reset);
        CHECK(fast.Next(.1f,true,false,false)==A::Idle);
        for(int i=2;i<59;++i)CHECK(fast.Next(i*.1f,true,false,true)==A::Wait);
        CHECK(fast.requests==1);
        using P=SkaldPolicy::PropAction;
        SkaldPolicy::PropRecovery recovery;
        CHECK(recovery.Next(true,.1f)==P::Ready);
        CHECK(recovery.Next(false,.1f)==P::Retry);
        CHECK(recovery.Next(false,.5f)==P::Wait);
        CHECK(recovery.Next(true,.1f)==P::Resume);
        CHECK(recovery.Next(true,.1f)==P::Ready);
        CHECK(recovery.Next(false,.1f)==P::Abort); // one repair, never an endless loop
        SkaldPolicy::PropRecovery missing;
        CHECK(missing.Next(false,0)==P::Retry);
        CHECK(missing.Next(false,1.25f)==P::Abort);
        CHECK(SkaldPolicy::NoteInterval(0)==1.f&&SkaldPolicy::NoteInterval(1)==3.f);
        CHECK(SkaldPolicy::NoteInterval(-1)==1.f&&SkaldPolicy::NoteInterval(2)==3.f);
        CHECK(SkaldPolicy::kNoteSlowSeconds<SkaldPolicy::NoteInterval(.5f)); // audible gaps, not a permanent root
    }
    void SkaldNumbers()
    {
        using namespace FWM::SkaldPolicy;
        CHECK(Near(Speed(1),.1f) && Near(Speed(3),.2f));
        CHECK(Near(Duration(Song::Fervor,3),11.6f));
        CHECK(Near(Duration(Song::Glory,3),14.5f));
        CHECK(Near(CriticalMultiplier(Critical(3)),2.1f));
        CHECK(Near(CriticalMultiplier(Critical(3)*2),2.7f));
        CHECK(Near(Advance(1,Recovery(3)),1.6f));
        CHECK(Near(Advance(1,Recovery(3)*2),2.2f));
        CHECK(Advance(-1,1)==0);
        float shield=30;
        CHECK(Absorb(20,shield)==0 && shield==10);
        CHECK(Absorb(25,shield)==15 && shield==0);
        CHECK(Absorb(3,shield)==3);
        CHECK(Near(DamageShare(20),.2f));
        CHECK(DamageShare(-10)==0 && DamageShare(200)==1);
        CHECK(DamageShare(std::numeric_limits<float>::quiet_NaN())==1);
        for(const auto song:{Song::Fervor,Song::Echoes,Song::Hollow,Song::Glory})CHECK(Cooldown(song)>Duration(song,3));
    }
    void SkaldPositions()
    {
        using namespace FWM::SkaldPolicy;
        // A bard plays where it stands, like a healer: it leaves to rejoin only well
        // past its leash and plays again well inside it, so the edge cannot flap.
        for(const float radius:{400.f,700.f,1000.f}){
            CHECK(!LeaveToRejoin(radius,radius)&&!LeaveToRejoin(radius+kLeashSlack,radius));
            CHECK(LeaveToRejoin(radius+kLeashSlack+1.f,radius));
            CHECK(BackInLeash(radius-kLeashInside,radius)&&!BackInLeash(radius,radius));
            CHECK(!BackInLeash(radius+kLeashSlack+1.f,radius));
        }
        // No leash (Fight With Me's leash switched off): it never leaves to rejoin.
        CHECK(!LeaveToRejoin(5000.f,0.f)&&BackInLeash(5000.f,0.f));
        CHECK(kLeashSlack+kLeashInside>=150.f);
        // Unable to get closer, it plays where it stands for a while rather than freezing.
        CHECK(kRejoinPatience>kFollowStall&&kRejoinRest>kRejoinPatience&&kRejoinProgress>kFollowProgress);
        CHECK(kUnstickInterval>kFollowStall);
        // The lute's swing in from the back is hidden, not the playing itself.
        CHECK(kLiftIn>0.f&&kLiftIn<kLayerSettle+.5f&&kOffPose>0.f&&kOffPose<30.f);
        CHECK(kNoteReach/kNoteSpeed>2.f&&kNoteReach/kNoteSpeed<kNoteLifetime);
        // A held lute sits within arm's reach of the chest; one across the room is a stray.
        CHECK(!Astray(0.f)&&!Astray(80.f)&&!Astray(kLuteReach)&&Astray(kLuteReach+1.f)&&Astray(600.f));
        CHECK(kLuteReach>kOffPose*4.f);
        // A left-over still settling from the exit is not mistaken for one an idle plays again.
        CHECK(!StrayMoving(0.f,40.f)&&!StrayMoving(kStraySettle-.01f,40.f));
        CHECK(!StrayMoving(kStraySettle,kStrayMoved)&&StrayMoving(kStraySettle,kStrayMoved+.5f)&&StrayMoving(10.f,5.f));
        CHECK(kStrayStop>=kStraySettle);
        // A strummed lute barely moves between frames; a swing in from the back does.
        const float frame=1.f/40.f;
        CHECK(!Swinging(1.f,frame)&&!Swinging(kSettledSpeed*frame,frame)&&Swinging(kSettledSpeed*frame+.5f,frame));
        CHECK(!Swinging(50.f,0.f)&&!Swinging(10.f,.2f)&&Swinging(40.f,.2f));
        CHECK(kOverlayRetry>.5f&&kOverlayRetry<3.f);
        // The spectral lute fades in once the hands have it, and a finished song's
        // lute waits only a moment for the walk to take it.
        CHECK(Appear(-1.f)==0.f&&Appear(0.f)==0.f&&Appear(kLuteAppear)==1.f&&Appear(100.f)==1.f);
        CHECK(Appear(kLuteAppear*.5f)>.4f&&Appear(kLuteAppear*.5f)<.6f);
        CHECK(kSpareFor>.1f&&kSpareFor<kOverlayRetry);
    }
    void SkaldWaves()
    {
        using namespace FWM::SkaldPolicy;
        WaveStacks normal;
        for(unsigned hit=1;hit<=20;++hit){
            const auto before=normal.count;
            const bool next=normal.NextIsStack();
            CHECK(normal.Hit(false)==next);
            CHECK(normal.hits==std::min(hit,10u));
            CHECK(normal.count==(hit<3?0u:hit<5?1u:hit<10?2u:3u));
            CHECK(Near(normal.factor,hit<3?0.f:hit<5?1.f:hit<10?1.5f:2.f));
            CHECK(normal.count>=before);
        }
        // Every combination retains earlier rolls; a later normal stack never weakens a proc.
        for(unsigned mask=0;mask<8;++mask){
            WaveStacks stacks;float previous=0.f;
            for(unsigned hit=1;hit<=10;++hit){
                const bool doubled=stacks.NextIsStack()&&(mask&(1u<<stacks.count));
                stacks.Hit(doubled);CHECK(stacks.factor>=previous);previous=stacks.factor;
            }
            CHECK(stacks.empowered==mask);
            CHECK(Near(stacks.factor,2.f+((mask&1)?1.f:0.f)+((mask&2)?.5f:0.f)+((mask&4)?.5f:0.f)));
        }
        CHECK(Near(WaveRadius(.5f),400.f));
        CHECK(WaveRadius(-1)==0 && WaveRadius(100)==kReach);
        CHECK(WaveAlpha(0)==0 && WaveAlpha(kWaveSeconds)==0 && WaveAlpha(.5f)>0);
        WaveContact standing;
        CHECK(!CrossWave(standing,0,100,400,true));
        CHECK(CrossWave(standing,100,700,400,true)); // A low-frame-rate sweep still hits.
        CHECK(!CrossWave(standing,700,1000,400,true)); // Never twice from this wave.
        WaveContact moving;
        CHECK(!CrossWave(moving,0,100,500,true));
        CHECK(CrossWave(moving,100,200,20,true)); // Running inward crosses the front.
        WaveContact late,blocked,far;
        CHECK(!CrossWave(late,600,800,100,true)); // Entering behind an old wave earns nothing.
        CHECK(!CrossWave(blocked,0,500,400,false));
        CHECK(!CrossWave(blocked,500,800,400,true)); // Opening line of sight after it passed is too late.
        CHECK(!CrossWave(far,0,1000,1001,true));
        WaveContact self;CHECK(CrossWave(self,0,1,0,true));
        WaveContact otherWave;CHECK(CrossWave(otherWave,0,500,400,true));
        WaveStacks otherListener;CHECK(otherListener.hits==0 && otherListener.factor==0);
        CHECK(Near(Advance(1,Recovery(3)*4),3.4f));
        CHECK(HitAlpha(-1)==0 && HitAlpha(0)==0 && HitAlpha(kHitSeconds)==0);
        CHECK(std::abs(TimidDamage(10.f)-.1f)<.0001f && TimidDamage(10000.f)==1.f && TimidDamage(0.f)==0.f);
        CHECK(HitAlpha(.06f)>HitAlpha(.25f) && HitAlpha(.25f)>HitAlpha(.45f));
        CHECK(BardCount(1)<BardCount(2)&&BardCount(3)==2*BardCount(2));
        CHECK(HitCount(1)<HitCount(2)&&HitCount(3)==2*HitCount(2));
        CHECK(RankSpread(3)>RankSpread(2)&&RankGlow(3)>RankGlow(2));
        CHECK(RecipientAura(3,.2f)>2*RecipientAura(2,.2f)&&RecipientAura(3,kHitParticleSeconds)==0);
        for(int rank=1;rank<=3;++rank)for(std::size_t i=0;i<kWaveMotes;++i)for(float age=-.1f;age<1.6f;age+=.04f){
            const auto mote=WaveMote(0,i,age,rank);
            CHECK(std::isfinite(mote.x)&&std::isfinite(mote.y)&&std::isfinite(mote.z));
            CHECK(mote.alpha>=0&&mote.alpha<=1&&mote.scale>0&&mote.scale<3);
            CHECK(std::hypot(mote.x,mote.y)<=kReach&&mote.z>=0&&mote.z<100);
            if(age<0||age>=kWaveSeconds||i>=WaveCount(rank))CHECK(mote.alpha==0);
        }
        for(int song=0;song<4;++song)for(std::size_t i=0;i<kBardMotes;++i)for(float time=-.1f;time<12.f;time+=.13f){
            const auto mote=BardMote(song,i,time,.1f);
            CHECK(std::isfinite(mote.x)&&std::isfinite(mote.y)&&std::isfinite(mote.z));
            CHECK(mote.alpha>=0&&mote.alpha<=1&&mote.scale>0&&mote.scale<2);
            CHECK(std::hypot(mote.x,mote.y)<80&&mote.z>=0&&mote.z<=162);
            if(time<0)CHECK(mote.alpha==0);
        }
        for(int song=0;song<4;++song)for(std::size_t i=0;i<kHitMotes;++i)for(float age=-.1f;age<2.f;age+=.03f){
            const auto mote=HitMote(song,i,age);
            CHECK(std::isfinite(mote.x)&&std::isfinite(mote.y)&&std::isfinite(mote.z));
            CHECK(mote.alpha>=0&&mote.alpha<=1&&mote.scale>0&&mote.scale<2);
            CHECK(std::hypot(mote.x,mote.y)<54&&mote.z>=0&&mote.z<=162);
            if(age<0||age>=kHitParticleSeconds)CHECK(mote.alpha==0);
        }
        CHECK(WaveCount(3)==4*WaveCount(1));
        CHECK(LuteVolume(true)<.25f*LuteVolume(false)&&Near(LuteVolume(false),.7f));
        // Single Note discords build in steps to a cap and never invert.
        for(unsigned stacks=0;stacks<=8;++stacks){
            const unsigned capped=std::min(stacks,kDiscordStacks);
            CHECK(Near(DiscordWeaken(stacks),.04f*capped)&&Near(DiscordOffBeat(stacks),.05f*capped));
            CHECK(Near(DiscordExposed(stacks),.04f*capped)&&Near(DiscordDrain(stacks),.04f*capped));
            CHECK(DiscordWeaken(stacks)<1.f&&DiscordOffBeat(stacks)<1.f);
        }
        CHECK(DiscordWeaken(0)==0.f&&DiscordExposed(1)>0.f);
        // Every kind comes up, and only the ones that can matter.
        std::array<unsigned,kDiscords> seen{};
        for(int i=0;i<600;++i)++seen[static_cast<std::size_t>(PickDiscord(static_cast<float>(i)/600.f,true,true))];
        for(const auto count:seen)CHECK(count>=95&&count<=105);
        for(int i=0;i<=100;++i){
            const float roll=static_cast<float>(i)/100.f;
            CHECK(PickDiscord(roll,false,true)!=Discord::Muddled);
            CHECK(PickDiscord(roll,true,false)!=Discord::Jarred);
            const auto either=PickDiscord(roll,false,false);
            CHECK(either!=Discord::Muddled&&either!=Discord::Jarred);
        }
        CHECK(PickDiscord(-1.f,true,true)==Discord::Weakened&&PickDiscord(5.f,true,true)==Discord::Jarred);
        CHECK(kJarredCooldown>NoteInterval(1.f)); // a stagger every note would be crowd control, not a debuff
        // Crescendos: rare, never twice on one enemy inside its immunity, every kind reachable.
        CHECK(CrescendoLands(0.f,kCrescendoImmunity)&&!CrescendoLands(kCrescendoChance,kCrescendoImmunity));
        CHECK(!CrescendoLands(0.f,kCrescendoImmunity-.01f)); // a volley cannot chain-lock its target
        CHECK(kCrescendoChance>0.f&&kCrescendoChance<=.1f&&kCrescendoImmunity>CrescendoSeconds(Crescendo::Lullaby)*2.f);
        std::array<unsigned,kCrescendos> held{};
        for(int i=0;i<400;++i)++held[static_cast<std::size_t>(PickCrescendo(static_cast<float>(i)/400.f,true))];
        for(const auto count:held)CHECK(count==100);
        for(int i=0;i<=100;++i)CHECK(PickCrescendo(static_cast<float>(i)/100.f,false)!=Crescendo::Silenced);
        for(std::size_t i=0;i<kCrescendos;++i){const float hold=CrescendoSeconds(static_cast<Crescendo>(i));CHECK(hold>=3.f&&hold<=6.f);}
        // A crescendo's icon lasts its whole hold; a discord's fades within a second.
        CHECK(MarkAlpha(2.f,3.f)>.99f&&MarkAlpha(2.75f,3.f)>0.f&&MarkAlpha(2.75f,3.f)<.99f&&MarkAlpha(3.f,3.f)==0.f);
        CHECK(MarkAlpha(2.f)==0.f&&kMajorMarkScale>1.f);
        // Overhead icons fade in quickly, hold, then fade out before they pile up.
        CHECK(MarkAlpha(-.1f)==0.f&&MarkAlpha(0.f)==0.f&&MarkAlpha(kMarkSeconds)==0.f);
        CHECK(MarkAlpha(.12f)>.99f&&MarkAlpha(.4f)>.99f&&MarkAlpha(.9f)>0.f&&MarkAlpha(.9f)<.99f);
        CHECK(kMarkSeconds<NoteInterval(0.f)+.2f);
        // The recorded playing arms loop without a jump where each pass wraps.
        for(const float length:{2.4f,3.2f}){
            const float body=length-kLayerSeam;
            for(float elapsed=0.f;elapsed<12.f;elapsed+=.01f){
                const auto at=LoopAt(elapsed,length);
                CHECK(at.time>=kLayerSeam-.0001f&&at.time<length+.0001f);
                CHECK(at.blend>=0.f&&at.blend<=1.f);
                if(at.blend>0.f)CHECK(at.other>=0.f&&at.other<=kLayerSeam+.0001f);
            }
            // Just before a wrap the pose is almost entirely the start of the next pass.
            const auto wrap=LoopAt(body-.001f,length);
            CHECK(wrap.blend>.99f&&std::abs(wrap.other-kLayerSeam)<.01f);
            CHECK(LoopAt(0.f,length).blend==0.f&&Near(LoopAt(0.f,length).time,kLayerSeam));
        }
        CHECK(kLayerMinimum>2.f*kLayerSeam&&kLayerRecord>kLayerMinimum);
        CHECK(LayerWeight(0.f,-1.f)==0.f&&LayerWeight(kLayerFade,-1.f)==1.f&&LayerWeight(10.f,-1.f)==1.f);
        CHECK(LayerWeight(10.f,0.f)==1.f&&LayerWeight(10.f,kLayerFade)==0.f&&LayerWeight(10.f,kLayerFade*.5f)>0.f);
        CHECK(LayerWeight(kLayerFade*.5f,-1.f)>0.f&&LayerWeight(kLayerFade*.5f,-1.f)<1.f);
        // The Single Note's lute reads white, not the cyan of Echoes.
        CHECK(std::ranges::all_of(kNoteLute,[](float channel){return channel>=.85f&&channel<=1.f;}));
        // Hits walk a pentatonic phrase within one octave.
        for(unsigned i=0;i<20;++i){const float pitch=NotePitch(i);CHECK(pitch>=1.f&&pitch<=2.f);}
        CHECK(NotePitch(0)==1.f&&Near(NotePitch(4),2.f)&&NotePitch(1)!=NotePitch(0));
        for(float time=0;time<20.f;time+=.07f){
            const auto veil=LuteVeil(time,.5f);
            CHECK(std::abs(veil.x)<=1.3f&&std::abs(veil.y)<=.8f&&std::abs(veil.z)<=.6f);
            CHECK(veil.scale>1.f&&veil.scale<1.05f&&veil.glow>.5f&&std::isfinite(veil.scroll));
        }
        MusicDuck duck;
        float music=.6f;
        music=duck.Update(music,true,.2f);CHECK(std::abs(music-.168f)<.0001f);
        // Multiple performers share one active flag; finishing one cannot restore the mix.
        music=duck.Update(music,true,5.f);CHECK(std::abs(music-.168f)<.0001f);
        music=duck.Update(music,false,.325f);CHECK(music>.168f&&music<.6f);
        music=duck.Update(music,false,.325f);CHECK(std::abs(music-.6f)<.0001f&&!duck.owned);
        music=duck.Update(.6f,true,.2f);
        music=duck.Update(.4f,true,.1f); // User or another mod adjusts the music during the song.
        CHECK(std::abs(music-.112f)<.0001f);
        CHECK(std::abs(duck.Restore(music)-.4f)<.0001f&&!duck.owned);
        music=duck.Update(.5f,true,.2f);CHECK(duck.Restore(.7f)==.7f);
        for(const auto& track:FWM::SkaldMusic::tracks){
            CHECK(track.waves.size()==10 && track.beats.size()==20);
            for(std::size_t n=0;n<track.waves.size();++n){
                CHECK(track.waves[n]==track.beats[n*2]);
                if(n)CHECK(std::abs(track.waves[n]-track.waves[n-1]-120.f/track.bpm)<.0011f);
            }
            CHECK(track.waves.back()<track.seconds);
            WaveStacks atEdge;
            for(const auto wave:track.waves){
                WaveContact contact;
                CHECK(CrossWave(contact,0,WaveRadius(track.seconds+kWaveSeconds-wave),kReach,true));
                atEdge.Hit(false);
            }
            CHECK(atEdge.count==3 && Near(atEdge.factor,2.f));
        }
    }


	void SteadyBreathRules()
	{
		// Fletcher's original serialized key and node position must remain usable.
		static_assert(static_cast<int>(Perk::kSteadyBreath) == 73);
		CHECK(PathOf(Role::kArcher)[12].perk == Perk::kSteadyBreath);
		CHECK(PathOf(Role::kArcher)[12].name == "Steady Breath");
		CHECK(Value(Perk::kSteadyBreath, 0) == 0);
		for (int rank = 1; rank <= 3; ++rank) {
			CHECK(Value(Perk::kSteadyBreath, rank) == 25.0f * rank);
			auto record = Fresh();
			record.peak = 50;
			for (int i = 0; i < rank; ++i) {
				CHECK(Learn(record, Role::kArcher, At(Role::kArcher, static_cast<Perk>(73)), 99) == Block::kNone);
			}
			const auto decoded = Decode(Encode(std::vector<Entry>{ { 0x1234u, record } }), kRecordVersion);
			CHECK(decoded.size() == 1);
			if (!decoded.empty()) {
				CHECK(Rank(decoded.front().second, Role::kArcher, Perk::kSteadyBreath) == rank);
				CHECK(Spent(decoded.front().second) == Spent(record));
			}
		}
	}

	void ArcherTraitRules()
	{
		for (int element = 0; element < 3; ++element) {
			Archery::Shot arrow{ element, false };
			CHECK(arrow.Resolve(true) == element);
			CHECK(arrow.Resolve(true) == -1);
			Archery::Shot blocked{ element, false };
			CHECK(blocked.Resolve(false) == -1);
			CHECK(blocked.Resolve(true) == -1);
		}
		Archery::Shot ordinary;
		CHECK(ordinary.Resolve(true) == -1);
		// Preserve the former Skirmisher/Sharpshooter saved slots and investment.
		static_assert(static_cast<int>(Perk::kFineArrows) == 74);
		static_assert(static_cast<int>(Perk::kSpellTouched) == 75);
		CHECK(PathOf(Role::kArcher)[13].name == "Fine Arrows");
		CHECK(PathOf(Role::kArcher)[14].name == "Spell-Touched");
		auto record = Fresh();
		record.peak = 50;
		for (std::size_t i = 0; i < kNodes; ++i) {
			for (int rank = 0; rank < 3; ++rank) Learn(record, Role::kArcher, i, kUnlimited);
		}
		const auto decoded = Decode(Encode(std::vector<Entry>{ { 0x1234u, record } }), kRecordVersion);
		CHECK(decoded.size() == 1);
		if (!decoded.empty()) {
			CHECK(Rank(decoded.front().second, Role::kArcher, static_cast<Perk>(74)) == 3);
			CHECK(Rank(decoded.front().second, Role::kArcher, static_cast<Perk>(75)) == 3);
			CHECK(Spent(decoded.front().second) == Spent(record));
		}
		for (int rank = 1; rank <= 3; ++rank) {
			CHECK(Value(Perk::kFineArrows, rank) == static_cast<float>(rank));
			const float chance = Value(Perk::kSpellTouched, rank);
			CHECK((chance == std::array{ 10.0f, 15.0f, 20.0f }[rank - 1]));
			CHECK(Archery::Imbued(chance, 0));
			CHECK(Archery::Imbued(chance, chance - 0.01f));
			CHECK(!Archery::Imbued(chance, chance));
		}
		CHECK(!Archery::Imbued(0, 0));
		CHECK(!Archery::Imbued(20, -1));
		CHECK(!Archery::Imbued(20, std::numeric_limits<float>::quiet_NaN()));
		CHECK(Near(Archery::ElementalDamage(0, 0), 8));
		CHECK(Near(Archery::ElementalDamage(50, 50), 2));
		CHECK(Archery::ElementalDamage(0, 100) == 0);
		CHECK(Archery::ElementalDamage(100, 0) == 0);
		CHECK(Archery::ElementalDamage(0, 150) == 0);
		CHECK(Near(Archery::ElementalDamage(0, -50), 12));
		CHECK(Archery::ElementalDamage(std::numeric_limits<float>::infinity(), 0) == 0);
	}

	void ChaosDownRules()
	{
		// These numeric keys are in existing Challenge/Humble/Duel saves and equipped slots.
		static_assert(static_cast<int>(Ability::kChaosDown) == 3);
		static_assert(static_cast<int>(Perk::kChaosDown) == 7);
		static_assert(static_cast<int>(Perk::kChaosDownWide) == 8);
		static_assert(static_cast<int>(Perk::kChaosDownLingering) == 9);
		CHECK(Info(Ability::kChaosDown).name == "Chaos Down");
		const auto tank = Role::kTank;
		const auto& path = PathOf(tank);
		CHECK(path[6].perk == Perk::kChaosDown && path[6].tier == 1);
		CHECK(path[7].name == "Widening Chaos" && path[8].name == "Lingering Chaos");
		for (int rank = 1; rank <= 3; ++rank) {
			CHECK(Near(Value(Perk::kChaosDown, rank), std::array{ 25.0f, 40.0f, 55.0f }[rank - 1]));
			CHECK(Near(Value(Perk::kChaosDownWide, rank), 50.0f * rank));
			CHECK(Near(Value(Perk::kChaosDownLingering, rank), 2.0f * rank));
		}
		CHECK(Near(Cooldown(Fresh(), tank, Ability::kChaosDown), 25.0f));
		CHECK(Near(ChaosDown::kReach + Value(Perk::kChaosDownWide, 3), 400.0f));
		CHECK(Near(ChaosDown::kSeconds + Value(Perk::kChaosDownLingering, 3), 14.0f));

		auto record = Fresh();
		record.peak = 35;
		for (const auto perk : { Perk::kTaunt, Perk::kTaunt, Perk::kTaunt, Perk::kTauntBrace, Perk::kTauntBrace, Perk::kTauntBrace,
			Perk::kChaosDown, Perk::kChaosDown, Perk::kChaosDown, Perk::kChaosDownWide, Perk::kChaosDownWide, Perk::kChaosDownLingering }) {
			CHECK(Learn(record, tank, At(tank, perk), 99) == Block::kNone);
		}
		// The old saved slot value (3) equips the replacement; no refund, respec or extra cost.
		CHECK(Equip(record, tank, 1, static_cast<Ability>(3)) == Block::kNone);
		const auto decoded = Decode(Encode(std::vector<Entry>{ { 0x1234u, record } }), kRecordVersion);
		CHECK(decoded.size() == 1);
		if (!decoded.empty()) {
			const auto& saved = decoded.front().second;
			CHECK(saved == record && Spent(saved) == Spent(record));
			CHECK(Rank(saved, tank, Ability::kChaosDown) == 3);
			CHECK(Rank(saved, tank, Perk::kChaosDownWide) == 2 && Rank(saved, tank, Perk::kChaosDownLingering) == 1);
		}

		const auto first = ChaosDown::Combine(100, 0, 0, 25, 8);
		CHECK(Near(first.amount, -25) && Near(first.seconds, 8));
		const auto repeat = ChaosDown::Combine(75, first.amount, 3, 25, 8);
		CHECK(Near(repeat.amount, -25) && Near(repeat.seconds, 8));
		const auto stronger = ChaosDown::Combine(75, -25, 3, 55, 8);
		CHECK(Near(stronger.amount, -55));
		const auto weaker = ChaosDown::Combine(45, -55, 12, 25, 8);
		CHECK(Near(weaker.amount, -55) && Near(weaker.seconds, 12));
		CHECK(Near(ChaosDown::Combine(150, 0, 0, 40, 8).amount, -60));
		// A separate speed change stays in place when our own contribution is removed.
		const auto modified = ChaosDown::Combine(60, -25, 3, 40, 8);
		CHECK(Near(modified.amount, -34));
		CHECK(Near(60 + (modified.amount - (-25)) - modified.amount, 85));
		CHECK(ChaosDown::Combine(0, 0, 0, 55, 8).amount == 0);
		CHECK(ChaosDown::Combine(100, 0, 0, 55, 0).amount == 0);
		CHECK(ChaosDown::Combine(std::numeric_limits<float>::quiet_NaN(), 0, 0, 55, 8).amount == 0);
		const auto attack = ChaosDown::CombineAttack(1, 0, 0, 25, 8);
		CHECK(Near(1 + attack.amount, 0.75f));
		const auto unset = ChaosDown::CombineAttack(0, 0, 0, 55, 8);
		CHECK(Near(unset.amount, 0.45f));
		const auto unsetRepeat = ChaosDown::CombineAttack(0.45f, unset.amount, 4, 25, 8);
		CHECK(Near(unsetRepeat.amount, 0.45f) && Near(unsetRepeat.seconds, 8));
		// A separate modifier being added then removed can leave one ULP above our
		// positive contribution on an initially unset multiplier. It is still unset.
		const float residueCurrent = std::nextafter(unset.amount, 1.0f);
		const auto residueRepeat = ChaosDown::CombineAttack(residueCurrent, unset.amount, 4, 25, 8);
		CHECK(Near(residueRepeat.amount, 0.45f));
		CHECK(residueCurrent + residueRepeat.amount - unset.amount > 0.44f);
		const float remaining = residueCurrent - unset.amount;
		CHECK(remaining + ChaosDown::AttackUnsetCorrection(remaining) == 0.0f);
		CHECK(-remaining + ChaosDown::AttackUnsetCorrection(-remaining) == 0.0f);
		CHECK(ChaosDown::AttackUnsetCorrection(0.1f) == 0.0f);
		CHECK(ChaosDown::AttackUnsetCorrection(1.2f) == 0.0f);
		CHECK(ChaosDown::AttackUnsetCorrection(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
		const auto attackRepeat = ChaosDown::CombineAttack(0.75f, attack.amount, 4, 25, 8);
		CHECK(Near(attackRepeat.amount, -0.25f));
		const auto attackStronger = ChaosDown::CombineAttack(0.75f, -0.25f, 4, 55, 8);
		CHECK(Near(attackStronger.amount, -0.55f));
		const auto attackWeaker = ChaosDown::CombineAttack(0.45f, -0.55f, 12, 25, 8);
		CHECK(Near(attackWeaker.amount, -0.55f) && Near(attackWeaker.seconds, 12));
		const auto quickened = ChaosDown::CombineAttack(1.5f, 0, 0, 40, 8);
		CHECK(Near(1.5f + quickened.amount, 0.9f));
		const auto otherMod = ChaosDown::CombineAttack(0.95f, -0.25f, 4, 40, 8);
		CHECK(Near(otherMod.amount, -0.48f));
		CHECK(Near(0.95f + (otherMod.amount - (-0.25f)) - otherMod.amount, 1.2f));
		CHECK(Near(ChaosDown::CombineAttack(0, 0, 0, 500, 8).amount, 0.1f));
		CHECK(ChaosDown::CombineAttack(1, 0, 0, 55, 0).seconds == 0);
		CHECK(ChaosDown::CombineAttack(std::numeric_limits<float>::quiet_NaN(), 0, 0, 55, 8).seconds == 0);
	}

	void Shape()
	{
		for (std::size_t r = 0; r < kRoles; ++r) {
			const auto  role = static_cast<Role>(r);
			const auto& path = PathOf(role);
			std::set<Perk> seen;
			int            abilityCount = 0, traits = 0, defaults = 0, capstones = 0;
			for (std::size_t i = 0; i < kNodes; ++i) {
				const auto& node = path[i];
				if (node.Empty()) {
					// Only ever the two places a capstone leaves.
					CHECK(i == 10 || i == 11);
					CHECK(path[9].capstone);
					continue;
				}
				CHECK(seen.insert(node.perk).second);
				CHECK(node.most >= 1 && node.most <= 3);
				CHECK(node.base <= node.most);
				CHECK(node.tier <= 2);
				CHECK(!node.name.empty());
				CHECK(node.glyph != 0);
				CHECK(node.cost >= 1 && node.step >= 1);
				if (node.kind == Kind::kAbility) {
					++abilityCount;
					defaults += node.base > 0 ? 1 : 0;
					CHECK(node.ability != Ability::kNone);
					CHECK(node.most == 3);
					if (node.capstone) {
						// Alone in the last ability's place.
						++capstones;
						CHECK(i == 9 && path[10].Empty() && path[11].Empty());
						continue;
					}
					// Both branches follow it and belong to it.
					CHECK(i + 2 < kNodes && path[i + 1].kind == Kind::kBranch && path[i + 2].kind == Kind::kBranch);
					CHECK(path[i + 1].ability == node.ability && path[i + 2].ability == node.ability);
					CHECK(path[i + 1].tier == node.tier && path[i + 2].tier == node.tier);
				}
				if (node.kind == Kind::kTrait) {
					++traits;
					CHECK(node.base == 0);
				}
			}
			CHECK(abilityCount == static_cast<int>(kAbilitiesPerPath));
			CHECK(traits == static_cast<int>(kTraitsPerPath));
			CHECK(defaults == 0);
			CHECK(capstones == (role == Role::kMage ? 1 : 0));
			const auto d = Defaults(role);
			CHECK(d[0] == Ability::kNone && d[1] == Ability::kNone);
		}
		// Every ability lives on at least one path (Ward on two), but the retired Atronach.
		for (std::size_t a = 1; a < kAbilityCount; ++a) {
			if (static_cast<Ability>(a) == Ability::kAtronachRetired) {
				for (std::size_t r = 0; r < kRoles; ++r) {
					CHECK(IndexOf(static_cast<Role>(r), Ability::kAtronachRetired) < 0);
				}
				continue;
			}
			int on = 0;
			for (std::size_t r = 0; r < kRoles; ++r) {
				on += IndexOf(static_cast<Role>(r), static_cast<Ability>(a)) >= 0 ? 1 : 0;
			}
			CHECK(on >= 1);
			CHECK(!Info(static_cast<Ability>(a)).name.empty());
			CHECK(Info(static_cast<Ability>(a)).glyph != 0);
		}
		CHECK(IndexOf(Role::kHealer, Ability::kWard) >= 0 && IndexOf(Role::kMage, Ability::kWard) < 0);
		CHECK(Signature() != 0);
	}

	void MageRework()
    {
        std::vector<std::uint8_t> bytes;
        const auto put=[&](std::uint32_t value,int count){for(int i=0;i<count;i++)bytes.push_back(static_cast<std::uint8_t>(value>>(8*i)));};
        put(1,4);put(0x14,4);put(50,2);put(static_cast<std::uint32_t>(kRoles),1);
        for(std::size_t r=0;r<kRoles;r++) {
            const bool mage=r==static_cast<std::size_t>(Role::kMage), healer=r==static_cast<std::size_t>(Role::kHealer);
            put(mage||healer?static_cast<std::uint32_t>(Ability::kWard):0,1);put(mage?static_cast<std::uint32_t>(Ability::kBurst):0,1);
            put(mage?6:healer?2:0,1);
            if(mage) for(const auto& [perk,rank]:std::array<std::pair<Perk,int>,6>{{{Perk::kWard,3},{Perk::kWardSpellward,3},{Perk::kWardMantle,1},{Perk::kBurst,3},{Perk::kBurstSearing,2},{Perk::kBurstTwin,1}}}) {put(static_cast<std::uint32_t>(perk),1);put(rank,1);}
            if(healer) {put(static_cast<std::uint32_t>(Perk::kWard),1);put(2,1);put(static_cast<std::uint32_t>(Perk::kHeal),1);put(3,1);}
        }
        put(0,4);
        auto decoded=Decode(bytes,kRecordVersion);
        CHECK(decoded.size()==1);
        if(decoded.empty()) return;
        auto& record=decoded[0].second;
        Repair(record,99);
        CHECK(IndexOf(Role::kMage,Ability::kBurst)==3 && IndexOf(Role::kMage,Ability::kDragonBreath)==6);
        CHECK(Rank(record,Role::kMage,Ability::kBurst)==3 && Rank(record,Role::kMage,Perk::kBurstSearing)==2 && Rank(record,Role::kMage,Perk::kBurstTwin)==1);
        CHECK(Rank(record,Role::kMage,Ability::kDragonBreath)==0 && Rank(record,Role::kMage,Ability::kWard)==0);
        CHECK(Rank(record,Role::kHealer,Ability::kWard)==2);
        CHECK(Spent(record,Role::kMage)==9);
        const auto& slots=record.slots[static_cast<std::size_t>(Role::kMage)];
        CHECK(slots[0]==Ability::kNone && slots[1]==Ability::kBurst);
        auto old=Decode(bytes,3);
        CHECK(old.size()==1);
        if(!old.empty()) {
            CHECK(Rank(old[0].second,Role::kHealer,Ability::kWard)==0);
            CHECK(Rank(old[0].second,Role::kHealer,Ability::kHeal)==3);
            CHECK(Spent(old[0].second,Role::kHealer)==6);
        }
        CHECK(IndexOf(Role::kHealer,Ability::kRenewal)==3);
        CHECK(IndexOf(Role::kHealer,Ability::kRejuvenate)==6);
        CHECK(IndexOf(Role::kHealer,Ability::kWard)==9);
        CHECK(RequiredLevel(PathOf(Role::kHealer)[6],1)==35);
        CHECK(RequiredLevel(PathOf(Role::kHealer)[9],1)==50);
    }

	void Words()
	{
		for (std::size_t r = 0; r < kRoles; ++r) {
			for (const auto& node : PathOf(static_cast<Role>(r))) {
				for (int rank = 1; rank <= node.most; ++rank) {
					CHECK(!Describe(node.perk, rank).empty());
				}
			}
		}
		CHECK(Describe(Perk::kWard, 1).find("20%") != std::string::npos);
		CHECK(Describe(Perk::kWard, 3).find("30%") != std::string::npos);
		CHECK(Describe(Perk::kMagnus, 1).find("never runs dry") != std::string::npos);
		CHECK(Describe(Perk::kMagnus, 3).find("50%") != std::string::npos);
		CHECK(Near(Third(Perk::kMagnus, 3), 50.0f) && Near(Third(Perk::kMagnus, 2), 0.0f));
		CHECK(Describe(Perk::kWardShared, 3).find("6 m farther") != std::string::npos);
		CHECK(Near(Value(Perk::kTaunt, 1), 10.0f) && Near(Value(Perk::kTaunt, 2), 15.0f) && Near(Value(Perk::kTaunt, 3), 30.0f));
		CHECK(Near(Value(Perk::kTaunt, 0), 0.0f));
		CHECK(Near(Second(Perk::kWarCry, 3), 1.0f));
	}


	void Points()
	{
		CHECK(Earned(5, 10) == 0);
		CHECK(Earned(10, 10) == 0);
		CHECK(Earned(11, 10) == 1);
		CHECK(Earned(50, 10) == 40);
		CHECK(Earned(50, 0) == 50);
		CHECK(Earned(50, -3) == 50);
		const auto record = Fresh();
		CHECK(Spent(record) == 0);
		for (std::size_t r = 0; r < kRoles; ++r) {
			CHECK(record.slots[r][0] == Ability::kNone && record.slots[r][1] == Ability::kNone);
			for (std::size_t i = 0; i < kNodes; ++i) {
				CHECK(record.ranks[r][i] == 0);
				const auto role = static_cast<Role>(r);
				const auto& node = PathOf(role)[i];
				for (int rank = 1; rank <= node.most; ++rank)
					CHECK(CostOf(role, i, rank) == (node.kind == Kind::kAbility ? rank : 1));
			}
		}
	}


	void Learning()
	{
		const auto tank = Role::kTank;
		auto record = Fresh();
		const auto taunt = At(tank, Perk::kTaunt), brace = At(tank, Perk::kTauntBrace), chaos = At(tank, Perk::kChaosDown);
		CHECK(CanLearn(record, tank, chaos, 99) == Block::kLevel);
		CHECK(CanLearn(record, tank, brace, 99) == Block::kParent);
		CHECK(CanLearn(record, tank, taunt, 0) == Block::kNoPoints);
		CHECK(Learn(record, tank, taunt, 1) == Block::kNone);
		CHECK(Spent(record) == 1);
		CHECK(CanLearn(record, tank, taunt, 2) == Block::kNoPoints);
		CHECK(Learn(record, tank, taunt, 3) == Block::kNone);
		CHECK(CanLearn(record, tank, taunt, 5) == Block::kNoPoints);
		CHECK(Learn(record, tank, taunt, 6) == Block::kNone);
		CHECK(CanLearn(record, tank, taunt, 99) == Block::kMaxed);
		CHECK(Spent(record) == 6);
		record.peak = 35;
		CHECK(Learn(record, tank, chaos, 7) == Block::kNone);
		CHECK(CanLearn(record, tank, At(tank, Perk::kStandFast), 99) == Block::kLevel);
		CHECK(CanForget(record, tank, taunt) == Block::kNone);
		CHECK(Forget(record, tank, chaos) == Block::kNone);
		for (int rank = 1; rank <= 3; ++rank) CHECK(Learn(record, tank, brace, 99) == Block::kNone);
		CHECK(CanForget(record, tank, taunt) == Block::kHolds);
		CHECK(Forget(record, tank, brace) == Block::kNone);
		CHECK(Forget(record, tank, taunt) == Block::kNone);
		CHECK(Spent(record) == 5); // Refund III's three points, plus one branch rank.
		CHECK(CanLearn(record, Role::kArcher, 12, 5) == Block::kNoPoints);
		CHECK(Learn(record, Role::kArcher, 12, 6) == Block::kNone);
		Reset(record, tank);
		CHECK(Spent(record) == 1 && Spent(record, tank) == 0);
		CHECK(Rank(record, tank, Ability::kTaunt) == 0);
		CHECK(record.slots[static_cast<std::size_t>(tank)][0] == Ability::kNone);
	}

    void AbilityLevels()
    {
        for(std::size_t r=0;r<kRoles;++r){
            const auto role=static_cast<Role>(r);const auto& path=PathOf(role);const int entry=role==Role::kSkald?10:0;
            for(const auto index:{std::size_t{6},std::size_t{9}}){
                auto record=Fresh();record.skaldUnlocked=role==Role::kSkald;const int required=index==6?35:50;
                record.peak=static_cast<std::uint16_t>(required-1);
                CHECK(CanLearn(record,role,index,kUnlimited)==Block::kLevel);
                record.peak=static_cast<std::uint16_t>(required);
                if(path[index].capstone)continue;
                CHECK(CanLearn(record,role,index,0)==Block::kNoPoints);
                CHECK(Learn(record,role,index,1+entry)==Block::kNone);
                CHECK(Spent(record)==1+entry); // No earlier ability purchases.
                CHECK(Repair(record,99)==false);
                CHECK(Learn(record,role,index+1,2+entry)==Block::kNone);
                CHECK(CanForget(record,role,index)==Block::kHolds); // Its own branch still depends on it.
                auto loaded=Decode(Encode(std::vector<Entry>{{42,record}}),kRecordVersion);
                CHECK(loaded.size()==1 && !Repair(loaded[0].second,99));
                CHECK(Rank(loaded[0].second,role,path[index].ability)==1);
                record.peak=static_cast<std::uint16_t>(required-1);
                CHECK(Repair(record,99));
                CHECK(Spent(record)==entry); // Refund only newly level-locked purchases and branches.
            }
        }
        CHECK(Why(Block::kNoPoints)=="Not enough perk points");
        CHECK(Why(Block::kHolds)=="Refund dependent perks first");
        CHECK(Why(Block::kNotMastered)=="Requires all perks at maximum rank");
        for(std::size_t role=0;role<kRoles;++role)for(const auto& node:PathOf(static_cast<Role>(role))) {
            if(node.kind!=Kind::kBranch)continue;
            for(int rank=1;rank<=3;++rank) {
                const auto expected=std::string("Requires ")+std::string(Info(node.ability).name)+" Rank "+std::to_string(rank);
                CHECK(Requirement(Block::kParent,node,rank)==expected);
            }
        }

    }

	void TraitLevels()
	{
		for (std::size_t r = 0; r < kRoles; ++r) {
			const auto role = static_cast<Role>(r);const int entry=role==Role::kSkald?10:0;
			for (std::size_t i = 12; i < kNodes; ++i) {
				auto record = Fresh(); record.peak = 1;record.skaldUnlocked=role==Role::kSkald;
				CHECK(Learn(record, role, i, 1+entry) == Block::kNone);
				record.peak = 24;
				CHECK(CanLearn(record, role, i, kUnlimited) == Block::kLevel);
				record.peak = 25;
				CHECK(Learn(record, role, i, 2+entry) == Block::kNone);
				record.peak = 49;
				CHECK(CanLearn(record, role, i, kUnlimited) == Block::kLevel);
				record.peak = 50;
				CHECK(Learn(record, role, i, 3+entry) == Block::kNone);
				CHECK(Spent(record) == 3+entry);
				record.peak = 24;
				CHECK(Repair(record, kUnlimited));
				CHECK(record.ranks[r][i] == 1 && Spent(record) == 1+entry);
			}
		}
	}

	void ProgressionMigration()
	{
		auto record = Fresh(); record.peak = 60;
		Learn(record, Role::kTank, 0, 99); Learn(record, Role::kTank, 0, 99);
		record.bonus[0] = 7;
		const auto bytes = Encode(std::vector<Entry>{{42, record}});
		for (const auto version : {1u, 2u}) {
			const auto migrated = Decode(bytes, version);
			CHECK(migrated.size() == 1);
			CHECK(migrated[0].second.peak == 60 && Spent(migrated[0].second) == 0);
			CHECK(migrated[0].second.slots == Fresh().slots);
			Book book;
			book.Load(migrated, migrated); // Unlimited backup must migrate as well.
			CHECK(book.SetUnlimited(42, false) == Block::kNone);
			CHECK(book.Unspent(42) == 50);
			CHECK(book.Learn(42, Role::kTank, 0) == Block::kNone);
			const auto saved = Decode(Encode(book.Records()), kRecordVersion);
			CHECK(Rank(saved[0].second, Role::kTank, Ability::kTaunt) == 1);
			CHECK(saved[0].second.peak == 60); // New saves keep purchases, no repeat reset.
		}
	}

	void Slots()
	{
		const auto fighter = Role::kFighter;
		auto       record = Fresh();
		const auto f = static_cast<std::size_t>(fighter);
		CHECK(Equip(record, fighter, 0, Ability::kRiposte) == Block::kNotLearned);
		CHECK(Equip(record, fighter, 0, Ability::kVolley) == Block::kBadNode);
		CHECK(Learn(record, fighter, At(fighter, Perk::kDisarm), 30) == Block::kNone);
		CHECK(Learn(record, fighter, At(fighter, Perk::kSecondWind), 30) == Block::kNone);
		// Swapping the two.
		CHECK(Equip(record, fighter, 0, Ability::kSecondWind) == Block::kNone);
		CHECK(record.slots[f][0] == Ability::kSecondWind && record.slots[f][1] == Ability::kDisarm);

		// Learn Riposte at level 35, carry it, then forget it: the slot refills.
		record.peak = 35;
		for (int i = 0; i < 2; ++i) {
			Learn(record, fighter, At(fighter, Perk::kDisarm), 30);
			Learn(record, fighter, At(fighter, Perk::kSecondWind), 30);
		}
		Learn(record, fighter, At(fighter, Perk::kFlanker), 30);
		CHECK(Spent(record, fighter) == 13);
		CHECK(Learn(record, fighter, At(fighter, Perk::kRiposte), 30) == Block::kNone);
		CHECK(Equip(record, fighter, 1, Ability::kRiposte) == Block::kNone);
		CHECK(record.slots[f][1] == Ability::kRiposte);
		CHECK(Forget(record, fighter, At(fighter, Perk::kRiposte)) == Block::kNone);
		CHECK(record.slots[f][1] != Ability::kRiposte && record.slots[f][1] != Ability::kNone && record.slots[f][1] != record.slots[f][0]);
	}

	void Repairs()
	{
		const auto mage = Role::kMage;
		const auto m = static_cast<std::size_t>(mage);
		auto       record = Fresh();
		record.ranks[m][At(mage, Perk::kSurge)] = 9;           // past its highest rank
		record.ranks[m][At(mage, Perk::kBurst)] = 0;            // below a default's
		record.ranks[m][At(mage, Perk::kBurstTwin)] = 1;      // above a parent at 1 is fine
		record.ranks[m][At(mage, Perk::kMagnus)] = 2;          // a capstone with nothing under it
		record.slots[m] = { Ability::kAtronachRetired, Ability::kMagnus };
		CHECK(Repair(record, 40));
		CHECK(Rank(record, mage, Ability::kSurge) == 3);
		CHECK(Rank(record, mage, Ability::kBurst) == 0);
		CHECK(Rank(record, mage, Perk::kBurstTwin) == 0);
		CHECK(Rank(record, mage, Ability::kMagnus) == 0);
		CHECK(record.slots[m][1] != Ability::kMagnus);
		CHECK(record.slots[m][0] != Ability::kAtronachRetired && record.slots[m][0] != Ability::kNone && record.slots[m][1] != record.slots[m][0]);
		CHECK(!Repair(record, 40));

		// Overspent (the level limit went up): the fullest path gives back until it fits.
		auto rich = Fresh();
		rich.peak = 50;
		for (std::size_t i = 0; i < kNodes; ++i) {
			for (int k = 0; k < 3; ++k) {
				Learn(rich, Role::kArcher, i, 100);
			}
		}
		const int all = Spent(rich, Role::kArcher);
		CHECK(all > 30);
		CHECK(Repair(rich, 12));
		CHECK(Spent(rich) <= 12);
		CHECK(Repair(rich, 0));
		CHECK(Spent(rich) == 0);
	}

	void Cooldowns()
	{
		auto record = Fresh();
		CHECK(Near(Cooldown(record, Role::kMage, Ability::kBurst), 30.0f));
		// Searing stays a support debuff; the other branches sharing its old table stay intact.
		CHECK(Near(Value(Perk::kBurstSearing, 1), 10.0f));
		CHECK(Near(Value(Perk::kBurstSearing, 2), 15.0f));
		CHECK(Near(Value(Perk::kBurstSearing, 3), 20.0f));
		CHECK(Near(Value(Perk::kTriage, 3), 45.0f));
		CHECK(Near(Value(Perk::kWardSpellward, 3), 6.0f));
		CHECK(Describe(Perk::kBurstTwin, 1).find("50% damage") != std::string::npos);
		CHECK(Near(Cooldown(record, Role::kTank, Ability::kTaunt), 16.0f));
		// The healing circle waits the same at every rank: Invigorate brings stamina, not speed.
		CHECK(Near(Cooldown(record, Role::kHealer, Ability::kHeal), 40.0f));
		CHECK(Near(Cooldown(record, Role::kHealer, Ability::kRejuvenate), 50.0f));
		for (int i = 0; i < 2; ++i) {
			Learn(record, Role::kHealer, At(Role::kHealer, Perk::kHeal), 30);
		}
		Learn(record, Role::kHealer, At(Role::kHealer, Perk::kHealInvigorate), 30);
		Learn(record, Role::kHealer, At(Role::kHealer, Perk::kHealInvigorate), 30);
		CHECK(Near(Cooldown(record, Role::kHealer, Ability::kHeal), 40.0f));
		for (std::size_t a = 1; a < kAbilityCount; ++a) {
			for (std::size_t r = 0; r < kRoles; ++r) {
				if (IndexOf(static_cast<Role>(r), static_cast<Ability>(a)) >= 0) {
					CHECK(Cooldown(record, static_cast<Role>(r), static_cast<Ability>(a)) > 0.0f);
				}
			}
		}
	}

	void Codec()
	{
		auto a = Fresh();
		a.peak = 37;
		Learn(a, Role::kBruiser, At(Role::kBruiser, Perk::kWarCry), 27);
		Learn(a, Role::kBruiser, At(Role::kBruiser, Perk::kWarCryRally), 27);
		Learn(a, Role::kBruiser, At(Role::kBruiser, Perk::kHeavyHands), 27);
		Equip(a, Role::kBruiser, 0, Ability::kBerserk);
		auto b = Fresh();
		b.peak = 12;
		const std::vector<Entry> in{ { 0x0001A2B3u, a }, { 0xFE000801u, b } };
		const auto bytes = Encode(in);
		const auto out = Decode(bytes, kRecordVersion);
		CHECK(out.size() == 2);
		CHECK(out[0].first == 0x0001A2B3u && out[0].second == a);
		CHECK(out[1].first == 0xFE000801u && out[1].second == b);
		// Short or unknown data loads what it can and never reads past the end. The last four bytes
		// are the unlock count, preceded by the bonus count (both empty here).
		auto cut = bytes;
		cut.resize(cut.size() - 8 - 3);
		CHECK(Decode(cut, kRecordVersion).size() == 1);
		auto trailer = bytes;
		trailer.resize(trailer.size() - 2);
		CHECK(Decode(trailer, kRecordVersion).size() == 2);
		CHECK(Decode(bytes, 0).empty());
		CHECK(Decode({}, kRecordVersion).empty());
	}

	// Every node of a path at its highest rank, capstone included.
	void MasterPath(Record& a_record, Role a_role)
	{
		a_record.peak = std::max<std::uint16_t>(a_record.peak, 50);
		for (int pass = 0; pass < 4; ++pass) {
			for (std::size_t i = 0; i < kNodes; ++i) {
				for (int k = 0; k < 3; ++k) {
					Learn(a_record, a_role, i, kUnlimited);
				}
			}
		}
	}

	void Bonus()
	{
		const auto archer = Role::kArcher;
		const auto a = static_cast<std::size_t>(archer);
		const auto tank = Role::kTank;
		auto       record = Fresh();
		// Not before every node of the path is mastered.
		CHECK(!Mastered(record, archer));
		CHECK(CanInvest(record, archer, 99, 50) == Block::kNotMastered);
		MasterPath(record, archer);
		CHECK(Mastered(record, archer));
		const int tree = Mastery(archer);
		CHECK(OnTree(record, archer) == tree && Spent(record, archer) == tree && Invested(record, archer) == 0);
		// Mage: the capstone too.
		auto mage = Fresh();
		MasterPath(mage, Role::kMage);
		CHECK(Mastered(mage, Role::kMage) && OnTree(mage, Role::kMage) == Mastery(Role::kMage));
		--mage.ranks[static_cast<std::size_t>(Role::kMage)][At(Role::kMage, Perk::kMagnus)];
		CHECK(!Mastered(mage, Role::kMage));

		// Leftover points, one at a time: spent on the path, and of their points in all.
		CHECK(CanInvest(record, archer, tree, 50) == Block::kNoPoints);
		CHECK(Invest(record, archer, tree + 2, 50) == Block::kNone);
		CHECK(Invest(record, archer, tree + 2, 50) == Block::kNone);
		CHECK(Invest(record, archer, tree + 2, 50) == Block::kNoPoints);
		CHECK(Invested(record, archer) == 2 && Spent(record, archer) == tree + 2 && OnTree(record, archer) == tree && Spent(record) == tree + 2);
		// Where it stops is the bonus's to say.
		CHECK(CanInvest(record, archer, 99, 0) == Block::kBonusFull);
		CHECK(CanInvest(record, archer, 99, 1) == Block::kNone);
		// Only that path's bonus.
		CHECK(Invested(record, tank) == 0);
		CHECK(CanInvest(record, tank, 99, 50) == Block::kNotMastered);
		// They take from what can be learned anywhere else.
		CHECK(CanLearn(record, Role::kFighter, At(Role::kFighter, Perk::kFlanker), tree + 2) == Block::kNoPoints);
		CHECK(CanLearn(record, Role::kFighter, At(Role::kFighter, Perk::kFlanker), tree + 3) == Block::kNone);
		// They open no tiers.
		auto partial = Fresh();
		partial.bonus[static_cast<std::size_t>(tank)] = 9;
		CHECK(!TierOpen(partial, tank, 1));
		CHECK(CanLearn(partial, tank, At(tank, Perk::kChaosDown), 99) == Block::kLevel);
		// Back out one at a time, and all of them when the path starts over.
		CHECK(Divest(record, archer) == Block::kNone);
		CHECK(Invested(record, archer) == 1);
		CHECK(CanDivest(record, Role::kMage) == Block::kNothing);
		Reset(record, archer);
		CHECK(Spent(record) == 0 && Invested(record, archer) == 0);
		CHECK(Divest(record, archer) == Block::kNothing);

		// Overspent after a load: the bonus gives back first, the ranks only after.
		auto over = Fresh();
		MasterPath(over, archer);
		over.bonus[a] = 5;
		CHECK(Repair(over, tree + 2));
		CHECK(Invested(over, archer) == 2 && OnTree(over, archer) == tree);
		CHECK(Repair(over, tree - 1));
		CHECK(Invested(over, archer) == 0 && OnTree(over, archer) == tree - 1);
		// A bonus on a path no longer mastered stays: nothing about it is wrong.
		auto kept = Fresh();
		MasterPath(kept, archer);
		kept.bonus[a] = 3;
		Forget(kept, archer, At(archer, Perk::kSpellTouched));
		CHECK(!Repair(kept, 99));
		CHECK(Invested(kept, archer) == 3);
		// Unlimited points and back: the bonus as it was before.
		auto cheat = kept;
		const auto before = cheat;
		cheat.bonus[a] = 40;
		Restore(cheat, before);
		CHECK(Invested(cheat, archer) == 3);

		// The save keeps them; a version 1 reader still reads everything else.
		auto saved = Fresh();
		MasterPath(saved, archer);
		saved.peak = 70;
		saved.bonus[a] = 7;
		saved.bonus[static_cast<std::size_t>(Role::kMage)] = 300;
		const std::vector<Entry> in{ { 0x14u, saved }, { 0x15u, Fresh() } };
		const auto               bytes = Encode(in);
		const auto               out = Decode(bytes, kRecordVersion);
		CHECK(out.size() == 2 && out[0].second == saved && out[1].second == Fresh());
		const auto old = Decode(bytes, 1);
		CHECK(old.size() == 2 && old[0].second.ranks == Fresh().ranks && old[0].second.bonus[a] == 0 && old[0].second.peak == saved.peak);
		// A version 1 record loads as it always did.
		auto plain = Fresh();
		plain.bonus = {};
		CHECK(Decode(Encode(std::vector<Entry>{ { 0x16u, plain } }), 1).size() == 1);
	}

	// Each companion's points are their own: earned from their own level, spent on their own
	// paths, given back to them alone.
	void Companions()
	{
		const auto              tank = Role::kTank;
		const auto              archer = Role::kArcher;
		constexpr std::uint32_t lydia = 0x000A2C94, aela = 0x0001A696, stranger = 0x00099999;
		Book                    book;
		book.points.above = 10;

		CHECK(book.Meet(stranger, 0) == nullptr);
		CHECK(book.Earned(stranger) == 0 && book.Unspent(stranger) == 0);
		CHECK(book.Learn(stranger, tank, At(tank, Perk::kTaunt)) == Block::kBadNode);
		CHECK(book.Meet(lydia, 30) != nullptr);
		CHECK(book.Meet(aela, 14) != nullptr);
		CHECK(book.Earned(lydia) == 20 && book.Earned(aela) == 4);

		// Spending is theirs alone.
		CHECK(book.Learn(lydia, tank, At(tank, Perk::kTaunt)) == Block::kNone);
		CHECK(book.Unspent(lydia) == 19 && book.Unspent(aela) == 4);
		for (const auto perk : { Perk::kVolley, Perk::kVolley, Perk::kDeadeye }) {
			CHECK(book.Learn(aela, archer, At(archer, perk)) == Block::kNone);
		}
		CHECK(book.Unspent(aela) == 0);
		// Out of her own points, with Lydia's untouched: they are not hers to spend.
		CHECK(book.Learn(aela, archer, At(archer, Perk::kSteadyBreath)) == Block::kNoPoints);
		CHECK(book.Unspent(lydia) == 19);

		// Her level rising gives her points, and nobody else; a lower level seen later takes none away.
		book.Meet(aela, 16);
		CHECK(book.Earned(aela) == 6 && book.Earned(lydia) == 20);
		book.Meet(aela, 12);
		CHECK(book.Earned(aela) == 6 && book.Unspent(aela) == 2);

		// Starting over gives back to whoever starts over.
		CHECK(book.Reset(lydia, tank) == Block::kNone);
		CHECK(book.Unspent(lydia) == 20 && book.Unspent(aela) == 2);

		// Unlimited points for one are not for the other, and going back restores only hers.
		CHECK(book.SetUnlimited(aela, true) == Block::kNone);
		CHECK(book.Unlimited(aela) && !book.Unlimited(lydia));
		CHECK(book.Earned(aela) == kUnlimited && book.Earned(lydia) == 20);
		CHECK(book.Learn(aela, archer, At(archer, Perk::kSteadyBreath)) == Block::kNone);
		CHECK(book.Learn(aela, archer, At(archer, Perk::kSteadyBreath)) == Block::kNone);
		CHECK(book.Unspent(aela) == kUnlimited);
		CHECK(book.Learn(lydia, tank, At(tank, Perk::kTaunt)) == Block::kNone);
		CHECK(book.SetUnlimited(aela, false) == Block::kNone);
		CHECK(book.Earned(aela) == 6 && book.Unspent(aela) == 2);
		CHECK(book.Unspent(lydia) == 19);

		// Bonus points: on their own mastered path, out of their own pool.
		book.Meet(lydia, 80);
		CHECK(book.Earned(lydia) == 70);
		for (int pass = 0; pass < 3; ++pass) {
			for (std::size_t i = 0; i < kNodes; ++i) {
				for (int k = 0; k < 3; ++k) {
					(void)book.Learn(lydia, tank, i);
				}
			}
		}
		CHECK(Mastered(*book.Find(lydia), tank));
		const int left = book.Unspent(lydia);
		CHECK(left == 70 - Mastery(tank));
		CHECK(book.Invest(lydia, tank, 5) == Block::kNone);
		CHECK(book.Invest(lydia, tank, 5) == Block::kNone);
		CHECK(book.Invest(lydia, tank, 0) == Block::kBonusFull);
		CHECK(book.Unspent(lydia) == left - 2 && book.Unspent(aela) == 2);
		CHECK(Invested(*book.Find(lydia), tank) == 2 && Invested(*book.Find(aela), tank) == 0);
		CHECK(book.Invest(aela, archer, 5) == Block::kNotMastered);
		CHECK(book.Invest(stranger, tank, 5) == Block::kBadNode);
		CHECK(book.Divest(lydia, tank) == Block::kNone);
		CHECK(book.Divest(aela, tank) == Block::kNothing);
		CHECK(book.Unspent(lydia) == left - 1);

		// Saved and loaded: each record back with its own companion, and their own points. With
		// unlimited points, bonus points go in like anything else, and leave with them.
		CHECK(book.SetUnlimited(lydia, true) == Block::kNone);
		CHECK(book.Invest(lydia, tank, 50) == Block::kNone);
		CHECK(Invested(*book.Find(lydia), tank) == 2);
		Book loaded;
		loaded.points = book.points;
		CHECK(loaded.Load(book.Records(), book.KeptAside()) == 0);
		CHECK(loaded.Unlimited(lydia) && !loaded.Unlimited(aela));
		CHECK(loaded.Unspent(aela) == 2);
		CHECK(Invested(*loaded.Find(lydia), tank) == 2);
		CHECK(loaded.SetUnlimited(lydia, false) == Block::kNone);
		CHECK(loaded.Unspent(lydia) == left - 1);
		CHECK(Invested(*loaded.Find(lydia), tank) == 1);
		CHECK(*loaded.Find(aela) == *book.Find(aela));

		// A record overspent for its own level loses only its own points.
		auto tampered = loaded.Records();
		for (auto& [id, record] : tampered) {
			if (id == aela) {
				record.peak = 11;
			}
		}
		Book strict;
		strict.points = book.points;
		CHECK(strict.Load(tampered, {}) == 1);
		CHECK(Spent(*strict.Find(aela)) <= 1);
		CHECK(strict.Unspent(lydia) == left - 1);

		// Giving back switched off: no ranks, no bonus points, no starting over.
		strict.points.respec = false;
		CHECK(strict.Forget(lydia, tank, At(tank, Perk::kIronHide)) == Block::kNoRespec);
		CHECK(strict.Divest(lydia, tank) == Block::kNoRespec);
		CHECK(strict.Reset(lydia, tank) == Block::kNoRespec);
		strict.points.unlimited = false;
		CHECK(strict.SetUnlimited(aela, true) == Block::kNoUnlimited);
	}

	void Capstone()
	{
		const auto mage = Role::kMage;
		const auto magnus = At(mage, Perk::kMagnus);
		auto       record = Fresh();
		record.peak = 50;
		CHECK(CostOf(mage, magnus, 1) == 1 && CostOf(mage, magnus, 2) == 2 && CostOf(mage, magnus, 3) == 3);
		CHECK(Mastery(mage) == 49);
		CHECK(CanLearn(record, mage, magnus, 99) == Block::kCapstone);
        // One rank in every other node unlocks Magnus; full mastery is not required.
        const auto& path = PathOf(mage);
        for (std::size_t i=0;i<kNodes;++i) if(!path[i].Empty() && !path[i].capstone)
            CHECK(Learn(record,mage,i,99)==Block::kNone);
        CHECK(Spent(record,mage)==12);
        CHECK(CanLearn(record,mage,magnus,99)==Block::kNone);
        record.peak=49;
        CHECK(CanLearn(record,mage,magnus,99)==Block::kLevel);
        record.peak=50;
        const auto unravel=At(mage,Perk::kUnravel);
        CHECK(Forget(record,mage,unravel)==Block::kNone);
        CHECK(CanLearn(record,mage,magnus,99)==Block::kCapstone);
        CHECK(Learn(record,mage,unravel,99)==Block::kNone);
        const int others=Spent(record,mage);
        CHECK(CanLearn(record,mage,magnus,others)==Block::kNoPoints);
        CHECK(Learn(record,mage,magnus,others+1)==Block::kNone);
        CHECK(CanLearn(record,mage,magnus,others+2)==Block::kNoPoints);
        CHECK(Learn(record,mage,magnus,others+3)==Block::kNone);
        CHECK(Equip(record,mage,0,Ability::kMagnus)==Block::kNone);
        CHECK(CanForget(record,mage,unravel)==Block::kHolds);
        CHECK(Learn(record,mage,unravel,99)==Block::kNone);
        CHECK(CanForget(record,mage,unravel)==Block::kNone);
        CHECK(Forget(record,mage,unravel)==Block::kNone);
        auto shaken=record;
        shaken.ranks[static_cast<std::size_t>(mage)][unravel]=0;
        CHECK(Repair(shaken,99));
        CHECK(Rank(shaken,mage,Ability::kMagnus)==0);
        CHECK(Spent(shaken,mage)==others-1);
        CHECK(shaken.slots[static_cast<std::size_t>(mage)][0]!=Ability::kMagnus);
        CHECK(Forget(record,mage,magnus)==Block::kNone);
        CHECK(Forget(record,mage,magnus)==Block::kNone);
        CHECK(CanForget(record,mage,unravel)==Block::kNone);
        CHECK(CanLearn(record,mage,10,99)==Block::kBadNode);
		// A save from before Magnus: the Atronach's ranks are dropped and the points come back.
		std::vector<std::uint8_t> old{ 1, 0, 0, 0, 0x34, 0x12, 0, 0, 30, 0, 6 };
		for (std::size_t r = 0; r < 6; ++r) {
			const bool m = r == static_cast<std::size_t>(mage);
			old.insert(old.end(), { static_cast<std::uint8_t>(m ? Ability::kAtronachRetired : Ability::kNone), 0, static_cast<std::uint8_t>(m ? 1 : 0) });
			if (m) {
				old.insert(old.end(), { static_cast<std::uint8_t>(Perk::kAtronachRetired), 2 });
			}
		}
		auto loaded = Decode(old, kRecordVersion);
		CHECK(loaded.size() == 1);
		if (!loaded.empty()) {
			Repair(loaded[0].second, 20);
			CHECK(Spent(loaded[0].second) == 0);
			CHECK(loaded[0].second.slots[static_cast<std::size_t>(mage)][0] == Ability::kNone);
		}
	}

	void DebugMode()
	{
		for(const float heading : {0.0f,1.5707963f,3.1415927f,4.7123890f}) {
			const auto at=DebugAbility::Aim(10,20,30,heading);
			CHECK(std::abs(std::hypot(at[0]-10,at[1]-20)-700.0f)<0.001f);
			CHECK(at[2]==30);
			CHECK(std::abs((at[0]-10)*std::cos(heading)-(at[1]-20)*std::sin(heading))<0.001f);
		}
		Book book;
		book.Meet(71, 20);
        CHECK(book.UnlockSkald(71)==Block::kNone); // entry cost is paid before Debug Mode
		const auto original=*book.Find(71);
		CHECK(book.SetUnlimited(71,true)==Block::kNone);
		// Every node can be tested at a low level, including branches before their parents.
		for(std::size_t r=0;r<kRoles;++r) {
			const auto role=static_cast<Role>(r);
			const auto& path=PathOf(role);
			for(std::size_t i=kNodes;i-- > 0;) {
				if(path[i].Empty()) continue;
				for(int rank=path[i].base;rank<path[i].most;++rank) CHECK(book.Learn(71,role,i)==Block::kNone);
				CHECK(book.Learn(71,role,i)==Block::kMaxed);
				CHECK(book.Unspent(71)==kUnlimited);
			}
		}
		const auto debug=*book.Find(71);
		Book loaded;
		loaded.Load(book.Records(),book.KeptAside());
		CHECK(loaded.Unlimited(71));
		CHECK(loaded.Find(71)->ranks==debug.ranks);
		CHECK(loaded.SetUnlimited(71,false)==Block::kNone);
		CHECK(*loaded.Find(71)==original);
		CHECK(loaded.Unspent(71)==0);
	}

	void Unlimited()
	{
		const auto tank = Role::kTank;
		const auto t = static_cast<std::size_t>(tank);
		auto       record = Fresh();
		record.peak = 50;
		Learn(record, tank, At(tank, Perk::kTaunt), 10);
		const auto before = record;
		// Unlimited points: everything bought, Chaos Down and Stand Fast carried.
		for (std::size_t i = 0; i < kNodes; ++i) {
			for (int k = 0; k < 3; ++k) {
				Learn(record, tank, i, kUnlimited);
			}
		}
		CHECK(Spent(record, tank) == Mastery(tank));
		CHECK(Equip(record, tank, 1, Ability::kChaosDown) == Block::kNone);
		CHECK(Equip(record, tank, 0, Ability::kStandFast) == Block::kNone);
		record.peak = 54;
		Restore(record, before);
		CHECK(Spent(record) == 1);
		CHECK(Rank(record, tank, Ability::kTaunt) == 1);
		CHECK(Rank(record, tank, Ability::kChaosDown) == 0);
		CHECK(record.peak == 54);
		// Both carried abilities were added in the meantime: back to the two defaults.
		CHECK(record.slots[t][0] == Ability::kTaunt && record.slots[t][1] == Ability::kNone);
		// A default carried throughout stays where it is.
		auto kept = before;
		Learn(kept, tank, At(tank, Perk::kChaosDown), kUnlimited);
		for (const auto perk : { Perk::kTauntBrace, Perk::kTauntBrace, Perk::kIronHide, Perk::kIronHide, Perk::kChaosDown }) {
			Learn(kept, tank, At(tank, perk), kUnlimited);
		}
		Equip(kept, tank, 0, Ability::kBodyguard);
		Equip(kept, tank, 1, Ability::kChaosDown);
		Restore(kept, before);
		CHECK(kept.slots[t][0] == Ability::kTaunt && kept.slots[t][1] == Ability::kNone);
	}
}

int main()
{
    CHECK(FWM::Paths::Info(FWM::Paths::Ability::kPinningShot).name == "Windstep");
    CHECK(FWM::Paths::Value(FWM::Paths::Perk::kPinningShot, 1) == 2);
    CHECK(FWM::Paths::Value(FWM::Paths::Perk::kPinningShot, 3) == 4);
    CHECK(FWM::Paths::Value(FWM::Paths::Perk::kPinningHeavy, 3) == 3);
    SkaldProgression();
    SkaldLuteEntry();
    SkaldNumbers();
    SkaldPositions();
    SkaldWaves();
    MageRework();
	TraitLevels();
	AbilityLevels();
	ProgressionMigration();
	SteadyBreathRules();
	ArcherTraitRules();
	ChaosDownRules();
	Unlimited();
	DebugMode();
	Capstone();
	Shape();
	Words();
	Points();
	Learning();
	Slots();
	Repairs();
	Cooldowns();
	Codec();
	Bonus();
	Companions();
	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
