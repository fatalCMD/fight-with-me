#pragma once

#include "FWM/TraitRules.h"

// Unique traits, found on each companion from their own records: perks that cast something on a
// swing, a hit or a bash; abilities that only work in a fight; the scripts of their own mod's
// quests with documented combat moves; a nerve that breaks (not counted for rarity).
// Shared perks and utility effects are excluded. Script-only effects need named notes in
// Data/SKSE/Plugins/FightWithMe/Traits/. Main thread.

namespace FWM::Traits
{
	struct Profile
	{
		std::vector<Trait> traits;
		Rarity             rarity{ Rarity::kCommon };
		int                confidence{ -1 };
	};

	// The notes files, once the game's data is loaded.
	void LoadNotes();

	// With the roster: finds a new companion's traits, and looks again every minute or so out of a
	// fight (a quest can unlock a move, or hand them a script).
	void Refresh();

	[[nodiscard]] const Profile* Find(RE::FormID a_id);

	// Every frame of a fight: a companion blinking across the field or turning to run is logged
	// with what they were up to, so a trait can be seen at work.
	void Tick(float a_dt);

	// A trait's spell landing, for the log.
	void InstallEvents();

	// A load or a new game.
	void Reset();
}
