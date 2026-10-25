#pragma once
#include "FWM/LoadoutPolicy.h"
#include "FWM/Spells.h"
namespace FWM::Loadouts {
using LoadoutPolicy::Pools;
Pools Of(RE::FormID actor);
Pools Effective(RE::FormID actor);
void BeginBattle(RE::Actor* actor,const Orders& orders);
void EndBattle();
bool Locked(RE::FormID actor);
const Orders* BattleOrders(RE::FormID actor,const Orders& saved);
std::string EquipBattle(RE::Actor* actor);
FWM::Hands Hands(RE::FormID actor);
Spells::Outcome Toggle(RE::Actor* actor,Role role,std::size_t pool,RE::FormID item);
std::vector<View::Spell> Weapons(RE::Actor* actor,Role role);
std::pair<RE::TESObjectWEAP*,RE::TESAmmo*> Ranged(RE::Actor* actor);
bool WeaponAllowed(RE::Actor* actor,const RE::TESObjectWEAP* weapon,Role role);
void Recheck(RE::Actor* actor,Role role);
void Enforce(RE::Actor* actor,Role role);
std::vector<std::uint8_t> Encode();
void Decode(SKSE::SerializationInterface* intfc,const std::vector<std::uint8_t>& bytes);
void Clear();
}
