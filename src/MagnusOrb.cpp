#include "FWM/MagnusOrb.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace FWM::MagnusOrb
{
	namespace
	{
		constexpr float kPi = 3.1415927f;
		constexpr float kTurn = 6.2831853f;

		// Plain: Magnus's white blue, the core nearly white.
		constexpr Color kBlue{ 0.55f, 0.78f, 1.0f };
		constexpr Color kWhite{ 0.86f, 0.94f, 1.0f };
		// Grand: a red heart, red and purple skins, blue only at the edge.
		constexpr Color kRed{ 0.9f, 0.1f, 0.06f };
		constexpr Color kCrimson{ 0.9f, 0.08f, 0.1f };
		constexpr Color kPurple{ 0.85f, 0.12f, 0.32f };
		constexpr Color kEdgeBlue{ 0.3f, 0.42f, 0.95f };
		constexpr Color kViolet{ 0.58f, 0.32f, 0.95f };
		constexpr Color kMagenta{ 0.88f, 0.2f, 0.55f };
		constexpr Color kRose{ 1.0f, 0.25f, 0.35f };

		float Unit(float a_t) { return std::clamp(a_t, 0.0f, 1.0f); }

		float Ease(float a_t)
		{
			const float t = Unit(a_t);
			return t * t * (3.0f - 2.0f * t);
		}

		// Out a little past full, then settling.
		float Swell(float a_t)
		{
			const float t = Unit(a_t);
			return t < 0.75f ? Ease(t / 0.75f) * 1.08f : 1.08f - 0.08f * Ease((t - 0.75f) / 0.25f);
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

		Vec   Add(Vec a, Vec b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
		Vec   Sub(Vec a, Vec b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
		Vec   Mul(Vec a, float k) { return { a.x * k, a.y * k, a.z * k }; }
		float Length(Vec a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
		Vec   Unitised(Vec a)
		{
			const float l = Length(a);
			return l > 1e-5f ? Mul(a, 1.0f / l) : Vec{ 1.0f, 0.0f, 0.0f };
		}
		Vec Cross(Vec a, Vec b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

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
		Mat AboutX(float a)
		{
			const float c = std::cos(a), s = std::sin(a);
			return { { { 1.0f, 0.0f, 0.0f }, { 0.0f, c, -s }, { 0.0f, s, c } } };
		}

		// Its +X pointed along a_dir, then turned a_roll about itself.
		Mat Pointing(Vec a_dir, float a_roll)
		{
			const Vec d = Unitised(a_dir);
			return Times(Times(AboutZ(std::atan2(d.y, d.x)), AboutY(std::atan2(-d.z, std::hypot(d.x, d.y)))), AboutX(a_roll));
		}

		// Each ring tipped over by its own angle about its own heading, as an armillary's are.
		constexpr std::array<std::array<float, 2>, kRings> kTips{ { { 0.35f, 0.0f }, { 1.1f, 2.1f }, { 1.45f, 4.2f } } };
		constexpr std::array<float, kRings>                kSpins{ 0.9f, -0.7f, 0.5f };

		// The way magicka runs from the orb into the chest, a_u of the way along it (0..1): off the
		// orb's face toward the mage, somewhere new each time round (a_round), swinging out to one side
		// and up, drawn in faster as it nears them. Grand, it also spirals round that way.
		Vec Path(int a_index, int a_round, float a_u, Vec a_orb, float a_scale, Vec a_chest, bool a_grand)
		{
			const Vec   toward = Unitised(Sub(a_chest, a_orb));
			const float a = Hash(a_index, a_round * 5 + 2) * kTurn, b = std::acos(2.0f * Hash(a_index, a_round * 5 + 3) - 1.0f);
			const Vec   scatter{ std::sin(b) * std::cos(a), std::sin(b) * std::sin(a), std::cos(b) };
			const Vec   start = Add(a_orb, Mul(Unitised(Add(Mul(toward, 0.7f), Mul(scatter, 0.8f))), kRadius * a_scale * 0.9f));
			const Vec   side = Unitised(Cross(toward, { 0.0f, 0.0f, 1.0f }));
			const Vec   bend = Add(Mul(Add(start, a_chest), 0.5f),
				  Add(Mul(side, 50.0f * (Hash(a_index, a_round * 5 + 4) - 0.5f)), Vec{ 0.0f, 0.0f, 30.0f * Hash(a_index, a_round * 5 + 5) }));
			const float k = std::pow(Unit(a_u), 1.4f);
			const float m = 1.0f - k;
			Vec         at = Add(Add(Mul(start, m * m), Mul(bend, 2.0f * m * k)), Mul(a_chest, k * k));
			if (a_grand) {
				const Vec   lift = Cross(side, toward);
				const float turn = a_u * 2.5f * kTurn + static_cast<float>(a_index);
				at = Add(at, Mul(Add(Mul(side, std::cos(turn)), Mul(lift, std::sin(turn))), 16.0f * std::sin(kPi * Unit(a_u))));
			}
			return at;
		}
	}

	Vec Home(Vec a_mage, float a_heading, float a_age, bool a_grand)
	{
		const auto& home = a_grand ? kGrandHome : kHome;
		const Vec   forward{ std::sin(a_heading), std::cos(a_heading), 0.0f };
		const Vec   right{ std::cos(a_heading), -std::sin(a_heading), 0.0f };
		Vec         at = Add(a_mage, Add(Mul(right, home.x + 4.0f * std::sin(a_age * 0.9f)), Mul(forward, home.y)));
		at.z += home.z + 6.0f * std::sin(a_age * 1.3f);
		return at;
	}

	Vec Follow(Vec a_at, Vec a_home, float a_dt)
	{
		const float k = 1.0f - std::exp(-std::max(a_dt, 0.0f) * 2.3f / kFollow);
		return Add(a_at, Mul(Sub(a_home, a_at), k));
	}

	Look At(float a_age, float a_seconds, bool a_grand)
	{
		Look        look;
		const float goAt = std::max(a_seconds - kGo, kGrow);
		const float going = Ease((a_age - goAt) / kGo);
		look.out = Ease(a_age / kGrow) * (1.0f - going);
		look.scale = std::max(0.0f, (0.15f + 0.85f * Swell(a_age / kGrow)) * (1.0f - 0.85f * going)) * (a_grand ? kGrandSize : 1.0f);
		look.alpha = Ease(a_age / 0.3f) * (1.0f - Ease((a_age - (a_seconds - 0.25f)) / 0.25f));
		look.core = 0.5f + 0.5f * std::sin(a_age * 4.5f);
		look.shell = a_age * 0.35f;
		look.swirl = -a_age * 0.6f;
		for (int i = 0; i < kRings; ++i) {
			const auto& tip = kTips[static_cast<std::size_t>(i)];
			look.rings[static_cast<std::size_t>(i)] = Times(Times(AboutZ(tip[1]), AboutX(tip[0])), AboutZ(a_age * kSpins[static_cast<std::size_t>(i)]));
		}
		// A surge of light as it opens, then a steady glow beating with the core.
		look.light = Unit(look.alpha * (0.75f + 0.25f * look.core) + 0.5f * Ease(a_age / 0.1f) * (1.0f - Ease((a_age - 0.1f) / 0.5f)));
		look.flow = Ease((a_age - kGrow * 0.6f) / 0.4f) * (1.0f - Ease((a_age - (goAt - 0.2f)) / 0.3f));
		look.halo = look.alpha * (a_grand ? 0.3f : 1.0f);
		look.rim = a_grand ? look.alpha : 0.0f;
		if (a_grand) {
			look.coreColor = kRed;
			look.swirlColor = kCrimson;
			look.shellColor = kPurple;
			look.haloColor = kViolet;
			look.rimColor = kEdgeBlue;
			look.ringColor = kViolet;
			look.streamColor = kMagenta;
			look.lightColor = kRose;
		} else {
			look.coreColor = kWhite;
			look.swirlColor = look.shellColor = look.haloColor = look.rimColor = look.ringColor = look.streamColor = look.lightColor = kBlue;
		}
		return look;
	}

	Mote FlowAt(int a_index, float a_age, Vec a_orb, float a_scale, Vec a_chest, float a_flow, bool a_grand)
	{
		const float n = static_cast<float>(a_index);
		const float period = 0.9f + 0.5f * Hash(a_index, 1);
		const float cycle = (a_age + n * 0.37f) / period;
		const float u = cycle - std::floor(cycle);
		Mote        mote;
		mote.at = Path(a_index, static_cast<int>(std::floor(cycle)), u, a_orb, a_scale, a_chest, a_grand);
		mote.size = (0.6f + 0.6f * Hash(a_index, 6)) * (1.0f - 0.6f * u) * (a_grand ? 1.7f : 1.0f);
		mote.alpha = std::pow(std::sin(kPi * u), 0.8f) * Unit(a_flow);
		mote.color = a_grand ? (a_index % 2 ? kPurple : kRed) : kBlue;
		return mote;
	}

	Stream TendrilAt(int a_index, float a_age, Vec a_orb, float a_scale, Vec a_chest, float a_flow, bool a_grand)
	{
		const float n = static_cast<float>(a_index);
		const Vec   to = Sub(a_chest, a_orb);
		const float inside = kRadius * a_scale * 0.75f;
		Stream      stream;
		stream.from = Add(a_orb, Mul(Unitised(to), inside));
		stream.scale = std::max(Length(to) - inside, 1.0f) / kTendrilLength;
		stream.turn = Pointing(to, n * kTurn / kTendrils + a_age * (1.5f + 0.4f * n));
		stream.alpha = a_grand ? 0.0f : Unit(a_flow) * (0.55f + 0.45f * (0.5f + 0.5f * std::sin(a_age * 7.0f + n * 2.0f)));
		return stream;
	}

	Stream VeilAt(int a_index, float a_age, Vec a_orb, float a_scale, Vec a_chest, float a_flow, bool a_grand)
	{
		auto        stream = TendrilAt(a_index, a_age, a_orb, a_scale, a_chest, a_flow, false);
		const float n = static_cast<float>(a_index);
		stream.turn = Pointing(Sub(a_chest, a_orb), n * kTurn / kTendrils - a_age * (0.9f + 0.3f * n));  // slower, and the other way
		stream.alpha = a_grand ? Unit(a_flow) * (0.65f + 0.35f * (0.5f + 0.5f * std::sin(a_age * 4.0f + n * 2.0f))) : 0.0f;
		return stream;
	}

	Wisp WispAt(int a_index, float a_age, Vec a_orb, float a_scale, Vec a_chest, float a_flow, bool a_grand)
	{
		Wisp wisp;
		if (!a_grand) {
			return wisp;
		}
		const int   index = a_index + 100;  // their own scatter, apart from the motes'
		const float n = static_cast<float>(a_index);
		const float period = 1.3f + 0.6f * Hash(index, 1);
		const float cycle = (a_age + n * 0.53f) / period;
		const int   round = static_cast<int>(std::floor(cycle));
		const float u = cycle - std::floor(cycle);
		wisp.at = Path(index, round, u, a_orb, a_scale, a_chest, true);
		wisp.turn = Pointing(Sub(Path(index, round, u + 0.02f, a_orb, a_scale, a_chest, true), Path(index, round, u - 0.02f, a_orb, a_scale, a_chest, true)), 0.0f);
		wisp.size = 1.0f + 0.5f * Hash(index, 6);
		wisp.alpha = std::pow(std::sin(kPi * u), 0.7f) * Unit(a_flow);
		wisp.color = a_index % 3 == 0 ? kViolet : (a_index % 3 == 1 ? kRed : kMagenta);
		return wisp;
	}
}
