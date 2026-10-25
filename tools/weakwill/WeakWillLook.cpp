#include "FWM/WeakWill.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
	using namespace FWM::WeakWill;
	const float requested = argc > 1 ? std::strtof(argv[1], nullptr) : 10;
	const float duration = std::isfinite(requested) ? std::clamp(requested, .6f, 60.0f) : 10;
	std::printf("{\"fps\":60,\"duration\":%.4f,\"seconds\":%.4f,\"names\":[", duration, duration + kFade);
	for (std::size_t i = 0; i < kNames.size(); ++i) std::printf("%s\"%s\"", i ? "," : "", kNames[i]);
	std::printf("],\"ranks\":[");
	for (int rank = 1; rank <= 3; ++rank) {
		std::printf("%s[", rank > 1 ? "," : "");
		for (int i = 0; i <= static_cast<int>(std::ceil((duration + kFade) * 60)); ++i) {
			const float age = static_cast<float>(i) / 60;
			const auto frame = At(age, duration, rank);
			std::printf("%s{\"light\":%.4f,\"parts\":[", i ? "," : "", frame.light);
			for (std::size_t k = 0; k < frame.parts.size(); ++k) {
				const auto& p = frame.parts[k];
				std::printf("%s[%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f]", k ? "," : "", p.scale, p.height, p.forward, p.turn, p.tilt, p.alpha, p.color[0], p.color[1], p.color[2]);
			}
			std::printf("]}");
		}
		std::printf("]");
	}
	std::printf("]}\n");
}
