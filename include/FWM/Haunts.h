#pragma once

// The ghostly smoke on whoever Kynareth's Maw's bite holds frozen, in the game: one effect for each
// bite, its slots following those it caught (Haunt has the numbers). Main thread.

namespace FWM::Haunts
{
	// The smoke for one bite near a_near, red for the wolf's third rank (a_grand). Its id for Hold, or 0
	// when it does not show.
	[[nodiscard]] std::uint32_t Begin(RE::Actor* a_near, bool a_grand);

	// a_enemy caught by it and held a_seconds: the smoke bursts out of them and clings while they stand.
	void Hold(std::uint32_t a_haunt, RE::Actor* a_enemy, float a_seconds);

	void Tick(float a_dt);
	void Clear();
}
