#pragma once

#include <algorithm>
#include <cmath>

namespace FWM::ChaosDown
{
	inline constexpr float kReach = 250.0f;
	inline constexpr float kSeconds = 8.0f;
	inline constexpr float kUnsetAttackEpsilon = 0.00001f;
	// Cancel only negligible floating-point residue after removing our modifier.
	[[nodiscard]] inline float AttackUnsetCorrection(float a_speed)
	{
		return std::isfinite(a_speed) && std::abs(a_speed) <= kUnsetAttackEpsilon ? -a_speed : 0.0f;
	}

	struct Slow
	{
		float amount{}, seconds{};
	};

	// SpeedMult uses 100 for ordinary speed. Remove our existing contribution before
	// calculating a percentage, so another tank refreshes the slow instead of stacking it.
	[[nodiscard]] inline Slow Combine(float a_speed, float a_previous, float a_left, float a_percent, float a_seconds)
	{
		if (!std::isfinite(a_speed) || !std::isfinite(a_previous) || !std::isfinite(a_left) ||
			!std::isfinite(a_percent) || !std::isfinite(a_seconds) || a_percent <= 0.0f || a_seconds <= 0.0f) {
			return {};
		}
		const float previous = std::min(a_previous, 0.0f);
		const float baseline = std::max(a_speed - previous, 0.0f);
		if (baseline <= 0.0f) return {};
		const float amount = -baseline * std::clamp(a_percent, 0.0f, 90.0f) / 100.0f;
		return { std::min(previous, amount), std::max(a_left, a_seconds) };
	}

	// Weapon-speed actor values use 1 for ordinary speed, but Skyrim also treats an
	// unset 0 as ordinary speed. A slow on that unset value therefore adds +0.75
	// for a 25% slow; subtracting 0.25 would create an invalid negative multiplier.
	// Keep only our temporary contribution, so its removal restores other mods' values.
	[[nodiscard]] inline Slow CombineAttack(float a_speed, float a_previous, float a_left, float a_percent, float a_seconds)
	{
		if (!std::isfinite(a_speed) || !std::isfinite(a_previous) || !std::isfinite(a_left) ||
			!std::isfinite(a_percent) || !std::isfinite(a_seconds) || a_percent <= 0.0f || a_seconds <= 0.0f) return {};
		const bool active = a_left > 0.0f;
		const float baseline = a_speed - (active ? a_previous : 0.0f);
		const float effective = baseline > kUnsetAttackEpsilon ? baseline : 1.0f;
		float target = effective * (1.0f - std::clamp(a_percent, 0.0f, 90.0f) / 100.0f);
		// A weaker recast must not replace an existing stronger attack slow.
		if (active && a_speed > 0.0f) target = std::min(target, a_speed);
		return { target - baseline, std::max(a_left, a_seconds) };
	}
}
