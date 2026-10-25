// Samples Elemental Burst's blasts with the plugin's own code (src/Blast.cpp) and prints them as JSON
// for the preview: each element's part names, then every frame's parts and light, plain and grand (the
// third tier), and the light's flight from a hand to a target's feet.
//
//   FightWithMeBlastLook [frames per second] > blast-look.json

#include "FWM/Blast.h"

#include <cstdio>
#include <cstdlib>

using namespace FWM::Blast;

namespace
{
	void Frames(Element a_element, bool a_grand, int a_fps)
	{
		const auto  names = Parts(a_element);
		const float seconds = Seconds(a_element, a_grand);
		const int   count = static_cast<int>(seconds * static_cast<float>(a_fps)) + 1;
		std::printf("{\"seconds\":%.3f,\"frames\":[\n", seconds);
		for (int i = 0; i < count; ++i) {
			const float t = static_cast<float>(i) / static_cast<float>(a_fps);
			const auto  look = At(a_element, t, a_grand);
			std::printf("{\"light\":%.3f,\"lightColor\":[%.3f,%.3f,%.3f],\"parts\":[", look.light, look.lightColor[0], look.lightColor[1], look.lightColor[2]);
			for (std::size_t n = 0; n < names.size(); ++n) {
				const auto& p = look.parts[n];
				std::printf("[%.2f,%.2f,%.2f,%.4f,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%d,%.4f,%.3f,%.4f]%s", p.at.x, p.at.y, p.at.z, p.yaw, p.tilt, p.scale, p.alpha, p.color[0], p.color[1], p.color[2],
					p.faces ? 1 : 0, p.auraAlpha, p.auraLift, p.auraYaw, n + 1 < names.size() ? "," : "");
			}
			std::printf("]}%s\n", i + 1 < count ? "," : "");
		}
		std::printf("]}");
	}
}

int main(int a_argc, char** a_argv)
{
	const int fps = a_argc > 1 ? std::atoi(a_argv[1]) : 60;
	std::printf("{\"fps\":%d,\"meshRadius\":%.1f,\"flow\":%.3f,\"lightReach\":%.2f,\"lightFade\":%.2f,\"centre\":%.2f,\"edge\":%.2f,\"grandTier\":%d,\"radius\":[%.1f,%.1f,%.1f],\"elements\":[\n",
		fps, kMeshRadius, kFlow, kLightReach, kLightFade, kCentre, kEdge, static_cast<int>(kGrandTier), kRadius[0], kRadius[1], kRadius[2]);
	for (int e = 0; e < static_cast<int>(Element::kCount); ++e) {
		const auto element = static_cast<Element>(e);
		const auto names = Parts(element);
		std::printf("{\"parts\":[");
		for (std::size_t n = 0; n < names.size(); ++n) {
			std::printf("\"%.*s\"%s", static_cast<int>(names[n].size()), names[n].data(), n + 1 < names.size() ? "," : "");
		}
		std::printf("],\n\"plain\":");
		Frames(element, false, fps);
		std::printf(",\n\"grand\":");
		Frames(element, true, fps);
		// The light's flight over 900 units from a mage's raised hand to the feet, at the same rate.
		const Vec   from{ -640.0f, -640.0f, 120.0f }, to{ 0.0f, 0.0f, 4.0f };
		const float flight = Flight(905.0f);
		const int   steps = static_cast<int>(flight * static_cast<float>(fps)) + 1;
		std::printf(",\n\"flight\":%.3f,\"from\":[%.1f,%.1f,%.1f],\"orb\":[\n", flight, from.x, from.y, from.z);
		for (int i = 0; i < steps; ++i) {
			const auto orb = OrbAt(element, from, to, static_cast<float>(i) / static_cast<float>(steps - 1));
			std::printf("[%.2f,%.2f,%.2f,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f]%s", orb.at.x, orb.at.y, orb.at.z, orb.yaw, orb.tilt, orb.alpha, orb.color[0], orb.color[1], orb.color[2],
				i + 1 < steps ? "," : "");
		}
		std::printf("]}%s\n", e + 1 < static_cast<int>(Element::kCount) ? "," : "");
	}
	std::printf("]}\n");
	return 0;
}
