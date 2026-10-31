#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace FWM::SkaldPolicy {
inline constexpr int kUnlockCost = 10;
inline constexpr float kNoteSlow = .30f;
inline constexpr float kNoteSlowSeconds = 1.f;
inline constexpr float kNoteReach = 800.f;
inline constexpr float kNoteSpeed = 320.f;
inline constexpr float kNoteLifetime = 4.f;
// A bard fights like a healer, inside the leash Fight With Me keeps around the
// player for its position. The Single Note is played on the move, giving ground to
// enemies; a Ljodh stands still, and stops to walk back once the player is well
// out of reach.
inline constexpr float kLeashSlack = 150.f;     // past the leash before it leaves to rejoin
inline constexpr float kLeashInside = 50.f;     // inside the leash before it plays again
inline constexpr float kRejoinProgress = 100.f; // closer than this counts as getting back
inline constexpr float kRejoinPatience = 8.f;   // seconds without that before giving up
inline constexpr float kRejoinRest = 10.f;      // then it plays where it is this long
inline constexpr float kFollowProgress = 60.f;  // own travel that counts as moving
inline constexpr float kFollowStall = 4.f;      // seconds without it before the graph is reset
inline constexpr float kUnstickInterval = 8.f;
inline bool LeaveToRejoin(float distance,float radius){return radius>0.f&&distance>radius+kLeashSlack;}
inline bool BackInLeash(float distance,float radius){return radius<=0.f||distance<=radius-kLeashInside;}
inline float NoteInterval(float roll){return 1.f+2.f*std::clamp(roll,0.f,1.f);}
// A mage keeps its distance, sidesteps and gives ground because the combat AI holds
// a spell for it. A bard playing on the move holds Lightning Bolt the same way: lent
// for the fight, drawn but never cast, its hands' glow hidden.
inline constexpr std::uint32_t kStanceSpell = 0x2DD29;
inline constexpr float kStrumIn = .4f;      // after the arms come in, before the first note on the move
inline constexpr float kStrumReport = 6.f;  // how often the log notes where a playing bard keeps
enum class PropAction { Ready, Retry, Wait, Resume, Abort };
struct PropRecovery {
    bool tried{}, waiting{};float elapsed{};
    PropAction Next(bool attached,float seconds){
        if(attached){if(waiting){waiting=false;return PropAction::Resume;}return PropAction::Ready;}
        if(!tried){tried=true;waiting=true;elapsed=0;return PropAction::Retry;}
        if(!waiting)return PropAction::Abort;
        elapsed+=std::max(0.f,seconds);
        return elapsed>=1.25f?PropAction::Abort:PropAction::Wait;
    }
};
// A modded sheathe animation can take longer than vanilla. An idle handed over
// while the bard was still walking can stay "current" without the graph ever
// entering it, and later requests for the same idle then do nothing. So each
// attempt clears the current idle first, then asks once, alternating the AI
// idle and the graph event, about a second apart.
inline constexpr float kLuteStartup = 6.f;
inline constexpr float kLuteAttempt = 1.f;
enum class LuteEntryAction { Wait, Sheathe, Reset, Idle, Graph, Timeout };
struct LuteEntry {
    float nextSheathe{}, nextTry{}, askedAt{-1.f};
    unsigned requests{};
    bool cleared{};
    LuteEntryAction Next(float age, bool sheathed, bool sheathing, bool prop) {
        if(age>=kLuteStartup)return LuteEntryAction::Timeout;
        if(prop)return LuteEntryAction::Wait;
        if(!sheathed) {
            // A redraw restarts the attempt once the hands are empty again.
            cleared=false;askedAt=-1.f;
            if(!sheathing && age>=nextSheathe){nextSheathe=age+1.5f;return LuteEntryAction::Sheathe;}
            return LuteEntryAction::Wait;
        }
        if(askedAt>=0.f && age-askedAt<kLuteAttempt)return LuteEntryAction::Wait;
        if(age<nextTry)return LuteEntryAction::Wait;
        if(!cleared){cleared=true;nextTry=age+.1f;return LuteEntryAction::Reset;}
        cleared=false;askedAt=age;++requests;
        return requests%2==1?LuteEntryAction::Idle:LuteEntryAction::Graph;
    }
};
inline constexpr float kFistReach = 180.0f;
inline constexpr float kPressureReach = 300.0f;
inline float TimidDamage(float damage){return std::min(1.f,std::max(0.f,damage)*.01f);}
inline constexpr float kReach = 1000.0f;
inline constexpr float kWaveSpeed = 800.0f;
inline constexpr float kWaveSeconds = kReach / kWaveSpeed;
inline constexpr float kWaveMeshRadius = 100.0f;
inline constexpr float kWaveContactRadius = 24.0f;
inline constexpr float kWaveHeight = 120.0f;
inline constexpr std::size_t kWaveSlots = 3;
inline constexpr float kHitSeconds = 0.72f;
inline constexpr float kHitRadius = 48.0f;
inline constexpr std::size_t kBardMotes = 96;
inline constexpr std::size_t kHitMotes = 64;
inline constexpr std::size_t kWaveMotes = 96;
inline constexpr std::size_t kHitBursts = 2;
inline constexpr float kHitParticleSeconds = 1.5f;
inline float Smooth(float x){x=std::clamp(x,0.f,1.f);return x*x*(3.f-2.f*x);}
inline float RankGlow(int rank){return rank>=3?1.6f:rank==2?1.f:.65f;}
inline float RankSpread(int rank){return rank>=3?1.22f:rank==2?1.f:.88f;}
inline std::size_t BardCount(int rank){return rank>=3?kBardMotes:rank==2?48:24;}
inline std::size_t HitCount(int rank){return rank>=3?kHitMotes:rank==2?32:16;}
inline std::size_t WaveCount(int rank){return rank>=3?kWaveMotes:rank==2?48:24;}
inline float RecipientAura(int rank,float age){
    if(age<0.f||age>=kHitParticleSeconds)return 0.f;
    return Smooth(age/.08f)*(1.f-Smooth((age-.35f)/(kHitParticleSeconds-.35f)))*(rank>=3?.85f:rank==2?.32f:.10f);
}
struct ParticleFrame {float x{},y{},z{},alpha{},scale{1.f};};
inline ParticleFrame RankMote(ParticleFrame mote,int rank){
    const float spread=RankSpread(rank);mote.x*=spread;mote.y*=spread;mote.z*=1.f+(spread-1.f)*.35f;
    mote.alpha*=RankGlow(rank);mote.scale*=spread;return mote;
}
inline ParticleFrame BardMote(int song,std::size_t index,float time,float beatAge){
    if(time<0.f)return {};
    const float i=static_cast<float>(index),life=3.4f+static_cast<float>(index%5)*.27f;
    const float u=std::fmod(time/life+i*.618034f+song*.13f,1.f);
    const float angle=i*2.399963f+time*(song==2?-.32f:.25f)+.3f*std::sin(time*.9f+i);
    const float radius=18.f+static_cast<float>(index%9)*5.f+12.f*std::sin(u*3.141593f);
    const float beat=beatAge<0.f?0.f:std::exp(-beatAge*5.f);
    const float alpha=Smooth(u/.08f)*(1.f-Smooth((u-.72f)/.28f))*(.72f+.28f*std::sin(time*2.f+i))*(.8f+.2f*beat);
    return {std::cos(angle)*radius,std::sin(angle)*radius,8.f+140.f*u+6.f*(1.f+std::sin(u*4.f+i)),
        alpha,.65f+static_cast<float>(index%4)*.18f+.12f*beat};
}
inline ParticleFrame HitMote(int song,std::size_t index,float age){
    const float i=static_cast<float>(index),delay=static_cast<float>(index%5)*.02f,life=1.1f+static_cast<float>(index%4)*.09f;
    const float u=(age-delay)/life;if(u<0.f||u>=1.f)return {};
    const float angle=i*2.399963f+(song==2?-1.f:1.f)*u*.8f+.25f*std::sin(u*3.f+i);
    const float radius=10.f+static_cast<float>(index%5)*5.f+15.f*u+5.f*std::sin(u*3.141593f);
    const float alpha=Smooth(u/.09f)*(1.f-Smooth((u-.65f)/.35f))*(.8f+.2f*std::sin(i+u*5.f));
    return {std::cos(angle)*radius,std::sin(angle)*radius,4.f+u*(60.f+static_cast<float>(index%6)*18.f),
        alpha,.75f+static_cast<float>(index%3)*.18f};
}
inline float LuteVolume(bool note){return note?.16f:.7f;}
struct SpectralFrame {float x{},y{},z{},angle{},scale{},glow{},scroll{};};
inline SpectralFrame LuteVeil(float time,float accent){
    return {1.3f*std::sin(time*2.1f),.8f*std::cos(time*1.7f),.6f*std::sin(time*2.6f),
        .025f*std::sin(time*1.8f),1.035f+.012f*std::sin(time*2.3f),
        .65f+.12f*std::sin(time*2.f)+accent*.25f,time*.065f+.04f*std::sin(time*1.4f)};
}
inline float HitAlpha(float age) {
    if(age<0.f||age>=kHitSeconds)return 0.f;
    return Smooth(age/.045f)*(1.f-Smooth((age-.13f)/(kHitSeconds-.13f)));
}
// Own only the temporary runtime multiplier. An outside edit becomes the new base.
// A single active flag represents any number of audible performers.
struct MusicDuck {
    float base{1.f},last{1.f},gain{1.f}; bool owned{};
    float Update(float observed,bool active,float dt){
        if(!owned){if(!active)return observed;base=observed;last=observed;gain=1.f;owned=true;}
        if(std::abs(observed-last)>.0001f)base=observed;
        const float target=active?.28f:1.f,step=.72f*std::max(0.f,dt)/(active?.20f:.65f);
        gain+=std::clamp(target-gain,-step,step);
        last=base*gain;
        if(!active&&gain>=1.f){owned=false;last=base;}
        return last;
    }
    float Restore(float observed){const float result=owned&&std::abs(observed-last)<=.0001f?base:observed;*this={};return result;}
};
inline constexpr std::array<unsigned,3> kStackHits{3,5,10};
// Stacks contribute 1 + .5 + .5. Each contribution keeps its own Old Tongue roll.
struct WaveStacks {
    unsigned hits{},count{},empowered{};
    float factor{};
    bool NextIsStack() const { return count<kStackHits.size() && hits+1==kStackHits[count]; }
    bool Hit(bool doubled) {
        if(hits>=kStackHits.back())return false;
        ++hits;
        if(count>=kStackHits.size()||hits!=kStackHits[count])return false;
        factor+=(count==0?1.f:.5f)*(doubled?2.f:1.f);
        if(doubled)empowered|=1u<<count;
        ++count;return true;
    }
};
struct WaveContact { float distance{}; bool seen{},hit{}; };
inline float WaveRadius(float age) { return std::clamp(age*kWaveSpeed,0.f,kReach); }
inline float WaveAlpha(float age) {
    auto smooth=[](float x){x=std::clamp(x,0.f,1.f);return x*x*(3.f-2.f*x);};
    return smooth(age/.06f)*(1.f-smooth((WaveRadius(age)/kReach-.78f)/.22f));
}
inline ParticleFrame WaveMote(int song,std::size_t index,float age,int rank){
    if(index>=WaveCount(rank)||age<0.f||age>=kWaveSeconds)return {};
    const float i=static_cast<float>(index),angle=i*2.399963f+(song==2?-.18f:.18f)*age+.035f*std::sin(age*3.f+i);
    // A broad, irregular wake of sparks rather than a geometric line around a ring.
    const float fraction=.25f+static_cast<float>(index%13)/12.f*.70f+.025f*std::sin(i*3.7f+age*2.5f);
    const float radius=WaveRadius(age)*fraction;
    const float drift=std::sin(i*2.1f+age*2.f);
    return {std::cos(angle)*radius,std::sin(angle)*radius,8.f+age*(18.f+static_cast<float>(index%7)*4.f)+12.f*drift*drift,
        WaveAlpha(age)*Smooth((age-static_cast<float>(index%7)*.018f)/.16f)*(.7f+.3f*drift),
        (1.f+static_cast<float>(index%4)*.18f)*RankSpread(rank)};
}
// Sweep the front, including a moving recipient, so frame rate cannot duplicate or skip hits.
inline bool CrossWave(WaveContact& contact,float before,float after,float distance,bool eligible) {
    if(!std::isfinite(distance)||distance<0.f)return false;
    const bool swept=distance+kWaveContactRadius>=before && distance-kWaveContactRadius<=after;
    const bool crossed=contact.seen && contact.distance-kWaveContactRadius>before && distance-kWaveContactRadius<=after;
    contact.distance=distance;contact.seen=true;
    if(contact.hit||!eligible||distance>kReach||after<before||(!swept&&!crossed))return false;
    contact.hit=true;return true;
}
inline constexpr std::uint32_t kLute = 0xDABAB;
enum class Song : std::uint8_t { Fervor, Echoes, Hollow, Glory };
constexpr float Rank(int rank, float a, float b, float c) { return rank <= 0 ? 0.f : rank == 1 ? a : rank == 2 ? b : c; }
constexpr float Speed(int r) { return Rank(r,.10f,.15f,.20f); }
constexpr float Recovery(int r) { return Rank(r,.25f,.40f,.60f); }
constexpr float Weakness(int r) { return Rank(r,.10f,.15f,.20f); }
constexpr float Critical(int r) { return Rank(r,.25f,.40f,.60f); }
constexpr float Chance(int r) { return Rank(r,.05f,.10f,.15f); }
constexpr float Duration(Song song,int lingering) { return (song==Song::Glory?10.f:8.f)*(1.f+Rank(lingering,.15f,.30f,.45f)); }
constexpr float Cooldown(Song song) { return song==Song::Echoes?45.f:song==Song::Glory?50.f:35.f; }
inline float DamageShare(float percent) { return std::isfinite(percent)?std::clamp(percent,0.f,100.f)/100.f:1.f; }
inline float Advance(float seconds,float recovery) { return std::max(0.f,seconds)*(1.f+std::clamp(recovery,0.f,2.4f)); }
inline float Absorb(float damage,float& shield) { const float taken=std::min(std::max(0.f,damage),std::max(0.f,shield));shield-=taken;return damage-taken; }
// Critical damage is an independent POTC roll on direct hits, never a recursive extra hit.
constexpr float CriticalMultiplier(float bonus) { return 1.5f+bonus; }

// Every Single Note that lands slows its target and adds a stack of one random
// discord. Hits of the same kind build to five stacks and refresh its clock.
enum class Discord : std::uint8_t { Weakened, OffBeat, Exposed, Winded, Muddled, Jarred, Count };
inline constexpr std::size_t kDiscords = static_cast<std::size_t>(Discord::Count);
inline constexpr unsigned kDiscordStacks = 5;
inline constexpr float kDiscordSeconds = 10.f;
inline constexpr float kJarredCooldown = 5.f;
inline constexpr float kJarredStagger = .25f;
inline constexpr float kRingVolume = .45f;
inline float DiscordShare(unsigned stacks,float each){return each*static_cast<float>(std::min(stacks,kDiscordStacks));}
inline float DiscordWeaken(unsigned stacks){return DiscordShare(stacks,.04f);}   // less damage dealt
inline float DiscordOffBeat(unsigned stacks){return DiscordShare(stacks,.05f);}  // slower attacks
inline float DiscordExposed(unsigned stacks){return DiscordShare(stacks,.04f);}  // more damage taken
inline float DiscordDrain(unsigned stacks){return DiscordShare(stacks,.04f);}    // stamina or magicka taken by this hit
// Uniform among the kinds that can matter: no magicka drain on a target without
// magicka, and no second stagger inside its cooldown.
inline Discord PickDiscord(float roll,bool hasMagicka,bool canJar){
    std::array<Discord,kDiscords> pool{};std::size_t count=0;
    for(std::size_t i=0;i<kDiscords;++i){
        const auto kind=static_cast<Discord>(i);
        if((kind==Discord::Muddled&&!hasMagicka)||(kind==Discord::Jarred&&!canJar))continue;
        pool[count++]=kind;
    }
    const auto index=static_cast<std::size_t>(std::clamp(roll,0.f,.9999f)*static_cast<float>(count));
    return pool[std::min(index,count-1)];
}
// Now and then a landed note is a crescendo: one powerful hold on its target.
// Each enemy then has a stretch of immunity, so a volley cannot chain-lock it.
enum class Crescendo : std::uint8_t { Frozen, Paralyzed, Lullaby, Silenced, Count };
inline constexpr std::size_t kCrescendos = static_cast<std::size_t>(Crescendo::Count);
inline constexpr float kCrescendoChance = .08f;
inline constexpr float kCrescendoImmunity = 15.f;
inline float CrescendoSeconds(Crescendo kind){return kind==Crescendo::Frozen||kind==Crescendo::Paralyzed?3.f:6.f;}
inline bool CrescendoLands(float roll,float sinceLast){return sinceLast>=kCrescendoImmunity&&roll<kCrescendoChance;}
// Uniform; Silenced (last) only for a target with magicka to lose.
inline Crescendo PickCrescendo(float roll,bool hasMagicka){
    const std::size_t count=hasMagicka?kCrescendos:kCrescendos-1;
    const auto index=static_cast<std::size_t>(std::clamp(roll,0.f,.9999f)*static_cast<float>(count));
    return static_cast<Crescendo>(std::min(index,count-1));
}
// Overhead icon over a struck enemy: a quick fade in, a hold, then half a second
// of fade out. A crescendo's icon stays for as long as its hold lasts.
inline constexpr float kMarkSeconds = 1.1f;
inline constexpr float kMajorMarkScale = 1.5f;
inline float MarkAlpha(float age,float life=kMarkSeconds){
    if(age<0.f||age>=life)return 0.f;
    return std::min(Smooth(age/.12f),1.f-Smooth((age-(life-.5f))/.5f));
}
// Hits ring through a pentatonic phrase, so a volley sounds like a tune rather
// than one repeated pluck. Pitch is a playback-rate ratio, one octave at most.
inline constexpr std::array<int,8> kPhrase{0,4,7,9,12,9,7,4};
inline float NotePitch(unsigned index){return std::exp2(static_cast<float>(kPhrase[index%kPhrase.size()])/12.f);}
// The Single Note's spectral lute is white; a Ljodh paints it its own colour.
inline constexpr std::array<float,3> kNoteLute{.9f,.95f,1.f};
inline constexpr float kOverlayScale = 1.012f;
// Skyrim's lute is never drawn in a fight; the spectral lute is the instrument.
// It fades in over kLuteAppear once the hands have it.
inline constexpr float kLuteAppear = .25f;
inline float Appear(float since){return std::clamp(since/kLuteAppear,0.f,1.f);}
// A spectral lute let go of (a finished song, a swing) waits this long for the
// walk to carry it on.
inline constexpr float kSpareFor = 1.2f;

// The lute idle opens with Skyrim's (hidden) lute swinging in from behind the back.
// The spectral lute waits until it reaches the playing pose (within kOffPose of any
// recorded frame), or for kLiftIn when nothing has been recorded yet.
inline constexpr float kLiftIn = .8f;
inline constexpr float kOffPose = 18.f;
// A lute is only drawn near the chest that holds it; farther than this, it is a stray.
inline constexpr float kLuteReach = 150.f;
inline bool Astray(float distance){return distance>kLuteReach;}
// A hit can cut the lute idle short and leave Skyrim's lute on the bard with
// nothing animating it. Such a left-over is hidden. The exit still moves it at
// first, so only motion after kStraySettle means an idle is playing it again.
inline constexpr float kStraySettle = 1.5f;
inline constexpr float kStrayMoved = 1.f;
inline constexpr float kStrayStop = 3.f;      // how often Skyrim is asked to put it away
inline bool StrayMoving(float age,float moved){return age>=kStraySettle&&moved>kStrayMoved;}
// Swinging in or out, a lute moves far faster than it ever does while strummed.
inline constexpr float kSettledSpeed = 120.f;
inline bool Swinging(float moved,float seconds){return seconds>0.f&&moved>kSettledSpeed*seconds;}
// An overlay that has not appeared this long after the lute came up is spawned again, once.
inline constexpr float kOverlayRetry = 1.5f;
// Upper-body layer: the arms are recorded from the bard's own lute idle and
// replayed over the walk, so the lute stays up and playing while the legs step.
inline constexpr float kLayerRecord = 3.2f;   // seconds of playing kept
inline constexpr float kLayerMinimum = 2.4f;  // shortest usable recording
inline constexpr float kLayerSettle = 1.f;    // skip the idle's lead-in before recording
inline constexpr float kLayerSeam = .35f;     // crossfade where the loop wraps
inline constexpr float kLayerFade = .25f;     // blend in and out over the walk's own arms
struct LoopSample { float time{},other{},blend{}; };
// Plays [seam, length) on a loop; the last seam of each pass fades into the
// first, so the wrap lands where the next pass begins.
inline LoopSample LoopAt(float elapsed,float length){
    const float body=std::max(kLayerSeam,length-kLayerSeam);
    const float t=kLayerSeam+std::fmod(std::max(0.f,elapsed),body);
    if(t<=body)return {t,0.f,0.f};
    return {t,t-body,(t-body)/kLayerSeam};
}
// sinceStop below zero: the layer is still playing.
inline float LayerWeight(float sinceStart,float sinceStop){
    const float in=Smooth(sinceStart/kLayerFade);
    return sinceStop<0.f?in:in*(1.f-Smooth(sinceStop/kLayerFade));
}
}
