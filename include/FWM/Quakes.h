#pragma once

// Earthshaker in the world: the slam's rocks, dust and cracks, the shake of it, and what it does to
// those it catches. At its first two ranks they fall where they stand; at its third the light enough
// are thrown up (about fifteen feet) and come down hard. Either way they are held down a while before
// they get up. How it looks and how high they go comes from Quake, which the tests and the preview
// share. Main thread.

namespace FWM::Quakes
{
	// The slam's look at a_at, as wide as a_reach (the grand third rank's with its giant rocks), with
	// the shake and the sound of it.
	void Slam(RE::Actor* a_by, const RE::NiPoint3& a_at, float a_reach, bool a_grand);

	// An enemy caught by a slam at a_from: felled where they stand, or at the third rank thrown up
	// (unless a boss or too big), then held down. True when thrown.
	bool Down(RE::Actor* a_enemy, const RE::NiPoint3& a_from, int a_rank, bool a_boss);

	// Every frame of a fight.
	void Tick(float a_dt);

	// Everything out: the fight is over, or a load.
	void Clear();
}
