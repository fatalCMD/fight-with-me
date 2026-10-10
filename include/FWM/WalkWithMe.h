#pragma once

#include "FWM/Orders.h"

// Optional link to Walk With Me through its public interface. Every call is a
// harmless no-op when it is not installed.

namespace FWM::WalkWithMe
{
	// After all plugins have loaded (kPostLoad).
	void Connect();

	[[nodiscard]] bool      Connected() noexcept;
	[[nodiscard]] bool      IsManaged(RE::FormID a_actor);
	[[nodiscard]] Formation CurrentFormation();
}
