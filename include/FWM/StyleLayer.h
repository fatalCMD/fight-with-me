#pragma once

#include "FWM/Orders.h"

#include <array>
#include <cstdint>

// How a role bends a follower's own combat style. The values mirror TESCombatStyle
// (CSGD, CSME, CSCR, CSLR, DATA) so this stays free of game types; Styles.cpp
// copies them in and out.

namespace FWM
{
	struct StyleData
	{
		// CSGD
		float offensive{ 0.5f };
		float defensive{ 0.5f };
		float groupOffensive{ 0.5f };
		float meleeScore{ 1.0f };
		float magicScore{ 1.0f };
		float rangedScore{ 1.0f };
		float shoutScore{ 1.0f };
		float unarmedScore{ 1.0f };
		float staffScore{ 1.0f };
		float avoidThreat{ 0.2f };
		// CSME
		float attackStaggered{ 1.0f };
		float powerAttackStaggered{ 1.0f };
		float powerAttackBlocking{ 1.0f };
		float bash{ 1.0f };
		float bashRecoil{ 1.0f };
		float bashAttack{ 1.0f };
		float bashPowerAttack{ 1.0f };
		float specialAttack{ 0.1f };
		// CSCR
		float circle{ 0.2f };
		float fallback{ 0.2f };
		float flankDistance{ 0.2f };
		float stalkTime{ 0.2f };
		// CSLR
		float strafe{ 0.2f };
		// DATA
		std::uint32_t flags{ 1 };

		static constexpr std::uint32_t kDueling = 1 << 0;
		static constexpr std::uint32_t kFlanking = 1 << 1;
		static constexpr std::uint32_t kAllowDualWielding = 1 << 2;
	};

	// a_weight is how far each value is pulled toward the role's target (0 keeps the
	// follower exactly as they were, 1 replaces their style). Score multipliers are
	// scaled rather than pulled, so a follower who never casts does not start to.
	[[nodiscard]] StyleData Layer(const StyleData& a_base, Role a_role, Position a_position, float a_weight);

	// Committed to one enemy: a companion sent after them, or an enemy a taunt holds on the
	// tank. Straight at them and stay on them: no circling or flanking while waiting a turn,
	// no falling back, no hanging behind a raised shield, no strafing at range. The fight's
	// own scores (what to fight with) are left alone.
	[[nodiscard]] StyleData Committed(const StyleData& a_style);
}
