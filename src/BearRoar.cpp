#include "FWM/BearRoar.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace FWM::BearRoar
{
	namespace
	{
		float Ease(float a_from, float a_to, float a_time)
		{
			const float t = std::clamp(a_time, 0.0f, 1.0f);
			return a_from + (a_to - a_from) * t * t * (3.0f - 2.0f * t);
		}
	}

	Pose At(float a_age, bool a_grand)
	{
		Pose p;
		if (!std::isfinite(a_age) || a_age < 0.0f || a_age >= kGone) {
			return p;
		}
		const float rise = Ease(0.0f, 1.0f, a_age / kRoar);
		const float cry = Ease(0.0f, 1.0f, (a_age - kRoar) / 0.15f);
		const float settle = Ease(0.0f, 1.0f, (a_age - kSettle) / (kFade - kSettle));
		const float fade = Ease(1.0f, 0.0f, (a_age - kFade) / (kGone - kFade));
		p.alpha = Ease(0.0f, 1.0f, a_age / 0.32f) * fade;
		p.scale = kScale * (0.78f + 0.22f * rise + 0.04f * (1.0f - fade));
		p.forward = 58.0f + 22.0f * cry - 10.0f * settle;
		p.height = 190.0f + 30.0f * rise - 12.0f * cry + 20.0f * (1.0f - fade);
		// Appear in the middle, draw left, sweep right, then return to center before fading.
		// Ease each turn so the changes of direction feel deliberate and smooth.
		const float drawLeft = Ease(0.0f, 1.0f, (a_age - 0.2f) / (kLeft - 0.2f));
		const float sweep = Ease(0.0f, 1.0f, (a_age - kLeft) / (kRight - kLeft));
		const float returnToCenter = Ease(1.0f, 0.0f, (a_age - kRight) / (kFade - kRight));
		const float arc = std::numbers::pi_v<float> * sweep;
		const float aim = drawLeft * std::cos(arc) * returnToCenter;
		p.side = 42.0f * aim;
		p.height -= 14.0f * std::sin(arc);
		if (a_grand) {
			p.scale *= kGrandScale;
			p.height += 25.0f;
			p.forward += 10.0f;
			p.tint = { 0.85f, 0.12f, 0.10f };
			p.glow = kRed;
		}
		p.pitch = 0.08f + 0.12f * rise - 0.17f * cry + 0.08f * settle;
		const float voice = cry * (1.0f - settle);
		// The bear's rest mesh already has an open mouth; a smaller extra turn keeps its chin forward.
		p.jaw = 0.02f + 0.08f * rise + 0.32f * voice + 0.018f * std::sin(a_age * 31.0f) * voice;
		p.yaw = 0.30f * aim + 0.004f * std::sin((a_age - kRoar) * 8.0f) * voice;
		p.pitch += 0.008f * std::sin(a_age * 37.0f) * voice;
		for (std::size_t i = 0; i < p.waves.size(); ++i) {
			const float elapsed = a_age - kRoar - static_cast<float>(i) * 0.24f;
			if (elapsed < 0.0f || elapsed >= 0.85f) {
				continue;
			}
			const float t = elapsed / 0.85f;
			p.waves[i] = { 140.0f + 310.0f * t, 35.0f + 145.0f * t,
				(a_grand ? 0.52f : 0.38f) * Ease(0.0f, 1.0f, t / 0.08f) * (1.0f - t) * (1.0f - t) };
		}
		return p;
	}

	Pose WarCryAt(float a_age, bool a_grand)
	{
		auto p = At(a_age, a_grand);
		// A spirit inhabiting the caster's upper body, not a projectile or an overhead summon.
		p.side = 0.0f;
		p.forward = 32.0f;
		// Every rank is a sabercat, with extra clearance for the larger crimson head.
		p.height = a_grand ? 285.0f : 215.0f;
		p.scale *= 0.90f;
		p.yaw *= 0.18f;
		p.pitch *= 0.4f;
		p.jaw *= a_grand ? 0.75f : 0.85f;
		p.glow = a_grand ? kRed : std::array<float, 3>{ 1.0f, 0.82f, 0.16f };
		for (auto& wave : p.waves) {
			wave.forward = 0.0f;
			wave.alpha *= 0.65f;
		}
		return p;
	}

	float Shake(float a_distance)
	{
		if (!std::isfinite(a_distance)) {
			return 0.0f;
		}
		const float t = std::clamp(a_distance / kShakeReach, 0.0f, 1.0f);
		return 0.65f * (1.0f - t) * (1.0f - t);
	}
}
