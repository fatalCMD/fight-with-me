#include "FWM/Specials.h"

#include "FWM/Abilities.h"
#include "FWM/Effects.h"
#include "FWM/Enemies.h"
#include "FWM/Forms.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Party.h"
#include "FWM/PathsLink.h"
#include "FWM/Settings.h"

namespace FWM::Specials
{
	namespace
	{
		struct Volley
		{
			RE::ActorHandle target;
			RE::FormID      by{ 0 };
			float           remaining{ 0.0f };
		};
		Volley volley;

		float wardLeft{ 0.0f };  // the ward on the player: one at a time, whoever cast it

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
		constexpr float kWardSeconds = 60.0f;  // as long as the flesh spells last

		std::mt19937 rng{ std::random_device{}() };

		// What came of a try. Nothing done and nothing said: on their own, and not needed.
		struct Result
		{
			bool        done{ false };
			std::string text;
		};

		Result Refused(std::string a_why) { return { false, std::move(a_why) }; }

		// Specials happen in the thick of it: mid-swing is no reason to wait (a fighter in a fight
		// almost always is), only being knocked about, down or in a kill move.
		bool Stunned(RE::Actor* a_actor)
		{
			auto* state = a_actor->AsActorState();
			return state->IsStaggered() || state->IsBleedingOut() || a_actor->IsInRagdollState() || a_actor->IsInKillMove();
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

		int OnPlayer()
		{
			const auto player = RE::PlayerCharacter::GetSingleton()->GetHandle();
			int        count = 0;
			for (const auto& profile : Enemies::All()) {
				auto enemy = profile.handle.get();
				if (enemy && Game::Usable(enemy.get()) && profile.target == player) {
					++count;
				}
			}
			return count;
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

		// --- Bodyguard: to your side, a challenge on arrival -----------------

		float GuardSeconds(const PathsLink::Kit& a_kit)
		{
			return a_kit.tree ? Paths::Value(Paths::Perk::kBodyguard, std::max(a_kit.Rank(Paths::Ability::kBodyguard), 1)) : kGuardSeconds;
		}

		// Shield Rush: arriving at your side, everyone close around you staggers.
		void ShieldRush(Party::Member& a_member, RE::Actor* a_tank)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			int   staggered = 0;
			for (const auto& profile : Enemies::All()) {
				auto enemy = profile.handle.get();
				if (enemy && Standing(enemy.get()) && Game::Distance(player, enemy.get()) <= Paths::kRushReach && Game::Stagger(enemy.get(), 0.6f, a_tank)) {
					Effects::Flash(enemy.get(), Effects::Look::kShaken);
					++staggered;
				}
			}
			if (staggered > 0) {
				Log::Info("[Special] {} arrives with a shield rush: {} staggered."sv, a_member.name, CountOf(staggered));
			}
		}

		Result Bodyguard(Party::Member& a_member, RE::Actor* a_tank, bool a_asked, const PathsLink::Kit& a_kit)
		{
			auto*       player = RE::PlayerCharacter::GetSingleton();
			const int   on = OnPlayer();
			const float health = Game::Fraction(player, RE::ActorValue::kHealth);
			if (!a_asked && !(health < 0.4f && on > 0)) {
				return {};
			}
			if (Game::Fraction(a_tank, RE::ActorValue::kHealth) < 0.25f) {
				return Refused("Too hurt to stand in front of you.");
			}
			a_member.guardFor = GuardSeconds(a_kit);
			if (const float armor = a_kit.Value(Paths::Perk::kBodyguardInterpose); armor > 0.0f) {
				Abilities::Lift(player->GetFormID(), RE::ActorValue::kDamageResist, armor, a_member.guardFor, static_cast<std::uint16_t>(Paths::Perk::kBodyguardInterpose));
			}
			a_member.guardTaunted = false;
			Effects::Flash(a_tank, Effects::Look::kGuard);
			Notices::Push(std::format("{} rushes to your side.", a_member.name), Notices::Kind::kOrder);
			Log::Info("[Special] {} guards you: {} on you, your health {:.0f}%, {:.0f} away."sv, a_member.name, CountOf(on), health * 100.0f,
				Game::Distance(a_tank, player));
			return { true, on > 0 ? "On the way to you." : "At your side; nobody is on you yet." };
		}

		// While guarding: the taunt on arrival, and back to their post once you are safe or the
		// moment has passed.
		void Guard(Party::Member& a_member, RE::Actor* a_tank, float a_dt, const PathsLink::Kit& a_kit)
		{
			if (a_member.guardFor <= 0.0f) {
				return;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			a_member.guardFor -= a_dt;
			if (!a_member.guardTaunted && Game::Distance(a_tank, player) < 450.0f) {
				a_member.guardTaunted = true;
				if (a_kit.Rank(Paths::Perk::kBodyguardRush) > 0) {
					ShieldRush(a_member, a_tank);
				}
				a_member.tauntReady = 0.0f;  // a bodyguard arrives with a challenge
				a_member.tauntRequested = true;
				a_member.askedAt = ::GetTickCount64();
			}
			if (a_member.guardFor <= 0.0f || (a_member.guardTaunted && Game::Fraction(player, RE::ActorValue::kHealth) > 0.75f)) {
				a_member.guardFor = 0.0f;
				Log::Info("[Special] {} goes back to their post."sv, a_member.name);
			}
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
				if (!enemy || !Game::Usable(enemy.get()) || !(profile.boss || profile.level >= level + 5)) {
					continue;
				}
				bool unused = false;
				if (Game::Distance(a_archer, enemy.get()) > 3500.0f || !a_archer->HasLineOfSight(enemy.get(), unused)) {
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
				return a_asked ? Refused("Nobody to fire at.") : Result{};
			}
			if (!(a_member.arsenal.ranged && a_member.arsenal.ammo)) {
				return Refused("Nothing to shoot with.");
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
			Effects::Flash(a_archer, Effects::Look::kVolley);
			// Rally Fire: everyone hits harder while it flies. Broadhead: its target's armor gives.
			if (const float rally = a_kit.Value(Paths::Perk::kVolleyRallyFire) / 100.0f; rally > 0.0f) {
				const auto source = static_cast<std::uint16_t>(Paths::Perk::kVolleyRallyFire);
				Abilities::Lift(RE::PlayerCharacter::GetSingleton()->GetFormID(), RE::ActorValue::kAttackDamageMult, rally, seconds, source);
				for (const auto& each : Party::Members()) {
					Abilities::Lift(each.id, RE::ActorValue::kAttackDamageMult, rally, seconds, source);
				}
			}
			if (const float broadhead = a_kit.Value(Paths::Perk::kVolleyBroadhead); broadhead > 0.0f) {
				Abilities::Lift(target->GetFormID(), RE::ActorValue::kDamageResist, -broadhead, seconds, static_cast<std::uint16_t>(Paths::Perk::kVolleyBroadhead));
			}
			const auto count = arrows == 5 ? "five"s : std::format("{}", arrows);
			Notices::Push(std::format("Volley! {} looses {} arrows at {}.", a_member.name, count, name), Notices::Kind::kOrder);
			Log::Info("[Special] {} calls a volley at {} (level {}, {:.0f} away): {} arrows, {:.0f} s."sv, a_member.name, name, target->GetLevel(),
				Game::Distance(a_archer, target.get()), arrows, seconds);
			return { true, std::format("Volley: {} arrows at {}.", count, name) };
		}

		// One arrow from the bow in hand (or the best carried), at the target's chest, a little
		// high for the drop. It costs an arrow from the quiver, like any other.
		bool Loose(RE::Actor* a_archer, RE::Actor* a_target)
		{
			RE::TESObjectWEAP* bow = nullptr;
			if (auto* right = a_archer->GetEquippedObject(false)) {
				if (auto* weapon = right->As<RE::TESObjectWEAP>(); weapon && weapon->IsBow()) {
					bow = weapon;
				}
			}
			RE::TESAmmo* ammo = a_archer->GetCurrentAmmo();
			std::int32_t have = 0;
			const auto   items = a_archer->GetInventory([](RE::TESBoundObject& a_object) { return a_object.IsWeapon() || a_object.IsAmmo(); });
			for (const auto& [object, entry] : items) {
				if (!object || entry.first <= 0) {
					continue;
				}
				if (auto* weapon = object->As<RE::TESObjectWEAP>(); !bow && weapon && weapon->IsBow()) {
					bow = weapon;
				}
				if (auto* arrows = object->As<RE::TESAmmo>(); arrows && !arrows->IsBolt() && (!ammo || ammo->IsBolt() || arrows == ammo)) {
					ammo = arrows;
					have = entry.first;
				}
			}
			if (!bow || !ammo || ammo->IsBolt() || have <= 0) {
				return false;
			}
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
			a_archer->RemoveItem(ammo, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
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
			if (close.empty()) {
				return Refused("Nobody close enough to stagger.");
			}
			Abilities::Challenge(a_bruiser);
			Abilities::Play(Forms::Get().tauntSound, a_bruiser);
			Effects::Flash(a_bruiser, Effects::Look::kWarCry);
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
			Notices::Push(std::format("{}'s war cry staggers {}.", a_member.name, CountOf(staggered)), Notices::Kind::kTaunt);
			Log::Info("[Special] {} war cry: {} of {} close by staggered."sv, a_member.name, staggered, close.size());
			return { true, staggered > 0 ? std::format("War cry: {} staggered.", CountOf(staggered)) : "War cry, but nobody staggered."s };
		}

		// --- Ward: alteration on the player ----------------------------------

		// The flesh spells reach only the one who casts them: cast through the warded one's own
		// hands, on them, with the companion to blame. True if it took.
		bool WardOne(RE::Actor* a_warded, RE::SpellItem* a_spell, RE::Actor* a_by)
		{
			auto* caster = a_warded ? a_warded->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant) : nullptr;
			if (!caster || !a_spell) {
				return false;
			}
			caster->CastSpellImmediate(a_spell, false, a_warded, 1.0f, false, 0.0f, a_by);
			auto* effect = a_spell->effects.empty() || !a_spell->effects[0] ? nullptr : a_spell->effects[0]->baseEffect;
			auto* target = a_warded->AsMagicTarget();
			return effect && target && target->HasMagicEffect(effect);
		}

		// What a path adds to each ward: Spellward's resistance, Flame Mantle's cloak.
		void WardExtras(RE::Actor* a_warded, RE::Actor* a_by, const PathsLink::Kit& a_kit)
		{
			if (const float spellward = a_kit.Value(Paths::Perk::kWardSpellward); spellward > 0.0f) {
				Abilities::Lift(a_warded->GetFormID(), RE::ActorValue::kResistMagic, spellward, Paths::kWardSeconds,
					static_cast<std::uint16_t>(Paths::Perk::kWardSpellward));
			}
			if (a_kit.Rank(Paths::Perk::kWardMantle) > 0) {
				WardOne(a_warded, Forms::Get().flameCloak, a_by);
			}
		}

		Result Ward(Party::Member& a_member, RE::Actor* a_caster, bool a_asked, const PathsLink::Kit& a_kit)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (wardLeft > 0.0f) {
				return a_asked ? Refused(std::format("Your ward is still up ({:.0f} s).", std::ceil(wardLeft))) : Result{};
			}
			const int   on = OnPlayer();
			const float health = Game::Fraction(player, RE::ActorValue::kHealth);
			if (!a_asked && !(on >= 2 || (on >= 1 && health < 0.6f))) {
				return {};
			}
			// Oakflesh, then Stone, Iron and Ebony as they rise in level; on a path, Oak, Iron and
			// Ebony by rank.
			const auto&    flesh = Forms::Get().flesh;
			const int      level = a_caster->GetLevel();
			RE::SpellItem* spell = nullptr;
			int            used = a_kit.tree ? static_cast<int>(Paths::Value(Paths::Perk::kWard, std::max(a_kit.Rank(Paths::Ability::kWard), 1))) :
			                                   (level < 15 ? 0 : (level < 30 ? 1 : (level < 45 ? 2 : 3)));
			for (; used >= 0 && !spell; --used) {
				spell = flesh[static_cast<std::size_t>(used)];
			}
			++used;  // the tier the spell came from
			if (!spell) {
				return Refused("No ward spell found in Skyrim.esm.");
			}
			const float cost = std::min(spell->CalculateMagickaCost(a_caster), 0.5f * Game::Maximum(a_caster, RE::ActorValue::kMagicka));
			if (Game::Current(a_caster, RE::ActorValue::kMagicka) < cost) {
				return Refused(std::format("Not enough magicka for {} ({:.0f}).", spell->GetName(), cost));
			}
			// Through the player's own hands, on them, with the companion to blame (and to pay).
			if (!WardOne(player, spell, a_caster)) {
				Log::Info("[Special] {}'s {} did not take on you."sv, a_member.name, spell->GetName());
				return Refused(std::format("{} did not take on you.", spell->GetName()));
			}
			a_caster->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kMagicka, cost);
			WardExtras(player, a_caster, a_kit);
			// Shared Ward: the most hurt of the others too, or all of them.
			int shared = 0;
			if (const int others = static_cast<int>(a_kit.Value(Paths::Perk::kWardShared)); others > 0) {
				std::vector<std::pair<float, RE::NiPointer<RE::Actor>>> hurt;
				for (const auto& each : Party::Members()) {
					auto ally = each.handle.get();
					if (ally && !each.summon && Game::Usable(ally.get()) && Game::Distance(a_caster, ally.get()) <= 4000.0f) {
						hurt.emplace_back(Game::Fraction(ally.get(), RE::ActorValue::kHealth), std::move(ally));
					}
				}
				std::sort(hurt.begin(), hurt.end(), [](const auto& a_a, const auto& a_b) { return a_a.first < a_b.first; });
				for (auto& [fraction, ally] : hurt) {
					if (shared >= others) {
						break;
					}
					if (WardOne(ally.get(), spell, a_caster)) {
						WardExtras(ally.get(), a_caster, a_kit);
						Effects::Flash(ally.get(), Effects::Look::kWardAura, 2.0f);
						++shared;
					}
				}
			}
			// A protective barrier on the caster and everyone who receives the ward.
			if (Settings::Get().abilityEffects) {
				Abilities::CastGesture(a_caster);
				Effects::Flash(a_caster, Effects::Look::kWardAura);
			}
			Abilities::HandGlow(a_caster, spell, 1.2f);
			Effects::Flash(player, Effects::Look::kWardAura);
			// The skin it gives, plainly: bark, stone, iron or ebony over you for a few seconds.
			if (auto* skin = Forms::Get().fleshShaders[static_cast<std::size_t>(std::clamp(used, 0, 3))]; skin && Settings::Get().abilityEffects) {
				player->ApplyEffectShader(skin, 4.0f);
			}
			wardLeft = kWardSeconds;
			Notices::Push(shared > 0 ? std::format("{} wards you and {} more: {}.", a_member.name, shared, spell->GetName()) :
			                           std::format("{} wards you: {}.", a_member.name, spell->GetName()),
				Notices::Kind::kHeal);
			Log::Info("[Special] {} casts {} on you ({:.0f} magicka; {} on you, your health {:.0f}%)."sv, a_member.name, spell->GetName(), cost, CountOf(on),
				health * 100.0f);
			return { true, std::format("{} on you.", spell->GetName()) };
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
				return a_asked ? Refused("Nobody to disarm.") : Result{};
			}
			const auto  name = Game::Name(target.get());
			const float d = Game::Distance(a_fighter, target.get());
			if (d > 450.0f) {
				return a_asked ? Refused(std::format("{} is too far ({:.0f} away).", name, d)) : Result{};
			}
			const auto* weapon = Disarmable(target.get());
			if (!weapon) {
				return a_asked ? Refused(std::format("{} holds no weapon to take.", name)) : Result{};
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
				return a_asked ? Refused(std::format("{} is too strong to disarm (level {}).", name, target->GetLevel())) : Result{};
			}
			const auto* profile = Enemies::Find(target->GetFormID());
			if (profile && profile->boss && std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < 0.5f) {
				Effects::Flash(a_fighter, Effects::Look::kStrike);
				Log::Info("[Special] {} goes for {}'s {}, who holds on."sv, a_member.name, name, weapon->GetName());
				return { true, std::format("{} held on to the {}.", name, weapon->GetName()) };
			}
			// As the shout does it: a stagger first, and the weapon goes only if they reel. The
			// engine then drops it from their hand on its own task queue, at a safe point in the
			// frame, and their AI knows to go and pick it up.
			const std::string what = weapon->GetName();
			auto*             tasks = RE::TaskQueueInterface::GetSingleton();
			if (!tasks) {
				return Refused("Could not disarm right now.");
			}
			Effects::Flash(a_fighter, Effects::Look::kStrike);
			if (!Game::Stagger(target.get(), 0.5f, a_fighter)) {
				Log::Info("[Special] {} goes for {}'s {}, who keeps their feet."sv, a_member.name, name, what);
				return { true, std::format("{} kept their feet and the {}.", name, what) };
			}
			auto targetHandle = target->GetHandle();
			auto fighterHandle = a_fighter->GetHandle();
			tasks->QueueActorDisarm(targetHandle, fighterHandle);
			Effects::Flash(target.get(), Effects::Look::kShaken);
			Effects::Sparks(target.get());
			disarming.push_back({ targetHandle, weapon, a_member.name, 1.5f });
			DisarmExtras(target.get(), a_fighter, a_kit);
			Notices::Push(std::format("{} knocks the {} out of {}'s hands.", a_member.name, what, name), Notices::Kind::kTaunt);
			Log::Info("[Special] {} disarms {} (level {}, {:.0f} away): the {}."sv, a_member.name, name, target->GetLevel(), d, what);
			return { true, std::format("Disarmed {}: the {} is on the ground.", name, what) };
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
		tasks->QueueActorDisarm(targetHandle, byHandle);
		Effects::Sparks(a_target);
		disarming.push_back({ targetHandle, weapon, Game::Name(a_by), 1.5f });
		return true;
	}

	float Cooldown(Special a_special)
	{
		return SpecialCooldown(a_special);
	}

	void Update(float a_dt)
	{
		wardLeft = std::max(wardLeft - a_dt, 0.0f);
		CheckDisarms(a_dt);
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
			if (member.summon) {
				continue;
			}
			const bool asked = member.specialRequested;
			auto       answer = [&](std::string a_text, bool a_done = false) {
				if (asked) {
					member.specialRequested = false;
					Party::Answer(member, std::move(a_text), a_done);
				}
			};
			auto actor = member.handle.get();
			if (!actor || !Game::Usable(actor.get()) || !actor->IsInCombat()) {
				member.guardFor = 0.0f;
				member.burstLeft = 0;
				answer("Only in a fight.");
				continue;
			}
			const auto* orders = Party::FindOrders(member.id);
			if (!orders) {
				continue;
			}
			// On a path, the special is whichever of them the path has slotted, if any.
			const auto kit = PathsLink::KitOf(member.id, orders->role);
			const auto special = kit.tree ? CarriedSpecial(kit) : SpecialOf(orders->role);
			if (special != Special::kBodyguard) {
				member.guardFor = 0.0f;
			}
			Guard(member, actor.get(), a_dt, kit);
			if (special == Special::kNone) {
				answer("They carry no special ability right now.");
				continue;
			}
			if (!asked && (!settings.specials || !PathsLink::AutoOn(*orders, kit, Paths::AbilityOf(special)))) {
				continue;
			}
			if (!settings.specials) {
				answer("Specials are switched off (Battle plan settings).");
				continue;
			}
			if (member.specialReady > 0.0f) {
				answer(std::format("Not yet: ready in {:.0f} s.", std::ceil(member.specialReady)));
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
			answer(result.text.empty() ? "Nothing to do right now."s : result.text, result.done);
			if (result.done) {
				Abilities::Used(member.id, actor.get());
				member.specialCooldown = kit.tree ? kit.Cooldown(Paths::AbilityOf(special)) : Cooldown(special);
				member.specialReady = member.specialCooldown;
			}
		}
	}

	void Tick(float a_dt)
	{
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
			member.burstIn = 0.14f;
		}
	}

	void Clear()
	{
		volley = {};
		disarming.clear();
		for (auto& member : Party::Members()) {
			member.guardFor = 0.0f;
			member.burstLeft = 0;
			if (member.specialRequested) {
				member.specialRequested = false;
				Party::Answer(member, "The fight is over.");
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
