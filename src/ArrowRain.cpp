#include "FWM/ArrowRain.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace FWM::ArrowRain
{
	namespace
	{
		constexpr float kTurn = 6.2831853f;
		constexpr float kPulse = 0.6f;  // the arrowheads pulse in once in this long

		float Unit(float a_t) { return std::clamp(a_t, 0.0f, 1.0f); }

		float Ease(float a_t)
		{
			const float t = Unit(a_t);
			return t * t * (3.0f - 2.0f * t);
		}

		// Fast, then settling.
		float Out(float a_t)
		{
			const float t = 1.0f - Unit(a_t);
			return 1.0f - t * t * t;
		}

		// 1 until a_start, then down to 0 over a_length.
		float Fade(float a_t, float a_start, float a_length) { return 1.0f - Ease((a_t - a_start) / a_length); }

		// The same scatter every time for the same numbers: 0..1.
		float Hash(int a_a, int a_b)
		{
			std::uint32_t h = static_cast<std::uint32_t>(a_a) * 0x9E3779B1u ^ static_cast<std::uint32_t>(a_b) * 0x85EBCA77u;
			h ^= h >> 15;
			h *= 0x2C1B3C6Du;
			h ^= h >> 12;
			h *= 0x297A2D39u;
			h ^= h >> 15;
			return static_cast<float>(h & 0xFFFFFFu) / 16777216.0f;
		}

		float Length(Vec a_v) { return std::sqrt(a_v.x * a_v.x + a_v.y * a_v.y + a_v.z * a_v.z); }
	}

	float Seconds(float a_rain) { return kCall + a_rain + kLanding + 1.0f; }

	Look At(float a_age, float a_rain, bool a_grand)
	{
		Look look;
		if (a_age < 0.0f) {
			return look;
		}
		const float t = a_age;
		const float u = t - kCall;                   // since the flare burst
		const float end = kCall + a_rain + kLanding;  // the last arrow down
		if (u >= 0.0f) {
			look.burst = Out(u / 0.05f) * Fade(u, 0.05f, 0.35f);
			look.burstScale = (0.4f + 0.9f * Out(u / 0.3f)) * (a_grand ? 1.3f : 1.0f);
			look.mark = Ease(u / 0.2f) * Fade(t, end, 0.6f);
			look.markScale = 1.0f + 0.35f * (1.0f - Out(u / 0.3f));
			const float phase = u / kPulse - std::floor(u / kPulse);
			look.heads = Ease(phase);
			look.headsAlpha = look.mark * Ease(phase / 0.15f) * Fade(phase, 0.45f, 0.55f);
			look.outer = a_grand ? 0.8f * look.mark : 0.0f;
			look.floor = 0.3f * look.mark;
		}
		const float s = u + 0.05f;  // the sky lights a moment before the burst reaches it
		if (s >= 0.0f) {
			look.sky = Ease(s / 0.15f) * Fade(t, end - kLanding, 0.5f) * (a_grand ? 1.0f : 0.8f);
			look.skyScale = (0.6f + 0.4f * Out(s / 0.4f)) * (a_grand ? 1.3f : 1.0f);
			look.crown = a_grand ? look.sky : 0.0f;
		}
		look.ring = 0.35f * t;
		look.dashes = -0.6f * t;
		look.swirl = 0.5f * t;
		look.crownTurn = -0.8f * t;
		return look;
	}

	Flare FlareAt(Vec a_from, Vec a_to, float a_age)
	{
		Flare flare;
		if (a_age < 0.0f || a_age > kCall) {
			return flare;
		}
		const float u = Unit(a_age / kCall);
		const float k = 1.0f - (1.0f - u) * (1.0f - u);  // slowing as it climbs
		// Up steeply first, curving over toward the sky above the mark.
		const Vec mid{ a_from.x + (a_to.x - a_from.x) * 0.15f, a_from.y + (a_to.y - a_from.y) * 0.15f, a_from.z + (a_to.z - a_from.z) * 0.85f };
		const float a = (1.0f - k) * (1.0f - k), b = 2.0f * (1.0f - k) * k, c = k * k;
		flare.at = { a * a_from.x + b * mid.x + c * a_to.x, a * a_from.y + b * mid.y + c * a_to.y, a * a_from.z + b * mid.z + c * a_to.z };
		const Vec heading{ 2.0f * (1.0f - k) * (mid.x - a_from.x) + 2.0f * k * (a_to.x - mid.x), 2.0f * (1.0f - k) * (mid.y - a_from.y) + 2.0f * k * (a_to.y - mid.y),
			2.0f * (1.0f - k) * (mid.z - a_from.z) + 2.0f * k * (a_to.z - mid.z) };
		flare.yaw = std::atan2(heading.y, heading.x);
		flare.tilt = std::atan2(-heading.z, std::hypot(heading.x, heading.y));
		flare.alpha = Ease(a_age / 0.04f) * Fade(a_age, kCall - 0.06f, 0.06f);
		return flare;
	}

	Trail TrailAt(float a_since, float a_landed, bool a_grand)
	{
		Trail trail;
		if (a_since < 0.0f) {
			return trail;
		}
		const float strength = a_grand ? 1.0f : 0.85f;
		const bool  flying = a_landed < 0.0f;
		const float flown = flying ? a_since : a_since - a_landed;  // how long it flew, or has so far
		const float down = std::max(a_landed, 0.0f);
		const float in = Ease(a_since / 0.04f);
		// The arrow stands where it struck a moment, glowing, then fades and leaves the game's own.
		trail.arrow = strength * in * (flying ? 1.0f : Fade(down, 0.3f, 0.5f));
		trail.length = Out(flown / 0.12f) * (flying ? 1.0f : 1.0f - Ease(down / 0.12f));
		trail.width = a_grand ? 1.35f : 1.0f;
		trail.alpha = 0.6f * strength * in * (flying ? 1.0f : 1.0f - Ease(down / 0.15f));
		trail.head = strength * in * (flying ? 1.0f : Fade(down, 0.05f, 0.25f));
		return trail;
	}

	Blast::Part TrailAxis(Vec a_velocity)
	{
		const float speed = Length(a_velocity);
		Blast::Part part;
		part.faces = true;
		if (speed > 1e-3f) {
			const Vec back{ -a_velocity.x / speed, -a_velocity.y / speed, -a_velocity.z / speed };
			part.yaw = std::atan2(back.y, back.x);
			part.tilt = std::acos(std::clamp(back.z, -1.0f, 1.0f));
		}
		return part;
	}

	Mat TrailTurn(Vec a_head, Vec a_velocity, Vec a_camera)
	{
		return Blast::Facing(TrailAxis(a_velocity), { a_camera.x - a_head.x, a_camera.y - a_head.y, a_camera.z - a_head.z });
	}

	Hit HitAt(float a_since, bool a_ground)
	{
		Hit hit;
		if (a_since < 0.0f) {
			return hit;
		}
		hit.spark = Out(a_since / 0.03f) * Fade(a_since, 0.03f, 0.15f);
		hit.sparkScale = 0.6f + 0.8f * Out(a_since / 0.15f);
		hit.ripple = Ease(a_since / 0.03f) * Fade(a_since, 0.05f, 0.3f) * (a_ground ? 1.0f : 0.0f);
		hit.rippleScale = 0.3f + Out(a_since / 0.35f);
		hit.dust = a_ground ? 0.75f * Ease(a_since / 0.06f) * Fade(a_since, 0.15f, 0.7f) : 0.0f;
		hit.dustScale = 0.5f + 0.9f * Out(a_since / 0.6f);
		hit.dustRise = 25.0f * Out(a_since / 0.8f);
		return hit;
	}

	Arrow DemoArrow(int a_index, int a_count, float a_rain)
	{
		Arrow arrow;
		const float angle = Hash(a_index, 1) * kTurn;
		const float reach = std::sqrt(Hash(a_index, 2)) * kRadius;
		arrow.to = { std::cos(angle) * reach, std::sin(angle) * reach, 0.0f };
		arrow.from = { arrow.to.x + (Hash(a_index, 3) - 0.5f) * 160.0f, arrow.to.y + (Hash(a_index, 4) - 0.5f) * 160.0f, kSky };
		arrow.loosed = kCall + static_cast<float>(a_index) * a_rain / static_cast<float>(std::max(a_count, 1));
		const float fall = Length({ arrow.to.x - arrow.from.x, arrow.to.y - arrow.from.y, arrow.to.z - arrow.from.z });
		arrow.lands = arrow.loosed + (fall - (kTip - kSink)) / kSpeed;
		return arrow;
	}

	Vec DemoHead(const Arrow& a_arrow, float a_age)
	{
		// The nock stops short of the mark by the arrow's length, less what of it goes into the ground.
		const Vec   along{ a_arrow.to.x - a_arrow.from.x, a_arrow.to.y - a_arrow.from.y, a_arrow.to.z - a_arrow.from.z };
		const float fall = Length(along), stop = (fall - (kTip - kSink)) / std::max(fall, 1e-3f);
		const float u = stop * Unit((a_age - a_arrow.loosed) / std::max(a_arrow.lands - a_arrow.loosed, 1e-3f));
		return { a_arrow.from.x + along.x * u, a_arrow.from.y + along.y * u, a_arrow.from.z + along.z * u };
	}

	Vec TipOf(Vec a_nock, Vec a_velocity)
	{
		const float speed = Length(a_velocity);
		if (speed < 1e-3f) {
			return { a_nock.x, a_nock.y, a_nock.z - kTip };
		}
		return { a_nock.x + a_velocity.x / speed * kTip, a_nock.y + a_velocity.y / speed * kTip, a_nock.z + a_velocity.z / speed * kTip };
	}
}
