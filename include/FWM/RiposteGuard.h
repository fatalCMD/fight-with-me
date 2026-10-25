#pragma once
#include <array>
#include <string_view>

namespace FWM::RiposteGuard {
// Only entry events. Dodge stop/end notifications must still reach the graph
// so another mod can clear its animation and invulnerability state normally.
constexpr bool DodgeEntry(std::string_view event) {
    constexpr std::array names{
        "dodge", "dodgestart", "dodgeforward", "dodgeback", "dodgeleft", "dodgeright",
        "tkdodgeforward", "tkdodgeback", "tkdodgeleft", "tkdodgeright",
        "stepdodgeforward", "stepdodgeback", "stepdodgeleft", "stepdodgeright"
    };
    for(std::string_view name:names) {
        if(name.size()!=event.size())continue;
        bool same=true;
        for(std::size_t i=0;i<name.size();++i) {
            const char c=event[i]>='A' && event[i]<='Z'?static_cast<char>(event[i]+('a'-'A')):event[i];
            if(c!=name[i]){same=false;break;}
        }
        if(same)return true;
    }
    return false;
}
constexpr bool Block(bool guarding,std::string_view event){return guarding && DodgeEntry(event);}
}
