#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>
#include "FWM/Orders.h"
namespace FWM::LoadoutPolicy {
using Pool=std::vector<std::uint32_t>;
using Pools=std::array<Pool,3>; // left spells, right spells, inventory weapons
constexpr std::size_t kLimit=64;
enum class WeaponKind { OneHanded, TwoHanded, Bow, Staff, Shield };
constexpr std::uint32_t WeaponBit(WeaponKind kind) { return 1u << static_cast<unsigned>(kind); }
constexpr std::uint32_t RoleWeapons(Role role) {
 using enum WeaponKind;
 switch(role) {
 case Role::kSkald:return 0; // An engaged optional zero mask means fists only, not unrestricted.
 case Role::kFighter:return WeaponBit(OneHanded);
 case Role::kTank:return WeaponBit(OneHanded)|WeaponBit(TwoHanded)|WeaponBit(Shield);
 case Role::kBruiser:return WeaponBit(TwoHanded);
 case Role::kArcher:return WeaponBit(Bow);
 case Role::kMage:case Role::kHealer:return WeaponBit(Staff);
 default:return 0;
 }
}
constexpr bool RoleAllows(std::uint32_t mask,WeaponKind kind) {return (mask&WeaponBit(kind))!=0;}
constexpr Role BattleRole(WeaponKind kind, Role preferred, bool shield) {
 if(kind==WeaponKind::Bow)return Role::kArcher;
 if(kind==WeaponKind::Staff)return preferred==Role::kHealer?Role::kHealer:Role::kMage;
 if(kind==WeaponKind::TwoHanded)return preferred==Role::kTank?Role::kTank:Role::kBruiser;
 return shield || preferred==Role::kTank?Role::kTank:Role::kFighter;
}
struct BattleChoice {
 std::uint32_t weapon{}, shield{};
 Role role{Role::kCount};
 bool attempted{};
 // A target change or equipment retry must never roll the encounter again.
 bool Lock(std::uint32_t w, std::uint32_t s, WeaponKind kind, Role preferred, bool keepRole=false) {
  if(attempted)return false;
  attempted=true; weapon=w;
  shield=kind==WeaponKind::OneHanded?s:0;
  role=w && !keepRole?BattleRole(kind,preferred,shield!=0):preferred;
  return w!=0;
 }
};
inline bool Contains(const Pool& pool,std::uint32_t id) {return std::find(pool.begin(),pool.end(),id)!=pool.end();}
inline bool Allows(const Pool& pool,std::uint32_t id) {return pool.empty() || Contains(pool,id);}
template<class Predicate> Pool Compatible(const Pool& saved,Predicate allowed) {
 Pool result=saved;
 std::erase_if(result,[&](auto id){return !allowed(id);});
 // An explicitly selected pool with no usable items cannot mean "choose anything".
 if(!saved.empty() && result.empty())result.push_back(0);
 return result;
}
// Automatic choice is a finite eligible set, never an unrestricted empty pool.
inline Pool Resolve(const Pool& saved,const Pool& eligible) {
 auto result=saved.empty()?eligible:Compatible(saved,[&](auto id){return Contains(eligible,id);});
 if(result.empty())result.push_back(0);
 return result;
}
struct Saved {std::uint32_t actor{}; Pools pools;};
inline std::vector<std::uint8_t> Encode(const std::vector<Saved>& records) {
 std::vector<std::uint8_t> out;
 auto put=[&](std::uint32_t n){for(int b=0;b<4;b++)out.push_back(static_cast<std::uint8_t>(n>>(b*8)));};
 for(const auto& row:records){put(row.actor);for(const auto& pool:row.pools){put(static_cast<std::uint32_t>(pool.size()));for(auto id:pool)put(id);}}
 return out;
}
inline std::vector<Saved> Decode(std::span<const std::uint8_t> bytes) {
 std::vector<Saved> out; std::size_t at=0;
 auto get=[&](){std::uint32_t n=0;for(int b=0;b<4;b++)n|=static_cast<std::uint32_t>(bytes[at++])<<(b*8);return n;};
 while(at<bytes.size()) {
  if(out.size()>=4096 || bytes.size()-at<16)return {};
  Saved row; row.actor=get(); if(!row.actor)return {};
  for(auto& pool:row.pools){if(bytes.size()-at<4)return {};auto n=get();if(n>kLimit || n>(bytes.size()-at)/4)return {};for(std::uint32_t i=0;i<n;i++){auto id=get();if(id && !Contains(pool,id))pool.push_back(id);}}
  out.push_back(std::move(row));
 }
 return out;
}
}
