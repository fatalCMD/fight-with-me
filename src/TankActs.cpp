#include "FWM/TankActs.h"
#include "FWM/Windsteps.h"

#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/Party.h"
#include "FWM/Settings.h"
#include "FWM/TankPolicy.h"
#include "FWM/WeakWills.h"

#include <mutex>

namespace FWM::TankActs
{
	namespace
	{
		struct Will
		{
			RE::ActorHandle actor;
			double until{ 0 };
			float length{ 0 }, percent{ 0 }, tagSeconds{ 0 };
			bool debug{false};
		};
		struct Tag { RE::ActorHandle actor; double until{ 0 }; float percent{ 0 }; };
		struct Guardian
		{
			RE::ActorHandle actor;
			RE::FormID id{ 0 };
			double until{ 0 };
			float length{ 0 }, share{ 0 }, reach{ 0 };
            std::uint64_t events{ 0 };
            float redirectedHealth{ 0 }, playerProtected{ 0 };
			bool debug{false};
		};
		std::mutex gate;
		double now{ 0 };
		std::unordered_map<RE::FormID, Will> wills;
		std::unordered_map<RE::FormID, Tag> tags;
		Guardian guardian;
		std::vector<RE::ActorHandle> party;
		bool installed{ false };
		thread_local bool redirected{ false };
		std::atomic<std::uint64_t> splits{ 0 }, weakened{ 0 };

		bool Standing(RE::Actor* actor)
		{
			return Game::Usable(actor) && !actor->AsActorState()->IsBleedingOut() && Game::Current(actor, RE::ActorValue::kHealth) > 0.0f;
		}
		std::vector<RE::ActorHandle> PartyNow()
		{
			std::vector<RE::ActorHandle> result;
			if (auto* player = RE::PlayerCharacter::GetSingleton()) result.push_back(player->GetHandle());
			for (const auto& member : Party::Members()) result.push_back(member.handle);
			return result;
		}
		bool Nearby(RE::Actor* tank, RE::Actor* ally, float reach)
		{
			return Standing(tank) && Standing(ally) && (tank->GetParentCell() == ally->GetParentCell() || (tank->GetWorldspace() && tank->GetWorldspace() == ally->GetWorldspace())) && Game::Distance(tank, ally) <= reach;
		}
		bool StillCarries(RE::FormID id, Paths::Ability ability)
		{
			const auto* orders = Party::FindOrders(id);
			return Party::Find(id) && orders && orders->role == Role::kTank && PathsLink::KitOf(id, orders->role).Carries(ability);
		}

        // Combat calls Actor's modifier implementation directly, bypassing the
        // ActorValueOwner vtable. Both routes call this final clamp BEFORE health is
        // written (SE 621120 + 101 -> vfunc 127). HandleHealthDamage is AFTER the write.
        // Chain the engine/mod clamp first to preserve god mode and essential limits.
        template <std::size_t I>
        struct Health {
            static float Thunk(RE::Actor* victim, RE::ActorValue av, float value) {
                float clamped = original(victim, av, value);
                if (redirected || av != RE::ActorValue::kHealth || !std::isfinite(clamped) || clamped >= 0)
                    return clamped;
                clamped *= Windsteps::DamageFactor(victim->GetFormID());
                Guardian guard;
                bool ally = false;
                {
                    std::scoped_lock lock(gate);
                    if (guardian.id && guardian.until > now) {
                        guard = guardian;
                        ally = std::find(party.begin(), party.end(), victim->GetHandle()) != party.end();
                    }
                }
                auto tank = guard.actor.get();
                const auto split = TankPolicy::Divide(-clamped, guard.share,
                    ally && Nearby(tank.get(), victim, guard.reach), tank.get() == victim, false);
                if (split.guardian <= 0) return clamped;
                // No second weapon hit, armor pass, or heal-back. Prevent nested splits.
                struct Bypass { Bypass() { redirected = true; } ~Bypass() { redirected = false; } } bypass;
                tank->AsActorValueOwner()->DamageActorValue(av, split.guardian);
                splits.fetch_add(1, std::memory_order_relaxed);
                {
                    std::scoped_lock lock(gate);
                    if (guardian.id == guard.id) {
                        ++guardian.events;
                        guardian.redirectedHealth += split.guardian;
                        if (victim->IsPlayerRef()) guardian.playerProtected += split.guardian;
                        if (guardian.events <= 3) Log::Info("[Tank] Last Bastion protects {}: {:.1f} -> {:.1f} health damage; {:.1f} redirected to {}."sv,
                            Game::Name(victim), -clamped, split.victim, split.guardian, Game::Name(tank.get()));
                    }
                }
                return -split.victim;
            }
            static inline REL::Relocation<decltype(Thunk)> original;
        };
        // Called with gate held, including zero-event casts so missing interception is visible.
        void EndGuardian(std::string_view reason) {
            if (guardian.id) Log::Info("[Tank] Last Bastion ended ({}): {} protected hits/ticks, {:.1f} health redirected ({:.1f} protected on player), guardian {:08X}."sv,
                reason, guardian.events, guardian.redirectedHealth, guardian.playerProtected, guardian.id);
            guardian = {};
        }
	}

	bool CanSave(RE::Actor* actor, float reach)
	{
		if (!Standing(actor) || (!actor->IsInCombat() && !PathsLink::DebugCasting(actor->GetFormID()))) return false;
		for (const auto& handle : PartyNow()) {
			auto ally = handle.get();
			if (!ally || !Nearby(actor, ally.get(), reach)) continue;
			if (TankPolicy::CriticalAlly(Game::Fraction(ally.get(), RE::ActorValue::kHealth), Game::Distance(actor, ally.get()), reach,
				actor == ally.get(), true, ally->AsActorState()->IsBleedingOut(), actor->IsInCombat() || ally->IsInCombat())) return true;
		}
		return false;
	}

	bool WeakWill(RE::Actor* actor, const PathsLink::Kit& kit)
	{
		if (!Standing(actor) || (!actor->IsInCombat() && !PathsLink::DebugCasting(actor->GetFormID()))) return false;
		const int rank = std::clamp(kit.Rank(Paths::Ability::kBodyguard), 1, 3);
		const float base = Paths::Second(Paths::Perk::kBodyguard, rank);
		const float seconds = base + kit.Value(Paths::Perk::kBodyguardInterpose);
		auto allies = PartyNow();
		{
			std::scoped_lock lock(gate);
			if (const auto it = wills.find(actor->GetFormID()); it != wills.end() && it->second.until > now) return false;
			wills[actor->GetFormID()] = { actor->GetHandle(), now + seconds, seconds, Paths::Value(Paths::Perk::kBodyguard, rank),
				base + (kit.Rank(Paths::Perk::kBodyguardRush) > 0 ? 4.0f : 0.0f), PathsLink::DebugCasting(actor->GetFormID()) };
			party = std::move(allies);
		}
		WeakWills::Call(actor, seconds, rank);
		return true;
	}

	bool LastBastion(RE::Actor* actor, const PathsLink::Kit& kit)
	{
		const float reach = TankPolicy::kBastionReach + kit.Value(Paths::Perk::kStandFastUnbroken);
		if (!installed || !Standing(actor) || (!PathsLink::DebugCasting(actor->GetFormID()) && !CanSave(actor, reach))) return false;
		const int rank = std::clamp(kit.Rank(Paths::Ability::kStandFast), 1, 3);
		const float seconds = TankPolicy::kBastionSeconds + kit.Value(Paths::Perk::kStandFastBreath);
		auto allies = PartyNow();
		std::scoped_lock lock(gate);
		if (guardian.id && guardian.until > now) {
			auto current = guardian.actor.get();
			if (Standing(current.get())) return false;
		}
		guardian = { actor->GetHandle(), actor->GetFormID(), now + seconds, seconds, Paths::Value(Paths::Perk::kStandFast, rank) / 100.0f, reach };
		guardian.debug=PathsLink::DebugCasting(actor->GetFormID());
		party = std::move(allies);
		return true;
	}

	float Attack(RE::Actor* attacker, RE::Actor* target, bool countDamage)
	{
		if (!attacker || !target || attacker == target) return 1.0f;
		const bool hostile = attacker->IsHostileToActor(target);
		std::scoped_lock lock(gate);
		const auto attackerHandle = attacker->GetHandle();
		const auto own = wills.find(attacker->GetFormID());
		const float selfPercent = own != wills.end() && own->second.until > now ? own->second.percent : 0.0f;
		if (attacker->IsPlayerRef() || std::find(party.begin(), party.end(), attackerHandle) != party.end()) {
			tags.erase(attacker->GetFormID());
			if (countDamage && selfPercent > 0) weakened.fetch_add(1, std::memory_order_relaxed);
			return TankPolicy::WillDamage(selfPercent, 0.0f, true);
		}
		if (const auto it = wills.find(target->GetFormID()); hostile && it != wills.end() && it->second.until > now && Standing(target)) {
			const auto& will = it->second;
			const auto id = attacker->GetFormID();
			// Bounded even in large battles; expired entries make room for new aggressors.
			std::erase_if(tags, [](const auto& tag) { return tag.second.until <= now; });
			if (tags.contains(id) || tags.size() < 128) {
				auto& tag = tags[id];
				if (tag.until <= now || will.percent >= tag.percent) {
					tag = { attacker->GetHandle(), now + will.tagSeconds, TankPolicy::Stronger(tag.until > now ? tag.percent / 100.0f : 0.0f, will.percent / 100.0f) * 100.0f };
				}
			}
		}
		const auto it = tags.find(attacker->GetFormID());
		const float taggedPercent = it != tags.end() && it->second.until > now ? it->second.percent : 0.0f;
		const float factor = TankPolicy::WillDamage(selfPercent, taggedPercent, false);
		if (countDamage && factor < 1) weakened.fetch_add(1, std::memory_order_relaxed);
		return factor;
	}
	Status WillOn(RE::FormID actor)
	{
		std::scoped_lock lock(gate);
		const auto it = wills.find(actor);
		return it == wills.end() ? Status{} : Status{ static_cast<float>(std::max(0.0, it->second.until - now)), it->second.length, it->second.percent };
	}
	Status BastionOn(RE::FormID actor)
	{
		std::scoped_lock lock(gate);
		return guardian.id == actor ? Status{ static_cast<float>(std::max(0.0, guardian.until - now)), guardian.length, guardian.share * 100.0f } : Status{};
	}
	void Tick(float dt)
	{
		if (!std::isfinite(dt) || dt <= 0.0f) return;
		if (!Settings::Get().specials) { Clear(); return; }
		std::vector<RE::FormID> ended;
		auto allies = PartyNow();
		{
			std::scoped_lock lock(gate);
			now += dt;
			std::erase_if(wills, [&](const auto& item) {
				auto actor = item.second.actor.get();
				if (item.second.until > now && Standing(actor.get()) && (item.second.debug ? PathsLink::Unlimited(item.first) : StillCarries(item.first, Paths::Ability::kBodyguard))) return false;
				ended.push_back(item.first); return true;
			});
			std::erase_if(tags, [](const auto& item) { auto actor = item.second.actor.get(); return item.second.until <= now || !Game::Usable(actor.get()); });
			auto tank = guardian.actor.get();
			if (guardian.id) {
                if (guardian.until <= now) EndGuardian("duration elapsed");
                else if (!Standing(tank.get())) EndGuardian("guardian down or unavailable");
                else if (!(guardian.debug ? PathsLink::Unlimited(guardian.id) : StillCarries(guardian.id, Paths::Ability::kStandFast))) EndGuardian("ability removed");
            }
			party = std::move(allies);
		}
		for (const auto id : ended) WeakWills::End(id);
	}
	void Clear()
	{
		std::vector<RE::FormID> ended;
		{
			std::scoped_lock lock(gate);
			for (const auto& [id, unused] : wills) ended.push_back(id);
			wills.clear(); tags.clear(); EndGuardian("combat ended or reset"); party.clear(); now = 0;
		}
		for (const auto id : ended) WeakWills::End(id);
		const auto count = splits.exchange(0), hits = weakened.exchange(0);
		if (count || hits) Log::Info("[Tank] Last Bastion split {} final health losses; Weak Will weakened {} attacks/effects."sv, count, hits);
	}
	void Install()
	{
		if (installed) return;
		REL::Relocation<std::uintptr_t> npc{ RE::VTABLE_Character[0] };
		REL::Relocation<std::uintptr_t> player{ RE::VTABLE_PlayerCharacter[0] };
		auto* who = RE::PlayerCharacter::GetSingleton();
		if (!who || *reinterpret_cast<std::uintptr_t*>(who) != player.address()) {
			Log::Info("[Tank] Last Bastion unavailable: Actor vtable did not match; no health hook installed."sv);
			return;
		}
		Health<0>::original = npc.write_vfunc(REL::Module::IsVR() ? 0x129 : 0x127, Health<0>::Thunk);
		Health<1>::original = player.write_vfunc(REL::Module::IsVR() ? 0x129 : 0x127, Health<1>::Thunk);
		installed = true;
		Log::Info("[Hook] Last Bastion: Character and Player pre-write health clamp, chained slot 127 (129 VR)."sv);
	}
}
