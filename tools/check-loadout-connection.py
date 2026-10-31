"""Exercise production pool-bridge methods with a simulated plugin loader.
Generate build/loadout-connection-audit, then configure/build/run with CMake/CTest.
The game and SKSE are not launched by this audit.
"""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
out = root / 'build/loadout-connection-audit'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'src/AbilityLink.cpp').read_text(encoding='utf-8')

def method(signature, text=source):
    start = text.index(signature)
    end = text.index('{', start)
    depth = 1
    end += 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

# Catch the original regression: consumers used a bridge that core never initializes.
plugin = (root / 'src/Plugin.cpp').read_text(encoding='utf-8')
assert 'AbilityLink::Connect();' in plugin
assert 'AbilityLink::SkaldUnlocked(a_id)' in (root / 'src/Party.cpp').read_text(encoding='utf-8')
for name in ('src/Loadouts.cpp', 'src/Spells.cpp'):
    text = (root / name).read_text(encoding='utf-8')
    assert 'AbilityLink::LoadoutMask(' in text and 'PathsLink::LoadoutMask(' not in text

harness = r'''
#include "FWM/Host.h"
#include "FWM/LoadoutPolicy.h"
#include <optional>
#include <string_view>
#include <cstdio>
#include <array>
#include <string>
#include <vector>
#include <cmath>
#include <limits>
#define FIGHTWITHMEPATHS_API
using namespace std::literals;
int scenario;
namespace RE { using FormID=std::uint32_t; }
namespace FWM::Paths { constexpr unsigned Signature() { return 123; } constexpr unsigned kNodes=15; enum class Block { kNone,kNoLute,kNoPoints,kBadNode }; }
namespace FightWithMePathsAPI {
__STATUS_WIRE__;
constexpr unsigned INTERFACE_VERSION = 6;
struct Level;
constexpr unsigned kSkaldUnlockedFlag=2;
constexpr unsigned kUnlimitedFlag=1;
struct Record { unsigned flags{};int earned{},spent{}; };
struct IPaths {
 unsigned learnedActor{},learnedRole{},learnedNode{};
 unsigned GetTreeSignature() { return scenario == 1 ? 999 : FWM::Paths::Signature(); }
 bool Read(unsigned actor, Record* out) { out->flags=actor==102?kSkaldUnlockedFlag:1;out->earned=40;out->spent=12;return actor!=103; }
 int Learn(unsigned actor,unsigned role,unsigned node) { learnedActor=actor;learnedRole=role;learnedNode=node;return static_cast<int>(actor==104?FWM::Paths::Block::kNoLute:actor==105?FWM::Paths::Block::kNoPoints:FWM::Paths::Block::kNone); }
};
}
namespace FWM::View { __VIEW_STATUS__; struct Member { unsigned id{};struct {bool skaldUnlocked{};}record;bool unlimited{};int pointsEarned{},pointsSpent{}; }; }
namespace FWM::Battle {
std::vector<View::Status> effects;
std::vector<View::Status> ActiveStatuses(unsigned) { return effects; }
}
__STATUS_EXPORT__
namespace FWM::Log {
template<class... T> void Info(T&&...) {}
template<class... T> void Warn(T&&...) {}
}
FightWithMePathsAPI::IPaths instance;
FightWithMePathsAPI::IPaths* GetInterface(unsigned version) {
 return version == FightWithMePathsAPI::INTERFACE_VERSION ? &instance : nullptr;
}
unsigned Mask(unsigned role) { return FWM::LoadoutPolicy::RoleWeapons(static_cast<FWM::Role>(role)); }
void* GetModuleHandleW(const wchar_t*) { return scenario == 0 ? nullptr : &instance; }
using Proc = void(*)();
Proc GetProcAddress(void*, const char* name) {
 if(std::string_view(name)=="FightWithMePaths_GetInterface")return reinterpret_cast<Proc>(&GetInterface);
 if(std::string_view(name)=="FightWithMePaths_GetLoadoutMask" && scenario != 2)return reinterpret_cast<Proc>(&Mask);
 if(std::string_view(name)=="FightWithMePaths_GetStatuses" && scenario == 3)return reinterpret_cast<Proc>(&FightWithMePaths_GetStatuses);
 return nullptr;
}
namespace FWM::AbilityLink {
FightWithMePathsAPI::IPaths* api{};
unsigned (*loadoutMask)(unsigned){};
bool (*isDecoy)(unsigned){};
bool (*isPerforming)(unsigned){};
bool (*levelOf)(unsigned, FightWithMePathsAPI::Level*){};
unsigned (*statusesOf)(unsigned, unsigned, FightWithMePathsAPI::Status*, unsigned){};
bool mismatched{};
__METHODS__
}
int main(int argc,char** argv) {
 scenario = argc > 1 ? argv[1][0]-'0' : 0;
 using namespace FWM;
 AbilityLink::Connect();
 if(AbilityLink::SkaldUnlocked(101) || AbilityLink::SkaldUnlocked(103))return 5;
 if(AbilityLink::SkaldUnlocked(102)!=(scenario==2 || scenario==3))return 6;
 const bool connected=scenario==2 || scenario==3;
 View::Member paid;paid.id=102;AbilityLink::SkaldProgress(paid);
 View::Member debug;debug.id=101;AbilityLink::SkaldProgress(debug);
 View::Member missing;missing.id=103;AbilityLink::SkaldProgress(missing);
 if(paid.record.skaldUnlocked!=connected || paid.unlimited || debug.record.skaldUnlocked || debug.unlimited!=connected)return 13;
 if(missing.pointsEarned || missing.unlimited || missing.record.skaldUnlocked)return 14;
 if(connected && (paid.pointsEarned!=40 || paid.pointsSpent!=12))return 15;
 if(AbilityLink::UnlockSkald(101)!=(connected?Paths::Block::kNone:Paths::Block::kBadNode))return 16;
 if(connected && (instance.learnedActor!=101 || instance.learnedRole!=static_cast<unsigned>(Role::kSkald) || instance.learnedNode!=Paths::kNodes))return 17;
 if(connected && (AbilityLink::UnlockSkald(104)!=Paths::Block::kNoLute || AbilityLink::UnlockSkald(105)!=Paths::Block::kNoPoints))return 18;
 Battle::effects={{"Fervor","2 / 3 stacks",0xEF10,View::Status::Tint::kBuff,7.5f,8.f,-1.f},
                  {"Rejuvenated","Magicka recovery",0xF5BB,View::Status::Tint::kMagicka,4.f,6.f,-1.f}};
 std::vector<View::Status> statuses;
 AbilityLink::Statuses(102,statuses);
 if(statuses.size()!=(scenario==3?2:0))return 7;
 if(scenario==3 && (statuses[0].name!="Fervor" || statuses[0].detail!="2 / 3 stacks" || statuses[0].left!=7.5f || statuses[1].tint!=View::Status::Tint::kMagicka))return 8;
 FightWithMePathsAPI::Status wire[2];wire[1].glyph=1234;
 if(FightWithMePaths_GetStatuses(102,sizeof(wire[0]),wire,1)!=1 || wire[1].glyph!=1234)return 9;
 if(FightWithMePaths_GetStatuses(102,1,wire,2)!=0 || FightWithMePaths_GetStatuses(102,sizeof(wire[0]),nullptr,2)!=0)return 10;
 Battle::effects.clear();statuses.clear();AbilityLink::Statuses(102,statuses);if(!statuses.empty())return 11;
 Battle::effects={{"Invalid","invalid timer",1,View::Status::Tint::kBuff,std::numeric_limits<float>::quiet_NaN()}};
 AbilityLink::Statuses(102,statuses);if(!statuses.empty())return 12;
 const auto mage=AbilityLink::LoadoutMask(Role::kMage);
 if(!Host::Abilities() && scenario != 3) {
  if(mage.has_value())return 1;
 } else {
  using namespace LoadoutPolicy;
  if(!mage || RoleAllows(*mage,WeaponKind::OneHanded) || !RoleAllows(*mage,WeaponKind::Staff))return 2;
  const auto tank=AbilityLink::LoadoutMask(Role::kTank);
  if(!tank || !RoleAllows(*tank,WeaponKind::TwoHanded) || !RoleAllows(*tank,WeaponKind::Shield))return 3;
  const auto skald=AbilityLink::LoadoutMask(Role::kSkald);
  if(!skald.has_value() || *skald!=0)return 4; // Explicitly empty, never unrestricted.
 }
 std::printf("%s bridge scenario %d passed\n",Host::Abilities()?"Paths":"Core",scenario);
}
'''
harness = harness.replace('__STATUS_WIRE__', method('struct Status', (root/'include/FightWithMePathsAPI.h').read_text()))
harness = harness.replace('__VIEW_STATUS__', method('struct Status', (root/'include/FWM/View.h').read_text()))
harness = harness.replace('__STATUS_EXPORT__', method('extern "C" FIGHTWITHMEPATHS_API std::uint32_t FightWithMePaths_GetStatuses(', (root/'paths/src/Main.cpp').read_text()))
harness = harness.replace('__METHODS__', '\n'.join(method(sig) for sig in ('\tvoid Connect()', '\tstd::optional<std::uint32_t> LoadoutMask(Role role)', '    bool SkaldUnlocked(RE::FormID actor)', '    void SkaldProgress(View::Member& member)', '    Paths::Block UnlockSkald(RE::FormID actor)', '    void Statuses(RE::FormID actor,')))
(out / 'Audit.cpp').write_text(harness, encoding='utf-8')
(out / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.20)
project(LoadoutConnectionAudit LANGUAGES CXX)
enable_testing()
foreach(target CoreBridge PathsBridge)
 add_executable(${{target}} Audit.cpp)
 target_compile_features(${{target}} PRIVATE cxx_std_23)
 target_include_directories(${{target}} PRIVATE "{(root / 'include').as_posix()}")
 foreach(scenario RANGE 0 3)
  add_test(NAME ${{target}}-${{scenario}} COMMAND ${{target}} ${{scenario}})
 endforeach()
endforeach()
target_compile_definitions(PathsBridge PRIVATE FWM_HOST_PATHS)
''', encoding='utf-8')
print('Verified core startup and pool consumers; generated native bridge tests for both DLL hosts.')
