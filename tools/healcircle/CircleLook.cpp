// Samples the healer's circle with the plugin's own code (src/HealCircle.cpp) and prints it as JSON
// for the preview: every frame's ring strength, rune turn, pulse, aura and motes.
//
//   FightWithMeCircleLook [frames per second] > circle-look.json

#include "FWM/HealCircle.h"

#include <cstdio>
#include <cstdlib>

using namespace FWM::HealCircle;

int main(int a_argc, char** a_argv)
{
	const int fps = a_argc > 1 ? std::atoi(a_argv[1]) : 30;
	std::printf("{\"seconds\":%.3f,\"radius\":%.2f,\"meshRadius\":%.2f,\"fps\":%d,\"rise\":%.3f,\"sink\":%.3f,\"auraSunk\":%.2f,\"auraFlow\":%.3f,\"frames\":[\n", kSeconds, kRadius,
		kMeshRadius, fps, kRise, kSink, kAuraSunk, kAuraFlow);
	const int count = static_cast<int>(kSeconds * static_cast<float>(fps)) + 1;
	for (int i = 0; i < count; ++i) {
		const float t = static_cast<float>(i) / static_cast<float>(fps);
		const auto  look = At(t, kSeconds, true);  // grand: its own parts are extra, the rest is the same
		std::printf("{\"t\":%.4f,\"ring\":%.4f,\"turn\":%.4f,\"pulse\":%.4f,\"aura\":%.4f,\"waves\":[%.4f,%.4f],\"edgeHeight\":[%.4f,%.4f],\"edgeAlpha\":[%.4f,%.4f],\"motes\":[", t, look.ring, look.turn, look.pulse, look.aura,
			look.waves[0], look.waves[1], look.edgeHeight[0], look.edgeHeight[1], look.edgeAlpha[0], look.edgeAlpha[1]);
		for (int m = 0; m < kMotes; ++m) {
			const auto& mote = look.motes[static_cast<std::size_t>(m)];
			std::printf("[%.3f,%.3f,%.3f,%.3f,%.3f]%s", mote.x, mote.y, mote.z, mote.size, mote.alpha, m + 1 < kMotes ? "," : "");
		}
		std::printf("],\"inner\":%.4f,\"innerTurn\":%.4f,\"heart\":%.4f,\"glints\":[", look.inner, look.innerTurn, look.heart);
		for (int g = 0; g < kGlints; ++g) {
			const auto& glint = look.glints[static_cast<std::size_t>(g)];
			std::printf("[%.3f,%.3f,%.3f,%.3f,%.3f]%s", glint.x, glint.y, glint.z, glint.size, glint.alpha, g + 1 < kGlints ? "," : "");
		}
		std::printf("]}%s\n", i + 1 < count ? "," : "");
	}
	std::printf("]}\n");
	return 0;
}
