// Samples Earthshaker with the plugin's own code (src/Quake.cpp) and prints it as JSON for the preview:
// the part names, then every frame's parts, at its first two ranks and its grand third; and the third
// rank's throw.
//
//   FightWithMeQuakeLook [frames per second] > quake-look.json

#include "FWM/Quake.h"

#include <cstdio>
#include <cstdlib>

using namespace FWM::Quake;

namespace
{
	void Frames(bool a_grand, int a_fps)
	{
		const auto  names = Parts();
		const float seconds = Seconds(a_grand);
		const int   count = static_cast<int>(seconds * static_cast<float>(a_fps)) + 1;
		std::printf("{\"seconds\":%.3f,\"frames\":[\n", seconds);
		for (int i = 0; i < count; ++i) {
			const auto look = At(static_cast<float>(i) / static_cast<float>(a_fps), a_grand);
			std::printf("[");
			for (std::size_t n = 0; n < names.size(); ++n) {
				const auto& p = look.parts[n];
				const auto& m = p.turn;
				std::printf("[%.2f,%.2f,%.2f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f]%s", p.at.x, p.at.y, p.at.z, m[0][0], m[0][1], m[0][2], m[1][0],
					m[1][1], m[1][2], m[2][0], m[2][1], m[2][2], p.scale, p.alpha, p.color[0], p.color[1], p.color[2], n + 1 < names.size() ? "," : "");
			}
			std::printf("]%s\n", i + 1 < count ? "," : "");
		}
		std::printf("]}");
	}
}

int main(int a_argc, char** a_argv)
{
	const int  fps = a_argc > 1 ? std::atoi(a_argv[1]) : 30;
	const auto names = Parts();
	std::printf("{\"fps\":%d,\"meshRadius\":%.1f,\"launchHeight\":%.1f,\"launchSpeed\":%.2f,\"airborne\":%.3f,\"gravity\":%.1f,\"throwOut\":%.1f,\"downed\":[%.2f,%.2f,%.2f],\"parts\":[", fps,
		kMeshRadius, kLaunchHeight, LaunchSpeed(), Airborne(), kGravity, kThrowOut, kDowned[0], kDowned[1], kDowned[2]);
	for (std::size_t n = 0; n < names.size(); ++n) {
		std::printf("\"%.*s\"%s", static_cast<int>(names[n].size()), names[n].data(), n + 1 < names.size() ? "," : "");
	}
	std::printf("],\n\"plain\":");
	Frames(false, fps);
	std::printf(",\n\"grand\":");
	Frames(true, fps);
	std::printf("}\n");
	return 0;
}
