#pragma once

#include "FWM/View.h"

namespace FWM::Party
{
	struct Member;
}

// The spells a mage or healer holds. Each hand can be given one of their own spells, or one
// of yours they do not know yet, which they are then taught. Out of a fight only; in one,
// the combat AI is kept to the choice (Overrides). Main thread.

namespace FWM::Spells
{
	// Everything they may hold under their role, their own spells first.
	[[nodiscard]] std::vector<View::Spell> OptionsFor(RE::Actor* a_companion, Role a_role);

	// A chosen spell as the plan shows it; an empty Spell for none.
	[[nodiscard]] View::Spell Describe(RE::FormID a_spell, RE::Actor* a_companion);

	// Whether their role still allows a spell chosen before (a mage turned healer).
	[[nodiscard]] bool Allowed(RE::FormID a_spell, RE::Actor* a_companion, Role a_role);

	struct Outcome
	{
		bool        done{ false };
		std::string text;
	};

	// Puts a_spell in a hand (0: back to their own choice), teaching it first if it is one of
	// yours. A spell that takes both hands fills both.
	Outcome Choose(RE::Actor* a_companion, Role a_role, std::size_t a_hand, RE::FormID a_spell);

	// After a change of role: drops what the new role does not allow.
	void Recheck(RE::Actor* a_companion, Role a_role);

	// Puts a mage's or healer's chosen spells back in their hands. Returns the names put there.
	std::string EquipChosen(RE::Actor* a_companion);

	// The spell conjures a weapon (Bound Sword and its kind).
	[[nodiscard]] bool Conjures(RE::FormID a_spell);

	// In a fight, on the planner's tick. Any spell may be chosen for a hand, and the combat AI
	// casts most of them itself, from the hand (Overrides lets chosen spells past its skill
	// check). A few kinds it never takes up at all, such as one cast on oneself to hurt those
	// around. A chosen spell it has not taken up a few seconds into a fight is cast for them
	// from its hand when it fits the moment: an aimed or touch spell at the enemy they fight,
	// one centred on themselves when an enemy is close, a ward, armor or cloak they have run
	// out of, a heal on themselves when hurt. It costs what it always costs. a_target: the
	// enemy they fight, or null.
	void Drive(Party::Member& a_member, RE::Actor* a_actor, RE::Actor* a_target, float a_dt);
}
