#include "FWM/BearRoar.h"

#include <cmath>
#include <cstdio>
#include <limits>

int main()
{
	using namespace FWM::BearRoar;
	int failures = 0;
	const auto check = [&](bool value, const char* name) { if (!value) { std::printf("FAIL: %s\n", name); ++failures; } };
	check(At(-1).alpha == 0 && At(0).alpha == 0 && At(kGone).alpha == 0 && At(kGone + 1).alpha == 0, "invisible outside its lifetime");
	check(At(std::numeric_limits<float>::quiet_NaN()).alpha == 0, "invalid age cannot light the model");
	check(At(kRoar + 0.2f).jaw > 0.35f && At(kRoar + 0.2f).jaw < 0.5f && At(kRoar + 0.2f).alpha > 0.99f, "mouth wide and head visible for the roar");
	check(At(kFade).jaw < 0.3f && At(kGone - 0.01f).alpha < 0.001f, "jaw settles before a clean fade");
	check(Shake(0) > 0.5f && Shake(600) < Shake(100) && Shake(kShakeReach) == 0 && Shake(kShakeReach + 100) == 0, "camera shake attenuates to zero at distance");
	check(Shake(std::numeric_limits<float>::infinity()) == 0, "invalid distance cannot shake the camera");
	const auto start = At(0.2f), left = At(kLeft), middle = At((kLeft + kRight) * 0.5f), right = At(kRight);
	check(start.alpha > 0 && start.side == 0 && start.yaw == 0, "bear first appears centered before drawing left");
	check(left.side > 0 && std::abs(middle.side) < 0.001f && right.side < 0 && middle.height < right.height,
		"head-on sweep travels left through center to right along a lower arc");
	check(left.side - right.side > 80 && left.yaw - right.yaw > 0.55f, "sweep has a visible span and head turn");
	check(left.yaw > 0 && right.yaw < 0 && At(kFade).side == 0 && At(kFade).yaw == 0 && At(kGone - 0.01f).side == 0,
		"head follows the arc then returns to center before fading");
	for (int i = 0; i < 405; ++i) {
		const float t = static_cast<float>(i) * 0.01f;
		const auto p = At(t);
		const auto sabre = WarCryAt(t), grandSabre = WarCryAt(t, true);
		check(sabre.side == 0 && sabre.forward == 32 && grandSabre.side == 0 && grandSabre.forward == 32 && sabre.height == 215 && grandSabre.height == 285, "War Cry keeps its small forward offset and rank-specific ground clearance");
		check(grandSabre.scale > sabre.scale * 1.3f && grandSabre.glow[0] > grandSabre.glow[1] * 5, "grandSabre has a distinctly larger red silhouette");
		check(sabre.alpha == p.alpha && grandSabre.alpha == p.alpha, "War Cry shares the full formation and fade timeline");
		const auto grand = At(t, true);
		check(grand.scale >= p.scale * 1.4f && grand.scale <= p.scale * 1.5f && grand.alpha == p.alpha && grand.jaw == p.jaw, "rank III is larger with unchanged roar timing");
		check(grand.tint[0] > grand.tint[1] * 5 && grand.glow[0] > grand.glow[1] * 5, "rank III head and roar are red");
		check(grand.side == p.side && grand.yaw == p.yaw && std::abs(p.side) <= 42.01f && std::abs(p.yaw) < 0.31f, "both ranks use the same controlled arc");
		if (t >= 0.2f && t < kLeft) {
			check(At(t + 0.001f).side >= p.side, "head draws left from the middle");
		}
		if (t >= kLeft && t < kRight) {
			check(At(t + 0.001f).side <= p.side, "roar sweeps once without reversing");
		}
		if (t >= kRight && t < kFade) {
			check(At(t + 0.001f).side >= p.side && p.side <= 0, "head returns smoothly from the right without overshooting center");
		}
		check(std::isfinite(p.height) && p.alpha >= 0 && p.alpha <= 1 && p.scale > 0 && p.jaw >= 0 && p.jaw < 0.9f, "bounded head pose");
		for (const auto& w : p.waves) {
			check(w.radius > 0 && w.alpha >= 0 && w.alpha <= 1 && (t >= kRoar || w.alpha == 0), "roar waves start with the roar");
		}
		if (t > 0.01f && t < kGone - 0.02f) {
			const auto next = At(t + 0.001f);
			check(std::abs(next.jaw - p.jaw) < 0.015f && std::abs(next.height - p.height) < 0.2f && std::abs(next.alpha - p.alpha) < 0.01f, "smooth animation across phase transitions");
			check(std::abs(next.side - p.side) < 0.14f && std::abs(next.yaw - p.yaw) < 0.001f, "arc enters and settles smoothly");
		}
	}
	std::printf("Bear roar timeline and distance checks: %d failures\n", failures);
	return failures ? 1 : 0;
}
