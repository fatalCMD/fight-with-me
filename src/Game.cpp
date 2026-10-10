#include "FWM/Game.h"

#include "FWM/Forms.h"

namespace FWM::Game
{
	std::string Name(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return {};
		}
		const char* name = a_actor->GetDisplayFullName();
		if (!name || !*name) {
			name = a_actor->GetName();
		}
		return name && *name ? std::string(name) : std::format("{:08X}", a_actor->GetFormID());
	}

	float Maximum(RE::Actor* a_actor, RE::ActorValue a_value)
	{
		if (!a_actor) {
			return 0.0f;
		}
		auto* values = a_actor->AsActorValueOwner();
		return values->GetPermanentActorValue(a_value) +
		       a_actor->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, a_value);
	}

	float Current(RE::Actor* a_actor, RE::ActorValue a_value)
	{
		return a_actor ? a_actor->AsActorValueOwner()->GetActorValue(a_value) : 0.0f;
	}

	float Fraction(RE::Actor* a_actor, RE::ActorValue a_value)
	{
		const float max = Maximum(a_actor, a_value);
		return max > 0.0f ? std::clamp(Current(a_actor, a_value) / max, 0.0f, 1.0f) : 0.0f;
	}

	float Distance(const RE::TESObjectREFR* a_a, const RE::TESObjectREFR* a_b)
	{
		return a_a && a_b ? a_a->GetPosition().GetDistance(a_b->GetPosition()) : std::numeric_limits<float>::max();
	}

	RE::NiPoint3 HeadPosition(RE::Actor* a_actor)
	{
		auto point = a_actor->GetPosition();
		if (auto* model = a_actor->Get3D()) {
			if (auto* head = model->GetObjectByName("NPC Head [Head]")) {
				return head->world.translate;
			}
		}
		point.z += a_actor->GetHeight() * 0.9f;
		return point;
	}

	bool IsBoss(RE::Actor* a_actor)
	{
		const auto& forms = Forms::Get();
		if (forms.dragon && a_actor->HasKeyword(forms.dragon)) {
			return true;
		}
		if (forms.boss) {
			if (const auto* extra = a_actor->extraList.GetByType<RE::ExtraLocationRefType>()) {
				return extra->locRefType == forms.boss;
			}
		}
		return false;
	}

	bool Usable(RE::Actor* a_actor)
	{
		return a_actor && !a_actor->IsDead() && !a_actor->IsDisabled() && a_actor->Is3DLoaded() && !a_actor->IsInKillMove();
	}

	RE::CombatController* Controller(RE::Actor* a_actor)
	{
		return a_actor ? a_actor->GetActorRuntimeData().combatController : nullptr;
	}

	RE::NiPointer<RE::Actor> CombatTarget(RE::Actor* a_actor)
	{
		if (auto* controller = Controller(a_actor)) {
			return controller->targetHandle.get();
		}
		return a_actor ? a_actor->GetActorRuntimeData().currentCombatTarget.get() : nullptr;
	}

	Loadout LoadoutOf(RE::Actor* a_actor)
	{
		auto* left = a_actor->GetEquippedObject(true);
		auto* right = a_actor->GetEquippedObject(false);
		if (left && left->IsArmor()) {
			return Loadout::kShield;
		}
		if (auto* weapon = right ? right->As<RE::TESObjectWEAP>() : nullptr) {
			switch (weapon->GetWeaponType()) {
			case RE::WEAPON_TYPE::kTwoHandSword:
			case RE::WEAPON_TYPE::kTwoHandAxe:
				return Loadout::kTwoHanded;
			case RE::WEAPON_TYPE::kBow:
			case RE::WEAPON_TYPE::kCrossbow:
				return Loadout::kBow;
			case RE::WEAPON_TYPE::kStaff:
				return Loadout::kStaff;
			case RE::WEAPON_TYPE::kHandToHandMelee:
				break;
			default:
				return Loadout::kOneHanded;
			}
		}
		if ((right && right->Is(RE::FormType::Spell)) || (left && left->Is(RE::FormType::Spell))) {
			return Loadout::kSpells;
		}
		if (auto* weapon = left ? left->As<RE::TESObjectWEAP>() : nullptr) {
			return weapon->GetWeaponType() == RE::WEAPON_TYPE::kStaff ? Loadout::kStaff : Loadout::kOneHanded;
		}
		return Loadout::kNone;
	}

	namespace
	{
		bool Restores(const RE::Effect* a_effect)
		{
			const auto* base = a_effect ? a_effect->baseEffect : nullptr;
			if (!base || base->IsHostile() || base->IsDetrimental()) {
				return false;
			}
			const auto archetype = base->GetArchetype();
			const bool modifier = archetype == RE::EffectArchetypes::ArchetypeID::kValueModifier ||
			                      archetype == RE::EffectArchetypes::ArchetypeID::kPeakValueModifier;
			if (modifier && base->data.primaryAV == RE::ActorValue::kHealth) {
				return true;
			}
			const auto& forms = Forms::Get();
			return forms.restoreHealth && base->HasKeyword(forms.restoreHealth);
		}

		// Heal Other and Healing Hands refuse undead, daedra and dwarven targets;
		// Heal Undead takes only undead. The conditions run on the one being healed.
		bool Works(const RE::Effect* a_effect, RE::Actor* a_caster, RE::Actor* a_patient)
		{
			if (!a_patient) {
				return true;
			}
			const auto* base = a_effect->baseEffect;
			return (!a_effect->conditions.head || a_effect->conditions.IsTrue(a_patient, a_caster)) &&
			       (!base->conditions.head || base->conditions.IsTrue(a_patient, a_caster));
		}

		template <class F>
		void ForEachSpell(RE::Actor* a_actor, F&& a_visit)
		{
			if (auto* base = a_actor->GetActorBase()) {
				if (const auto* list = base->GetSpellList()) {
					for (std::uint32_t i = 0; list->spells && i < list->numSpells; ++i) {
						if (list->spells[i]) {
							a_visit(list->spells[i]);
						}
					}
				}
			}
			for (auto* spell : a_actor->GetActorRuntimeData().addedSpells) {
				if (spell) {
					a_visit(spell);
				}
			}
		}
	}

	bool HealsOthers(const RE::SpellItem* a_spell, RE::Actor* a_caster, RE::Actor* a_patient)
	{
		if (!a_spell || a_spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell ||
			a_spell->GetCastingType() == RE::MagicSystem::CastingType::kConstantEffect) {
			return false;
		}
		const auto delivery = a_spell->GetDelivery();
		for (const auto* effect : a_spell->effects) {
			if (!Restores(effect) || !Works(effect, a_caster, a_patient)) {
				continue;
			}
			// Aimed at someone else, or a burst around the caster (Grand Healing).
			if (delivery != RE::MagicSystem::Delivery::kSelf || effect->effectItem.area > 0) {
				return true;
			}
		}
		return false;
	}

	float HealAmount(const RE::SpellItem* a_spell, float a_concentrationSeconds)
	{
		float amount = 0.0f;
		for (const auto* effect : a_spell->effects) {
			if (Restores(effect)) {
				amount = std::max(amount, effect->effectItem.magnitude);
			}
		}
		if (a_spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration) {
			amount *= a_concentrationSeconds;
		}
		return amount;
	}

	Magic MagicOf(RE::Actor* a_actor, RE::Actor* a_patient)
	{
		Magic magic;
		if (!a_actor) {
			return magic;
		}
		ForEachSpell(a_actor, [&](RE::SpellItem* a_spell) {
			if (a_spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell ||
				a_spell->GetCastingType() == RE::MagicSystem::CastingType::kConstantEffect) {
				return;
			}
			if (HealsOthers(a_spell, a_actor, a_patient)) {
				magic.healOther = true;
				magic.heals.push_back(a_spell);
				return;
			}
			if (a_spell->GetDelivery() != RE::MagicSystem::Delivery::kSelf) {
				for (const auto* effect : a_spell->effects) {
					if (effect && effect->baseEffect && effect->baseEffect->IsHostile()) {
						magic.offensive = true;
						break;
					}
				}
			}
		});
		return magic;
	}

	bool HealsParty(RE::Actor* a_actor)
	{
		// A vampire player can be out of Heal Other's reach; the other companions still count.
		return MagicOf(a_actor, RE::PlayerCharacter::GetSingleton()).healOther || MagicOf(a_actor, a_actor).healOther;
	}

	Arsenal ArsenalOf(RE::Actor* a_actor)
	{
		Arsenal arsenal;
		if (!a_actor) {
			return arsenal;
		}
		bool bow = false, crossbow = false, arrows = false, bolts = false;
		auto note = [&](const RE::TESObjectWEAP* a_weapon) {
			switch (a_weapon->GetWeaponType()) {
			case RE::WEAPON_TYPE::kOneHandSword:
			case RE::WEAPON_TYPE::kOneHandDagger:
			case RE::WEAPON_TYPE::kOneHandAxe:
			case RE::WEAPON_TYPE::kOneHandMace:
			case RE::WEAPON_TYPE::kTwoHandSword:
			case RE::WEAPON_TYPE::kTwoHandAxe:
				arsenal.melee = true;
				break;
			case RE::WEAPON_TYPE::kBow:
				arsenal.ranged = bow = true;
				break;
			case RE::WEAPON_TYPE::kCrossbow:
				arsenal.ranged = crossbow = true;
				break;
			case RE::WEAPON_TYPE::kStaff:
				arsenal.casts = true;
				break;
			default:
				break;
			}
		};
		for (bool left : { false, true }) {
			if (auto* form = a_actor->GetEquippedObject(left)) {
				if (const auto* weapon = form->As<RE::TESObjectWEAP>()) {
					note(weapon);
				}
			}
		}
		const auto items = a_actor->GetInventory([](RE::TESBoundObject& a_object) { return a_object.IsWeapon() || a_object.IsAmmo(); });
		for (const auto& [object, entry] : items) {
			if (entry.first <= 0 || !object) {
				continue;
			}
			if (const auto* weapon = object->As<RE::TESObjectWEAP>()) {
				note(weapon);
			} else if (auto* ammo = object->As<RE::TESAmmo>()) {
				(ammo->IsBolt() ? bolts : arrows) = true;
			}
		}
		arsenal.ammo = !arsenal.ranged || (bow && arrows) || (crossbow && bolts);
		arsenal.casts = arsenal.casts || MagicOf(a_actor).offensive;
		arsenal.heals = HealsParty(a_actor);
		return arsenal;
	}

	namespace
	{
		RE::NiCamera* FindCamera(RE::NiAVObject* a_object)
		{
			if (!a_object) {
				return nullptr;
			}
			if (auto* camera = netimmerse_cast<RE::NiCamera*>(a_object)) {
				return camera;
			}
			if (auto* node = a_object->AsNode()) {
				for (const auto& child : node->GetChildren()) {
					if (auto* camera = FindCamera(child.get())) {
						return camera;
					}
				}
			}
			return nullptr;
		}
	}

	std::optional<std::pair<float, float>> Project(const RE::NiPoint3& a_point)
	{
		auto* owner = RE::PlayerCamera::GetSingleton();
		auto* camera = FindCamera(owner ? owner->cameraRoot.get() : nullptr);
		if (!camera) {
			return std::nullopt;
		}
		float x = 0.0f, y = 0.0f, z = 0.0f;
		if (!camera->WorldPtToScreenPt3(a_point, x, y, z, 1e-5f) || !std::isfinite(x) || !std::isfinite(y) || z <= 0.0f) {
			return std::nullopt;
		}
		if (x < -0.05f || x > 1.05f || y < -0.05f || y > 1.05f) {
			return std::nullopt;
		}
		return std::pair{ x, 1.0f - y };
	}
}
