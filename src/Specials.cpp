#include "FWM/Specials.h"

#include "FWM/Abilities.h"
#include "FWM/Enemies.h"
#include "FWM/Forms.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Party.h"
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

		constexpr float kGuardSeconds = 10.0f;
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

		// --- Bodyguard: to your side, a challenge on arrival -----------------

		Result Bodyguard(Party::Member& a_member, RE::Actor* a_tank, bool a_asked)
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
			a_member.guardFor = kGuardSeconds;
			a_member.guardTaunted = false;
			Notices::Push(std::format("{} rushes to your side.", a_member.name), Notices::Kind::kOrder);
			Log::Info("[Special] {} guards you: {} on you, your health {:.0f}%, {:.0f} away."sv, a_member.name, CountOf(on), health * 100.0f,
				Game::Distance(a_tank, player));
			return { true, on > 0 ? "On the way to you." : "At your side; nobody is on you yet." };
		}

		// While guarding: the taunt on arrival, and back to their post once you are safe or the
		// moment has passed.
		void Guard(Party::Member& a_member, RE::Actor* a_tank, float a_dt)
		{
			if (a_member.guardFor <= 0.0f) {
				return;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			a_member.guardFor -= a_dt;
			if (!a_member.guardTaunted && Game::Distance(a_tank, player) < 450.0f) {
				a_member.guardTaunted = true;
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
				if (auto marked = a_member.marked.get(); marked && !marked->IsDead()) {
					return marked;
				}
				if (auto* controller = Game::Controller(a_archer)) {
					if (auto target = controller->targetHandle.get(); target && !target->IsDead()) {
						return target;
					}
				}
				return nullptr;
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

		Result Volley(Party::Member& a_member, RE::Actor* a_archer, bool a_asked)
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
			const auto name = Game::Name(target.get());
			// Five arrows in quick succession from the one who calls it; anyone else at range
			// turns on the same enemy meanwhile.
			a_member.burstTarget = target->GetHandle();
			a_member.burstLeft = 5;
			a_member.burstIn = 0.0f;
			volley = { target->GetHandle(), a_member.id, kVolleySeconds };
			Notices::Push(std::format("Volley! {} looses five arrows at {}.", a_member.name, name), Notices::Kind::kOrder);
			Log::Info("[Special] {} calls a volley at {} (level {}, {:.0f} away)."sv, a_member.name, name, target->GetLevel(),
				Game::Distance(a_archer, target.get()));
			return { true, std::format("Volley: five arrows at {}.", name) };
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

		Result WarCry(Party::Member& a_member, RE::Actor* a_bruiser, bool a_asked)
		{
			std::vector<RE::NiPointer<RE::Actor>> close;
			for (const auto& profile : Enemies::All()) {
				auto enemy = profile.handle.get();
				if (enemy && Game::Usable(enemy.get()) && Game::Distance(a_bruiser, enemy.get()) <= 350.0f) {
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
			const auto& forms = Forms::Get();
			Abilities::PlayIdle(a_bruiser, forms.idleCry);
			Abilities::Play(forms.tauntSound, a_bruiser);
			int                                   staggered = 0;
			std::uniform_real_distribution<float> roll(0.0f, 1.0f);
			for (auto& enemy : close) {
				const auto* profile = Enemies::Find(enemy->GetFormID());
				if (profile && profile->boss && roll(rng) > 0.5f) {
					continue;  // a boss stands it half the time
				}
				// The stagger a heavy blow plays: its strength, then the event.
				enemy->SetGraphVariableFloat("staggerMagnitude", 0.8f);
				if (enemy->NotifyAnimationGraph("staggerStart")) {
					++staggered;
				}
			}
			Notices::Push(std::format("{}'s war cry staggers {}.", a_member.name, CountOf(staggered)), Notices::Kind::kTaunt);
			Log::Info("[Special] {} war cry: {} of {} close by staggered."sv, a_member.name, staggered, close.size());
			return { true, staggered > 0 ? std::format("War cry: {} staggered.", CountOf(staggered)) : "War cry, but nobody staggered."s };
		}

		// --- Ward: alteration on the player ----------------------------------

		Result Ward(Party::Member& a_member, RE::Actor* a_caster, bool a_asked)
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
			// Oakflesh, then Stone, Iron and Ebony as they rise in level.
			const auto&    flesh = Forms::Get().flesh;
			const int      level = a_caster->GetLevel();
			RE::SpellItem* spell = nullptr;
			int            used = level < 15 ? 0 : (level < 30 ? 1 : (level < 45 ? 2 : 3));
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
			// The flesh spells reach only the one who casts them: cast through the player's own
			// hands, on them, with the companion to blame (and to pay).
			auto* caster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
			if (!caster) {
				return Refused("Could not cast on you.");
			}
			caster->CastSpellImmediate(spell, false, player, 1.0f, false, 0.0f, a_caster);
			auto*      effect = spell->effects.empty() || !spell->effects[0] ? nullptr : spell->effects[0]->baseEffect;
			auto*      target = player->AsMagicTarget();
			const bool took = effect && target && target->HasMagicEffect(effect);
			if (!took) {
				Log::Info("[Special] {}'s {} did not take on you."sv, a_member.name, spell->GetName());
				return Refused(std::format("{} did not take on you.", spell->GetName()));
			}
			a_caster->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kMagicka, cost);
			Abilities::HandGlow(a_caster, spell, 1.2f);
			// The skin it gives, plainly: bark, stone, iron or ebony over you for a few seconds.
			if (auto* skin = Forms::Get().fleshShaders[static_cast<std::size_t>(std::clamp(used, 0, 3))]) {
				player->ApplyEffectShader(skin, 4.0f);
			}
			wardLeft = kWardSeconds;
			Notices::Push(std::format("{} wards you: {}.", a_member.name, spell->GetName()), Notices::Kind::kHeal);
			Log::Info("[Special] {} casts {} on you ({:.0f} magicka; {} on you, your health {:.0f}%)."sv, a_member.name, spell->GetName(), cost, CountOf(on),
				health * 100.0f);
			return { true, std::format("{} on you.", spell->GetName()) };
		}

		// --- Disarm: the weapon out of their hands -----------------------------

		// Unequipped and dropped at their feet, enchantment and all, as the Disarm shout does:
		// they must go and pick it up, or draw something else.
		void KnockAway(RE::Actor* a_target, RE::TESObjectWEAP* a_weapon)
		{
			RE::ExtraDataList* worn = nullptr;
			const auto         items = a_target->GetInventory([a_weapon](RE::TESBoundObject& a_object) { return &a_object == a_weapon; });
			for (const auto& [object, entry] : items) {
				if (!entry.second || !entry.second->extraLists) {
					continue;
				}
				for (auto* list : *entry.second->extraLists) {
					if (list && (list->HasType(RE::ExtraDataType::kWorn) || list->HasType(RE::ExtraDataType::kWornLeft))) {
						worn = list;
						break;
					}
				}
			}
			if (auto* equip = RE::ActorEquipManager::GetSingleton()) {
				equip->UnequipObject(a_target, a_weapon, worn, 1, nullptr, false, true, true, true);
			}
			const auto at = a_target->GetPosition() + RE::NiPoint3{ 0.0f, 0.0f, 60.0f };
			a_target->RemoveItem(a_weapon, 1, RE::ITEM_REMOVE_REASON::kDropping, worn, nullptr, &at);
		}

		Result Disarm(Party::Member& a_member, RE::Actor* a_fighter, bool a_asked)
		{
			auto* controller = Game::Controller(a_fighter);
			auto  target = controller ? controller->targetHandle.get() : RE::NiPointer<RE::Actor>{};
			if (!target || !Game::Usable(target.get())) {
				return a_asked ? Refused("Nobody to disarm.") : Result{};
			}
			const auto  name = Game::Name(target.get());
			const float d = Game::Distance(a_fighter, target.get());
			if (d > 450.0f) {
				return a_asked ? Refused(std::format("{} is too far ({:.0f} away).", name, d)) : Result{};
			}
			auto*       right = target->GetEquippedObject(false);
			const auto* weapon = right ? right->As<RE::TESObjectWEAP>() : nullptr;
			if (!weapon || weapon->IsHandToHandMelee() || weapon->IsStaff()) {
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
			// As the shout: up to their own level and ten more; a boss holds on half the time.
			if (target->GetLevel() > a_fighter->GetLevel() + 10) {
				return a_asked ? Refused(std::format("{} is too strong to disarm (level {}).", name, target->GetLevel())) : Result{};
			}
			const auto* profile = Enemies::Find(target->GetFormID());
			if (profile && profile->boss && std::uniform_real_distribution<float>(0.0f, 1.0f)(rng) < 0.5f) {
				Log::Info("[Special] {} goes for {}'s {}, who holds on."sv, a_member.name, name, weapon->GetName());
				return { true, std::format("{} held on to the {}.", name, weapon->GetName()) };
			}
			const std::string what = weapon->GetName();
			KnockAway(target.get(), const_cast<RE::TESObjectWEAP*>(weapon));
			target->SetGraphVariableFloat("staggerMagnitude", 0.35f);
			target->NotifyAnimationGraph("staggerStart");
			Notices::Push(std::format("{} knocks the {} out of {}'s hands.", a_member.name, what, name), Notices::Kind::kTaunt);
			Log::Info("[Special] {} disarms {} (level {}): the {} is on the ground, {:.0f} away."sv, a_member.name, name, target->GetLevel(), what, d);
			return { true, std::format("Disarmed {}: the {} is on the ground.", name, what) };
		}
	}

	float Cooldown(Special a_special)
	{
		return SpecialCooldown(a_special);
	}

	void Update(float a_dt)
	{
		wardLeft = std::max(wardLeft - a_dt, 0.0f);
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
			member.specialReady = std::max(member.specialReady - a_dt, 0.0f);
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
				answer("Only in a fight.");
				continue;
			}
			const auto* orders = Party::FindOrders(member.id);
			if (!orders) {
				continue;
			}
			const auto special = SpecialOf(orders->role);
			if (special != Special::kBodyguard) {
				member.guardFor = 0.0f;
			}
			Guard(member, actor.get(), a_dt);
			if (!asked && (!settings.specials || !orders->autoSpecial)) {
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
				result = Bodyguard(member, actor.get(), asked);
				break;
			case Special::kVolley:
				result = Volley(member, actor.get(), asked);
				break;
			case Special::kWarCry:
				result = WarCry(member, actor.get(), asked);
				break;
			case Special::kWard:
				result = Ward(member, actor.get(), asked);
				break;
			case Special::kDisarm:
				result = Disarm(member, actor.get(), asked);
				break;
			default:
				break;
			}
			answer(result.text.empty() ? "Nothing to do right now."s : result.text, result.done);
			if (result.done) {
				member.specialCooldown = Cooldown(special);
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
			if (!archer || !target || target->IsDead() || !Game::Usable(archer.get()) || Stunned(archer.get())) {
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
	}

	RE::ActorHandle VolleyTarget()
	{
		return volley.remaining > 0.0f ? volley.target : RE::ActorHandle{};
	}
}
