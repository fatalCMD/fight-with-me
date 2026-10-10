#pragma once

#include "FWM/Orders.h"

// Small reads over game objects shared by the planner, the abilities and the menu.
// Everything here is main-thread only unless it says otherwise.

namespace FWM::Game
{
	static_assert(sizeof(RE::ActorHandle) == sizeof(std::uint32_t));

	// ActorHandle has no public constructor from its raw value.
	[[nodiscard]] inline RE::ActorHandle FromNative(std::uint32_t a_value) noexcept
	{
		RE::ActorHandle handle;
		std::memcpy(&handle, &a_value, sizeof(a_value));
		return handle;
	}

	[[nodiscard]] inline std::uint32_t Native(const RE::ActorHandle& a_handle) noexcept
	{
		return a_handle.native_handle();
	}

	[[nodiscard]] std::string Name(RE::Actor* a_actor);

	// 0..1 of the current maximum (permanent plus temporary modifiers).
	[[nodiscard]] float Fraction(RE::Actor* a_actor, RE::ActorValue a_value);
	[[nodiscard]] float Current(RE::Actor* a_actor, RE::ActorValue a_value);
	[[nodiscard]] float Maximum(RE::Actor* a_actor, RE::ActorValue a_value);

	[[nodiscard]] float Distance(const RE::TESObjectREFR* a_a, const RE::TESObjectREFR* a_b);

	// Head height, for markers and line of sight.
	[[nodiscard]] RE::NiPoint3 HeadPosition(RE::Actor* a_actor);

	[[nodiscard]] bool IsBoss(RE::Actor* a_actor);

	// Alive, loaded, enabled and not mid-killmove.
	[[nodiscard]] bool Usable(RE::Actor* a_actor);

	[[nodiscard]] RE::CombatController* Controller(RE::Actor* a_actor);

	// The actor the controller is fighting right now, or null.
	[[nodiscard]] RE::NiPointer<RE::Actor> CombatTarget(RE::Actor* a_actor);

	enum class Loadout : std::uint8_t
	{
		kNone,
		kShield,     // a shield in the left hand
		kOneHanded,  // a one-handed weapon
		kTwoHanded,
		kBow,
		kStaff,
		kSpells
	};

	[[nodiscard]] Loadout LoadoutOf(RE::Actor* a_actor);

	struct Magic
	{
		bool                        offensive{ false };  // any hostile spell aimed at others
		bool                        healOther{ false };
		std::vector<RE::SpellItem*> heals;  // every heal that reaches someone else
	};

	// Spells from the base record and any added at runtime. With a patient, only
	// heals whose conditions accept that patient count.
	[[nodiscard]] Magic MagicOf(RE::Actor* a_actor, RE::Actor* a_patient = nullptr);

	// Has a heal that works on the player or on someone like this companion.
	[[nodiscard]] bool HealsParty(RE::Actor* a_actor);

	// Heal per cast, for comparing spells: magnitude, times the channel time for
	// concentration spells.
	[[nodiscard]] float HealAmount(const RE::SpellItem* a_spell, float a_concentrationSeconds);
	[[nodiscard]] bool  HealsOthers(const RE::SpellItem* a_spell, RE::Actor* a_caster = nullptr, RE::Actor* a_patient = nullptr);

	// Weapons equipped or carried, and spells known. Walks the inventory, so callers
	// cache it for a few seconds.
	[[nodiscard]] Arsenal ArsenalOf(RE::Actor* a_actor);

	// Screen position (0..1, origin top left) of a world point, if it is in front
	// of the camera and on screen.
	[[nodiscard]] std::optional<std::pair<float, float>> Project(const RE::NiPoint3& a_point);
}
