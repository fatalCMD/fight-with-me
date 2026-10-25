#include "FWM/BearRoar.h"

#include <cstdio>

int main()
{
	using namespace FWM::BearRoar;
	std::printf("{\"seconds\":%.4f,\"roar\":%.4f,\"fade\":%.4f,\"fps\":60", kGone, kRoar, kFade);
	for (int variant = 0; variant < 4; ++variant) {
		const bool grand = (variant & 1) != 0;
		std::printf(",\"%s\":[", variant == 3 ? "warGrand" : variant == 2 ? "warFrames" : grand ? "grand" : "frames");
		for (int i = 0; i <= 243; ++i) {
			const float t = static_cast<float>(i) / 60.0f;
			const auto p = variant >= 2 ? WarCryAt(t, grand) : At(t, grand);
			std::printf("%s{\"t\":%.4f,\"side\":%.4f,\"forward\":%.4f,\"height\":%.4f,\"pitch\":%.4f,\"yaw\":%.4f,\"s\":%.4f,\"jaw\":%.4f,\"a\":%.4f,\"tint\":[%.4f,%.4f,%.4f],\"glow\":[%.4f,%.4f,%.4f],\"waves\":[",
				i ? "," : "", t, p.side, p.forward, p.height, p.pitch, p.yaw, p.scale, p.jaw, p.alpha,
				p.tint[0], p.tint[1], p.tint[2], p.glow[0], p.glow[1], p.glow[2]);
			for (std::size_t k = 0; k < p.waves.size(); ++k) {
				const auto& w = p.waves[k];
				std::printf("%s[%.4f,%.4f,%.4f]", k ? "," : "", w.forward, w.radius, w.alpha);
			}
			std::printf("]}");
		}
		std::printf("]");
	}
	std::printf("}\n");
}
