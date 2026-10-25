#include "FWM/Haunt.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace FWM::Haunt
{
	namespace
	{
		constexpr float kTurn = 6.2831853f;

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

	Look At(float a_since, float a_held, bool a_grand)
	{
		Look look;
		if (a_since < 0.0f || a_since > Seconds(a_held)) {
			return look;
		}
		const float t = a_since;
		const float strength = (a_grand ? kGrandStrength : 1.0f) * Ease(t / kBurst) * Fade(t, a_held, kLinger);
		const float breath = 0.85f + 0.15f * std::sin(t * 4.0f);  // the glow pulses, slow and uneasy
		look.mist = 0.55f * strength;
		look.glow = 0.7f * strength * breath;
		look.ground = 0.6f * strength;
		look.flare = (a_grand ? 1.4f : 1.0f) * Out(t / 0.05f) * Fade(t, 0.05f, 0.35f);
		look.scale = 1.0f + 0.15f * (1.0f - Out(t / 0.4f)) + 0.08f * Ease((t - a_held) / kLinger);  // out of them, clinging, loosening as it goes
		for (int k = 0; k < kWisps; ++k) {
			// Each curls up off them and thins, over and over, never two together.
			const float every = (a_grand ? 1.0f : 1.3f) + 0.4f * Hash(k, 1);
			const float since = t - every * Hash(k, 2);
			if (since < 0.0f) {
				continue;
			}
			const float phase = Fraction(since / every);
			const float angle = Hash(k, 3) * kTurn + 0.9f * phase + 0.7f * std::floor(since / every);
			const float reach = 26.0f + 14.0f * Hash(k, 4) + 10.0f * phase;
			auto&       wisp = look.wisps[static_cast<std::size_t>(k)];
			wisp.x = std::cos(angle) * reach;
			wisp.y = std::sin(angle) * reach;
			wisp.z = 20.0f + 70.0f * Hash(k, 5) + 70.0f * phase;
			wisp.size = 0.7f + 0.8f * phase;
			wisp.alpha = std::min(strength, 1.0f) * Ease(phase / 0.2f) * Fade(phase, 0.5f, 0.5f);
		}
		return look;
	}

	float SizeFor(float a_height) { return std::clamp(a_height / kHeight, 0.5f, 3.0f); }

	Turn Lean(float a_x, float a_y, float a_z)
	{
		Turn turn{ { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } } };
		const float across = std::hypot(a_x, a_y);
		if (across < 0.001f || a_z <= 0.0f) {
			return turn;
		}
		// About the level axis square to the lean, by its angle off upright.
		const float angle = std::min(std::atan2(across, a_z), kMostLean);
		const float kx = -a_y / across, ky = a_x / across;
		const float c = std::cos(angle), s = std::sin(angle), v = 1.0f - c;
		turn[0] = { c + v * kx * kx, v * kx * ky, s * ky };
		turn[1] = { v * kx * ky, c + v * ky * ky, -s * kx };
		turn[2] = { -s * ky, s * kx, c };
		return turn;
	}
}
