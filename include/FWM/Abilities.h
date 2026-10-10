#pragma once

#include "FWM/Party.h"

// The two roles that act on their own: a tank's taunt and a healer's heal.

namespace FWM::Abilities
{
	struct Hold
	{
		RE::ActorHandle enemy;
		RE::ActorHandle tank;
		float           remaining{ 0.0f };
	};

	// Runs taunts and heals for everyone in the party and ages the holds.
	void Update(float a_dt);

	void Clear();

	[[nodiscard]] const std::vector<Hold>& Holds();

	// The tank currently holding this enemy, if any.
	[[nodiscard]] RE::ActorHandle HolderOf(const RE::ActorHandle& a_enemy);

	struct TauntPreview
	{
		float radius{ 0.0f };
		int   certainUpTo{ 0 };  // enemy level held for certain
		int   noChanceFrom{ 0 };  // enemy level that can never be held
		float hold{ 0.0f };
		float cooldown{ 0.0f };
		float stamina{ 0.0f };
	};

	[[nodiscard]] TauntPreview PreviewTaunt(RE::Actor* a_tank);

	struct HealPreview
	{
		std::string spell;    // what they will cast
		bool        borrowed{ false };  // the fallback spell, not their own
		float       amount{ 0.0f };
		float       cost{ 0.0f };
	};

	[[nodiscard]] std::optional<HealPreview> PreviewHeal(RE::Actor* a_healer);
}
