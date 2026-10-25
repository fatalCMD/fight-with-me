"""Compile the production equipment guards against a small fake engine.

Generate build/autopool-recovery-audit, then build/run with CMake/CTest.
Checks automatic vs explicit spells, role exclusions, recovery, and resumption.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
out = root / 'build/autopool-recovery-audit'
out.mkdir(parents=True, exist_ok=True)
source = (root / 'src/Overrides.cpp').read_text(encoding='utf-8')

def method(signature, after=0):
    start = source.index(signature, after)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

harness = r'''
#include "FWM/LoadoutPolicy.h"
#include <atomic>
#include <memory>
#include <cstdio>
namespace RE {
struct TESForm {
 virtual ~TESForm()=default;
 unsigned id{}; bool armor{};
 unsigned GetFormID()const{return id;}
 template<class T> T* As(){return dynamic_cast<T*>(this);}
 bool IsWeapon()const;
 bool IsArmor()const{return armor;}
};
struct TESObjectWEAP:TESForm {
 bool staff{},bound{},two{};
 bool IsStaff()const{return staff;} bool IsBound()const{return bound;}
 bool IsTwoHandedSword()const{return two;} bool IsTwoHandedAxe()const{return false;}
};
bool TESForm::IsWeapon()const{return dynamic_cast<const TESObjectWEAP*>(this)!=nullptr;}
struct MagicItem:TESForm{};
struct Actor{};
struct Handle{unsigned value{1};unsigned native_handle()const{return value;}};
struct CombatController{Handle attackerHandle;};
struct CombatInventoryItem{TESForm* item{};struct{int equipSlot{};}itemSlot;};
}
namespace FWM::Audit {
constexpr unsigned kLeftHand=0,kRightHand=1;
enum class Takes{kLeft,kRight,kEither,kBoth};
Takes TakesOf(int slot){return static_cast<Takes>(slot);}
struct HandsEntry{
 unsigned actor{1}; const RE::Actor* body{};
 std::array<unsigned,2> hands{};
 LoadoutPolicy::Pools pools;
 bool twoHandedOnly{},battleLocked{},staffLocked{},roleRestricted{},caster{},recovering{};
 std::array<bool,2> explicitSpells{};
 struct Seen{std::atomic<bool>looked{};std::array<std::atomic<unsigned short>,2>casters{};};
 std::shared_ptr<Seen>seen;
};
struct Entry{bool resting{},noMelee{};};
struct Table{Entry entry;};
std::atomic<std::shared_ptr<const std::vector<HandsEntry>>> chosenHands;
std::atomic<std::shared_ptr<Table>>current;
std::atomic<unsigned>kept{},rested{},melee{};
const Entry* Listed(const Table* table,RE::CombatController*){return table?&table->entry:nullptr;}
bool original(RE::CombatInventoryItem*,RE::CombatController*){return true;}
__GUARDS__
void publish(const HandsEntry& entry){chosenHands.store(std::make_shared<const std::vector<HandsEntry>>(std::vector{entry}));}
}
#define CHECK(test) do{if(!(test)){std::printf("FAIL line %d: %s\n",__LINE__,#test);return 1;}}while(false)
int main(){
 using namespace FWM;using namespace FWM::Audit;
 RE::Actor actor;RE::CombatController controller;
 RE::TESObjectWEAP dagger,staff;dagger.id=11;staff.id=12;staff.staff=true;
 RE::TESForm shield;shield.id=13;shield.armor=true;
 RE::MagicItem fire,heal;fire.id=21;heal.id=22;
 HandsEntry mage;mage.body=&actor;mage.caster=true;mage.roleRestricted=true;
 mage.pools={LoadoutPolicy::Resolve({}, {21}),LoadoutPolicy::Resolve({}, {21}),LoadoutPolicy::Resolve({}, {12})};
 publish(mage);
 RE::CombatInventoryItem item;item.itemSlot.equipSlot=static_cast<int>(Takes::kEither);
 item.item=&dagger;CHECK(!Thunk(&item,&controller));
 item.item=&shield;CHECK(!Thunk(&item,&controller));
 item.item=nullptr;CHECK(!Thunk(&item,&controller));
 item.item=&staff;CHECK(Thunk(&item,&controller));CHECK(KeepsHands(&item,&controller,0));
 item.item=&fire;CHECK(KeepsHands(&item,&controller,0));
 item.item=&heal;CHECK(!KeepsHands(&item,&controller,0));
 CHECK(!Chosen(&actor,&fire)); // automatic must preserve native school skill checks
 mage.explicitSpells[0]=true;publish(mage);CHECK(Chosen(&actor,&fire));CHECK(!Chosen(&actor,&heal));
 mage.explicitSpells={};mage.recovering=true;publish(mage);
 item.item=&fire;CHECK(!KeepsHands(&item,&controller,0));
 item.item=&staff;CHECK(!KeepsHands(&item,&controller,0));CHECK(!Thunk(&item,&controller));
 item.item=nullptr;CHECK(!Thunk(&item,&controller));
 mage.recovering=false;publish(mage);
 item.item=&fire;CHECK(KeepsHands(&item,&controller,0));
 item.item=&staff;CHECK(Thunk(&item,&controller));
 mage.pools[0]=mage.pools[1]=LoadoutPolicy::Resolve({},{});publish(mage);
 item.item=&fire;CHECK(!KeepsHands(&item,&controller,0));
 // Manual core-only pools retain their existing behavior; caster roles still reject fists.
 mage.roleRestricted=false;mage.pools={};publish(mage);
 item.item=&fire;CHECK(KeepsHands(&item,&controller,0));
 item.item=nullptr;CHECK(!Thunk(&item,&controller));
 // A tank's automatic shield/two-hander pool remains usable.
 HandsEntry tank;tank.roleRestricted=true;tank.pools[2]={11,13};publish(tank);
 item.item=&shield;CHECK(Thunk(&item,&controller));
 item.item=&dagger;CHECK(Thunk(&item,&controller));
 item.item=&staff;CHECK(!Thunk(&item,&controller));
 controller.attackerHandle.value=99;CHECK(Thunk(&item,&controller));
 std::puts("Production pool/caster guards: automatic, manual, recovery, resumption, tank, and unmanaged actors passed.");
}
'''
guards = '\n'.join((method('\t\tbool HandAllows('), method('\t\tbool KeepsHands('),
                    method('\t\t\tstatic bool Chosen('),
                    method('\t\t\tstatic bool Thunk(', source.index('\t\tstruct WeaponShouldEquip'))))
(out / 'Audit.cpp').write_text(harness.replace('__GUARDS__', guards), encoding='utf-8')
(out / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.20)
project(AutopoolRecoveryAudit LANGUAGES CXX)
enable_testing()
add_executable(AutopoolRecovery Audit.cpp)
target_compile_features(AutopoolRecovery PRIVATE cxx_std_23)
target_include_directories(AutopoolRecovery PRIVATE "{(root / 'include').as_posix()}")
add_test(NAME ProductionPoolRecoveryGuards COMMAND AutopoolRecovery)
''', encoding='utf-8')
print('Generated production automatic-pool/recovery guard audit.')
