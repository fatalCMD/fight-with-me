#include "FWM/Skald.h"
#include "FWM/PathBonus.h"
#include "FWM/PartyBalance.h"

#include "FWM/Abilities.h"
#include "FWM/AzurasMoon.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/Party.h"
#include "FWM/PathsLink.h"
#include "FWM/Rules.h"
#include "FWM/Settings.h"
#include "FWM/TankActs.h"


namespace FWM::PathBonus
{
	namespace
	{
		// Lifts kept up on the planner's tick lapse this long after the fight stops asking.
		constexpr float kHold = 2.0f;

		struct Factor
		{
			const RE::Actor* body{ nullptr };
			float            healing{ 1.0f };
			float            cost{ 1.0f };
			float            taken{ 1.0f };

			friend bool operator==(const Factor&, const Factor&) = default;
		};
		std::atomic<std::shared_ptr<const std::vector<Factor>>> factors;

		std::atomic<std::uint64_t> heals{ 0 }, costs{ 0 }, blows{ 0 }, harms{ 0 };

		struct Known
		{
			Role  role{ Role::kCount };
			int   spent{ 0 };
			float percent{ 0.0f };

		};
		std::unordered_map<RE::FormID, Known> known;

		std::shared_ptr<const std::vector<Factor>> Table() { return factors.load(std::memory_order_acquire); }

		const Factor* Find(const std::vector<Factor>& a_table, const RE::TESObjectREFR* a_ref)
		{
			if (!a_ref) {
				return nullptr;
			}
			for (const auto& factor : a_table) {
				if (static_cast<const RE::TESObjectREFR*>(factor.body) == a_ref) {
					return &factor;
				}
			}
			return nullptr;
		}

		// A multiplier the game reads as 1 while it is still 0, as most actors' attack speeds are:
		// how much to add to make it a_share more than it is without this lift.
		float Raise(RE::Actor* a_actor, RE::FormID a_id, RE::ActorValue a_value, float a_share)
		{
			const auto  on = Abilities::BoostOn(a_id, a_value, kSource);
			const float without = Game::Current(a_actor, a_value) - (on ? on->amount : 0.0f);
			const float base = without > 0.0f ? without : 1.0f;
			return std::round((base * (1.0f + a_share) - without) * 1000.0f) / 1000.0f;
		}

		// --- Health effects: a healer's heals stronger, harm to a tank weaker -------------------

		// ActiveEffect slot 0 (AdjustForPerks, 33278 on SE): where the caster's perks and then the
		// target's set an effect's magnitude once it lands. Every restore-health effect a healer
		// causes passes here, from a spell they cast or a potion they drink, and every damage-health
		// effect that lands on a tank, before any of it is given.
		template <std::size_t I>
		struct Strengthen
		{
			static void Thunk(RE::ActiveEffect* a_this, RE::Actor* a_caster, RE::MagicTarget* a_target)
			{
				original(a_this, a_caster, a_target);
				if (!a_this || a_this->magnitude <= 0.0f) {
					return;
				}
				const auto* base = a_this->GetBaseObject();
                if (base && base->IsDetrimental() && (base->data.primaryAV == RE::ActorValue::kHealth || base->data.secondaryAV == RE::ActorValue::kHealth)) {
                    auto* outgoingTarget = a_target ? a_target->GetTargetStatsObject() : nullptr;
                    const bool instant=a_this->duration<=0 && a_this->spell && a_this->spell->GetSpellType()==RE::MagicSystem::SpellType::kSpell && a_this->spell->GetCastingType()==RE::MagicSystem::CastingType::kFireAndForget;
                    a_this->magnitude=Skald::Outgoing(a_caster,outgoingTarget?outgoingTarget->As<RE::Actor>():nullptr,a_this->magnitude,instant,true);
                    a_this->magnitude *= PartyBalance::Multiplier(a_caster, outgoingTarget ? outgoingTarget->As<RE::Actor>() : nullptr);
                    auto* ref = a_target ? a_target->GetTargetStatsObject() : nullptr;
                    if (AzurasMoon::Negate(a_caster, ref ? ref->As<RE::Actor>() : nullptr, a_this->magnitude)) {
                        a_this->magnitude = 0;
                        return;
                    }
                }
				if (!base || base->data.primaryAV != RE::ActorValue::kHealth) {
					return;
				}
				if (base->IsDetrimental()) {
					auto* ref = a_target ? a_target->GetTargetStatsObject() : nullptr;
					a_this->magnitude *= TankActs::Attack(a_caster, ref ? ref->As<RE::Actor>() : nullptr);
				}
				const auto table = Table();
				if (!table) {
					return;
				}
				if (base->IsDetrimental()) {
					const auto* factor = a_target ? Find(*table, a_target->GetTargetStatsObject()) : nullptr;
					if (factor && factor->taken < 1.0f) {
						a_this->magnitude *= factor->taken;
						harms.fetch_add(1, std::memory_order_relaxed);
					}
				} else if (!base->IsHostile()) {
					const auto* factor = Find(*table, a_caster);
					if (factor && factor->healing > 1.0f) {
						a_this->magnitude *= factor->healing;
						heals.fetch_add(1, std::memory_order_relaxed);
					}
				}
			}
			static inline REL::Relocation<decltype(Thunk)> original;
		};

		// --- Blows: a tank's share of every weapon hit ---------------------------------------

		// The helper HitData::Populate (42832) calls to adjust a hit's damage (42844 on SE) ends its
		// perk pass with the target's Mod Incoming Damage, on the hit's whole damage before armor and
		// blocking. Every blow and arrow that lands on anyone passes there.
		struct Lighten
		{
			static void Thunk(std::uint32_t a_point, RE::Actor* a_target, RE::Actor* a_attacker, RE::TESObjectWEAP* a_weapon, float* a_damage)
			{
				original(a_point, a_target, a_attacker, a_weapon, a_damage);
				if (!a_target || !a_damage) {
					return;
				}
                if (AzurasMoon::Negate(a_attacker, a_target, *a_damage)) { *a_damage = 0; return; }
				const float outgoing = TankActs::Attack(a_attacker, a_target);
				if (*a_damage <= 0.0f) return;
				*a_damage = Skald::Outgoing(a_attacker,a_target,*a_damage,true,false) * outgoing * PartyBalance::Multiplier(a_attacker, a_target);
				const auto table = Table();
				const auto* factor = table ? Find(*table, a_target) : nullptr;
				if (factor && factor->taken < 1.0f) {
					*a_damage *= factor->taken;
					blows.fetch_add(1, std::memory_order_relaxed);
				}
			}
			static inline REL::Relocation<decltype(Thunk)> original;
		};

		// --- Cost: a mage's spells, cheaper the more spent on their path ------------------------

		// MagicItem::CalculateMagickaCost (11213) ends with the caster's Mod Spell Cost perks. Every
		// cost the game works out passes there, for the cast itself, concentration's drain, the
		// combat AI weighing what it can afford, and the plan's spell list alike.
		struct Cheapen
		{
			static void Thunk(std::uint32_t a_point, RE::Actor* a_caster, RE::MagicItem* a_spell, float* a_cost)
			{
				original(a_point, a_caster, a_spell, a_cost);
				if (!a_caster || !a_spell || !a_cost || *a_cost <= 0.0f || a_spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell) {
					return;
				}
				const auto table = Table();
				const auto* factor = table ? Find(*table, a_caster) : nullptr;
				if (factor && factor->cost < 1.0f) {
					*a_cost *= factor->cost;
					costs.fetch_add(1, std::memory_order_relaxed);
				}
			}
			static inline REL::Relocation<decltype(Thunk)> original;
		};

		std::uintptr_t CallTarget(std::uintptr_t a_site)
		{
			std::int32_t relative = 0;
			std::memcpy(&relative, reinterpret_cast<const void*>(a_site + 1), sizeof(relative));
			return a_site + 5 + static_cast<std::intptr_t>(relative);
		}

		bool InCode(std::uintptr_t a_address)
		{
			const auto text = REL::Module::get().segment(REL::Segment::textx);
			return a_address >= text.address() && a_address < text.address() + text.size();
		}

		// `mov ecx, a_point` then `call BGSEntryPoint::HandleEntryPoint` inside a function. Found by
		// those bytes and that target, so a game build where they differ is left alone.
		std::uintptr_t EntryCall(std::uintptr_t a_function, std::uint8_t a_point, std::size_t a_length)
		{
			const REL::Relocation<std::uintptr_t> entryPoint{ RELOCATION_ID(23073, 23526) };
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(a_function);
			for (std::size_t i = 5; i < a_length; ++i) {
				if (bytes[i] == 0xE8 && bytes[i - 5] == 0xB9 && bytes[i - 4] == a_point && !bytes[i - 3] && !bytes[i - 2] && !bytes[i - 1] &&
					CallTarget(a_function + i) == entryPoint.address()) {
					return a_function + i;
				}
			}
			return 0;
		}

		// Mod Spell Cost in MagicItem::CalculateMagickaCost.
		std::uintptr_t CostCall()
		{
			return EntryCall(REL::Relocation<std::uintptr_t>{ RELOCATION_ID(11213, 11321) }.address(), 0x26, 0x180);
		}

		// Mod Incoming Damage, in the helper HitData::Populate calls.
		std::uintptr_t IncomingCall()
		{
			const auto  populate = REL::Relocation<std::uintptr_t>{ RELOCATION_ID(42832, 44001) }.address();
			const auto* bytes = reinterpret_cast<const std::uint8_t*>(populate);
			for (std::size_t i = 0; i < 0x800; ++i) {
				if (bytes[i] != 0xE8) {
					continue;
				}
				const auto callee = CallTarget(populate + i);
				if (!InCode(callee)) {
					continue;
				}
				if (const auto site = EntryCall(callee, 0x24, 0x200)) {
					return site;
				}
			}
			return 0;
		}
	}

	void Publish()
	{
		const auto& tuning = Settings::Get().tuning;
		std::vector<Factor> next;
		for (const auto& member : Party::Members()) {
			auto actor = member.handle.get();
			if (!member.CanTakeRole() || !actor || actor->IsCommandedActor() || actor->IsPlayerRef()) {
				continue;
			}
			const auto* orders = Party::FindOrders(member.id);
			const auto  kit = orders ? PathsLink::KitOf(member.id, orders->role) : PathsLink::Kit{};
			const int   spent = kit.tree && kit.record ? Paths::Spent(*kit.record, kit.role) : 0;
			const float percent = spent > 0 ? Rules::PathBonus(kit.role, spent, tuning) : 0.0f;
			auto&       was = known[member.id];
			if (was.role != kit.role || was.percent != percent) {
				if (percent > 0.0f) {
					Log::Info("[Paths] {}'s path bonus: {} ({} point(s) on the {})."sv, member.name, Rules::PathBonusText(kit.role, percent), spent,
						Paths::Info(kit.role).title);
				} else if (was.percent > 0.0f) {
					Log::Info("[Paths] {} has no path bonus now."sv, member.name);
				}
			}
			was.role = kit.role;
			was.spent = spent;
			was.percent = percent;
			Factor factor{ actor.get() };
			if (kit.role == Role::kHealer) {
				factor.healing = 1.0f + percent / 100.0f;
			} else if (kit.role == Role::kMage) {
				factor.cost = 1.0f - percent / 100.0f;
			} else if (kit.role == Role::kTank) {
				factor.taken = 1.0f - percent / 100.0f;
			}
			next.push_back(factor);
		}
		std::erase_if(known, [](const auto& a_entry) { return !Party::Find(a_entry.first) || !Party::CanHaveRole(a_entry.first); });
		const auto now = Table();
		if (!now || *now != next) {
			factors.store(std::make_shared<const std::vector<Factor>>(std::move(next)), std::memory_order_release);
		}
	}

	void Fight()
	{
		for (const auto& member : Party::Members()) {
			if (!member.CanTakeRole()) {
				continue;
			}
			const auto it = known.find(member.id);
			auto       actor = member.handle.get();
			if (it == known.end() || it->second.percent <= 0.0f || !actor || actor->IsCommandedActor() || !Game::Usable(actor.get()) || !actor->IsInCombat()) {
				continue;
			}
			auto&       bonus = it->second;
			const float share = bonus.percent / 100.0f;
			switch (bonus.role) {
			case Role::kFighter:
				for (const auto value : { RE::ActorValue::kWeaponSpeedMult, RE::ActorValue::kLeftWeaponSpeedMultiply }) {
					Abilities::Keep(member.id, value, Raise(actor.get(), member.id, value, share), kHold, kSource);
				}
				break;
			case Role::kBruiser:
				Abilities::Keep(member.id, RE::ActorValue::kAttackDamageMult, Raise(actor.get(), member.id, RE::ActorValue::kAttackDamageMult, share), kHold,
					kSource);
				break;
			case Role::kArcher:
				Abilities::Keep(member.id, RE::ActorValue::kStaminaRateMult, bonus.percent, kHold, kSource);
				break;
			default:
				break;
			}
		}
	}

	void Clear()
	{
        PartyBalance::Clear();
		known.clear();
		factors.store(nullptr, std::memory_order_release);
	}

	float Healing(RE::FormID a_id)
	{
		const auto it = known.find(a_id);
		return it != known.end() && it->second.role == Role::kHealer ? 1.0f + it->second.percent / 100.0f : 1.0f;
	}

	void Install()
	{
		TankActs::Install();
		// Health spells, enchantments and potions are value modifiers. Patch each concrete
		// vtable, including health absorption; inherited slots do not follow a base-table edit.
		REL::Relocation<std::uintptr_t> plain{ RE::VTABLE_ValueModifierEffect[0] };
		REL::Relocation<std::uintptr_t> conditions{ RE::VTABLE_ValueAndConditionsEffect[0] };
		REL::Relocation<std::uintptr_t> accumulating{ RE::VTABLE_AccumulatingValueModifierEffect[0] };
		REL::Relocation<std::uintptr_t> dual{ RE::VTABLE_DualValueModifierEffect[0] };
		REL::Relocation<std::uintptr_t> absorb{ RE::VTABLE_AbsorbEffect[0] };
		Strengthen<0>::original = plain.write_vfunc(0, Strengthen<0>::Thunk);
		Strengthen<1>::original = conditions.write_vfunc(0, Strengthen<1>::Thunk);
		Strengthen<2>::original = accumulating.write_vfunc(0, Strengthen<2>::Thunk);
		Strengthen<3>::original = dual.write_vfunc(0, Strengthen<3>::Thunk);
		Strengthen<4>::original = absorb.write_vfunc(0, Strengthen<4>::Thunk);

		const auto cost = CostCall();
		const auto incoming = IncomingCall();
		SKSE::AllocTrampoline(28);
		if (cost) {
			Cheapen::original = SKSE::GetTrampoline().write_call<5>(cost, Cheapen::Thunk);
		}
		if (incoming) {
			Lighten::original = SKSE::GetTrampoline().write_call<5>(incoming, Lighten::Thunk);
		}
		AzurasMoon::SetInterception(incoming != 0);
		Log::Info("[Hook] Path bonus: healing and incoming tank spells{}{}."sv,
			cost ? "; mages' spell costs"sv : "; NOT mages' spell costs (the call was not where it was expected)"sv,
			incoming ? "; incoming tank weapon damage"sv : "; NOT weapon damage (the call was not where it was expected)"sv);
	}

	Stats TakeStats()
	{
		return { heals.exchange(0), costs.exchange(0), blows.exchange(0), harms.exchange(0) };
	}
}
