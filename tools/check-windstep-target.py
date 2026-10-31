"""Generate a native test of the production Windstep target registration/cleanup.
Fake combat groups simulate the engine forgetting a hidden target between ticks.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
out=root/'build/windstep-target-audit';out.mkdir(parents=True,exist_ok=True)
s=(root/'src/WindstepDecoys.cpp').read_text(encoding='utf-8')
methods=s[s.index('bool Engage('):s.index('bool Is(',s.index('void Drop('))]
harness=r'''
#include <algorithm>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <cstdio>
namespace RE {
using FormID=unsigned;
struct Actor;
struct ActorHandle {
 std::shared_ptr<Actor> p;
 auto get() const {return p;}
 explicit operator bool() const {return bool(p);}
 bool operator==(const ActorHandle&)const=default;
};
template<class T>using NiPointer=std::shared_ptr<T>;
struct TESForm{};
struct Cell:TESForm{bool IsInteriorCell()const{return true;}};
struct World:TESForm{};
struct Vec{float x{},y{},z{};bool operator==(const Vec&)const=default;};
struct CombatTarget {
 enum class Flags{ kTargetKnown=1,kTargetLost=2 };
 struct FlagSet {int value{};void set(Flags f){value|=int(f);}void reset(Flags f){value&=~int(f);}} flags;
 ActorHandle targetHandle;int detectLevel{};float stealthPoints{};
 struct {Vec pos;TESForm* space{};} lastKnownLoc;
 struct {float timeStamp{};} lastKnownTimeStamp;
};
struct Group {std::mutex lock;std::vector<CombatTarget> targets;};
struct Controller {Group* combatGroup{};};
struct Actor:std::enable_shared_from_this<Actor>{
 FormID id{};Controller* controller{};Cell cell;World world;Vec pos{10,20,30};bool disabled{},deleted{};int starts{};
 ActorHandle GetHandle(){return {shared_from_this()};}
 FormID GetFormID()const{return id;}
 Vec GetPosition()const{return pos;}
 Cell* GetParentCell(){return &cell;} World* GetWorldspace(){return &world;}
 bool StartCombat(Actor*){++starts;return true;}
 void StopCombat(){}void Disable(){disabled=true;}void SetDelete(bool v){deleted=v;}
};
using BSWriteLockGuard=std::unique_lock<std::mutex>;
using BSReadLockGuard=std::unique_lock<std::mutex>;
struct AITimer{static float QTimer(){return 42;}};
}
namespace FWM::Game {
bool Usable(RE::Actor* a){return a&&!a->disabled;}
RE::Controller* Controller(RE::Actor* a){return a?a->controller:nullptr;}
}
namespace FWM::Overrides {
bool Takes(RE::Controller* c,RE::ActorHandle h){return c&&c->combatGroup&&std::ranges::any_of(c->combatGroup->targets,[&](const auto& t){return t.targetHandle==h;});}
}
namespace FWM::Abilities {int drops{};void DropHoldsOn(RE::ActorHandle){++drops;}}
namespace FWM::WindstepDecoys {
struct Decoy{RE::NiPointer<RE::Actor> actor;std::vector<RE::ActorHandle> enemies;};
struct Spare{RE::ActorHandle actor;RE::FormID id{};};
std::shared_mutex gate;std::unordered_map<RE::FormID,Decoy> active;std::vector<Spare> spares;
bool Is(RE::FormID id){return active.contains(id);}
__METHODS__
}
int main(){using namespace FWM;int failures=0;auto check=[&](bool ok,const char* text){if(!ok){++failures;std::printf("FAIL: %s\n",text);}};
 RE::Group group;RE::Controller controller{&group};
 auto enemy=std::make_shared<RE::Actor>();enemy->id=1;enemy->controller=&controller;
 auto caster=std::make_shared<RE::Actor>();caster->id=2;
 auto echo=std::make_shared<RE::Actor>();echo->id=3;
 group.targets.push_back(RE::CombatTarget{});group.targets.back().targetHandle=caster->GetHandle();
 auto handle=echo->GetHandle();WindstepDecoys::active.emplace(3,WindstepDecoys::Decoy{echo,{}});
 check(WindstepDecoys::Engage(enemy.get(),echo.get()),"original pursuer accepts active echo");
 check(Overrides::Takes(&controller,handle),"echo registered for core target selector");
 check(group.targets.size()==2&&enemy->starts==1,"original target preserved and combat started once");
 group.targets.back().flags.value=2;group.targets.back().detectLevel=-100;
 check(WindstepDecoys::Engage(enemy.get(),echo.get()),"sustain hidden target");
 check(group.targets.size()==2&&enemy->starts==1,"sustain does not duplicate target or restart attack");
 check(group.targets.back().flags.value==1&&group.targets.back().detectLevel==1000,"lost detection refreshed while illusion exists");
 check(group.targets.back().lastKnownLoc.pos==echo->pos,"AI knows where stationary echo is");
 group.targets.pop_back();
 check(WindstepDecoys::Engage(enemy.get(),echo.get())&&Overrides::Takes(&controller,handle),"engine dropping echo is repaired next tick");
 check(WindstepDecoys::active.at(3).enemies.size()==1,"cleanup records each enemy once");
 WindstepDecoys::Drop(handle);
 check(!handle&&echo->disabled&&!echo->deleted&&!WindstepDecoys::active.contains(3),"expiry disables the echo without deleting it and clears handle");
 check(WindstepDecoys::spares.size()==1&&WindstepDecoys::spares[0].id==3,"expired echo is kept as a spare");
 check(group.targets.size()==1&&group.targets[0].targetHandle==caster->GetHandle(),"expiry restores ordinary target list");
 check(Abilities::drops==1,"expiry releases forced hold");
 check(!WindstepDecoys::Engage(enemy.get(),echo.get()),"expired echo cannot regain enemies");
 std::printf("Windstep target lifecycle: %d failures\n",failures);return failures?1:0;
}
'''.replace('__METHODS__',methods)
(out/'Audit.cpp').write_text(harness,encoding='utf-8')
(out/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(WindstepTargetAudit LANGUAGES CXX)
enable_testing()
add_executable(WindstepTargetAudit Audit.cpp)
target_compile_features(WindstepTargetAudit PRIVATE cxx_std_23)
add_test(NAME WindstepTargetLifecycle COMMAND WindstepTargetAudit)
''',encoding='utf-8')
print('Generated tests using production Engage and Drop methods.')
