#pragma once

// Companions and potions: in a fight they drink what they carry when they need it (health,
// magicka or stamina; a fortify against a boss; a resist against a caster), and at rest they
// brew what they are short of, mixing it in their hands, better grades and stranger kinds as
// they rise in level. Each companion's Potions switch turns both off. Main thread.

namespace FWM::Potions
{
	// On the planner's tick, in a fight.
	void Fight(float a_dt);

	// Every update, out of a fight; cheap when nobody is brewing.
	void Rest(float a_dt);

	struct Stock
	{
		int healing{ 0 };
		int magicka{ 0 };
		int stamina{ 0 };
		int other{ 0 };  // fortify, resist and the rest
	};

	[[nodiscard]] Stock StockOf(RE::Actor* a_actor);
}
