"""Build an isolated audit of the actual runtime snapshot and direct-damage code."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
out = root / 'build/party-balance-audit'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'src/PartyBalance.cpp').read_text()
source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
harness = r'''
#include "FWM/PartyBalancePolicy.h"
#include "FWM/SkaldPolicy.h"
#include <atomic>
#include <memory>
#include <vector>
#include <string_view>
#include <cstdio>
#include <cstdint>
using namespace std::literals;
namespace RE {
using FormID=std::uint32_t;
enum class ActorValue {kHealth};
struct Actor;
struct ActorHandle {Actor* p{}; struct Ptr {Actor* p; Actor* get()const{return p;} Actor* operator->()const{return p;} explicit operator bool()const{return p!=nullptr;}}; Ptr get()const{return {p};}};
struct Actor {
 FormID id{};bool dead{},disabled{},loaded{true},combat{true},bleed{},commanded{},player{};float distance{},health{1000};int cell{1},world{1};
 bool IsDead()const{return dead;}bool Is3DLoaded()const{return loaded;}bool IsInCombat()const{return combat;}
 bool IsBleedingOut()const{return bleed;}bool IsPlayerRef()const{return player;}bool IsCommandedActor()const{return commanded;}
 Actor* AsActorState(){return this;}Actor* AsActorValueOwner(){return this;}FormID GetFormID()const{return id;}
 int GetParentCell()const{return cell;}int GetWorldspace()const{return world;}
 void DamageActorValue(ActorValue,float x){health-=x;}
};
struct PlayerCharacter {static inline Actor* player{};static Actor* GetSingleton(){return player;}};
}
namespace FWM {
namespace Settings {struct Data {bool enabled{true},partyBalance{true};float followerDamage{100};float partyExtraPenalty{5},partyAdvantagePenalty{10},partyMinimumDamage{50};};inline Data cfg;const Data& Get(){return cfg;}}
namespace Host {inline bool abilities=true;bool Abilities(){return abilities;}}
namespace Game {bool Usable(RE::Actor* a){return a&&!a->dead&&!a->disabled&&a->loaded;}float Current(RE::Actor* a,RE::ActorValue){return a->health;}float Distance(RE::Actor* a,RE::Actor*){return a->distance;}}
namespace Skald {inline int calls=0;float Outgoing(RE::Actor*,RE::Actor*,float amount,bool,bool){++calls;return amount;}}
namespace Log {template<class... T>void Info(std::string_view,T...) {}}
namespace Party {
 struct Member {RE::ActorHandle handle;RE::FormID id;bool summon{};};inline std::vector<Member> members;
 const auto& Members(){return members;}
 bool IsPartyActor(RE::Actor* a){return a->player||std::any_of(members.begin(),members.end(),[&](auto& m){return m.handle.p==a;});}
}
namespace Enemies {struct Profile{RE::ActorHandle handle;};inline std::vector<Profile> enemies;const auto& All(){return enemies;}}
}
__SOURCE__
int main(){
 using namespace FWM;int failures=0;
 auto check=[&](bool pass,const char* why){if(!pass){++failures;std::printf("FAIL: %s\n",why);}};
 auto near=[](float a,float b){return std::abs(a-b)<.0001f;};
 RE::Actor player;player.id=20;player.player=true;RE::PlayerCharacter::player=&player;
 RE::Actor a[8];for(int i=0;i<8;++i)a[i].id=static_cast<RE::FormID>(i+100);
 for(int i=0;i<4;++i)Party::members.push_back({{&a[i]},a[i].id,false});
 Enemies::enemies={{{&a[4]}}};
 PartyBalance::Update(0);
 check(near(PartyBalance::Multiplier(&a[0],&a[4]),.55f),"runtime four versus one");
 check(near(PartyBalance::Damage(&a[0],&a[4],100),55)&&near(a[4].health,945),"direct damage reduced exactly once");
 check(PartyBalance::Multiplier(&player,&a[4])==1,"player unaffected");
 check(PartyBalance::Multiplier(&a[4],&a[0])==1,"enemy damage unaffected");
 check(PartyBalance::Multiplier(&a[0],&a[0])==1,"self costs unaffected");
 check(PartyBalance::Damage(&a[0],&a[4],-20)==0,"healing cannot become damage");
 check(PartyBalance::Damage(&a[0],&a[4],NAN)==0,"invalid amount rejected");
 Settings::cfg.partyBalance=false;
 check(PartyBalance::Multiplier(&a[0],&a[4])==1,"disable takes effect at hit time");
 PartyBalance::Update(0);Settings::cfg.partyBalance=true;
 check(PartyBalance::Multiplier(&a[0],&a[4])==1,"disabled snapshot cleared");
 PartyBalance::Update(0);Enemies::enemies.push_back({{&a[5]}});PartyBalance::Update(1);
 check(near(PartyBalance::Multiplier(&a[0],&a[4]),.65f),"new enemies relieve penalty");
 a[1].bleed=true;a[2].distance=5000;a[3].combat=false;PartyBalance::Update(.1f);
 check(PartyBalance::Multiplier(&a[0],&a[4])==1,"incapacitated distant idle followers excluded; solo immediately full damage");
 check(PartyBalance::Multiplier(&a[1],&a[4])==1,"inactive follower removed from snapshot");
 a[1].bleed=false;a[2].distance=0;a[3].combat=true;
 Party::members.push_back(Party::members[0]);PartyBalance::Clear();PartyBalance::Update(0);
 check(near(PartyBalance::Multiplier(&a[0],&a[4]),.65f),"duplicate roster entries not counted twice");
 Party::members.pop_back();Party::members[1].summon=true;a[2].commanded=true;a[3].cell=2;a[3].world=2;
 PartyBalance::Update(1);check(PartyBalance::Multiplier(&a[0],&a[4])==1,"summons and different spaces excluded");
 Host::abilities=false;PartyBalance::Update(0);check(PartyBalance::Multiplier(&a[0],&a[4])==1,"core does not own a second scaler");
 Host::abilities=true;Party::members[1].summon=false;a[2].commanded=false;a[3].cell=1;a[3].world=1;
 PartyBalance::Update(0);PartyBalance::Clear();check(PartyBalance::Multiplier(&a[0],&a[4])==1,"load/reset clears runtime state");
 Settings::cfg.partyBalance=false;Settings::cfg.followerDamage=20;PartyBalance::Update(0);
 check(near(PartyBalance::Multiplier(&a[0],&a[4]),.2f),"master works with party scaling off");
 a[0].combat=false;PartyBalance::Update(0);check(near(PartyBalance::Multiplier(&a[0],&a[4]),.2f),"first hit from an idle follower is scaled");
 check(PartyBalance::Multiplier(&player,&a[4])==1 && PartyBalance::Multiplier(&a[4],&a[0])==1,"master excludes player and enemies");
 Settings::cfg.followerDamage=0;const float hp=a[4].health;check(PartyBalance::Damage(&a[0],&a[4],100)==0 && a[4].health==hp,"zero affects direct ability damage");
 a[0].combat=true;Settings::cfg.partyBalance=true;Settings::cfg.followerDamage=50;PartyBalance::Clear();PartyBalance::Update(0);
 check(near(PartyBalance::Multiplier(&a[0],&a[4]),.325f),"master combines once with automatic scaling below floor");
 Party::members.resize(1);PartyBalance::Update(0);check(near(PartyBalance::Multiplier(&a[0],&a[4]),.5f),"master applies to a single follower");
 Settings::cfg.enabled=false;check(PartyBalance::Multiplier(&a[0],&a[4])==1,"mod disabled bypasses master immediately");
 std::printf("Production party balance audit: %s\n",failures?"FAIL":"PASS");return failures?1:0;
}
'''.replace('__SOURCE__', source)
(out / 'Audit.cpp').write_text(harness)
(out / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.20)
project(PartyBalanceAudit LANGUAGES CXX)
enable_testing()
add_executable(PartyBalanceAudit Audit.cpp)
target_compile_features(PartyBalanceAudit PRIVATE cxx_std_23)
target_include_directories(PartyBalanceAudit PRIVATE "{(root / 'include').as_posix()}")
add_test(NAME PartyBalanceRuntime COMMAND PartyBalanceAudit)
''')

# Every direct offensive health write must carry its source through the same runtime path.
for name in ('Arts', 'Blasts', 'Circles', 'Ripostes', 'ArrowArts', 'DragonBreaths', 'SpellArrows'):
    text = (root / 'src' / (name + '.cpp')).read_text()
    assert 'DamageActorValue(RE::ActorValue::kHealth' not in text, name
    assert 'PartyBalance::Damage(' in text, name
hooks = (root / 'src/PathBonus.cpp').read_text()
assert hooks.count('PartyBalance::Multiplier(') == 2
assert 'base->IsDetrimental()' in hooks and 'base->data.secondaryAV == RE::ActorValue::kHealth' in hooks
print('Generated production runtime audit; direct damage and both engine hooks are wired.')
