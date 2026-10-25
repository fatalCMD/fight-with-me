// Samples Magnus's orb with the plugin's own code (src/MagnusOrb.cpp) and prints it as JSON for the
// preview: a mage standing at the origin facing north, the orb's place and look every frame, the motes
// of magicka and the streams pouring into them; once for Magnus's first rank (12 s, plain) and once for
// its third (25 s, grand).
//
//   FightWithMeMagnusLook [frames per second] > magnus-look.json

#include "FWM/MagnusOrb.h"

#include <cstdio>
#include <cstdlib>

using namespace FWM::MagnusOrb;

namespace
{
	void Print(const Mat& a_m)
	{
		std::printf("[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f]", a_m[0][0], a_m[0][1], a_m[0][2], a_m[1][0], a_m[1][1], a_m[1][2], a_m[2][0], a_m[2][1], a_m[2][2]);
	}

	void Print(const Color& a_c) { std::printf("[%.3f,%.3f,%.3f]", a_c[0], a_c[1], a_c[2]); }

	void Streams(const char* a_name, bool a_veils, float a_t, Vec a_orb, const Look& a_look, Vec a_chest, bool a_grand)
	{
		std::printf(",\"%s\":[", a_name);
		for (int s = 0; s < kTendrils; ++s) {
			const auto stream = a_veils ? VeilAt(s, a_t, a_orb, a_look.scale, a_chest, a_look.flow, a_grand) : TendrilAt(s, a_t, a_orb, a_look.scale, a_chest, a_look.flow, a_grand);
			std::printf("{\"from\":[%.2f,%.2f,%.2f],\"scale\":%.4f,\"alpha\":%.3f,\"turn\":", stream.from.x, stream.from.y, stream.from.z, stream.scale, stream.alpha);
			Print(stream.turn);
			std::printf("}%s", s + 1 < kTendrils ? "," : "");
		}
		std::printf("]");
	}

	void Sequence(float a_seconds, bool a_grand, int a_fps)
	{
		const Vec mage{ 0.0f, 0.0f, 0.0f }, chest{ 0.0f, 0.0f, kChest };
		std::printf("{\"seconds\":%.3f,\"frames\":[\n", a_seconds);
		const int count = static_cast<int>(a_seconds * static_cast<float>(a_fps)) + 1;
		Vec       followed = Home(mage, 0.0f, 0.0f, a_grand);
		for (int i = 0; i < count; ++i) {
			const float t = static_cast<float>(i) / static_cast<float>(a_fps);
			const auto  look = At(t, a_seconds, a_grand);
			followed = Follow(followed, Home(mage, 0.0f, t, a_grand), 1.0f / static_cast<float>(a_fps));
			// Out of the chest to its place, and back at the end.
			const Vec orb{ chest.x + (followed.x - chest.x) * look.out, chest.y + (followed.y - chest.y) * look.out, chest.z + (followed.z - chest.z) * look.out };
			std::printf("{\"t\":%.3f,\"orb\":[%.2f,%.2f,%.2f],\"scale\":%.4f,\"alpha\":%.4f,\"core\":%.4f,\"shell\":%.4f,\"swirl\":%.4f,\"light\":%.4f,\"flow\":%.4f,\"halo\":%.4f,\"rim\":%.4f", t, orb.x, orb.y, orb.z,
				look.scale, look.alpha, look.core, look.shell, look.swirl, look.light, look.flow, look.halo, look.rim);
			std::printf(",\"colors\":{\"core\":");
			Print(look.coreColor);
			std::printf(",\"swirl\":");
			Print(look.swirlColor);
			std::printf(",\"shell\":");
			Print(look.shellColor);
			std::printf(",\"halo\":");
			Print(look.haloColor);
			std::printf(",\"rim\":");
			Print(look.rimColor);
			std::printf(",\"ring\":");
			Print(look.ringColor);
			std::printf(",\"stream\":");
			Print(look.streamColor);
			std::printf(",\"light\":");
			Print(look.lightColor);
			std::printf("},\"rings\":[");
			for (int r = 0; r < kRings; ++r) {
				Print(look.rings[static_cast<std::size_t>(r)]);
				std::printf("%s", r + 1 < kRings ? "," : "");
			}
			std::printf("],\"flows\":[");
			for (int f = 0; f < kFlows; ++f) {
				const auto mote = FlowAt(f, t, orb, look.scale, chest, look.flow, a_grand);
				std::printf("[%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f]%s", mote.at.x, mote.at.y, mote.at.z, mote.size, mote.alpha, mote.color[0], mote.color[1], mote.color[2],
					f + 1 < kFlows ? "," : "");
			}
			std::printf("]");
			Streams("tendrils", false, t, orb, look, chest, a_grand);
			Streams("veils", true, t, orb, look, chest, a_grand);
			std::printf(",\"wisps\":[");
			for (int w = 0; w < kWisps; ++w) {
				const auto wisp = WispAt(w, t, orb, look.scale, chest, look.flow, a_grand);
				std::printf("{\"at\":[%.2f,%.2f,%.2f],\"size\":%.3f,\"alpha\":%.3f,\"color\":[%.3f,%.3f,%.3f],\"turn\":", wisp.at.x, wisp.at.y, wisp.at.z, wisp.size, wisp.alpha,
					wisp.color[0], wisp.color[1], wisp.color[2]);
				Print(wisp.turn);
				std::printf("}%s", w + 1 < kWisps ? "," : "");
			}
			std::printf("]}%s\n", i + 1 < count ? "," : "");
		}
		std::printf("]}");
	}
}

int main(int a_argc, char** a_argv)
{
	const int fps = a_argc > 1 ? std::atoi(a_argv[1]) : 30;
	std::printf("{\"fps\":%d,\"radius\":%.1f,\"tendrilLength\":%.1f,\"drift\":%.3f,\"stream\":%.3f,\"lightReach\":%.1f,\"lightFade\":%.2f,\"grow\":%.3f,\"go\":%.3f,\n\"plain\":", fps,
		kRadius, kTendrilLength, kDrift, kStream, kLightReach, kLightFade, kGrow, kGo);
	Sequence(12.0f, false, fps);
	std::printf(",\n\"grand\":");
	Sequence(25.0f, true, fps);
	std::printf("}\n");
	return 0;
}
