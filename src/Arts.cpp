#include "FWM/Skald.h"
#include "FWM/PartyBalance.h"
#include "FWM/SecondWinds.h"
#include "FWM/AbilityAuto.h"
#include "FWM/AbilityPolicy.h"
#include "FWM/Arts.h"
#include "FWM/Ripostes.h"
#include "FWM/RiposteFx.h"
#include "FWM/ArrowArts.h"
#include "FWM/Windsteps.h"
#include "FWM/ArrowArtsFx.h"
#include "FWM/Berserks.h"
#include "FWM/Surges.h"
#include "FWM/Loadouts.h"
#include "FWM/DragonBreaths.h"
#include "FWM/DragonBreath.h"
#include "FWM/ArcherAmmo.h"
#include "FWM/EffectAudio.h"

#include <map>

#include "FWM/Abilities.h"
#include "FWM/ArrowRain.h"
#include "FWM/Blast.h"
#include "FWM/Blasts.h"
#include "FWM/Bastions.h"
#include "FWM/ChaosDown.h"
#include "FWM/ChaosPulses.h"
#include "FWM/Cloaks.h"
#include "FWM/Effects.h"
#include "FWM/Enemies.h"
#include "FWM/Forms.h"
#include "FWM/Freeze.h"
#include "FWM/Game.h"
#include "FWM/Haunts.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Orbs.h"
#include "FWM/Party.h"
#include "FWM/PathBonus.h"
#include "FWM/Quake.h"
#include "FWM/Quakes.h"
#include "FWM/Rainfall.h"
#include "FWM/Settings.h"
#include "FWM/Specials.h"
#include "FWM/Sunders.h"
#include "FWM/Text.h"
#include "FWM/TankActs.h"
#include "FWM/TimeBend.h"
#include "FWM/WolfBite.h"

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
            int riposteRank{1};
			float surge{ 0.0f }, surgeLength{ 0.0f };
			float renewal{ 0.0f }, renewalLength{ 0.0f }, renewalPulse{ 0.0f }, renewalShare{ 0.0f }, renewalTriage{ 0.0f };
			std::uint32_t cloak{ 0 };  // Renewal's cloak of water (Cloaks), while it shows
			float magnus{ 0.0f }, magnusLength{ 0.0f }, magnusPower{ 0.0f };
			bool  lastStandSpent{ false };
			bool  guardianSpent{ false };
			RE::ActorHandle unravelOn;
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
			float perSecond{ 0.0f };
			float left{ 0.0f };
			float spurtIn{ 0.0f };
            RE::ActorHandle by;
		};
		std::vector<Bleed> bleeds;

		constexpr float kRainChest = 70.0f;       // an arrow at someone under the rain is aimed this far up them
		constexpr float kRainAstray = 30.0f;      // and goes up to this far astray
		constexpr float kRainAimOver = 40.0f;     // one at nobody, this far over the ground
		constexpr float kRainGraze = 70.0f;       // an arrow landing this near someone grazes them
		constexpr float kRainTall = 140.0f;       // up to this far above their feet
		constexpr float kRainGrazeShare = 0.5f;   // for this share of the bow and arrow's blow

		// One of a rain's arrows in the air, followed until it lands so a near miss still grazes.
		struct Shaft : Game::Arrow
		{
			float flown{ 0.0f };
		};

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
			std::uint32_t      look{ 0 };  // Rainfall's, trailing its arrows
			std::string        name;       // the archer's
			std::vector<Shaft> shafts;
			std::size_t        turn{ 0 };  // whose turn under it to be aimed at
			int                struck{ 0 };
			int                grazed{ 0 };
            RE::NiPoint3 portal{},forward{},right{};
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

		Result Refused(std::string_view a_why) { return { false, std::string(a_why) }; }

		bool Stunned(RE::Actor* a_actor)
		{
			auto* state = a_actor->AsActorState();
			return state->IsStaggered() || state->IsBleedingOut() || a_actor->IsInRagdollState() || a_actor->IsInKillMove();
		}

		bool Standing(RE::Actor* a_actor)
		{
			return Game::Usable(a_actor) && !a_actor->AsActorState()->IsBleedingOut() && !a_actor->IsInRagdollState();
		}

		// For the player: a number and the noun, so a translation needs only its two forms.
		std::string CountSaid(int a_count)
		{
			return a_count == 0 ? std::string(Tr("no enemy")) : Trn(a_count, "{} enemy", "{} enemies");
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

		// The base damage of the weapon in hand: what the wolf's bite, Earthshaker and a riposte multiply.
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
				if (AbilityAuto::Visible(a_from, a_actor.get(), std::min(a_reach, AbilityPolicy::kSupportReach))) {
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

		void Hurt(RE::Actor* a_by, RE::Actor* a_enemy, float a_amount)
		{
			if (a_amount > 0.0f && a_enemy && !a_enemy->IsDead()) {
				PartyBalance::Damage(a_by, a_enemy, a_amount);
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

		Result ChaosDown(Party::Member& a_member, RE::Actor* a_tank, const PathsLink::Kit& a_kit, bool a_asked)
		{
			const float reach = FWM::ChaosDown::kReach + a_kit.Value(Perk::kChaosDownWide);
			const float seconds = FWM::ChaosDown::kSeconds + a_kit.Value(Perk::kChaosDownLingering);
			const int rank = std::max(a_kit.Rank(Ability::kChaosDown), 1);
			const float percent = Paths::Value(Perk::kChaosDown, rank);
			auto enemies = EnemiesWithin(a_tank->GetPosition(), reach);
			std::erase_if(enemies, [&](const auto& enemy) {
				return enemy.get() == a_tank || Party::IsPartyActor(enemy.get()) || !InSight(a_tank, enemy.get(), reach);
			});
			if (enemies.empty() && !PathsLink::DebugCasting(a_member.id)) return a_asked ? Refused(Tr("No enemies close enough for Chaos Down.")) : Result{};
			if (!a_asked && enemies.size() < 2) return {};

			int slowed = 0;
			for (const auto& enemy : enemies) {
				float lasting = 0.0f;
				for (const auto value : { RE::ActorValue::kSpeedMult, RE::ActorValue::kWeaponSpeedMult, RE::ActorValue::kLeftWeaponSpeedMultiply }) {
					const auto old = Abilities::BoostOn(enemy->GetFormID(), value, Source(Perk::kChaosDown));
					const auto combine = value == RE::ActorValue::kSpeedMult ? FWM::ChaosDown::Combine : FWM::ChaosDown::CombineAttack;
					const auto slow = combine(Game::Current(enemy.get(), value), old ? old->amount : 0.0f, old ? old->left : 0.0f, percent, seconds);
					if (slow.seconds <= 0.0f) continue;
					Abilities::Lift(enemy->GetFormID(), value, slow.amount, slow.seconds, Source(Perk::kChaosDown));
					lasting = std::max(lasting, slow.seconds);
				}
				if (lasting > 0.0f) {
					ChaosPulses::Bind(enemy.get(), lasting, rank >= 3);
					++slowed;
				}
			}
			if (slowed == 0 && !PathsLink::DebugCasting(a_member.id)) return a_asked ? Refused(Tr("No enemies can be slowed here.")) : Result{};
			Abilities::Challenge(a_tank);
			ChaosPulses::Call(a_tank, reach, rank >= 3, seconds);
			Show();  // The matching restraint aura follows the actual slow timers.
			Notices::Used(Trf("{} uses Chaos Down: {} slowed by {:.0f}% for {:.0f} s.", a_member.name, CountSaid(slowed), percent, seconds), Paths::Ability::kChaosDown, Notices::Kind::kOrder);
			Log::Info("[Paths] {} uses Chaos Down: {} enemies, -{:.0f}% movement and attack speed, {:.0f} s, {:.0f} reach."sv, a_member.name, slowed, percent, seconds, reach);
			return { true, Trf("Chaos Down: {} slowed by {:.0f}% for {:.0f} s.", CountSaid(slowed), percent, seconds) };
		}

		Result StandFast(Party::Member& a_member, RE::Actor* a_tank, const PathsLink::Kit& a_kit, bool a_asked)
		{
			if (!TankActs::LastBastion(a_tank, a_kit)) {
				return a_asked ? Refused(Tr("Needs a nearby living ally below 25% health in combat, and no other active Last Bastion.")) : Result{};
			}
			const auto status = TankActs::BastionOn(a_member.id);
			Abilities::Challenge(a_tank);
			Bastions::Call(a_tank, status.length, a_kit.Rank(Ability::kStandFast));
			Notices::Used(Trf("{}: \"Behind me!\" Absorbing {:.0f}% of nearby allies' damage.", a_member.name, status.percent), Ability::kStandFast, Notices::Kind::kOrder);
			Log::Info("[Tank] {} becomes the Last Bastion: {:.0f}% final health loss transferred for {:.0f} s."sv, a_member.name, status.percent, status.length);
			return { true, Trf("Last Bastion: absorbs {:.0f}% of nearby party damage for {:.0f} s.", status.percent, status.length) };
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
			if (scratched == 0 && !PathsLink::DebugCasting(a_member.id)) {
				return Refused(Tr("Nobody within reach needs it."));
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
				// A cloak of water round them while it lasts; the game's healing light if it cannot show.
				Cloaks::End(a_state.cloak);
				a_state.cloak = Cloaks::Wear(a_healer, a_state.renewalLength, a_kit.Rank(Ability::kRenewal) >= 3);
				if (!a_state.cloak) {
					Effects::Flash(a_healer, Effects::Look::kHealAura);
				}
			}
			Abilities::Play(Forms::Get().healSound, a_healer);
			Notices::Used(Trf("{} calls up Renewal: {:.0f}% health a second for {:.0f} s{}.", a_member.name, a_state.renewalShare * 100.0f, a_state.renewalLength,
							  lifted ? Trf("; {} back on their feet", lifted) : ""s),
				Paths::Ability::kRenewal, Notices::Kind::kHeal);
			Log::Info("[Paths] {} starts Renewal ({:.0f}%/s for {:.0f} s; {} hurt, {} lifted)."sv, a_member.name, a_state.renewalShare * 100.0f, a_state.renewalLength,
				hurt, lifted);
			return { true, Trf("Renewal: {:.0f}% a second for {:.0f} s.", a_state.renewalShare * 100.0f, a_state.renewalLength) };
		}

		void Pulse(Party::Member& a_member, RE::Actor* a_healer, State& a_state)
		{
			for (const auto& ally : Team(a_healer, Settings::Get().tuning.healReach)) {
				const bool  badly = Game::Fraction(ally.get(), RE::ActorValue::kHealth) < Paths::kTriageLine;
				const float give = Game::Maximum(ally.get(), RE::ActorValue::kHealth) * a_state.renewalShare * (badly ? 1.0f + a_state.renewalTriage : 1.0f) *
				                   PathBonus::Healing(a_member.id);
				ally->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kHealth, give);
				if (Settings::Get().abilityEffects && Game::Fraction(ally.get(), RE::ActorValue::kHealth) < 0.99f) {
					// The healer's water washing down whoever it heals, or the game's healing light without it.
					if (a_state.cloak) {
						if (ally.get() != a_healer) {
							Cloaks::Wash(a_state.cloak, ally.get());
						}
					} else {
						Effects::Flash(ally.get(), Effects::Look::kHealAura, 0.9f);
					}
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
				return a_asked ? Refused(Tr("Already berserk.")) : Result{};
			}
			const float damage = Paths::Value(Perk::kBerserk, std::max(a_kit.Rank(Ability::kBerserk), 1)) / 100.0f;
			a_state.berserk = a_state.berserkLength = Paths::kBerserkSeconds;
			Berserks::Call(a_bruiser,a_state.berserk,a_kit.Rank(Ability::kBerserk));
			a_state.bloodlust = a_kit.Value(Perk::kBerserkBloodlust) / 100.0f;
			Abilities::Lift(a_member.id, RE::ActorValue::kAttackDamageMult, damage, a_state.berserk, Source(Perk::kBerserk));
			if (const float armor = a_kit.Value(Perk::kBerserkThickSkull); armor > 0.0f) {
				Abilities::Lift(a_member.id, RE::ActorValue::kDamageResist, armor, a_state.berserk, Source(Perk::kBerserkThickSkull));
			}
			Abilities::Challenge(a_bruiser);
			Notices::Used(Trf("{} goes berserk: +{:.0f}% damage for {:.0f} s.", a_member.name, damage * 100.0f, a_state.berserk), Paths::Ability::kBerserk, Notices::Kind::kTaunt);
			Log::Info("[Paths] {} goes berserk (+{:.0f}% damage, {} close, boss near: {})."sv, a_member.name, damage * 100.0f, close.size(), bossNear);
			return { true, Trf("Berserk: +{:.0f}% damage.", damage * 100.0f) };
		}

		// Bloodlust: a kill while berserk heals and keeps the fury going.
		void Feed(Party::Member& a_member, RE::Actor* a_bruiser, State& a_state)
		{
			if (a_state.berserk <= 0.0f || a_state.bloodlust <= 0.0f) {
				return;
			}
			a_bruiser->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kHealth, Game::Maximum(a_bruiser, RE::ActorValue::kHealth) * a_state.bloodlust);
			a_state.berserk += 3.0f;
			Berserks::Extend(a_bruiser,a_state.berserk);
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
			if(PathsLink::DebugCasting(a_member.id)) {
				const int rank=a_kit.Rank(Ability::kSunder);
				PowerSwing(a_bruiser);
				if (!Sunders::Break(a_bruiser,a_bruiser,Paths::kSunderSeconds,rank>=3)) return Refused(Tr("Could not show the Sunder effect."));
				EffectAudio::Begin(Soundscape::Kind::Sunder,a_bruiser,a_bruiser->GetPosition(),1.8f,rank);
				return {true,std::string(Tr("Sunder effect activated."))};
			}
			auto target = Chosen(a_member, a_bruiser);
			if (!target || !Standing(target.get())) {
				return a_asked ? Refused(Tr("Nobody to sunder.")) : Result{};
			}
			const auto  name = Game::Name(target.get());
			const float d = Game::Distance(a_bruiser, target.get());
			if (d > 350.0f || !AbilityAuto::Visible(a_bruiser, target.get(), 350.0f)) {
				return a_asked ? Refused(Trf("{} is out of reach ({:.0f} away).", name, d)) : Result{};
			}
			if (!a_asked && !(Boss(target.get()) || target->GetLevel() >= a_bruiser->GetLevel())) {
				return {};
			}
			const int rank = std::max(a_kit.Rank(Ability::kSunder), 1);
			const float armor = Paths::Value(Perk::kSunder, rank);
			const float crack = a_kit.Value(Perk::kSunderCrack);
			const float shatter = a_kit.Value(Perk::kSunderShatter);
			const auto  enemy = target->GetHandle();
			PowerSwing(a_bruiser);
			Effects::Flash(a_bruiser, Effects::Look::kStrike);
			Later(a_bruiser, 0.35f, [=](RE::Actor* a_by) {
				auto victim = enemy.get();
				if (!AbilityAuto::Visible(a_by, victim.get(), 400.0f)) {
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
				EffectAudio::Begin(Soundscape::Kind::Sunder,a_by,victim->GetPosition(),1.8f,rank);
				if (!Sunders::Break(a_by, victim.get(), Paths::kSunderSeconds, rank >= 3)) {
					Effects::Flash(victim.get(), Effects::Look::kSunderImpact);
					Effects::Sparks(victim.get());
				}
			});
			Notices::Used(Trf("{} sunders {}: -{:.0f} armor for {:.0f} s.", a_member.name, name, armor, Paths::kSunderSeconds), Paths::Ability::kSunder, Notices::Kind::kTaunt);
			Log::Info("[Paths] {} sunders {} (-{:.0f} armor{}{})."sv, a_member.name, name, armor, shatter > 0.0f ? std::format(", -{:.0f}% resistances", shatter) : "",
				crack > 0.0f ? ", staggered" : "");
			return { true, Trf("Sundered {}: -{:.0f} armor.", name, armor) };
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
			if (close.empty() && !PathsLink::DebugCasting(a_member.id)) {
				return Refused(Tr("Nobody close enough to feel it."));
			}
			const float damage = WeaponDamage(a_bruiser) * times;
			PowerSwing(a_bruiser);
			Effects::Flash(a_bruiser, Effects::Look::kStrike);
			const auto name = a_member.name;
			// The slam: rocks burst out of the ground; everyone in reach falls where they stand, or at the
			// third rank the light enough are thrown up; then they are held down a while.
			Later(a_bruiser, 0.4f, [=](RE::Actor* a_by) {
				const auto origin = a_by->GetPosition();
				Abilities::Play(Forms::Get().quakeSound, a_by);
				Effects::Flash(a_by, Effects::Look::kEarthshaker);
				Quakes::Slam(a_by, origin, reach, rank >= 3);
				int felled = 0, thrown = 0;
				for (const auto& enemy : EnemiesWithin(origin, reach)) {
					Hurt(a_by, enemy.get(), damage);
					if (!enemy->IsDead()) {
						(Quakes::Down(enemy.get(), origin, rank, Boss(enemy.get())) ? thrown : felled)++;
						Effects::Flash(enemy.get(), Effects::Look::kShaken);
					}
				}
				Log::Info("[Paths] {}'s Earthshaker (rank {}): {} felled, {} thrown, {:.0f} damage each, held down {:.1f} s."sv, name, rank, felled, thrown, damage,
					Quake::Downed(rank, false));
			});
			if (aftershock) {
				Later(a_bruiser, 1.9f, [=](RE::Actor* a_by) {
					Effects::Flash(a_by, Effects::Look::kEarthshaker, 1.2f);
					Quakes::Slam(a_by, a_by->GetPosition(), reach * 0.6f, false);
					for (const auto& enemy : EnemiesWithin(a_by->GetPosition(), reach)) {
						Game::Stagger(enemy.get(), 0.5f, a_by);
					}
					Abilities::Play(Forms::Get().quakeSound, a_by);
				});
			}
			Notices::Used(Trf("{} shakes the earth: {} within {:.0f} go down.", a_member.name, CountSaid(static_cast<int>(close.size())), reach),
				Paths::Ability::kEarthshaker, Notices::Kind::kTaunt);
			return { true, Trf("Earthshaker: {} in reach.", CountSaid(static_cast<int>(close.size()))) };
		}

		// --- Fighter --------------------------------------------------------------

		Result SecondWind(Party::Member& a_member, RE::Actor* a_fighter, const PathsLink::Kit& a_kit, bool a_asked)
		{
			const float health = Game::Fraction(a_fighter, RE::ActorValue::kHealth);
			const float stamina = Game::Fraction(a_fighter, RE::ActorValue::kStamina);
			if (!a_asked && health >= 0.35f) {
				return {};
			}
			if (health > 0.95f && stamina > 0.95f && !PathsLink::DebugCasting(a_member.id)) {
				return Refused(Tr("Not winded yet."));
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
			SecondWinds::Call(a_fighter, rank);
			Notices::Used(Trf("{} gets a second wind.", a_member.name), Paths::Ability::kSecondWind, Notices::Kind::kHeal);
			Log::Info("[Paths] {} gets a second wind at {:.0f}% health: +{:.0f}% health, +{:.0f}% stamina."sv, a_member.name, health * 100.0f, heal * 100.0f, breath * 100.0f);
			return { true, Trf("Second wind: {:.0f}% health back.", heal * 100.0f) };
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
			if (!a_asked && (!(on >= 2 || bossOn) || Ripostes::Benched(a_member.id))) {
				return {};
			}
			if (!Ripostes::CanGuard(a_fighter)) {
				return a_asked ? Refused(Tr("Needs a weapon or shield to block with, and no spell or staff in the off hand.")) : Result{};
			}
			a_state.riposteRank = std::max(a_kit.Rank(Ability::kRiposte), 1);
            const float seconds=Paths::Value(Perk::kRiposte,a_state.riposteRank);
            if(!Ripostes::Guard(a_fighter,a_state.riposteRank,seconds)) return {false, std::string(Tr("Cannot raise a guard right now."))};
            a_state.riposte = a_state.riposteLength = seconds;
			a_state.counter = a_kit.Value(Perk::kRiposteCounter);
			a_state.riposteDisarm = a_kit.Rank(Perk::kRiposteDisarming) > 0;
			// The held weapon shimmer signals the raised guard; contact gets the flash. Ripostes
			// announces it once the guard is actually up.
			Log::Info("[Paths] {} readies a riposte for {:.0f} s ({} on them)."sv, a_member.name, a_state.riposte, on);
			return { true, Trf("Riposte: guard the next melee blow, {:.0f} s.", a_state.riposte) };
		}

		// A blow blocked while Riposte is up.
		void Retort(Party::Member& a_member, RE::Actor* a_fighter, State& a_state, RE::Actor* a_attacker)
		{
			if (!Enemies::Find(a_attacker->GetFormID()) || Game::Distance(a_fighter, a_attacker) > 450.0f || a_attacker->IsDead()) {
				return;
			}
			if(!Ripostes::Counter(a_fighter,a_attacker,a_state.riposteRank,WeaponDamage(a_fighter),a_state.counter,Boss(a_attacker))) return;
            a_state.riposte=0; // consume once before any queued duplicate hit is processed
			bool disarmed = false;
			if (a_state.riposteDisarm && a_attacker->GetLevel() <= a_fighter->GetLevel() + 10 && Specials::TakeWeapon(a_fighter, a_attacker)) {
				a_state.riposteDisarm = false;
				disarmed = true;
			}
			Log::Info("[Paths] {} ripostes {}{}{}."sv, a_member.name, Game::Name(a_attacker), ", countered and pushed back", disarmed ? " and loses their weapon" : "");
		}

		// Kynareth's Maw, a giant spectral wolf's head called down on an enemy the fighter stands before: our own mesh,
		// the vanilla wolf's head with fur, glowing like a spirit (Effects::Head), posed here frame by frame. It takes shape right in
		// front of the enemy with its jaws open round them, rears and trembles, clamps down so its
		// front half bites into them, shakes its catch, and fades. The bite lands as the jaws close and
		// catches what the head itself touches: a capsule from the skull to the muzzle, a skull wide.
		// Nothing of it is an actor, and nothing of it is saved.
		// The timeline and the head's measurements live in WolfBite.h, shared with the preview tool.
		using WolfBite::kHitStop;
		using WolfBite::kHitStopSpeed;
		using WolfBite::kSnap;
		using WolfBite::kTense;
		constexpr float kWolfStagger = 0.7f;  // how hard the bite staggers them: a clear recoil to be caught in

		constexpr RE::NiPoint3 Ni(WolfBite::Vec a_v) { return { a_v.x, a_v.y, a_v.z }; }
		constexpr RE::NiPoint3 kHeadBack = Ni(WolfBite::kHeadBack);
		constexpr RE::NiPoint3 kHeadFront = Ni(WolfBite::kHeadFront);
		constexpr RE::NiPoint3 kTeeth = Ni(WolfBite::kTeeth);
		constexpr float        kHeadRadius = WolfBite::kHeadRadius;

		RE::NiMatrix3 Ni(const WolfBite::Mat& a_m)
		{
			RE::NiMatrix3 out;
			for (int i = 0; i < 3; ++i) {
				for (int j = 0; j < 3; ++j) {
					out.entry[i][j] = a_m[i][j];
				}
			}
			return out;
		}

		struct Spirit
		{
			RE::NiPointer<RE::BSTempEffectParticle> effect;
			RE::NiPointer<RE::NiAVObject>           head;  // its nodes, once the mesh has loaded
			RE::NiPointer<RE::NiAVObject>           jaw;
			std::vector<RE::BSShaderProperty*>      skins;  // every shader on it, faded in and out together
			RE::ActorHandle                         by;
			RE::ActorHandle                         target;
			std::string                             name;  // the fighter's
			RE::NiPoint3                            at;    // the target's feet, followed until the lunge
			float                                   heading{ 0.0f };
			float                                   chest{ 0.0f };  // how high above them the jaws close
			float                                   damage{ 0.0f };
			float                                   share{ 0.0f };    // of a bitten one's health, bled away
			float                                   seconds{ 0.0f };  // how long the wounds bleed
			float                                   hold{ 0.0f };     // how long whatever it catches stands frozen
			bool                                    compact{ true };
            bool                                    grand{ false };   // the third rank's red wolf, holding on longer
			float                                   age{ 0.0f };
			bool                                    snarled{ false };
			bool                                    swung{ false };
			bool                                    bitten{ false };
			float                                   spurtIn{ 0.0f };
		};
		std::vector<Spirit> spirits;

		// A heavy blow's catch: the world all but stops for a blink. Only from normal speed, and put
		// back only if nothing else changed it meanwhile (the battle plan, a shout, a killcam).
		struct
		{
			std::chrono::steady_clock::time_point until;
			bool                                  on{ false };
		} hitStop;

		void StopTime()
		{
			auto* timer = RE::BSTimer::GetSingleton();
			if (!timer || hitStop.on || TimeBend::Active() || std::abs(RE::BSTimer::QGlobalTimeMultiplier() - 1.0f) > 0.01f) {
				return;
			}
			timer->SetGlobalTimeMultiplier(kHitStopSpeed, true);
			hitStop.until = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(kHitStop * 1000.0f));
			hitStop.on = true;
		}

		void StartTime(bool a_now)
		{
			if (!hitStop.on || (!a_now && std::chrono::steady_clock::now() < hitStop.until)) {
				return;
			}
			hitStop.on = false;
			auto* timer = RE::BSTimer::GetSingleton();
			if (timer && std::abs(RE::BSTimer::QGlobalTimeMultiplier() - kHitStopSpeed) < 0.01f) {
				timer->SetGlobalTimeMultiplier(1.0f, true);
			}
		}

		// Shortest distance between the segments p0-p1 and q0-q1.
		float SegmentGap(const RE::NiPoint3& a_p0, const RE::NiPoint3& a_p1, const RE::NiPoint3& a_q0, const RE::NiPoint3& a_q1)
		{
			constexpr float kTiny = 1e-4f;
			const auto      d1 = a_p1 - a_p0;
			const auto      d2 = a_q1 - a_q0;
			const auto      r = a_p0 - a_q0;
			const float     a = d1.Dot(d1), e = d2.Dot(d2), f = d2.Dot(r);
			float           s = 0.0f, t = 0.0f;
			if (a <= kTiny && e <= kTiny) {
				return r.Length();
			}
			if (a <= kTiny) {
				t = std::clamp(f / e, 0.0f, 1.0f);
			} else {
				const float c = d1.Dot(r);
				if (e <= kTiny) {
					s = std::clamp(-c / a, 0.0f, 1.0f);
				} else {
					const float b = d1.Dot(d2);
					const float denom = a * e - b * b;
					s = denom > kTiny ? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
					t = (b * s + f) / e;
					if (t < 0.0f) {
						t = 0.0f;
						s = std::clamp(-c / a, 0.0f, 1.0f);
					} else if (t > 1.0f) {
						t = 1.0f;
						s = std::clamp((b - c) / a, 0.0f, 1.0f);
					}
				}
			}
			return ((a_p0 + d1 * s) - (a_q0 + d2 * t)).Length();
		}

		// Half an enemy's breadth, from its bounds: a man's is about 20, a troll's or a dragon's far more.
		float Breadth(RE::Actor* a_actor)
		{
			const auto low = a_actor->GetBoundMin();
			const auto high = a_actor->GetBoundMax();
			return std::clamp(0.25f * ((high.x - low.x) + (high.y - low.y)), 15.0f, 200.0f);
		}

		// Where the head is and how its jaw stands at this moment of the bite, in the world.
		struct Pose
		{
			RE::NiTransform head;
			float           jaw{ 0.0f };
			float           alpha{ 1.0f };
		};

		Pose PoseOf(const Spirit& a_spirit, bool display = false)
		{
			auto bite = WolfBite::At(a_spirit.age, a_spirit.heading, { a_spirit.at.x, a_spirit.at.y, a_spirit.at.z }, a_spirit.chest, a_spirit.grand);
			if (display) bite = WolfBite::Display(bite,a_spirit.at.z,a_spirit.grand,a_spirit.compact);
			Pose       pose;
			pose.head.rotate = Ni(bite.rotate);
			pose.head.translate = { bite.translate.x, bite.translate.y, bite.translate.z };
			pose.head.scale = bite.scale;
			pose.jaw = bite.jaw;
			pose.alpha = bite.alpha;
			return pose;
		}

		// Our pose onto the mesh, through whatever transform the effect gave its root.
		void Place(Spirit& a_spirit, const Pose& a_pose)
		{
			auto* root = a_spirit.effect ? a_spirit.effect->particleObject.get() : nullptr;
			if (!root || !root->parent) {
				return;
			}
			if (!a_spirit.head) {
				a_spirit.head.reset(root->GetObjectByName("Head"));
				a_spirit.jaw.reset(root->GetObjectByName("Jaw"));
				if (a_spirit.head) {
					RE::BSVisit::TraverseScenegraphGeometries(a_spirit.head.get(), [&](RE::BSGeometry* a_geometry) {
						auto* shader = a_geometry->GetGeometryRuntimeData().shaderProperty.get();
						if (shader && std::find(a_spirit.skins.begin(), a_spirit.skins.end(), shader) == a_spirit.skins.end()) {
							a_spirit.skins.push_back(shader);
						}
						return RE::BSVisit::BSVisitControl::kContinue;
					});
				}
				if (!a_spirit.head) {
					return;
				}
				Log::Info("[Paths] {}'s spectral wolf head shows {:.2f} s in{}."sv, a_spirit.name, a_spirit.age, a_spirit.jaw ? "" : ", but without its jaw node");
			}
			const auto& parent = root->world;
			const auto  inverse = parent.rotate.Transpose();
			auto&       local = a_spirit.head->local;
			local.rotate = inverse * a_pose.head.rotate;
			local.translate = inverse * (a_pose.head.translate - parent.translate) * (1.0f / parent.scale);
			local.scale = a_pose.head.scale / parent.scale;
			if (a_spirit.jaw) {
				a_spirit.jaw->local.rotate = Ni(WolfBite::JawTurn(a_pose.jaw));
			}
			// Under the alpha test the fur grows in from the skin outward as this rises, and recedes as
			// it falls; the glow fades with it.
			for (auto* shader : a_spirit.skins) {
				shader->alpha = a_pose.alpha;
				if (auto* lit = netimmerse_cast<RE::BSLightingShaderProperty*>(shader); lit && lit->material) {
					static_cast<RE::BSLightingShaderMaterialBase*>(lit->material)->materialAlpha = a_pose.alpha;
				} else if (auto* glow = netimmerse_cast<RE::BSEffectShaderProperty*>(shader); glow && glow->material) {
					static_cast<RE::BSEffectShaderMaterial*>(glow->material)->baseColor.alpha = a_pose.alpha;
				}
			}
			RE::NiUpdateData update{ 0.0f, RE::NiUpdateData::Flag::kNone };
			a_spirit.head->Update(update);
		}

		void PlayAt(RE::BGSSoundDescriptorForm* a_sound, const RE::NiPoint3& a_at)
		{
			RE::BSSoundHandle sound;
			if (auto* audio = RE::BSAudioManager::GetSingleton(); audio && a_sound && audio->GetSoundHandle(sound, a_sound)) {
				sound.SetPosition(a_at);
				sound.Play();
			}
		}

		// The fighter stands before the enemy: near, and inside the arc it faces.
		bool Before(RE::Actor* a_fighter, RE::Actor* a_enemy)
		{
			const auto  to = a_fighter->GetPosition() - a_enemy->GetPosition();
			const float d = std::hypot(to.x, to.y);
			if (d > Paths::kWolfReach) {
				return false;
			}
			const float heading = a_enemy->GetAngleZ();
			return d < 1.0f || (to.x * std::sin(heading) + to.y * std::cos(heading)) / d >= std::cos(Paths::kWolfArc * RE::NI_PI / 180.0f);
		}

		RE::NiPointer<RE::Actor> NearestBefore(RE::Actor* a_fighter)
		{
			RE::NiPointer<RE::Actor> nearest;
			for (const auto& enemy : EnemiesWithin(a_fighter->GetPosition(), Paths::kWolfReach)) {
				if (Before(a_fighter, enemy.get()) && InSight(a_fighter, enemy.get(), Paths::kWolfReach) &&
					(!nearest || Game::Distance(a_fighter, enemy.get()) < Game::Distance(a_fighter, nearest.get()))) {
					nearest = enemy;
				}
			}
			return nearest;
		}

		Result SpectralWolf(Party::Member& a_member, RE::Actor* a_fighter, const PathsLink::Kit& a_kit, bool a_asked)
		{
			const bool debug=PathsLink::DebugCasting(a_member.id);
			auto target = debug ? RE::NiPointer<RE::Actor>{} : Chosen(a_member, a_fighter);
			if (target && !(Standing(target.get()) && InSight(a_fighter, target.get(), a_asked ? Paths::kWolfReach : 600.0f))) {
				target = nullptr;
			}
			if (target && !Before(a_fighter, target.get())) {
				if (!a_asked) {
					return {};
				}
				// Asked: someone else they do stand before will do.
				const auto chosen = target;
				target = NearestBefore(a_fighter);
				if (!target) {
					return Refused(Trf("They must stand before {}, not beside or behind it.", Game::Name(chosen.get())));
				}
			}
			if (!target && a_asked && !debug) {
				target = NearestBefore(a_fighter);
			}
			if (!target && !debug) {
				return a_asked ? Refused(Tr("Nobody they stand before.")) : Result{};
			}
			if (!a_asked) {
				const bool strong = Boss(target.get()) || target->GetLevel() >= a_fighter->GetLevel();
				const bool bunched = EnemiesWithin(target->GetPosition(), 180.0f).size() >= 2;
				if (!strong && !bunched) {
					return {};
				}
			}
			if (std::any_of(spirits.begin(), spirits.end(), [&](const Spirit& a_spirit) { return a_spirit.by == a_fighter->GetHandle(); })) {
				return a_asked ? Refused(Tr("Their wolf is still out.")) : Result{};
			}

			// It faces the enemy from the fighter's side, which is the enemy's front.
			const auto at=debug?PathsLink::DebugAim(a_fighter):target->GetPosition();
			const auto to=at-a_fighter->GetPosition();
			const int   rank = std::max(a_kit.Rank(Ability::kSpectralWolf), 1);
			Spirit      spirit;
			spirit.by = a_fighter->GetHandle();
			spirit.target = target?target->GetHandle():RE::ActorHandle{};
			spirit.name = a_member.name;
			spirit.at = at;
			spirit.heading = std::hypot(to.x, to.y) > 1.0f ? std::atan2(to.x, to.y) : a_fighter->GetAngleZ();
			spirit.chest = (target?target->GetHeight():a_fighter->GetHeight()) * 0.55f;
			// Snapshot both the bite and max-health bleed once; the wound ticks use these values.
			spirit.damage = WeaponDamage(a_fighter) * Paths::Value(Perk::kSpectralWolf, rank);
			spirit.share = Paths::Second(Perk::kSpectralWolf, rank) / 100.0f;
			spirit.seconds = Paths::kWolfBleedSeconds + a_kit.Value(Perk::kWolfHemorrhage);
			spirit.hold = Paths::kWolfHold[static_cast<std::size_t>(rank - 1)] + (a_kit.Rank(Perk::kWolfMaul) > 0 ? Paths::kWolfMaulHold : 0.0f);
			spirit.grand = rank >= 3;
            spirit.compact=Settings::Get().compactEffects;
			spirit.effect.reset(Effects::Head(target?target.get():a_fighter, spirit.at, spirit.heading, WolfBite::Gone(spirit.grand) + 0.1f, spirit.grand));
			if (!spirit.effect) {
				Log::Warn("[Paths] Kynareth's Maw from {} has no head to show (is meshes\\FightWithMe\\SpectralWolfHead.nif installed?); it bites unseen."sv, a_member.name);
			}
			EffectAudio::Begin(Soundscape::Kind::Wolf,a_fighter,spirit.at,WolfBite::Gone(spirit.grand),rank);
			spirits.push_back(std::move(spirit));

			Effects::Flash(a_fighter, Effects::Look::kSpiritWolf);
			const auto name = debug?std::string(Tr("10 m ahead")):Game::Name(target.get());
			Notices::Used(Trf("{} calls down Kynareth's Maw on {}!", a_member.name, name), Paths::Ability::kSpectralWolf, Notices::Kind::kTaunt);
			Log::Info("[Paths] {} calls down Kynareth's Maw{} on {}, {:.0f} away ({:.0f} damage, {:.0f}% bled over {:.0f} s, held {:.1f} s)."sv, a_member.name,
				spirits.back().grand ? " (red)" : "", name, to.Length(), spirits.back().damage, spirits.back().share * 100.0f, spirits.back().seconds,
				spirits.back().hold);
			return { true, Trf("Kynareth's Maw on {}.", name) };
		}

		// The jaws close: everything the head touches is bitten.
		void Bite(Spirit& a_spirit, const Pose& a_pose)
		{
			const auto  back = a_pose.head * kHeadBack;
			const auto  front = a_pose.head * kHeadFront;
			const float radius = kHeadRadius * a_pose.head.scale;
			const auto  teeth = a_pose.head * kTeeth;
			const auto  by = a_spirit.by.get();
			std::string   caught;
			int           hit = 0;
			std::uint32_t haunt = 0;  // the ghostly smoke on those it holds, once the first is caught
			for (const auto& enemy : EnemiesWithin(teeth, 800.0f)) {
				const float        breadth = Breadth(enemy.get());
				const auto         feet = enemy->GetPosition();
				const RE::NiPoint3 low{ feet.x, feet.y, feet.z + breadth };
				auto               top = Game::HeadPosition(enemy.get());
				top.z = std::max(top.z, low.z);
				const float gap = SegmentGap(back, front, low, top);
				if (gap > radius + breadth) {
					continue;
				}
				++hit;
				Hurt(by.get(), enemy.get(), a_spirit.damage);
				if (!enemy->IsDead()) {
					// Caught still mid-stagger, as though struck stiff by the blow, in a ghostly smoke. Not in the
					// air: a flyer held still would hang there.
					Game::Stagger(enemy.get(), kWolfStagger, by.get());
					const float hold = a_spirit.hold * (Boss(enemy.get()) ? Paths::kWolfBossHold : 1.0f);
					if (!enemy->AsActorState()->IsFlying()) {
						Freeze::Hold(enemy.get(), Paths::kWolfCatch, hold);
					}
					if (!haunt && Settings::Get().abilityEffects) {
						haunt = Haunts::Begin(enemy.get(), a_spirit.grand);
					}
					if (haunt) {
						Haunts::Hold(haunt, enemy.get(), Paths::kWolfCatch + hold);
					} else {
						Effects::Flash(enemy.get(), Effects::Look::kShaken, 0.8f);
					}
					const float share = a_spirit.share * (Boss(enemy.get()) ? Paths::kWolfBossBleed : 1.0f);
					const float total = Game::Maximum(enemy.get(), RE::ActorValue::kHealth) * share;
					bleeds.push_back({ enemy->GetHandle(), total / a_spirit.seconds, a_spirit.seconds, 0.6f, by->GetHandle() });
				}
				auto at = teeth;
				at.z = std::clamp(at.z, low.z, top.z);
				Effects::Spray(enemy.get(), at, a_spirit.heading, Effects::Blood::kBite, 2.4f);
				Effects::Spray(enemy.get(), at, a_spirit.heading + RE::NI_PI, Effects::Blood::kGore, 1.8f);
				caught += std::format("{}{} ({:.0f} off)", caught.empty() ? "" : ", ", Game::Name(enemy.get()), gap);
			}
			// The weight of it: the crunch of the jaws, a giant's blow on the ground under it, the
			// camera jolted, and the world caught still for a blink.
			PlayAt(Forms::Get().blowSound, teeth);
			PlayAt(Forms::Get().quakeSound, teeth);
			RE::ShakeCamera(0.6f, teeth, 0.6f);
			StopTime();
			auto* model = a_spirit.effect ? a_spirit.effect->particleObject.get() : nullptr;
			const std::string_view seen = a_spirit.head ? ""sv :
			                              !a_spirit.effect ? " unseen (no head was made)"sv :
			                              !model ? " unseen (its mesh had not loaded)"sv :
			                              !model->parent ? " unseen (its mesh was never placed in the world)"sv :
			                                               " unseen (no Head node in its mesh)"sv;
			Log::Info("[Paths] Kynareth's Maw from {} bites{}: a head {:.0f} across catches {}{}."sv, a_spirit.name, seen, radius * 2.0f, CountOf(hit),
				caught.empty() ? "" : ": " + caught);
		}

		void Spirits(float a_dt)
		{
			StartTime(false);
			for (auto& spirit : spirits) {
				spirit.age += a_dt;
				// It keeps in front of its quarry until the jaws start to close.
				if (spirit.age < kTense) {
					auto target = spirit.target.get();
					auto by = spirit.by.get();
					if (target && !target->IsDead()) {
						spirit.at = target->GetPosition();
						if (by) {
							const auto to = target->GetPosition() - by->GetPosition();
							if (std::hypot(to.x, to.y) > 1.0f) {
								spirit.heading = std::atan2(to.x, to.y);
							}
						}
					}
				}
				const auto pose = PoseOf(spirit);
				const auto teeth = pose.head * kTeeth;
				if (!spirit.snarled && spirit.age >= WolfBite::kSnarl) {
					spirit.snarled = true;
					// The layered voice is timed by the shared Wolf sound plan.
				}
				// The fighter's own swing lands with the bite.
				if (!spirit.swung && spirit.age >= WolfBite::kSwing) {
					spirit.swung = true;
					auto by = spirit.by.get();
					auto target = spirit.target.get();
					if (by && target && Game::Usable(by.get()) && Game::Distance(by.get(), target.get()) < 350.0f) {
						PowerSwing(by.get());
					}
				}
				if (!spirit.bitten && spirit.age >= kSnap) {
					spirit.bitten = true;
					Bite(spirit, pose);
				}
				// Blood from the jaws while it shakes them.
				if (spirit.bitten && spirit.age < WolfBite::ShakeEnd(spirit.grand)) {
					spirit.spurtIn -= a_dt;
					if (spirit.spurtIn <= 0.0f) {
						spirit.spurtIn = 0.22f;
						if (auto target = spirit.target.get()) {
							Effects::Spray(target.get(), teeth, spirit.heading + std::uniform_real_distribution<float>(-1.2f, 1.2f)(rng), Effects::Blood::kBleed, 1.4f);
						}
					}
				}
				Place(spirit, PoseOf(spirit, true));
			}
			std::erase_if(spirits, [](const Spirit& a_spirit) { return a_spirit.age >= WolfBite::Gone(a_spirit.grand); });
		}

		// --- Archer ---------------------------------------------------------------

		// The bow in hand (or the best carried) and the arrows they would shoot from it.
        std::pair<RE::TESObjectWEAP*,RE::TESAmmo*> BowAndArrows(RE::Actor* archer) {return Loadouts::Ranged(archer);}

        Result ExplosiveArrow(Party::Member& member,RE::Actor* actor,const PathsLink::Kit& kit,bool asked)
        {
            const bool debug=PathsLink::DebugCasting(member.id);auto target=Chosen(member,actor);
            if(!debug&&(!target||!InSight(actor,target.get(),AbilityPolicy::kEnemyReach)))return asked?Refused(Tr("No enemy in sight.")):Result{};
            const int rank=std::max(1,kit.Rank(Ability::kDeadeye));
            const auto aim=debug?PathsLink::DebugAim(actor)+RE::NiPoint3{0,0,70}:target->GetPosition();
            if(!ArrowArts::Explosive(actor,target.get(),aim,rank,kit.Value(Perk::kDeadeyeHawkeye)*70.f,kit.Value(Perk::kDeadeyePatience),debug))return Refused(Tr("Needs a bow and arrows, or a shot is already preparing."));
            Notices::Used(Trf("{} prepares an explosive arrow.",member.name),Ability::kDeadeye,Notices::Kind::kOrder);
            return {true,std::string(Tr("Explosive arrow readied."))};
        }

        Result Windstep(Party::Member& member, RE::Actor* actor, const PathsLink::Kit& kit, bool asked)
        {
            if (!Windsteps::Call(actor, std::max(1, kit.Rank(Ability::kPinningShot)), kit.Value(Perk::kPinningHeavy), kit.Rank(Perk::kPinningTwin)>0, PathsLink::DebugCasting(member.id)))
                return asked ? Refused(Tr("No safe space to retreat.")) : Result{};
            Notices::Used(Trf("{} slips away on the wind.", member.name), Ability::kPinningShot, Notices::Kind::kOrder);
            return {true, std::string(Tr("Windstep."))};
        }

		Result ArrowRain(Party::Member& a_member, RE::Actor* a_archer, const PathsLink::Kit& a_kit, bool a_asked)
		{
			const bool debug=PathsLink::DebugCasting(a_member.id);
			RE::NiPointer<RE::Actor> centre;
			if (a_asked && !debug) {
				centre = Chosen(a_member, a_archer);
			}
			if (!centre && !debug) {
				// Where they stand thickest.
				std::size_t most = 0;
				for (const auto& profile : Enemies::All()) {
					auto enemy = profile.handle.get();
					if (!enemy || !Standing(enemy.get()) || !InSight(a_archer, enemy.get(), AbilityPolicy::kEnemyReach)) {
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
			if (!centre && !debug) {
				return a_asked ? Refused(Tr("Nobody to rain on.")) : Result{};
			}
			const auto [bow, ammo] = debug ? Specials::DebugRanged(a_archer) : BowAndArrows(a_archer);
			if (!bow || !ammo) {
				return Refused(Tr("Needs a bow and arrows to copy."));
			}
			const int rank = std::max(a_kit.Rank(Ability::kArrowRain), 1);
			const int count = static_cast<int>(Paths::Value(Perk::kArrowRain, rank) + a_kit.Value(Perk::kArrowRainDownpour));
			// A vertical portal opens directly in front of the archer.
			const auto at=debug?PathsLink::DebugAim(a_archer):centre->GetPosition();
			auto forward=at-a_archer->GetPosition();forward.z=0;
            if(forward.Length()<1)forward={std::sin(a_archer->GetAngleZ()),std::cos(a_archer->GetAngleZ()),0};
            forward=forward/std::max(forward.Length(),1.f);
            const auto portal=a_archer->GetPosition()+forward*ArrowRain::kPortalForward+RE::NiPoint3{0,0,ArrowRain::kPortalHeight};
            a_archer->SetHeading(std::atan2(forward.x,forward.y));
            const auto look = Rainfall::Call(a_archer, centre.get(), at, Paths::kRainSeconds, rank >= 3);
			rains.push_back({ a_archer->GetHandle(), centre?centre->GetHandle():RE::ActorHandle{}, at, ammo, bow, count, Paths::kRainSeconds / static_cast<float>(count), ArrowRain::kCall,
				a_kit.Rank(Perk::kArrowRainPinning) > 0, look, a_member.name });
            rains.back().portal=portal;rains.back().forward=forward;rains.back().right=forward.Cross({0,0,1});
			const auto name = debug?std::string(Tr("10 m ahead")):Game::Name(centre.get());
			Notices::Used(Trf("{} fires {} arrows through a portal at {}.", a_member.name, count, name), Paths::Ability::kArrowRain, Notices::Kind::kOrder);
			Log::Info("[Paths] {} rains {} arrows on {}."sv, a_member.name, count, name);
			return { true, Trf("Arrow rain: {} arrows on {}.", count, name) };
		}

		void Fall(Rain& a_rain)
		{
			auto archer = a_rain.archer.get();
			if (!archer || archer->IsDead() || !archer->Is3DLoaded()) {
				a_rain.left = 0;
				return;
			}
			if (auto over = a_rain.over.get(); over && !over->IsDead()) {
				a_rain.centre = over->GetPosition();
			}
			std::uniform_real_distribution<float> unit(0.0f, 1.0f);
			const float angle = unit(rng) * 6.2831853f;
			// Each arrow at one of those under the rain in turn, a little astray, so every one tells; at
			// the ground somewhere inside it when nobody is there.
			RE::NiPoint3 ground;
			float        over = kRainAimOver;
			if (const auto under = EnemiesWithin(a_rain.centre, Paths::kRainRadius); !under.empty()) {
				const auto& enemy = under[a_rain.turn++ % under.size()];
				const float astray = unit(rng) * kRainAstray;
				ground = enemy->GetPosition() + RE::NiPoint3{ std::cos(angle) * astray, std::sin(angle) * astray, 0.0f };
				over = kRainChest;
			} else {
				const float radius = std::sqrt(unit(rng)) * Paths::kRainRadius;
				ground = a_rain.centre + RE::NiPoint3{ std::cos(angle) * radius, std::sin(angle) * radius, 0.0f };
			}
			const RE::NiPoint3 spot = ground + RE::NiPoint3{ 0.0f, 0.0f, over };
			const RE::NiPoint3 origin = a_rain.portal + a_rain.right*((unit(rng)-.5f)*65.f) + RE::NiPoint3{0,0,(unit(rng)-.5f)*65.f};
            // Keep the stream forward even if its original target runs behind the caster.
            if((spot-origin).Dot(a_rain.forward)<80.f) { --a_rain.left; return; }
            // Do not create a projectile on the far side of a nearby wall.
            if(auto* cell=archer->GetParentCell();cell)if(auto* world=cell->GetbhkWorld();world){
                const auto start=archer->GetPosition()+RE::NiPoint3{0,0,ArrowRain::kPortalHeight};
                const float scale=RE::bhkWorld::GetWorldScale();RE::bhkPickData pick{};
                pick.rayInput.from={start.x*scale,start.y*scale,start.z*scale,0};
                pick.rayInput.to={origin.x*scale,origin.y*scale,origin.z*scale,0};
                RE::CFilter own{};archer->GetCollisionFilterInfo(own);
                pick.rayInput.filterInfo.filter=static_cast<std::uint32_t>(RE::COL_LAYER::kLOS)|(own.GetSystemGroup()?own.GetSystemGroup()<<16:1u<<16);
                world->PickObject(pick);if(!pick.pickFailed&&pick.rayOutput.HasHit()){a_rain.left=0;return;}
            }
			const auto         dir = spot - origin;
			RE::Projectile::ProjectileRot rotation{ -std::atan2(dir.z, std::hypot(dir.x, dir.y)), std::atan2(dir.x, dir.y) };
			RE::ProjectileHandle          handle;
			RE::Projectile::LaunchArrow(&handle, archer.get(), a_rain.ammo, a_rain.bow, origin, rotation);
			if(auto projectile=handle.get()){
                auto& data=projectile->GetProjectileRuntimeData();
                const auto velocity=dir*(ArrowRain::kSpeed/std::max(dir.Length(),1.f));
                data.velocity=velocity;data.linearVelocity=velocity;
                const auto* object=projectile->GetBaseObject();
                const auto* base=object?object->As<RE::BGSProjectile>():nullptr;
                if(base&&base->data.speed>0)data.speedMult=ArrowRain::kSpeed/base->data.speed;
            }
            Rainfall::Loose(a_rain.look, handle, spot, ground.z);
			Shaft shaft;
			shaft.handle = handle;
			shaft.at = origin;
			shaft.velocity = dir*(ArrowRain::kSpeed/std::max(dir.Length(),1.f));
			a_rain.shafts.push_back(shaft);
			--a_rain.left;
		}

		// An arrow down: anyone under the rain within a graze of where its point went in, but the one it
		// struck (the game's own hit has dealt with them), takes a share of its blow.
		void Graze(Rain& a_rain, const Shaft& a_shaft, RE::ObjectRefHandle a_struck)
		{
			if (a_struck) {
				++a_rain.struck;
			}
			const auto   speed = a_shaft.velocity.Length();
			const auto   tip = speed > 1.0f ? a_shaft.at + a_shaft.velocity * (ArrowRain::kTip / speed) : a_shaft.at;
			const float  blow = (static_cast<float>(a_rain.bow ? a_rain.bow->GetAttackDamage() : 0) + (a_rain.ammo ? a_rain.ammo->GetRuntimeData().data.damage : 0.0f)) *
				kRainGrazeShare;
			const auto   struck = a_struck.get();
			for (const auto& profile : Enemies::All()) {
				auto enemy = profile.handle.get();
				if (!enemy || !Standing(enemy.get()) || enemy.get() == struck.get()) {
					continue;
				}
				const auto at = enemy->GetPosition();
				if (std::hypot(at.x - tip.x, at.y - tip.y) <= kRainGraze && tip.z > at.z - kRainGraze && tip.z < at.z + kRainTall) {
					PartyBalance::Damage(a_rain.archer.get().get(), enemy.get(), blow);
					++a_rain.grazed;
				}
			}
		}

		// --- Mage -----------------------------------------------------------------

		Result Surge(Party::Member& a_member, RE::Actor* a_mage, const PathsLink::Kit& a_kit, State& a_state, bool a_asked)
		{
			const float magicka = Game::Fraction(a_mage, RE::ActorValue::kMagicka);
			if (!a_asked && !(a_member.drained || magicka < 0.3f)) {
				return {};
			}
			if (a_state.surge > 0.0f) {
				return a_asked ? Refused(Tr("Their magicka is already surging.")) : Result{};
			}
			const float times = Paths::Value(Perk::kSurge, std::max(a_kit.Rank(Ability::kSurge), 1));
			a_state.surge = a_state.surgeLength = Paths::kSurgeSeconds;
            Surges::Call(a_mage,a_state.surge,a_kit.Rank(Ability::kSurge));
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
			// Its distinct release cue is played once by Abilities::Used.
			Notices::Used(Trf("{}'s magicka surges: {:.0f}x as fast for {:.0f} s.", a_member.name, times, a_state.surge), Paths::Ability::kSurge, Notices::Kind::kHeal);
			Log::Info("[Paths] {} surges ({:.0f}x magicka, at {:.0f}%)."sv, a_member.name, times, magicka * 100.0f);
			return { true, Trf("Arcane surge: magicka {:.0f}x as fast.", times) };
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
			for(bool left:{false,true}) {auto* held=a_mage->GetEquippedObject(left);note(held?held->As<RE::SpellItem>():nullptr,100);}
            const auto pools=Loadouts::Effective(a_member.id);
            for(std::size_t hand=0;hand<2;hand++)for (const auto id : pools[hand]) {
				note(id ? RE::TESForm::LookupByID<RE::SpellItem>(id) : nullptr, 10);
			}
			for (auto* spell : Game::SpellsOf(a_mage)) {
				note(spell, 1);
			}
			const auto best = std::max_element(count.begin(), count.end());
			return *best > 0 ? static_cast<std::size_t>(best - count.begin()) : 0;
		}

		// The spell's own sound as it leaves the hand.
		RE::BGSSoundDescriptorForm* ReleaseOf(const RE::SpellItem* a_spell)
		{
			if (!a_spell) {
				return nullptr;
			}
			for (const auto* effect : a_spell->effects) {
				if (!effect || !effect->baseEffect) {
					continue;
				}
				for (const auto& pair : effect->baseEffect->effectSounds) {
					if (pair.id == RE::MagicSystem::SoundID::kRelease && pair.sound) {
						return pair.sound;
					}
				}
			}
			return nullptr;
		}

		// A spark of their element flies to the target's feet and bursts there (Blasts), striking every
		// enemy round it; the tier sets its force and reach.
		Result Burst(Party::Member& a_member, RE::Actor* a_mage, const PathsLink::Kit& a_kit, bool a_asked)
		{
			const bool debug=PathsLink::DebugCasting(a_member.id);
			const auto aim=PathsLink::DebugAim(a_mage);
			auto target = debug ? RE::NiPointer<RE::Actor>{} : a_asked ? Chosen(a_member, a_mage) : Opponent(a_mage);
			if (!debug && (!target || !InSight(a_mage, target.get(), a_asked ? 3000.0f : AbilityPolicy::kEnemyReach))) {
				return a_asked ? Refused(Tr("Nobody in sight to strike.")) : Result{};
			}
			const auto     element = ElementOf(a_member, a_mage);
			const auto     tier = static_cast<std::size_t>(std::clamp(static_cast<int>(Paths::Value(Perk::kBurst, std::max(a_kit.Rank(Ability::kBurst), 1))), 1, 3) - 1);
			auto*          spell = Forms::Get().bursts[element][tier];
			Blasts::Strike strike;
			strike.element = static_cast<Blast::Element>(element);
			strike.damage = Paths::kBurstDamage[tier];
			strike.radius = Blast::kRadius[tier];
			strike.grand = tier == Blast::kGrandTier;
			strike.stagger = 0.3f + 0.15f * static_cast<float>(tier);
			strike.searing = a_kit.Value(Perk::kBurstSearing);
			strike.searingSeconds = Paths::kSearingSeconds;
			strike.source = Source(Perk::kBurstSearing);
			strike.release = ReleaseOf(spell);
			if (!Blasts::Cast(a_mage, a_member.name, target.get(), strike, debug?&aim:nullptr)) {
				return Refused(Tr("Could not cast it."));
			}
			if (Settings::Get().abilityEffects) {
				Abilities::CastGesture(a_mage);
			}
			if (spell) {
				Abilities::HandGlow(a_mage, spell, 1.0f, kRightHand);
			}
			if (!debug && a_kit.Rank(Perk::kBurstTwin) > 0) {
				auto echo = strike;
				echo.damage *= Paths::kBurstEchoShare;
				const auto first = target->GetHandle();
				Later(a_mage, 0.45f, [=, name = a_member.name](RE::Actor* a_from) {
					auto                     was = first.get();
					RE::NiPointer<RE::Actor> next;
					if (was) {
						for (const auto& enemy : EnemiesWithin(was->GetPosition(), 800.0f)) {
							if (enemy != was && InSight(a_from, enemy.get(), AbilityPolicy::kEnemyReach)) {
								next = enemy;
								break;
							}
						}
					}
					if (!next) {
						next = was;
					}
					if (next && InSight(a_from, next.get(), AbilityPolicy::kEnemyReach)) {
						Blasts::Cast(a_from, name, next.get(), echo);
					}
				});
			}
			const auto name = debug?std::string(Tr("10 m ahead")):Game::Name(target.get());
			const std::string kind(element == 0 ? Tr("fire") : (element == 1 ? Tr("frost") : Tr("lightning")));
			Log::Info("[Paths] {} sends a {} burst at {} ({:.0f} at its middle, reach {:.0f}{})."sv, a_member.name, kind, name, strike.damage, strike.radius,
				strike.searing > 0.0f ? std::format(", searing {:.0f}", strike.searing) : "");
			Notices::Used(Trf("{} unleashes a burst of {} at {}.", a_member.name, kind, name), Ability::kBurst, Notices::Kind::kOrder);
			return { true, Trf("A burst of {} at {}.", kind, name) };
		}

		Result DragonBreathCast(Party::Member& member, RE::Actor* actor, const PathsLink::Kit& kit, bool asked)
        {
            const int rank=kit.Rank(Ability::kDragonBreath);
            const bool debug=PathsLink::DebugCasting(member.id);
            const auto aim=PathsLink::DebugAim(actor);
            auto target=debug?RE::NiPointer<RE::Actor>{}:asked?Chosen(member,actor):Opponent(actor);
            const float reach=DragonBreath::TargetReach(rank,kit.Rank(Perk::kBreathReach),Settings::Get().compactEffects);
            if(!debug&&(!target||!InSight(actor,target.get(),reach))) return asked?Refused(Tr("No enemy within breath range.")):Result{};
            const auto element=static_cast<int>(ElementOf(member,actor));
            if(!DragonBreaths::Cast(actor,target.get(),rank,element,kit.Rank(Perk::kBreathReach),kit.Rank(Perk::kBreathLingering),debug?&aim:nullptr)) return asked?Refused(Tr("Needs room in front to breathe.")):Result{};
            Abilities::CastGesture(actor);
            Notices::Used(Trf("{} unleashes a stream of {}.",member.name,element==0?Tr("fire"):element==1?Tr("frost"):Tr("lightning")),Ability::kDragonBreath,Notices::Kind::kOrder);
            return {true,std::string(Tr("Dragon's Breath unleashed."))};
        }

		// Magnus, the top of the Storm: for a while their magicka never runs dry, then their
		// destruction strikes harder, then magic turns aside from them too.
		Result Magnus(Party::Member& a_member, RE::Actor* a_mage, const PathsLink::Kit& a_kit, State& a_state, bool a_asked)
		{
			if (a_state.magnus > 0.0f) {
				return a_asked ? Refused(Trf("Magnus is already with them ({:.0f} s).", std::ceil(a_state.magnus))) : Result{};
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
			}
			// The great orb behind them, pouring magicka in; the third rank's is the grand one.
			Orbs::Summon(a_mage, a_member.name, a_state.magnus, rank >= 3);
			Notices::Used(Trf("{} draws on Magnus: magicka without end for {:.0f} s.", a_member.name, a_state.magnus), Paths::Ability::kMagnus, Notices::Kind::kHeal);
			Log::Info("[Paths] {} draws on Magnus (rank {}): {:.0f} s, destruction +{:.0f}%, resist magic +{:.0f}%."sv, a_member.name, rank, a_state.magnus,
				a_state.magnusPower * 100.0f, resist);
			return { true, Trf("Magnus: magicka without end for {:.0f} s.", a_state.magnus) };
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
		void Strengthen(RE::Actor* a_by, float a_power, RE::Actor* a_target, RE::FormID a_source)
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
			Hurt(a_by, a_target, effect->effectItem.magnitude * share);
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
				keep(RE::ActorValue::kStaminaRateMult, a_kit.Value(Perk::kSteadyBreath), Perk::kSteadyBreath);

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
				EffectAudio::Begin(Soundscape::Kind::Ward,a_actor,a_actor->GetPosition(),1.5f);
				Notices::Push(Trf("{} makes a last stand.", a_member.name), Notices::Kind::kTaunt, 0xF5D7, Tr("Last Stand"));
				Log::Info("[Paths] {} makes a last stand: +{:.0f}% health, +{:.0f} armor."sv, a_member.name, share * 100.0f, Paths::kLastStandArmor);
			}
			if (a_kit.role == Role::kHealer && !a_state.guardianSpent && a_kit.Rank(Perk::kGuardianSpirit) > 0) {
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!player->IsDead() && Game::Fraction(player, RE::ActorValue::kHealth) < Paths::kGuardianLine) {
					a_state.guardianSpent = true;
					const float share = a_kit.Value(Perk::kGuardianSpirit) / 100.0f * PathBonus::Healing(a_member.id);
					player->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kHealth, Game::Maximum(player, RE::ActorValue::kHealth) * share);
					Effects::Flash(player, Effects::Look::kHealAura, 2.0f);
					EffectAudio::Begin(Soundscape::Kind::SecondWind,a_actor,player->GetPosition(),1.5f);
					Effects::Flash(a_actor, Effects::Look::kHealAura);
					Notices::Push(Trf("{}'s guardian spirit pulls you back: +{:.0f}% health.", a_member.name, share * 100.0f), Notices::Kind::kHeal, 0xF6E2, Tr("Guardian Spirit"));
					Log::Info("[Paths] {}'s Guardian Spirit heals you for {:.0f}%."sv, a_member.name, share * 100.0f);
				}
			}
		}

		// --- Running them -------------------------------------------------------------

		Result Run(Party::Member& a_member, RE::Actor* a_actor, const PathsLink::Kit& a_kit, State& a_state, Ability a_ability, bool a_asked)
		{
            if(Skald::IsSong(a_ability)){
                if(!Skald::HasLute(a_actor))return Refused(Tr("Requires a lute in this companion inventory."));
                if(!a_kit.record || !a_kit.record->skaldUnlocked)return Refused(Tr("Unlock the Skald tree with 10 earned skill points first."));
                if(!Skald::Cast(a_actor,a_kit,a_ability))return {};
                return {true,std::string(Tr("Preparing the Ljodh."))};
            }
			switch (a_ability) {
			case Ability::kChaosDown:
				return ChaosDown(a_member, a_actor, a_kit, a_asked);
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
			case Ability::kSpectralWolf:
				return SpectralWolf(a_member, a_actor, a_kit, a_asked);
			case Ability::kDeadeye:
				return ExplosiveArrow(a_member, a_actor, a_kit, a_asked);
			case Ability::kPinningShot:
				return Windstep(a_member, a_actor, a_kit, a_asked);
			case Ability::kArrowRain:
				return ArrowRain(a_member, a_actor, a_kit, a_asked);
			case Ability::kSurge:
				return Surge(a_member, a_actor, a_kit, a_state, a_asked);
			case Ability::kDragonBreath:
				return DragonBreathCast(a_member, a_actor, a_kit, a_asked);
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
					auto* form = a_event->source ? RE::TESForm::LookupByID(a_event->source) : nullptr;
                    auto* weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr;
                    const bool melee = !a_event->projectile && (!form || (weapon && !weapon->IsBow() && !weapon->IsCrossbow() && !weapon->IsStaff()));
                    if (attacker && melee) {
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

	void DebugActivate(Party::Member& member, RE::Actor* actor, const PathsLink::Kit& kit, Paths::Ability ability)
	{
		if (!PathsLink::DebugCasting(member.id)) return;
		auto result=Run(member,actor,kit,StateOf(member.id),ability,true);
		Party::Answer(member,result.text.empty()?std::string(Tr("Nothing to do right now.")):result.text,result.done);
		if(result.done) Abilities::Used(member.id,actor,ability,true);
	}

	void Request(RE::FormID a_member, Paths::Ability a_ability)
	{
        if (!Party::CanHaveRole(a_member)) return;
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
            const float recovery=Skald::Recovery(id);
            for(std::size_t i=0;i<state.clocks.size();++i){auto& clock=state.clocks[i];clock.ready=Cooldowns::Down(clock.ready,a_seconds*(Skald::IsSong(static_cast<Ability>(i))?1.f:1.f+recovery));}
		}
	}

    void ReduceCooldowns(RE::FormID id,float seconds){
        const auto it=states.find(id);if(it==states.end())return;
        for(std::size_t i=0;i<it->second.clocks.size();++i)if(!Skald::IsSong(static_cast<Ability>(i)))it->second.clocks[i].ready=Cooldowns::Down(it->second.clocks[i].ready,seconds);
    }
    void CancelSong(RE::FormID id,Paths::Ability ability){const auto it=states.find(id);if(it!=states.end()&&Skald::IsSong(ability))it->second.clocks[static_cast<std::size_t>(ability)]={};}

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
			if (!member.CanTakeRole()) {
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
					Party::Answer(member, Trf("{} is not one of the abilities they carry.", Paths::Info(ability).name));
				} else if (!fighting) {
					clock.requested = false;
					Party::Answer(member, Tr("Only in a fight."));
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
                if(!asked && Ripostes::Guarding(member.id)) continue;
				auto       answer = [&](std::string_view a_text, bool a_done = false) {
                    if (asked) {
                        clock.requested = false;
                        Party::Answer(member, std::move(a_text), a_done);
                    }
				};
				if (!asked && (!settings.specials || !PathsLink::AutoOn(*orders, kit, ability))) {
					continue;
				}
				if (!settings.specials) {
					answer(Tr("Abilities are switched off (Battle plan settings)."));
					continue;
				}
				if (clock.ready > 0.0f) {
					answer(Trf("Not yet: ready in {:.0f} s.", std::ceil(clock.ready)));
					continue;
				}
				if (actor->AsActorState()->IsBleedingOut()) {
					answer(Tr("Down themselves."));
					continue;
				}
                if(Skald::Singing(member.id)){answer(Tr("Already performing a Ljodh."));continue;}
                if(Skald::IsSong(ability)&&Skald::Relocating(member.id))continue;
				if (DragonBreaths::Channeling(actor.get())) continue;
				if (Stunned(actor.get())) {
					if (asked && Abilities::WaitedTooLong(member)) {
						answer(Tr("Knocked about; could not."));
					}
					continue;
				}
				if (!asked && !AbilityAuto::Allow(member, actor.get(), ability)) continue;
				if (const auto block=Abilities::ResourceBlock(actor.get(),ability); !block.empty()) { answer(block); continue; }
				const auto result = Run(member, actor.get(), kit, state, ability, asked);
				if(Skald::IsSong(ability)&&!result.done&&result.text.empty()){clock.requested=false;continue;}
				answer(result.text.empty() ? std::string(Tr("Nothing to do right now.")) : result.text, result.done);
				if (result.done) {
					Abilities::Used(member.id, actor.get(), ability, asked);
					clock.cooldown = kit.Cooldown(ability);
					clock.ready = clock.cooldown;
				}
			}
		}
	}

	void Tick(float a_dt)
	{
        Ripostes::Tick(a_dt);
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
		Spirits(a_dt);
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
			Hurt(a_bleed.by.get().get(), enemy.get(), a_bleed.perSecond * a_dt);
			a_bleed.spurtIn -= a_dt;
			if (a_bleed.spurtIn <= 0.0f) {
				a_bleed.spurtIn = 0.8f;
				auto at = enemy->GetPosition();
				at.z += (Game::HeadPosition(enemy.get()).z - at.z) * 0.65f;
				Effects::Spray(enemy.get(), at, std::uniform_real_distribution<float>(0.0f, 2.0f * RE::NI_PI)(rng), Effects::Blood::kBleed);
			}
			return false;
		});
		// Arrows falling, and landing.
		for (auto& rain : rains) {
			rain.in -= a_dt;
			while (rain.in <= 0.0f && rain.left > 0) {
				Fall(rain);
				rain.in += rain.every;
			}
			std::erase_if(rain.shafts, [&](Shaft& a_shaft) {
                if(auto projectile=a_shaft.handle.get();projectile&&projectile->GetProjectileRuntimeData().impacts.empty()&&!projectile->GetProjectileRuntimeData().flags.any(RE::Projectile::Flags::kProcessedImpacts, RE::Projectile::Flags::kDestroyed)){
                    auto& data=projectile->GetProjectileRuntimeData();
                    const float speed=a_shaft.velocity.Length();
                    if(speed>1){data.velocity=a_shaft.velocity*(ArrowRain::kSpeed/speed);data.linearVelocity=data.velocity;}
                }
				a_shaft.flown += a_dt;
				RE::ObjectRefHandle struck;
				if (!Game::Landed(a_shaft, a_shaft.flown, a_dt, &struck)) {
					return false;
				}
				Graze(rain, a_shaft, struck);
				return true;
			});
		}
		std::erase_if(rains, [&](const Rain& a_rain) {
			if (a_rain.left > 0 || !a_rain.shafts.empty()) {
				return false;
			}
			Log::Info("[Paths] {}'s arrow rain is down: {} arrows struck someone, {} grazes."sv, a_rain.name, a_rain.struck, a_rain.grazed);
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
			if (!member.CanTakeRole()) {
				continue;
			}
			auto it = states.find(member.id);
			if (it == states.end()) {
				continue;
			}
			auto& state = it->second;
			state.berserk = std::max(state.berserk - a_dt, 0.0f);
			state.riposte = Ripostes::Guarding(member.id) ? std::max(state.riposte - a_dt, 0.0f) : 0;
			state.surge = std::max(state.surge - a_dt, 0.0f);
			state.magnus = std::max(state.magnus - a_dt, 0.0f);
			auto        actor = member.handle.get();
			const auto* orders = Party::FindOrders(member.id);
			if (!actor || !orders || !Game::Usable(actor.get()) || (!actor->IsInCombat() && !PathsLink::Unlimited(member.id))) {
				state.renewal = 0.0f;
				Cloaks::End(state.cloak);
				state.cloak = 0;
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
				if (state.renewal <= 0.0f) {
					state.cloak = 0;  // it falls away on its own as Renewal ends
				}
			}
			const auto kit = PathsLink::KitOf(member.id, orders->role);
			if (kit.tree && actor->IsInCombat()) {
				Brink(member, actor.get(), kit, state);
			}
		}
	}


	void Clear()
	{
        Ripostes::Clear();
		Berserks::End();
        Surges::End();
		StartTime(true);
		strikes.clear();
		for (auto& spirit : spirits) {
			if (spirit.effect) {
				spirit.effect->lifetime = 0.0f;
			}
		}
		spirits.clear();
		lastStrengthened.clear();
		magnusLive = false;
		pins.clear();
		bleeds.clear();
		rains.clear();
		for (auto& [id, state] : states) {
			state.berserk = state.riposte = state.surge = state.renewal = state.magnus = 0.0f;
			state.cloak = 0;  // Cloaks::Clear takes the cloaks themselves
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
						Party::Answer(*member, Tr("The fight is over."));
					}
				}
			}
		}
	}

	void Reset()
	{
		ArcherAmmo::Clear();
		Clear();
		states.clear();
		const std::scoped_lock guard(eventLock);
		blocked.clear();
		killers.clear();
	}

	void InstallEvents()
	{
		ArcherAmmo::Install();
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
		// Collect all three timers: an immobile target can still have its attacks slowed.
		std::map<RE::FormID, float> chaosTimes;
		for (const auto value : { RE::ActorValue::kSpeedMult, RE::ActorValue::kWeaponSpeedMult, RE::ActorValue::kLeftWeaponSpeedMultiply }) {
			for (const auto& [id, left] : Abilities::BoostTimes(value, Source(Perk::kChaosDown))) chaosTimes[id] = std::max(chaosTimes[id], left);
		}
		for (const auto& [id, left] : chaosTimes) add(RE::NiPointer<RE::Actor>(RE::TESForm::LookupByID<RE::Actor>(id)), left);
		ChaosPulses::Keep(on);  // Keep metadata while visuals are switched off; their controller observes the setting.
		on.clear();
		show(Effects::Look::kChaosDown);  // Retire any legacy floating rune art.
		if (!Settings::Get().abilityEffects) {
			for (const auto look : { Effects::Look::kBerserk, Effects::Look::kFocus, Effects::Look::kSurge, Effects::Look::kPinned, Effects::Look::kMagnus,
				Effects::Look::kRiposte, Effects::Look::kRenewal, Effects::Look::kBleeding, Effects::Look::kChaosDown }) {
				show(look);
			}
			return;
		}
		each(&State::berserk, Effects::Look::kBerserk);
		each(&State::surge, Effects::Look::kSurge);
		each(&State::magnus, Effects::Look::kMagnus);
		each(&State::riposte, Effects::Look::kRiposte);
		// Renewal's trance only on a healer whose cloak of water could not show.
		for (const auto& [id, state] : states) {
			add(RE::NiPointer<RE::Actor>(RE::TESForm::LookupByID<RE::Actor>(id)), state.cloak ? 0.0f : state.renewal);
		}
		show(Effects::Look::kRenewal);
		for (const auto& pin : pins) {
			add(pin.enemy.get(), pin.left);
		}
		show(Effects::Look::kPinned);
		for (const auto& bleed : bleeds) {
			add(bleed.enemy.get(), bleed.left);
		}
		show(Effects::Look::kBleeding);
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
				tweaks.defensive += 0.85f;
                tweaks.offensive -= 0.85f;
			}
			break;
		case Role::kArcher:

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
				a_out.push_back({ std::string(Tr("Berserk")), std::string(Tr("More damage and a power attack at every chance")), Paths::Info(Ability::kBerserk).glyph, Tint::kBuff, s.berserk, s.berserkLength });
			}
			if (s.riposte > 0.0f) {
				a_out.push_back({ std::string(Tr("Riposte")), std::string(Tr("Guarding the next melee blow: counter, knockback and floor impact")), Paths::Info(Ability::kRiposte).glyph, Tint::kArmor, s.riposte, s.riposteLength });
			}

			if (s.surge > 0.0f) {
				a_out.push_back({ std::string(Tr("Arcane Surge")), std::string(Tr("Magicka flowing back many times as fast")), Paths::Info(Ability::kSurge).glyph, Tint::kMagicka, s.surge, s.surgeLength });
			}
			if (s.magnus > 0.0f) {
				a_out.push_back({ std::string(Tr("Magnus")), std::string(Tr("Magicka without end")), Paths::Info(Ability::kMagnus).glyph, Tint::kMagicka, s.magnus, s.magnusLength });
			}
			if (s.renewal > 0.0f) {
				a_out.push_back({ std::string(Tr("Renewal")), std::string(Tr("Healing everyone near a little every second")), Paths::Info(Ability::kRenewal).glyph, Tint::kBuff, s.renewal, s.renewalLength });
			}
		}
		if (const auto will = TankActs::WillOn(a_member); will.left > 0.0f) {
			a_out.push_back({ std::string(Tr("Weak Will")), Trf("Own and attackers' damage reduced by {:.0f}%", will.percent), Paths::kGlyphSteelHelm, Tint::kArmor, will.left, will.length });
		}
		if (const auto bastion = TankActs::BastionOn(a_member); bastion.left > 0.0f) {
			a_out.push_back({ std::string(Tr("Last Bastion")), Trf("Absorbing {:.0f}% of nearby party damage", bastion.percent), Paths::kGlyphBastion, Tint::kArmor, bastion.left, bastion.length });
		}
        if (const auto wind = Abilities::BoostOn(a_member, RE::ActorValue::kSpeedMult, Source(Perk::kPinningShot))) {
            a_out.push_back({std::string(Tr("Windstep")), Trf("Moving {:.0f}% faster", wind->amount), Paths::Info(Ability::kPinningShot).glyph, Tint::kBuff, wind->left, wind->length});
        }
		if (const auto last = Abilities::BoostOn(a_member, RE::ActorValue::kDamageResist, Source(Perk::kLastStand))) {
			a_out.push_back({ std::string(Tr("Last Stand")), Trf("+{:.0f} armor", last->amount), 0xF5D7, Tint::kArmor, last->left, last->length });
		}
	}
}
