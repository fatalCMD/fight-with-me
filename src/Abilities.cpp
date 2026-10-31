#include "FWM/Skald.h"
#include "FWM/AbilityAuto.h"
#include "FWM/AbilityPolicy.h"
#include "FWM/Abilities.h"
#include "FWM/Ripostes.h"
#include "FWM/Arts.h"
#include "FWM/Specials.h"
#include "FWM/DragonBreaths.h"
#include "FWM/AbilityResource.h"
#include "FWM/EffectAudio.h"
#include "FWM/TauntBears.h"

#include "FWM/Circles.h"
#include "FWM/ChaosDown.h"
#include "FWM/Effects.h"
#include "FWM/Enemies.h"
#include "FWM/Forms.h"
#include "FWM/Game.h"
#include "FWM/HealCircle.h"
#include "FWM/Rejuvenations.h"
#include "FWM/RejuvenateField.h"
#include "FWM/RejuvenatePolicy.h"
#include "FWM/Leash.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/PathBonus.h"
#include "FWM/Rules.h"
#include "FWM/Settings.h"
#include "FWM/Text.h"

#ifdef GetObject
#	undef GetObject  // the Windows macro, not the default object lookup
#endif

namespace FWM::Abilities
{
	bool Busy(RE::Actor* a_actor)
	{
		auto* state = a_actor->AsActorState();
		return a_actor->IsAttacking() || a_actor->IsInKillMove() || a_actor->IsInRagdollState() || state->IsStaggered() ||
		       state->IsBleedingOut() || a_actor->IsStaggering();
	}

	// Busy (mid-swing, staggered) is waited out for a moment of real time, then answered.
	bool WaitedTooLong(const Party::Member& a_member)
	{
		return ::GetTickCount64() - a_member.askedAt > 1500;
	}

	void Play(RE::BGSSoundDescriptorForm* a_sound, RE::Actor* a_at)
	{
		auto* audio = RE::BSAudioManager::GetSingleton();
		if (!audio || !a_sound || !a_at) {
			return;
		}
		RE::BSSoundHandle handle;
		if (!audio->GetSoundHandle(handle, a_sound)) {
			return;
		}
		handle.SetPosition(a_at->GetPosition());
		handle.SetObjectToFollow(a_at->Get3D());
		handle.Play();
	}

	// An idle pushed past the AI's checks can leave someone in a pose the combat AI does not
	// know how to leave: a drink or a cheer mid-fight, weapons still out.
	bool PlayIdle(RE::Actor* a_actor, RE::TESIdleForm* a_idle)
	{
		if (!a_actor || !a_idle) {
			return false;
		}
		auto* process = a_actor->GetActorRuntimeData().currentProcess;
		return process && process->PlayIdle(a_actor, a_idle, nullptr);
	}

	bool Challenge(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}
		const auto& forms = Forms::Get();
		const bool  shield = Game::LoadoutOf(a_actor) == Game::Loadout::kShield;
		for (auto* idle : { shield ? forms.idleShield : forms.idleCry, shield ? nullptr : forms.idleWeapon }) {
			if (PlayIdle(a_actor, idle)) {
				return true;
			}
		}
		// Mid-swing the AI refuses. The two combat idles are made for a drawn weapon and end
		// on their own, so their event can be sent anyway; the cheer never is.
		auto* combat = shield ? forms.idleShield : forms.idleWeapon;
		return combat && !combat->animEventName.empty() && a_actor->NotifyAnimationGraph(combat->animEventName);
	}

	bool CastGesture(RE::Actor* a_caster)
	{
		auto* idle = Forms::Get().idleMagic;
		if (!a_caster || !idle || Busy(a_caster)) {
			return false;
		}
		if (PlayIdle(a_caster, idle)) {
			return true;
		}
		// Refused mid-fight. With a spell in hand the gesture belongs to the stance they are in
		// and ends on its own, so its event is sent anyway, as the taunt's combat idles are.
		auto       holdsSpell = [&](bool a_left) {
            auto* form = a_caster->GetEquippedObject(a_left);
            return form && form->Is(RE::FormType::Spell);
		};
		const bool ready = a_caster->AsActorState()->IsWeaponDrawn() && (holdsSpell(true) || holdsSpell(false));
		return ready && !idle->animEventName.empty() && a_caster->NotifyAnimationGraph(idle->animEventName);
	}

    float ResourceCost(RE::Actor* actor, Paths::Ability ability) {
        if (!actor || PathsLink::DebugCasting(actor->GetFormID())) return 0;
        const auto* orders=Party::FindOrders(actor->GetFormID());
        if (!orders) return 0;
        const auto kit=PathsLink::KitOf(actor->GetFormID(),orders->role);
        const int rank=kit.tree?std::max(1,kit.Rank(ability)):(actor->GetLevel()>=60?3:actor->GetLevel()>=30?2:1);
        return AbilityResource::Cost(Game::Maximum(actor,ChoosesSpells(orders->role)?RE::ActorValue::kMagicka:RE::ActorValue::kStamina),rank)*Skald::ResourceFactor(actor->GetFormID(),ability);
    }
    std::string ResourceBlock(RE::Actor* actor, Paths::Ability ability) {
        if (!actor) return std::string(Tr("Companion unavailable."));
        if(Skald::IsSong(ability)&&!Skald::HasLute(actor))return std::string(Tr("Requires a lute in this companion inventory."));
        if (PathsLink::DebugCasting(actor->GetFormID())) return {};
        const auto* orders=Party::FindOrders(actor->GetFormID());
        if (!orders) return std::string(Tr("No role selected."));
        if (orders->role==Role::kBruiser && !Game::ArsenalOf(actor).twoHanded) return std::string(Tr("Requires a two-handed melee weapon in inventory."));
        const bool magic=ChoosesSpells(orders->role);
        const float maximum=Game::Maximum(actor,magic?RE::ActorValue::kMagicka:RE::ActorValue::kStamina),cost=ResourceCost(actor,ability);
        if (!std::isfinite(maximum) || maximum<=0 || !std::isfinite(Game::Current(actor,magic?RE::ActorValue::kMagicka:RE::ActorValue::kStamina)) || Game::Current(actor,magic?RE::ActorValue::kMagicka:RE::ActorValue::kStamina)<cost)
            return Trf("Not enough {}: requires {:.0f}.",magic?Tr("magicka"):Tr("stamina"),cost);
        return {};
    }
	void Used(RE::FormID a_member, RE::Actor* a_actor, Paths::Ability a_ability, bool a_ordered)
	{
        if (const auto* orders=Party::FindOrders(a_member); orders && a_actor) {
            const auto value=ChoosesSpells(orders->role)?RE::ActorValue::kMagicka:RE::ActorValue::kStamina;
            const float cost=ResourceCost(a_actor,a_ability);
            a_actor->AsActorValueOwner()->DamageActorValue(value,cost);
            Log::Info("[AbilityCost] {} uses {}: {:.1f} {} spent, {:.1f} remaining."sv,Game::Name(a_actor),Paths::Info(a_ability).name,cost,ChoosesSpells(orders->role)?"magicka":"stamina",Game::Current(a_actor,value));
        }
        Skald::ConsumeInspiration(a_member,a_ability);
		if (!PathsLink::DebugCasting(a_member)) AbilityAuto::Used(a_member, a_actor, a_ability, a_ordered);
		// Only activation cues here. Timed impacts and persistent effects own their audio.
		Soundscape::Kind sound=Soundscape::Kind::Count;
		switch(a_ability){
		case Paths::Ability::kSecondWind:sound=Soundscape::Kind::SecondWind;break;
		case Paths::Ability::kRiposte:break; // clang belongs to the intercepted hit
		case Paths::Ability::kDeadeye:break; // impact owns its sound
		case Paths::Ability::kVolley:break; // each real shot owns its gust
		case Paths::Ability::kSurge:sound=Soundscape::Kind::Surge;break;
		case Paths::Ability::kBerserk:sound=Soundscape::Kind::Berserk;break;
		case Paths::Ability::kWard:sound=Soundscape::Kind::Ward;break;
		case Paths::Ability::kDisarm:sound=Soundscape::Kind::Disarm;break;
		default:break;
		}
		int rank = 1;
		if (const auto* orders = Party::FindOrders(a_member)) rank = std::max(1, PathsLink::KitOf(a_member, orders->role).Rank(a_ability));
		if(a_actor&&sound!=Soundscape::Kind::Count)EffectAudio::Begin(sound,a_actor,a_actor->GetPosition(),2.5f,rank,true);
		if (rank >= 3 && a_actor) {
			Effects::Look look = Effects::Look::kCount;
			switch (a_ability) {
			// Second Wind supplies its own layered rank III burst.
			case Paths::Ability::kRiposte: break; // authored counter and impact
			case Paths::Ability::kDeadeye: break; // spinning rune and explosion
			case Paths::Ability::kVolley: look=Effects::Look::kVolley; break;
			case Paths::Ability::kSurge: look=Effects::Look::kSurge; break;
			// Berserk's own mantle supplies its rank III reveal.
			case Paths::Ability::kWarCry: look=Effects::Look::kWarCry; break;
			// Disarm supplies its own rank III weapon flash and crossed slashes.
			case Paths::Ability::kPinningShot: break; // Windstep supplies its own echoes and gust
			default: break;
			}
			if (look != Effects::Look::kCount) Effects::FinalRank(a_actor, look);
		}

	}

	void HandGlow(RE::Actor* a_caster, RE::SpellItem* a_spell, float a_seconds, std::size_t a_hand)
	{
		if (!a_caster || !a_spell || a_spell->effects.empty() || !a_spell->effects[0] || !a_spell->effects[0]->baseEffect) {
			return;
		}
		if (auto* art = a_spell->effects[0]->baseEffect->data.castingArt) {
			auto* model = a_caster->Get3D();
			auto* hand = model ? model->GetObjectByName(a_hand == kRightHand ? "NPC R MagicNode [RMag]" : "NPC L MagicNode [LMag]") : nullptr;
			a_caster->ApplyArtObject(art, a_seconds, nullptr, false, false, hand);
		}
	}

	namespace
	{
		std::vector<Hold> holds;
		std::mt19937      rng{ std::random_device{}() };

		constexpr float kUnitsPerFoot = 21.33f;  // effect areas are authored in feet

		using Party::Answer;

		bool Hidden(RE::Actor* a_actor)
		{
			return Game::Current(a_actor, RE::ActorValue::kInvisibility) > 0.0f;
		}

		constexpr float kTauntWait = 1.2f;  // seconds a taunt waits for its animation

		// --- Boosts: a taunt's armor, Rejuvenate's magicka ------------------

		// A lift on one of someone's values for a while. It sits on the value's temporary
		// modifier, which a save keeps, so each comes off when it runs out, and whatever was on
		// when the game saved goes in the co-save and comes off again after loading it.
		struct Boost
		{
			RE::FormID     id{ 0 };
			RE::ActorValue value{ RE::ActorValue::kNone };
			float          amount{ 0.0f };
			float          left{ 0.0f };
			float          length{ 0.0f };  // how long it was set for
			std::uint16_t  source{ 0 };     // what gave it: 0 for a taunt's armor and Rejuvenate without a path, else the Paths::Perk
		};
		std::vector<Boost> boosts;
		std::vector<Boost> fromSave;  // on in a save just loaded; taken off at the next update

		void Mod(RE::FormID a_id, RE::ActorValue a_value, float a_amount, bool a_removing = false)
		{
			if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_id); actor && a_amount != 0.0f) {
				actor->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, a_value, a_amount);
				if (a_removing && (a_value == RE::ActorValue::kWeaponSpeedMult || a_value == RE::ActorValue::kLeftWeaponSpeedMultiply)) {
					// Skyrim's unset zero means ordinary attack speed. A tiny positive rounding
					// remnant would instead mean almost no attacks. Leave substantive modifiers alone.
					const float correction = ChaosDown::AttackUnsetCorrection(Game::Current(actor, a_value));
					if (correction != 0.0f) actor->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, a_value, correction);
				}
				if (a_value == RE::ActorValue::kSpeedMult) {
					// Skyrim can cache movement until encumbrance changes. Invalidate it both
					// when slowing and when restoring, without leaving any carry-weight change.
					auto* values = actor->AsActorValueOwner();
					values->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, 0.1f);
					values->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kCarryWeight, -0.1f);
				}
			}
		}

		// Off at once: every boost of one kind, or every boost.
		void Drop(std::optional<RE::ActorValue> a_value)
		{
			std::erase_if(boosts, [&](const Boost& a_boost) {
				if (a_value && a_boost.value != *a_value) {
					return false;
				}
				Mod(a_boost.id, a_boost.value, -a_boost.amount, true);
				return true;
			});
		}

		// (who, amount) or (who, seconds left) for every boost of one kind.
		std::vector<std::pair<std::uint32_t, float>> Listed(RE::ActorValue a_value, bool a_left)
		{
			std::vector<std::pair<std::uint32_t, float>> out;
			for (const auto& boost : boosts) {
				if (boost.value == a_value) {
					out.emplace_back(boost.id, a_left ? boost.left : boost.amount);
				}
			}
			return out;
		}

		// The numbers behind a taunt: from the tank's path when they have one, else from
		// their level.
		struct TauntNumbers
		{
			float hold{ 0.0f };      // for an enemy of the tank's own level
			float cooldown{ 0.0f };
			float armor{ 0.0f };     // braced with
			float intimidate{ 0.0f };  // held enemies deal this much less, 0..1
		};

		TauntNumbers NumbersFor(RE::Actor* a_tank, const PathsLink::Kit& a_kit)
		{
			const auto&  settings = Settings::Get();
			const auto&  tuning = settings.tuning;
			const int    level = a_tank->GetLevel();
			TauntNumbers n;
			if (a_kit.tree) {
				n.hold = Paths::Value(Paths::Perk::kTaunt, std::max(a_kit.Rank(Paths::Ability::kTaunt), 1));
				n.cooldown = a_kit.Cooldown(Paths::Ability::kTaunt);
				n.armor = settings.brace ? a_kit.Value(Paths::Perk::kTauntBrace) : 0.0f;
				n.intimidate = a_kit.Value(Paths::Perk::kTauntIntimidate) / 100.0f;
			} else {
				n.hold = Rules::HoldSeconds(level, level, tuning);
				n.cooldown = Rules::TauntCooldown(level, tuning);
				n.armor = settings.brace ? Rules::TauntArmor(level, tuning) : 0.0f;
			}
			return n;
		}

		// Seconds an enemy stays on the tank. On a path, a rank sets the hold, and an enemy far
		// below the tank still stays longer, as without one.
		float HoldFor(int a_tankLevel, int a_enemyLevel, const TauntNumbers& a_numbers, const PathsLink::Kit& a_kit)
		{
			const auto& tuning = Settings::Get().tuning;
			if (!a_kit.tree) {
				return Rules::HoldSeconds(a_tankLevel, a_enemyLevel, tuning);
			}
			const bool cowed = a_tankLevel - a_enemyLevel >= tuning.cowedLevelGap;
			return a_numbers.hold * (cowed ? tuning.cowedHoldMult : 1.0f);
		}

		// Braced, or braced again, for as long as their taunt holds an enemy of their own level.
		float BraceTank(RE::Actor* a_tank, const TauntNumbers& a_numbers, const PathsLink::Kit& a_kit)
		{
			if (a_numbers.armor > 0.0f) {
				Lift(a_tank->GetFormID(), RE::ActorValue::kDamageResist, a_numbers.armor, a_numbers.hold,
					a_kit.tree ? static_cast<std::uint16_t>(Paths::Perk::kTauntBrace) : 0);
			}
			return a_numbers.armor;
		}

		// For the player: a number and the noun, so a translation needs only its two forms.
		std::string CountSaid(int a_count)
		{
			return a_count == 0 ? std::string(Tr("no enemy")) : Trn(a_count, "{} enemy", "{} enemies");
		}

		std::string Count(int a_count)
		{
			static constexpr std::array<std::string_view, 7> words{ "no enemy", "one enemy", "two enemies", "three enemies", "four enemies", "five enemies", "six enemies" };
			return a_count < static_cast<int>(words.size()) ? std::string(words[static_cast<std::size_t>(a_count)]) : std::format("{} enemies", a_count);
		}

		// --- Shield wall ---------------------------------------------------

		constexpr float kShieldReach = 320.0f;  // anything this close is fought, not blocked
		constexpr float kShieldMax = 8.0f;      // longest it stays up before looking again
		constexpr float kShieldRest = 1.5f;

		bool HasShield(RE::Actor* a_actor)
		{
			auto* left = a_actor->GetEquippedObject(true);
			return left && left->IsArmor();
		}

		void LowerShield(Party::Member& a_member, RE::Actor* a_tank, std::string_view a_why = {})
		{
			if (!a_member.shielding) {
				return;
			}
			a_member.shielding = false;
			a_member.shieldFacing = {};
			a_member.shieldRest = std::max(a_member.shieldRest, kShieldRest);
			if (a_tank && !Ripostes::Guarding(a_tank->GetFormID())) {
				a_tank->NotifyAnimationGraph("blockStop");
			}
			if (!a_why.empty()) {
				Log::Info("[Taunt] {} lowers the shield: {}."sv, a_member.name, a_why);
			}
		}

		// The block raised against an arrow in flight (BlockIncoming), let go of.
		void DropBlock(Party::Member& a_member, RE::Actor* a_tank)
		{
			if (a_member.blockRaised && a_tank && !a_member.shielding && !Ripostes::Guarding(a_tank->GetFormID())) {
				a_tank->NotifyAnimationGraph("blockStop");
			}
			a_member.blockRaised = false;
			a_member.blockHold = 0.0f;
			a_member.blockTries = 0;
		}

		// Drawing fire is only half the job: arrows and spells aimed at the tank should land
		// on a raised shield. Up against whoever is shooting at them, turned to the nearest
		// (Battle hands that one to the combat AI, which turns and closes under the shield),
		// and down the moment anything is close enough to fight.
		void ShieldWall(Party::Member& a_member, RE::Actor* a_tank, float a_dt)
		{
			a_member.shieldRest = std::max(a_member.shieldRest - a_dt, 0.0f);
			auto* state = a_tank->AsActorState();
			if (!Settings::Get().shieldWall || !HasShield(a_tank) || state->IsBleedingOut() || state->IsStaggered() || a_tank->IsInKillMove()) {
				LowerShield(a_member, a_tank, a_member.shielding ? "they cannot hold it"sv : ""sv);
				return;
			}
			// Sent after someone: a raised shield slows the walk there and turns them away from it.
			if (a_member.marked) {
				LowerShield(a_member, a_tank, a_member.shielding ? "sent after a marked enemy"sv : ""sv);
				return;
			}

			const auto               tankHandle = a_tank->GetHandle();
			RE::NiPointer<RE::Actor> nearest;
			float                    nearestAt = std::numeric_limits<float>::max();
			int                      shooters = 0;
			bool                     inReach = false;
			for (const auto& profile : Enemies::All()) {
				auto enemy = profile.handle.get();
				if (!enemy || !Game::Usable(enemy.get())) {
					continue;
				}
				const float d = Game::Distance(a_tank, enemy.get());
				// A shooter the tank cannot get to (on a ledge, across a gap) is no reason to stand
				// behind the shield facing them: that is all the tank would ever do.
				const bool outOfReach = std::any_of(a_member.outOfReach.begin(), a_member.outOfReach.end(),
					[&](const auto& a_entry) { return a_entry.first == profile.handle; });
				if (d <= kShieldReach) {
					inReach = true;
				} else if ((profile.archer || profile.caster) && profile.target == tankHandle && !outOfReach) {
					++shooters;
					if (d < nearestAt) {
						nearestAt = d;
						nearest = enemy;
					}
				}
			}

			if (a_member.shielding) {
				a_member.shieldFor += a_dt;
				if (inReach) {
					LowerShield(a_member, a_tank, "an enemy is in reach"sv);
				} else if (shooters == 0) {
					LowerShield(a_member, a_tank, "nobody is shooting at them"sv);
				} else if (a_member.shieldFor > kShieldMax) {
					LowerShield(a_member, a_tank);  // a breath, then up again if still needed
				} else if (!a_tank->IsBlocking()) {
					// The game drops a block for many reasons; ask again, but not forever.
					if (++a_member.shieldMisses >= 3) {
						LowerShield(a_member, a_tank, "the game will not hold the block"sv);
						a_member.shieldRest = 6.0f;
					} else {
						a_tank->NotifyAnimationGraph("blockStart");
						a_member.shieldFacing = nearest->GetHandle();
					}
				} else {
					a_member.shieldMisses = 0;
					a_member.shieldFacing = nearest->GetHandle();
				}
				return;
			}

			if (a_member.shieldRest > 0.0f || inReach || shooters == 0 || a_tank->IsAttacking()) {
				return;
			}
			if (!a_tank->NotifyAnimationGraph("blockStart")) {
				a_member.shieldRest = 3.0f;  // the graph refused it right now
				return;
			}
			a_member.shielding = true;
			a_member.shieldFor = 0.0f;
			a_member.shieldMisses = 0;
			a_member.shieldFacing = nearest->GetHandle();
			Log::Info("[Taunt] {} raises the shield: {} shooting at them, the nearest {} {:.0f} away."sv, a_member.name, Count(shooters),
				Game::Name(nearest.get()), nearestAt);
		}

		// --- Taunt ---------------------------------------------------------

		void Taunt(Party::Member& a_member, RE::Actor* a_tank, const Orders& a_orders, const PathsLink::Kit& a_kit, float a_dt)
		{
			const auto& settings = Settings::Get();
			const auto& tuning = settings.tuning;
			a_member.inCombatFor += a_dt;
			auto refuse = [&](std::string_view a_why) {
				if (a_member.tauntRequested) {
					a_member.tauntRequested = false;
					Answer(a_member, std::move(a_why));
				}
			};
			if (!settings.taunt && !PathsLink::DebugCasting(a_member.id)) {
				refuse(Tr("Taunts are switched off (Tanks settings)."));
				return;
			}
			if (a_member.tauntReady > 0.0f && !PathsLink::DebugCasting(a_member.id)) {
				refuse(Trf("Not yet: ready in {:.0f} s.", std::ceil(a_member.tauntReady)));
				return;
			}
			if (Busy(a_tank)) {
				if (a_member.tauntRequested && WaitedTooLong(a_member)) {
					refuse(Tr("Could not break off to taunt."));
				}
				return;
			}


            if (const auto block=ResourceBlock(a_tank,Paths::Ability::kTaunt); !block.empty()) { refuse(block); return; }
			if (!a_member.tauntRequested && !AbilityAuto::Allow(a_member, a_tank, Paths::Ability::kTaunt)) return;

			const auto preview = PreviewTaunt(a_tank);
			const auto numbers = NumbersFor(a_tank, a_kit);
			const auto tankHandle = a_tank->GetHandle();
			const int  tankLevel = a_tank->GetLevel();

			struct Candidate
			{
				RE::NiPointer<RE::Actor> actor;
				Enemies::Profile*        profile;
			};
			std::vector<Candidate> candidates;
			bool                   peel = false;
			const auto playerHandle = RE::PlayerCharacter::GetSingleton()->GetHandle();
			int        drawn = 0;  // archers and casters on the player among the candidates
			for (auto& profile : Enemies::All()) {
				auto enemy = profile.handle.get();
				// An archer or caster on the player is drawn from much further: their fire goes to
				// the tank, whose style is built to block it, instead of the player.
				const bool  shootingPlayer = (profile.archer || profile.caster) && profile.target == playerHandle;
				const float reach = a_member.tauntRequested ? (shootingPlayer ? std::max(preview.radius, tuning.tauntRangedReach) : preview.radius) : std::min(preview.radius, 900.0f);
				if (!enemy || !Game::Usable(enemy.get()) || Game::Distance(a_tank, enemy.get()) > reach) {
					continue;
				}
				// Only ever an enemy: never anyone in the party, nor anyone fighting at your side.
				if (Party::IsPartyActor(enemy.get()) || enemy->IsPlayerTeammate()) {
					continue;
				}
				// Already ours with time to spare: not worth a taunt on its own.
				bool fresh = true;
				for (const auto& hold : holds) {
					if (hold.enemy == profile.handle && hold.tank == tankHandle &&
						hold.remaining > 0.5f * HoldFor(tankLevel, profile.level, numbers, a_kit)) {
						fresh = false;
					}
				}
				if (!fresh) {
					continue;
				}
				bool unused = false;
				if (!a_tank->HasLineOfSight(enemy.get(), unused)) {
					continue;
				}
				// Someone else in the party is taking this enemy's attention.
				if (auto victim = profile.target.get(); victim && victim.get() != a_tank && Party::IsPartyActor(victim.get())) {
					peel = true;
				}
				candidates.push_back({ enemy, &profile });
				drawn += shootingPlayer ? 1 : 0;
			}
			if (candidates.empty() && !PathsLink::DebugCasting(a_member.id)) {
				a_member.tauntPending = false;
				refuse(Trf("No enemy in sight within {:.0f}.", preview.radius));
				return;
			}

			if (!a_member.tauntPending) {
				const bool opener = settings.tauntOpener && !a_member.opened && a_member.inCombatFor < 4.0f;
				const bool wanted = a_member.tauntRequested || (PathsLink::AutoOn(a_orders, a_kit, Paths::Ability::kTaunt) && (peel || opener));
				if (!wanted) {
					return;
				}
				// Taunting the moment the cooldown ends reads as a loop. A ready tank weighs it
				// up every few seconds instead, so the challenges land at uneven moments.
				if (!a_member.tauntRequested) {
					if (a_member.tauntRollIn > 0.0f) {
						return;
					}
					a_member.tauntRollIn = settings.tauntCheckSeconds;
					if (std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) > settings.tauntChance) {
						return;
					}
				}
				a_member.tauntPending = true;
				a_member.tauntPendingFor = 0.0f;
			}

			// The challenge lands with its animation: tried again for a moment if the game
			// refuses it, and only taken without one once that moment has passed.
			const bool animated = Challenge(a_tank);
			if (!animated && a_member.tauntPendingFor < kTauntWait && !PathsLink::DebugCasting(a_member.id)) {
				return;
			}
			a_member.tauntPending = false;
			const auto& forms = Forms::Get();
			if (settings.glowSeconds > 0.0f) {
				Effects::Flash(a_tank, Effects::Look::kTaunt, settings.glowSeconds);
			}
			if (!TauntBears::Call(a_tank, a_kit.Rank(Paths::Ability::kTaunt) >= 3)) {
				Play(forms.tauntSound, a_tank);
			}
			Used(a_member.id, a_tank, Paths::Ability::kTaunt, a_member.tauntRequested);
			const float braced = BraceTank(a_tank, numbers, a_kit);

			int         heldCount = 0;
			std::string shrugged;
			std::uniform_real_distribution<float> roll(0.0f, 1.0f);
			for (auto& candidate : candidates) {
				const auto& profile = *candidate.profile;
				const float chance = Rules::TauntChance(tankLevel, profile.level, profile.boss, tuning);
				if (chance <= 0.0f || roll(rng) > chance) {
					if (shrugged.empty()) {
						shrugged = profile.name;
					}
					continue;
				}
				const float seconds = HoldFor(tankLevel, profile.level, numbers, a_kit);
				bool        refreshed = false;
				for (auto& hold : holds) {
					if (hold.enemy == profile.handle) {
						hold.tank = tankHandle;
						hold.remaining = std::max(hold.remaining, seconds);
						refreshed = true;
					}
				}
				if (!refreshed) {
					holds.push_back({ profile.handle, tankHandle, seconds });
				}
				if (numbers.intimidate > 0.0f) {
					Lift(profile.id, RE::ActorValue::kAttackDamageMult, -numbers.intimidate, seconds, static_cast<std::uint16_t>(Paths::Perk::kTauntIntimidate));
				}
				++heldCount;
			}

			const bool ordered = a_member.tauntRequested;
			if (ordered) {
				Answer(a_member, heldCount > 0 ? Trf("Taunted: {} turned on them.", CountSaid(heldCount)) :
				                                 Trf("Taunted, but {} shrugged it off.", shrugged.empty() ? std::string(Tr("everyone")) : shrugged),
					heldCount > 0);
			}
			a_member.tauntCooldown = numbers.cooldown;
			a_member.tauntReady = a_member.tauntCooldown;
			a_member.opened = true;
			a_member.tauntRequested = false;

			const auto  turned = Trn(heldCount, "{} enemy turns on them", "{} enemies turn on them");
			std::string line = Trf("{} taunts: {}.", a_member.name, turned);
			if (drawn > 0 && heldCount > 0) {
				line = Trf("{} draws the fire: {}.", a_member.name, turned);
			}
			if (!shrugged.empty()) {
				line += Trf(" {} shrugs it off.", shrugged);
			}
			if (braced > 0.0f) {
				line += Trf(" Braced: +{:.0f} armor.", braced);
			}
			Notices::Used(line, Paths::Ability::kTaunt, Notices::Kind::kTaunt);
			Log::Info("[Taunt] {} (Lv {}) radius {:.0f}: held {} of {}, idle {}, why {}, braced +{:.0f} armor for {:.0f} s."sv, a_member.name, tankLevel,
				preview.radius, heldCount, candidates.size(), animated ? "played"sv : "refused"sv,
				ordered ? "ordered"sv : (peel ? "peel"sv : "opener"sv), braced, braced > 0.0f ? numbers.hold : 0.0f);
		}

		void AgeHolds(float a_dt)
		{
			const float breakDistance = Settings::Get().tuning.holdBreakDistance;
			std::erase_if(holds, [&](Hold& a_hold) {
				a_hold.remaining -= a_dt;
				if (a_hold.remaining <= 0.0f) {
					return true;
				}
				auto enemy = a_hold.enemy.get();
				auto tank = a_hold.tank.get();
				if (!enemy || !tank || enemy->IsDead() || !Game::Usable(tank.get())) {
					return true;
				}
				return tank->AsActorState()->IsBleedingOut() || Hidden(tank.get()) || Game::Distance(tank.get(), enemy.get()) > breakDistance;
			});
			for (auto& member : Party::Members()) {
				member.held = 0;
			}
			for (const auto& hold : holds) {
				for (auto& member : Party::Members()) {
					if (member.handle == hold.tank) {
						++member.held;
					}
				}
			}
		}

		// --- Heal: a circle of healing light on the ground --------------------

		// Knocked about too badly to do anything: staggered, thrown, or in a kill move.
		bool Reeling(RE::Actor* a_actor)
		{
			return a_actor->AsActorState()->IsStaggered() || a_actor->IsInRagdollState() || a_actor->IsInKillMove();
		}

		// The game's own magic art on both of someone's hands.
		void BothHands(RE::Actor* a_actor, RE::BGSArtObject* a_art, float a_seconds)
		{
			auto* model = a_actor->Get3D();
			if (!a_art || !model) {
				return;
			}
			for (const auto* node : { "NPC L MagicNode [LMag]", "NPC R MagicNode [RMag]" }) {
				if (auto* hand = model->GetObjectByName(node)) {
					a_actor->ApplyArtObject(a_art, a_seconds, nullptr, false, false, hand);
				}
			}
		}

		// What a healer's or mage's ability looks like: a gesture with magic in hand, the colour in
		// both hands, the sound, and a column of sparks round them that a smaller one answers on
		// everyone it reaches. Green for a heal, blue for Rejuvenate.
		void ShowCast(RE::Actor* a_caster, std::span<const RE::NiPointer<RE::Actor>> a_reached, RE::BGSArtObject* a_hands,
			RE::BGSSoundDescriptorForm* a_sound, Effects::Look a_aura)
		{
			if (!Settings::Get().abilityEffects) {
				return;
			}
			CastGesture(a_caster);
			BothHands(a_caster, a_hands, 2.0f);
			Play(a_sound, a_caster);
			Effects::Flash(a_caster, a_aura);
			for (const auto& actor : a_reached) {
				if (actor.get() != a_caster) {
					Effects::Flash(actor.get(), a_aura, 2.2f);
				}
			}
		}

		// Everyone a healer's heal reaches: you, the companions and their summons, alive and
		// within healReach of the healer.
		std::vector<RE::NiPointer<RE::Actor>> Team(RE::Actor* a_healer, float a_reach)
		{
			std::vector<RE::NiPointer<RE::Actor>> team;
			auto add = [&](RE::NiPointer<RE::Actor> a_actor) {
				if (AbilityAuto::Visible(a_healer, a_actor.get(), a_reach)) {
					team.push_back(std::move(a_actor));
				}
			};
			add(RE::NiPointer<RE::Actor>(RE::PlayerCharacter::GetSingleton()));
			for (auto& member : Party::Members()) {
				add(member.handle.get());
			}
			return team;
		}

		// The healer's circle spends the shared ranked magicka cost on activation. A
		// circle of light raised on the ground where it helps most (Circles): everyone of the party
		// in it heals a little every moment, the undead in it burn, and hurt companions walk into it.
		// Without a path it gives a set amount over its life, more as the healer rises in level; on
		// one, a share of each one's own health a second, more with each rank. On their own, they
		// raise it once anyone is down or under their line.
		void Heal(Party::Member& a_member, RE::Actor* a_healer, const Orders& a_orders, const PathsLink::Kit& a_kit)
		{
			const auto& settings = Settings::Get();
			const auto& tuning = settings.tuning;
			auto        refuse = [&](std::string_view a_why) {
                if (a_member.healRequested) {
                    a_member.healRequested = false;
                    Answer(a_member, std::move(a_why));
                }
			};
			if (!settings.heal && !PathsLink::DebugCasting(a_member.id)) {
				refuse(Tr("Healing is switched off (Healers settings)."));
				return;
			}
			if (a_member.healReady > 0.0f && !PathsLink::DebugCasting(a_member.id)) {
				refuse(Trf("Not yet: ready in {:.0f} s.", std::ceil(a_member.healReady)));
				return;
			}
			if (a_healer->AsActorState()->IsBleedingOut()) {
				refuse(Tr("Down themselves."));
				return;
			}
			if (Reeling(a_healer)) {
				if (a_member.healRequested && WaitedTooLong(a_member)) {
					refuse(Tr("Knocked about; could not."));
				}
				return;
			}
			if (!PathsLink::AutoOn(a_orders, a_kit, Paths::Ability::kHeal) && !a_member.healRequested) {
				return;
			}
			if (Circles::Standing(a_member.id)) {
				refuse(Tr("Their circle still stands."));
				return;
			}

            if (const auto block=ResourceBlock(a_healer,Paths::Ability::kHeal); !block.empty()) { refuse(block); return; }
			if (!a_member.healRequested && !AbilityAuto::Allow(a_member, a_healer, Paths::Ability::kHeal)) return;
			const auto team = Team(a_healer, a_member.healRequested ? tuning.healReach : std::min(tuning.healReach, AbilityPolicy::kSupportReach));
			const float line = static_cast<float>(a_orders.healThreshold) / 100.0f;
			float       lowest = 1.0f;
			RE::Actor*  worst = nullptr;
			int         down = 0;
			int         hurt = 0;
			std::vector<HealCircle::Ally> allies;
			allies.reserve(team.size());
			for (const auto& actor : team) {
				const float health = Game::Fraction(actor.get(), RE::ActorValue::kHealth);
				const bool  bleeding = actor->AsActorState()->IsBleedingOut();
				down += bleeding ? 1 : 0;
				hurt += health < 0.95f ? 1 : 0;
				if (health < lowest) {
					lowest = health;
					worst = actor.get();
				}
				const auto at = actor->GetPosition();
				allies.push_back({ at.x, at.y, health, actor->IsPlayerRef(), bleeding });
			}
			if (!a_member.healRequested && down == 0 && lowest >= line) {
				return;
			}
			if (down == 0 && hurt == 0 && !PathsLink::DebugCasting(a_member.id)) {
				refuse(Trf("Nobody within {:.0f} needs healing.", tuning.healReach));
				return;
			}

			Circles::Strength strength;
			strength.seconds = HealCircle::kSeconds;
			strength.radius = HealCircle::kRadius;
			strength.bonus = PathBonus::Healing(a_member.id);
			if (a_kit.tree) {
				const int rank = std::max(a_kit.Rank(Paths::Ability::kHeal), 1);
				strength.healShare = Paths::Value(Paths::Perk::kHeal, rank) / 100.0f;
				strength.burnShare = (0.005f + 0.005f * static_cast<float>(rank));
				strength.staminaShare = a_kit.Value(Paths::Perk::kHealInvigorate) / 100.0f;
				strength.radius += a_kit.Value(Paths::Perk::kHealWide);
				strength.grand = rank >= 3 || a_kit.Rank(Paths::Perk::kHealWide) >= 3;
				strength.triage = a_kit.Value(Paths::Perk::kTriage) / 100.0f;
			} else {
				strength.healFlat = Rules::TeamHealAmount(a_healer->GetLevel(), tuning) / HealCircle::kSeconds;
				strength.burnFlat = strength.healFlat * 0.4f;
			}
			const int at = HealCircle::Place(allies, strength.radius);
			auto*     on = at >= 0 ? team[static_cast<std::size_t>(at)].get() : a_healer;
			if (!Circles::Raise(a_member.id, a_member.name, on, strength)) {
				refuse(Tr("Could not raise the circle here."));
				return;
			}
			ShowCast(a_healer, {}, Forms::Get().handsGreen, Forms::Get().healSound, Effects::Look::kHealAura);
			Used(a_member.id, a_healer, Paths::Ability::kHeal, a_member.healRequested);
			a_member.healReady = a_kit.tree ? a_kit.Cooldown(Paths::Ability::kHeal) : tuning.healCooldown;
			const auto where = on->IsPlayerRef() ? std::string(Tr("you")) : (on == a_healer ? std::string(Tr("themselves")) : Game::Name(on));
			Notices::Used(Trf("{} raises a healing circle under {}.", a_member.name, where), Paths::Ability::kHeal, Notices::Kind::kHeal);
			Log::Info("[Heal] {} raises the circle ({} down; lowest {} at {:.0f}%){}."sv, a_member.name, down,
				worst ? (worst->IsPlayerRef() ? "you"s : Game::Name(worst)) : "nobody"s, lowest * 100.0f, a_member.healRequested ? ", as ordered" : "");
			if (a_member.healRequested) {
				a_member.healRequested = false;
				Answer(a_member, Trf("Healing circle under {}.", where), true);
			}
		}

		// --- Rejuvenate: bounded restoration inside an anchored circle ------

		// Direct magicka restoration works even while casting. Allies must stay in the circle,
		// which lasts longer with rank. On their own, they use it once you or a caster in
		// the party has run dry or is close to it.
		void Rejuvenate(Party::Member& a_member, RE::Actor* a_healer, const Orders& a_orders, const PathsLink::Kit& a_kit)
		{
			const auto& settings = Settings::Get();
			const auto& tuning = settings.tuning;
			auto        refuse = [&](std::string_view a_why) {
                if (a_member.rejuvenateRequested) {
                    a_member.rejuvenateRequested = false;
                    Answer(a_member, std::move(a_why));
                }
			};
			if (!settings.rejuvenate && !PathsLink::DebugCasting(a_member.id)) {
				refuse(Tr("Rejuvenate is switched off (Healers settings)."));
				return;
			}
			if (a_member.rejuvenateReady > 0.0f && !PathsLink::DebugCasting(a_member.id)) {
				refuse(Trf("Not yet: ready in {:.0f} s.", std::ceil(a_member.rejuvenateReady)));
				return;
			}
			if (a_healer->AsActorState()->IsBleedingOut()) {
				refuse(Tr("Down themselves."));
				return;
			}
			if (Reeling(a_healer)) {
				if (a_member.rejuvenateRequested && WaitedTooLong(a_member)) {
					refuse(Tr("Knocked about; could not."));
				}
				return;
			}
			if (!PathsLink::AutoOn(a_orders, a_kit, Paths::Ability::kRejuvenate) && !a_member.rejuvenateRequested) {
				return;
			}

            if (const auto block=ResourceBlock(a_healer,Paths::Ability::kRejuvenate); !block.empty()) { refuse(block); return; }
			if (!a_member.rejuvenateRequested && !AbilityAuto::Allow(a_member, a_healer, Paths::Ability::kRejuvenate)) return;
			const int lookRank = a_kit.tree ? std::max(a_kit.Rank(Paths::Ability::kRejuvenate), 1) : (a_healer->GetLevel() >= 60 ? 3 : a_healer->GetLevel() >= 30 ? 2 : 1);
			const float reach = RejuvenatePolicy::Radius(lookRank);
			std::vector<RE::NiPointer<RE::Actor>> reached;
			bool                                  needed = false;
			bool                                  anyShort = false;
			std::string                           dry;
			// You first: low on magicka yourself is as good a reason as a companion running dry.
			if (auto* player = RE::PlayerCharacter::GetSingleton(); AbilityAuto::Visible(a_healer, player, reach)) {
				const float yours = Game::Fraction(player, RE::ActorValue::kMagicka);
				anyShort = yours < 0.95f;
				if (yours < 0.3f && RejuvenatedFor(player->GetFormID()) <= 0.0f) {
					needed = true;
					dry = "you";
				}
				reached.emplace_back(player);
			}
			for (const auto& member : Party::Members()) {
				auto actor = !member.CanTakeRole() ? RE::NiPointer<RE::Actor>{} : member.handle.get();
				if (!AbilityAuto::Visible(a_healer, actor.get(), reach)) {
					continue;
				}
				const auto* orders = Party::FindOrders(member.id);
				const bool  caster = (orders && ChoosesSpells(orders->role)) || member.arsenal.casts;
				const float magicka = Game::Fraction(actor.get(), RE::ActorValue::kMagicka);
				anyShort = anyShort || magicka < 0.95f;
				if (caster && (member.drained || magicka < 0.3f) && RejuvenatedFor(member.id) <= 0.0f) {
					needed = true;
					if (dry.empty()) {
						dry = member.name;
					}
				}
				reached.push_back(std::move(actor));
			}
			if (!a_member.rejuvenateRequested && !needed) {
				return;
			}
			if (!anyShort && !PathsLink::DebugCasting(a_member.id)) {
				refuse(Tr("Everyone's magicka is full."));
				return;
			}

			const int   level = a_healer->GetLevel();
			const float seconds = a_kit.tree ? Paths::Value(Paths::Perk::kRejuvenate, std::max(a_kit.Rank(Paths::Ability::kRejuvenate), 1)) :
			                                   Rules::RejuvenateSeconds(level, tuning);
			const float wellspring = a_kit.Value(Paths::Perk::kRejuvenateWellspring) / 100.0f;
			const float vigor = a_kit.Value(Paths::Perk::kRejuvenateVigor) / 100.0f;
			for (const auto& actor : reached) {
				if (wellspring > 0.0f && RejuvenateField::On(actor.get()).left <= 0) {
					const float give = RejuvenatePolicy::Restore(Game::Maximum(actor.get(), RE::ActorValue::kMagicka), Game::Current(actor.get(), RE::ActorValue::kMagicka), wellspring, 1.0f, 30.0f);
					actor->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kMagicka, give);
				}
			}
			RejuvenateField::Begin(a_healer, lookRank, seconds, vigor);
			ShowCast(a_healer, reached, Forms::Get().handsBlue, Forms::Get().rejuvenateSound, Effects::Look::kRejuvenateAura);
			Rejuvenations::Begin(a_healer, lookRank, seconds);
			Used(a_member.id, a_healer, Paths::Ability::kRejuvenate, a_member.rejuvenateRequested);
			a_member.rejuvenateCooldown = a_kit.tree ? a_kit.Cooldown(Paths::Ability::kRejuvenate) : Rules::RejuvenateCooldown(level, tuning);
			a_member.rejuvenateReady = a_member.rejuvenateCooldown;
			Notices::Used(Trf("{} raises Rejuvenate: {:g}% magicka per second for {:.0f} s.", a_member.name, RejuvenatePolicy::Rate(lookRank)*100, seconds),
				Paths::Ability::kRejuvenate, Notices::Kind::kHeal);
			Log::Info("[Rejuvenate] {} raises rank {} circle: ranked magicka restoration, radius {:.0f}, {:.0f} s, cooldown {:.0f} s{}."sv, a_member.name, lookRank, reach,
				seconds, a_member.rejuvenateCooldown,
				a_member.rejuvenateRequested ? ", as ordered"s : (dry.empty() ? ""s : std::format(" ({} ran low)", dry)));
			if (a_member.rejuvenateRequested) {
				a_member.rejuvenateRequested = false;
				Answer(a_member, Trf("Rejuvenate: {:g}% magicka per second inside the circle for {:.0f} s.", RejuvenatePolicy::Rate(lookRank)*100, seconds), true);
			}
		}
	}

	void DebugActivate(Party::Member& member, Role role, Paths::Ability ability, int rank)
	{
		if (!member.CanTakeRole() || !PathsLink::Unlimited(member.id)) { Party::Answer(member, Tr("Enable Debug Mode first.")); return; }
		const int index=static_cast<std::size_t>(role)<Paths::kRoles ? Paths::IndexOf(role,ability) : -1;
		if (index<0 || rank<1 || rank>3 || ability==Paths::Ability::kNone) { Party::Answer(member,Tr("Invalid ability rank.")); return; }
		auto actor=member.handle.get();
		if (!actor || !Game::Usable(actor.get()) || !actor->Get3D()) { Party::Answer(member,Tr("Companion unavailable.")); return; }
		if (Busy(actor.get()) || DragonBreaths::Channeling(actor.get())) { Party::Answer(member,Tr("Companion is busy.")); return; }
		auto record=Paths::Fresh();
        if(Skald::IsSong(ability)) {
            const auto* actual=PathsLink::Find(member.id);
            const auto* own=Party::FindOrders(member.id);
            if(!Skald::HasLute(actor.get())){Party::Answer(member,Tr("Requires a lute in this companion inventory."));return;}
            if(!actual || !actual->record.skaldUnlocked){Party::Answer(member,Tr("Unlock the Skald tree with 10 earned skill points first."));return;}
            if(!own || own->role!=Role::kSkald){Party::Answer(member,Tr("Assign the Skald role before playing a Ljodh."));return;}
            record.skaldUnlocked=true;
        }
		record.ranks[static_cast<std::size_t>(role)][static_cast<std::size_t>(index)]=static_cast<std::uint8_t>(rank);
		PathsLink::Kit kit; kit.role=role; kit.tree=true; kit.record=&record; kit.slots[0]=ability;
		const PathsLink::ScopedDebugCast debug(member.id,kit);
		Orders orders; if (const auto* saved=Party::FindOrders(member.id)) orders=*saved; orders.role=role;
		// Test requests and cooldowns never replace the player's queued orders or cooldowns.
		const auto before=member;
		switch(ability) {
		case Paths::Ability::kTaunt: member.tauntRequested=true; Taunt(member,actor.get(),orders,kit,0); break;
		case Paths::Ability::kHeal: member.healRequested=true; Heal(member,actor.get(),orders,kit); break;
		case Paths::Ability::kRejuvenate: member.rejuvenateRequested=true; Rejuvenate(member,actor.get(),orders,kit); break;
		default:
			if (Paths::SpecialFor(ability)!=Special::kNone) Specials::DebugActivate(member,actor.get(),kit,ability);
			else Arts::DebugActivate(member,actor.get(),kit,ability);
			break;
		}
		member.tauntRequested=before.tauntRequested; member.healRequested=before.healRequested; member.rejuvenateRequested=before.rejuvenateRequested;
		member.tauntReady=before.tauntReady; member.healReady=before.healReady; member.rejuvenateReady=before.rejuvenateReady;
		member.tauntPending=before.tauntPending; member.tauntPendingFor=before.tauntPendingFor;
		member.rejuvenateCooldown=before.rejuvenateCooldown;
		Log::Info("[Debug] {} requested {} rank {}."sv,member.name,Paths::Info(ability).name,rank);
	}

	void Update(float a_dt)
	{
		AgeHolds(a_dt);
		for (auto& member : Party::Members()) {
			// The cooldowns run in Party::Cool, on world time, fight or not.
			member.tauntRollIn = std::max(member.tauntRollIn - a_dt, 0.0f);
			if (member.tauntPending) {
				member.tauntPendingFor += a_dt;
			}
			if (!member.CanTakeRole()) {
				continue;
			}
			auto actor = member.handle.get();
			if (!actor || !Game::Usable(actor.get()) || !actor->IsInCombat()) {
				member.inCombatFor = 0.0f;
				member.opened = false;
				member.tauntPending = false;
				LowerShield(member, actor.get());
				DropBlock(member, actor.get());
				continue;
			}
			const auto* orders = Party::FindOrders(member.id);
			if (!orders) {
				continue;
			}
			if (orders->role != Role::kTank) {
				LowerShield(member, actor.get(), "no longer the tank"sv);
			}
			// What they carry: on a path, the two abilities slotted there; without one, the
			// role's own. A request for something not carried is answered at once.
			const auto kit = PathsLink::KitOf(member.id, orders->role);
			auto       notCarried = [&](bool& a_requested, Paths::Ability a_ability) {
                if (a_requested && !kit.Carries(a_ability)) {
                    a_requested = false;
                    Answer(member, Trf("{} is not one of the abilities they carry.", Paths::Info(a_ability).name));
                }
			};
			notCarried(member.tauntRequested, Paths::Ability::kTaunt);
			notCarried(member.healRequested, Paths::Ability::kHeal);
			notCarried(member.rejuvenateRequested, Paths::Ability::kRejuvenate);
			if (orders->role == Role::kTank) {
				if (kit.Carries(Paths::Ability::kTaunt)) {
					Taunt(member, actor.get(), *orders, kit, a_dt);
				} else {
					member.inCombatFor += a_dt;
				}
				ShieldWall(member, actor.get(), a_dt);
			} else {
				member.inCombatFor += a_dt;
				if (kit.Carries(Paths::Ability::kHeal)) {
					Heal(member, actor.get(), *orders, kit);
				}
				if (kit.Carries(Paths::Ability::kRejuvenate)) {
					Rejuvenate(member, actor.get(), *orders, kit);
				}
			}
		}
	}

	void BlockIncoming(float a_dt)
	{
		// Every enemy arrow and spell in flight, once a frame.
		struct Flying
		{
			RE::NiPoint3 at;
			RE::NiPoint3 velocity;
			RE::Actor*   target{ nullptr };
			RE::Actor*   shooter{ nullptr };
		};
		std::vector<Flying> flying;
		auto*               manager = RE::Projectile::Manager::GetSingleton();
		if (!Settings::Get().shieldWall || !manager) {
			return;
		}
		// Most parties have no tank: then the projectiles are never walked at all.
		bool watching = false;
		for (const auto& member : Party::Members()) {
			const auto* orders = Party::FindOrders(member.id);
			watching = watching || member.blockRaised || (orders && orders->role == Role::kTank && member.CanTakeRole());
		}
		if (!watching) {
			return;
		}
		// The lists change whenever anyone shoots, so only the handles are copied, under the
		// manager's own lock (the one it takes to add a projectile), and looked up after.
		std::vector<RE::ProjectileHandle> handles;
		{
			RE::BSSpinLockGuard guard(manager->projectileLock);
			handles.reserve(manager->limited.size() + manager->unlimited.size());
			handles.insert(handles.end(), manager->limited.begin(), manager->limited.end());
			handles.insert(handles.end(), manager->unlimited.begin(), manager->unlimited.end());
		}
		for (const auto& handle : handles) {
			auto projectile = handle.get();
			if (!projectile) {
				continue;
			}
			auto&      data = projectile->GetProjectileRuntimeData();
			const auto velocity = data.linearVelocity;
			if (velocity.SqrLength() < 300.0f * 300.0f) {
				continue;  // landed, or stuck in something
			}
			auto  from = data.shooter.get();
			auto* shooter = from ? from->As<RE::Actor>() : nullptr;
			if (!shooter || !Enemies::Find(shooter->GetFormID())) {
				continue;  // not an enemy's
			}
			auto aimed = data.desiredTarget.get();
			flying.push_back({ projectile->GetPosition(), velocity, aimed ? aimed->As<RE::Actor>() : nullptr, shooter });
		}

		for (auto& member : Party::Members()) {
			member.blockHold = std::max(member.blockHold - a_dt, 0.0f);
			auto tank = member.handle.get();
			const auto* orders = Party::FindOrders(member.id);
			if (!tank || !orders || orders->role != Role::kTank || !member.CanTakeRole() || !Game::Usable(tank.get()) || !HasShield(tank.get())) {
				DropBlock(member, tank.get());
				continue;
			}
			// Will one reach them in the next second? Aimed at them, it only has to come close;
			// aimed at someone else, it has to be about to hit them anyway.
			const RE::NiPoint3 chest = tank->GetPosition() + RE::NiPoint3{ 0.0f, 0.0f, 90.0f };
			const Flying*      incoming = nullptr;
			for (const auto& f : flying) {
				const float speed2 = f.velocity.SqrLength();
				const auto  r = chest - f.at;
				const float t = r.Dot(f.velocity) / speed2;
				if (t < 0.0f || t > 1.0f) {
					continue;
				}
				const float miss = (r - f.velocity * t).Length();
				if (miss < (f.target == tank.get() ? 300.0f : 120.0f)) {
					incoming = &f;
					break;
				}
			}
			auto* state = tank->AsActorState();
			const bool able = tank->IsInCombat() && state->IsWeaponDrawn() && !tank->IsAttacking() && !state->IsStaggered() && !state->IsBleedingOut() &&
			                  !tank->IsInRagdollState();
			if (incoming && able) {
				member.blockHold = 0.8f;
				if (!tank->IsBlocking() && member.blockTries < 3) {
					// Turned to meet it: a shield only covers the front.
					const auto  source = incoming->shooter->GetPosition();
					const auto  at = tank->GetPosition();
					const float dx = source.x - at.x, dy = source.y - at.y;
					if (dx * dx + dy * dy > 1.0f) {
						tank->SetHeading(std::atan2(dx, dy));
					}
					tank->NotifyAnimationGraph("blockStart");
					++member.blockTries;
					member.blockRaised = true;
					if (const auto now = ::GetTickCount64(); now - member.blockSaidAt > 5000) {
						member.blockSaidAt = now;
						Log::Info("[Taunt] {} raises the shield against {}'s {} ({:.0f} away)."sv, member.name, Game::Name(incoming->shooter),
							incoming->target == tank.get() ? "shot at them"sv : "stray shot"sv, Game::Distance(tank.get(), incoming->shooter));
					}
				}
			} else if (member.blockRaised && (member.blockHold <= 0.0f || !able)) {
				DropBlock(member, tank.get());  // down again, unless the shield wall holds it up on purpose
			}
			if (tank->IsBlocking() || !member.blockRaised) {
				member.blockTries = 0;
			}
		}
	}

	void Clear()
	{
		holds.clear();
		Unbrace();
		for (auto& member : Party::Members()) {
			auto actor = member.handle.get();
			LowerShield(member, actor.get());
			DropBlock(member, actor.get());
			member.tauntPending = false;
			if (member.tauntRequested || member.healRequested || member.rejuvenateRequested) {
				member.tauntRequested = false;
				member.healRequested = false;
				member.rejuvenateRequested = false;
				Answer(member, Tr("The fight is over."));
			}
		}
	}

	const std::vector<Hold>& Holds()
	{
		return holds;
	}

	RE::ActorHandle HolderOf(const RE::ActorHandle& a_enemy)
	{
		for (const auto& hold : holds) {
			if (hold.enemy == a_enemy) {
				return hold.tank;
			}
		}
		return {};
	}

	TauntPreview PreviewTaunt(RE::Actor* a_tank)
	{
		const auto&  tuning = Settings::Get().tuning;
		TauntPreview p;
		const int    level = a_tank->GetLevel();
		const auto*  orders = Party::FindOrders(a_tank->GetFormID());
		const auto   kit = PathsLink::KitOf(a_tank->GetFormID(), orders ? orders->role : Role::kTank);
		const auto   numbers = NumbersFor(a_tank, kit);
		p.radius = Rules::TauntRadius(Game::Current(a_tank, RE::ActorValue::kBlock), Game::Current(a_tank, RE::ActorValue::kHeavyArmor), tuning);
		p.certainUpTo = level + Rules::TauntReach(level, tuning);
		p.noChanceFrom = p.certainUpTo + tuning.tauntFalloffLevels + 1;
		p.hold = numbers.hold;
		p.cooldown = numbers.cooldown;
		p.stamina = ResourceCost(a_tank, Paths::Ability::kTaunt);
		p.armorSeconds = p.hold;
		p.armor = numbers.armor;
		return p;
	}

	void AddHold(const RE::ActorHandle& a_enemy, const RE::ActorHandle& a_tank, float a_seconds)
	{
		for (auto& hold : holds) {
			if (hold.enemy == a_enemy) {
				hold.tank = a_tank;
				hold.remaining = std::max(hold.remaining, a_seconds);
				return;
			}
		}
		holds.push_back({ a_enemy, a_tank, a_seconds });
	}

	void DropHoldsOn(const RE::ActorHandle& target)
	{
		std::erase_if(holds,[&](const Hold& hold){return hold.tank==target;});
	}

	void Lift(RE::FormID a_id, RE::ActorValue a_value, float a_amount, float a_seconds, std::uint16_t a_source)
	{
		const auto it = std::find_if(boosts.begin(), boosts.end(),
			[&](const Boost& a_boost) { return a_boost.id == a_id && a_boost.value == a_value && a_boost.source == a_source; });
		if (it == boosts.end()) {
			Mod(a_id, a_value, a_amount);
			boosts.push_back({ a_id, a_value, a_amount, a_seconds, a_seconds, a_source });
		} else {
			Mod(a_id, a_value, a_amount - it->amount);
			it->amount = a_amount;
			it->left = a_seconds;
			it->length = a_seconds;
		}
	}

	void Keep(RE::FormID a_id, RE::ActorValue a_value, float a_amount, float a_seconds, std::uint16_t a_source)
	{
		if (a_amount != 0.0f) {
			const auto it = std::find_if(boosts.begin(), boosts.end(),
				[&](const Boost& a_boost) { return a_boost.id == a_id && a_boost.value == a_value && a_boost.source == a_source; });
			if (it != boosts.end() && it->amount == a_amount) {
				it->left = std::max(it->left, a_seconds);  // the same lift kept up: no need to touch the value
				return;
			}
			Lift(a_id, a_value, a_amount, a_seconds, a_source);
			return;
		}
		DropSource(a_source, a_id, a_value);
	}

	void DropSource(std::uint16_t a_source, std::optional<RE::FormID> a_id, std::optional<RE::ActorValue> a_value)
	{
		std::erase_if(boosts, [&](const Boost& a_boost) {
			if (a_boost.source != a_source || (a_id && a_boost.id != *a_id) || (a_value && a_boost.value != *a_value)) {
				return false;
			}
			Mod(a_boost.id, a_boost.value, -a_boost.amount, true);
			return true;
		});
	}

	std::optional<BoostTime> BoostOn(RE::FormID a_id, RE::ActorValue a_value, std::uint16_t a_source)
	{
		for (const auto& boost : boosts) {
			if (boost.id == a_id && boost.value == a_value && boost.source == a_source) {
				return BoostTime{ boost.amount, boost.left, boost.length };
			}
		}
		return std::nullopt;
	}

	std::vector<SavedBoost> Boosts()
	{
		std::vector<SavedBoost> out;
		for (const auto& boost : boosts) {
			out.push_back({ boost.id, static_cast<std::uint32_t>(boost.value), boost.amount });
		}
		return out;
	}

	std::vector<std::pair<std::uint32_t, float>> BoostTimes(RE::ActorValue a_value, std::uint16_t a_source)
	{
		std::vector<std::pair<std::uint32_t, float>> out;
		for (const auto& boost : boosts) {
			if (boost.value == a_value && boost.source == a_source) out.emplace_back(boost.id, boost.left);
		}
		return out;
	}

	void LoadBoosts(const std::vector<SavedBoost>& a_saved)
	{
		for (const auto& saved : a_saved) {
			fromSave.push_back({ saved.id, static_cast<RE::ActorValue>(saved.value), saved.amount, 0.0f, 0.0f, 0 });
		}
	}

	void UpdateBoosts(float a_dt)
	{
		RejuvenateField::Tick(a_dt);
		for (const auto& boost : fromSave) {
			Mod(boost.id, boost.value, -boost.amount, true);
			if (boost.value == RE::ActorValue::kDamageResist) {
				Log::Info("[Taunt] {:08X}: {:+.0f} armor from an ability still on when the game was saved is off again."sv, boost.id, boost.amount);
			} else if (boost.value == RE::ActorValue::kMagickaRateMult) {
				Log::Info("[Heal] {:08X}: magicka quickened by an ability when the game was saved is back to normal."sv, boost.id);
			} else {
				Log::Info("[Paths] {:08X}: {:+.2f} to actor value {} from an ability still on when the game was saved is off again."sv, boost.id, boost.amount,
					static_cast<int>(boost.value));
			}
		}
		fromSave.clear();
		std::erase_if(boosts, [&](Boost& a_boost) {
			a_boost.left -= a_dt;
			if (a_boost.left > 0.0f) {
				return false;
			}
			Mod(a_boost.id, a_boost.value, -a_boost.amount, true);
			return true;
		});
	}

	void Unbrace()
	{
		Drop(RE::ActorValue::kDamageResist);
	}

	void EndBoosts()
	{
		RejuvenateField::Clear();
		Drop(std::nullopt);
		fromSave.clear();
	}

	std::vector<std::pair<std::uint32_t, float>> Braced()
	{
		return Listed(RE::ActorValue::kDamageResist, false);
	}

	std::vector<std::pair<std::uint32_t, float>> BraceTimes()
	{
		return Listed(RE::ActorValue::kDamageResist, true);
	}

	std::vector<std::pair<std::uint32_t, float>> Rejuvenated()
	{
		return Listed(RE::ActorValue::kMagickaRateMult, false);
	}

	std::optional<BoostTime> BoostOn(RE::FormID a_id, RE::ActorValue a_value)
	{
		for (const auto& boost : boosts) {
			if (boost.id == a_id && boost.value == a_value) {
				return BoostTime{ boost.amount, boost.left, boost.length };
			}
		}
		return std::nullopt;
	}

	float RejuvenatedFor(RE::FormID a_id)
	{
		if (const auto field = RejuvenateField::On(RE::TESForm::LookupByID<RE::Actor>(a_id)); field.left > 0) return field.left;
		for (const auto& boost : boosts) {
			if (boost.id == a_id && boost.value == RE::ActorValue::kMagickaRateMult) {
				return boost.left;
			}
		}
		return 0.0f;
	}

	void LoadBraced(const std::vector<std::pair<std::uint32_t, float>>& a_saved)
	{
		for (const auto& [id, amount] : a_saved) {
			fromSave.push_back({ id, RE::ActorValue::kDamageResist, amount, 0.0f, 0.0f });
		}
	}

	void LoadRejuvenated(const std::vector<std::pair<std::uint32_t, float>>& a_saved)
	{
		for (const auto& [id, amount] : a_saved) {
			fromSave.push_back({ id, RE::ActorValue::kMagickaRateMult, amount, 0.0f, 0.0f });
		}
	}
}
