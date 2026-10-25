#include "FWM/Rejuvenation.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace FWM::Rejuvenation
{
	namespace {
		constexpr float pi = 3.14159265f, tau = 2.0f * pi;
		float Ease(float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); }
		float Hash(int i, int salt) {
			std::uint32_t n = static_cast<std::uint32_t>(i + 1) * 0x9E3779B1u ^ static_cast<std::uint32_t>(salt) * 0x85EBCA77u;
			n ^= n >> 16; n *= 0x7FEB352Du; n ^= n >> 15;
			return static_cast<float>(n & 0xFFFFu) / 65535.0f;
		}
	}
	Look At(float age, int rank, float seconds)
	{
		Look look;
		if (!std::isfinite(age) || !std::isfinite(seconds) || age < 0 || seconds <= 0 || age >= seconds) return look;
		rank = std::clamp(rank, 1, 3);
		age = std::isfinite(age) ? std::max(age, 0.0f) : kSeconds;
		const bool grand = rank == 3;
		look.radius = std::array{ 280.0f, 320.0f, 435.0f }[static_cast<std::size_t>(rank - 1)];
		const float on = Ease(age / 0.85f) * (1.0f - Ease((age - (seconds - std::min(2.8f, seconds * .20f))) / std::min(2.8f, seconds * .20f)));
		const float breath = 0.92f + 0.08f * std::sin(age * 1.6f);
		look.parts[0] = { 0.94f + 0.06f * Ease(age / 1.1f), age * 0.035f, 0, on * breath, { 0.8f, 0.92f, 1.0f } };
		look.parts[1] = { 1.08f, -age * 0.08f, 0.5f, on * 0.22f, { 0.05f, 0.30f, 1.0f } };
		look.parts[2] = { 1.0f, age * 0.16f, 0, on * (grand ? 0.65f : 0.38f), { 0.12f, 0.43f, 1.0f } };
		look.parts[3] = { 0.91f, -age * 0.12f, 4, on * (grand ? 0.42f : 0.20f), { 0.12f, 0.65f, 1.0f } };
		look.parts[4] = { 1.0f, -age * 0.10f, 6, on * (grand ? 0.65f : 0.20f), { 0.12f, 0.50f, 1.0f } };
		look.parts[5] = { 0.55f, -age * 0.065f, 1.2f, grand ? on * 0.40f : 0.0f, { 0.5f, 0.8f, 1.0f } };
		for (int i = 0; i < 3; ++i) {
			const float phase = age / 2.8f + static_cast<float>(i) / 3.0f;
			const float u = phase - std::floor(phase);
			const float fade = Ease(u / 0.22f) * (1.0f - Ease((u - 0.48f) / 0.52f));
			look.parts[6 + i] = { 0.98f + 0.045f * Ease(u), age * (i % 2 ? -0.10f : 0.12f),
				6.0f + (grand ? 132.0f : 76.0f) * Ease(u), on * fade * (grand ? 0.78f : 0.62f),
				Color{ 0.10f, 0.54f, 1.0f } };
		}
		// A soft ivory-blue heart keeps the textured seal readable inside the azure rim.
		look.parts[9] = { 1.0f, 0, 1.6f, on * breath * (grand ? 0.64f : 0.48f), { 0.95f, 0.98f, 1.0f } };
		for (auto& part : look.parts) for (auto& channel : part.color) channel *= 1.55f;
		look.light = on * (grand ? 0.90f : 0.60f) * breath;
		look.lightColor = Color{ 0.08f, 0.38f, 1.0f };
		const int count = rank == 1 ? 32 : rank == 2 ? 48 : kMotes;
		for (int i = 0; i < kMotes; ++i) {
			auto& mote = look.motes[static_cast<std::size_t>(i)];
			const float period = 2.8f + 2.2f * Hash(i, 4);
			const float phase = age / period + Hash(i, 7);
			const float u = phase - std::floor(phase);
			const float a = Hash(i, 1) * tau + age * (0.12f + 0.13f * Hash(i, 2)) + u * 0.50f;
			const float r = (0.25f + 0.70f * std::sqrt(Hash(i, 3))) * kMeshRadius * (1.0f - 0.16f * Ease(u));
			mote.at = { r * std::cos(a), r * std::sin(a), 8.0f + u * (grand ? 155.0f : 82.0f) + 6.0f * std::sin(a * 3.0f + age) };
			mote.size = (0.70f + Hash(i, 5) * (i % 9 == 0 ? 1.5f : 0.8f)) * (grand ? 1.40f : 1.15f);
			mote.alpha = i < count ? on * Ease(u / 0.16f) * (1.0f - Ease((u - 0.58f) / 0.42f)) * (0.5f + 0.5f * Hash(i, 8)) : 0.0f;
			mote.color = i % 5 == 0 ? Color{ 0.65f, 0.85f, 1.0f } : Color{ 0.12f, 0.55f, 1.0f };
		}
		return look;
	}
}
