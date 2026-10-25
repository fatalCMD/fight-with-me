#include "FWM/WolfBite.h"

#include <algorithm>
#include <cmath>

namespace FWM::WolfBite
{
	namespace
	{
		Mat Multiply(const Mat& a_a, const Mat& a_b)
		{
			Mat out{};
			for (int i = 0; i < 3; ++i) {
				for (int j = 0; j < 3; ++j) {
					out[i][j] = a_a[i][0] * a_b[0][j] + a_a[i][1] * a_b[1][j] + a_a[i][2] * a_b[2][j];
				}
			}
			return out;
		}

		Vec Turn(const Mat& a_m, Vec a_v)
		{
			return { a_m[0][0] * a_v.x + a_m[0][1] * a_v.y + a_m[0][2] * a_v.z, a_m[1][0] * a_v.x + a_m[1][1] * a_v.y + a_m[1][2] * a_v.z,
				a_m[2][0] * a_v.x + a_m[2][1] * a_v.y + a_m[2][2] * a_v.z };
		}

		Vec Add(Vec a_a, Vec a_b) { return { a_a.x + a_b.x, a_a.y + a_b.y, a_a.z + a_b.z }; }
		Vec Sub(Vec a_a, Vec a_b) { return { a_a.x - a_b.x, a_a.y - a_b.y, a_a.z - a_b.z }; }
		Vec Scale(Vec a_v, float a_s) { return { a_v.x * a_s, a_v.y * a_s, a_v.z * a_s }; }

		Mat TurnX(float a_angle)
		{
			const float c = std::cos(a_angle), s = std::sin(a_angle);
			return { { { 1.0f, 0.0f, 0.0f }, { 0.0f, c, -s }, { 0.0f, s, c } } };
		}

		// A turn about Z that takes +Y to the heading.
		Mat Facing(float a_heading)
		{
			const float c = std::cos(a_heading), s = std::sin(a_heading);
			return { { { c, s, 0.0f }, { -s, c, 0.0f }, { 0.0f, 0.0f, 1.0f } } };
		}

		float Ease(float a_from, float a_to, float a_t)
		{
			const float t = std::clamp(a_t, 0.0f, 1.0f);
			return a_from + (a_to - a_from) * t * t * (3.0f - 2.0f * t);
		}
	}

	// The head turns about the enemy's middle, which stays between its jaws the whole time: it never
	// comes from anywhere, it is simply there in front of them, and only rears and clamps down.
	Pose At(float a_age, float a_heading, Vec a_at, float a_chest, bool a_grand)
	{
		const float t = a_age;
		const float shake = ShakeEnd(a_grand), gone = Gone(a_grand);
		const float length = kHeadLength * kHeadScale;
		float       back = 0.0f, rise = 0.0f, pitch = 0.0f, yaw = 0.0f, grow = 1.0f;
		Pose        pose;
		if (t < kRise) {
			const float u = t / kRise;
			rise = Ease(-0.25f, 0.0f, u);
			pitch = Ease(0.1f, 0.3f, u);
			grow = Ease(0.7f, 1.0f, u);
			pose.alpha = Ease(0.0f, 1.0f, u * 1.4f);
			pose.jaw = kJawOpen * 0.85f * Ease(0.0f, 1.0f, u);
		} else if (t < kTense) {
			const float u = (t - kRise) / (kTense - kRise);
			const float tremble = 0.012f * std::sin(t * 70.0f) * (0.4f + 0.6f * u);
			back = Ease(0.0f, 0.07f, u) + tremble;
			rise = tremble;
			pitch = Ease(0.3f, 0.42f, u);
			pose.jaw = kJawOpen * Ease(0.85f, 1.0f, u) + 0.03f * std::sin(t * 45.0f);
		} else if (t < kSnap) {
			const float u = (t - kTense) / (kSnap - kTense);
			const float slam = u * u * u;
			back = 0.07f - 0.17f * slam;  // into them
			pitch = 0.42f - 0.56f * slam;
			pose.jaw = kJawOpen * (1.0f - slam);
		} else if (t < shake) {
			const float u = (t - kSnap) / (shake - kSnap);
			const float fade = (1.0f - u) * (1.0f - u);
			back = -0.1f + 0.05f * u + 0.03f * std::sin(t * 31.0f) * fade;
			pitch = -0.14f + 0.05f * std::sin(t * 23.0f) * fade;
			yaw = 0.16f * std::sin(t * 18.0f) * fade;  // thrashing
			pose.jaw = 0.04f * std::max(0.0f, std::sin(t * 18.0f)) * fade;
		} else {
			const float u = std::min((t - shake) / (gone - shake), 1.0f);
			back = -0.05f + 0.25f * u;
			rise = 0.25f * u;
			pitch = -0.09f + 0.15f * u;
			grow = 1.0f + 0.12f * u;
			pose.jaw = kJawOpen * 0.4f * Ease(0.0f, 1.0f, u * 2.0f);
			pose.alpha = 1.0f - Ease(0.0f, 1.0f, u);
		}
		const auto  turn = Facing(a_heading);
		const auto  local = Multiply(Facing(yaw), TurnX(pitch));
		const float scale = kHeadScale * grow;
		const Vec   middle{ 0.0f, 0.0f, a_chest };
		const Vec   offset{ 0.0f, -back * length, rise * length };
		pose.rotate = Multiply(turn, local);
		pose.scale = scale;
		pose.translate = Add(a_at, Turn(turn, Sub(Add(middle, offset), Turn(local, Scale(kGrip, scale)))));
		pose.translate.z += std::max(0.0f, a_at.z + kClearance - Lowest(pose));
		return pose;
	}

	Pose Display(Pose pose, float ground, bool grand)
	{
		if (!grand) return pose;
		const Vec grip = Place(pose, kGrip);
		pose.scale *= 1.28f;
		pose.translate = Sub(grip, Turn(pose.rotate, Scale(kGrip, pose.scale)));
		pose.translate.z += std::max(0.0f, ground + kClearance - Lowest(pose));
		return pose;
	}

	float Lowest(const Pose& a_pose)
	{
		float lowest = 1e9f;
		for (const auto& point : kSkullHull) {
			lowest = std::min(lowest, Place(a_pose, point).z);
		}
		const auto jaw = JawTurn(a_pose.jaw);
		for (const auto& point : kJawHull) {
			lowest = std::min(lowest, Place(a_pose, Add(kJawHinge, Turn(jaw, point))).z);
		}
		return lowest - kFur * a_pose.scale;
	}

	Vec Place(const Pose& a_pose, Vec a_local)
	{
		return Add(Turn(a_pose.rotate, Scale(a_local, a_pose.scale)), a_pose.translate);
	}

	Mat JawTurn(float a_radians)
	{
		return TurnX(-a_radians);
	}
}
