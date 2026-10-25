// Samples Renewal's cloak of water with the plugin's own code (src/WaterCloak.cpp) and prints it as JSON
// for the preview: a twelve-second Renewal on a healer at the middle, its pulses each second washing
// three hurt companions round them until each is well (after 5, 8 and 11 pulses), the washes taking
// the mesh's eight slots as Cloaks hands them out; plain, and gold as the third rank wears it.
//
//   FightWithMeCloakLook [frames per second] > cloak-look.json

#include "FWM/WaterCloak.h"

#include <array>
#include <cstdio>
#include <cstdlib>

using namespace FWM::WaterCloak;

namespace
{
	constexpr float kSeconds = 12.0f;  // Paths::kRenewalSeconds
	constexpr std::array<std::array<float, 2>, 3> kAllies{ { { 150.0f, 60.0f }, { -140.0f, 90.0f }, { 40.0f, -170.0f } } };
	constexpr std::array<int, 3> kPulses{ 5, 8, 11 };  // how many pulses each takes to be well

	struct Slot
	{
		int   ally{ -1 };
		float from{ -100.0f };
	};

	void Run(bool a_grand, int a_fps)
	{
		const float  seconds = kSeconds + kWashSeconds;
		const int    count = static_cast<int>(seconds * static_cast<float>(a_fps)) + 1;
		const auto&  water = WaterOf(a_grand);
		const auto&  foam = FoamOf(a_grand);
		std::printf("{\"size\":%.3f,\"water\":[%.3f,%.3f,%.3f],\"foam\":[%.3f,%.3f,%.3f],\"frames\":[\n", a_grand ? kGrandSize : 1.0f, water[0], water[1], water[2], foam[0], foam[1], foam[2]);
		std::array<Slot, kWashes> slots{};
		int                       pulses = 0;
		for (int i = 0; i < count; ++i) {
			const float t = static_cast<float>(i) / static_cast<float>(a_fps);
			// A pulse each second while it lasts, the first as it starts (Arts' Renewal), washing whoever is still hurt.
			while (static_cast<float>(pulses) <= t && static_cast<float>(pulses) < kSeconds) {
				for (int a = 0; a < 3; ++a) {
					if (pulses >= kPulses[static_cast<std::size_t>(a)]) {
						continue;
					}
					for (auto& slot : slots) {
						if (t - slot.from >= kWashSeconds) {
							slot = { a, static_cast<float>(pulses) };
							break;
						}
					}
				}
				++pulses;
			}
			const auto look = At(t, kSeconds, a_grand);
			std::printf("{\"look\":[%.3f,%.3f,%.3f,%.4f,%.3f,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f],\"ripples\":[", look.veil, look.inner, look.streams, look.twist, look.crown, look.crownTurn,
				look.pool, look.poolScale, look.rise, look.light, look.gleam, look.gleamScale);
			for (int k = 0; k < kRipples; ++k) {
				std::printf("[%.3f,%.3f]%s", look.ripples[static_cast<std::size_t>(k)].scale, look.ripples[static_cast<std::size_t>(k)].alpha, k + 1 < kRipples ? "," : "");
			}
			std::printf("],\"drops\":[");
			for (int k = 0; k < kDrops; ++k) {
				const auto& d = look.drops[static_cast<std::size_t>(k)];
				std::printf("[%.2f,%.2f,%.2f,%.3f,%.3f]%s", d.x, d.y, d.z, d.size, d.alpha, k + 1 < kDrops ? "," : "");
			}
			std::printf("],\"washes\":[");
			for (int k = 0; k < kWashes; ++k) {
				const auto& slot = slots[static_cast<std::size_t>(k)];
				const auto  wash = WashAt(slot.ally >= 0 ? t - slot.from : -1.0f);
				std::printf("[%d,%.2f,%.3f,%.3f,%.3f,%.3f]%s", slot.ally, wash.z, wash.scale, wash.alpha, wash.ring, wash.ringScale, k + 1 < kWashes ? "," : "");
			}
			std::printf("]}%s\n", i + 1 < count ? "," : "");
		}
		std::printf("]}");
	}
}

int main(int a_argc, char** a_argv)
{
	const int fps = a_argc > 1 ? std::atoi(a_argv[1]) : 30;
	std::printf("{\"fps\":%d,\"renewal\":%.2f,\"seconds\":%.3f,\"height\":%.1f,\"flow\":%.3f,\"streamFlow\":%.3f,\"lightFade\":%.2f,\"lightReach\":%.1f,"
				"\"allies\":[[%.1f,%.1f],[%.1f,%.1f],[%.1f,%.1f]],\n\"plain\":",
		fps, kSeconds, kSeconds + kWashSeconds, kHeight, kFlow, kStreamFlow, kLightFade, kLightReach, kAllies[0][0], kAllies[0][1], kAllies[1][0], kAllies[1][1], kAllies[2][0],
		kAllies[2][1]);
	Run(false, fps);
	std::printf(",\n\"grand\":");
	Run(true, fps);
	std::printf("}\n");
	return 0;
}
