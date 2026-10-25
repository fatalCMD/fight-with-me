#include "FWM/HealCircle.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace FWM::HealCircle
{
	namespace
	{
		float Ease(float a_t)
		{
			const float t = std::clamp(a_t, 0.0f, 1.0f);
			return t * t * (3.0f - 2.0f * t);
		}

		// Out past the top, then settling: the aura surging up out of the ground.
		float Burst(float a_t)
		{
			const float t = std::clamp(a_t, 0.0f, 1.0f);
			return t < 0.7f ? Ease(t / 0.7f) * 1.12f : 1.12f - 0.12f * Ease((t - 0.7f) / 0.3f);
		}

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

		float Need(const Ally& a_ally)
		{
			const float missing = 1.0f - std::clamp(a_ally.health, 0.0f, 1.0f);
			return a_ally.down ? 2.0f : (a_ally.player ? 2.0f * missing : missing);
		}
	}

	Look At(float a_age, float a_seconds, bool a_grand)
	{
		Look       look;
		if (!std::isfinite(a_age) || !std::isfinite(a_seconds) || a_age < 0 || a_seconds <= 0 || a_age >= a_seconds) return look;
		const float sinkAt = std::max(a_seconds - kSink, kRise);
		look.ring = Ease(a_age / 0.35f) * (1.0f - Ease((a_age - sinkAt) / kSink));
		look.turn = a_age * 0.045f;
		look.pulse = 0.5f + 0.5f * std::sin(a_age * 3.2f);
		look.aura = std::max(0.0f, Burst(a_age / kRise) * (1.0f - Ease((a_age - sinkAt) / kSink)));
		look.waves = { a_age * 0.16f, -a_age * 0.12f };
		for (std::size_t i = 0; i < look.edgeHeight.size(); ++i) {
			const float phase = std::max(a_age, 0.0f) / 3.2f + static_cast<float>(i) * 0.5f;
			const float u = phase - std::floor(phase);
			look.edgeHeight[i] = 24.0f * Ease(u);
			look.edgeAlpha[i] = look.ring * Ease(u / 0.16f) * std::pow(1.0f - Ease(u), 1.5f);
		}
		// Each mote rises from the ring on its own beat, swirling a little round it as it goes, and
		// fades in near the ground and out near the top; none show before the ring does.
		for (int i = 0; i < kMotes; ++i) {
			const float n = static_cast<float>(i);
			const float period = 2.2f + 0.35f * static_cast<float>(i % 5);
			const float cycle = (a_age + n * 0.53f) / period;
			const float u = cycle - std::floor(cycle);
			const float angle = n * (6.2831853f / kMotes) + 0.17f * std::sin(n * 2.3f) + u * 0.55f;
			const float reach = kMeshRadius * (0.97f + 0.05f * std::sin(u * 6.2831853f + n));
			auto&       mote = look.motes[static_cast<std::size_t>(i)];
			mote.x = std::cos(angle) * reach;
			mote.y = std::sin(angle) * reach;
			mote.z = 2.0f + u * (30.0f + 6.0f * static_cast<float>(i % 3));
			mote.size = 1.0f + 0.30f * static_cast<float>(i % 2) + 0.15f * std::sin(a_age * 4.0f + n);
			mote.alpha = std::pow(std::sin(3.1415927f * u), 1.5f) * look.ring;
		}
		if (!a_grand) {
			return look;
		}
		look.inner = Ease((a_age - 0.15f) / 0.35f) * (1.0f - Ease((a_age - sinkAt) / kSink));
		look.innerTurn = -a_age * 0.065f;
		look.heart = look.ring * (0.8f + 0.2f * look.pulse);
		// Each speck rises from a spot of its own anywhere in the circle, and from a new spot each time.
		for (int i = 0; i < kGlints; ++i) {
			const float n = static_cast<float>(i);
			const float period = 1.6f + 1.0f * Hash(i, 1);
			const float cycle = (a_age + n * 0.37f) / period;
			const int   round = static_cast<int>(std::floor(cycle));
			const float u = cycle - std::floor(cycle);
			const float angle = Hash(i, round * 7 + 2) * 6.2831853f;
			const float reach = std::sqrt(Hash(i, round * 7 + 3)) * kMeshRadius * 0.88f;
			auto&       glint = look.glints[static_cast<std::size_t>(i)];
			glint.x = std::cos(angle) * reach + 3.0f * std::sin(u * 5.0f + n);
			glint.y = std::sin(angle) * reach + 3.0f * std::cos(u * 4.0f + n);
			glint.z = 1.0f + u * (44.0f + 22.0f * Hash(i, 4));
			glint.size = 0.85f + 0.55f * Hash(i, 5);
			glint.alpha = std::pow(std::sin(3.1415927f * u), 1.5f) * look.ring;
		}
		return look;
	}

	int Place(std::span<const Ally> a_allies, float a_radius)
	{
		int   best = -1;
		float most = 0.0f;
		for (std::size_t i = 0; i < a_allies.size(); ++i) {
			const auto& centre = a_allies[i];
			if (Need(centre) <= 0.01f) {
				continue;
			}
			float covered = 0.0f;
			for (const auto& other : a_allies) {
				if (std::hypot(other.x - centre.x, other.y - centre.y) <= a_radius) {
					covered += Need(other);
				}
			}
			const bool better = covered > most + 0.001f || (std::abs(covered - most) <= 0.001f && centre.player);
			if (best < 0 || better) {
				best = static_cast<int>(i);
				most = covered;
			}
		}
		return best;
	}
}
