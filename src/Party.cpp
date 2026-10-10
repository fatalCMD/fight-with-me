#include "FWM/Party.h"

#include "FWM/Forms.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Settings.h"
#include "FWM/WalkWithMe.h"

namespace FWM::Party
{
	namespace
	{
		std::vector<Member>                    members;
		std::unordered_map<RE::FormID, Orders> orders;

		bool IsFollower(RE::Actor* a_actor, bool& a_summon)
		{
			a_summon = false;
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
			return Playable(Loadout(a_actor), a_arsenal, Settings::Get().healFallback);
		}

		// A saved role this companion can no longer play (the weapon was sold, the
		// borrowed heal switched off) gives way to the closest one they can.
		void Enforce(Member& a_member, RE::Actor* a_actor)
		{
			const auto it = orders.find(a_member.id);
			if (it == orders.end()) {
				return;
			}
			const bool lend = Settings::Get().healFallback;
			const auto lack = FWM::LackFor(it->second.role, a_member.arsenal, lend);
			if (lack == Lack::kNothing) {
				return;
			}
			const auto before = it->second.role;
			const auto after = Playable(Loadout(a_actor), a_member.arsenal, lend);
			it->second = ForRole(after, it->second);
			Log::Info("[Party] {} can no longer be a {} ({}) and fights as a {}."sv, a_member.name, Info(before).name, Why(lack), Info(after).name);
			Notices::Push(std::format("{} cannot be a {}: {} Now a {}.", a_member.name, Info(before).name, Why(lack), Info(after).name), Notices::Kind::kWarning);
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
			}
			member.handle = actor->GetHandle();
			member.id = actor->GetFormID();
			member.name = Game::Name(actor.get());
			member.summon = summon;
			member.managedByWalkWithMe = WalkWithMe::IsManaged(member.id);
			member.arsenalIn -= a_dt;
			if (!summon && (!member.arsenalKnown || member.arsenalIn <= 0.0f)) {
				member.arsenal = Game::ArsenalOf(actor.get());
				member.arsenalKnown = true;
				member.arsenalIn = actor->IsInCombat() ? 3.0f : 10.0f;  // an inventory scan is the costly part
			}
			if (!summon) {
				(void)OrdersFor(actor.get());
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
		members = std::move(next);
	}

	void ResetRuntime()
	{
		members.clear();
	}

	void Clear()
	{
		members.clear();
		orders.clear();
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

	Orders& OrdersFor(RE::Actor* a_actor)
	{
		const auto id = a_actor->GetFormID();
		if (const auto it = orders.find(id); it != orders.end()) {
			return it->second;
		}
		Orders fresh;
		if (Settings::Get().guessRoles) {
			fresh = ForRole(Guess(a_actor, Game::ArsenalOf(a_actor)));
		}
		Log::Info("[Party] {} joins as {} ({} / {})."sv, Game::Name(a_actor), Info(fresh.role).name,
			Info(fresh.focus).name, Info(fresh.position).name);
		return orders.emplace(id, fresh).first->second;
	}

	const Orders* FindOrders(RE::FormID a_id)
	{
		const auto it = orders.find(a_id);
		return it != orders.end() ? &it->second : nullptr;
	}

	void SetOrders(RE::FormID a_id, const Orders& a_orders)
	{
		orders[a_id] = a_orders;
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
		for (const auto& [id, saved] : a_orders) {
			orders[id] = saved;
		}
	}

	Lack LackFor(RE::FormID a_id, Role a_role)
	{
		const auto* member = Find(a_id);
		if (!member || !member->arsenalKnown) {
			return Lack::kNothing;
		}
		return FWM::LackFor(a_role, member->arsenal, Settings::Get().healFallback);
	}

	bool IsPartyActor(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}
		if (a_actor->IsPlayerRef()) {
			return true;
		}
		return Find(a_actor->GetFormID()) != nullptr;
	}
}
