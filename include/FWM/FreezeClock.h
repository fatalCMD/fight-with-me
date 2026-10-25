#pragma once
#include <algorithm>
#include <cmath>

namespace FWM::Freeze
{
	struct Clock
	{
		float after{ 0 }, left{ 0 };
		void Extend(float delay, float seconds)
		{
			if (!std::isfinite(delay) || !std::isfinite(seconds) || seconds <= 0) return;
			delay = std::max(delay, 0.0f);
			const float start = std::min(after, delay);
			left = std::max(after + left, delay + seconds) - start;
			after = start;
		}
		void Tick(float dt)
		{
			if (!std::isfinite(dt) || dt <= 0) return;
			const float waiting = std::min(after, dt);
			after -= waiting;
			left = std::max(left - (dt - waiting), 0.0f);
		}
		[[nodiscard]] bool Active() const { return after <= 0 && left > 0; }
	};
	[[nodiscard]] inline float AnimationStep(float dt, bool held)
	{
		return held && std::isfinite(dt) && dt > 0 ? dt * 0.0001f : dt;
	}
}
