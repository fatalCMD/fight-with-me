#include "FWM/Sunder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace FWM::Sunder
{
	namespace
	{
		constexpr float kPi = 3.141592654f;
		float Unit(float a_t) { return std::clamp(a_t, 0.0f, 1.0f); }
		float Ease(float a_t) { const auto t = Unit(a_t); return t * t * (3.0f - 2.0f * t); }
		float Fade(float a_t, float a_start, float a_length) { return 1.0f - Ease((a_t - a_start) / a_length); }
		float Hash(std::size_t a_i, std::uint32_t a_seed)
		{
			auto h = static_cast<std::uint32_t>(a_i) * 0x9E3779B1u ^ a_seed * 0x85EBCA77u;
			h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; h *= 0x297A2D39u; h ^= h >> 15;
			return static_cast<float>(h & 0xFFFFFFu) / 16777216.0f;
		}
		Part Show(float a_scale, float a_alpha, Color a_color)
		{
			Part p;
			p.scale = a_scale; p.alpha = a_alpha; p.color = a_color;
			return p;
		}
		float Duration(float a_duration) { return std::isfinite(a_duration) ? std::max(a_duration, 0.6f) : 0.6f; }
	}

	float Seconds(float a_duration) { return Duration(a_duration) + 0.5f; }

	Look At(float a_age, float a_duration, bool a_grand)
	{
		Look out;
		const auto t = a_age;
		const auto duration = Duration(a_duration);
		if (!std::isfinite(t) || t < 0.0f || t >= Seconds(duration)) return out;
		// Fissures flash through the struck plate before it sheds pieces. The visible scar then
		// settles to a quiet amber mark for the actual debuff duration, without periodic explosions.
		const auto reveal = Ease(t / 0.09f);
		const auto strike = Fade(t, 0.10f, 0.38f);
		const auto second = a_grand ? Ease((t - 0.24f) / 0.055f) * Fade(t, 0.30f, 0.22f) : 0.0f;
		const auto linger = Fade(t, duration, 0.5f);
		const auto breath = 0.97f + 0.03f * std::sin(t * 2.4f);
		out.cracks = Show(1, reveal * ((a_grand ? 0.19f : 0.14f) * breath + 0.72f * strike + 0.26f * second) * linger,
			{ 1.0f, a_grand ? 0.31f : 0.45f, a_grand ? 0.065f : 0.16f });
		out.grandCracks = Show(1, a_grand ? Ease((t - 0.04f) / 0.11f) * (0.15f * breath + 0.74f * strike + 0.44f * second) * linger : 0,
			{ 1.0f, 0.25f, 0.045f });
		out.flash = Show((0.8f + 0.5f * Ease(t / 0.12f)) * (a_grand ? 1.18f : 1),
			0.65f * reveal * Fade(t, 0.055f, 0.16f) + 0.26f * second, { 1, 0.62f, 0.29f });
		out.flash.at = { 0, 19, 1 };
		out.dust = Show(0.55f + 1.35f * Ease((t - 0.12f) / 0.9f),
			(a_grand ? 0.27f : 0.18f) * Ease((t - 0.1f) / 0.12f) * Fade(t, 0.34f, 0.8f), { 0.42f, 0.37f, 0.29f });
		out.dust.at = { 0, 4, -t * 11 };
		out.dust.roll = 0.13f * t;
		out.light = 0.58f * reveal * Fade(t, 0.08f, 0.31f) + 0.28f * second;

		const std::size_t count = a_grand ? kFragments : kFragments / 2;
		for (std::size_t i = 0; i < count; ++i) {
			const auto delay = 0.115f + 0.085f * Hash(i, 1) + (i >= kFragments / 2 ? 0.16f : 0.0f);
			const auto u = t - delay;
			const auto life = 0.76f + 0.36f * Hash(i, 2);
			if (u <= 0 || u >= life) continue;
			const auto angle = kPi * 0.5f + static_cast<float>(i) * 2.39996323f;
			const auto c = std::cos(angle), s = std::sin(angle);
			const auto speed = (85 + 95 * Hash(i, 3)) * (a_grand ? 1.40f : 1.0f);
			const auto up = 25 + 90 * Hash(i, 4);
			const auto z = -25 + 46 * Hash(i, 5);
			const auto radial = 1.0f - 0.13f * std::abs(z) / 34;
			auto& p = out.fragments[i];
			p.at = { c * (27 * radial + speed * u), s * (18 * radial + speed * u), z + up * u - 170 * u * u };
			p.yaw = angle - kPi * 0.5f + u * (Hash(i, 6) - 0.5f) * 7;
			p.tilt = u * (Hash(i, 7) - 0.5f) * 12;
			p.roll = u * (Hash(i, 8) - 0.5f) * 14;
			p.scale = (0.75f + 0.5f * Hash(i, 9)) * (a_grand ? 1.4f : 1.0f);
			p.alpha = Ease(u / 0.025f) * Fade(u, life * 0.55f, life * 0.45f);
			// The albedo supplies iron detail. A small warm cast reads as recently broken metal,
			// and cools back toward neutral steel as the pieces tumble clear of the impact.
			const auto hot = Fade(u, 0.04f, 0.32f);
			p.color = { 0.88f + 0.12f * hot, 0.90f - 0.12f * hot, 0.96f - 0.31f * hot };
		}
		for (std::size_t i = 0; i < (a_grand ? kSparks : kSparks / 2); ++i) {
			const auto u = t - 0.105f - 0.06f * Hash(i, 10) - (i >= kSparks / 2 ? 0.17f : 0.0f);
			const auto life = 0.29f + 0.25f * Hash(i, 11);
			if (u <= 0 || u >= life) continue;
			const auto angle = kPi * 0.5f + static_cast<float>(i) * 2.39996323f;
			const auto c = std::cos(angle), s = std::sin(angle);
			const auto across = (160 + 160 * Hash(i, 12)) * (a_grand ? 1.2f : 1.0f);
			const auto up = 60 + 160 * Hash(i, 13);
			auto& p = out.sparks[i];
			p.at = { c * (19 + across * u), s * (14 + across * u), 2 + up * u - 220 * u * u };
			p.yaw = angle;
			p.tilt = -std::atan2(up - 440 * u, across);
			p.scale = 0.6f + 0.65f * Hash(i, 14);
			p.alpha = Ease(u / 0.015f) * Fade(u, life * 0.15f, life * 0.85f);
			p.color = { 1, 0.56f - 0.26f * u / life, 0.13f - 0.10f * u / life };
		}
		return out;
	}
}
