#pragma once

#include "FWM/Orders.h"

// Path of the Companion's side of Fight With Me. With it installed, Fight With Me gives each
// companion their role, so the path that counts follows it, and the enemy they were sent after
// in the battle plan is the one their abilities go for. Main thread.

namespace FWM::OrdersLink
{
	// After every plugin has loaded.
	void Connect();

	// Fight With Me is here.
	[[nodiscard]] bool Active();

	// The role Fight With Me has saved for them, if any.
	[[nodiscard]] std::optional<Role> RoleOf(RE::FormID a_member);

	// The enemy they were sent after, or none.
	[[nodiscard]] RE::ActorHandle MarkOf(RE::FormID a_member);
}
