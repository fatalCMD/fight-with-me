#pragma once

// Holding someone still in whatever pose they are in: their animation stops where it stands, as though
// time had stopped for them alone, until they are let go and it carries on. Caught mid-stagger they
// cannot move, strike or cast, for none of it starts until the stagger ends. Horizontal controller
// motion is held separately so a frozen stagger cannot keep sliding; gravity remains active. No
// actor movement flags or actor values are changed. Set from the main thread; animation and native
// velocity updates read a transient snapshot wherever they run.

namespace FWM::Freeze
{
	// Hooks character animation and the two character-controller velocity setters. Once, after data load.
	void Install();

	// a_actor held still for a_seconds, starting a_after seconds from now (holding again extends it).
	void Hold(RE::Actor* a_actor, float a_after, float a_seconds);

	// Whether a_actor is held still now.
	[[nodiscard]] bool Held(RE::Actor* a_actor);

    // A channel holds only travel; animation and gravity continue. Separate from enemy hit-stop.
    // Independent channels release only their own hold (0: Dragon's Breath).
    void Anchor(RE::Actor* a_actor, std::uint32_t a_owner = 0);
    void ReleaseAnchor(RE::Actor* a_actor, std::uint32_t a_owner = 0);

	// Counts the holds down and lets go of the finished, the dead and the gone.
	void Tick(float a_dt);

	// Everyone let go at once: the fight over, a load, the mod switched off.
	void Clear();
}
