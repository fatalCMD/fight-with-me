// Samples Arrow Rain with the plugin's own code (src/ArrowRain.cpp) and prints it as JSON for the
// preview, with ArrowRain::DemoArrow standing in for the game's arrows: a plain rain of 12 arrows and a
// grand one of 28. Each frame has the mark and sky, the flare (shot from an archer off to one side), and
// every arrow's nock, spectral arrow, streak and hit; the preview turns the arrows to its own camera.
//
//   FightWithMeArrowRainLook [frames per second] > arrowrain-look.json

#include "FWM/ArrowRain.h"

#include <cstdio>
#include <cstdlib>

using namespace FWM::ArrowRain;

namespace
{
	constexpr float kRain = 2.0f;                     // Paths::kRainSeconds
	constexpr Vec   kBow{ -520.0f, -620.0f, 110.0f };  // the archer's bow hand

	void Frames(int a_count, bool a_grand, int a_fps)
	{
		const float seconds = Seconds(kRain);
		const int   count = static_cast<int>(seconds * static_cast<float>(a_fps)) + 1;
		std::printf("{\"arrows\":%d,\"seconds\":%.3f,\"frames\":[\n", a_count, seconds);
		for (int i = 0; i < count; ++i) {
			const float t = static_cast<float>(i) / static_cast<float>(a_fps);
			const auto  look = At(t, kRain, a_grand);
			const auto  flare = FlareAt(kBow, { 0.0f, 0.0f, kSky }, t);
			std::printf("{\"look\":[%.3f,%.3f,%.4f,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.3f,%.4f,%.3f,%.3f],", look.mark, look.markScale, look.ring, look.dashes, look.heads, look.headsAlpha,
				look.outer, look.floor, look.sky, look.skyScale, look.swirl, look.crown, look.crownTurn, look.burst, look.burstScale);
			std::printf("\"flare\":[%.2f,%.2f,%.2f,%.4f,%.4f,%.3f],\"arrows\":[", flare.at.x, flare.at.y, flare.at.z, flare.yaw, flare.tilt, flare.alpha);
			for (int k = 0; k < a_count; ++k) {
				const auto  arrow = DemoArrow(k, a_count, kRain);
				const auto  head = DemoHead(arrow, t);
				const float landed = t >= arrow.lands ? t - arrow.lands : -1.0f;
				const auto  trail = TrailAt(t - arrow.loosed, landed, a_grand);
				const auto  axis = TrailAxis({ arrow.to.x - arrow.from.x, arrow.to.y - arrow.from.y, arrow.to.z - arrow.from.z });
				const auto  hit = HitAt(landed, true);
				const auto  struck = arrow.to;  // where its point went in
				std::printf("[%.2f,%.2f,%.2f,%.4f,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f]%s", head.x, head.y, head.z, axis.yaw, axis.tilt, trail.length * trail.width,
					trail.alpha, trail.head, trail.arrow, hit.sparkScale, hit.spark, hit.rippleScale, hit.ripple, hit.dustScale, hit.dust, hit.dustRise, struck.x, struck.y, struck.z, k + 1 < a_count ? "," : "");
			}
			std::printf("]}%s\n", i + 1 < count ? "," : "");
		}
		std::printf("]}");
	}
}

int main(int a_argc, char** a_argv)
{
	const int fps = a_argc > 1 ? std::atoi(a_argv[1]) : 60;
	std::printf("{\"fps\":%d,\"radius\":%.1f,\"sky\":%.1f,\"call\":%.3f,\"stroke\":%.3f,\"drift\":%.3f,\"bow\":[%.1f,%.1f,%.1f],\"trailColor\":[%.3f,%.3f,%.3f],\"markColor\":[%.3f,%.3f,%.3f],"
				"\"skyColor\":[%.3f,%.3f,%.3f],\"dustColor\":[%.3f,%.3f,%.3f],\n\"plain\":",
		fps, kRadius, kSky, kCall, kStroke, kDrift, kBow.x, kBow.y, kBow.z, kTrailColor[0], kTrailColor[1], kTrailColor[2], kMarkColor[0], kMarkColor[1], kMarkColor[2], kSkyColor[0],
		kSkyColor[1], kSkyColor[2], kDustColor[0], kDustColor[1], kDustColor[2]);
	Frames(9, false, fps);
	std::printf(",\n\"grand\":");
	Frames(18, true, fps);
	std::printf("}\n");
	return 0;
}
