#include "FWM/Disarms.h"
#include "FWM/AbilityAuto.h"
#include "FWM/AbilityPolicy.h"
#include "FWM/Specials.h"
#include "FWM/ArrowArts.h"
#include "FWM/Loadouts.h"

#include "FWM/Abilities.h"
#include "FWM/AzurasMoon.h"
#include "FWM/ArcherAmmo.h"
#include "FWM/Effects.h"
#include "FWM/TauntBears.h"
#include "FWM/EffectAudio.h"
#include "FWM/Enemies.h"
#include "FWM/Forms.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Party.h"
#include "FWM/PathsLink.h"
#include "FWM/Settings.h"
#include "FWM/Text.h"
#include "FWM/TankActs.h"

namespace FWM::Specials
{
	namespace
	{
		struct DebugVolley { RE::ActorHandle actor; RE::NiPoint3 point; int left; float in; };
		std::vector<DebugVolley> debugVolleys;
		struct Volley
		{
			RE::ActorHandle target;
			RE::FormID      by{ 0 };
			float           remaining{ 0.0f };
		};
		Volley volley;


		// A disarm handed to the engine, checked a moment later for the log: the engine drops
		// the weapon on its own task queue, not while we wait.
		struct Disarming
		{
			RE::ActorHandle          target;
			const RE::TESObjectWEAP* weapon{ nullptr };
			std::string              by;
			float                    checkIn{ 0.0f };
		};
		std::vector<Disarming> disarming;

		constexpr float kVolleySeconds = 10.0f;

		std::mt19937 rng{ std::random_device{}() };

		// What came of a try. Nothing done and nothing said: on their own, and not needed.
		struct Result
		{
			bool        done{ false };
			std::string text;
		};

		Result Refused(std::string_view a_why) { return { false, std::string(a_why) }; }

		// Specials happen in the thick of it: mid-swing is no reason to wait (a fighter in a fight
		// almost always is), only being knocked about, down or in a kill move.
		bool Stunned(RE::Actor* a_actor)
		{
			auto* state = a_actor->AsActorState();
			return state->IsStaggered() || state->IsBleedingOut() || a_actor->IsInRagdollState() || a_actor->IsInKillMove();
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

		// The one they are fighting, if that is an enemy of the party: a companion who turned
		// on the player or a friend by mistake gets no special against them.
		RE::NiPointer<RE::Actor> Opponent(RE::Actor* a_actor)
		{
			auto* controller = Game::Controller(a_actor);
			auto  target = controller ? controller->targetHandle.get() : RE::NiPointer<RE::Actor>{};
			return target && !target->IsDead() && Enemies::Find(controller->targetHandle) ? target : nullptr;
		}

		// Someone a stagger or a disarm can land on: standing, not already thrown about.
		bool Standing(RE::Actor* a_actor)
		{
			auto* state = a_actor->AsActorState();
			return Game::Usable(a_actor) && !state->IsBleedingOut() && !a_actor->IsInRagdollState();
		}


		// The one of the five specials a path has slotted, if any (two can never both be).
		Special CarriedSpecial(const PathsLink::Kit& a_kit)
		{
			for (const auto ability : a_kit.slots) {
				if (const auto special = Paths::SpecialFor(ability); special != Special::kNone) {
					return special;
				}
			}
			return Special::kNone;
		}

		// --- Weak Will: attackers lose the force behind their attacks --------

		Result Bodyguard(Party::Member& a_member, RE::Actor* a_tank, bool a_asked, const PathsLink::Kit& a_kit)
		{
			if (!TankActs::WeakWill(a_tank, a_kit)) {
				return a_asked ? Refused(Tr("Weak Will is already active, or they cannot fight.")) : Result{};
			}
			a_member.guardFor = 0.0f;  // the saved Bodyguard slot no longer changes positioning
			Abilities::Challenge(a_tank);
			Abilities::Play(Forms::Get().tauntSound, a_tank);
			const auto status = TankActs::WillOn(a_member.id);
			Notices::Used(Trf("{}: \"Your strength fails you!\" They and their attackers deal {:.0f}% less damage.", a_member.name, status.percent), Paths::Ability::kBodyguard, Notices::Kind::kOrder);
			Log::Info("[Tank] {} invokes Weak Will: caster and attackers deal {:.0f}% less damage, stance {:.0f} s."sv, a_member.name, status.percent, status.length);
			return { true, Trf("Weak Will: they and their attackers deal {:.0f}% less damage.", status.percent) };
		}

		// --- Volley: everyone at range on one enemy --------------------------

		RE::NiPointer<RE::Actor> VolleyPick(Party::Member& a_member, RE::Actor* a_archer, bool a_asked)
		{
			if (a_asked) {
				if (auto marked = a_member.marked.get(); marked && !marked->IsDead() && Enemies::Find(a_member.marked)) {
					return marked;
				}
				return Opponent(a_archer);
			}
			// On their own: a boss, or someone well above their level, in sight.
			RE::NiPointer<RE::Actor> best;
			int                      bestWeight = -1;
			const int                level = a_archer->GetLevel();
			for (const auto& profile : Enemies::All()) {
				auto enemy = profile.handle.get();
				if (!enemy || !Game::Usable(enemy.get()) || Game::Fraction(enemy.get(), RE::ActorValue::kHealth) <= 0.35f || !(profile.boss || profile.level >= level + 5)) {
					continue;
				}
				bool unused = false;
				if (Game::Distance(a_archer, enemy.get()) > AbilityPolicy::kEnemyReach || !a_archer->HasLineOfSight(enemy.get(), unused)) {
					continue;
				}
				const int weight = profile.level + (profile.boss ? 100 : 0);
				if (weight > bestWeight) {
					bestWeight = weight;
					best = enemy;
				}
			}
			return best;
		}

		Result Volley(Party::Member& a_member, RE::Actor* a_archer, bool a_asked, const PathsLink::Kit& a_kit)
		{
			if (!a_asked && volley.remaining > 0.0f) {
				return {};
			}
			auto target = VolleyPick(a_member, a_archer, a_asked);
			if (!target) {
				return a_asked ? Refused(Tr("Nobody to fire at.")) : Result{};
			}
			const auto loadout=Loadouts::Ranged(a_archer);
			if (!loadout.first || !loadout.second) {
				return Refused(Tr("Nothing to shoot with."));
			}
			const auto  name = Game::Name(target.get());
			const int   rank = std::max(a_kit.Rank(Paths::Ability::kVolley), 1);
			const int   arrows = a_kit.tree ? static_cast<int>(Paths::Value(Paths::Perk::kVolley, rank)) : 5;
			const float seconds = a_kit.tree ? Paths::Second(Paths::Perk::kVolley, rank) : kVolleySeconds;
			// Arrows in quick succession from the one who calls it; anyone else at range turns on
			// the same enemy meanwhile.
			a_member.burstTarget = target->GetHandle();
			a_member.burstLeft = arrows;
			a_member.burstIn = 0.0f;
			volley = { target->GetHandle(), a_member.id, seconds };
			ArrowArts::VolleyWindow(a_archer,rank,seconds);
			// Rally Fire: everyone hits harder while it flies. Broadhead: its target's armor gives.
			if (const float rally = a_kit.Value(Paths::Perk::kVolleyRallyFire) / 100.0f; rally > 0.0f) {
				const auto source = static_cast<std::uint16_t>(Paths::Perk::kVolleyRallyFire);
				auto* player = RE::PlayerCharacter::GetSingleton();
				if (AbilityAuto::Visible(a_archer, player, AbilityPolicy::kSupportReach))
					Abilities::Lift(player->GetFormID(), RE::ActorValue::kAttackDamageMult, rally, seconds, source);
				for (const auto& each : Party::Members()) {
					const auto ally = each.handle.get();
					if (AbilityAuto::Visible(a_archer, ally.get(), AbilityPolicy::kSupportReach))
						Abilities::Lift(each.id, RE::ActorValue::kAttackDamageMult, rally, seconds, source);
				}
			}
			if (const float broadhead = a_kit.Value(Paths::Perk::kVolleyBroadhead); broadhead > 0.0f) {
				Abilities::Lift(target->GetFormID(), RE::ActorValue::kDamageResist, -broadhead, seconds, static_cast<std::uint16_t>(Paths::Perk::kVolleyBroadhead));
			}
			const auto count = arrows == 5 ? "five"s : std::format("{}", arrows);
			Notices::Used(Trf("Volley! {} looses {} arrows at {}.", a_member.name, count, name), Paths::Ability::kVolley, Notices::Kind::kOrder);
			Log::Info("[Special] {} calls a volley at {} (level {}, {:.0f} away): {} arrows, {:.0f} s."sv, a_member.name, name, target->GetLevel(),
				Game::Distance(a_archer, target.get()), arrows, seconds);
			return { true, Trf("Volley: {} arrows at {}.", count, name) };
		}

		// One arrow from the bow in hand (or the best carried), at the target's chest, a little
		// high for the drop. It costs an arrow from the quiver, like any other.
		bool Loose(RE::Actor* a_archer, RE::Actor* a_target)
		{
            const auto [bow,ammo]=Loadouts::Ranged(a_archer);
            if(!bow || !ammo)return false;
			auto*        model = a_archer->Get3D();
			auto*        hand = model ? model->GetObjectByName("NPC L Hand [LHnd]") : nullptr;
			RE::NiPoint3 origin = hand ? hand->world.translate : a_archer->GetPosition() + RE::NiPoint3{ 0.0f, 0.0f, 110.0f };
			RE::NiPoint3 aim = Game::HeadPosition(a_target) - RE::NiPoint3{ 0.0f, 0.0f, 25.0f };
			const float  flat = std::hypot(aim.x - origin.x, aim.y - origin.y);
			aim.z += 6.0e-6f * flat * flat;  // arrows drop
			const auto   dir = aim - origin;
			RE::Projectile::ProjectileRot rotation{ -std::atan2(dir.z, std::hypot(dir.x, dir.y)), std::atan2(dir.x, dir.y) };
			a_archer->SetHeading(rotation.z);
			RE::ProjectileHandle handle;
			RE::Projectile::LaunchArrow(&handle, a_archer, ammo, bow, origin, rotation);
			if (!ArcherAmmo::Unlimited(a_archer)) a_archer->RemoveItem(ammo, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			return true;
		}

		// --- War cry: everything close around them staggers ------------------

		Result WarCry(Party::Member& a_member, RE::Actor* a_bruiser, bool a_asked, const PathsLink::Kit& a_kit)
		{
			const int   rank = std::max(a_kit.Rank(Paths::Ability::kWarCry), 1);
			const float reach = a_kit.tree ? Paths::Value(Paths::Perk::kWarCry, rank) : 350.0f;
			const float force = a_kit.tree ? Paths::Second(Paths::Perk::kWarCry, rank) : 0.8f;
			std::vector<RE::NiPointer<RE::Actor>> close;
			for (const auto& profile : Enemies::All()) {
				auto enemy = profile.handle.get();
				if (enemy && Standing(enemy.get()) && Game::Distance(a_bruiser, enemy.get()) <= reach) {
					close.push_back(std::move(enemy));
				}
			}
			const float health = Game::Fraction(a_bruiser, RE::ActorValue::kHealth);
			if (!a_asked && !(close.size() >= 3 || (close.size() >= 2 && health < 0.4f))) {
				return {};
			}
			if (close.empty() && !PathsLink::DebugCasting(a_member.id)) {
				return Refused(Tr("Nobody close enough to stagger."));
			}
			Abilities::Challenge(a_bruiser);
			if (!TauntBears::WarCry(a_bruiser, rank)) {
				Effects::Flash(a_bruiser, Effects::Look::kWarCry);
				EffectAudio::Begin(Soundscape::Kind::WarCry, a_bruiser, a_bruiser->GetPosition(), 3.0f, rank, true);
			}
			int                                   staggered = 0;
			std::uniform_real_distribution<float> roll(0.0f, 1.0f);
			for (auto& enemy : close) {
				const auto* profile = Enemies::Find(enemy->GetFormID());
				if (enemy->AsActorState()->IsStaggered() || (profile && profile->boss && roll(rng) > 0.5f)) {
					continue;  // already reeling; a boss stands it half the time
				}
				// The engine's stagger, as from a heavy blow: it refuses anyone it would not
				// stagger itself, so the count is of those it really took.
				if (Game::Stagger(enemy.get(), force, a_bruiser)) {
					++staggered;
					Effects::Flash(enemy.get(), Effects::Look::kShaken);
					// Unnerve: those it staggers hit softer for a while.
					if (const float unnerve = a_kit.Value(Paths::Perk::kWarCryUnnerve) / 100.0f; unnerve > 0.0f) {
						Abilities::Lift(enemy->GetFormID(), RE::ActorValue::kAttackDamageMult, -unnerve, Paths::kUnnerveSeconds,
							static_cast<std::uint16_t>(Paths::Perk::kWarCryUnnerve));
					}
				}
			}
			// Rallying Roar: the roar gives everyone near some breath back.
			if (const float rally = a_kit.Value(Paths::Perk::kWarCryRally) / 100.0f; rally > 0.0f) {
				std::vector<RE::Actor*> allies{ RE::PlayerCharacter::GetSingleton() };
				for (const auto& each : Party::Members()) {
					if (auto ally = each.handle.get(); ally && Game::Usable(ally.get())) {
						allies.push_back(ally.get());
					}
				}
				for (auto* ally : allies) {
					if (Game::Distance(a_bruiser, ally) <= Paths::kRallyReach) {
						ally->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kStamina, Game::Maximum(ally, RE::ActorValue::kStamina) * rally);
						if (ally != a_bruiser) {
							Effects::Flash(ally, Effects::Look::kRally);
						}
					}
				}
			}
			Notices::Used(Trf("{}'s war cry staggers {}.", a_member.name, CountSaid(staggered)), Paths::Ability::kWarCry, Notices::Kind::kTaunt);
			Log::Info("[Special] {} war cry: {} of {} close by staggered."sv, a_member.name, staggered, close.size());
			return { true, staggered > 0 ? Trf("War cry: {} staggered.", CountSaid(staggered)) : std::string(Tr("War cry, but nobody staggered.")) };
		}

        // kWard is the saved ability ID now occupied by Azura's Moon.
        Result Ward(Party::Member& member, RE::Actor* caster, bool asked, const PathsLink::Kit& kit)
        {
            const int rank = kit.tree ? std::max(kit.Rank(Paths::Ability::kWard), 1) : (caster->GetLevel() >= 60 ? 3 : caster->GetLevel() >= 30 ? 2 : 1);
            if (!AzurasMoon::Cast(caster, rank, kit.Value(Paths::Perk::kWardShared), kit.Value(Paths::Perk::kWardSpellward), asked))
                return asked ? Refused(Tr("Azura's blessing is already active or unavailable.")) : Result{};
            Abilities::CastGesture(caster);
            Notices::Used(Trf("{} invokes Azura's Moon.", member.name), Paths::Ability::kWard, Notices::Kind::kHeal);
            return { true, std::string(Tr("Azura's blessing is spreading.")) };
        }

		// --- Disarm: the weapon out of their hands -----------------------------

		// What the shout would take: a real weapon, drawn, in the right hand. Fists, staves and
		// bound weapons stay.
		const RE::TESObjectWEAP* Disarmable(RE::Actor* a_target)
		{
			if (!a_target->AsActorState()->IsWeaponDrawn()) {
				return nullptr;
			}
			auto*       right = a_target->GetEquippedObject(false);
			const auto* weapon = right ? right->As<RE::TESObjectWEAP>() : nullptr;
			return weapon && !weapon->IsHandToHandMelee() && !weapon->IsStaff() && !weapon->IsBound() ? weapon : nullptr;
		}

		// What a path adds to a disarm that took: Expose's broken guard, Sweep's fall.
		void DisarmExtras(RE::Actor* a_target, RE::Actor* a_fighter, const PathsLink::Kit& a_kit)
		{
			if (const float expose = a_kit.Value(Paths::Perk::kDisarmExpose); expose > 0.0f) {
				Abilities::Lift(a_target->GetFormID(), RE::ActorValue::kDamageResist, -expose, Paths::kExposeSeconds,
					static_cast<std::uint16_t>(Paths::Perk::kDisarmExpose));
			}
			if (a_kit.Rank(Paths::Perk::kDisarmSweep) > 0) {
				if (auto* process = a_target->GetActorRuntimeData().currentProcess) {
					process->KnockExplosion(a_target, a_fighter->GetPosition(), 1.0f);
				}
			}
		}

		Result Disarm(Party::Member& a_member, RE::Actor* a_fighter, bool a_asked, const PathsLink::Kit& a_kit)
		{
			auto target = Opponent(a_fighter);
			if (!target || !Standing(target.get())) {
				return a_asked ? Refused(Tr("Nobody to disarm.")) : Result{};
			}
			const auto  name = Game::Name(target.get());
			const float d = Game::Distance(a_fighter, target.get());
			if (d > 450.0f) {
				return a_asked ? Refused(Trf("{} is too far ({:.0f} away).", name, d)) : Result{};
			}
			const auto* weapon = Disarmable(target.get());
			if (!weapon) {
				return a_asked ? Refused(Trf("{} holds no weapon to take.", name)) : Result{};
			}
			if (!a_asked) {
				const auto* profile = Enemies::Find(target->GetFormID());
				const bool  worth = (profile && profile->boss) || target->GetLevel() >= a_fighter->GetLevel() || weapon->IsTwoHandedSword() ||
				                   weapon->IsTwoHandedAxe();
				if (!worth || d > 300.0f) {
					return {};
				}
			}
			// As the shout: up to their own level and ten more (on a path, more with each rank); a
			// boss holds on half the time.
			const int above = a_kit.tree ? static_cast<int>(Paths::Value(Paths::Perk::kDisarm, std::max(a_kit.Rank(Paths::Ability::kDisarm), 1))) : 10;
			if (target->GetLevel() > a_fighter->GetLevel() + above) {
				return a_asked ? Refused(Trf("{} is too strong to disarm (level {}).", name, target->GetLevel())) : Result{};
			}
			const auto* profile = Enemies::Find(target->GetFormID());
			if (profile && profile->boss && std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < 0.5f) {
				Disarms::Strike(target.get(), a_kit.Rank(Paths::Ability::kDisarm));
				Log::Info("[Special] {} goes for {}'s {}, who holds on."sv, a_member.name, name, weapon->GetName());
				return { true, Trf("{} held on to the {}.", name, weapon->GetName()) };
			}
			// As the shout does it: a stagger first, and the weapon goes only if they reel. The
			// engine then drops it from their hand on its own task queue, at a safe point in the
			// frame, and their AI knows to go and pick it up.
			const std::string what = weapon->GetName();
			auto*             tasks = RE::TaskQueueInterface::GetSingleton();
			if (!tasks) {
				return Refused(Tr("Could not disarm right now."));
			}
			Disarms::Strike(target.get(), a_kit.Rank(Paths::Ability::kDisarm));
			if (!Game::Stagger(target.get(), 0.5f, a_fighter)) {
				Log::Info("[Special] {} goes for {}'s {}, who keeps their feet."sv, a_member.name, name, what);
				return { true, Trf("{} kept their feet and the {}.", name, what) };
			}
			auto targetHandle = target->GetHandle();
			auto fighterHandle = a_fighter->GetHandle();
			tasks->QueueActorDisarm(targetHandle, fighterHandle);
			disarming.push_back({ targetHandle, weapon, a_member.name, 1.5f });
			DisarmExtras(target.get(), a_fighter, a_kit);
			Notices::Used(Trf("{} knocks the {} out of {}'s hands.", a_member.name, what, name), Paths::Ability::kDisarm, Notices::Kind::kTaunt);
			Log::Info("[Special] {} disarms {} (level {}, {:.0f} away): the {}."sv, a_member.name, name, target->GetLevel(), d, what);
			return { true, Trf("Disarmed {}: the {} is on the ground.", name, what) };
		}

		// Whether the engine took the weapon, once it has had a moment to.
		void CheckDisarms(float a_dt)
		{
			std::erase_if(disarming, [&](Disarming& a_entry) {
				a_entry.checkIn -= a_dt;
				if (a_entry.checkIn > 0.0f) {
					return false;
				}
				auto target = a_entry.target.get();
				if (target && !target->IsDead() && target->GetEquippedObject(false) == a_entry.weapon) {
					Log::Info("[Special] {} still holds the {} after {}'s disarm."sv, Game::Name(target.get()), a_entry.weapon->GetName(), a_entry.by);
				}
				return true;
			});
		}
	}

	bool LooseArrow(RE::Actor* a_archer, RE::Actor* a_target)
	{
		return a_archer && a_target && Loose(a_archer, a_target);
	}

	bool TakeWeapon(RE::Actor* a_by, RE::Actor* a_target)
	{
		const auto* weapon = a_by && a_target ? Disarmable(a_target) : nullptr;
		auto*       tasks = RE::TaskQueueInterface::GetSingleton();
		if (!weapon || !tasks) {
			return false;
		}
		auto targetHandle = a_target->GetHandle();
		auto byHandle = a_by->GetHandle();
		Disarms::Strike(a_target, 1);
		tasks->QueueActorDisarm(targetHandle, byHandle);
		disarming.push_back({ targetHandle, weapon, Game::Name(a_by), 1.5f });
		return true;
	}

	float Cooldown(Special a_special)
	{
		return SpecialCooldown(a_special);
	}

	std::pair<RE::TESObjectWEAP*, RE::TESAmmo*> DebugRanged(RE::Actor* actor)
	{
		auto ranged=Loadouts::Ranged(actor);
		if(ranged.first && ranged.second) return ranged;
		// Tests copy forms without adding equipment to the follower's inventory.
		if(auto* data=RE::TESDataHandler::GetSingleton()) {
			for(auto* weapon:data->GetFormArray<RE::TESObjectWEAP>()) {
				if(weapon && weapon->IsBow()) return {weapon,Forms::Get().ironArrow};
			}
		}
		return {};
	}
	bool LooseArrowAt(RE::Actor* actor, const RE::NiPoint3& point)
	{
		if(!actor || !actor->Get3D()) return false;
		const auto [bow,ammo]=DebugRanged(actor);
		if(!bow || !ammo) return false;
		auto* hand=actor->Get3D()->GetObjectByName("NPC L Hand [LHnd]");
		const auto origin=hand?hand->world.translate:actor->GetPosition()+RE::NiPoint3{0,0,110};
		auto aim=point;const float flat=std::hypot(aim.x-origin.x,aim.y-origin.y);aim.z+=6.0e-6f*flat*flat;
		const auto dir=aim-origin;
		RE::Projectile::ProjectileRot rotation{-std::atan2(dir.z,std::hypot(dir.x,dir.y)),std::atan2(dir.x,dir.y)};
		RE::ProjectileHandle handle;RE::Projectile::LaunchArrow(&handle,actor,ammo,bow,origin,rotation);
		return static_cast<bool>(handle);
	}

	void DebugActivate(Party::Member& member, RE::Actor* actor, const PathsLink::Kit& kit, Paths::Ability ability)
	{
		if (!PathsLink::DebugCasting(member.id)) return;
		Result result;
		switch(Paths::SpecialFor(ability)) {
		case Special::kBodyguard: result=Bodyguard(member,actor,true,kit); break;
		case Special::kVolley: {
			const auto ranged=DebugRanged(actor);
			if(!ranged.first || !ranged.second) { result=Refused(Tr("No bow and arrow forms available.")); break; }
			const auto point=PathsLink::DebugAim(actor)+RE::NiPoint3{0,0,70};
			const int count=static_cast<int>(Paths::Value(Paths::Perk::kVolley,kit.Rank(ability)));
			debugVolleys.push_back({actor->GetHandle(),point,count,0});
			ArrowArts::VolleyWindow(actor,std::max(1,kit.Rank(ability)),static_cast<float>(count)*.28f+.5f);
			result={true,std::string(Tr("Volley: 10 m ahead."))}; break;
		}
		case Special::kWarCry: result=WarCry(member,actor,true,kit); break;
		case Special::kWard: result=Ward(member,actor,true,kit); break;
		case Special::kDisarm: Abilities::Challenge(actor); result={Disarms::Strike(actor,kit.Rank(ability)),std::string(Tr("Disarm effect activated."))}; break;
		default: break;
		}
		Party::Answer(member,result.text.empty()?std::string(Tr("Nothing to do right now.")):result.text,result.done);
		if(result.done) Abilities::Used(member.id,actor,ability,true);
	}

	void Update(float a_dt)
	{
		if (volley.remaining > 0.0f) {
			volley.remaining -= a_dt;
			auto target = volley.target.get();
			if (!target || target->IsDead() || volley.remaining <= 0.0f) {
				Log::Info("[Special] The volley is over{}."sv, target && target->IsDead() ? std::format(": {} is dead", Game::Name(target.get())) : "");
				volley = {};
			}
		}

		const auto& settings = Settings::Get();
		for (auto& member : Party::Members()) {
			if (!member.CanTakeRole()) {
				continue;
			}
			const bool asked = member.specialRequested;
			auto       answer = [&](std::string_view a_text, bool a_done = false) {
				if (asked) {
					member.specialRequested = false;
					Party::Answer(member, std::move(a_text), a_done);
				}
			};
			auto actor = member.handle.get();
			if (!actor || !Game::Usable(actor.get()) || !actor->IsInCombat()) {
				member.guardFor = 0.0f;
				member.burstLeft = 0;
				answer(Tr("Only in a fight."));
				continue;
			}
			const auto* orders = Party::FindOrders(member.id);
			if (!orders) {
				continue;
			}
			// On a path, the special is whichever of them the path has slotted, if any.
			const auto kit = PathsLink::KitOf(member.id, orders->role);
			const auto special = kit.tree ? CarriedSpecial(kit) : SpecialOf(orders->role);
			member.guardFor = 0.0f;
			if (special == Special::kNone) {
				answer(Tr("They carry no special ability right now."));
				continue;
			}
			if (!asked && (!settings.specials || !PathsLink::AutoOn(*orders, kit, Paths::AbilityOf(special)))) {
				continue;
			}
			if (!settings.specials) {
				answer(Tr("Specials are switched off (Battle plan settings)."));
				continue;
			}
			if (member.specialReady > 0.0f) {
				answer(Trf("Not yet: ready in {:.0f} s.", std::ceil(member.specialReady)));
				continue;
			}
			if (actor->AsActorState()->IsBleedingOut()) {
				answer(Tr("Down themselves."));
				continue;
			}
			if (Stunned(actor.get())) {
				if (asked && Abilities::WaitedTooLong(member)) {
					answer(Tr("Knocked about; could not."));
				}
				continue;
			}
			if (!asked && !AbilityAuto::Allow(member, actor.get(), Paths::AbilityOf(special))) continue;
			if (const auto block=Abilities::ResourceBlock(actor.get(),Paths::AbilityOf(special)); !block.empty()) { answer(block); continue; }
			Result result;
			switch (special) {
			case Special::kBodyguard:
				result = Bodyguard(member, actor.get(), asked, kit);
				break;
			case Special::kVolley:
				result = Volley(member, actor.get(), asked, kit);
				break;
			case Special::kWarCry:
				result = WarCry(member, actor.get(), asked, kit);
				break;
			case Special::kWard:
				result = Ward(member, actor.get(), asked, kit);
				break;
			case Special::kDisarm:
				result = Disarm(member, actor.get(), asked, kit);
				break;
			default:
				break;
			}
			answer(result.text.empty() ? std::string(Tr("Nothing to do right now.")) : result.text, result.done);
			if (result.done) {
				Abilities::Used(member.id, actor.get(), Paths::AbilityOf(special), asked);
				member.specialCooldown = kit.tree ? kit.Cooldown(Paths::AbilityOf(special)) : Cooldown(special);
				member.specialReady = member.specialCooldown;
			}
		}
	}

	void Tick(float a_dt)
	{
		CheckDisarms(a_dt);
		std::erase_if(debugVolleys,[&](DebugVolley& shot) {
			auto actor=shot.actor.get();
			if(!actor || !Game::Usable(actor.get()) || !PathsLink::Unlimited(actor->GetFormID())) return true;
			shot.in-=a_dt;
			if(shot.in<=0) { if(!LooseArrowAt(actor.get(),shot.point)) return true; --shot.left; shot.in=.28f; }
			return shot.left<=0;
		});
		for (auto& member : Party::Members()) {
			if (member.burstLeft <= 0) {
				continue;
			}
			member.burstIn -= a_dt;
			if (member.burstIn > 0.0f) {
				continue;
			}
			auto archer = member.handle.get();
			auto target = member.burstTarget.get();
			if (!archer || !target || target->IsDead() || !Game::Usable(archer.get()) || !archer->IsInCombat() || Stunned(archer.get())) {
				member.burstLeft = 0;
				continue;
			}
			if (!Loose(archer.get(), target.get())) {
				Log::Info("[Special] {} ran out of arrows with {} of the volley left."sv, member.name, member.burstLeft);
				member.burstLeft = 0;
				continue;
			}
			--member.burstLeft;
			member.burstIn = 0.28f;
		}
	}

	void Clear()
	{
		debugVolleys.clear();
		volley = {};
		disarming.clear();
		for (auto& member : Party::Members()) {
			member.guardFor = 0.0f;
			member.burstLeft = 0;
			if (member.specialRequested) {
				member.specialRequested = false;
				Party::Answer(member, Tr("The fight is over."));
			}
		}
	}

	RE::ActorHandle VolleyTarget()
	{
		return volley.remaining > 0.0f ? volley.target : RE::ActorHandle{};
	}

	float VolleyLeft()
	{
		return std::max(volley.remaining, 0.0f);
	}
}
