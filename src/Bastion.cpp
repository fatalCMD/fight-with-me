#include "FWM/Bastion.h"
#include <algorithm>
#include <cmath>

namespace FWM::Bastion
{
	namespace { float Ease(float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); } }
	float Shake(float distance, int rank)
	{
		if (!std::isfinite(distance) || distance < 0 || distance >= kShakeReach) return 0;
		constexpr std::array<float, 3> strength{ .14f, .16f, .20f };
		return strength[std::clamp(rank, 1, 3) - 1] * (1 - Ease(distance / kShakeReach));
	}
	Frame At(float age, float duration, int rank, bool compact)
	{
		Frame f;
		if (!std::isfinite(age) || !std::isfinite(duration) || age < 0 || duration <= 0 || age >= duration + kFade) return f;
		const bool grand = rank >= 3;
		const float end = 1 - Ease((age - duration) / kFade);
		const float form = Ease(age / .58f);
		const float fall = std::clamp((age - .62f) / (kImpact - .62f), 0.0f, 1.0f);
		const float since = std::max(age - kImpact, 0.0f);
		const float landed = age >= kImpact ? 1.0f : 0.0f;
		const float dissolve = 1 - Ease((age - 1.55f) / (kStoneGone - 1.55f));
		const float settle = landed * 2.2f * std::sin(since * 27.0f) * std::exp(-since * 13.0f);
		const float stone = form * dissolve * end;
		const float size = grand ? 1.14f : (rank == 2 ? 1.10f : 1.0f);
		const std::array<float, 3> body = grand ? std::array<float, 3>{ .92f, .045f, .065f } : std::array<float, 3>{ .64f, .82f, 1.0f };
		const std::array<float, 3> edge = grand ? std::array<float, 3>{ 1.0f, .035f, .065f } : std::array<float, 3>{ .36f, .66f, 1.0f };
		for (std::size_t i = 0; i < f.parts.size(); ++i) f.parts[i].color = i == 0 ? body : edge;
		for (int i : { 0, 1 }) {
			f.parts[i].scale = size;
			f.parts[i].height = (grand ? 148.0f : 112.0f) * (1 - fall * fall * fall) + settle;
		}
		f.parts[0].alpha = (grand ? .39f : .30f) * stone;
		f.parts[1].alpha = (grand ? .55f : .44f) * stone;
		f.parts[2].scale = (grand ? 1.52f : size) * (1 + 1.65f * Ease(since / .95f));
		f.parts[2].alpha = .72f * landed * (1 - Ease(since / 1.2f)) * end;
		f.parts[3].scale = (grand ? 1.52f : size) * (1 + .80f * Ease(since / 1.5f));
		f.parts[3].height = 18 * Ease(since / 1.1f);
		f.parts[3].turn = .12f * since;
		f.parts[3].alpha = .35f * landed * Ease(since / .10f) * (1 - Ease(since / 1.8f)) * end;
		const float ward = Ease((age - kImpact) / .55f) * end;
		const float breath = .90f + .10f * std::sin(age * 2.15f);
		f.parts[4].alpha = .26f * ward * breath;
		f.parts[5].alpha = .68f * ward * breath;
		f.parts[6].alpha = (grand ? .65f : .46f) * ward;
		for (int i : { 4, 5 }) { f.parts[i].turn = .045f * std::sin(age * .85f); f.parts[i].height = 1.5f * std::sin(age * 1.3f); }
		f.parts[6].turn = -.15f * age; f.parts[6].height = 5 * std::sin(age * .9f);
		f.light = (.12f * ward + .24f * landed * std::exp(-since * 7)) * end;
		if(compact)for(int i : {0,1,2,3}){f.parts[i].scale*=.45f;f.parts[i].height*=.45f;}
        return f;
	}
}
