#include "FWM/WeakWill.h"
#include <algorithm>
#include <cmath>

namespace FWM::WeakWill
{
	namespace
	{
		float Ease(float x) { const float t = std::clamp(x, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
	}
	Frame At(float age, float duration, int rank)
	{
		Frame result;
		if (!std::isfinite(age) || !std::isfinite(duration) || age < 0 || duration <= 0 || age >= duration + kFade) return result;
		const bool grand = rank >= 3;
		const float appear = Ease(age / .7f), fade = 1 - Ease((age - duration) / kFade);
		const float envelope = appear * fade;
		const float settle = Ease((age - kHelmetHold) / (kHelmetEnd - kHelmetHold));
		const float helmet = envelope * (1 - settle);
		const float breath = .96f + .04f * std::sin(age * 1.7f);
		const float size = grand ? 5.9f : (rank == 2 ? 3.85f : 3.6f);
		for (auto& part : result.parts) {
			part.scale = size * (.88f + .12f * appear + .03f * (1 - fade));
			part.height = 13 + 5 * (1 - appear) + 1.0f * std::sin(age * 1.45f) + 6 * (1 - fade);
			part.forward = 1.5f;
			part.tilt = -.018f + .012f * std::sin(age * 1.2f);
		}
		result.parts[0].alpha = (grand ? .55f : .43f) * helmet * breath;
		result.parts[1].alpha = (grand ? .46f : .28f) * helmet * breath;
		result.parts[1].color = { .53f, .76f, 1.0f };
		result.parts[2].alpha = ((grand ? .36f : .25f) * (1 - settle) + (grand ? .30f : .22f) * settle) * envelope * breath;
		result.parts[2].color = { .37f, .61f, .87f };
		result.parts[2].turn = .09f * std::sin(age * .8f);
		result.parts[2].height += 2.0f * std::sin(age * 1.3f);
		result.parts[3].alpha = ((grand ? .54f : .34f) * (1 - settle) + (grand ? .43f : .29f) * settle) * envelope;
		result.parts[3].color = { .62f, .82f, 1.0f };
		result.parts[3].turn = -.10f * age;
		result.parts[3].height += 3.0f * std::sin(age * .9f);
		// The large apparition dissolves into close, quiet wisps around the shoulders.
		// Both use the same attachment and easing, avoiding a pop into a separate effect.
		for (std::size_t i = 2; i < result.parts.size(); ++i) {
			auto& part = result.parts[i];
			part.scale += ((grand ? 3.05f : 2.7f) - part.scale) * settle;
			part.height -= 38 * settle;
		}
		if (grand) {
			result.parts[0].color = { .88f, .55f, 1.0f };
			result.parts[1].color = { .67f, .16f, 1.0f };
			result.parts[2].color = { .48f, .07f, .90f };
			result.parts[3].color = { .80f, .24f, 1.0f };
		}
		result.light = ((grand ? .14f : .10f) * (1 - settle) + .025f * settle) * envelope * breath;
		return result;
	}
}
