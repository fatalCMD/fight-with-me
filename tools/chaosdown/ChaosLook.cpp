#include "FWM/ChaosPulse.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv)
{
	using namespace FWM::ChaosPulse;
	const float requested = argc > 1 ? std::strtof(argv[1], nullptr) : 8.0f;
	const float bindingSeconds = std::isfinite(requested) ? std::clamp(requested, 0.6f, 120.0f) : 8.0f;
	std::printf("{\"seconds\":%.4f,\"radius\":%.4f,\"fps\":60,\"names\":[", bindingSeconds, kRadius);
	for (std::size_t i = 0; i < kNames.size(); ++i) std::printf("%s\"%s\"", i ? "," : "", kNames[i]);
	std::printf("],\"joints\":[");
	for (std::size_t i = 0; i < kJoints.size(); ++i) std::printf("%s\"%s\"", i ? "," : "", kJoints[i]);
	std::printf("]");
	for (bool grand : { false, true }) {
	std::printf(",\"%s\":[", grand ? "grand" : "frames");
	for (int i = 0; i <= static_cast<int>(bindingSeconds * 60.0f + 0.5f); ++i) {
		const float t = static_cast<float>(i) / 60.0f;
		const auto p = At(t, grand, bindingSeconds);
		std::printf("%s{\"t\":%.4f,\"light\":%.4f,\"parts\":[", i ? "," : "", t, p.light);
		for (std::size_t k = 0; k < p.parts.size(); ++k) {
			const auto& part = p.parts[k];
			std::printf("%s{\"s\":%.4f,\"r\":%.4f,\"z\":%.4f,\"a\":%.4f,\"c\":[%.4f,%.4f,%.4f]}", k ? "," : "", part.scale, part.turn, part.height, part.alpha, part.color[0], part.color[1], part.color[2]);
		}
		const auto& s = p.spirit;
		std::printf("],\"spirit\":{\"s\":%.4f,\"z\":%.4f,\"y\":%.4f,\"a\":%.4f,\"lean\":%.4f,\"roll\":%.4f,\"twist\":%.4f,\"c\":[%.4f,%.4f,%.4f],\"bones\":[", s.scale, s.height, s.forward, s.alpha, s.lean, s.roll, s.twist, s.color[0], s.color[1], s.color[2]);
		for (std::size_t j = 0; j < s.bones.size(); ++j) {
			const auto& b=s.bones[j];
			std::printf("%s{\"at\":[%.4f,%.4f,%.4f],\"m\":[", j ? "," : "", b.at[0],b.at[1],b.at[2]);
			for (std::size_t k=0;k<9;++k) std::printf("%s%.6f",k?",":"",b.turn[k]);
			std::printf("]}");
		}
		std::printf("]}}");
	}
	std::printf("]");
	}
	std::printf(",\"binding\":{\"seconds\":%.4f,\"radius\":%.4f,\"names\":[", bindingSeconds, kBindingRadius);
	for (std::size_t i = 0; i < kBindingNames.size(); ++i) std::printf("%s\"%s\"", i ? "," : "", kBindingNames[i]);
	std::printf("]");
	for (bool grand : { false, true }) {
		std::printf(",\"%s\":[", grand ? "grand" : "frames");
		for (int i = 0; i <= static_cast<int>(std::ceil(bindingSeconds * 60.0f)); ++i) {
			const float age = static_cast<float>(i) / 60.0f;
			const auto frame = BindAt(age, bindingSeconds - age, grand);
			std::printf("%s{\"t\":%.4f,\"parts\":[", i ? "," : "", age);
			for (std::size_t k = 0; k < frame.parts.size(); ++k) {
				const auto& p = frame.parts[k];
				std::printf("%s{\"s\":%.4f,\"r\":%.4f,\"z\":%.4f,\"a\":%.4f,\"c\":[%.4f,%.4f,%.4f]}", k ? "," : "", p.scale, p.turn, p.height, p.alpha, p.color[0], p.color[1], p.color[2]);
			}
			std::printf("]}");
		}
		std::printf("]");
	}
	std::printf("}}\n");
}
