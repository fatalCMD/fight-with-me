#pragma once

// Keeps a companion inside an area around the player during a fight, the way
// vanilla guards keep to their post: the combat AI treats targets outside the area
// as out of bounds, breaks off the chase and comes back.
//
// The area is the engine's own CombatAreaReference (re-centred on the player every
// quarter second), added to the companion's combat controller at a priority above
// the default area and below a package's hold-position area. It is found again
// each tick by a marker rather than held, so it never outlives the controller that
// owns it. Built field by field from the 1.5.97 constructor, with the vtable from
// CommonLib, so SE and AE share one path; the layout is checked against the
// controller's own default area before the first one is made.

namespace FWM::Leash
{
	[[nodiscard]] bool Available() noexcept;

	// Around the player, or around a_center when given (a companion walking into a healer's
	// circle, centred on its marker: the combat AI walks them into the area without leaving the
	// fight). a_radius <= 0 lets the companion roam again.
	void Apply(RE::Actor* a_follower, float a_radius, RE::TESObjectREFR* a_center = nullptr, bool a_recovering = false);
}
