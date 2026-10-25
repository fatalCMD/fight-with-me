#include "FWM/WaterCloak.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace FWM::WaterCloak
{
	namespace
	{
		constexpr float kTurn = 6.2831853f;
		constexpr float kRippleEvery = 1.2f;  // a ripple spreads across the pool this long
		constexpr float kHead = 150.0f;       // a wash starts just over the head
		constexpr float kFeet = 5.0f;

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

		float Fraction(float a_t) { return a_t - std::floor(a_t); }

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
	}

	Look At(float a_age, float a_seconds, bool a_grand)
	{
		Look look;
		if (a_age < 0.0f || a_age > a_seconds) {
			return look;
		}
		const float t = a_age;
		const float out = Fade(t, a_seconds - kGoOut, kGoOut);
		// It pours in from the top: the swirl over the head first, then the streams and the sheath, the
		// pool last as the water reaches the ground.
		look.crown = Ease(t / 0.2f) * out;
		look.streams = Ease((t - 0.05f) / 0.4f) * out;
		look.veil = 0.75f * Ease((t - 0.15f) / 0.45f) * out;
		look.inner = 0.6f * Ease((t - 0.25f) / 0.45f) * out * (0.85f + 0.15f * std::sin(t * 3.0f));
		look.pool = Ease((t - 0.3f) / 0.4f) * out;
		look.poolScale = 0.6f + 0.4f * Out((t - 0.3f) / 0.6f);
		look.rise = 1.0f - 0.15f * Ease((t - (a_seconds - kGoOut)) / kGoOut);  // sinking into the pool as it ends
		look.twist = -0.9f * t;
		look.crownTurn = 1.6f * t;
		look.light = Ease(t / kComeIn) * out * (0.85f + 0.15f * std::sin(t * 2.2f));
		if (a_grand) {
			// The third rank's swirl burns brighter under a glow of its own, and lights more round them.
			const float beat = std::sin(t * 2.5f);
			look.crown *= 1.55f;
			look.gleam = Ease(t / 0.3f) * out * (0.85f + 0.15f * beat);
			look.gleamScale = 1.0f + 0.08f * beat;
			look.light *= 1.3f;
		}
		for (int k = 0; k < kRipples; ++k) {
			const float since = t - 0.3f + static_cast<float>(k) * kRippleEvery / static_cast<float>(kRipples);
			if (since < 0.0f) {
				continue;
			}
			const float phase = Fraction(since / kRippleEvery);
			auto&       ripple = look.ripples[static_cast<std::size_t>(k)];
			ripple.scale = 0.6f + 2.0f * Out(phase);
			ripple.alpha = look.pool * Ease(phase / 0.1f) * (1.0f - Ease(phase));
		}
		for (int k = 0; k < kDrops; ++k) {
			// Each drips off the sheath and falls into the pool, over and over, never two together.
			const float every = 0.9f + 0.3f * Hash(k, 1);
			const float since = t - 0.4f - every * Hash(k, 2);
			if (since < 0.0f) {
				continue;
			}
			const float phase = Fraction(since / every);
			const float high = 70.0f + 45.0f * Hash(k, 3);
			const float angle = Hash(k, 4) * kTurn + 0.5f * std::floor(since / every);
			const float reach = 30.0f + 6.0f * Hash(k, 5);
			auto&       drop = look.drops[static_cast<std::size_t>(k)];
			drop.x = std::cos(angle) * reach;
			drop.y = std::sin(angle) * reach;
			drop.z = std::max(high * (1.0f - phase * phase), 2.0f);  // falling faster as it goes
			drop.size = 0.8f + 0.4f * Hash(k, 6);
			drop.alpha = look.veil / 0.75f * Ease(phase / 0.1f) * Fade(phase, 0.85f, 0.15f);
		}
		return look;
	}

	Wash WashAt(float a_since)
	{
		Wash wash;
		if (a_since < 0.0f || a_since > kWashSeconds) {
			return wash;
		}
		const float u = Unit(a_since / kWashRun);
		wash.z = kHead - (kHead - kFeet) * Ease(u);
		wash.scale = 0.85f + 0.25f * std::sin(3.14159265f * u);  // widest about their middle
		wash.alpha = Ease(a_since / 0.12f) * Fade(a_since, kWashRun - 0.1f, 0.2f);
		const float down = a_since - (kWashRun - 0.1f);  // since it reached their feet
		if (down >= 0.0f) {
			wash.ring = Ease(down / 0.05f) * Fade(down, 0.1f, 0.5f);
			wash.ringScale = 0.5f + 1.6f * Out(down / 0.6f);
		}
		return wash;
	}

	float SizeFor(float a_height) { return std::clamp(a_height / kHeight, 0.5f, 2.5f); }
}
