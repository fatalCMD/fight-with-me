"""Generate build/companion-theme-audit; configure/build/test it with CMake/CTest.

Compiles production theme routing and both settings queues with simulated DLLs
and SKSE tasks. Requires the sibling Wayfarer checkout for the three-mod audit.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
walk = root.parent / 'Wayfarer'
out = root / 'build/companion-theme-audit'
out.mkdir(parents=True, exist_ok=True)

def method(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

source = (root / 'src/Settings.cpp').read_text(encoding='utf-8')
menu = (walk / 'src/menu_ui.cpp').read_text(encoding='utf-8')
assert (root / 'include/CompanionThemeLink.h').read_bytes() == (walk / 'include/CompanionThemeLink.h').read_bytes()
colors = lambda path: re.findall(r'RGBA\([^\n]*', path.read_text(encoding='utf-8'))
assert colors(root / 'include/FWM/UITheme.h') == colors(walk / 'include/ui_theme.h')

(out / 'windows.h').write_text('''#pragma once
void* GetModuleHandleW(const wchar_t*);
using Proc = void(*)();
Proc GetProcAddress(void*, const char*);
''')
harness = r'''
#include "CompanionThemeLink.h"
#include "FWM/UITheme.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <cstdio>
using namespace std::literals;
using Mod=CompanionThemeLink::Mod;
struct Data {int uiTheme=1,other=123;};
std::array<Data,3> chosen;
std::array<int,3> writes{},saved{};
int self=0,mask=0,missingSetter=-1,failedSave=-1;
bool save(int i,const Data& data){chosen[i]=data;++writes[i];if(i==failedSave)return false;saved[i]=data.uiTheme;return true;}
bool set(int i,int theme){auto d=chosen[i];d.uiTheme=theme;return save(i,d);}
int get0(){return chosen[0].uiTheme;} int get1(){return chosen[1].uiTheme;} int get2(){return chosen[2].uiTheme;}
bool set0(int t){return set(0,t);} bool set1(int t){return set(1,t);} bool set2(int t){return set(2,t);}
void* GetModuleHandleW(const wchar_t* dll){
 for(int i=0;i<3;++i)if(std::wstring_view(dll)==CompanionThemeLink::peers[i].dll && (mask&(1<<i)))
  return reinterpret_cast<void*>(static_cast<intptr_t>(i+1));
 return nullptr;
}
Proc GetProcAddress(void* module,const char* name){
 const int i=static_cast<int>(reinterpret_cast<intptr_t>(module))-1;
 const Proc gets[]{reinterpret_cast<Proc>(get0),reinterpret_cast<Proc>(get1),reinterpret_cast<Proc>(get2)};
 const Proc sets[]{reinterpret_cast<Proc>(set0),reinterpret_cast<Proc>(set1),reinterpret_cast<Proc>(set2)};
 if(std::string_view(name)==CompanionThemeLink::peers[i].get)return gets[i];
 if(std::string_view(name)==CompanionThemeLink::peers[i].set && i!=missingSetter)return sets[i];
 return nullptr;
}
namespace SKSE {
struct Tasks{std::vector<std::function<void()>> queued;void AddTask(std::function<void()> f){queued.push_back(f);}} tasks;
Tasks* GetTaskInterface(){return &tasks;}
void Drain(){auto queue=std::move(tasks.queued);tasks.queued.clear();for(auto& f:queue)f();}
}
namespace FWM::Host {bool Abilities(){return self==1;}}
namespace FWM::Log {template<class... T>void Warn(T&&...){}}
namespace FWM::Settings {
using ::Data;
const Data& Chosen(){return chosen[self];} const Data& Get(){return chosen[self];}
bool Apply(const Data& d){return save(self,d);}
std::mutex submitLock;
struct Pending{std::uint64_t first;Data second;std::optional<int> theme;};
std::optional<Pending> pending;
bool taskQueued=false;
std::atomic<std::uint64_t> requested{},applied{};
std::atomic<bool> saveFailed{};
__FWM_METHODS__
}
namespace Walk {
using SettingsData=Data;
namespace UITheme {bool Share(int t){return CompanionThemeLink::Share(Mod::Walk,t);}}
struct Settings {
 static Settings& GetSingleton(){static Settings s;return s;}
 const Data& Get(){return chosen[2];}
};
struct FormationController {
 static FormationController& GetSingleton(){static FormationController c;return c;}
 bool ApplyPreferences(const Data& d){return save(2,d);}
 Data Snapshot(){return chosen[2];}
};
void Cache(const Data&){}
SettingsData draft;bool dirty=false,shareTheme=false;
std::string feedback;
std::mutex settingsMutex;
struct PendingSettings{std::uint64_t first;Data second;std::optional<int> theme;};
std::optional<PendingSettings> pendingSettings;
bool settingsTaskQueued=false;
std::atomic<std::uint64_t> requestedRevision{},appliedRevision{};
std::atomic<int> applyResult{};
__WWM_METHOD__
}
void submit(const Data& d,bool theme){
 if(self==2){Walk::draft=d;Walk::shareTheme=theme;Walk::dirty=true;Walk::SaveChanges();}
 else FWM::Settings::Submit(d,theme);
}
bool failed(){return self==2?Walk::applyResult==2:FWM::Settings::saveFailed.load();}
int failures=0;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d mask=%d writer=%d: %s\n",__LINE__,mask,self,#x);++failures;}}while(false)
int main(){
 for(mask=1;mask<8;++mask)for(self=0;self<3;++self){
  if(!(mask&(1<<self)))continue;
  for(int i=0;i<3;++i){chosen[i]={i,100+i};writes[i]=0;}
  const int authority=(mask&1)?0:(mask&2)?1:2;
  CHECK(CompanionThemeLink::Current(static_cast<Mod>(self),chosen[self].uiTheme)==authority);
  for(int theme=0;theme<3;++theme){
   auto edit=chosen[self];edit.uiTheme=theme;submit(edit,true);
   auto stale=chosen[self];stale.other+=10;submit(stale,false);
   CHECK(SKSE::tasks.queued.size()==1);SKSE::Drain();CHECK(!failed());
   CHECK(chosen[self].other==stale.other);
   for(int peer=0;peer<3;++peer)if(mask&(1<<peer)){
    CHECK(chosen[peer].uiTheme==theme);CHECK(saved[peer]==theme);
    CHECK(CompanionThemeLink::Current(static_cast<Mod>(peer),chosen[peer].uiTheme)==theme);
    if(peer!=self)CHECK(chosen[peer].other==100+peer);
   }
   for(int peer=0;peer<3;++peer)chosen[peer].uiTheme=saved[peer];
   CHECK(CompanionThemeLink::Current(static_cast<Mod>(self),chosen[self].uiTheme)==theme);
  }
  // External theme change after an unrelated draft was submitted.
  auto stale=chosen[self];stale.uiTheme=0;stale.other=900;submit(stale,false);
  set(self,1);const auto before=writes;SKSE::Drain();CHECK(!failed());
  CHECK(chosen[self].uiTheme==1 && chosen[self].other==900);
  for(int peer=0;peer<3;++peer)if(peer!=self)CHECK(writes[peer]==before[peer]);
 }
 mask=7;
 for(self=0;self<3;++self)for(int fault=0;fault<2;++fault){
  chosen={Data{1,111},Data{1,222},Data{1,333}};
  missingSetter=fault==0?(self+1)%3:-1;failedSave=fault==1?(self+1)%3:-1;
  auto edit=chosen[self];edit.uiTheme=2;submit(edit,true);SKSE::Drain();CHECK(failed());
  CHECK(chosen[(self+2)%3].uiTheme==2);
  missingSetter=failedSave=-1;
 }
 self=2;mask=4;chosen[2].uiTheme=2;CHECK(CompanionThemeLink::Current(Mod::Walk,2)==2);
 mask=5;chosen[0].uiTheme=0;CHECK(CompanionThemeLink::Current(Mod::Walk,2)==0);
 chosen[0].uiTheme=-1;CHECK(CompanionThemeLink::Current(Mod::Walk,2)==2);
 CHECK(CompanionThemeLink::Normalize(99)==0);
 std::printf("All 7 installation combinations, every menu writer, restart, coalescing, stale drafts and failure propagation: %d failures\n",failures);
 return failures?1:0;
}
'''
harness = harness.replace('__FWM_METHODS__', '\n'.join(method(source, s) for s in (
    '\tint Theme()', '\tbool ApplyTheme(', '    bool ShareTheme(', '\tstd::uint64_t Submit(')))
harness = harness.replace('__WWM_METHOD__', method(menu, '        void SaveChanges()'))
(out / 'Audit.cpp').write_text(harness, encoding='utf-8')
(out / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.20)
project(CompanionThemeAudit LANGUAGES CXX)
enable_testing()
add_executable(PathsTheme Audit.cpp)
target_compile_features(PathsTheme PRIVATE cxx_std_23)
target_include_directories(PathsTheme PRIVATE "{out.as_posix()}" "{(root / 'include').as_posix()}")
add_test(NAME CompanionThemes COMMAND PathsTheme)
''', encoding='utf-8')
print('Generated production theme routing/queue audit; palette and protocol copies match.')
