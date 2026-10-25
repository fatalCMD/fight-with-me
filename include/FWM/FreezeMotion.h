#pragma once

#include <array>

namespace FWM::Freeze
{
	// Suppress travel across the ground while the pose is held. Keep vertical velocity and the Havok
	// vector's fourth lane intact, so gravity, slopes and engine bookkeeping do not become a levitation.
	// There is no saved impulse to restore: after release the engine supplies fresh movement normally.
	[[nodiscard]] inline std::array<float, 4> MovementStep(std::array<float, 4> velocity, bool held)
	{
		if (held) {
			velocity[0] = 0.0f;
			velocity[1] = 0.0f;
		}
		return velocity;
	}
}
