#pragma once

#include <algorithm>
#include <cmath>

namespace FWM::TankPolicy
{
	inline constexpr float kCritical = 0.25f;
	inline constexpr float kBastionSeconds = 8.0f;
	inline constexpr float kBastionReach = 800.0f;
	inline constexpr float kWeakWillCooldown = 40.0f;
	inline constexpr float kBastionCooldown = 60.0f;

	[[nodiscard]] inline bool CriticalAlly(float health, float distance, float reach, bool self, bool living, bool down, bool combat)
	{
		return !self && living && !down && combat && std::isfinite(health) && health > 0.0f && health < kCritical &&
			std::isfinite(distance) && distance >= 0.0f && distance <= reach;
	}
	struct Split
	{
		float victim{ 0.0f }, guardian{ 0.0f };
	};
	// The input is final health loss, after the victim's armor, blocking and resistance.
	// A transferred loss bypasses this split and all ability mitigation a second time.
	[[nodiscard]] inline Split Divide(float loss, float share, bool validGuardian, bool self, bool redirected)
	{
		if (!std::isfinite(loss) || loss <= 0.0f || !validGuardian || self || redirected || !std::isfinite(share)) return { loss, 0.0f };
		const float sent = loss * std::clamp(share, 0.0f, 0.90f);
		return { loss - sent, sent };
	}
	[[nodiscard]] inline float Stronger(float existing, float incoming) { return std::clamp(std::max(existing, incoming), 0.0f, 0.80f); }
	// The caster accepts their own Weak Will penalty. Party immunity only excludes
	// enemy tags; it must never bypass the caster's temporary tradeoff.
	[[nodiscard]] inline float WillDamage(float selfPercent, float taggedPercent, bool partyMember)
	{
		const float own = std::isfinite(selfPercent) ? selfPercent / 100.0f : 0.0f;
		const float tagged = !partyMember && std::isfinite(taggedPercent) ? taggedPercent / 100.0f : 0.0f;
		return 1.0f - Stronger(own, tagged);
	}
}
