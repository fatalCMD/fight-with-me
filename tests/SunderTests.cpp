#include "FWM/Sunder.h"

#include <cmath>
#include <cstdio>
#include <limits>

int main()
{
	using namespace FWM::Sunder;
	int failures = 0;
	auto check = [&](bool ok, const char* why) { if (!ok) { ++failures; std::printf("FAIL: %s\n", why); } };
	auto allParts = [](const Look& look, auto predicate) {
		if (!predicate(look.cracks) || !predicate(look.grandCracks) || !predicate(look.flash) || !predicate(look.dust)) return false;
		for (const auto& p : look.fragments) if (!predicate(p)) return false;
		for (const auto& p : look.sparks) if (!predicate(p)) return false;
		return true;
	};
	auto invisible = [&](const Look& look) { return look.light == 0 && allParts(look, [](const Part& p) { return p.alpha == 0; }); };
	check(invisible(At(-.1f, 12, true)), "nothing shows before impact");
	check(invisible(At(0, 12, true)), "first frame is clean before the fracture reveal");
	check(invisible(At(Seconds(12), 12, true)), "all parts and light vanish at effect expiry");
	check(invisible(At(std::numeric_limits<float>::quiet_NaN(), 12, true)), "invalid clock does not poison rendering");
	check(At(.08f, 12).cracks.alpha > .4f, "cracks precede detached fragments");
	check(allParts(At(.08f, 12), [](const Part& p) { return std::isfinite(p.alpha); }), "early crack frame is finite");
	const auto plain = At(.4f, 12), grand = At(.4f, 12, true);
	std::size_t normalCount = 0, grandCount = 0;
	for (const auto& p : plain.fragments) if (p.alpha > 0) ++normalCount;
	for (const auto& p : grand.fragments) if (p.alpha > 0) ++grandCount;
	check(normalCount == 14 && grandCount == 28, "rank III sheds twice as many armor fragments");
	check(plain.grandCracks.alpha == 0 && grand.grandCracks.alpha > 0, "rank III alone reveals the deeper fracture network");
	check(At(10, 12).cracks.alpha > 0 && At(10, 12).flash.alpha == 0, "quiet scar remains for the actual armor debuff");
	const auto late = At(11, 12, true);
	check(late.dust.alpha == 0 && late.light == 0, "impact light and dust finish rather than looping through debuff");
	for (const auto& p : late.fragments) check(p.alpha == 0, "fragments do not linger around actor");
	check(At(12.25f, 12).cracks.alpha < At(11.5f, 12).cracks.alpha, "scar fades smoothly when debuff ends");
	const auto fallingA = At(.7f, 12).fragments[0], fallingB = At(.8f, 12).fragments[0];
	check(fallingB.at.z < fallingA.at.z, "detached armor follows gravity");
	for (bool enhanced : { false, true }) for (int frame = 0; frame <= 1800; ++frame) {
		const auto look = At(static_cast<float>(frame) / 144, 12, enhanced);
		check(std::isfinite(look.light) && look.light >= 0 && look.light <= 1, "finite bounded light throughout effect");
		check(allParts(look, [](const Part& p) {
			return std::isfinite(p.at.x) && std::isfinite(p.at.y) && std::isfinite(p.at.z) &&
				std::isfinite(p.yaw) && std::isfinite(p.tilt) && std::isfinite(p.roll) &&
				std::isfinite(p.scale) && p.scale >= 0 && std::isfinite(p.alpha) && p.alpha >= 0 && p.alpha <= 1 &&
				std::isfinite(p.color[0]) && std::isfinite(p.color[1]) && std::isfinite(p.color[2]);
		}), "finite transforms and bounded alpha at every frame");
	}
	std::printf("Sunder: %s\n", failures ? "FAILED" : "passed");
	return failures ? 1 : 0;
}
