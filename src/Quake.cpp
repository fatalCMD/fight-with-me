#include "FWM/Quake.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace FWM::Quake
{
	namespace
	{
		constexpr float kTurn = 6.2831853f;
		constexpr float R = kMeshRadius;

		constexpr std::array<std::string_view, 44> kParts{ "Flash", "Cracks", "DustRing",
			"Dust0", "Dust1", "Dust2", "Dust3", "Dust4", "Dust5", "Dust6", "Dust7",
			"Slab00", "Slab01", "Slab02", "Slab03", "Slab04", "Slab05", "Slab06", "Slab07", "Slab08", "Slab09", "Slab10", "Slab11",
			"Chunk00", "Chunk01", "Chunk02", "Chunk03", "Chunk04", "Chunk05", "Chunk06", "Chunk07",
			"Chunk08", "Chunk09", "Chunk10", "Chunk11", "Chunk12", "Chunk13", "Chunk14", "Chunk15",
			"Spire0", "Spire1", "Spire2", "Spire3", "Spire4" };
		static_assert(kParts.size() <= kMaxParts);
		enum Index : std::size_t { kFlash, kCracks, kDustRing, kDust, kSlab = kDust + 8, kChunk = kSlab + 12, kSpire = kChunk + 16 };

		constexpr Color kWarm{ 1.0f, 0.78f, 0.5f };
		constexpr Color kCrack{ 0.09f, 0.07f, 0.05f };
		constexpr Color kDustColor{ 0.42f, 0.37f, 0.31f };

		float Unit(float a_t) { return std::clamp(a_t, 0.0f, 1.0f); }

		float Ease(float a_t)
		{
			const float t = Unit(a_t);
			return t * t * (3.0f - 2.0f * t);
		}

		float Out(float a_t)
		{
			const float t = 1.0f - Unit(a_t);
			return 1.0f - t * t * t;
		}

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

		Mat Times(const Mat& a, const Mat& b)
		{
			Mat out{};
			for (int i = 0; i < 3; ++i) {
				for (int j = 0; j < 3; ++j) {
					for (int k = 0; k < 3; ++k) {
						out[i][j] += a[i][k] * b[k][j];
					}
				}
			}
			return out;
		}
		Mat AboutZ(float a)
		{
			const float c = std::cos(a), s = std::sin(a);
			return { { { c, -s, 0.0f }, { s, c, 0.0f }, { 0.0f, 0.0f, 1.0f } } };
		}
		Mat AboutY(float a)
		{
			const float c = std::cos(a), s = std::sin(a);
			return { { { c, 0.0f, s }, { 0.0f, 1.0f, 0.0f }, { -s, 0.0f, c } } };
		}

		// A rock spun about itself, then leant over toward a_heading.
		Mat Leaning(float a_heading, float a_lean, float a_spin) { return Times(Times(AboutZ(a_heading), AboutY(a_lean)), AboutZ(a_spin)); }

		Part Glow(float a_scale, float a_alpha, Color a_color, float a_z = 0.0f)
		{
			Part part;
			part.at = { 0.0f, 0.0f, a_z };
			part.scale = a_scale;
			part.alpha = a_alpha;
			part.color = a_color;
			return part;
		}

		// A rock breaking up through the ground a_start after the slam, staying low (a_bury of its breadth
		// still in the earth, only its top out), and sinking back in from a_sinkAt.
		Part Erupting(float a_t, float a_start, float a_rise, float a_sinkAt, float a_x, float a_y, float a_size, Mat a_turn, float a_bury)
		{
			Part        part;
			const float u = a_t - a_start;
			if (u <= 0.0f) {
				return part;
			}
			const float up = Out(u / a_rise);
			const float sink = Ease((a_t - a_sinkAt) / 0.55f);
			part.at = { a_x, a_y, -a_size * kRock * (a_bury + 0.5f * (1.0f - up) + 1.0f * sink) };
			part.turn = a_turn;
			part.scale = a_size * (0.75f + 0.25f * up) * (1.0f - 0.25f * sink);
			part.alpha = 1.0f - sink;
			return part;
		}
	}

	float Seconds(bool a_grand) { return a_grand ? 3.2f : 2.6f; }

	std::span<const std::string_view> Parts() { return kParts; }

	Look At(float a_age, bool a_grand)
	{
		Look look;
		if (a_age < 0.0f) {
			return look;
		}
		const float t = a_age, end = Seconds(a_grand);
		auto&       p = look.parts;
		p[kFlash] = Glow((a_grand ? 1.5f : 1.0f) * (0.3f + 0.9f * Out(t / 0.08f)), Fade(t, 0.04f, 0.25f), kWarm, 20.0f);
		p[kCracks] = Glow(0.25f + 0.75f * Out(t / 0.2f), 0.85f * Ease(t / 0.03f) * Fade(t, end - 1.0f, 0.9f), kCrack, 1.5f);
		p[kDustRing] = Glow(Out(t / 0.5f), 0.65f * Ease(t / 0.05f) * Fade(t, 0.25f, 0.7f), kDustColor, 2.0f);
		// Dust thrown up round the slam, rolling out and climbing as it thins.
		for (int k = 0; k < 8; ++k) {
			const float angle = static_cast<float>(k) * (kTurn / 8.0f) + 0.4f * Hash(k, 1);
			const float reach = (0.3f + 0.4f * Hash(k, 2)) * R;
			const float u = t - reach / R * 0.25f;
			auto&       dust = p[kDust + static_cast<std::size_t>(k)];
			if (u <= 0.0f) {
				continue;
			}
			const float out = 1.0f + 0.2f * Out(u);
			dust.at = { std::cos(angle) * reach * out, std::sin(angle) * reach * out, 10.0f + 30.0f * Out(u / 1.6f) };
			dust.turn = AboutZ(u * 0.3f + static_cast<float>(k));
			dust.scale = (a_grand ? 1.5f : 1.0f) * (0.4f + 1.1f * Out(u / 0.8f) + 0.25f * u);
			dust.alpha = 0.6f * Ease(u / 0.12f) * Fade(u, 0.7f, 1.3f);
			dust.color = kDustColor;
		}
		// The rocks: two rings of them breaking out of the ground as the shock runs outward, leaning away
		// from the slam, kept low to it.
		for (int k = 0; k < 12; ++k) {
			const float angle = static_cast<float>(k) * (kTurn / 12.0f) + 0.25f * (Hash(k, 3) - 0.5f);
			const float reach = (k % 2 ? 0.7f : 0.45f) * R + 0.08f * R * Hash(k, 4);
			const float size = (0.8f + 0.5f * Hash(k, 5)) * (a_grand ? 1.3f : 1.0f);
			const Mat   turn = Leaning(angle, 0.25f + 0.3f * Hash(k, 6), Hash(k, 7) * kTurn);
			p[kSlab + static_cast<std::size_t>(k)] = Erupting(t, reach / R * 0.22f, 0.13f, end - 0.9f + 0.3f * Hash(k, 8), std::cos(angle) * reach, std::sin(angle) * reach, size, turn, 0.32f);
		}
		// Small rocks flung up out of the middle, tumbling, then settling into the dust: never far up.
		for (int k = 0; k < 16; ++k) {
			constexpr float kFall = 1300.0f;
			const float     angle = static_cast<float>(k) * (kTurn / 16.0f) + 0.5f * Hash(k, 9);
			const float     across = 120.0f + 200.0f * Hash(k, 10);
			const float     up = 200.0f + 200.0f * Hash(k, 11);
			const float     from = (0.15f + 0.15f * Hash(k, 12)) * R;
			const float     u = t - 0.02f;
			auto&           chunk = p[kChunk + static_cast<std::size_t>(k)];
			if (u <= 0.0f) {
				continue;
			}
			const float landed = (up + std::sqrt(up * up + 2.0f * kFall * 10.0f)) / kFall;
			const float flying = std::min(u, landed);
			const float reach = from + across * flying;
			chunk.at = { std::cos(angle) * reach, std::sin(angle) * reach, std::max(10.0f + up * flying - 0.5f * kFall * flying * flying, 0.0f) };
			chunk.turn = Leaning(angle + 3.0f * flying, 4.0f * flying + static_cast<float>(k), static_cast<float>(k) * 1.7f);
			chunk.scale = (0.15f + 0.15f * Hash(k, 13)) * (1.0f - Ease((u - landed - 0.3f) / 0.5f));
			chunk.alpha = chunk.scale > 0.0f ? 1.0f : 0.0f;
		}
		if (a_grand) {
			// Grand: bigger rocks breaking up round the middle, clear of whoever slammed, still low, last to sink back.
			for (int k = 0; k < 5; ++k) {
				const float angle = static_cast<float>(k) * (kTurn / 5.0f) + 0.3f * Hash(k, 14);
				const float reach = (0.28f + 0.1f * Hash(k, 15)) * R;
				const float size = 1.65f + 0.5f * Hash(k, 16);
				const Mat   turn = Leaning(angle, 0.15f + 0.25f * Hash(k, 17), Hash(k, 18) * kTurn);
				p[kSpire + static_cast<std::size_t>(k)] = Erupting(t, 0.03f + 0.03f * static_cast<float>(k), 0.16f, end - 0.75f, std::cos(angle) * reach, std::sin(angle) * reach, size, turn, 0.3f);
			}
		}
		for (auto& part : p) {
			part.scale = std::max(part.scale, 0.0f);
			part.alpha = Unit(part.alpha);
		}
		return look;
	}

	float LaunchSpeed() { return std::sqrt(2.0f * kGravity * kLaunchHeight); }

	float Airborne() { return 2.0f * LaunchSpeed() / kGravity; }

	float Downed(int a_rank, bool a_boss)
	{
		const float held = kDowned[static_cast<std::size_t>(std::clamp(a_rank, 1, 3) - 1)];
		return a_boss ? held * kBossDowned : held;
	}
}
