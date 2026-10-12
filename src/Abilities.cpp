#include "FWM/Abilities.h"

#include "FWM/Enemies.h"
#include "FWM/Forms.h"
#include "FWM/Game.h"
#include "FWM/Leash.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Rules.h"
#include "FWM/Settings.h"

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

	// The game refuses an idle mid-swing or when its conditions fail; the graph event the
	// idle names often still plays.
	bool PlayIdle(RE::Actor* a_actor, RE::TESIdleForm* a_idle)
	{
		if (!a_actor || !a_idle) {
			return false;
		}
		if (auto* process = a_actor->GetActorRuntimeData().currentProcess; process && process->PlayIdle(a_actor, a_idle, nullptr)) {
			return true;
		}
		return !a_idle->animEventName.empty() && a_actor->NotifyAnimationGraph(a_idle->animEventName);
	}

	void HandGlow(RE::Actor* a_caster, RE::SpellItem* a_spell, float a_seconds)
	{
		if (!a_caster || !a_spell || a_spell->effects.empty() || !a_spell->effects[0] || !a_spell->effects[0]->baseEffect) {
			return;
		}
		if (auto* art = a_spell->effects[0]->baseEffect->data.castingArt) {
			auto* model = a_caster->Get3D();
			auto* hand = model ? model->GetObjectByName("NPC L MagicNode [LMag]") : nullptr;
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

		RE::TESIdleForm* TauntIdle(RE::Actor* a_tank)
		{
			const auto& forms = Forms::Get();
			// Skyrim's own shield bang (CombatIdleShield) is the human cousin of the draugr's;
			// without a shield, the raised-weapon war cry.
			if (Game::LoadoutOf(a_tank) == Game::Loadout::kShield && forms.idleShield) {
				return forms.idleShield;
			}
			return forms.idleCry ? forms.idleCry : forms.idleWeapon;
		}

		// The game refuses an idle mid-swing or when its conditions fail; the graph event the
		// idle names often still plays.
		bool PlayTauntAnimation(RE::Actor* a_tank)
		{
			auto* idle = TauntIdle(a_tank);
			if (!idle) {
				return false;
			}
			if (auto* process = a_tank->GetActorRuntimeData().currentProcess; process && process->PlayIdle(a_tank, idle, nullptr)) {
				return true;
			}
			return !idle->animEventName.empty() && a_tank->NotifyAnimationGraph(idle->animEventName);
		}

		constexpr float kTauntWait = 1.2f;  // seconds a taunt waits for its animation

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
			if (a_tank) {
				a_tank->NotifyAnimationGraph("blockStop");
			}
			if (!a_why.empty()) {
				Log::Info("[Taunt] {} lowers the shield: {}."sv, a_member.name, a_why);
			}
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

		void Taunt(Party::Member& a_member, RE::Actor* a_tank, const Orders& a_orders, float a_dt)
		{
			const auto& settings = Settings::Get();
			const auto& tuning = settings.tuning;
			a_member.inCombatFor += a_dt;
			auto refuse = [&](std::string a_why) {
				if (a_member.tauntRequested) {
					a_member.tauntRequested = false;
					Answer(a_member, std::move(a_why));
				}
			};
			if (!settings.taunt) {
				refuse("Taunts are switched off (Tanks settings).");
				return;
			}
			if (a_member.tauntReady > 0.0f) {
				refuse(std::format("Not yet: ready in {:.0f} s.", std::ceil(a_member.tauntReady)));
				return;
			}
			if (Busy(a_tank)) {
				if (a_member.tauntRequested && WaitedTooLong(a_member)) {
					refuse("Could not break off to taunt.");
				}
				return;
			}
			if (Game::Current(a_tank, RE::ActorValue::kStamina) < tuning.tauntStamina) {
				refuse(std::format("Too tired: a taunt takes {:.0f} stamina.", tuning.tauntStamina));
				return;
			}

			const auto preview = PreviewTaunt(a_tank);
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
				const float reach = shootingPlayer ? std::max(preview.radius, tuning.tauntRangedReach) : preview.radius;
				if (!enemy || !Game::Usable(enemy.get()) || Game::Distance(a_tank, enemy.get()) > reach) {
					continue;
				}
				// Already ours with time to spare: not worth a taunt on its own.
				bool fresh = true;
				for (const auto& hold : holds) {
					if (hold.enemy == profile.handle && hold.tank == tankHandle &&
						hold.remaining > 0.5f * Rules::HoldSeconds(tankLevel, profile.level, tuning)) {
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
			if (candidates.empty()) {
				a_member.tauntPending = false;
				refuse(std::format("No enemy in sight within {:.0f}.", preview.radius));
				return;
			}

			if (!a_member.tauntPending) {
				const bool opener = settings.tauntOpener && !a_member.opened && a_member.inCombatFor < 4.0f;
				const bool wanted = a_member.tauntRequested || (a_orders.autoAbility && (peel || opener));
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
			const bool animated = PlayTauntAnimation(a_tank);
			if (!animated && a_member.tauntPendingFor < kTauntWait) {
				return;
			}
			a_member.tauntPending = false;
			const auto& forms = Forms::Get();
			if (forms.glow && settings.glowSeconds > 0.0f) {
				a_tank->ApplyEffectShader(forms.glow, settings.glowSeconds);
			}
			Play(forms.tauntSound, a_tank);
			a_tank->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kStamina, tuning.tauntStamina);

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
				const float seconds = Rules::HoldSeconds(tankLevel, profile.level, tuning);
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
				++heldCount;
			}

			const bool ordered = a_member.tauntRequested;
			if (ordered) {
				Answer(a_member, heldCount > 0 ? std::format("Taunted: {} turned on them.", Count(heldCount)) :
				                                 std::format("Taunted, but {} shrugged it off.", shrugged.empty() ? "everyone"s : shrugged),
					heldCount > 0);
			}
			a_member.tauntCooldown = Rules::TauntCooldown(tankLevel, tuning);
			a_member.tauntReady = a_member.tauntCooldown;
			a_member.opened = true;
			a_member.tauntRequested = false;

			std::string line = std::format("{} taunts: {} {}.", a_member.name, Count(heldCount), heldCount == 1 ? "turns on them" : "turn on them");
			if (drawn > 0 && heldCount > 0) {
				line = std::format("{} draws the fire: {} {}.", a_member.name, Count(heldCount), heldCount == 1 ? "turns on them" : "turn on them");
			}
			if (!shrugged.empty()) {
				line += std::format(" {} shrugs it off.", shrugged);
			}
			Notices::Push(std::move(line), Notices::Kind::kTaunt);
			Log::Info("[Taunt] {} (Lv {}) radius {:.0f}: held {} of {}, idle {}, why {}."sv, a_member.name, tankLevel,
				preview.radius, heldCount, candidates.size(), animated ? "played"sv : "refused"sv,
				ordered ? "ordered"sv : (peel ? "peel"sv : "opener"sv));
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

		// --- Heal ----------------------------------------------------------

		struct Cast
		{
			RE::SpellItem* spell{ nullptr };
			float          effectiveness{ 1.0f };
			float          cost{ 0.0f };
			float          amount{ 0.0f };
			bool           borrowed{ false };
		};

		float Reach(const RE::SpellItem* a_spell)
		{
			if (a_spell->GetDelivery() != RE::MagicSystem::Delivery::kSelf) {
				return std::numeric_limits<float>::max();
			}
			std::uint32_t area = 0;
			for (const auto* effect : a_spell->effects) {
				area = std::max(area, effect->effectItem.area);
			}
			return std::max(static_cast<float>(area) * kUnitsPerFoot, 150.0f);
		}

		std::optional<Cast> ChooseHeal(RE::Actor* a_healer, RE::Actor* a_patient, float a_distance)
		{
			const auto& settings = Settings::Get();
			const float seconds = settings.concentrationSeconds;
			auto        cost = [&](const RE::SpellItem* a_spell) {
                const float base = a_spell->CalculateMagickaCost(a_healer);
                return a_spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration ? base * seconds : base;
			};
			auto effectiveness = [&](const RE::SpellItem* a_spell) {
				return a_spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration ? seconds : 1.0f;
			};

			std::optional<Cast> best;
			for (auto* spell : Game::MagicOf(a_healer, a_patient).heals) {
				if (a_distance > Reach(spell)) {
					continue;
				}
				const float amount = Game::HealAmount(spell, seconds);
				if (!best || amount > best->amount) {
					best = Cast{ spell, effectiveness(spell), cost(spell), amount, false };
				}
			}
			if (!best && settings.healFallback && Game::HealsOthers(Forms::Get().fallbackHeal, a_healer, a_patient)) {
				auto*       spell = Forms::Get().fallbackHeal;
				const float skill = Game::Current(a_healer, RE::ActorValue::kRestoration);
				const float scale = 0.5f + std::clamp(skill, 0.0f, 100.0f) / 100.0f;
				best = Cast{ spell, effectiveness(spell) * scale, cost(spell), Game::HealAmount(spell, seconds) * scale, true };
			}
			return best;
		}

		std::string PatientName(RE::Actor* a_healer, RE::Actor* a_patient)
		{
			return a_patient == a_healer ? std::string("themselves") : (a_patient->IsPlayerRef() ? std::string("you") : Game::Name(a_patient));
		}

		// The heal straight from the instant caster, without a hand or an animation: for a
		// borrowed spell, and when a cast by hand does not land.
		void HealDirectly(Party::Member& a_member, RE::Actor* a_healer, RE::Actor* a_patient, RE::SpellItem* a_spell, float a_effectiveness, float a_cost)
		{
			auto* caster = a_healer->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
			if (!caster) {
				return;
			}
			// A self-centred burst heals everyone around the healer, the patient included.
			auto* aim = a_spell->GetDelivery() == RE::MagicSystem::Delivery::kSelf ? a_healer : a_patient;
			caster->CastSpellImmediate(a_spell, false, aim, a_effectiveness, false, 0.0f, a_healer);
			a_healer->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kMagicka, a_cost);
			// The spell's own casting glow on the left hand, so even a heal without the
			// animation is seen to come from the healer.
			if (!a_spell->effects.empty() && a_spell->effects[0] && a_spell->effects[0]->baseEffect) {
				if (auto* art = a_spell->effects[0]->baseEffect->data.castingArt) {
					auto* model = a_healer->Get3D();
					auto* hand = model ? model->GetObjectByName("NPC L MagicNode [LMag]") : nullptr;
					a_healer->ApplyArtObject(art, 1.2f, nullptr, false, false, hand);
				}
			}
			Notices::Push(std::format("{} heals {}.", a_member.name, PatientName(a_healer, a_patient)), Notices::Kind::kHeal);
		}

		void EndCast(Party::Member& a_member, RE::Actor* a_healer)
		{
			if (a_member.castSpell && a_healer) {
				a_healer->SetGraphVariableBool("bWantCastLeft", false);
			}
			a_member.castSpell = nullptr;
			a_member.castPatient = {};
			a_member.castPhase = 0;
		}

		void Face(RE::Actor* a_actor, RE::Actor* a_toward)
		{
			const auto from = a_actor->GetPosition();
			const auto to = a_toward->GetPosition();
			const float dx = to.x - from.x, dy = to.y - from.y;
			if (dx * dx + dy * dy > 1.0f) {
				a_actor->SetHeading(std::atan2(dx, dy));
			}
		}

		// A heal cast the way the healer casts anything, in three steps: the spell goes into
		// the left hand; once it is there the hand's caster selects it, aims at the patient
		// and asks for the cast (the engine's own path, which plays the animation); when the
		// charge is done it is let go. The healer turns to the patient throughout.
		bool StartCast(Party::Member& a_member, RE::Actor* a_healer, RE::Actor* a_patient, RE::SpellItem* a_spell, float a_effectiveness, float a_cost,
			float a_expected)
		{
			auto* hand = a_healer->GetMagicCaster(RE::MagicSystem::CastingSource::kLeftHand);
			auto* equip = RE::ActorEquipManager::GetSingleton();
			auto* defaults = RE::BGSDefaultObjectManager::GetSingleton();
			auto* slot = defaults ? defaults->GetObject<RE::BGSEquipSlot>(RE::DEFAULT_OBJECTS::kLeftHandEquip) : nullptr;
			if (!hand || !equip || !slot) {
				return false;
			}
			if (a_healer->GetEquippedObject(true) != a_spell) {
				equip->EquipSpell(a_healer, a_spell, slot);
			}
			a_member.castPatient = a_patient->GetHandle();
			a_member.castSpell = a_spell;
			a_member.castElapsed = 0.0f;
			a_member.castPhase = 0;
			a_member.castPhaseFor = 0.0f;
			a_member.castFurthest = 0;
			a_member.castHealthAt = Game::Fraction(a_patient, RE::ActorValue::kHealth);
			a_member.castExpected = a_expected;
			a_member.castEffectiveness = a_effectiveness;
			a_member.castCost = a_cost;
			return true;
		}

		void ContinueCast(Party::Member& a_member, RE::Actor* a_healer, float a_dt, float a_concentrationSeconds)
		{
			a_member.castElapsed += a_dt;
			a_member.castPhaseFor += a_dt;
			auto  patient = a_member.castPatient.get();
			auto* spell = a_member.castSpell;
			auto* hand = a_healer->GetMagicCaster(RE::MagicSystem::CastingSource::kLeftHand);
			if (!patient || patient->IsDead() || !hand) {
				EndCast(a_member, a_healer);
				return;
			}
			const auto who = PatientName(a_healer, patient.get());
			auto       giveUp = [&](std::string_view a_why) {
                HealDirectly(a_member, a_healer, patient.get(), spell, a_member.castEffectiveness, a_member.castCost);
                Log::Info("[Heal] {}'s cast of {} at {} {} (caster state reached {}); healed directly."sv, a_member.name, spell->GetName(), who, a_why,
                    a_member.castFurthest);
                if (++a_member.castFailures == 2) {
                    Log::Info("[Heal] {}'s hands will not cast here; healing directly for the rest of this fight."sv, a_member.name);
                }
                EndCast(a_member, a_healer);
			};
			a_member.castFurthest = std::max(a_member.castFurthest, static_cast<int>(hand->state.underlying()));

			switch (a_member.castPhase) {
			case 0:  // waiting for the spell to be in hand
				if (a_healer->GetEquippedObject(true) == spell) {
					// A spell swapped in for a bow or a two-hander is not drawn yet.
					a_healer->DrawWeaponMagicHands(true);
					hand->SetCurrentSpell(spell);
					hand->desiredTarget = patient->CreateRefHandle();
					Face(a_healer, patient.get());
					hand->RequestCastImpl();
					a_member.castPhase = 1;
					a_member.castPhaseFor = 0.0f;
				} else if (a_member.castPhaseFor > 1.0f) {
					giveUp("could not equip it");
				}
				return;
			case 1:  // charging: keep aimed and facing, let go once it is ready
			{
				hand->desiredTarget = patient->CreateRefHandle();
				Face(a_healer, patient.get());
				const bool concentration = spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration;
				const float hold = spell->GetChargeTime() + (concentration ? a_concentrationSeconds : 0.25f);
				const bool  ready = hand->state.underlying() >= static_cast<std::uint32_t>(RE::MagicCaster::State::kReady);
				a_healer->SetGraphVariableBool("bWantCastLeft", true);
				if (a_member.castPhaseFor > 1.0f && a_member.castFurthest < static_cast<int>(RE::MagicCaster::State::kUnk02)) {
					a_healer->SetGraphVariableBool("bWantCastLeft", false);
					giveUp("never began to charge");
				} else if ((ready && !concentration) || a_member.castPhaseFor > hold) {
					a_healer->SetGraphVariableBool("bWantCastLeft", false);
					a_member.castPhase = 2;
					a_member.castPhaseFor = 0.0f;
				} else if (a_member.castPhaseFor > hold + 1.0f) {
					giveUp("never charged");
				}
				return;
			}
			default:  // released: did it land?
			{
				// Landed means a real share of the heal, not a point or two of regeneration.
				const float health = Game::Fraction(patient.get(), RE::ActorValue::kHealth);
				const float room = 1.0f - a_member.castHealthAt;
				const float wanted = std::max(0.04f, std::min(a_member.castExpected, room) * 0.5f);
				if (health >= a_member.castHealthAt + wanted && a_member.castFurthest < static_cast<int>(RE::MagicCaster::State::kUnk02)) {
					Log::Info("[Heal] {} recovered on their own ({:.0f}% to {:.0f}%) while {} was casting; no heal needed."sv, who,
						a_member.castHealthAt * 100.0f, health * 100.0f, a_member.name);
					EndCast(a_member, a_healer);
				} else if (health >= a_member.castHealthAt + wanted) {
					Notices::Push(std::format("{} heals {}.", a_member.name, who), Notices::Kind::kHeal);
					Log::Info("[Heal] {} casts {} at {} by hand ({:.0f}% to {:.0f}%, caster state reached {})."sv, a_member.name, spell->GetName(), who,
						a_member.castHealthAt * 100.0f, health * 100.0f, a_member.castFurthest);
					a_member.castFailures = 0;
					EndCast(a_member, a_healer);
				} else if (a_member.castPhaseFor > 1.0f) {
					giveUp("did not land");
				}
				return;
			}
			}
		}

		// How long a healer keeps trying to reach a downed companion before healing from
		// where they are, if that is close enough, or leaving it.
		constexpr float kReviveGiveUp = 8.0f;

		// Healers turn back from a walk below this, and do not start one under kReviveFit.
		constexpr float kReviveFit = 0.5f;
		constexpr float kReviveTurnBack = 0.35f;

		void StopReviving(Party::Member& a_member, std::string_view a_why = {})
		{
			if (a_member.reviving && !a_why.empty()) {
				auto patient = a_member.reviving.get();
				Log::Info("[Heal] {} stops going to {}: {}."sv, a_member.name, patient ? Game::Name(patient.get()) : std::string("them"), a_why);
			}
			a_member.reviving = {};
			a_member.revivingFor = 0.0f;
		}

		// Another healer already going to (or casting on) this downed companion.
		bool Tended(const Party::Member& a_self, RE::Actor* a_patient)
		{
			const auto handle = a_patient->GetHandle();
			for (const auto& other : Party::Members()) {
				if (&other != &a_self && (other.reviving == handle || (other.castSpell && other.castPatient == handle))) {
					return true;
				}
			}
			return false;
		}

		// The heal itself: the best spell for the distance, then by hand, or directly when
		// the hand cannot. False when there is no spell or not the magicka for it.
		bool Treat(Party::Member& a_member, RE::Actor* a_healer, RE::Actor* a_patient, float a_distance, float a_health)
		{
			const auto& settings = Settings::Get();
			const auto  cast = ChooseHeal(a_healer, a_patient, a_distance);
			if (!cast || Game::Current(a_healer, RE::ActorValue::kMagicka) < cast->cost) {
				return false;
			}
			a_member.healReady = settings.tuning.healCooldown;
			Log::Info("[Heal] {} -> {} ({:.0f}% health, {:.0f} away) with {}{}, effectiveness {:.2f}, cost {:.0f}."sv, a_member.name,
				PatientName(a_healer, a_patient), a_health * 100.0f, a_distance, cast->spell->GetName(), cast->borrowed ? " (borrowed)"sv : ""sv,
				cast->effectiveness, cast->cost);
			// A spell of their own is cast from the hand; a borrowed one has no hand to come
			// from, and a spell aimed at others cannot be aimed at the healer themselves.
			const float maxHealth = std::max(Game::Maximum(a_patient, RE::ActorValue::kHealth), 1.0f);
			const bool  aimable = a_patient != a_healer || cast->spell->GetDelivery() == RE::MagicSystem::Delivery::kSelf;
			if (cast->borrowed || !aimable || a_member.castFailures >= 2 ||
				!StartCast(a_member, a_healer, a_patient, cast->spell, cast->effectiveness, cast->cost, cast->amount / maxHealth)) {
				HealDirectly(a_member, a_healer, a_patient, cast->spell, cast->effectiveness, cast->cost);
			}
			return true;
		}

		void Heal(Party::Member& a_member, RE::Actor* a_healer, const Orders& a_orders, float a_dt)
		{
			if (a_member.castSpell) {
				ContinueCast(a_member, a_healer, a_dt, Settings::Get().concentrationSeconds);
				return;
			}
			const auto& settings = Settings::Get();
			const auto& tuning = settings.tuning;
			auto        refuse = [&](std::string a_why) {
				if (a_member.healRequested) {
					a_member.healRequested = false;
					Answer(a_member, std::move(a_why));
				}
			};
			if (!settings.heal) {
				StopReviving(a_member, "healing is switched off");
				refuse("Healing is switched off (Healers settings).");
				return;
			}
			if (a_healer->AsActorState()->IsBleedingOut()) {
				StopReviving(a_member, "they went down on the way");
				refuse("Down themselves.");
				return;
			}
			if (!a_member.reviving) {
				if (a_member.healReady > 0.0f) {
					refuse(std::format("Not yet: ready in {:.0f} s.", std::ceil(a_member.healReady)));
					return;
				}
				if (Busy(a_healer)) {
					if (a_member.healRequested && WaitedTooLong(a_member)) {
						refuse("Could not break off to heal.");
					}
					return;
				}
				if (!a_orders.autoAbility && !a_member.healRequested) {
					return;
				}
			} else if (a_member.healRequested) {
				if (auto patient = a_member.reviving.get()) {
					refuse(std::format("Already on the way to {}.", Game::Name(patient.get())));
				}
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			std::vector<RE::NiPointer<RE::Actor>> allies;
			std::vector<Rules::HealCandidate>     candidates;
			auto                                  add = [&](RE::Actor* a_ally) {
                if (!a_ally || a_ally->IsDead() || !a_ally->Is3DLoaded()) {
                    return;
                }
                // An undead or dwarven ally is no patient for Heal Other; tend to the others.
                if (!ChooseHeal(a_healer, a_ally, 0.0f)) {
                    return;
                }
                Rules::HealCandidate c;
                c.health = Game::Fraction(a_ally, RE::ActorValue::kHealth);
                c.distance = Game::Distance(a_healer, a_ally);
                c.self = a_ally == a_healer;
                c.player = a_ally->IsPlayerRef();
                c.bleeding = a_ally->AsActorState()->IsBleedingOut();
                c.taken = c.bleeding && Tended(a_member, a_ally);
                for (const auto& profile : Enemies::All()) {
                    if (profile.target.get().get() == a_ally) {
                        c.attacked = true;
                        break;
                    }
                }
                allies.emplace_back(a_ally);
                candidates.push_back(c);
			};
			add(player);
			for (auto& member : Party::Members()) {
				add(member.handle.get().get());
			}

			const float threshold = static_cast<float>(a_orders.healThreshold) / 100.0f;
			int         pick = Rules::PickHealTarget(candidates, threshold, tuning);

			// On the way to a downed companion: heal them once within arm's reach. You come
			// first, so needing a heal yourself turns the healer back to you.
			if (a_member.reviving) {
				auto        patient = a_member.reviving.get();
				const bool  you = pick >= 0 && candidates[static_cast<std::size_t>(pick)].player;
				const auto  name = patient ? Game::Name(patient.get()) : std::string("them");
				if (!patient || patient->IsDead() || !patient->AsActorState()->IsBleedingOut() || you) {
					Log::Info("[Heal] {} stops going to {}: {}."sv, a_member.name, name,
						you ? "you need healing first"sv : (patient && !patient->IsDead() ? "they are back up"sv : "they are gone"sv));
					StopReviving(a_member);
				} else if (Game::Fraction(a_healer, RE::ActorValue::kHealth) < kReviveTurnBack) {
					StopReviving(a_member, "too hurt to go on");
				} else {
					a_member.revivingFor += a_dt;
					const float distance = Game::Distance(a_healer, patient.get());
					const bool  close = distance <= tuning.reviveDistance;
					if ((!close && a_member.revivingFor < kReviveGiveUp) || Busy(a_healer)) {
						return;
					}
					StopReviving(a_member);
					if (!close) {
						if (distance > tuning.healRange) {
							Log::Info("[Heal] {} could not reach {} ({:.0f} away after {:.0f} s)."sv, a_member.name, name, distance, kReviveGiveUp);
							a_member.healReady = 3.0f;  // a breath before the next try
							return;
						}
						Log::Info("[Heal] {} could not get closer to {} than {:.0f}; healing from there."sv, a_member.name, name, distance);
					}
					Treat(a_member, a_healer, patient.get(), distance, Game::Fraction(patient.get(), RE::ActorValue::kHealth));
					return;
				}
				if (a_member.healReady > 0.0f || Busy(a_healer)) {
					return;
				}
			}

			if (pick < 0 && a_member.healRequested) {
				// Told to heal: the most hurt ally, whatever the line.
				float lowest = 0.95f;
				for (std::size_t i = 0; i < candidates.size(); ++i) {
					if (candidates[i].health < lowest && candidates[i].distance <= tuning.healRange) {
						lowest = candidates[i].health;
						pick = static_cast<int>(i);
					}
				}
			}
			const bool asked = a_member.healRequested;
			if (pick < 0) {
				refuse(std::format("Nobody within {:.0f} needs healing.", tuning.healRange));
				return;
			}
			a_member.healRequested = false;

			auto*       target = allies[static_cast<std::size_t>(pick)].get();
			const auto& chosen = candidates[static_cast<std::size_t>(pick)];
			if (chosen.bleeding && !chosen.player && !chosen.self && chosen.distance > tuning.reviveDistance) {
				// A companion who is down is gone to and healed up close: a small combat area
				// around them, which the combat AI walks the healer into, weapons still out.
				// Too hurt themselves to cross the field: heal from here if it reaches.
				if (Leash::Available() && Game::Fraction(a_healer, RE::ActorValue::kHealth) >= kReviveFit) {
					const auto cast = ChooseHeal(a_healer, target, tuning.reviveDistance);
					if (!cast || Game::Current(a_healer, RE::ActorValue::kMagicka) < cast->cost) {
						if (asked) {
							Answer(a_member, cast ? std::format("Not enough magicka: {} takes {:.0f}.", cast->spell->GetName(), cast->cost) : "Knows no heal for them."s);
						}
						return;
					}
					a_member.reviving = target->GetHandle();
					a_member.revivingFor = 0.0f;
					Log::Info("[Heal] {} goes to {} (down, {:.0f} away)."sv, a_member.name, Game::Name(target), chosen.distance);
					Notices::Push(std::format("{} runs to {}.", a_member.name, Game::Name(target)), Notices::Kind::kHeal);
					if (asked) {
						Answer(a_member, std::format("Running to {}, who is down.", Game::Name(target)), true);
					}
					return;
				}
				if (chosen.distance > tuning.healRange) {
					if (asked) {
						Answer(a_member, std::format("{} is too far to heal ({:.0f} away).", Game::Name(target), chosen.distance));
					}
					return;  // no way to walk them over, and too far to heal from here
				}
			}
			const bool treated = Treat(a_member, a_healer, target, chosen.distance, chosen.health);
			if (asked) {
				if (treated) {
					Answer(a_member, std::format("Healing {}.", PatientName(a_healer, target)), true);
				} else if (const auto cast = ChooseHeal(a_healer, target, chosen.distance)) {
					Answer(a_member, std::format("Not enough magicka: {} takes {:.0f}.", cast->spell->GetName(), cast->cost));
				} else {
					Answer(a_member, std::format("Knows no heal for {}.", PatientName(a_healer, target)));
				}
			}
		}
	}

	void Update(float a_dt)
	{
		AgeHolds(a_dt);
		for (auto& member : Party::Members()) {
			member.tauntReady = std::max(member.tauntReady - a_dt, 0.0f);
			member.healReady = std::max(member.healReady - a_dt, 0.0f);
			member.tauntRollIn = std::max(member.tauntRollIn - a_dt, 0.0f);
			if (member.tauntPending) {
				member.tauntPendingFor += a_dt;
			}
			if (member.summon) {
				continue;
			}
			auto actor = member.handle.get();
			if (!actor || !Game::Usable(actor.get()) || !actor->IsInCombat()) {
				member.inCombatFor = 0.0f;
				member.opened = false;
				member.tauntPending = false;
				// One more try by hand next fight: a hand that keeps failing goes straight
				// back to direct heals after a single miss.
				member.castFailures = std::min(member.castFailures, 1);
				StopReviving(member, "they left the fight");
				LowerShield(member, actor.get());
				EndCast(member, actor.get());
				continue;
			}
			const auto* orders = Party::FindOrders(member.id);
			if (!orders) {
				continue;
			}
			if (orders->role != Role::kHealer) {
				StopReviving(member, "no longer the healer");  // the leash goes back to you
			}
			if (orders->role != Role::kTank) {
				LowerShield(member, actor.get(), "no longer the tank"sv);
			}
			if (orders->role == Role::kTank) {
				Taunt(member, actor.get(), *orders, a_dt);
				ShieldWall(member, actor.get(), a_dt);
			} else if (orders->role == Role::kHealer) {
				member.inCombatFor += a_dt;
				Heal(member, actor.get(), *orders, a_dt);
			} else {
				member.inCombatFor += a_dt;
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
			watching = watching || member.blockRaised || (orders && orders->role == Role::kTank && !member.summon);
		}
		if (!watching) {
			return;
		}
		auto collect = [&](const RE::BSTArray<RE::ProjectileHandle>& a_list) {
			for (const auto& handle : a_list) {
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
		};
		collect(manager->limited);
		collect(manager->unlimited);

		for (auto& member : Party::Members()) {
			member.blockHold = std::max(member.blockHold - a_dt, 0.0f);
			auto tank = member.handle.get();
			const auto* orders = Party::FindOrders(member.id);
			if (!tank || !orders || orders->role != Role::kTank || member.summon || !Game::Usable(tank.get()) || !HasShield(tank.get())) {
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
			const bool able = !tank->IsAttacking() && !state->IsStaggered() && !state->IsBleedingOut() && !tank->IsInKillMove();
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
				// Down again unless the shield wall is holding it up on purpose.
				if (!member.shielding) {
					tank->NotifyAnimationGraph("blockStop");
				}
				member.blockRaised = false;
			}
			if (tank->IsBlocking() || !member.blockRaised) {
				member.blockTries = 0;
			}
		}
	}

	void Clear()
	{
		holds.clear();
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
		p.radius = Rules::TauntRadius(Game::Current(a_tank, RE::ActorValue::kBlock), Game::Current(a_tank, RE::ActorValue::kHeavyArmor), tuning);
		p.certainUpTo = level + Rules::TauntReach(level, tuning);
		p.noChanceFrom = p.certainUpTo + tuning.tauntFalloffLevels + 1;
		p.hold = Rules::HoldSeconds(level, level, tuning);
		p.cooldown = Rules::TauntCooldown(level, tuning);
		p.stamina = tuning.tauntStamina;
		return p;
	}

	std::optional<HealPreview> PreviewHeal(RE::Actor* a_healer)
	{
		const auto cast = ChooseHeal(a_healer, RE::PlayerCharacter::GetSingleton(), 0.0f);
		if (!cast) {
			return std::nullopt;
		}
		return HealPreview{ cast->spell->GetName(), cast->borrowed, cast->amount, cast->cost };
	}
}
