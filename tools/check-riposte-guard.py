"""Native audit of the production Riposte event gate and owned travel anchors.
Generate build/riposte-guard-audit, then configure/build/test with CMake/CTest.
"""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
out = root / 'build/riposte-guard-audit'
out.mkdir(parents=True, exist_ok=True)
riposte = (root / 'src/Ripostes.cpp').read_text(encoding='utf-8')
freeze = (root / 'src/Freeze.cpp').read_text(encoding='utf-8')

def method(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

# Counter must release before the normal attack; cancellation/expiry share Lower.
counter = method(riposte, 'bool Counter(')
assert counter.index('Stop(fighter->GetFormID())') < counter.index('NotifyAnimationGraph("attackStart")')
assert 'Lower(g)' in method(riposte, 'void Clear(')
assert 'Lower(g)' in method(riposte, 'void Stop(')
assert 'Lower(g)' in method(riposte, 'void Tick(')
assert 'FWM::Ripostes::Install();' in (root / 'paths/src/Main.cpp').read_text(encoding='utf-8')

harness = r'''
#include "FWM/RiposteGuard.h"
#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#include <cstdio>
namespace RE {
using FormID=unsigned;
using BSFixedString=std::string;
struct IAnimationGraphManagerHolder{virtual ~IAnimationGraphManagerHolder()=default;};
struct TESObjectREFR:IAnimationGraphManagerHolder{FormID id{};FormID GetFormID()const{return id;}};
struct Actor:TESObjectREFR {
 bool usable{true},loaded{true};std::vector<std::string>events;
 Actor* GetHandle(){return this;}bool Is3DLoaded()const{return loaded;}
 void NotifyAnimationGraph(std::string event){events.push_back(event);}
};
}
namespace FWM::Freeze {
struct Holding{RE::Actor*actor{};RE::FormID id{};unsigned owner{};};
std::vector<Holding>anchors;int publications{},motionStops{};
bool Usable(RE::Actor* a){return a && a->usable;}
void Publish(){++publications;}
void StopCarriedMotion(Holding&,RE::Actor*){++motionStops;}
__ANCHORS__
}
namespace FWM::Ripostes {
std::atomic<std::shared_ptr<const std::unordered_set<RE::FormID>>>guarded;
std::atomic<std::uint64_t>dodgeBlocks{};
constexpr unsigned guardAnchor=1;
int forwarded{};
bool original(RE::IAnimationGraphManagerHolder*,const RE::BSFixedString&){++forwarded;return true;}
struct Pointer {RE::Actor*value{};RE::Actor*get(){return value;}explicit operator bool()const{return value!=nullptr;}RE::Actor*operator->(){return value;}};
struct Handle {RE::Actor*value{};Pointer get(){return {value};}};
struct GuardState{Handle actor;RE::FormID id{};};
__METHODS__
}
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(false)
int main(){
 using namespace FWM;using namespace Ripostes;
 RE::Actor one,two;one.id=1;two.id=2;
 CHECK(Thunk(&one,"TKDodgeBack"));
 SetGuarded(1,true);Freeze::Anchor(&one,guardAnchor);
 CHECK(!Thunk(&one,"TKDodgeBack"));CHECK(!Thunk(&one,"DodgeLeft"));
 CHECK(Thunk(&two,"TKDodgeBack"));CHECK(dodgeBlocks==2);
 for(auto event:{"blockStart","blockStop","TKDodgeStop","TKDR_DodgeEnd","TKDR_DodgeStart","staggerStart","attackStart"})CHECK(Thunk(&one,event));
 // Refreshing a hold is idempotent; a simultaneous spell channel owns a separate hold.
 Freeze::Anchor(&one,guardAnchor);CHECK(Freeze::anchors.size()==1);
 Freeze::Anchor(&one,0);CHECK(Freeze::anchors.size()==2);
 GuardState guard{{&one},1};Lower(guard);
 CHECK(Thunk(&one,"TKDodgeBack"));CHECK(one.events.back()=="blockStop");
 CHECK(Freeze::anchors.size()==1 && Freeze::anchors.front().owner==0);
 Freeze::ReleaseAnchor(&one,0);CHECK(Freeze::anchors.empty());
 // Missing 3D and stale handles must still clear the transient event restriction.
 SetGuarded(1,true);Freeze::Anchor(&one,guardAnchor);one.loaded=false;Lower(guard);
 CHECK(Thunk(&one,"DodgeLeft"));CHECK(Freeze::anchors.empty());
 SetGuarded(1,true);GuardState stale{{nullptr},1};Lower(stale);CHECK(Thunk(&one,"DodgeLeft"));
 SetGuarded(1,true);SetGuarded(2,true);Lower(guard);
 CHECK(Thunk(&one,"TKDodgeForward"));CHECK(!Thunk(&two,"TKDodgeForward"));
 std::puts("Riposte gate: guarded-only dodge suppression, event cleanup, release, stale handles and independent anchors passed.");
}
'''
harness = harness.replace('__ANCHORS__', method(freeze, '    void Anchor(') + '\n' + method(freeze, '    void ReleaseAnchor('))
harness = harness.replace('__METHODS__', '\n'.join(method(riposte, sig) for sig in ('void SetGuarded(', ' static bool Thunk(', 'void Lower(')))
(out / 'Audit.cpp').write_text(harness, encoding='utf-8')
(out / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.20)
project(RiposteGuardAudit LANGUAGES CXX)
enable_testing()
add_executable(RiposteGuard Audit.cpp)
target_compile_features(RiposteGuard PRIVATE cxx_std_23)
target_include_directories(RiposteGuard PRIVATE "{(root / 'include').as_posix()}")
add_test(NAME RiposteGuardLifecycle COMMAND RiposteGuard)
''', encoding='utf-8')
print('Generated production Riposte dodge-gate and owned-anchor audit.')
