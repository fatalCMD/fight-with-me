#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace FWM::RiposteGuard {
// Only entry events. Dodge and attack stop/end notifications must still reach the graph
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
constexpr bool StartsWith(std::string_view event,std::string_view prefix) {
    if(event.size()<prefix.size())return false;
    for(std::size_t i=0;i<prefix.size();++i) {
        const char c=event[i]>='A' && event[i]<='Z'?static_cast<char>(event[i]+('a'-'A')):event[i];
        if(c!=prefix[i])return false;
    }
    return true;
}
// Swings, bashes and casts the combat AI starts while the guard is up. Holding these back is what
// keeps a riposter waiting behind the guard; the counter itself is sent once the guard is lowered.
constexpr bool SwingEntry(std::string_view event) {
    constexpr std::array prefixes{"attackstart", "attackpowerstart", "bashstart", "bashpowerstart", "begincast"};
    for(std::string_view prefix:prefixes)if(StartsWith(event,prefix))return true;
    return false;
}
enum class Held : std::uint8_t { None, Dodge, Swing, Lower };
// The AI's own blockStop is held too, so the guard does not flicker down between its attempts to swing.
constexpr Held Kind(std::string_view event) {
    if(DodgeEntry(event))return Held::Dodge;
    if(SwingEntry(event))return Held::Swing;
    if(event.size()==9 && StartsWith(event,"blockstop"))return Held::Lower;
    return Held::None;
}
constexpr bool Block(bool guarding,std::string_view event){return guarding && Kind(event)!=Held::None;}

// What a hand holds, as far as blocking goes. Fists and a second weapon block here
// (Hand to Hand Blocking, Dual Wield Parrying); a spell or staff in the off hand never does.
enum class Hand : std::uint8_t { Empty, Melee, Shield, Torch, Spell, Staff, Bow };
constexpr bool CanGuard(Hand right,Hand left) {
    if(right==Hand::Bow || right==Hand::Staff)return false;
    if(left==Hand::Shield)return true;
    return right!=Hand::Spell && left!=Hand::Spell && left!=Hand::Staff;
}

// A guard has to come up within kRaiseWithin (a swing in progress may finish first), and it
// goes after kSlack seconds spent neither blocking nor swinging, raised or not. Swings only
// pause that count, so swinging alone never holds a guard open.
// Since they may not swing behind it, the guard also goes once nobody near has been after them
// for kAloneFor.
constexpr float kRaiseWithin=2.5f,kSlack=1.0f,kAloneFor=1.5f;
// Guards that never came up, in a row, before Riposte stops being chosen for the fight.
constexpr int kMissesToBench=2;
struct Watch {
    enum class Step : std::uint8_t { Holding, Raised, NeverRaised, Dropped, Alone };
    float age{},idle{},alone{};bool raised{};
    constexpr Step Advance(float dt,bool blocking,bool busy,bool threatened=true) {
        age+=dt;
        alone=threatened?0:alone+dt;
        if(alone>kAloneFor)return Step::Alone;
        if(blocking){idle=0;if(!raised){raised=true;return Step::Raised;}return Step::Holding;}
        if(!busy)idle+=dt;
        if(raised)return idle>kSlack?Step::Dropped:Step::Holding;
        return idle>kSlack || age>kRaiseWithin?Step::NeverRaised:Step::Holding;
    }
};
}
