#include "FWM/Party.h"
#include "FWM/Loadouts.h"

#include "FWM/Forms.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Settings.h"
#include "FWM/Text.h"
#include "FWM/WalkWithMe.h"
#include "FWM/AbilityLink.h"
#include "FWM/WindstepDecoys.h"

namespace FWM::Party
{
	namespace
	{
		std::vector<Member>                    members;
		std::unordered_map<RE::FormID, Orders> orders;
		std::unordered_map<RE::FormID, Hands>  hands;  // spells chosen for a mage's or healer's hands

		// Guessed with nothing in their hands, and for how many more seconds to look again.
		std::unordered_map<RE::FormID, float> unsure;

		// The cooldowns of a companion out of the roster (beyond the loaded area, or not back yet
		// after a load): they run down all the same and are handed back on return.
		struct Waits
		{
			float taunt{ 0.0f }, tauntLength{ 0.0f };
			float heal{ 0.0f };
			float rejuvenate{ 0.0f }, rejuvenateLength{ 0.0f };
			float special{ 0.0f }, specialLength{ 0.0f };

			[[nodiscard]] bool Running() const { return taunt > 0.0f || heal > 0.0f || rejuvenate > 0.0f || special > 0.0f; }

			void Down(float a_seconds)
			{
				taunt = Cooldowns::Down(taunt, a_seconds);
				heal = Cooldowns::Down(heal, a_seconds);
				rejuvenate = Cooldowns::Down(rejuvenate, a_seconds);
				special = Cooldowns::Down(special, a_seconds);
			}
		};
		std::unordered_map<RE::FormID, Waits> away;
		Cooldowns::Clock                      worldClock;

		Waits WaitsOf(const Member& a_member)
		{
			return { a_member.tauntReady, a_member.tauntCooldown, a_member.healReady, a_member.rejuvenateReady, a_member.rejuvenateCooldown,
				a_member.specialReady, a_member.specialCooldown };
		}

		void HandBack(Member& a_member, const Waits& a_waits)
		{
			a_member.tauntReady = a_waits.taunt;
			a_member.tauntCooldown = std::max(a_member.tauntCooldown, a_waits.tauntLength);
			a_member.healReady = a_waits.heal;
			a_member.rejuvenateReady = a_waits.rejuvenate;
			a_member.rejuvenateCooldown = std::max(a_member.rejuvenateCooldown, a_waits.rejuvenateLength);
			a_member.specialReady = a_waits.special;
			a_member.specialCooldown = std::max(a_member.specialCooldown, a_waits.specialLength);
		}

		bool IsFollower(RE::Actor* a_actor, bool& a_summon)
		{
			a_summon = false;
			if(WindstepDecoys::Is(a_actor->GetFormID())||AbilityLink::IsWindstepDecoy(a_actor->GetFormID()))return false;
			if (a_actor->IsCommandedActor()) {
				const auto commander = a_actor->GetCommandingActor();
				if (commander && commander->IsPlayerRef()) {
					a_summon = true;
					return true;
				}
			}
			if (a_actor->IsPlayerTeammate()) {
				return true;
			}
			if (auto* faction = Forms::Get().currentFollower) {
				if (a_actor->IsInFaction(faction) && a_actor->GetFactionRank(faction, false) >= 0) {
					return true;
				}
			}
			return WalkWithMe::IsManaged(a_actor->GetFormID());
		}

		Role Loadout(RE::Actor* a_actor)
		{
			if (auto* nff = Forms::Get().nffRole; nff && a_actor->IsInFaction(nff)) {
				switch (a_actor->GetFactionRank(nff, false)) {
				case 1:
					return Role::kHealer;
				case 2:
					return Role::kTank;
				default:
					break;
				}
			}
			switch (Game::LoadoutOf(a_actor)) {
			case Game::Loadout::kShield:
				return Role::kTank;
			case Game::Loadout::kTwoHanded:
				return Role::kBruiser;
			case Game::Loadout::kBow:
				return Role::kArcher;
			case Game::Loadout::kStaff:
				return Role::kMage;
			case Game::Loadout::kSpells:
				return Game::HealsParty(a_actor) ? Role::kHealer : Role::kMage;
			default:
				return Role::kFighter;
			}
		}

		Role Guess(RE::Actor* a_actor, const Arsenal& a_arsenal)
		{
			return Playable(Loadout(a_actor), a_arsenal);
		}

		// Straight after a load a companion can be in the fight before anything is in their
		// hands, and every first guess comes out Fighter. Guessed again once something is,
		// unless you have changed their orders since.
		void Reconsider(Member& a_member, RE::Actor* a_actor, float a_dt)
		{
			const auto it = unsure.find(a_member.id);
			if (it == unsure.end()) {
				return;
			}
			if (Game::LoadoutOf(a_actor) == Game::Loadout::kNone) {
				it->second -= a_dt;
				if (it->second <= 0.0f) {
					unsure.erase(it);  // a brawler after all
				}
				return;
			}
			unsure.erase(it);
			const auto saved = orders.find(a_member.id);
			if (saved == orders.end() || saved->second != ForRole(Role::kFighter)) {
				return;
			}
			const auto role = Guess(a_actor, a_member.arsenal);
			if (role == Role::kFighter) {
				return;
			}
			saved->second = ForRole(role);
			Log::Info("[Party] {} has something in hand now: a {} after all ({} / {})."sv, a_member.name, Info(role).name,
				Info(saved->second.focus).name, Info(saved->second.position).name);
		}

		// A saved role this companion can no longer play (the weapon was sold, the spells
		// forgotten) gives way to the closest one they can.
		void Enforce(Member& a_member, RE::Actor* a_actor)
		{
			const auto it = orders.find(a_member.id);
			if (it == orders.end()) {
				return;
			}
			const auto lack = FWM::LackFor(it->second.role, a_member.arsenal);
			if (lack == Lack::kNothing) {
				return;
			}
			const auto before = it->second.role;
			const auto after = Playable(Loadout(a_actor), a_member.arsenal);
			it->second = ForRole(after, it->second);
			Log::Info("[Party] {} can no longer be a {} ({}) and fights as a {}."sv, a_member.name, Info(before).name, Why(lack), Info(after).name);
			Notices::Push(Trf("{} cannot be a {}: {} Now a {}.", a_member.name, Info(before).name, Why(lack), Info(after).name), Notices::Kind::kWarning);
		}
	}

	void Refresh(float a_dt)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* lists = RE::ProcessLists::GetSingleton();
		if (!player || !lists) {
			members.clear();
			return;
		}

		std::vector<std::pair<RE::FormID, Waits>> before;
		for (const auto& member : members) {
			before.emplace_back(member.id, WaitsOf(member));
		}

		std::vector<Member> next;
		next.reserve(members.size() + 2);
		for (auto& handle : lists->highActorHandles) {
			auto actor = handle.get();
			if (!actor || actor.get() == player || actor->IsDead() || actor->IsDisabled() || !actor->Is3DLoaded()) {
				continue;
			}
			bool summon = false;
			if (!IsFollower(actor.get(), summon)) {
				continue;
			}
			Member member;
			if (auto* old = Find(actor->GetFormID())) {
				member = std::move(*old);  // keep this fight's state
			} else if (const auto back = away.find(actor->GetFormID()); back != away.end()) {
				HandBack(member, back->second);
				away.erase(back);
			}
			member.handle = actor->GetHandle();
			member.id = actor->GetFormID();
			member.name = Game::Name(actor.get());
			member.summon = summon;
            member.creature = !CanHaveRole(actor.get()) && !summon;
			member.managedByWalkWithMe = WalkWithMe::IsManaged(member.id);
			member.arsenalIn -= a_dt;
			if (member.CanTakeRole() && (!member.arsenalKnown || member.arsenalIn <= 0.0f)) {
				member.arsenal = Game::ArsenalOf(actor.get());
				member.arsenalKnown = true;
				member.arsenalIn = actor->IsInCombat() ? 3.0f : 10.0f;  // an inventory scan is the costly part
			}
			if (member.CanTakeRole()) {
				(void)OrdersFor(actor.get());
				Reconsider(member, actor.get(), a_dt);
				Enforce(member, actor.get());
			}
			next.push_back(std::move(member));
		}

		// Stable order for the menu: companions first, then summons, then by name.
		std::sort(next.begin(), next.end(), [](const Member& a, const Member& b) {
			if (a.summon != b.summon) {
				return !a.summon;
			}
			return a.name < b.name;
		});
		// Whoever left the roster keeps their cooldowns running, away from it.
		for (const auto& [id, waits] : before) {
			const bool stays = std::any_of(next.begin(), next.end(), [id](const Member& a_member) { return a_member.id == id; });
			if (!stays && waits.Running()) {
				away[id] = waits;
			}
		}
		members = std::move(next);
	}

	float Cool(float a_frame)
	{
		auto*       calendar = RE::Calendar::GetSingleton();
		const float seconds = calendar ? worldClock.Advance(a_frame, calendar->GetDaysPassed(), calendar->GetHour(), calendar->GetTimescale()) : a_frame;
		for (auto& member : members) {
			member.tauntReady = Cooldowns::Down(member.tauntReady, seconds);
			member.healReady = Cooldowns::Down(member.healReady, seconds);
			member.rejuvenateReady = Cooldowns::Down(member.rejuvenateReady, seconds);
			member.specialReady = Cooldowns::Down(member.specialReady, seconds);
		}
		std::erase_if(away, [seconds](auto& a_entry) {
			a_entry.second.Down(seconds);
			return !a_entry.second.Running();
		});
		return seconds;
	}

	std::vector<Cooldowns::Wait> SavedWaits()
	{
		std::vector<Cooldowns::Wait> out;
		auto add = [&](RE::FormID a_id, const Waits& a_waits) {
			auto one = [&](std::uint8_t a_key, float a_left, float a_length) {
				if (a_left > 0.0f) {
					out.push_back({ a_id, a_key, a_left, std::max(a_length, a_left) });
				}
			};
			one(Cooldowns::kTaunt, a_waits.taunt, a_waits.tauntLength);
			one(Cooldowns::kHeal, a_waits.heal, a_waits.heal);
			one(Cooldowns::kRejuvenate, a_waits.rejuvenate, a_waits.rejuvenateLength);
			one(Cooldowns::kSpecial, a_waits.special, a_waits.specialLength);
		};
		for (const auto& member : members) {
			if (!member.summon) {
				add(member.id, WaitsOf(member));
			}
		}
		for (const auto& [id, waits] : away) {
			add(id, waits);
		}
		return out;
	}

	void LoadWaits(std::span<const Cooldowns::Wait> a_waits)
	{
		for (const auto& wait : a_waits) {
			auto* member = Find(wait.id);
			auto  waits = member ? WaitsOf(*member) : away[wait.id];
			switch (wait.key) {
			case Cooldowns::kTaunt:
				waits.taunt = wait.left;
				waits.tauntLength = wait.length;
				break;
			case Cooldowns::kHeal:
				waits.heal = wait.left;
				break;
			case Cooldowns::kRejuvenate:
				waits.rejuvenate = wait.left;
				waits.rejuvenateLength = wait.length;
				break;
			case Cooldowns::kSpecial:
				waits.special = wait.left;
				waits.specialLength = wait.length;
				break;
			default:
				continue;  // a path ability's: Arts keeps those
			}
			if (member) {
				HandBack(*member, waits);
			} else {
				away[wait.id] = waits;
			}
		}
		std::erase_if(away, [](const auto& a_entry) { return !a_entry.second.Running(); });
	}

	void ResetRuntime()
	{
        Loadouts::EndBattle();
		members.clear();
		worldClock.Restart();
	}

	void Clear()
	{
		members.clear();
		orders.clear();
		hands.clear();
		unsure.clear();
		away.clear();
		worldClock.Restart();
	}

	std::vector<Member>& Members()
	{
		return members;
	}

	Member* Find(RE::FormID a_id)
	{
		for (auto& member : members) {
			if (member.id == a_id) {
				return &member;
			}
		}
		return nullptr;
	}

    bool CanHaveRole(RE::Actor* a_actor)
    {
        const auto* race = a_actor ? a_actor->GetRace() : nullptr;
        if (!race) return false;
        const bool creature = race->HasKeywordString("ActorTypeCreature") ||
            race->HasKeywordString("ActorTypeAnimal") || race->HasKeywordString("ActorTypeDragon");
        return RoleAllowed(race->HasKeywordString("ActorTypeNPC"), creature, a_actor->IsCommandedActor());
    }

    bool CanHaveRole(RE::FormID a_id)
    {
        return CanHaveRole(RE::TESForm::LookupByID<RE::Actor>(a_id));
    }

	Orders& OrdersFor(RE::Actor* a_actor)
	{
        // Only Refresh creates orders; keep accidental callers from assigning a creature.
        static Orders unassigned;
        if (!CanHaveRole(a_actor)) {
            unassigned = Orders{};
            unassigned.autoAbility = unassigned.autoSpecial = unassigned.autoRejuvenate = false;
            unassigned.position = Position::kFree;
            return unassigned;
        }
		const auto id = a_actor->GetFormID();
		if (const auto it = orders.find(id); it != orders.end()) {
			return it->second;
		}
		Orders fresh;
		if (Settings::Get().guessRoles) {
			fresh = ForRole(Guess(a_actor, Game::ArsenalOf(a_actor)));
			if (fresh.role == Role::kFighter && Game::LoadoutOf(a_actor) == Game::Loadout::kNone) {
				unsure[id] = 30.0f;
			}
		}
		Log::Info("[Party] {} joins as {} ({} / {})."sv, Game::Name(a_actor), Info(fresh.role).name,
			Info(fresh.focus).name, Info(fresh.position).name);
		return orders.emplace(id, fresh).first->second;
	}

	const Orders* FindOrders(RE::FormID a_id)
	{
        if (!CanHaveRole(a_id)) return nullptr;
		const auto it = orders.find(a_id);
        if(it!=orders.end())if(auto* battle=Loadouts::BattleOrders(a_id,it->second))return battle;
		return it != orders.end() ? &it->second : nullptr;
	}

	void SetOrders(RE::FormID a_id, const Orders& a_orders)
	{
        if (!CanHaveRole(a_id)) return;
		auto& kept = orders[a_id];
        const auto preferred=kept.role;
		kept = a_orders;
        if(Loadouts::Locked(a_id))kept.role=preferred;
		kept.position = Live(kept.position);
	}

	std::vector<std::pair<std::uint32_t, Orders>> SavedOrders()
	{
		std::vector<std::pair<std::uint32_t, Orders>> out(orders.begin(), orders.end());
		std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		return out;
	}

	void LoadOrders(const std::vector<std::pair<std::uint32_t, Orders>>& a_orders)
	{
		orders.clear();
		unsure.clear();
		for (const auto& [id, saved] : a_orders) {
			auto& kept = orders[id];
			kept = saved;
			kept.position = Live(kept.position);
		}
	}

	void AdoptOrders(const std::vector<std::pair<std::uint32_t, Orders>>& a_orders)
	{
		for (const auto& [id, saved] : a_orders) {
			if (const auto [it, added] = orders.try_emplace(id, saved); added) {
				it->second.position = Live(it->second.position);
			}
		}
	}

	Hands HandsOf(RE::FormID a_id)
	{
		const auto it = hands.find(a_id);
		return it != hands.end() ? it->second : Hands{};
	}

	void SetHands(RE::FormID a_id, const Hands& a_hands)
	{
        if (a_hands != Hands{} && !CanHaveRole(a_id)) return;
		if (a_hands == Hands{}) {
			hands.erase(a_id);
		} else {
			hands[a_id] = a_hands;
		}
	}

	std::vector<std::pair<std::uint32_t, Hands>> SavedHands()
	{
		std::vector<std::pair<std::uint32_t, Hands>> out(hands.begin(), hands.end());
		std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
		return out;
	}

	void LoadHands(const std::vector<std::pair<std::uint32_t, Hands>>& a_hands)
	{
		hands.clear();
		for (const auto& [id, saved] : a_hands) {
			SetHands(id, saved);
		}
	}

	Lack LackFor(RE::FormID a_id, Role a_role)
	{
		if (a_role == Role::kBruiser) return FWM::LackFor(a_role, Game::ArsenalOf(RE::TESForm::LookupByID<RE::Actor>(a_id)));
		const auto* member = Find(a_id);
		if (!member || !member->arsenalKnown) {
			return Lack::kNothing;
		}
		return FWM::LackFor(a_role, member->arsenal);
	}

	bool IsPartyActor(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}
		if (a_actor->IsPlayerRef()) {
			return true;
		}
		if(WindstepDecoys::Is(a_actor->GetFormID())||AbilityLink::IsWindstepDecoy(a_actor->GetFormID()))return true;
		return Find(a_actor->GetFormID()) != nullptr;
	}

	void Answer(Member& a_member, std::string_view a_text, bool a_done)
	{
		Log::Info("[Ability] {}: {}"sv, a_member.name, a_text);
		a_member.answer = std::string(a_text);
		a_member.answerAt = ::GetTickCount64();
		a_member.answerDone = a_done;
	}
}
