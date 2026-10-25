#include "FWM/PanelBindings.h"
#include "FWM/Host.h"
#include <cstdio>
int main() {
    using namespace FWM;
    int failures=0;
    auto check=[&](bool good,const char* why){if(!good){++failures;std::printf("FAIL: %s\n",why);}};
    check(PanelBindings::Keyboard({71},{71},true,false)==Hotkey{73},"legacy paired keyboard collision repaired");
    check(PanelBindings::Gamepad(2,2,true,false)==8,"legacy paired gamepad collision repaired");
    check(PanelBindings::Keyboard({71},{71},false,false)==Hotkey{71},"standalone key preserved");
    check(PanelBindings::Gamepad(2,2,false,false)==2,"standalone pad preserved");
    check(PanelBindings::Keyboard({71},{71},true,true)==Hotkey{71},"explicit user key preserved");
    check(PanelBindings::Gamepad(2,2,true,true)==2,"explicit user pad preserved");
    check(PanelBindings::Keyboard({71,kCtrl},{71},true,false)==Hotkey{71,kCtrl},"modifier chord preserved");
    check(PanelBindings::Keyboard({71},{72},true,false)==Hotkey{71},"nonconflicting key preserved");
    check(PanelBindings::Gamepad(2,4,true,false)==2,"nonconflicting pad preserved");
    check(PanelBindings::Keyboard({0},{0},true,false)==Hotkey{0},"unbound key stays unbound");
    check(PanelBindings::Keyboard({73},{71},true,false)==Hotkey{73},"new default stable");
#if defined(FWM_HOST_PATHS)
    check(Host::Abilities()&&!Host::Orders(),"Paths owns abilities only");
    check(!Host::Steers(),"paired Paths must not compete for steering");
    Host::SetPathsAlone();check(Host::Steers(),"standalone Paths steers its abilities");
#else
    check(!Host::Abilities()&&Host::Orders()&&Host::Steers(),"core owns orders and never abilities");
#endif
    std::printf("Plugin split: %d failures\n",failures);return failures?1:0;
}
