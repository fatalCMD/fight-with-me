#pragma once

#include <functional>

namespace FightWithMePathsAPI
{
	struct Plan;
}

// The battle plan (an SKSE Menu Framework window), the HUD notices and the
// Menu Framework section. Without Menu Framework 3 none of this registers and
// the plugin runs on saved orders and INI defaults.

namespace FWM::Menu
{
	void Register();  // at data load

	// Paths - Follower Ability Skilltree registers this instead, at data load: its settings page,
	// and without Fight With Me (Host::PathsAlone) the panel with the trees and its key, in place
	// of the battle plan. The point rules are Paths' own record keeping, read and saved through
	// the page's two calls.
	struct PathsRules
	{
		int  above{ 10 };        // a point for every level past this one
		int  extra{ 0 };         // points every companion has on top
		bool respec{ true };     // ranks can be given back
		bool unlimited{ true };  // the unlimited points button is offered
	};
	struct PathsPage
	{
		std::function<PathsRules()>            read;
		std::function<bool(const PathsRules&)> write;  // in use at once and saved; false when the file could not be written
	};
	void RegisterPaths(PathsPage a_page);

	// Path of the Companion beside Fight With Me: its abilities and its tree, drawn into the battle
	// plan's window from the plan's own render (FightWithMePathsAPI::IPaths::DrawInPlan).
	void DrawInPlan(FightWithMePathsAPI::Plan& a_plan);

	[[nodiscard]] bool Available() noexcept;

	// Safe from any thread.
	void Open();
	void Close();
	[[nodiscard]] bool IsOpen() noexcept;
}
