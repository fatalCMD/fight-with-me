#pragma once

#include "FWM/Orders.h"

// Each role's special ability, on a button in the battle plan and, when switched on, on
// their own:
//   Bodyguard (tank)    runs to the player when they are in trouble, taunts on arrival
//   Volley (archer)     every archer and mage fires at one enemy for a while
//   War cry (bruiser)   staggers every enemy close around them
//   Ward (mage, healer) casts Oak/Stone/Iron/Ebonyflesh on the player
//   Disarm (fighter)    the Disarm shout's effect at their opponent
// Main thread, on the planner's tick, during a fight.

namespace FWM::Specials
{
	void Update(float a_dt);
	void Tick(float a_dt);  // every frame: a volley's arrows, in quick succession
	void Clear();  // the fight is over

	// While a volley flies: the enemy every archer and mage fires at.
	[[nodiscard]] RE::ActorHandle VolleyTarget();

	// Seconds a special waits before it can be used again.
	[[nodiscard]] float Cooldown(Special a_special);
}
