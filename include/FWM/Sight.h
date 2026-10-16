#pragma once

// Out of a fight, companions keep watch. An enemy in view, theirs or yours, brings their
// weapons out; with auto battle the companion who sees it starts the fight instead of
// waiting to be attacked. In a fight, a companion still out of it joins in. Main thread.

namespace FWM::Sight
{
	// Every update while nobody is fighting; it looks twice a second.
	void Update(float a_dt);

	// Every update while you or anyone in the party fights: a companion the combat AI has
	// not brought in yet joins whoever is on you or on one of them (with auto battle,
	// anyone in the fight), instead of standing by until they are hit. Whoever is still
	// left out after a moment has the reason logged.
	void Join(float a_dt);

	// A fight has started: weapons are the combat AI's business now.
	void FightStarted();

	// A load or a new game.
	void Reset();
}
