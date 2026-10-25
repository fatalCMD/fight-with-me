#pragma once

#include "FWM/StyleLayer.h"
#include <optional>

namespace FightWithMePathsAPI
{
	struct Plan;
}

// Fight With Me's side of Path of the Companion. Fight With Me runs no ability: once a planning
// tick it asks what the abilities running there need of each companion's fight, and carries it
// out with its own orders, so one plugin alone steers them. Main thread.

namespace FWM::AbilityLink
{
	// After every plugin has loaded.
	void Connect();

	// Path of the Companion is here and built from the same sources.
	[[nodiscard]] bool Active();

	// Paths supplies weapon compatibility; without Paths, preserve standalone pools.
	[[nodiscard]] std::optional<std::uint32_t> LoadoutMask(Role role);
	bool IsWindstepDecoy(RE::FormID actor);

	struct Request
	{
		RE::ActorHandle     target;  // an enemy to turn on (a volley), or none
		RE::ObjectRefHandle circle;  // a healing circle's centre to walk into, or none
		float               radius{ 0.0f };
		StyleTweaks         tweaks;  // how their path changes the way they fight
	};

	struct Hold
	{
		RE::ActorHandle enemy;
		RE::ActorHandle tank;
	};

	// Once a planning tick, before the companions are steered.
	void Fetch();

	// What is asked of a companion this tick, or nothing.
	[[nodiscard]] const Request* For(RE::FormID a_member);

	// Enemies a taunt holds on a tank this tick.
	[[nodiscard]] const std::vector<Hold>& Holds();

	// A record from Fight With Me's part of a save made before abilities moved out: Path of the
	// Companion's now. False when it is not installed to take it.
	bool Adopt(SKSE::SerializationInterface* a_intfc, std::uint32_t a_type, std::uint32_t a_version, const std::vector<std::uint8_t>& a_bytes);

	// A load or a fight's end: nothing is asked any more.
	void Clear();

	// Render thread, inside the battle plan's window: Path of the Companion draws its abilities
	// and its tree into the plan. Does nothing without it.
	void DrawInPlan(FightWithMePathsAPI::Plan& a_plan);
}
