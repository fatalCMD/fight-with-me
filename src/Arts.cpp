#include "FWM/Arts.h"

#include "FWM/Abilities.h"
#include "FWM/Effects.h"
#include "FWM/Enemies.h"
#include "FWM/Forms.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Party.h"
#include "FWM/PathBonus.h"
#include "FWM/Settings.h"
#include "FWM/Specials.h"

namespace FWM::Arts
{
	namespace
	{
		using Paths::Ability;
		using Paths::Perk;

		std::uint16_t Source(Perk a_perk) { return static_cast<std::uint16_t>(a_perk); }

		// What each companion has going, beyond the cooldowns.
		struct State
		{
			std::array<Clock, Paths::kAbilityCount> clocks{};
			float berserk{ 0.0f }, berserkLength{ 0.0f }, bloodlust{ 0.0f };
			float riposte{ 0.0f }, riposteLength{ 0.0f }, counter{ 0.0f };
			bool  riposteDisarm{ false };
			float deadeye{ 0.0f }, deadeyeLength{ 0.0f }, hawkeye{ 0.0f };
			float surge{ 0.0f }, surgeLength{ 0.0f };
			float renewal{ 0.0f }, renewalLength{ 0.0f }, renewalPulse{ 0.0f }, renewalShare{ 0.0f }, renewalTriage{ 0.0f };
			float magnus{ 0.0f }, magnusLength{ 0.0f }, magnusPower{ 0.0f };
			bool  lastStandSpent{ false };
			bool  guardianSpent{ false };
			RE::ActorHandle unravelOn;
			float fletchIn{ 5.0f };
			std::uint64_t fletchToldAt{ 0 };
		};
		std::unordered_map<RE::FormID, State> states;

		// A blow that lands a moment after its swing starts.
		struct Strike
		{
			RE::ActorHandle                        by;
			float                                  in{ 0.0f };
			std::function<void(RE::Actor* a_by)>   land;
		};
		std::vector<Strike> strikes;

		struct Pin
		{
			RE::ActorHandle enemy;
			RE::ActorHandle by;
			float           left{ 0.0f };
		};
		std::vector<Pin> pins;

		struct Bleed
		{
			RE::ActorHandle enemy;
			float           perSecond{ 0.0f };
			float           left{ 0.0f };
		};
		std::vector<Bleed> bleeds;

		struct Rain
		{
			RE::ActorHandle    archer;
			RE::ActorHandle    over;
			RE::NiPoint3       centre;
			RE::TESAmmo*       ammo{ nullptr };
			RE::TESObjectWEAP* bow{ nullptr };
			int                left{ 0 };
			float              every{ 0.1f };
			float              in{ 0.0f };
			bool               pinning{ false };
		};
		std::vector<Rain> rains;

		// From the event sinks, which may not run on the main thread: acted on in Tick.
		struct Blocked
		{
			RE::FormID      defender{ 0 };
			RE::ActorHandle attacker;
		};
		std::mutex              eventLock;
		std::vector<Blocked>    blocked;
		std::vector<RE::FormID> killers;

		// Spells landing while someone draws on Magnus, to strengthen.
		struct SpellHit
		{
			RE::FormID      caster{ 0 };
			RE::ActorHandle target;
			RE::FormID      source{ 0 };
		};
		std::vector<SpellHit>                  spellHits;
		std::atomic<bool>                      magnusLive{ false };
		float                                  elapsed{ 0.0f };
		std::unordered_map<RE::FormID, float> lastStrengthened;

		std::mt19937 rng{ std::random_device{}() };

		State& StateOf(RE::FormID a_id) { return states[a_id]; }

		struct Result
		{
			bool        done{ false };
			std::string text;
		};

		Result Refused(std::string a_why) { return { false, std::move(a_why) }; }

		bool Stunned(RE::Actor* a_actor)
		{
			auto* state = a_actor->AsActorState();
			return state->IsStaggered() || state->IsBleedingOut() || a_actor->IsInRagdollState() || a_actor->IsInKillMove();
		}

		bool Standing(RE::Actor* a_actor)
		{
			return Game::Usable(a_actor) && !a_actor->AsActorState()->IsBleedingOut() && !a_actor->IsInRagdollState();
		}

		std::string CountOf(int a_count)
		{
			static constexpr std::array<std::string_view, 6> words{ "no enemy", "one enemy", "two enemies", "three enemies", "four enemies", "five enemies" };
			return a_count < static_cast<int>(words.size()) ? std::string(words[static_cast<std::size_t>(a_count)]) : std::format("{} enemies", a_count);
		}

		RE::NiPointer<RE::Actor> Opponent(RE::Actor* a_actor)
		{
			auto* controller = Game::Controller(a_actor);
			auto  target = controller ? controller->targetHandle.get() : RE::NiPointer<RE::Actor>{};
			return target && !target->IsDead() && Enemies::Find(controller->targetHandle) ? target : nullptr;
		}

		// The enemy the player sent them after, if still in the fight, else their opponent.
		RE::NiPointer<RE::Actor> Chosen(const Party::Member& a_member, RE::Actor* a_actor)
		{
			if (auto marked = a_member.marked.get(); marked && !marked->IsDead() && Enemies::Find(a_member.marked)) {
				return marked;
			}
			return Opponent(a_actor);
		}

		std::vector<RE::NiPointer<RE::Actor>> EnemiesWithin(const RE::NiPoint3& a_at, float a_reach)
		{
			std::vector<RE::NiPointer<RE::Actor>> out;
			for (const auto& profile : Enemies::All()) {
				auto enemy = profile.handle.get();
				if (enemy && Standing(enemy.get()) && enemy->GetPosition().GetDistance(a_at) <= a_reach) {
					out.push_back(std::move(enemy));
				}
			}
			return out;
		}

		int OnPlayer()
		{
			const auto player = RE::PlayerCharacter::GetSingleton()->GetHandle();
			int        count = 0;
			for (const auto& profile : Enemies::All()) {
				auto enemy = profile.handle.get();
				count += enemy && Game::Usable(enemy.get()) && profile.target == player ? 1 : 0;
			}
			return count;
		}

		int OnParty()
		{
			int count = 0;
			for (const auto& profile : Enemies::All()) {
				auto enemy = profile.handle.get();
				auto victim = profile.target.get();
				count += enemy && Game::Usable(enemy.get()) && victim && Party::IsPartyActor(victim.get()) ? 1 : 0;
			}
			return count;
		}

		bool Boss(RE::Actor* a_actor)
		{
			const auto* profile = Enemies::Find(a_actor->GetFormID());
			return profile ? profile->boss : Game::IsBoss(a_actor);
		}

		// The base damage of the weapon in hand: what Cleave, Earthshaker and a riposte multiply.
		float WeaponDamage(RE::Actor* a_actor)
		{
			for (const bool left : { false, true }) {
				auto*       form = a_actor->GetEquippedObject(left);
				const auto* weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr;
				if (weapon && !weapon->IsStaff() && weapon->attackDamage > 0) {
					return static_cast<float>(weapon->attackDamage);
				}
			}
			return 8.0f;
		}

		// You, the companions and their summons, alive and within reach.
		std::vector<RE::NiPointer<RE::Actor>> Team(RE::Actor* a_from, float a_reach)
		{
			std::vector<RE::NiPointer<RE::Actor>> team;
			auto add = [&](RE::NiPointer<RE::Actor> a_actor) {
				if (a_actor && !a_actor->IsDead() && a_actor->Is3DLoaded() && Game::Distance(a_from, a_actor.get()) <= a_reach) {
					team.push_back(std::move(a_actor));
				}
			};
			add(RE::NiPointer<RE::Actor>(RE::PlayerCharacter::GetSingleton()));
			for (auto& member : Party::Members()) {
				add(member.handle.get());
			}
			return team;
		}

		// A power attack, if the behaviour graph will take one now: the swing a blow lands with.
		bool PowerSwing(RE::Actor* a_actor)
		{
			if (!a_actor->AsActorState()->IsWeaponDrawn() || a_actor->IsAttacking()) {
				return false;
			}
			return a_actor->NotifyAnimationGraph("attackPowerStartInPlace") || a_actor->NotifyAnimationGraph("attackStart");
		}

		void Later(RE::Actor* a_by, float a_in, std::function<void(RE::Actor*)> a_land)
		{
			strikes.push_back({ a_by->GetHandle(), a_in, std::move(a_land) });
		}

		void Hurt(RE::Actor* a_enemy, float a_amount)
		{
			if (a_amount > 0.0f && a_enemy && !a_enemy->IsDead()) {
				a_enemy->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kHealth, a_amount);
			}
		}

		void Knock(RE::Actor* a_enemy, const RE::NiPoint3& a_from, float a_force)
		{
			if (auto* process = a_enemy->GetActorRuntimeData().currentProcess) {
				process->KnockExplosion(a_enemy, a_from, a_force);
			}
		}

		bool InSight(RE::Actor* a_from, RE::Actor* a_to, float a_reach)
		{
			bool unused = false;
			return Game::Distance(a_from, a_to) <= a_reach && a_from->HasLineOfSight(a_to, unused);
		}

		void Strip(RE::Actor* a_enemy, float a_amount, float a_seconds, Perk a_perk)
		{
			for (const auto value : { RE::ActorValue::kResistFire, RE::ActorValue::kResistFrost, RE::ActorValue::kResistShock }) {
				Abilities::Lift(a_enemy->GetFormID(), value, -a_amount, a_seconds, Source(a_perk));
			}
		}

		// --- Tank -----------------------------------------------------------------

		Result Challenge(Party::Member& a_member, RE::Actor* a_tank, const PathsLink::Kit& a_kit, bool a_asked)
		{
			auto*                    player = RE::PlayerCharacter::GetSingleton();
			RE::NiPointer<RE::Actor> target;
			if (a_asked) {
				target = Chosen(a_member, a_tank);
				if (!target) {
					float best = 1e9f;
					for (const auto& profile : Enemies::All()) {
						auto enemy = profile.handle.get();
						if (enemy && Game::Usable(enemy.get()) && profile.target == player->GetHandle() && Game::Distance(player, enemy.get()) < best) {
							best = Game::Distance(player, enemy.get());
							target = enemy;
						}
					}
				}
			} else {
				// A boss, or someone well above them, on the party and not already theirs.
				int bestWeight = -1;
				for (const auto& profile : Enemies::All()) {
					auto enemy = profile.handle.get();
					auto victim = profile.target.get();
					if (!enemy || !Game::Usable(enemy.get()) || !(profile.boss || profile.level >= a_tank->GetLevel() + 5) || !victim ||
						!Party::IsPartyActor(victim.get()) || victim.get() == a_tank || Abilities::HolderOf(profile.handle) == a_tank->GetHandle() ||
						!InSight(a_tank, enemy.get(), 2500.0f)) {
						continue;
					}
					const int weight = profile.level + (profile.boss ? 100 : 0);
					if (weight > bestWeight) {
						bestWeight = weight;
						target = enemy;
					}
				}
			}
			if (!target) {
				return a_asked ? Refused("Nobody to challenge.") : Result{};
			}
			const auto name = Game::Name(target.get());
			if (Game::Distance(a_tank, target.get()) > 2500.0f) {
				return Refused(std::format("{} is too far to hear it.", name));
			}
			const float seconds = Paths::Value(Perk::kChallenge, std::max(a_kit.Rank(Ability::kChallenge), 1));
			Abilities::Challenge(a_tank);
			Abilities::Play(Forms::Get().tauntSound, a_tank);
			Effects::Flash(a_tank, Effects::Look::kTaunt);
			Effects::Flash(target.get(), Effects::Look::kChallenge);
			Abilities::AddHold(target->GetHandle(), a_tank->GetHandle(), seconds);
			if (const float humble = a_kit.Value(Perk::kChallengeHumble) / 100.0f; humble > 0.0f) {
				Abilities::Lift(target->GetFormID(), RE::ActorValue::kAttackDamageMult, -humble, seconds, Source(Perk::kChallengeHumble));
			}
			if (const float duel = a_kit.Value(Perk::kChallengeDuel) / 100.0f; duel > 0.0f) {
				Abilities::Lift(a_tank->GetFormID(), RE::ActorValue::kAttackDamageMult, duel, seconds, Source(Perk::kChallengeDuel));
			}
			Notices::Push(std::format("{} challenges {}: it turns on them for {:.0f} s.", a_member.name, name, seconds), Notices::Kind::kTaunt);
			Log::Info("[Paths] {} challenges {} (level {}{}), held {:.0f} s."sv, a_member.name, name, target->GetLevel(), Boss(target.get()) ? ", a boss" : "", seconds);
			return { true, std::format("Challenged {}: on them for {:.0f} s.", name, seconds) };
		}

		Result StandFast(Party::Member& a_member, RE::Actor* a_tank, const PathsLink::Kit& a_kit, bool a_asked)
		{
			auto*       player = RE::PlayerCharacter::GetSingleton();
			const float yours = Game::Fraction(player, RE::ActorValue::kHealth);
			if (!a_asked && !(OnParty() >= 3 || (yours < 0.5f && OnPlayer() >= 1))) {
				return {};
			}
			const auto  team = Team(a_tank, Paths::kStandFastReach);
			const float armor = Paths::Value(Perk::kStandFast, std::max(a_kit.Rank(Ability::kStandFast), 1));
			const float unbroken = a_kit.Value(Perk::kStandFastUnbroken);
			const float breath = a_kit.Value(Perk::kStandFastBreath) / 100.0f;
			Abilities::Challenge(a_tank);
			Abilities::Play(Forms::Get().tauntSound, a_tank);
			for (const auto& ally : team) {
				Abilities::Lift(ally->GetFormID(), RE::ActorValue::kDamageResist, armor, Paths::kStandFastSeconds, Source(Perk::kStandFast));
				if (unbroken > 0.0f) {
					Abilities::Lift(ally->GetFormID(), RE::ActorValue::kResistMagic, unbroken, Paths::kStandFastSeconds, Source(Perk::kStandFastUnbroken));
				}
				if (breath > 0.0f) {
					ally->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kStamina, Game::Maximum(ally.get(), RE::ActorValue::kStamina) * breath);
				}
				Effects::Flash(ally.get(), Effects::Look::kStandFast);
			}
			Notices::Push(std::format("{}: \"Stand fast!\" +{:.0f} armor for {} of the party.", a_member.name, armor, team.size()), Notices::Kind::kOrder);
			Log::Info("[Paths] {} calls Stand Fast: +{:.0f} armor on {} for {:.0f} s."sv, a_member.name, armor, team.size(), Paths::kStandFastSeconds);
			return { true, std::format("Stand fast: +{:.0f} armor for {} of you.", armor, team.size()) };
		}

		// --- Healer ---------------------------------------------------------------

		Result Renewal(Party::Member& a_member, RE::Actor* a_healer, const PathsLink::Kit& a_kit, State& a_state, bool a_asked)
		{
			const auto team = Team(a_healer, Settings::Get().tuning.healReach);
			int        hurt = 0, scratched = 0, down = 0;
			for (const auto& ally : team) {
				const float health = Game::Fraction(ally.get(), RE::ActorValue::kHealth);
				hurt += health < 0.7f ? 1 : 0;
				scratched += health < 0.95f ? 1 : 0;
				down += ally->AsActorState()->IsBleedingOut() ? 1 : 0;
			}
			if (!a_asked && hurt < 2 && down == 0) {
				return {};
			}
			if (scratched == 0) {
				return Refused("Nobody within reach needs it.");
			}
			a_state.renewalShare = Paths::Value(Perk::kRenewal, std::max(a_kit.Rank(Ability::kRenewal), 1)) / 100.0f;
			a_state.renewalTriage = a_kit.Value(Perk::kTriage) / 100.0f;
			a_state.renewalLength = Paths::kRenewalSeconds + a_kit.Value(Perk::kRenewalRoots);
			a_state.renewal = a_state.renewalLength;
			a_state.renewalPulse = 0.0f;
			// Lifeline: whoever is down gets straight back up.
			int lifted = 0;
			if (a_kit.Rank(Perk::kRenewalLifeline) > 0) {
				for (const auto& ally : team) {
					if (ally->AsActorState()->IsBleedingOut()) {
						const float most = Game::Maximum(ally.get(), RE::ActorValue::kHealth);
						const float now = Game::Current(ally.get(), RE::ActorValue::kHealth);
						ally->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kHealth, std::max(most * Paths::kLifelineHealth - now, 0.0f));
						++lifted;
					}
				}
			}
			if (Settings::Get().abilityEffects) {
				Abilities::CastGesture(a_healer);
				Effects::Flash(a_healer, Effects::Look::kHealAura);
			}
			Abilities::Play(Forms::Get().healSound, a_healer);
			Notices::Push(std::format("{} calls up Renewal: {:.0f}% health a second for {:.0f} s{}.", a_member.name, a_state.renewalShare * 100.0f, a_state.renewalLength,
							  lifted ? std::format("; {} back on their feet", lifted) : ""s),
				Notices::Kind::kHeal);
			Log::Info("[Paths] {} starts Renewal ({:.0f}%/s for {:.0f} s; {} hurt, {} lifted)."sv, a_member.name, a_state.renewalShare * 100.0f, a_state.renewalLength,
				hurt, lifted);
			return { true, std::format("Renewal: {:.0f}% a second for {:.0f} s.", a_state.renewalShare * 100.0f, a_state.renewalLength) };
		}

		void Pulse(Party::Member& a_member, RE::Actor* a_healer, State& a_state)
		{
			for (const auto& ally : Team(a_healer, Settings::Get().tuning.healReach)) {
				const bool  badly = Game::Fraction(ally.get(), RE::ActorValue::kHealth) < Paths::kTriageLine;
				const float give = Game::Maximum(ally.get(), RE::ActorValue::kHealth) * a_state.renewalShare * (badly ? 1.0f + a_state.renewalTriage : 1.0f) *
				                   PathBonus::Healing(a_member.id);
				ally->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kHealth, give);
				if (Settings::Get().abilityEffects && Game::Fraction(ally.get(), RE::ActorValue::kHealth) < 0.99f) {
					Effects::Flash(ally.get(), Effects::Look::kHealAura, 0.9f);
				}
			}
		}

		// --- Bruiser --------------------------------------------------------------

		Result Berserk(Party::Member& a_member, RE::Actor* a_bruiser, const PathsLink::Kit& a_kit, State& a_state, bool a_asked)
		{
			const auto close = EnemiesWithin(a_bruiser->GetPosition(), 400.0f);
			bool       bossNear = false;
			for (const auto& enemy : EnemiesWithin(a_bruiser->GetPosition(), 1500.0f)) {
				bossNear = bossNear || Boss(enemy.get());
			}
			if (!a_asked && !(bossNear || close.size() >= 2)) {
				return {};
			}
			if (a_state.berserk > 0.0f) {
				return a_asked ? Refused("Already berserk.") : Result{};
			}
			const float damage = Paths::Value(Perk::kBerserk, std::max(a_kit.Rank(Ability::kBerserk), 1)) / 100.0f;
			a_state.berserk = a_state.berserkLength = Paths::kBerserkSeconds;
			a_state.bloodlust = a_kit.Value(Perk::kBerserkBloodlust) / 100.0f;
			Abilities::Lift(a_member.id, RE::ActorValue::kAttackDamageMult, damage, a_state.berserk, Source(Perk::kBerserk));
			if (const float armor = a_kit.Value(Perk::kBerserkThickSkull); armor > 0.0f) {
				Abilities::Lift(a_member.id, RE::ActorValue::kDamageResist, armor, a_state.berserk, Source(Perk::kBerserkThickSkull));
			}
			Abilities::Challenge(a_bruiser);
			Abilities::Play(Forms::Get().tauntSound, a_bruiser);
			Notices::Push(std::format("{} goes berserk: +{:.0f}% damage for {:.0f} s.", a_member.name, damage * 100.0f, a_state.berserk), Notices::Kind::kTaunt);
			Log::Info("[Paths] {} goes berserk (+{:.0f}% damage, {} close, boss near: {})."sv, a_member.name, damage * 100.0f, close.size(), bossNear);
			return { true, std::format("Berserk: +{:.0f}% damage.", damage * 100.0f) };
		}

		// Bloodlust: a kill while berserk heals and keeps the fury going.
		void Feed(Party::Member& a_member, RE::Actor* a_bruiser, State& a_state)
		{
			if (a_state.berserk <= 0.0f || a_state.bloodlust <= 0.0f) {
				return;
			}
			a_bruiser->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kHealth, Game::Maximum(a_bruiser, RE::ActorValue::kHealth) * a_state.bloodlust);
			a_state.berserk += 3.0f;
			a_state.berserkLength = std::max(a_state.berserkLength, a_state.berserk);
			for (const auto perk : { Perk::kBerserk, Perk::kBerserkThickSkull }) {
				for (const auto value : { RE::ActorValue::kAttackDamageMult, RE::ActorValue::kDamageResist }) {
					if (const auto on = Abilities::BoostOn(a_member.id, value, Source(perk))) {
						Abilities::Lift(a_member.id, value, on->amount, a_state.berserk, Source(perk));
					}
				}
			}
			Log::Info("[Paths] {}'s bloodlust: a kill feeds the fury ({:.0f} s left)."sv, a_member.name, a_state.berserk);
		}

		Result Sunder(Party::Member& a_member, RE::Actor* a_bruiser, const PathsLink::Kit& a_kit, bool a_asked)
		{
			auto target = Chosen(a_member, a_bruiser);
			if (!target || !Standing(target.get())) {
				return a_asked ? Refused("Nobody to sunder.") : Result{};
			}
			const auto  name = Game::Name(target.get());
			const float d = Game::Distance(a_bruiser, target.get());
			if (d > 350.0f) {
				return a_asked ? Refused(std::format("{} is out of reach ({:.0f} away).", name, d)) : Result{};
			}
			if (!a_asked && !(Boss(target.get()) || target->GetLevel() >= a_bruiser->GetLevel())) {
				return {};
			}
			const float armor = Paths::Value(Perk::kSunder, std::max(a_kit.Rank(Ability::kSunder), 1));
			const float crack = a_kit.Value(Perk::kSunderCrack);
			const float shatter = a_kit.Value(Perk::kSunderShatter);
			const auto  enemy = target->GetHandle();
			PowerSwing(a_bruiser);
			Effects::Flash(a_bruiser, Effects::Look::kStrike);
			Later(a_bruiser, 0.35f, [=](RE::Actor* a_by) {
				auto victim = enemy.get();
				if (!victim || victim->IsDead()) {
					return;
				}
				Abilities::Lift(victim->GetFormID(), RE::ActorValue::kDamageResist, -armor, Paths::kSunderSeconds, Source(Perk::kSunder));
				if (shatter > 0.0f) {
					Strip(victim.get(), shatter, Paths::kSunderSeconds, Perk::kSunderShatter);
					Abilities::Lift(victim->GetFormID(), RE::ActorValue::kResistMagic, -shatter, Paths::kSunderSeconds, Source(Perk::kSunderShatter));
				}
				if (crack > 0.0f) {
					Game::Stagger(victim.get(), crack, a_by);
				}
				Abilities::Play(Forms::Get().cleaveSound, victim.get());
				Effects::Flash(victim.get(), Effects::Look::kSunderImpact);
				Effects::Sparks(victim.get());
			});
			Notices::Push(std::format("{} sunders {}: -{:.0f} armor for {:.0f} s.", a_member.name, name, armor, Paths::kSunderSeconds), Notices::Kind::kTaunt);
			Log::Info("[Paths] {} sunders {} (-{:.0f} armor{}{})."sv, a_member.name, name, armor, shatter > 0.0f ? std::format(", -{:.0f}% resistances", shatter) : "",
				crack > 0.0f ? ", staggered" : "");
			return { true, std::format("Sundered {}: -{:.0f} armor.", name, armor) };
		}

		Result Earthshaker(Party::Member& a_member, RE::Actor* a_bruiser, const PathsLink::Kit& a_kit, bool a_asked)
		{
			const int   rank = std::max(a_kit.Rank(Ability::kEarthshaker), 1);
			const float reach = Paths::Value(Perk::kEarthshaker, rank) + a_kit.Value(Perk::kEarthshakerFault);
			const float times = Paths::Second(Perk::kEarthshaker, rank);
			const bool  aftershock = a_kit.Rank(Perk::kEarthshakerAftershock) > 0;
			const auto  close = EnemiesWithin(a_bruiser->GetPosition(), reach);
			if (!a_asked && close.size() < 3) {
				return {};
			}
			if (close.empty()) {
				return Refused("Nobody close enough to feel it.");
			}
			const float damage = WeaponDamage(a_bruiser) * times;
			PowerSwing(a_bruiser);
			Effects::Flash(a_bruiser, Effects::Look::kStrike);
			const auto name = a_member.name;
			Later(a_bruiser, 0.4f, [=](RE::Actor* a_by) {
				const auto origin = a_by->GetPosition();
				Abilities::Play(Forms::Get().quakeSound, a_by);
				Effects::Flash(a_by, Effects::Look::kEarthshaker);
				int knocked = 0;
				for (const auto& enemy : EnemiesWithin(origin, reach)) {
					Hurt(enemy.get(), damage);
					if (!enemy->IsDead()) {
						Knock(enemy.get(), origin, 1.5f);
						Effects::Flash(enemy.get(), Effects::Look::kShaken);
						++knocked;
					}
				}
				Log::Info("[Paths] {}'s Earthshaker: {} knocked down, {:.0f} damage each."sv, name, knocked, damage);
			});
			if (aftershock) {
				Later(a_bruiser, 1.9f, [=](RE::Actor* a_by) {
					Effects::Flash(a_by, Effects::Look::kEarthshaker, 1.2f);
					for (const auto& enemy : EnemiesWithin(a_by->GetPosition(), reach)) {
						Game::Stagger(enemy.get(), 0.5f, a_by);
					}
					Abilities::Play(Forms::Get().quakeSound, a_by);
				});
			}
			Notices::Push(std::format("{} shakes the earth: {} within {:.0f} go down.", a_member.name, CountOf(static_cast<int>(close.size())), reach),
				Notices::Kind::kTaunt);
			return { true, std::format("Earthshaker: {} in reach.", CountOf(static_cast<int>(close.size()))) };
		}

		// --- Fighter --------------------------------------------------------------

		Result SecondWind(Party::Member& a_member, RE::Actor* a_fighter, const PathsLink::Kit& a_kit, bool a_asked)
		{
			const float health = Game::Fraction(a_fighter, RE::ActorValue::kHealth);
			const float stamina = Game::Fraction(a_fighter, RE::ActorValue::kStamina);
			if (!a_asked && health >= 0.35f) {
				return {};
			}
			if (health > 0.95f && stamina > 0.95f) {
				return Refused("Not winded yet.");
			}
			const int   rank = std::max(a_kit.Rank(Ability::kSecondWind), 1);
			const float heal = Paths::Value(Perk::kSecondWind, rank) / 100.0f;
			const float breath = Paths::Second(Perk::kSecondWind, rank) / 100.0f;
			auto*       values = a_fighter->AsActorValueOwner();
			values->RestoreActorValue(RE::ActorValue::kHealth, Game::Maximum(a_fighter, RE::ActorValue::kHealth) * heal);
			values->RestoreActorValue(RE::ActorValue::kStamina, Game::Maximum(a_fighter, RE::ActorValue::kStamina) * breath);
			if (const float armor = a_kit.Value(Perk::kSecondWindResolve); armor > 0.0f) {
				Abilities::Lift(a_member.id, RE::ActorValue::kDamageResist, armor, Paths::kResolveSeconds, Source(Perk::kSecondWindResolve));
			}
			Effects::Flash(a_fighter, Effects::Look::kSecondWind);
			Notices::Push(std::format("{} gets a second wind.", a_member.name), Notices::Kind::kHeal);
			Log::Info("[Paths] {} gets a second wind at {:.0f}% health: +{:.0f}% health, +{:.0f}% stamina."sv, a_member.name, health * 100.0f, heal * 100.0f, breath * 100.0f);
			return { true, std::format("Second wind: {:.0f}% health back.", heal * 100.0f) };
		}

		Result Riposte(Party::Member& a_member, RE::Actor* a_fighter, const PathsLink::Kit& a_kit, State& a_state, bool a_asked)
		{
			int  on = 0;
			bool bossOn = false;
			for (const auto& profile : Enemies::All()) {
				auto enemy = profile.handle.get();
				if (enemy && Game::Usable(enemy.get()) && profile.target == a_fighter->GetHandle() && Game::Distance(a_fighter, enemy.get()) < 450.0f) {
					++on;
					bossOn = bossOn || profile.boss;
				}
			}
			if (!a_asked && !(on >= 2 || bossOn)) {
				return {};
			}
			a_state.riposte = a_state.riposteLength = Paths::Value(Perk::kRiposte, std::max(a_kit.Rank(Ability::kRiposte), 1));
			a_state.counter = a_kit.Value(Perk::kRiposteCounter);
			a_state.riposteDisarm = a_kit.Rank(Perk::kRiposteDisarming) > 0;
			Effects::Flash(a_fighter, Effects::Look::kGuard);
			Notices::Push(std::format("{} sets themselves to riposte.", a_member.name), Notices::Kind::kOrder);
			Log::Info("[Paths] {} readies a riposte for {:.0f} s ({} on them)."sv, a_member.name, a_state.riposte, on);
			return { true, std::format("Riposte: every blocked blow answered, {:.0f} s.", a_state.riposte) };
		}

		// A blow blocked while Riposte is up.
		void Retort(Party::Member& a_member, RE::Actor* a_fighter, State& a_state, RE::Actor* a_attacker)
		{
			if (!Enemies::Find(a_attacker->GetFormID()) || Game::Distance(a_fighter, a_attacker) > 450.0f || a_attacker->IsDead()) {
				return;
			}
			const bool staggered = Game::Stagger(a_attacker, 0.6f, a_fighter);
			Hurt(a_attacker, WeaponDamage(a_fighter) * a_state.counter);
			Effects::Flash(a_attacker, Effects::Look::kShaken);
			bool disarmed = false;
			if (a_state.riposteDisarm && a_attacker->GetLevel() <= a_fighter->GetLevel() + 10 && Specials::TakeWeapon(a_fighter, a_attacker)) {
				a_state.riposteDisarm = false;
				disarmed = true;
			}
			Log::Info("[Paths] {} ripostes {}{}{}."sv, a_member.name, Game::Name(a_attacker), staggered ? ", who staggers" : "", disarmed ? " and loses their weapon" : "");
		}

		Result Cleave(Party::Member& a_member, RE::Actor* a_fighter, const PathsLink::Kit& a_kit, bool a_asked)
		{
			const bool  whirl = a_kit.Rank(Perk::kCleaveWhirlwind) > 0;
			const float times = Paths::Value(Perk::kCleave, std::max(a_kit.Rank(Ability::kCleave), 1));
			const float bleed = a_kit.Value(Perk::kCleaveLacerate);
			auto        inArc = [whirl](RE::Actor* a_by, RE::Actor* a_enemy) {
                if (whirl) {
                    return true;
                }
                const float heading = a_by->GetAngleZ();
                const auto  to = a_enemy->GetPosition() - a_by->GetPosition();
                const float length = std::hypot(to.x, to.y);
                return length < 1.0f || (to.x * std::sin(heading) + to.y * std::cos(heading)) / length > 0.5f;
			};
			int inReach = 0;
			for (const auto& enemy : EnemiesWithin(a_fighter->GetPosition(), Paths::kCleaveReach)) {
				inReach += inArc(a_fighter, enemy.get()) ? 1 : 0;
			}
			if (!a_asked && inReach < 2) {
				return {};
			}
			if (inReach == 0) {
				return Refused("Nobody in reach.");
			}
			const float damage = WeaponDamage(a_fighter) * times;
			PowerSwing(a_fighter);
			const auto name = a_member.name;
			Later(a_fighter, 0.35f, [=](RE::Actor* a_by) {
				int hit = 0;
				for (const auto& enemy : EnemiesWithin(a_by->GetPosition(), Paths::kCleaveReach)) {
					if (!inArc(a_by, enemy.get())) {
						continue;
					}
					Hurt(enemy.get(), damage);
					Game::Stagger(enemy.get(), 0.35f, a_by);
					Effects::Flash(enemy.get(), Effects::Look::kShaken);
					if (bleed > 0.0f) {
						bleeds.push_back({ enemy->GetHandle(), bleed, Paths::kLacerateSeconds });
					}
					++hit;
				}
				if (hit > 0) {
					Abilities::Play(Forms::Get().cleaveSound, a_by);
				}
				Log::Info("[Paths] {}'s Cleave hits {} for {:.0f}{}."sv, name, CountOf(hit), damage, bleed > 0.0f ? std::format(", bleeding {:.0f}/s", bleed) : "");
			});
			Effects::Flash(a_fighter, Effects::Look::kCleave);
			return { true, std::format("Cleave: {} in reach.", CountOf(inReach)) };
		}

		// --- Archer ---------------------------------------------------------------

		// The bow in hand (or the best carried) and the arrows they would shoot from it.
		std::pair<RE::TESObjectWEAP*, RE::TESAmmo*> BowAndArrows(RE::Actor* a_archer)
		{
			RE::TESObjectWEAP* bow = nullptr;
			if (auto* right = a_archer->GetEquippedObject(false)) {
				if (auto* weapon = right->As<RE::TESObjectWEAP>(); weapon && weapon->IsBow()) {
					bow = weapon;
				}
			}
			RE::TESAmmo* ammo = a_archer->GetCurrentAmmo();
			if (ammo && ammo->IsBolt()) {
				ammo = nullptr;
			}
			std::int32_t most = 0;
			for (const auto& [object, entry] : a_archer->GetInventory([](RE::TESBoundObject& a_object) { return a_object.IsWeapon() || a_object.IsAmmo(); })) {
				if (!object || entry.first <= 0) {
					continue;
				}
				if (auto* weapon = object->As<RE::TESObjectWEAP>(); !bow && weapon && weapon->IsBow()) {
					bow = weapon;
				}
				if (auto* arrows = object->As<RE::TESAmmo>(); !ammo && arrows && !arrows->IsBolt() && entry.first > most) {
					most = entry.first;
					ammo = arrows;
				}
			}
			return { bow, ammo };
		}

		Result Deadeye(Party::Member& a_member, RE::Actor* a_archer, const PathsLink::Kit& a_kit, State& a_state, bool a_asked)
		{
			bool worth = false;
			for (const auto& profile : Enemies::All()) {
				auto enemy = profile.handle.get();
				if (enemy && Game::Usable(enemy.get()) && (profile.boss || profile.caster) && InSight(a_archer, enemy.get(), 3000.0f)) {
					worth = true;
					break;
				}
			}
			if (!a_asked && !worth) {
				return {};
			}
			if (a_state.deadeye > 0.0f) {
				return a_asked ? Refused("Already steady.") : Result{};
			}
			const float damage = Paths::Value(Perk::kDeadeye, std::max(a_kit.Rank(Ability::kDeadeye), 1)) / 100.0f;
			a_state.deadeye = a_state.deadeyeLength = Paths::kDeadeyeSeconds + a_kit.Value(Perk::kDeadeyePatience);
			a_state.hawkeye = a_kit.Value(Perk::kDeadeyeHawkeye) / 100.0f;
			Abilities::Lift(a_member.id, RE::ActorValue::kAttackDamageMult, damage, a_state.deadeye, Source(Perk::kDeadeye));
			Effects::Flash(a_archer, Effects::Look::kVolley);
			Notices::Push(std::format("{} steadies their aim: +{:.0f}% damage for {:.0f} s.", a_member.name, damage * 100.0f, a_state.deadeye), Notices::Kind::kOrder);
			Log::Info("[Paths] {} takes Deadeye (+{:.0f}% for {:.0f} s)."sv, a_member.name, damage * 100.0f, a_state.deadeye);
			return { true, std::format("Deadeye: +{:.0f}% damage.", damage * 100.0f) };
		}

		void Pinned(RE::Actor* a_archer, const RE::ActorHandle& a_enemy, float a_seconds, float a_bossChance, const std::string& a_by, float a_in)
		{
			Later(a_archer, a_in, [=](RE::Actor* a_from) {
				auto enemy = a_enemy.get();
				if (!enemy || enemy->IsDead()) {
					return;
				}
				if (Boss(enemy.get()) && std::uniform_real_distribution<float>(0.0f, 100.0f)(rng) >= a_bossChance) {
					Log::Info("[Paths] {} shrugs off {}'s pinning shot."sv, Game::Name(enemy.get()), a_by);
					return;
				}
				Game::Stagger(enemy.get(), 0.7f, a_from);
				Effects::Flash(enemy.get(), Effects::Look::kShaken);
				pins.push_back({ a_enemy, a_from->GetHandle(), a_seconds });
				Log::Info("[Paths] {} pins {} for {:.0f} s."sv, a_by, Game::Name(enemy.get()), a_seconds);
			});
		}

		Result PinningShot(Party::Member& a_member, RE::Actor* a_archer, const PathsLink::Kit& a_kit, bool a_asked)
		{
			auto*                    player = RE::PlayerCharacter::GetSingleton();
			RE::NiPointer<RE::Actor> target;
			if (a_asked) {
				target = Chosen(a_member, a_archer);
			}
			if (!target) {
				// Whoever is closing on you, or on a caster of the party.
				float best = 1e9f;
				for (const auto& profile : Enemies::All()) {
					auto enemy = profile.handle.get();
					auto victim = profile.target.get();
					if (!enemy || !Standing(enemy.get()) || !victim) {
						continue;
					}
					bool guarded = victim.get() == player;
					if (!guarded) {
						if (const auto* member = Party::Find(victim->GetFormID()); member && !member->summon) {
							const auto* orders = Party::FindOrders(member->id);
							guarded = orders && ChoosesSpells(orders->role);
						}
					}
					const float d = Game::Distance(victim.get(), enemy.get());
					if (guarded && d < 600.0f && d < best && InSight(a_archer, enemy.get(), 3000.0f)) {
						best = d;
						target = enemy;
					}
				}
			}
			if (!target) {
				return a_asked ? Refused("Nobody to pin.") : Result{};
			}
			if (!(a_member.arsenal.ranged && a_member.arsenal.ammo)) {
				return Refused("Nothing to shoot with.");
			}
			if (!Specials::LooseArrow(a_archer, target.get())) {
				return Refused("Could not get a shot off.");
			}
			const int   rank = std::max(a_kit.Rank(Ability::kPinningShot), 1);
			const float seconds = Paths::Value(Perk::kPinningShot, rank) + a_kit.Value(Perk::kPinningHeavy);
			const float bossChance = a_kit.Second(Perk::kPinningHeavy);
			const float flight = Game::Distance(a_archer, target.get()) / 3500.0f + 0.05f;
			Pinned(a_archer, target->GetHandle(), seconds, bossChance, a_member.name, flight);
			if (a_kit.Rank(Perk::kPinningTwin) > 0) {
				RE::NiPointer<RE::Actor> second;
				float                    best = 600.0f;
				for (const auto& enemy : EnemiesWithin(target->GetPosition(), 600.0f)) {
					if (enemy != target && Game::Distance(target.get(), enemy.get()) < best) {
						best = Game::Distance(target.get(), enemy.get());
						second = enemy;
					}
				}
				if (second && Specials::LooseArrow(a_archer, second.get())) {
					Pinned(a_archer, second->GetHandle(), seconds, bossChance, a_member.name, Game::Distance(a_archer, second.get()) / 3500.0f + 0.05f);
				}
			}
			const auto name = Game::Name(target.get());
			return { true, std::format("Pinning shot at {}.", name) };
		}

		Result ArrowRain(Party::Member& a_member, RE::Actor* a_archer, const PathsLink::Kit& a_kit, bool a_asked)
		{
			RE::NiPointer<RE::Actor> centre;
			if (a_asked) {
				centre = Chosen(a_member, a_archer);
			}
			if (!centre) {
				// Where they stand thickest.
				std::size_t most = 0;
				for (const auto& profile : Enemies::All()) {
					auto enemy = profile.handle.get();
					if (!enemy || !Standing(enemy.get()) || !InSight(a_archer, enemy.get(), 3500.0f)) {
						continue;
					}
					const auto around = EnemiesWithin(enemy->GetPosition(), Paths::kRainRadius).size();
					if (around > most) {
						most = around;
						centre = enemy;
					}
				}
				if (!a_asked && most < 3) {
					return {};
				}
			}
			if (!centre) {
				return a_asked ? Refused("Nobody to rain on.") : Result{};
			}
			const auto [bow, ammo] = BowAndArrows(a_archer);
			if (!bow || !ammo) {
				return Refused("Needs a bow and arrows to copy.");
			}
			const int count = static_cast<int>(Paths::Value(Perk::kArrowRain, std::max(a_kit.Rank(Ability::kArrowRain), 1)) + a_kit.Value(Perk::kArrowRainDownpour));
			rains.push_back({ a_archer->GetHandle(), centre->GetHandle(), centre->GetPosition(), ammo, bow, count, Paths::kRainSeconds / static_cast<float>(count), 0.0f,
				a_kit.Rank(Perk::kArrowRainPinning) > 0 });
			Effects::Flash(a_archer, Effects::Look::kArrowRain);
			const auto name = Game::Name(centre.get());
			Notices::Push(std::format("{} calls down a rain of {} arrows on {}.", a_member.name, count, name), Notices::Kind::kOrder);
			Log::Info("[Paths] {} rains {} arrows on {}."sv, a_member.name, count, name);
			return { true, std::format("Arrow rain: {} arrows on {}.", count, name) };
		}

		void Fall(Rain& a_rain)
		{
			auto archer = a_rain.archer.get();
			if (!archer) {
				a_rain.left = 0;
				return;
			}
			if (auto over = a_rain.over.get(); over && !over->IsDead()) {
				a_rain.centre = over->GetPosition();
			}
			std::uniform_real_distribution<float> unit(0.0f, 1.0f);
			const float  angle = unit(rng) * 6.2831853f;
			const float  radius = std::sqrt(unit(rng)) * Paths::kRainRadius;
			RE::NiPoint3 spot = a_rain.centre + RE::NiPoint3{ std::cos(angle) * radius, std::sin(angle) * radius, 40.0f };
			RE::NiPoint3 origin = spot + RE::NiPoint3{ (unit(rng) - 0.5f) * 160.0f, (unit(rng) - 0.5f) * 160.0f, 1100.0f };
			const auto   dir = spot - origin;
			RE::Projectile::ProjectileRot rotation{ -std::atan2(dir.z, std::hypot(dir.x, dir.y)), std::atan2(dir.x, dir.y) };
			RE::ProjectileHandle          handle;
			RE::Projectile::LaunchArrow(&handle, archer.get(), a_rain.ammo, a_rain.bow, origin, rotation);
			--a_rain.left;
		}

		// --- Mage -----------------------------------------------------------------

		Result Surge(Party::Member& a_member, RE::Actor* a_mage, const PathsLink::Kit& a_kit, State& a_state, bool a_asked)
		{
			const float magicka = Game::Fraction(a_mage, RE::ActorValue::kMagicka);
			if (!a_asked && !(a_member.drained || magicka < 0.3f)) {
				return {};
			}
			if (a_state.surge > 0.0f) {
				return a_asked ? Refused("Their magicka is already surging.") : Result{};
			}
			const float times = Paths::Value(Perk::kSurge, std::max(a_kit.Rank(Ability::kSurge), 1));
			a_state.surge = a_state.surgeLength = Paths::kSurgeSeconds;
			Abilities::Lift(a_member.id, RE::ActorValue::kMagickaRateMult, (times - 1.0f) * 100.0f, a_state.surge, Source(Perk::kSurge));
			if (const float overflow = a_kit.Value(Perk::kSurgeOverflow) / 100.0f; overflow > 0.0f) {
				a_mage->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kMagicka, Game::Maximum(a_mage, RE::ActorValue::kMagicka) * overflow);
			}
			if (const float conduit = a_kit.Value(Perk::kSurgeConduit); conduit > 0.0f) {
				for (const auto& ally : Team(a_mage, 4000.0f)) {
					if (ally.get() != a_mage) {
						Abilities::Lift(ally->GetFormID(), RE::ActorValue::kMagickaRateMult, conduit, a_state.surge, Source(Perk::kSurgeConduit));
					}
				}
			}
			if (Settings::Get().abilityEffects) {
				Abilities::CastGesture(a_mage);
			}
			Abilities::Play(Forms::Get().rejuvenateSound, a_mage);
			Notices::Push(std::format("{}'s magicka surges: {:.0f}x as fast for {:.0f} s.", a_member.name, times, a_state.surge), Notices::Kind::kHeal);
			Log::Info("[Paths] {} surges ({:.0f}x magicka, at {:.0f}%)."sv, a_member.name, times, magicka * 100.0f);
			return { true, std::format("Arcane surge: magicka {:.0f}x as fast.", times) };
		}

		// Fire, frost or shock: the element of the spells they hold, or of most of those they
		// know; fire if none.
		std::size_t ElementOf(const Party::Member& a_member, RE::Actor* a_mage)
		{
			std::array<int, 3> count{};
			auto               note = [&](const RE::SpellItem* a_spell, int a_weight) {
                if (!a_spell) {
                    return;
                }
                for (const auto* effect : a_spell->effects) {
                    const auto* base = effect ? effect->baseEffect : nullptr;
                    if (!base || !base->IsHostile()) {
                        continue;
                    }
                    switch (base->data.resistVariable) {
                    case RE::ActorValue::kResistFire:
                        count[0] += a_weight;
                        return;
                    case RE::ActorValue::kResistFrost:
                        count[1] += a_weight;
                        return;
                    case RE::ActorValue::kResistShock:
                        count[2] += a_weight;
                        return;
                    default:
                        break;
                    }
                }
			};
			for (const auto id : Party::HandsOf(a_member.id)) {
				note(id ? RE::TESForm::LookupByID<RE::SpellItem>(id) : nullptr, 10);
			}
			for (auto* spell : Game::SpellsOf(a_mage)) {
				note(spell, 1);
			}
			const auto best = std::max_element(count.begin(), count.end());
			return *best > 0 ? static_cast<std::size_t>(best - count.begin()) : 0;
		}

		bool Hurl(RE::Actor* a_mage, RE::SpellItem* a_spell, RE::Actor* a_target)
		{
			auto* caster = a_mage->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
			if (!caster || !a_spell || !a_target) {
				return false;
			}
			caster->CastSpellImmediate(a_spell, false, a_target, 1.0f, false, 0.0f, a_mage);
			return true;
		}

		Result Burst(Party::Member& a_member, RE::Actor* a_mage, const PathsLink::Kit& a_kit, bool a_asked)
		{
			auto target = a_asked ? Chosen(a_member, a_mage) : Opponent(a_mage);
			if (!target || !InSight(a_mage, target.get(), 3000.0f)) {
				return a_asked ? Refused("Nobody in sight to strike.") : Result{};
			}
			const auto element = ElementOf(a_member, a_mage);
			const auto tier = static_cast<std::size_t>(std::clamp(static_cast<int>(Paths::Value(Perk::kBurst, std::max(a_kit.Rank(Ability::kBurst), 1))), 1, 3) - 1);
			auto*      spell = Forms::Get().bursts[element][tier];
			if (!spell || !Hurl(a_mage, spell, target.get())) {
				return Refused("Could not cast it.");
			}
			if (Settings::Get().abilityEffects) {
				Abilities::CastGesture(a_mage);
			}
			Abilities::HandGlow(a_mage, spell, 1.0f, kRightHand);
			const float searing = a_kit.Value(Perk::kBurstSearing);
			if (searing > 0.0f) {
				Strip(target.get(), searing, Paths::kSearingSeconds, Perk::kBurstSearing);
			}
			if (a_kit.Rank(Perk::kBurstTwin) > 0) {
				const auto first = target->GetHandle();
				Later(a_mage, 0.45f, [=](RE::Actor* a_from) {
					auto                     was = first.get();
					RE::NiPointer<RE::Actor> next;
					if (was) {
						for (const auto& enemy : EnemiesWithin(was->GetPosition(), 800.0f)) {
							if (enemy != was) {
								next = enemy;
								break;
							}
						}
					}
					if (!next) {
						next = was;
					}
					if (next && !next->IsDead() && Hurl(a_from, spell, next.get()) && searing > 0.0f) {
						Strip(next.get(), searing, Paths::kSearingSeconds, Perk::kBurstSearing);
					}
				});
			}
			const auto name = Game::Name(target.get());
			Log::Info("[Paths] {} hurls {} at {}."sv, a_member.name, spell->GetName(), name);
			return { true, std::format("{} at {}.", spell->GetName(), name) };
		}

		// Magnus, the top of the Storm: for a while their magicka never runs dry, then their
		// destruction strikes harder, then magic turns aside from them too.
		Result Magnus(Party::Member& a_member, RE::Actor* a_mage, const PathsLink::Kit& a_kit, State& a_state, bool a_asked)
		{
			if (a_state.magnus > 0.0f) {
				return a_asked ? Refused(std::format("Magnus is already with them ({:.0f} s).", std::ceil(a_state.magnus))) : Result{};
			}
			bool boss = false;
			int  enemies = 0;
			for (const auto& profile : Enemies::All()) {
				auto enemy = profile.handle.get();
				if (enemy && Game::Usable(enemy.get())) {
					++enemies;
					boss = boss || profile.boss;
				}
			}
			const float magicka = Game::Fraction(a_mage, RE::ActorValue::kMagicka);
			if (!a_asked && !(a_member.drained || (boss && magicka < 0.7f) || (enemies >= 3 && magicka < 0.4f))) {
				return {};
			}
			const int rank = std::max(a_kit.Rank(Ability::kMagnus), 1);
			a_state.magnus = a_state.magnusLength = Paths::Value(Perk::kMagnus, rank);
			a_state.magnusPower = Paths::Second(Perk::kMagnus, rank) / 100.0f;
			const float resist = Paths::Third(Perk::kMagnus, rank);
			a_mage->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kMagicka, Game::Maximum(a_mage, RE::ActorValue::kMagicka));
			if (resist > 0.0f) {
				Abilities::Lift(a_member.id, RE::ActorValue::kResistMagic, resist, a_state.magnus, Source(Perk::kMagnus));
			}
			magnusLive = true;
			if (Settings::Get().abilityEffects) {
				Abilities::CastGesture(a_mage);
				Effects::Flash(a_mage, Effects::Look::kWardAura);
			}
			Abilities::Play(Forms::Get().rejuvenateSound, a_mage);
			Notices::Push(std::format("{} draws on Magnus: magicka without end for {:.0f} s.", a_member.name, a_state.magnus), Notices::Kind::kHeal);
			Log::Info("[Paths] {} draws on Magnus (rank {}): {:.0f} s, destruction +{:.0f}%, resist magic +{:.0f}%."sv, a_member.name, rank, a_state.magnus,
				a_state.magnusPower * 100.0f, resist);
			return { true, std::format("Magnus: magicka without end for {:.0f} s.", a_state.magnus) };
		}

		// While Magnus holds: the magicka they spend comes straight back.
		void Brim(RE::Actor* a_mage)
		{
			const float most = Game::Maximum(a_mage, RE::ActorValue::kMagicka);
			const float now = Game::Current(a_mage, RE::ActorValue::kMagicka);
			if (most > now) {
				a_mage->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kMagicka, most - now);
			}
		}

		// A destruction spell from someone under Magnus lands harder: a share of its own magnitude
		// on top. Concentration spells hit many times a second, so theirs are counted at most every
		// half second per target, at half the share.
		void Strengthen(RE::Actor* a_caster, float a_power, RE::Actor* a_target, RE::FormID a_source)
		{
			auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_source);
			auto* effect = spell ? spell->GetCostliestEffectItem() : nullptr;
			auto* base = effect ? effect->baseEffect : nullptr;
			if (!base || !base->IsHostile() || base->data.associatedSkill != RE::ActorValue::kDestruction || !Enemies::Find(a_target->GetFormID())) {
				return;
			}
			float share = a_power;
			if (spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration) {
				auto& last = lastStrengthened[a_target->GetFormID()];
				if (elapsed - last < 0.5f) {
					return;
				}
				last = elapsed;
				share *= 0.5f;
			}
			Hurt(a_target, effect->effectItem.magnitude * share);
			(void)a_caster;
		}

		// --- Traits -----------------------------------------------------------------

		// Lifts kept up while they fight; each lapses a moment after it stops being asked for.
		void Traits(const Party::Member& a_member, RE::Actor* a_actor, const PathsLink::Kit& a_kit, State& a_state)
		{
			constexpr float kHold = 2.0f;
			auto keep = [&](RE::ActorValue a_value, float a_amount, Perk a_perk) { Abilities::Keep(a_member.id, a_value, a_amount, kHold, Source(a_perk)); };
			const auto opponent = Opponent(a_actor);
			switch (a_kit.role) {
			case Role::kTank:
				keep(RE::ActorValue::kDamageResist, a_kit.Value(Perk::kIronHide), Perk::kIronHide);
				break;
			case Role::kHealer:
				keep(RE::ActorValue::kMagickaRateMult, a_kit.Value(Perk::kSerenity), Perk::kSerenity);
				break;
			case Role::kBruiser:
				{
					const bool low = opponent && Game::Fraction(opponent.get(), RE::ActorValue::kHealth) < Paths::kExecuteLine;
					keep(RE::ActorValue::kAttackDamageMult, low ? a_kit.Value(Perk::kExecutioner) / 100.0f : 0.0f, Perk::kExecutioner);
				}
				break;
			case Role::kFighter:
				{
					keep(RE::ActorValue::kAttackDamageMult, a_kit.Value(Perk::kFlanker) / 100.0f, Perk::kFlanker);
					keep(RE::ActorValue::kStaminaRateMult, a_kit.Value(Perk::kEndurance), Perk::kEndurance);
					bool open = false;
					if (opponent) {
						auto*      right = opponent->GetEquippedObject(false);
						const bool armed = right && right->As<RE::TESObjectWEAP>() && !right->As<RE::TESObjectWEAP>()->IsHandToHandMelee();
						open = opponent->AsActorState()->IsStaggered() || (!armed && !opponent->IsPlayerTeammate() && opponent->GetRace() &&
																			   opponent->GetRace()->HasKeywordString("ActorTypeNPC"));
					}
					keep(RE::ActorValue::kAttackDamageMult, open ? a_kit.Value(Perk::kOpportunist) / 100.0f : 0.0f, Perk::kOpportunist);
				}
				break;
			case Role::kArcher:
				keep(RE::ActorValue::kAttackDamageMult, a_kit.Value(Perk::kSharpshooter) / 100.0f, Perk::kSharpshooter);
				{
					const bool quarry = a_state.deadeye > 0.0f && opponent && Enemies::Find(opponent->GetFormID()) &&
					                    (Enemies::Find(opponent->GetFormID())->caster || Enemies::Find(opponent->GetFormID())->archer);
					keep(RE::ActorValue::kAttackDamageMult, quarry ? a_state.hawkeye : 0.0f, Perk::kDeadeyeHawkeye);
				}
				break;
			case Role::kMage:
				{
					keep(RE::ActorValue::kMagickaRateMult, a_kit.Value(Perk::kArcaneFlow), Perk::kArcaneFlow);
					keep(RE::ActorValue::kResistMagic, a_kit.Value(Perk::kSpellShield), Perk::kSpellShield);
					keep(RE::ActorValue::kAbsorbChance, a_kit.Second(Perk::kSpellShield), Perk::kSpellShield);
					// Unravel follows whoever they fight.
					const float unravel = a_kit.Value(Perk::kUnravel);
					const auto  now = opponent && unravel > 0.0f ? opponent->GetHandle() : RE::ActorHandle{};
					if (a_state.unravelOn && a_state.unravelOn != now) {
						if (auto was = a_state.unravelOn.get()) {
							Abilities::DropSource(Source(Perk::kUnravel), was->GetFormID());
						}
					}
					a_state.unravelOn = now;
					if (auto on = now.get()) {
						for (const auto value : { RE::ActorValue::kResistFire, RE::ActorValue::kResistFrost, RE::ActorValue::kResistShock }) {
							Abilities::Keep(on->GetFormID(), value, -unravel, kHold, Source(Perk::kUnravel));
						}
					}
				}
				break;
			default:
				break;
			}
		}

		// Once a fight, at the brink.
		void Brink(Party::Member& a_member, RE::Actor* a_actor, const PathsLink::Kit& a_kit, State& a_state)
		{
			if (a_kit.role == Role::kTank && !a_state.lastStandSpent && a_kit.Rank(Perk::kLastStand) > 0 && !a_actor->AsActorState()->IsBleedingOut() &&
				Game::Fraction(a_actor, RE::ActorValue::kHealth) < 0.25f) {
				a_state.lastStandSpent = true;
				const float share = a_kit.Value(Perk::kLastStand) / 100.0f;
				a_actor->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kHealth, Game::Maximum(a_actor, RE::ActorValue::kHealth) * share);
				Abilities::Lift(a_member.id, RE::ActorValue::kDamageResist, Paths::kLastStandArmor, Paths::kLastStandSeconds, Source(Perk::kLastStand));
				Effects::Flash(a_actor, Effects::Look::kRally);
				Notices::Push(std::format("{} makes a last stand.", a_member.name), Notices::Kind::kTaunt);
				Log::Info("[Paths] {} makes a last stand: +{:.0f}% health, +{:.0f} armor."sv, a_member.name, share * 100.0f, Paths::kLastStandArmor);
			}
			if (a_kit.role == Role::kHealer && !a_state.guardianSpent && a_kit.Rank(Perk::kGuardianSpirit) > 0) {
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!player->IsDead() && Game::Fraction(player, RE::ActorValue::kHealth) < Paths::kGuardianLine) {
					a_state.guardianSpent = true;
					const float share = a_kit.Value(Perk::kGuardianSpirit) / 100.0f * PathBonus::Healing(a_member.id);
					player->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kHealth, Game::Maximum(player, RE::ActorValue::kHealth) * share);
					Effects::Flash(player, Effects::Look::kHealAura, 2.0f);
					Effects::Flash(a_actor, Effects::Look::kHealAura);
					Notices::Push(std::format("{}'s guardian spirit pulls you back: +{:.0f}% health.", a_member.name, share * 100.0f), Notices::Kind::kHeal);
					Log::Info("[Paths] {}'s Guardian Spirit heals you for {:.0f}%."sv, a_member.name, share * 100.0f);
				}
			}
		}

		// --- Running them -------------------------------------------------------------

		Result Run(Party::Member& a_member, RE::Actor* a_actor, const PathsLink::Kit& a_kit, State& a_state, Ability a_ability, bool a_asked)
		{
			switch (a_ability) {
			case Ability::kChallenge:
				return Challenge(a_member, a_actor, a_kit, a_asked);
			case Ability::kStandFast:
				return StandFast(a_member, a_actor, a_kit, a_asked);
			case Ability::kRenewal:
				return Renewal(a_member, a_actor, a_kit, a_state, a_asked);
			case Ability::kBerserk:
				return Berserk(a_member, a_actor, a_kit, a_state, a_asked);
			case Ability::kSunder:
				return Sunder(a_member, a_actor, a_kit, a_asked);
			case Ability::kEarthshaker:
				return Earthshaker(a_member, a_actor, a_kit, a_asked);
			case Ability::kSecondWind:
				return SecondWind(a_member, a_actor, a_kit, a_asked);
			case Ability::kRiposte:
				return Riposte(a_member, a_actor, a_kit, a_state, a_asked);
			case Ability::kCleave:
				return Cleave(a_member, a_actor, a_kit, a_asked);
			case Ability::kDeadeye:
				return Deadeye(a_member, a_actor, a_kit, a_state, a_asked);
			case Ability::kPinningShot:
				return PinningShot(a_member, a_actor, a_kit, a_asked);
			case Ability::kArrowRain:
				return ArrowRain(a_member, a_actor, a_kit, a_asked);
			case Ability::kSurge:
				return Surge(a_member, a_actor, a_kit, a_state, a_asked);
			case Ability::kBurst:
				return Burst(a_member, a_actor, a_kit, a_asked);
			case Ability::kMagnus:
				return Magnus(a_member, a_actor, a_kit, a_state, a_asked);
			default:
				return {};
			}
		}

		// --- Events -----------------------------------------------------------------

		struct HitSink final : RE::BSTEventSink<RE::TESHitEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* a_event, RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				if (a_event && a_event->target && a_event->cause && a_event->flags.any(RE::TESHitEvent::Flag::kHitBlocked)) {
					auto* attacker = a_event->cause->As<RE::Actor>();
					if (attacker) {
						const std::scoped_lock guard(eventLock);
						blocked.push_back({ a_event->target->GetFormID(), attacker->GetHandle() });
					}
				}
				if (magnusLive && a_event && a_event->target && a_event->cause && a_event->source) {
					auto* target = a_event->target->As<RE::Actor>();
					if (target) {
						const std::scoped_lock guard(eventLock);
						spellHits.push_back({ a_event->cause->GetFormID(), target->GetHandle(), a_event->source });
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		} hitSink;

		struct DeathSink final : RE::BSTEventSink<RE::TESDeathEvent>
		{
			RE::BSEventNotifyControl ProcessEvent(const RE::TESDeathEvent* a_event, RE::BSTEventSource<RE::TESDeathEvent>*) override
			{
				if (a_event && a_event->dead && a_event->actorKiller) {
					const std::scoped_lock guard(eventLock);
					killers.push_back(a_event->actorKiller->GetFormID());
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		} deathSink;

		void TakeEvents()
		{
			std::vector<Blocked>    hits;
			std::vector<RE::FormID> kills;
			std::vector<SpellHit>   spells;
			{
				const std::scoped_lock guard(eventLock);
				hits.swap(blocked);
				kills.swap(killers);
				spells.swap(spellHits);
			}
			for (const auto& hit : spells) {
				auto it = states.find(hit.caster);
				auto target = hit.target.get();
				if (it != states.end() && it->second.magnus > 0.0f && it->second.magnusPower > 0.0f && target && !target->IsDead()) {
					Strengthen(RE::TESForm::LookupByID<RE::Actor>(hit.caster), it->second.magnusPower, target.get(), hit.source);
				}
			}
			for (const auto& hit : hits) {
				auto* member = Party::Find(hit.defender);
				auto  it = states.find(hit.defender);
				auto  attacker = hit.attacker.get();
				if (!member || it == states.end() || it->second.riposte <= 0.0f || !attacker) {
					continue;
				}
				if (auto fighter = member->handle.get()) {
					Retort(*member, fighter.get(), it->second, attacker.get());
				}
			}
			for (const auto killer : kills) {
				auto* member = Party::Find(killer);
				auto  actor = member ? member->handle.get() : RE::NiPointer<RE::Actor>{};
				const auto* orders = member ? Party::FindOrders(member->id) : nullptr;
				if (!actor || !orders) {
					continue;
				}
				const auto kit = PathsLink::KitOf(member->id, orders->role);
				if (!kit.tree) {
					continue;
				}
				auto& state = StateOf(member->id);
				if (kit.role == Role::kBruiser) {
					if (const float momentum = kit.Value(Perk::kMomentum) / 100.0f; momentum > 0.0f) {
						actor->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kHealth, Game::Maximum(actor.get(), RE::ActorValue::kHealth) * momentum);
					}
					Feed(*member, actor.get(), state);
				}
			}
		}
	}

	bool IsNew(Paths::Ability a_ability)
	{
		switch (a_ability) {
		case Ability::kNone:
		case Ability::kTaunt:
		case Ability::kBodyguard:
		case Ability::kHeal:
		case Ability::kRejuvenate:
		case Ability::kWard:
		case Ability::kWarCry:
		case Ability::kDisarm:
		case Ability::kVolley:
			return false;
		default:
			return true;
		}
	}

	void Request(RE::FormID a_member, Paths::Ability a_ability)
	{
		if (a_ability != Ability::kNone && static_cast<std::size_t>(a_ability) < Paths::kAbilityCount) {
			StateOf(a_member).clocks[static_cast<std::size_t>(a_ability)].requested = true;
		}
	}

	bool Asked(RE::FormID a_member)
	{
		const auto it = states.find(a_member);
		return it != states.end() && std::any_of(it->second.clocks.begin(), it->second.clocks.end(), [](const Clock& a_clock) { return a_clock.requested; });
	}

	Clock ClockOf(RE::FormID a_member, Paths::Ability a_ability)
	{
		const auto it = states.find(a_member);
		return it == states.end() || static_cast<std::size_t>(a_ability) >= Paths::kAbilityCount ? Clock{} : it->second.clocks[static_cast<std::size_t>(a_ability)];
	}

	void Cool(float a_seconds)
	{
		for (auto& [id, state] : states) {
			for (auto& clock : state.clocks) {
				clock.ready = Cooldowns::Down(clock.ready, a_seconds);
			}
		}
	}

	std::vector<Cooldowns::Wait> SavedWaits()
	{
		std::vector<Cooldowns::Wait> out;
		for (const auto& [id, state] : states) {
			for (std::size_t a = 1; a < Paths::kAbilityCount; ++a) {
				const auto& clock = state.clocks[a];
				if (clock.ready > 0.0f) {
					out.push_back({ id, static_cast<std::uint8_t>(a), clock.ready, std::max(clock.cooldown, clock.ready) });
				}
			}
		}
		return out;
	}

	void LoadWaits(std::span<const Cooldowns::Wait> a_waits)
	{
		for (const auto& wait : a_waits) {
			if (wait.key == 0 || wait.key >= Paths::kAbilityCount) {
				continue;  // not a path ability's: the party keeps those
			}
			auto& clock = StateOf(wait.id).clocks[wait.key];
			clock.ready = wait.left;
			clock.cooldown = wait.length;
		}
	}

	void Update()
	{
		const auto& settings = Settings::Get();
		TakeEvents();
		for (auto& member : Party::Members()) {
			auto& state = StateOf(member.id);
			if (member.summon) {
				continue;
			}
			auto        actor = member.handle.get();
			const auto* orders = Party::FindOrders(member.id);
			const bool  fighting = actor && orders && Game::Usable(actor.get()) && actor->IsInCombat();
			const auto  kit = orders ? PathsLink::KitOf(member.id, orders->role) : PathsLink::Kit{};
			for (std::size_t a = 1; a < Paths::kAbilityCount; ++a) {
				auto& clock = state.clocks[a];
				if (!clock.requested) {
					continue;
				}
				const auto ability = static_cast<Ability>(a);
				if (!kit.tree || !kit.Carries(ability)) {
					clock.requested = false;
					Party::Answer(member, std::format("{} is not one of the abilities they carry.", Paths::Info(ability).name));
				} else if (!fighting) {
					clock.requested = false;
					Party::Answer(member, "Only in a fight.");
				}
			}
			if (!fighting || !kit.tree) {
				continue;
			}
			Traits(member, actor.get(), kit, state);
			for (const auto ability : kit.slots) {
				if (!IsNew(ability)) {
					continue;
				}
				auto&      clock = state.clocks[static_cast<std::size_t>(ability)];
				const bool asked = clock.requested;
				auto       answer = [&](std::string a_text, bool a_done = false) {
                    if (asked) {
                        clock.requested = false;
                        Party::Answer(member, std::move(a_text), a_done);
                    }
				};
				if (!asked && (!settings.specials || !PathsLink::AutoOn(*orders, kit, ability))) {
					continue;
				}
				if (!settings.specials) {
					answer("Abilities are switched off (Battle plan settings).");
					continue;
				}
				if (clock.ready > 0.0f) {
					answer(std::format("Not yet: ready in {:.0f} s.", std::ceil(clock.ready)));
					continue;
				}
				if (actor->AsActorState()->IsBleedingOut()) {
					answer("Down themselves.");
					continue;
				}
				if (Stunned(actor.get())) {
					if (asked && Abilities::WaitedTooLong(member)) {
						answer("Knocked about; could not.");
					}
					continue;
				}
				const auto result = Run(member, actor.get(), kit, state, ability, asked);
				answer(result.text.empty() ? "Nothing to do right now."s : result.text, result.done);
				if (result.done) {
					Abilities::Used(member.id, actor.get());
					clock.cooldown = kit.Cooldown(ability);
					clock.ready = clock.cooldown;
				}
			}
		}
	}

	void Tick(float a_dt)
	{
		elapsed += a_dt;
		magnusLive = std::any_of(states.begin(), states.end(), [](const auto& a_entry) { return a_entry.second.magnus > 0.0f; });
		TakeEvents();
		// Blows landing.
		for (std::size_t i = 0; i < strikes.size();) {
			auto& strike = strikes[i];
			strike.in -= a_dt;
			if (strike.in > 0.0f) {
				++i;
				continue;
			}
			auto by = strike.by.get();
			auto land = std::move(strike.land);
			strikes.erase(strikes.begin() + static_cast<std::ptrdiff_t>(i));
			if (by && Game::Usable(by.get()) && land) {
				land(by.get());
			}
		}
		// Pins: staggered again whenever they find their feet.
		std::erase_if(pins, [&](Pin& a_pin) {
			a_pin.left -= a_dt;
			auto enemy = a_pin.enemy.get();
			if (a_pin.left <= 0.0f || !enemy || enemy->IsDead()) {
				return true;
			}
			if (!enemy->AsActorState()->IsStaggered()) {
				auto by = a_pin.by.get();
				Game::Stagger(enemy.get(), 0.6f, by.get());
			}
			return false;
		});
		// Bleeding.
		std::erase_if(bleeds, [&](Bleed& a_bleed) {
			a_bleed.left -= a_dt;
			auto enemy = a_bleed.enemy.get();
			if (a_bleed.left <= 0.0f || !enemy || enemy->IsDead()) {
				return true;
			}
			Hurt(enemy.get(), a_bleed.perSecond * a_dt);
			return false;
		});
		// Arrows falling.
		for (auto& rain : rains) {
			rain.in -= a_dt;
			while (rain.in <= 0.0f && rain.left > 0) {
				Fall(rain);
				rain.in += rain.every;
			}
		}
		std::erase_if(rains, [&](const Rain& a_rain) {
			if (a_rain.left > 0) {
				return false;
			}
			if (a_rain.pinning) {
				auto archer = a_rain.archer.get();
				for (const auto& enemy : EnemiesWithin(a_rain.centre, Paths::kRainRadius)) {
					Game::Stagger(enemy.get(), 0.6f, archer.get());
				}
			}
			return true;
		});
		// Timers, Renewal's pulses, and the last-ditch saves.
		for (auto& member : Party::Members()) {
			if (member.summon) {
				continue;
			}
			auto it = states.find(member.id);
			if (it == states.end()) {
				continue;
			}
			auto& state = it->second;
			state.berserk = std::max(state.berserk - a_dt, 0.0f);
			state.riposte = std::max(state.riposte - a_dt, 0.0f);
			state.deadeye = std::max(state.deadeye - a_dt, 0.0f);
			state.surge = std::max(state.surge - a_dt, 0.0f);
			state.magnus = std::max(state.magnus - a_dt, 0.0f);
			auto        actor = member.handle.get();
			const auto* orders = Party::FindOrders(member.id);
			if (!actor || !orders || !Game::Usable(actor.get()) || !actor->IsInCombat()) {
				state.renewal = 0.0f;
				continue;
			}
			if (state.magnus > 0.0f) {
				Brim(actor.get());
			}
			if (state.renewal > 0.0f) {
				state.renewal -= a_dt;
				state.renewalPulse -= a_dt;
				if (state.renewalPulse <= 0.0f) {
					state.renewalPulse += 1.0f;
					Pulse(member, actor.get(), state);
				}
			}
			const auto kit = PathsLink::KitOf(member.id, orders->role);
			if (kit.tree) {
				Brink(member, actor.get(), kit, state);
			}
		}
	}

	void Rest(float a_dt)
	{
		if (!PathsLink::Active()) {
			return;
		}
		for (auto& member : Party::Members()) {
			if (member.summon) {
				continue;
			}
			auto        actor = member.handle.get();
			const auto* orders = Party::FindOrders(member.id);
			if (!actor || !orders || orders->role != Role::kArcher || !Game::Usable(actor.get()) || actor->IsInCombat()) {
				continue;
			}
			auto& state = StateOf(member.id);
			state.fletchIn -= a_dt;
			if (state.fletchIn > 0.0f) {
				continue;
			}
			state.fletchIn = 20.0f;
			const auto kit = PathsLink::KitOf(member.id, orders->role);
			const int  keep = static_cast<int>(kit.Value(Perk::kFletcher));
			if (keep <= 0) {
				continue;
			}
			auto [bow, ammo] = BowAndArrows(actor.get());
			if (!bow) {
				continue;
			}
			if (!ammo) {
				ammo = Forms::Get().ironArrow;
			}
			if (!ammo) {
				continue;
			}
			int have = 0;
			for (const auto& [object, entry] : actor->GetInventory([&](RE::TESBoundObject& a_object) { return &a_object == ammo; })) {
				have += object ? entry.first : 0;
			}
			if (have >= keep) {
				continue;
			}
			const int make = std::min(5, keep - have);
			actor->AddObjectToContainer(ammo, nullptr, make, nullptr);
			Log::Info("[Paths] {} fletches {} {} ({} now, keeps {})."sv, member.name, make, ammo->GetName(), have + make, keep);
			if (const auto now = ::GetTickCount64(); have < 5 && now - state.fletchToldAt > 120000) {
				state.fletchToldAt = now;
				Notices::Push(std::format("{} fletches more {}.", member.name, ammo->GetName()), Notices::Kind::kInfo);
			}
		}
	}

	void Clear()
	{
		strikes.clear();
		lastStrengthened.clear();
		magnusLive = false;
		pins.clear();
		bleeds.clear();
		rains.clear();
		for (auto& [id, state] : states) {
			state.berserk = state.riposte = state.deadeye = state.surge = state.renewal = state.magnus = 0.0f;
			state.lastStandSpent = false;
			state.guardianSpent = false;
			if (auto was = state.unravelOn.get()) {
				Abilities::DropSource(Source(Perk::kUnravel), was->GetFormID());
			}
			state.unravelOn = {};
			for (auto& clock : state.clocks) {
				if (clock.requested) {
					clock.requested = false;
					if (auto* member = Party::Find(id)) {
						Party::Answer(*member, "The fight is over.");
					}
				}
			}
		}
	}

	void Reset()
	{
		Clear();
		states.clear();
		const std::scoped_lock guard(eventLock);
		blocked.clear();
		killers.clear();
	}

	void InstallEvents()
	{
		if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
			events->AddEventSink<RE::TESHitEvent>(&hitSink);
			events->AddEventSink<RE::TESDeathEvent>(&deathSink);
		}
	}

	void Show()
	{
		std::vector<RE::NiPointer<RE::Actor>>     alive;
		std::vector<std::pair<RE::Actor*, float>> on;
		auto add = [&](RE::NiPointer<RE::Actor> a_actor, float a_seconds) {
			if (a_actor && a_seconds > 0.0f) {
				on.emplace_back(a_actor.get(), a_seconds);
				alive.push_back(std::move(a_actor));
			}
		};
		auto show = [&](Effects::Look a_look) {
			Effects::Keep(a_look, on);
			on.clear();
		};
		auto each = [&](float State::* a_timer, Effects::Look a_look) {
			for (const auto& [id, state] : states) {
				add(RE::NiPointer<RE::Actor>(RE::TESForm::LookupByID<RE::Actor>(id)), state.*a_timer);
			}
			show(a_look);
		};
		if (!Settings::Get().abilityEffects) {
			for (const auto look : { Effects::Look::kBerserk, Effects::Look::kFocus, Effects::Look::kSurge, Effects::Look::kPinned, Effects::Look::kMagnus,
				Effects::Look::kRiposte, Effects::Look::kRenewal }) {
				show(look);
			}
			return;
		}
		each(&State::berserk, Effects::Look::kBerserk);
		each(&State::deadeye, Effects::Look::kFocus);
		each(&State::surge, Effects::Look::kSurge);
		each(&State::magnus, Effects::Look::kMagnus);
		each(&State::riposte, Effects::Look::kRiposte);
		each(&State::renewal, Effects::Look::kRenewal);
		for (const auto& pin : pins) {
			add(pin.enemy.get(), pin.left);
		}
		show(Effects::Look::kPinned);
	}

	StyleTweaks TweaksFor(RE::FormID a_member, const PathsLink::Kit& a_kit)
	{
		StyleTweaks tweaks;
		if (!a_kit.tree) {
			return tweaks;
		}
		const auto it = states.find(a_member);
		const auto* state = it == states.end() ? nullptr : &it->second;
		switch (a_kit.role) {
		case Role::kTank:
			if (const int rank = a_kit.Rank(Perk::kShieldMaster); rank > 0) {
				tweaks.bash = Paths::Value(Perk::kShieldMaster, rank);
				tweaks.defensive = 0.05f * static_cast<float>(rank);
			}
			break;
		case Role::kBruiser:
			if (const int rank = a_kit.Rank(Perk::kHeavyHands); rank > 0) {
				tweaks.power = Paths::Value(Perk::kHeavyHands, rank);
			}
			if (state && state->berserk > 0.0f) {
				tweaks.power *= 3.0f;
				tweaks.offensive += 0.3f;
				tweaks.fallback = 0.0f;
			}
			break;
		case Role::kFighter:
			if (const int rank = a_kit.Rank(Perk::kFlanker); rank > 0) {
				tweaks.flank = 1.0f + 0.5f * static_cast<float>(rank);
				tweaks.circle = 1.0f + 0.25f * static_cast<float>(rank);
			}
			if (state && state->riposte > 0.0f) {
				tweaks.defensive += 0.35f;
			}
			break;
		case Role::kArcher:
			if (const int rank = a_kit.Rank(Perk::kSkirmisher); rank > 0) {
				tweaks.fallback = Paths::Value(Perk::kSkirmisher, rank);
				tweaks.strafe = Paths::Value(Perk::kSkirmisher, rank);
			}
			if (state && state->deadeye > 0.0f) {
				tweaks.fallback *= 0.2f;
				tweaks.strafe *= 0.3f;
				tweaks.avoidThreat = 0.3f;
			}
			break;
		case Role::kMage:
			if (state && state->surge > 0.0f) {
				tweaks.offensive += 0.2f;
			}
			if (state && state->magnus > 0.0f) {
				tweaks.offensive += 0.3f;
			}
			break;
		default:
			break;
		}
		return tweaks;
	}

	void Statuses(RE::FormID a_member, std::vector<View::Status>& a_out)
	{
		using Tint = View::Status::Tint;
		const auto it = states.find(a_member);
		if (it != states.end()) {
			const auto& s = it->second;
			if (s.berserk > 0.0f) {
				a_out.push_back({ "Berserk", "More damage and a power attack at every chance", Paths::Info(Ability::kBerserk).glyph, Tint::kBuff, s.berserk, s.berserkLength });
			}
			if (s.riposte > 0.0f) {
				a_out.push_back({ "Riposte", "Every blocked blow staggers whoever struck it", Paths::Info(Ability::kRiposte).glyph, Tint::kArmor, s.riposte, s.riposteLength });
			}
			if (s.deadeye > 0.0f) {
				a_out.push_back({ "Deadeye", "Steady aim: more damage, holding their ground", Paths::Info(Ability::kDeadeye).glyph, Tint::kBuff, s.deadeye, s.deadeyeLength });
			}
			if (s.surge > 0.0f) {
				a_out.push_back({ "Arcane Surge", "Magicka flowing back many times as fast", Paths::Info(Ability::kSurge).glyph, Tint::kMagicka, s.surge, s.surgeLength });
			}
			if (s.magnus > 0.0f) {
				a_out.push_back({ "Magnus", "Magicka without end", Paths::Info(Ability::kMagnus).glyph, Tint::kMagicka, s.magnus, s.magnusLength });
			}
			if (s.renewal > 0.0f) {
				a_out.push_back({ "Renewal", "Healing everyone near a little every second", Paths::Info(Ability::kRenewal).glyph, Tint::kBuff, s.renewal, s.renewalLength });
			}
		}
		if (const auto stand = Abilities::BoostOn(a_member, RE::ActorValue::kDamageResist, Source(Perk::kStandFast))) {
			a_out.push_back({ "Stand Fast", std::format("+{:.0f} armor from a battle cry", stand->amount), Paths::Info(Ability::kStandFast).glyph, Tint::kArmor, stand->left,
				stand->length });
		}
		if (const auto last = Abilities::BoostOn(a_member, RE::ActorValue::kDamageResist, Source(Perk::kLastStand))) {
			a_out.push_back({ "Last Stand", std::format("+{:.0f} armor", last->amount), 0xF5D7, Tint::kArmor, last->left, last->length });
		}
	}
}
