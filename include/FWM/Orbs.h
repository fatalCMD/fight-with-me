#pragma once

// Magnus in the world: while Magnus holds a mage, a great orb comes out of them and floats behind
// and over them, drifting after them as they move and pouring magicka into them through streams and
// motes, lit and humming like the Eye of Magnus; at the end it is drawn back into them. Its third rank
// calls up the grand orb, bigger and red at heart. Only a look: what Magnus does is Arts'. How it looks
// over time comes from MagnusOrb, which the tests and the preview share. Main thread.

namespace FWM::Orbs
{
	// Calls it up behind a_mage for a_seconds. One to a mage: calling again starts it afresh.
	void Summon(RE::Actor* a_mage, std::string a_name, float a_seconds, bool a_grand);

	// Every frame of a fight.
	void Tick(float a_dt);

	// Every orb gone at once: the fight is over, or a load.
	void Clear();
}
