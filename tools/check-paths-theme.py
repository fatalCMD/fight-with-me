"""Exercise production queued theme changes with/without the core plugin.
Generate build/paths-theme-audit and configure/build/test with CMake/CTest.
"""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
out = root / 'build/paths-theme-audit'
out.mkdir(parents=True, exist_ok=True)
(out / 'windows.h').write_text('#pragma once\n')
source = (root / 'src/Settings.cpp').read_text(encoding='utf-8')
def method(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

menu = (root / 'src/Menu.cpp').read_text(encoding='utf-8')
page = menu[menu.index('void __stdcall RenderPathsSettings()'):menu.index('// SETTINGS_END')]
assert 'if (alone)' not in page and 'if (!alone)' not in page
assert 'shareTheme = true;' in page and 'const int theme=Settings::Theme();' in page
assert 'SetSection("Path of the Companions")' in menu
assert 'A branch never outranks' not in menu
assert 'FightWithMe_SetTheme' in (root / 'src/Plugin.cpp').read_text(encoding='utf-8')

harness = r'''
#include "FWM/UITheme.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>
#include <cstdio>
using namespace std::literals;
int scenario=0,coreTheme=2,coreOther=777,coreWrites=0;
namespace FWM::Host{bool Abilities(){return scenario!=3;}}
namespace FWM::Log{template<class... T>void Warn(T&&...){}}
bool coreSet(int theme){coreTheme=theme;++coreWrites;return scenario!=4;}
int coreGet(){return coreTheme;}
void* GetModuleHandleW(const wchar_t* name){return scenario==0 || std::wstring_view(name)!=L"FightWithMe.dll"?nullptr:&coreTheme;}
using Proc=void(*)();
Proc GetProcAddress(void*,const char* name){
 if(std::string_view(name)=="FightWithMe_GetTheme")return reinterpret_cast<Proc>(&coreGet);
 if(std::string_view(name)=="FightWithMe_SetTheme" && scenario!=2)return reinterpret_cast<Proc>(&coreSet);
 return nullptr;
}
#include "CompanionThemeLink.h"
namespace SKSE {
struct Tasks{std::vector<std::function<void()>>queued;void AddTask(std::function<void()>task){queued.push_back(task);}}tasks;
Tasks* GetTaskInterface(){return &tasks;}
void Drain(){auto queue=std::move(tasks.queued);tasks.queued.clear();for(auto& task:queue)task();}
}
namespace FWM::Settings {
struct Data{int uiTheme=1,other=123;};
Data chosen;int localWrites=0;
const Data& Chosen(){return chosen;}const Data& Get(){return chosen;}
bool Apply(const Data& data){chosen=data;++localWrites;return true;}
std::mutex submitLock;
struct Pending{std::uint64_t first;Data second;std::optional<int>theme;};
std::optional<Pending>pending;
bool taskQueued=false;
std::atomic<std::uint64_t>requested{},applied{};
std::atomic<bool>saveFailed{};
__METHODS__
}
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);return 1;}}while(false)
int main(int argc,char**argv){
 scenario=argc>1?argv[1][0]-'0':0;
 using namespace FWM::Settings;
 const bool paired=scenario!=0 && scenario!=3;
 CHECK(Theme()==(paired?2:1));
 // Other Paths settings must not silently replace core's current theme.
 auto edit=Chosen();edit.other=456;Submit(edit,false);SKSE::Drain();
 CHECK(coreWrites==0 && coreOther==777 && Chosen().other==456);
 // Explicit theme edits stay queued and survive coalescing with another setting.
 edit.uiTheme=0;Submit(edit,true);
 auto later=Chosen();later.other=789;Submit(later,false);
 CHECK(coreWrites==0);SKSE::Drain();
 CHECK(Chosen().uiTheme==0 && Chosen().other==789 && coreOther==777);
 CHECK(applied==requested);
 CHECK(saveFailed==(scenario==2 || scenario==4));
 if(scenario==1 || scenario==4)CHECK(coreWrites==1 && coreTheme==0 && Theme()==0);
 if(scenario==0 || scenario==3)CHECK(coreWrites==0 && Theme()==0);
 if(scenario==2)CHECK(coreWrites==0 && Theme()==2); // older core reports unsupported setter
 if(scenario==1){coreTheme=1;CHECK(Theme()==1);} // core changes remain live in Paths
 CHECK(ApplyTheme(99));CHECK(Chosen().uiTheme==0 && Chosen().other==789);
 std::printf("Theme scenario %d passed\n",scenario);
}
'''
harness = harness.replace('__METHODS__', '\n'.join(method(s) for s in ('\tint Theme()', '\tbool ApplyTheme(', '    bool ShareTheme(', '\tstd::uint64_t Submit(')))
(out / 'Audit.cpp').write_text(harness, encoding='utf-8')
(out / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.20)
project(PathsThemeAudit LANGUAGES CXX)
enable_testing()
add_executable(PathsTheme Audit.cpp)
target_compile_features(PathsTheme PRIVATE cxx_std_23)
target_include_directories(PathsTheme PRIVATE "{out.as_posix()}" "{(root / 'include').as_posix()}")
foreach(scenario RANGE 0 4)
 add_test(NAME Theme-${{scenario}} COMMAND PathsTheme ${{scenario}})
endforeach()
''', encoding='utf-8')
print('Generated standalone, paired, old-core, core-only and save-failure theme audits.')
