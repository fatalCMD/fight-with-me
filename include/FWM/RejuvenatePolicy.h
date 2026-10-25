#pragma once
#include <algorithm>
#include <cmath>

namespace FWM::RejuvenatePolicy
{
	inline float Seconds(float requested) { return std::isfinite(requested) ? std::clamp(requested, 5.0f, 12.0f) : 8.0f; }
	inline float Cooldown(float seconds) { return 30.0f + 2.5f * Seconds(seconds); }
	inline float Radius(int rank) { return rank >= 3 ? 435.0f : rank == 2 ? 320.0f : 280.0f; }
	inline float Rate(int rank) { return rank >= 3 ? .05f : rank == 2 ? .035f : .02f; }
	inline float Cap(int rank) { return rank >= 3 ? 20.0f : rank == 2 ? 12.0f : 6.0f; }
	inline bool Inside(float dx, float dy, float dz, float radius) {
		return std::abs(dz) <= 180.0f && dx * dx + dy * dy <= radius * radius;
	}
	struct Pulse {
		float magickaSeconds{}, staminaShareSeconds{};
		void Include(float dt, float left, float vigor) {
			const float seconds = std::max(0.0f, std::min(dt, left));
			magickaSeconds = std::max(magickaSeconds, seconds);
			staminaShareSeconds = std::max(staminaShareSeconds, vigor * seconds);
		}
	};
	// A bounded direct restore, independent of vanilla regeneration delays and modifiers.
	inline float Restore(float maximum, float current, float sharePerSecond, float seconds, float capPerSecond = 6.0f)
	{
		if (!std::isfinite(maximum) || !std::isfinite(current) || !std::isfinite(sharePerSecond) || !std::isfinite(seconds) || maximum <= 0 || seconds <= 0) return 0;
		return std::min(std::max(0.0f, maximum - current), std::min(std::max(0.0f, maximum * sharePerSecond), capPerSecond) * seconds);
	}
}
