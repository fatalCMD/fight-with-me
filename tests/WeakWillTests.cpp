#include "FWM/WeakWill.h"
#include <cmath>
#include <cstdio>
#include <limits>

int main()
{
	using namespace FWM::WeakWill;
	int failures = 0;
	auto check = [&](bool condition, const char* why) { if (!condition) { std::printf("FAIL: %s\n", why); ++failures; } };
	for (int rank = 1; rank <= 3; ++rank) {
		for (float time : { -1.0f, 0.0f, 10.0f + kFade, 30.0f, std::numeric_limits<float>::quiet_NaN() }) {
			const auto frame = At(time, 10, rank);
			for (const auto& p : frame.parts) check(p.alpha == 0, "no helmet before reveal or after expiry");
			check(frame.light == 0, "no lingering light");
		}
		for (int i = 0; i < 1600; ++i) {
			const auto frame = At(static_cast<float>(i) / 144, 10, rank);
			for (const auto& p : frame.parts) check(std::isfinite(p.scale) && p.scale > 0 && std::isfinite(p.alpha) && p.alpha >= 0 && p.alpha <= .55f &&
				std::isfinite(p.height) && std::isfinite(p.turn) && std::isfinite(p.tilt), "finite translucent helmet pose");
			check(frame.parts[0].scale == frame.parts[1].scale && frame.parts[0].height == frame.parts[1].height, "rim remains fitted to the helmet");
		}
		check(At(.3f, 10, rank).parts[0].alpha < At(1, 10, rank).parts[0].alpha, "helmet softly appears");
		check(At(2.3f, 10, rank).parts[0].alpha < At(1, 10, rank).parts[0].alpha, "helmet dissolves after its entrance");
		check(At(kHelmetEnd, 10, rank).parts[0].alpha == 0 && At(9, 10, rank).parts[1].alpha == 0, "helmet and rim stay gone for the remaining stance");
		check(At(9, 10, rank).parts[2].alpha > 0 && At(9, 10, rank).parts[3].alpha > 0, "subtle wisps and motes remain during the stance");
		check(At(9, 10, rank).parts[2].height < 0 && At(9, 10, rank).parts[2].scale < At(1, 10, rank).parts[2].scale, "remaining effect settles around the follower's shoulders");
		check(At(10.4f, 10, rank).parts[2].alpha < At(9, 10, rank).parts[2].alpha, "remaining wisps fade when the stance ends");
		check(At(1.65f, 1, rank).parts[0].alpha == 0, "early cancellation cannot leave the entrance visible");
	}
	check(At(1, 10, 3).parts[0].scale > At(1, 10, 1).parts[0].scale * 1.2f, "rank III visibly larger");
	check(At(1, 10, 3).parts[2].alpha > At(1, 10, 1).parts[2].alpha, "rank III richer spectral wisps");
	const auto violet = At(1, 10, 3).parts[1].color;
	check(violet[2] > violet[1] * 4 && violet[0] > violet[1] * 3, "rank III uses a violet spectral rim");
	std::printf("Weak Will: %s\n", failures ? "FAILED" : "passed");
	return failures ? 1 : 0;
}
