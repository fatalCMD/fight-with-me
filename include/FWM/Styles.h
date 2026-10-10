#pragma once

#include "FWM/Orders.h"

// One runtime combat style per follower: their own style with the role layered on
// top, pointed to from their combat controller while they fight.
//
// CombatController::combatStyle (+0x38) is a live pointer that the controller's
// SaveGame never writes, so a runtime style never reaches the save.

namespace FWM::Styles
{
	// Point the follower's controller at their layered style, rebuilding it when
	// the orders or the underlying style changed. Cheap when nothing did.
	void Apply(RE::Actor* a_follower, const Orders& a_orders, Position a_resolved);

	// Put the follower's own style back if ours is still in place.
	void Release(RE::Actor* a_follower);

	void ReleaseAll();
}
