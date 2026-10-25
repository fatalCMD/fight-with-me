"""Exercise production pool-bridge methods with a simulated plugin loader.
Generate build/loadout-connection-audit, then configure/build/run with CMake/CTest.
The game and SKSE are not launched by this audit.
"""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
out = root / 'build/loadout-connection-audit'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'src/AbilityLink.cpp').read_text(encoding='utf-8')

def method(signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    end += 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

# Catch the original regression: consumers used a bridge that core never initializes.
plugin = (root / 'src/Plugin.cpp').read_text(encoding='utf-8')
assert 'AbilityLink::Connect();' in plugin
for name in ('src/Loadouts.cpp', 'src/Spells.cpp'):
    text = (root / name).read_text(encoding='utf-8')
    assert 'AbilityLink::LoadoutMask(' in text and 'PathsLink::LoadoutMask(' not in text

harness = r'''
#include "FWM/Host.h"
#include "FWM/LoadoutPolicy.h"
#include <optional>
#include <string_view>
#include <cstdio>
using namespace std::literals;
int scenario;
namespace FWM::Paths { constexpr unsigned Signature() { return 123; } }
namespace FightWithMePathsAPI {
constexpr unsigned INTERFACE_VERSION = 5;
struct IPaths { unsigned GetTreeSignature() { return scenario == 1 ? 999 : FWM::Paths::Signature(); } };
}
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
 return nullptr;
}
namespace FWM::AbilityLink {
FightWithMePathsAPI::IPaths* api{};
unsigned (*loadoutMask)(unsigned){};
bool (*isDecoy)(unsigned){};
bool mismatched{};
__METHODS__
}
int main(int argc,char** argv) {
 scenario = argc > 1 ? argv[1][0]-'0' : 0;
 using namespace FWM;
 AbilityLink::Connect();
 const auto mage=AbilityLink::LoadoutMask(Role::kMage);
 if(!Host::Abilities() && scenario != 3) {
  if(mage.has_value())return 1;
 } else {
  using namespace LoadoutPolicy;
  if(!mage || RoleAllows(*mage,WeaponKind::OneHanded) || !RoleAllows(*mage,WeaponKind::Staff))return 2;
  const auto tank=AbilityLink::LoadoutMask(Role::kTank);
  if(!tank || !RoleAllows(*tank,WeaponKind::TwoHanded) || !RoleAllows(*tank,WeaponKind::Shield))return 3;
 }
 std::printf("%s bridge scenario %d passed\n",Host::Abilities()?"Paths":"Core",scenario);
}
'''
harness = harness.replace('__METHODS__', method('\tvoid Connect()') + '\n' + method('\tstd::optional<std::uint32_t> LoadoutMask(Role role)'))
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
