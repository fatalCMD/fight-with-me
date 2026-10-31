#include "FWM/Loadouts.h"
#include "FWM/Party.h"
#include "FWM/Game.h"
#include "FWM/Text.h"
#include "FWM/Log.h"
#include "FWM/AbilityLink.h"
#include "FWM/Settings.h"
#include "FWM/SkaldPolicy.h"
#ifdef GetObject
#undef GetObject
#endif
namespace FWM::Loadouts {
namespace {std::unordered_map<RE::FormID,Pools> saved;
struct BattleKit { LoadoutPolicy::BattleChoice choice; Orders orders; };
std::unordered_map<RE::FormID,BattleKit> battles;
struct Eligible {Pools pools; Role role{Role::kCount}; std::uint64_t at{}; int level{}; bool anySpell{};};
std::unordered_map<RE::FormID,Eligible> eligible;
std::mt19937 dice{std::random_device{}()};
std::unordered_map<RE::FormID,std::uint64_t> checked;
bool Two(const RE::TESObjectWEAP* w){return w && !w->IsBound() && (w->IsTwoHandedSword()||w->IsTwoHandedAxe());}
LoadoutPolicy::WeaponKind Kind(const RE::TESObjectWEAP* w) {
 using enum LoadoutPolicy::WeaponKind;
 return w->IsStaff()?Staff:(w->IsBow()||w->IsCrossbow())?Bow:Two(w)?TwoHanded:OneHanded;
}
bool FitsRole(RE::TESForm* item,Role role) {
 if(role==Role::kSkald)return false;  // the lute is its weapon, whatever the setting
 if(!Settings::Get().roleWeapons)return true;
 const auto mask=AbilityLink::LoadoutMask(role);if(!mask)return true;
 if(auto* w=item?item->As<RE::TESObjectWEAP>():nullptr)return LoadoutPolicy::RoleAllows(*mask,Kind(w));
 auto* armor=item?item->As<RE::TESObjectARMO>():nullptr;
 return armor && armor->IsShield() && LoadoutPolicy::RoleAllows(*mask,LoadoutPolicy::WeaponKind::Shield);
}
}
Pools Of(RE::FormID actor) {
 if(auto it=saved.find(actor);it!=saved.end())return it->second;
 Pools pools;const auto old=Party::HandsOf(actor);
 for(std::size_t h=0;h<2;h++)if(old[h])pools[h].push_back(old[h]);
 return pools;
}
bool Locked(RE::FormID actor) { auto it=battles.find(actor);return it!=battles.end() && it->second.choice.weapon; }
Pools Effective(RE::FormID actor) {
 auto pools=Of(actor);
 // Cache inventory/known-spell enumeration; snapshots are published every frame.
 if(const auto* orders=Party::FindOrders(actor);orders && AbilityLink::LoadoutMask(orders->role)) {
  auto* body=RE::TESForm::LookupByID<RE::Actor>(actor);
  auto& choices=eligible[actor];const auto now=::GetTickCount64();
  const auto level=body?body->GetLevel():0;const auto anySpell=Settings::Get().healersAnySpell;
  if(!choices.at || now-choices.at>=250 || choices.role!=orders->role || choices.level!=level || choices.anySpell!=anySpell) {
   choices={};choices.at=now;choices.role=orders->role;choices.level=level;choices.anySpell=anySpell;
   const auto spells=Spells::Automatic(body,orders->role);
   choices.pools[0]=spells[0];choices.pools[1]=spells[1];
   if(body)for(const auto& [object,entry]:body->GetInventory([](RE::TESBoundObject& o){return o.IsWeapon()||o.IsArmor();}))
    if(object && entry.first>0 && FitsRole(object,orders->role))choices.pools[2].push_back(object->GetFormID());
   std::sort(choices.pools[2].begin(),choices.pools[2].end());
  }
  for(std::size_t i=0;i<pools.size();++i)pools[i]=LoadoutPolicy::Resolve(pools[i],choices.pools[i]);
 }
 if(auto it=battles.find(actor);it!=battles.end() && it->second.choice.weapon) {
  pools[2]={it->second.choice.weapon};
  if(it->second.choice.shield)pools[2].push_back(it->second.choice.shield);
 }
 return pools;
}
const Orders* BattleOrders(RE::FormID actor,const Orders& base) {
 auto it=battles.find(actor);if(it==battles.end() || !it->second.choice.weapon)return nullptr;
 if(AbilityLink::LoadoutMask(base.role))return nullptr; // assigned path wins over the rolled weapon
 it->second.orders=base;it->second.orders.role=it->second.choice.role;
 if(base.role!=it->second.orders.role)it->second.orders=Retask(base,it->second.orders.role);
 return &it->second.orders;
}
void EndBattle(){battles.clear();checked.clear();eligible.clear();}
void BeginBattle(RE::Actor* actor,const Orders& orders) {
 if(actor&&orders.role==Role::kSkald){battles[actor->GetFormID()].choice={};return;}
 if(!actor || !Party::CanHaveRole(actor) || battles.contains(actor->GetFormID()))return;
 const auto id=actor->GetFormID();const auto pool=Of(id)[2];
 auto& kit=battles[id];
 std::vector<RE::TESObjectWEAP*> weapons,oneHanded;
 std::vector<RE::TESObjectARMO*> shields;
 for(const auto& [object,entry]:actor->GetInventory([](RE::TESBoundObject& o){return o.IsWeapon()||o.IsArmor();})) {
  if(!object || entry.first<=0 || !FitsRole(object,orders.role))continue;
  if(auto* w=object->As<RE::TESObjectWEAP>();w && !w->IsBound()) {
   if(w->IsMelee() && !Two(w))oneHanded.push_back(w);
   if(LoadoutPolicy::Contains(pool,w->GetFormID()))weapons.push_back(w);
  }
  if(auto* shield=object->As<RE::TESObjectARMO>();shield && shield->IsShield() && LoadoutPolicy::Contains(pool,shield->GetFormID()))shields.push_back(shield);
 }
 // Shields supplement a compatible weapon. A shield-only pool uses the strongest carried one-hander.
 if(weapons.empty() && !shields.empty() && !oneHanded.empty()) {
  weapons.push_back(*std::max_element(oneHanded.begin(),oneHanded.end(),[](auto* a,auto* b){return a->GetAttackDamage()<b->GetAttackDamage();}));
 }
 RE::TESObjectWEAP* weapon=weapons.empty()?nullptr:weapons[std::uniform_int_distribution<std::size_t>(0,weapons.size()-1)(dice)];
 using enum LoadoutPolicy::WeaponKind;
 const auto kind=!weapon?OneHanded:weapon->IsStaff()?Staff:(weapon->IsBow()||weapon->IsCrossbow())?Bow:Two(weapon)?TwoHanded:OneHanded;
 const auto shield=kind==OneHanded && !shields.empty()?shields[std::uniform_int_distribution<std::size_t>(0,shields.size()-1)(dice)]->GetFormID():0;
 if(kit.choice.Lock(weapon?weapon->GetFormID():0,shield,kind,orders.role,AbilityLink::LoadoutMask(orders.role).has_value())) {
  checked.erase(id);
  Log::Info("[Loadout] {} starts this battle with {} as {}; weapon locked until combat ends."sv,Game::Name(actor),weapon->GetName(),Info(kit.choice.role).name);
  EquipBattle(actor);
 }
}
std::string EquipBattle(RE::Actor* actor) {
 if(!actor || !Locked(actor->GetFormID()) || actor->IsDead() || actor->IsInKillMove() || actor->IsAttacking())return {};
 const auto& kit=battles.at(actor->GetFormID()).choice;
 auto* equip=RE::ActorEquipManager::GetSingleton();if(!equip)return {};
 const auto inventory=actor->GetInventory();
 auto owns=[&](RE::TESBoundObject* object){const auto it=inventory.find(object);return object && it!=inventory.end() && it->second.first>0;};
 auto* weapon=RE::TESForm::LookupByID<RE::TESObjectWEAP>(kit.weapon);
 // If another mod removes the weapon, do not invent it or reroll mid-fight.
 if(!owns(weapon))return {};
 for(bool left:{false,true}) {
  auto* held=actor->GetEquippedObject(left);if(!held || held->GetFormID()==kit.weapon || held->GetFormID()==kit.shield)continue;
  if(auto* object=held->As<RE::TESBoundObject>())equip->UnequipObject(actor,object);
 }
 if(actor->GetEquippedObject(false)!=weapon) {
  auto* defaults=RE::BGSDefaultObjectManager::GetSingleton();
  auto* right=defaults?defaults->GetObject<RE::BGSEquipSlot>(RE::DEFAULT_OBJECTS::kRightHandEquip):nullptr;
  // Two-handers and bows use their native both-hands slot.
  equip->EquipObject(actor,weapon,nullptr,1,Two(weapon)||weapon->IsBow()||weapon->IsCrossbow()?nullptr:right);
 }
 if(kit.shield) {
  auto* shield=RE::TESForm::LookupByID<RE::TESObjectARMO>(kit.shield);
  if(owns(shield) && actor->GetEquippedObject(true)!=shield)equip->EquipObject(actor,shield);
 }
 if(weapon->IsBow()||weapon->IsCrossbow()) {auto [bow,ammo]=Ranged(actor);if(ammo && actor->GetCurrentAmmo()!=ammo)equip->EquipObject(actor,ammo);}
 return weapon->GetName();
}
FWM::Hands Hands(RE::FormID actor) {
 // Automatic candidates must not become explicit selections or scripted casts.
 const auto savedPools=Of(actor);const auto pools=Effective(actor);FWM::Hands hands{};
 for(std::size_t h=0;h<2;h++)if(!savedPools[h].empty() && !pools[h].empty())hands[h]=pools[h].front();
 return hands;
}
bool WeaponAllowed(RE::Actor* actor,const RE::TESObjectWEAP* weapon,Role role) {
 if(!actor || !weapon || (role==Role::kBruiser && !Two(weapon) && Settings::Get().roleWeapons))return false;
 if(!FitsRole(const_cast<RE::TESObjectWEAP*>(weapon),role))return false;
 return LoadoutPolicy::Allows(Effective(actor->GetFormID())[2],weapon->GetFormID());
}
std::vector<View::Spell> Weapons(RE::Actor* actor,Role role) {
 std::vector<View::Spell> out;if(!actor)return out;
 for(const auto& [object,entry]:actor->GetInventory([](RE::TESBoundObject& o){return o.IsWeapon()||o.IsArmor();})) {
  if(!object || entry.first<=0)continue;
  auto* w=object->As<RE::TESObjectWEAP>();auto* armor=object->As<RE::TESObjectARMO>();
  if((!w || w->IsBound()) && (!armor || !armor->IsShield()))continue;
  View::Spell v;v.id=object->GetFormID();v.name=object->GetName();v.weapon=true;v.allowed=FitsRole(object,role);v.shield=armor!=nullptr;v.count=entry.first;v.cost=w?static_cast<float>(w->GetAttackDamage()):static_cast<float>(armor->GetArmorRating());v.twoHanded=Two(w);out.push_back(v);
 }
 std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.name<b.name;});return out;
}
std::pair<RE::TESObjectWEAP*,RE::TESAmmo*> Ranged(RE::Actor* actor) {
 if(!actor)return {};
 const auto items=actor->GetInventory([](RE::TESBoundObject& o){return o.IsWeapon()||o.IsAmmo();});
 auto* held=actor->GetEquippedObject(false);RE::TESObjectWEAP* bow=nullptr;
 for(const auto& [object,entry]:items){auto* w=object&&entry.first>0?object->As<RE::TESObjectWEAP>():nullptr;
  if(!w || !(w->IsBow()||w->IsCrossbow()) || !WeaponAllowed(actor,w,Role::kArcher))continue;
  if(!bow || w==held || (bow!=held && w->GetAttackDamage()>bow->GetAttackDamage()))bow=w;
 }
 if(!bow)return {};
 RE::TESAmmo* ammo=nullptr;auto* current=actor->GetCurrentAmmo();
 for(const auto& [object,entry]:items){auto* a=object&&entry.first>0?object->As<RE::TESAmmo>():nullptr;
  if(!a || a->IsBolt()!=bow->IsCrossbow())continue;
  if(!ammo || a==current || (ammo!=current && a->GetRuntimeData().data.damage>ammo->GetRuntimeData().data.damage))ammo=a;
 }
 return {bow,ammo};
}
Spells::Outcome Toggle(RE::Actor* actor,Role role,std::size_t index,RE::FormID item) {
 if(!actor || index>2 || !Party::CanHaveRole(actor))return {false,std::string(Tr("Companion unavailable."))};
 if(actor->IsInCombat())return {false,std::string(Tr("Change pools outside combat."))};
 const auto id=actor->GetFormID();auto pools=Of(id);auto& pool=pools[index];
 if(!item) pool.clear();
 else if(LoadoutPolicy::Contains(pool,item))std::erase(pool,item);
 else {
  if(pool.size()>=LoadoutPolicy::kLimit)return {false,std::string(Tr("This pool is full."))};
  if(index==2) {
   if(!FitsRole(RE::TESForm::LookupByID(item),role))return {false,std::string(Tr("Unavailable for this role."))};
   const auto options=Weapons(actor,role);if(std::none_of(options.begin(),options.end(),[&](const auto& w){return w.id==item;}))return {false,std::string(Tr("That weapon is not available in their inventory."))};
  }
  else {
   if(!ChoosesSpells(role))return {false,std::string(Tr("Only mages and healers have spell pools."))};
   const auto description=Spells::Describe(item,actor);
   if(description.twoHanded && pools[1-index].size()>=LoadoutPolicy::kLimit && !LoadoutPolicy::Contains(pools[1-index],item))return {false,std::string(Tr("The other hand's pool is full."))};
   const auto result=Spells::Choose(actor,role,index,item);if(!result.done)return result;
   // Both-handed rituals must be in both sets or an unrestricted opposite hand.
   const auto spell=Spells::Describe(item,actor);
   if(spell.twoHanded && !LoadoutPolicy::Contains(pools[1-index],item)) {
    if(pools[1-index].size()>=LoadoutPolicy::kLimit) return {false,std::string(Tr("The other hand's pool is full."))};
    pools[1-index].push_back(item);
   }
  }
  pool.push_back(item);
 }
 // Removing a two-handed ritual removes it from both pools.
 if(index<2 && item && !LoadoutPolicy::Contains(pool,item) && Spells::Describe(item,actor).twoHanded)std::erase(pools[1-index],item);
 saved[id]=std::move(pools);Party::SetHands(id,{});
 if(auto* member=Party::Find(id)){member->armed=false;member->handsChecked=false;member->spellIdle=0;}
 checked.erase(id); eligible.erase(id); Enforce(actor,role);
 return {true,std::string(Tr("Pool updated."))};
}
void Recheck(RE::Actor* actor,Role role) {
 if(!actor || Locked(actor->GetFormID()))return;
 if(AbilityLink::LoadoutMask(role))return; // compatibility is applied to Effective, not saved choices
 auto pools=Of(actor->GetFormID());bool changed=false;
 if(!ChoosesSpells(role))return;
 for(std::size_t h=0;h<2;h++){auto n=pools[h].size();std::erase_if(pools[h],[&](auto id){return !Spells::Allowed(id,actor,role);});changed|=n!=pools[h].size();}
 if(changed){saved[actor->GetFormID()]=std::move(pools);Party::SetHands(actor->GetFormID(),{});}
}
void Enforce(RE::Actor* actor,Role role) {
 if(!actor || actor->IsDead() || actor->IsInKillMove() || actor->IsAttacking())return;
 const auto now=::GetTickCount64();auto& last=checked[actor->GetFormID()];if(last && now-last<1000)return;last=now;
 auto* equip=RE::ActorEquipManager::GetSingleton();if(!equip)return;
 if(role==Role::kSkald){
  if(auto it=battles.find(actor->GetFormID());it!=battles.end())it->second.choice={};
  auto* defaults=RE::BGSDefaultObjectManager::GetSingleton();
  for(bool left:{false,true})if(auto* held=actor->GetEquippedObject(left)){
   if(held->GetFormID()==SkaldPolicy::kStanceSpell)continue;  // held like a mage's, never cast
   auto* slot=defaults?defaults->GetObject<RE::BGSEquipSlot>(left?RE::DEFAULT_OBJECTS::kLeftHandEquip:RE::DEFAULT_OBJECTS::kRightHandEquip):nullptr;
   if(auto* object=held->As<RE::TESBoundObject>())equip->UnequipObject(actor,object,nullptr,1,slot,false,true,false,true);
  }
  return;
 }
 if(Locked(actor->GetFormID())){EquipBattle(actor);return;}
 const auto pools=Of(actor->GetFormID());
 const bool restricted=AbilityLink::LoadoutMask(role).has_value();
 // A Bruiser keeps a two-hander in hand, unless role weapons are switched off.
 const bool twoHanders=role==Role::kBruiser && Settings::Get().roleWeapons;
 if(!restricted && !twoHanders && pools[2].empty())return;
 const auto allowed=Effective(actor->GetFormID());
 bool wrong=false;
 for(bool left:{false,true}) {
  auto* held=actor->GetEquippedObject(left);auto* w=held?held->As<RE::TESObjectWEAP>():nullptr;
  const auto* armor=held?held->As<RE::TESObjectARMO>():nullptr;
  const bool invalidSpell=restricted && held && held->As<RE::SpellItem>() && !LoadoutPolicy::Allows(allowed[left?0:1],held->GetFormID());
  const bool invalidShield=restricted && armor && armor->IsShield() && !LoadoutPolicy::Allows(allowed[2],held->GetFormID());
  if(held && ((w && !WeaponAllowed(actor,w,role)) || invalidSpell || invalidShield || (twoHanders && !w))) {
   if(auto* object=held->As<RE::TESBoundObject>())equip->UnequipObject(actor,object);
   wrong=true;
  }
 }
 if(twoHanders || (wrong && !ChoosesSpells(role))) {
  auto* held=actor->GetEquippedObject(false);auto* w=held?held->As<RE::TESObjectWEAP>():nullptr;
  if(w && WeaponAllowed(actor,w,role))return;
  RE::TESObjectWEAP* best=nullptr;
  for(const auto& option:Weapons(actor,role))if(auto* candidate=RE::TESForm::LookupByID<RE::TESObjectWEAP>(option.id);WeaponAllowed(actor,candidate,role) && (!best || candidate->GetAttackDamage()>best->GetAttackDamage()))best=candidate;
  if(best)equip->EquipObject(actor,best);
 }
}
std::vector<std::uint8_t> Encode(){std::vector<LoadoutPolicy::Saved> rows;for(const auto& [id,pools]:saved)rows.push_back({id,pools});return LoadoutPolicy::Encode(rows);}
void Decode(SKSE::SerializationInterface* intfc,const std::vector<std::uint8_t>& bytes){
 for(const auto& row:LoadoutPolicy::Decode(bytes)){
  RE::FormID actor=0;if(!intfc->ResolveFormID(row.actor,actor)||!actor)continue;
  Pools pools;for(std::size_t h=0;h<3;h++)for(auto id:row.pools[h]){RE::FormID resolved=0;if(intfc->ResolveFormID(id,resolved)&&resolved)pools[h].push_back(resolved);}
  saved[actor]=std::move(pools);
 }
}
void Clear(){saved.clear();EndBattle();}
}
