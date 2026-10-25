#include "FWM/Spells.h"
#include "FWM/Loadouts.h"
#include "FWM/AbilityLink.h"

#include "FWM/Abilities.h"
#include "FWM/Enemies.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/Notices.h"
#include "FWM/Overrides.h"
#include "FWM/Party.h"
#include "FWM/Settings.h"
#include "FWM/Text.h"

#ifdef GetObject
#	undef GetObject  // the Windows macro, not the default object lookup
#endif

namespace FWM::Spells
{
	namespace
	{
		constexpr RE::FormID kBothHands = 0x013F45;  // Skyrim.esm BothHands: the master spells and rituals

		RE::BGSEquipSlot* HandSlot(std::size_t a_hand)
		{
			auto* defaults = RE::BGSDefaultObjectManager::GetSingleton();
			return defaults ? defaults->GetObject<RE::BGSEquipSlot>(a_hand == kRightHand ? RE::DEFAULT_OBJECTS::kRightHandEquip : RE::DEFAULT_OBJECTS::kLeftHandEquip) :
			                  nullptr;
		}

		struct Fit
		{
			bool left{ false };
			bool right{ false };
			bool both{ false };  // takes both hands at once
		};

		// The hands a spell can be held in, from its equip slot. Most say "either".
		Fit FitOf(const RE::SpellItem* a_spell)
		{
			const auto* slot = a_spell->GetEquipSlot();
			if (!slot) {
				return { true, true, false };
			}
			if (slot == HandSlot(kLeftHand)) {
				return { true, false, false };
			}
			if (slot == HandSlot(kRightHand)) {
				return { false, true, false };
			}
			if (slot->GetFormID() == kBothHands) {
				return { true, true, true };
			}
			auto* defaults = RE::BGSDefaultObjectManager::GetSingleton();
			if (defaults && slot == defaults->GetObject<RE::BGSEquipSlot>(RE::DEFAULT_OBJECTS::kEitherHandEquip)) {
				return { true, true, false };
			}
			return {};  // the voice, or something no hand can hold
		}

		const RE::EffectSetting* MainEffect(const RE::SpellItem* a_spell)
		{
			const auto* effect = a_spell->GetCostliestEffectItem();
			return effect ? effect->baseEffect : nullptr;
		}

		Rules::SpellTier TierOf(const RE::SpellItem* a_spell)
		{
			const auto* base = MainEffect(a_spell);
			return Rules::TierFromSkill(base ? base->GetMinimumSkillLevel() : 0);
		}

		Rules::School SchoolOf(const RE::SpellItem* a_spell)
		{
			const auto* base = MainEffect(a_spell);
			switch (base ? base->GetMagickSkill() : RE::ActorValue::kNone) {
			case RE::ActorValue::kDestruction:
				return Rules::School::kDestruction;
			case RE::ActorValue::kRestoration:
				return Rules::School::kRestoration;
			case RE::ActorValue::kAlteration:
				return Rules::School::kAlteration;
			case RE::ActorValue::kConjuration:
				return Rules::School::kConjuration;
			case RE::ActorValue::kIllusion:
				return Rules::School::kIllusion;
			default:
				return Rules::School::kOther;
			}
		}

		// A spell cast from the hands: not a power, a shout, an ability or a disease, and named.
		bool Castable(const RE::SpellItem* a_spell)
		{
			if (!a_spell || a_spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell ||
				a_spell->GetCastingType() == RE::MagicSystem::CastingType::kConstantEffect || a_spell->effects.empty() || !MainEffect(a_spell)) {
				return false;
			}
			const char* name = a_spell->GetName();
			const auto  fit = FitOf(a_spell);
			return name && *name && (fit.left || fit.right);
		}

		bool Knows(RE::Actor* a_companion, const RE::SpellItem* a_spell)
		{
			const auto spells = Game::SpellsOf(a_companion);
			return std::find(spells.begin(), spells.end(), a_spell) != spells.end();
		}

		// Meant to hurt or hinder whoever it lands on: any hostile effect.
		bool Hostile(const RE::SpellItem* a_spell)
		{
			return std::any_of(a_spell->effects.begin(), a_spell->effects.end(),
				[](const RE::Effect* a_effect) { return a_effect && a_effect->baseEffect && a_effect->baseEffect->IsHostile(); });
		}

		bool RoleAllows(const RE::SpellItem* a_spell, RE::Actor* a_companion, Role a_role)
		{
			return Rules::SpellAllowed(a_role, Game::IsHeal(a_spell), Hostile(a_spell), TierOf(a_spell), a_companion->GetLevel(), Settings::Get().healersAnySpell);
		}

		View::Spell ViewOf(RE::SpellItem* a_spell, RE::Actor* a_companion, bool a_known)
		{
			View::Spell v;
			const auto  fit = FitOf(a_spell);
			v.id = a_spell->GetFormID();
			v.name = a_spell->GetName();
			v.school = SchoolOf(a_spell);
			v.tier = TierOf(a_spell);
			v.cost = a_spell->CalculateMagickaCost(a_companion);
			v.known = a_known;
			v.healing = Game::IsHeal(a_spell);
			v.left = fit.left;
			v.right = fit.right;
			v.twoHanded = fit.both;
			return v;
		}

		std::string_view HandName(std::size_t a_hand) { return a_hand == kRightHand ? Tr("right hand") : Tr("left hand"); }
	}

	std::vector<View::Spell> OptionsFor(RE::Actor* a_companion, Role a_role)
	{
		std::vector<View::Spell> out;
		if (!a_companion || !ChoosesSpells(a_role)) {
			return out;
		}
		std::unordered_set<RE::FormID> seen;
		auto add = [&](RE::SpellItem* a_spell, bool a_known) {
			if (Castable(a_spell) && (AbilityLink::LoadoutMask(a_role) || RoleAllows(a_spell, a_companion, a_role)) && seen.insert(a_spell->GetFormID()).second) {
				auto option = ViewOf(a_spell, a_companion, a_known);
				option.allowed = RoleAllows(a_spell, a_companion, a_role);
				out.push_back(std::move(option));
			}
		};
		for (auto* spell : Game::SpellsOf(a_companion)) {
			add(spell, true);
		}
		for (auto* spell : Game::SpellsOf(RE::PlayerCharacter::GetSingleton())) {
			add(spell, false);
		}
		std::stable_sort(out.begin(), out.end(), [](const View::Spell& a, const View::Spell& b) {
			if (a.known != b.known) {
				return a.known;
			}
			if (a.school != b.school) {
				return a.school < b.school;
			}
			if (a.tier != b.tier) {
				return a.tier < b.tier;
			}
			return a.name < b.name;
		});
		return out;
	}

	View::Spell Describe(RE::FormID a_spell, RE::Actor* a_companion)
	{
		auto* spell = a_spell ? RE::TESForm::LookupByID<RE::SpellItem>(a_spell) : nullptr;
		return spell && a_companion ? ViewOf(spell, a_companion, Knows(a_companion, spell)) : View::Spell{};
	}

    std::array<std::vector<RE::FormID>,2> Automatic(RE::Actor* actor, Role role)
    {
        std::array<std::vector<RE::FormID>,2> pools;
        if (!actor || !ChoosesSpells(role)) return pools;
        for (auto* spell : Game::SpellsOf(actor)) {
            if (!Castable(spell) || !RoleAllows(spell,actor,role)) continue;
            const auto fit=FitOf(spell);
            if(fit.left)pools[0].push_back(spell->GetFormID());
            if(fit.right)pools[1].push_back(spell->GetFormID());
        }
        for(auto& pool:pools){std::sort(pool.begin(),pool.end());pool.erase(std::unique(pool.begin(),pool.end()),pool.end());}
        return pools;
    }

	bool Allowed(RE::FormID a_spell, RE::Actor* a_companion, Role a_role)
	{
		auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_spell);
		return spell && a_companion && Castable(spell) && RoleAllows(spell, a_companion, a_role);
	}

	Outcome Choose(RE::Actor* a_companion, Role a_role, std::size_t a_hand, RE::FormID a_spell)
	{
		const auto id = a_companion->GetFormID();
		const auto name = Game::Name(a_companion);
		auto       hands = Loadouts::Hands(id);
		const auto other = a_hand == kLeftHand ? kRightHand : kLeftHand;
		auto       twoHanded = [](RE::FormID a_id) {
            auto* spell = a_id ? RE::TESForm::LookupByID<RE::SpellItem>(a_id) : nullptr;
            return spell && FitOf(spell).both;
		};

		if (!a_spell) {
			// A spell that took both hands leaves both.
			if (hands[a_hand] && hands[a_hand] == hands[other] && twoHanded(hands[a_hand])) {
				hands[other] = 0;
			}
			hands[a_hand] = 0;
			Party::SetHands(id, hands);
			Log::Info("[Spells] {}: the {} is back to their own choice."sv, name, HandName(a_hand));
			return { true, Trf("Their {} is theirs to choose again.", HandName(a_hand)) };
		}

		auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_spell);
		if (!spell || !Castable(spell)) {
			return { false, std::string(Tr("That is not a spell for the hands.")) };
		}
		if (!RoleAllows(spell, a_companion, a_role)) {
			return { false, a_role == Role::kMage ? Trf("A Mage leaves {} to a healer.", spell->GetName()) :
			                                         Trf("{} is beyond a Healer of level {}.", spell->GetName(), a_companion->GetLevel()) };
		}
		const auto fit = FitOf(spell);
		if (!fit.both && !(a_hand == kLeftHand ? fit.left : fit.right)) {
			return { false, Trf("{} is cast from the {} only.", spell->GetName(), HandName(other)) };
		}

		// One of yours they do not know yet: taught first.
		bool taught = false;
		if (!Knows(a_companion, spell)) {
			if (!Knows(RE::PlayerCharacter::GetSingleton(), spell) || !a_companion->AddSpell(spell)) {
				return { false, Trf("Could not teach {} {}.", name, spell->GetName()) };
			}
			taught = true;
			Notices::Push(Trf("You teach {} {}.", name, spell->GetName()), Notices::Kind::kInfo);
		}

		if (fit.both) {
			hands = { a_spell, a_spell };
		} else {
			if (twoHanded(hands[other])) {
				hands[other] = 0;  // a two-handed spell cannot keep the other hand
			}
			hands[a_hand] = a_spell;
		}
		Party::SetHands(id, hands);

		// In hand now, so the sheet and the next fight both start from it.
		auto* equip = RE::ActorEquipManager::GetSingleton();
		auto* slot = fit.both ? spell->GetEquipSlot() : HandSlot(a_hand);
		if (equip && slot) {
			equip->EquipSpell(a_companion, spell, slot);
		}
		Log::Info("[Spells] {}{} {} in the {} ({}, {})."sv, name, taught ? " learns" : " holds", spell->GetName(), fit.both ? "both hands"sv : HandName(a_hand),
			Rules::tierNames[static_cast<std::size_t>(TierOf(spell))], Rules::schoolNames[static_cast<std::size_t>(SchoolOf(spell))]);
		return { true, taught ? Trf("Taught {}; it is in their {} now.", spell->GetName(), fit.both ? Tr("hands") : HandName(a_hand)) :
		                        Trf("{} in their {}.", spell->GetName(), fit.both ? Tr("hands") : HandName(a_hand)) };
	}

	bool Conjures(RE::FormID a_spell)
	{
		const auto* spell = a_spell ? RE::TESForm::LookupByID<RE::SpellItem>(a_spell) : nullptr;
		return spell && std::any_of(spell->effects.begin(), spell->effects.end(), [](const RE::Effect* a_effect) {
			return a_effect && a_effect->baseEffect && a_effect->baseEffect->GetArchetype() == RE::EffectArchetypes::ArchetypeID::kBoundWeapon;
		});
	}

	std::string EquipChosen(RE::Actor* a_companion)
	{
		auto*       equip = RE::ActorEquipManager::GetSingleton();
		const auto  hands = a_companion ? Loadouts::Hands(a_companion->GetFormID()) : Hands{};
		std::string names;
		for (const auto hand : { kLeftHand, kRightHand }) {
			auto* spell = hands[hand] ? RE::TESForm::LookupByID<RE::SpellItem>(hands[hand]) : nullptr;
			if (!spell || !equip || (hand == kRightHand && hands[kRightHand] == hands[kLeftHand])) {
				continue;
			}
			const auto fit = FitOf(spell);
			if (auto* slot = fit.both ? spell->GetEquipSlot() : HandSlot(hand)) {
				equip->EquipSpell(a_companion, spell, slot);
				names += std::format("{}{}", names.empty() ? "" : " and ", spell->GetName());
			}
		}
		return names;
	}

	void Recheck(RE::Actor* a_companion, Role a_role)
	{
        Loadouts::Recheck(a_companion,a_role);
		if (!a_companion || !ChoosesSpells(a_role)) {
			return;  // kept for when they are a mage or healer again
		}
		const auto id = a_companion->GetFormID();
		auto       hands = Loadouts::Hands(id);
		bool       changed = false;
		for (const auto hand : { kLeftHand, kRightHand }) {
			if (hands[hand] && !Allowed(hands[hand], a_companion, a_role)) {
				auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(hands[hand]);
				Log::Info("[Spells] {}: {} is not for a {}; the {} is theirs to choose again."sv, Game::Name(a_companion), spell ? spell->GetName() : "a spell",
					Info(a_role).name, HandName(hand));
				hands[hand] = 0;
				changed = true;
			}
		}
		if (changed) {
			Party::SetHands(id, hands);
		}
	}

	namespace
	{
		// Seconds without a chosen spell cast before one is cast for them: a short pause while the
		// combat AI has cast none of them itself this fight, a longer one once it has shown it will.
		constexpr float kIdleWhileAiSilent = 1.2f;
		constexpr float kIdleOnceAiCasts = 3.0f;

		// How a chosen spell is cast for them.
		enum class Use : std::uint8_t
		{
			kNone,        // left to the combat AI: concentration, summons, spells for someone else
			kAtTarget,    // aimed, or at a target or a spot: the enemy they fight, in sight
			kTouch,       // a touch: the enemy they fight, within arm's reach
			kAroundSelf,  // cast on themselves to hurt those around (an explosion at their feet)
			kKeepUp,      // a ward, armor, a cloak, any lasting good on themselves: when it runs out
			kHealSelf     // a heal on themselves: when hurt
		};

		Use UseOf(const RE::SpellItem* a_spell)
		{
			if (a_spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration) {
				return Use::kNone;
			}
			const bool hostile = Hostile(a_spell);
			switch (a_spell->GetDelivery()) {
			case RE::MagicSystem::Delivery::kSelf:
				if (hostile) {
					return Use::kAroundSelf;
				}
				if (Game::IsHeal(a_spell)) {
					return Use::kHealSelf;
				}
				{
					const auto* main = a_spell->GetCostliestEffectItem();
					return main && main->effectItem.duration > 0 ? Use::kKeepUp : Use::kNone;
				}
			case RE::MagicSystem::Delivery::kTouch:
				return hostile ? Use::kTouch : Use::kNone;
			case RE::MagicSystem::Delivery::kAimed:
			case RE::MagicSystem::Delivery::kTargetActor:
			case RE::MagicSystem::Delivery::kTargetLocation:
				return hostile ? Use::kAtTarget : Use::kNone;
			default:
				return Use::kNone;
			}
		}

		// How far a spell cast on oneself reaches: its explosion's radius, else its area.
		float ReachAround(const RE::SpellItem* a_spell)
		{
			float reach = 0.0f;
			for (const auto* effect : a_spell->effects) {
				if (!effect || !effect->baseEffect) {
					continue;
				}
				if (const auto* blast = effect->baseEffect->data.explosion) {
					reach = std::max(reach, blast->data.radius);
				}
				reach = std::max(reach, static_cast<float>(effect->effectItem.area) * 21.33f);  // an area is in feet
			}
			return std::clamp(reach > 0.0f ? reach : 400.0f, 200.0f, 1200.0f);
		}

		float NearestEnemy(RE::Actor* a_actor)
		{
			float nearest = std::numeric_limits<float>::max();
			for (const auto& profile : Enemies::All()) {
				if (auto enemy = profile.handle.get(); enemy && Game::Usable(enemy.get())) {
					nearest = std::min(nearest, Game::Distance(a_actor, enemy.get()));
				}
			}
			return nearest;
		}

		std::string_view HowCast(Use a_use)
		{
			switch (a_use) {
			case Use::kAtTarget:
				return "at their enemy"sv;
			case Use::kTouch:
				return "on the enemy in reach"sv;
			case Use::kAroundSelf:
				return "around themselves"sv;
			case Use::kKeepUp:
				return "on themselves, as it had run out"sv;
			case Use::kHealSelf:
				return "to heal themselves"sv;
			default:
				return ""sv;
			}
		}
	}

	void Drive(Party::Member& a_member, RE::Actor* a_actor, RE::Actor* a_target, float a_dt)
	{
		for (auto& ready : a_member.spellReady) {
			ready = std::max(ready - a_dt, 0.0f);
		}
		const auto pools = Loadouts::Effective(a_member.id);
        auto hands = Loadouts::Hands(a_member.id);
		if (!a_actor || hands == Hands{}) {
			a_member.spellIdle = 0.0f;
			return;
		}
		// The combat AI casting one of them itself: nothing to do for a while.
		for (const auto hand : { kLeftHand, kRightHand }) for(const auto selected:pools[hand]) {
			auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(selected);
			if (spell && a_actor->IsCasting(spell)) {
				a_member.spellIdle = 0.0f;
				a_member.spellsByAi = true;
				return;
			}
		}
		// A staff or an automatically chosen spell can be busy outside the selected pools.
		// Let the native cast finish before considering a fallback spell.
		for (const auto source : { RE::MagicSystem::CastingSource::kLeftHand, RE::MagicSystem::CastingSource::kRightHand }) {
			const auto* caster = a_actor->GetMagicCaster(source);
			if (caster && (caster->state == RE::MagicCaster::State::kCharging || caster->state == RE::MagicCaster::State::kCasting)) {
				a_member.spellIdle = 0.0f;
				return;
			}
		}
		a_member.spellIdle += a_dt;
		auto* state = a_actor->AsActorState();
		if (a_member.spellIdle < (a_member.spellsByAi ? kIdleOnceAiCasts : kIdleWhileAiSilent) || state->IsStaggered() || state->IsBleedingOut() || a_actor->IsInRagdollState() ||
			a_actor->IsInKillMove() || a_actor->IsAttacking()) {
			return;
		}

		const bool  targetLive = a_target && Game::Usable(a_target) && !Party::IsPartyActor(a_target);
		const float health = Game::Fraction(a_actor, RE::ActorValue::kHealth);
		const float magicka = Game::Current(a_actor, RE::ActorValue::kMagicka);
		bool        unused = false;
		const bool  inSight = targetLive && a_actor->HasLineOfSight(a_target, unused);
		const float toTarget = targetLive ? Game::Distance(a_actor, a_target) : std::numeric_limits<float>::max();
		const float nearest = NearestEnemy(a_actor);

		// The best fit now: a heal when badly hurt, then a blow at the enemy, then what has run out.
		std::size_t best = 2;
		Use         bestUse = Use::kNone;
		int bestRank = 0;
        RE::FormID bestSpell=0;
		// Only a spell the combat AI never took up: one it has, it casts from the hand itself.
		const auto seen = Overrides::SeenHands(Game::Native(a_member.handle));
		if (!seen || a_member.inCombatFor < 4.0f) {
			return;
		}
		for (const auto hand : { kRightHand, kLeftHand }) for(const auto selected:pools[hand]) {
			if (a_member.spellReady[hand] > 0.0f) {
				continue;
			}
			if (selected==hands[hand] && (*seen)[hand] != 0) {
				continue;
			}
			auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(selected);
			if (!spell || spell->CalculateMagickaCost(a_actor) > magicka) {
				continue;
			}
			const auto use = UseOf(spell);
			int        rank = 0;
			switch (use) {
			case Use::kHealSelf:
				rank = health < 0.4f ? 4 : 0;
				break;
			case Use::kAtTarget:
				rank = inSight && toTarget <= 3500.0f ? 3 : 0;
				break;
			case Use::kTouch:
				rank = inSight && toTarget <= 180.0f ? 3 : 0;
				break;
			case Use::kAroundSelf:
				rank = nearest <= ReachAround(spell) * 0.9f ? 3 : 0;
				break;
			case Use::kKeepUp:
				{
					const auto* main = spell->GetCostliestEffectItem();
					auto*       target = a_actor->AsMagicTarget();
					rank = main && main->baseEffect && target && !target->HasMagicEffect(main->baseEffect) && nearest <= 3000.0f ? 2 : 0;
				}
				break;
			default:
				break;
			}
			if (rank > bestRank) {
				best = hand;
				bestUse = use;
				bestRank = rank;
                bestSpell=selected;
			}
		}
		if (bestRank == 0) {
			return;
		}

		auto*      spell = RE::TESForm::LookupByID<RE::SpellItem>(bestSpell);
		const bool atEnemy = bestUse == Use::kAtTarget || bestUse == Use::kTouch;
		RE::Actor* on = atEnemy ? a_target : a_actor;
		// From the hand that holds it, turned to the target and with the target set as the
		// caster's own, so an aimed spell leaves the hand toward them.
		auto* caster = a_actor->GetMagicCaster(best == kLeftHand ? RE::MagicSystem::CastingSource::kLeftHand : RE::MagicSystem::CastingSource::kRightHand);
		if (!spell || !caster || !on) {
			return;
		}
		if (atEnemy) {
			const auto to = a_target->GetPosition() - a_actor->GetPosition();
			a_actor->SetHeading(std::atan2(to.x, to.y));
			caster->desiredTarget = a_target->CreateRefHandle();
		}
		const float cost = spell->CalculateMagickaCost(a_actor);
		Abilities::CastGesture(a_actor);
		Abilities::HandGlow(a_actor, spell, 0.8f, best);
		caster->CastSpellImmediate(spell, false, on, 1.0f, false, 0.0f, a_actor);
		a_actor->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kMagicka, cost);
		a_member.spellReady[best] = std::max(1.5f, spell->GetChargeTime() + 1.0f);
		++a_member.spellsDriven;
		if (!a_member.spellTold[best]) {
			a_member.spellTold[best] = true;
			Log::Info("[Spells] {} casts {} {} ({:.0f} magicka); the combat AI had cast none of their chosen spells for {:.1f} s."sv, a_member.name,
				spell->GetName(), HowCast(bestUse), cost, a_member.spellIdle);
		}
		a_member.spellIdle = 0.0f;
	}
}
