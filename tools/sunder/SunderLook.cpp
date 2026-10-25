// Samples the same armor fracture poses used by the plugin. Optional arguments: fps, debuff seconds.
#include "FWM/Sunder.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

using namespace FWM::Sunder;
namespace
{
	void PartJSON(const Part& p)
	{
		std::printf("[%.3f,%.3f,%.3f,%.5f,%.5f,%.5f,%.4f,%.4f,%.4f,%.4f,%.4f]", p.at.x, p.at.y, p.at.z,
			p.yaw, p.tilt, p.roll, p.scale, p.alpha, p.color[0], p.color[1], p.color[2]);
	}
	void Frames(bool grand, int fps, float duration)
	{
		const auto seconds = Seconds(duration);
		const auto count = static_cast<int>(seconds * static_cast<float>(fps)) + 1;
		std::printf("{\"duration\":%.3f,\"seconds\":%.3f,\"frames\":[\n", duration, seconds);
		for (int i = 0; i < count; ++i) {
			const auto look = At(static_cast<float>(i) / static_cast<float>(fps), duration, grand);
			std::printf("{\"light\":%.4f,\"parts\":[", look.light);
			PartJSON(look.cracks); std::printf(","); PartJSON(look.grandCracks); std::printf(",");
			PartJSON(look.flash); std::printf(","); PartJSON(look.dust);
			for (const auto& p : look.fragments) { std::printf(","); PartJSON(p); }
			for (const auto& p : look.sparks) { std::printf(","); PartJSON(p); }
			std::printf("]}%s\n", i + 1 < count ? "," : "");
		}
		std::printf("]}");
	}
}

int main(int argc, char** argv)
{
	const int fps = argc > 1 ? std::clamp(std::atoi(argv[1]), 1, 240) : 60;
	const float duration = argc > 2 ? std::clamp(static_cast<float>(std::atof(argv[2])), 0.6f, 120.0f) : 12.0f;
	std::printf("{\"fps\":%d,\"height\":%.1f,\"parts\":[\"Cracks\",\"GrandCracks\",\"Flash\",\"Dust\"", fps, kHeight);
	for (std::size_t i = 0; i < kFragments; ++i) std::printf(",\"Fragment%02zu\"", i);
	for (std::size_t i = 0; i < kSparks; ++i) std::printf(",\"Spark%02zu\"", i);
	std::printf("],\"plain\":"); Frames(false, fps, duration);
	std::printf(",\"grand\":"); Frames(true, fps, duration);
	std::printf("}\n");
}
