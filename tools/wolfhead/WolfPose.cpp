// Samples Kynareth's Maw's bite with the plugin's own code (src/WolfBite.cpp, src/Haunt.cpp) and
// prints it as JSON for the preview: every frame's head transform, jaw and fade, and when each beat
// falls; then the same for the third rank's red wolf, which holds on longer; and for each, the ghostly
// smoke on the one it bites and how far into their stagger they are caught still. The enemy stands at
// the origin with the fighter to the south, so the head faces north (+Y).
//
//   FightWithMeWolfPose [chest height] [frames per second] > pose.json

#include "FWM/Haunt.h"
#include "FWM/Paths.h"
#include "FWM/WolfBite.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

using namespace FWM::WolfBite;

namespace
{
	void Vector(Vec a_v) { std::printf("[%.4f,%.4f,%.4f]", a_v.x, a_v.y, a_v.z); }

	// How far into a stagger the bitten one is: recoiling until caught, held there, then carrying on
	// once let go (the preview tips them back by this much).
	float Recoil(float a_since, float a_held)
	{
		const float catchAt = FWM::Paths::kWolfCatch;
		if (a_since < 0.0f) {
			return 0.0f;
		}
		if (a_since < catchAt) {
			const float u = a_since / catchAt;
			return u * (2.0f - u);
		}
		const float free = a_since - catchAt - a_held;
		return free < 0.0f ? 1.0f : std::max(0.0f, 1.0f - free / 0.5f);
	}

	void Frames(float a_chest, int a_fps, bool a_grand)
	{
		const float held = FWM::Paths::kWolfHold[a_grand ? 2 : 1];
		const float end = std::max(Gone(a_grand), kSnap + FWM::Paths::kWolfCatch + FWM::Haunt::Seconds(held));
		const int   count = static_cast<int>(end * static_cast<float>(a_fps)) + 1;
		std::printf("{\"shake\":%.3f,\"gone\":%.3f,\"held\":%.3f,\"end\":%.3f,\"frames\":[\n", ShakeEnd(a_grand), Gone(a_grand), held, end);
		for (int i = 0; i < count; ++i) {
			const float t = static_cast<float>(i) / static_cast<float>(a_fps);
			const auto  collision = At(std::min(t, Gone(a_grand)), 0.0f, {}, a_chest, a_grand);
			const auto  pose = Display(collision, 0, a_grand);
			const auto& r = pose.rotate;
			const float since = t - kSnap;
			const auto  haunt = FWM::Haunt::At(since, FWM::Paths::kWolfCatch + held, a_grand);
			std::printf("{\"t\":%.4f,\"r\":[%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f],\"p\":", t, r[0][0], r[0][1], r[0][2], r[1][0], r[1][1], r[1][2], r[2][0], r[2][1], r[2][2]);
			Vector(pose.translate);
			std::printf(",\"hit\":{\"p\":");
			Vector(collision.translate);
			std::printf(",\"s\":%.4f}", collision.scale);
			std::printf(",\"s\":%.4f,\"jaw\":%.4f,\"a\":%.4f,\"recoil\":%.3f,\"haunt\":[%.3f,%.3f,%.3f,%.3f,%.3f,[", pose.scale, pose.jaw, t < Gone(a_grand) ? pose.alpha : 0.0f,
				Recoil(since, held), haunt.mist, haunt.glow, haunt.ground, haunt.flare, haunt.scale);
			for (int w = 0; w < FWM::Haunt::kWisps; ++w) {
				const auto& wisp = haunt.wisps[static_cast<std::size_t>(w)];
				std::printf("[%.2f,%.2f,%.2f,%.3f,%.3f]%s", wisp.x, wisp.y, wisp.z, wisp.size, wisp.alpha, w + 1 < FWM::Haunt::kWisps ? "," : "");
			}
			std::printf("]]}%s\n", i + 1 < count ? "," : "");
		}
		std::printf("]}");
	}
}

int main(int a_argc, char** a_argv)
{
	const float chest = a_argc > 1 ? static_cast<float>(std::atof(a_argv[1])) : 70.0f;
	const int   fps = a_argc > 2 ? std::atoi(a_argv[2]) : 60;
	std::printf("{\"chest\":%.2f,\"fps\":%d,\"scale\":%.3f,\"radius\":%.3f,\n", chest, fps, kHeadScale, kHeadRadius);
	std::printf("\"beats\":{\"rise\":%.3f,\"tense\":%.3f,\"snarl\":%.3f,\"swing\":%.3f,\"snap\":%.3f,\"shake\":%.3f,\"gone\":%.3f,\"hitStop\":%.3f,\"hitStopSpeed\":%.3f,\"catch\":%.3f},\n",
		kRise, kTense, kSnarl, kSwing, kSnap, kShake, kGone, kHitStop, kHitStopSpeed, FWM::Paths::kWolfCatch);
	const auto& blue = FWM::Haunt::GlowOf(false);
	const auto& red = FWM::Haunt::GlowOf(true);
	const auto& blueSmoke = FWM::Haunt::SmokeOf(false);
	const auto& redSmoke = FWM::Haunt::SmokeOf(true);
	std::printf("\"haunt\":{\"height\":%.1f,\"lean\":%.3f,\"rise\":%.3f,\"glow\":[[%.3f,%.3f,%.3f],[%.3f,%.3f,%.3f]],\"smoke\":[[%.3f,%.3f,%.3f],[%.3f,%.3f,%.3f]]},\n", FWM::Haunt::kHeight,
		FWM::Haunt::kMostLean, FWM::Haunt::kRise, blue[0], blue[1], blue[2], red[0], red[1], red[2], blueSmoke[0], blueSmoke[1], blueSmoke[2], redSmoke[0], redSmoke[1], redSmoke[2]);
	std::printf("\"points\":{\"back\":");
	Vector(kHeadBack);
	std::printf(",\"front\":");
	Vector(kHeadFront);
	std::printf(",\"teeth\":");
	Vector(kTeeth);
	std::printf(",\"grip\":");
	Vector(kGrip);
	std::printf("},\n\"plain\":");
	Frames(chest, fps, false);
	std::printf(",\n\"grand\":");
	Frames(chest, fps, true);
	std::printf("}\n");
	return 0;
}
