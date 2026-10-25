#pragma once

// Which plugin this code runs in. Fight With Me and Path of the Companion are built from the
// same sources. Fight With Me is battle orders only: roles, focus, positions, the enemy they are
// sent after, and the weapons and spells they may use. Every ability, unique trait, level earned
// by fighting and path bonus is Path of the Companion's, with Fight With Me or without it.
//
// Ownership is compile-time. Shared UI references still link some inactive code into both DLLs.

namespace FWM::Host
{
#if defined(FWM_HOST_PATHS)
	inline constexpr bool kAbilities = true;
	inline constexpr bool kOrders = false;
#else
	inline constexpr bool kAbilities = false;
	inline constexpr bool kOrders = true;
#endif

	// The same, for conditions: MSVC calls kAbilities && something a constant condition (C4127).
	[[nodiscard]] constexpr bool Abilities() noexcept { return kAbilities; }
	[[nodiscard]] constexpr bool Orders() noexcept { return kOrders; }

	// Set once by Path of the Companion, after every plugin has loaded and before anything else
	// here runs, when Fight With Me is not installed. Never set inside Fight With Me.
	void SetPathsAlone();

	[[nodiscard]] bool PathsAlone() noexcept;

	// Who steers companions in a fight (whom they turn on, where they may go, how their combat
	// style is bent): Fight With Me when it is installed, otherwise Path of the Companion, for
	// what its own abilities ask. Only one of them ever does, or each would undo the other.
	[[nodiscard]] inline bool Steers() noexcept { return kOrders || PathsAlone(); }
}
