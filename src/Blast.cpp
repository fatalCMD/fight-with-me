#include "FWM/Blast.h"

#include <algorithm>
#include <cmath>

namespace FWM::Blast
{
	namespace
	{
		constexpr float kTurn = 6.2831853f;

		// The grand blast's own parts come last.
		constexpr std::array<std::string_view, 63> kFireParts{ "Flash", "Wave", "Dome", "Ground",
			"Plume00", "Plume01", "Plume02", "Plume03", "Plume04", "Plume05", "Plume06",
			"Tongue00", "Tongue01", "Tongue02", "Tongue03", "Tongue04", "Tongue05", "Tongue06", "Tongue07", "Tongue08", "Tongue09",
			"Ember00", "Ember01", "Ember02", "Ember03", "Ember04", "Ember05", "Ember06", "Ember07",
			"Ember08", "Ember09", "Ember10", "Ember11", "Ember12", "Ember13", "Ember14", "Ember15",
			"Burst00", "Burst01", "Burst02", "Burst03", "Burst04", "Burst05",
			"Flare00", "Flare01", "Flare02", "Flare03", "Flare04", "Flare05", "Flare06", "Flare07", "Flare08", "Flare09",
			"Flare10", "Flare11", "Flare12", "Flare13", "Flare14", "Flare15", "Flare16", "Flare17", "Flare18", "Flare19" };
		constexpr std::array<std::string_view, 51> kFrostParts{ "Flash", "Wave", "Dome", "Ground",
			"Shard00", "Shard01", "Shard02", "Shard03", "Shard04", "Shard05", "Shard06", "Shard07", "Shard08",
			"Shard09", "Shard10", "Shard11", "Shard12", "Shard13", "Shard14", "Shard15", "Shard16", "Shard17",
			"Flake00", "Flake01", "Flake02", "Flake03", "Flake04", "Flake05", "Flake06", "Flake07",
			"Flake08", "Flake09", "Flake10", "Flake11", "Flake12", "Flake13", "Flake14", "Flake15",
			"Spire", "Chip00", "Chip01", "Chip02", "Chip03", "Chip04", "Chip05", "Chip06", "Chip07", "Chip08", "Chip09", "Chip10", "Chip11" };
		constexpr std::array<std::string_view, 34> kShockParts{ "Flash", "Wave", "Dome", "Ground",
			"Bolt0", "Bolt1", "Bolt2",
			"Arc00", "Arc01", "Arc02", "Arc03", "Arc04", "Arc05", "Arc06", "Arc07",
			"Spark00", "Spark01", "Spark02", "Spark03", "Spark04", "Spark05", "Spark06", "Spark07",
			"Spark08", "Spark09", "Spark10", "Spark11", "Spark12", "Spark13", "Spark14", "Spark15",
			"Twin0", "Twin1", "Twin2" };
		static_assert(kFireParts.size() <= kMaxParts && kFrostParts.size() <= kMaxParts && kShockParts.size() <= kMaxParts);

		enum Common : std::size_t { kFlash, kWave, kDome, kGround, kOwn };

		float Unit(float a_t) { return std::clamp(a_t, 0.0f, 1.0f); }

		float Ease(float a_t)
		{
			const float t = Unit(a_t);
			return t * t * (3.0f - 2.0f * t);
		}

		// Fast, then settling: a blast throwing something out.
		float Out(float a_t)
		{
			const float t = 1.0f - Unit(a_t);
			return 1.0f - t * t * t;
		}

		// 1 until a_start, then down to 0 over a_length.
		float Fade(float a_t, float a_start, float a_length) { return 1.0f - Ease((a_t - a_start) / a_length); }

		// Out past full size, then back: ice breaking through the ground.
		float Pop(float a_t)
		{
			const float t = Unit(a_t);
			return t < 0.7f ? Ease(t / 0.7f) * 1.15f : 1.15f - 0.15f * Ease((t - 0.7f) / 0.3f);
		}

		// The same scatter every time for the same numbers, so a blast looks alike in the game, the
		// tests and the preview: 0..1.
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

		struct Stop
		{
			float at;
			Color color;
		};
		constexpr std::array<Stop, 4> kFireHeat{ { { 0.0f, { 0.55f, 0.08f, 0.02f } }, { 0.4f, { 1.0f, 0.4f, 0.08f } }, { 0.7f, { 1.0f, 0.7f, 0.28f } },
			{ 1.0f, { 1.0f, 0.93f, 0.78f } } } };
		constexpr std::array<Stop, 4> kFrostHeat{ { { 0.0f, { 0.25f, 0.5f, 1.0f } }, { 0.6f, { 0.55f, 0.85f, 1.0f } }, { 0.85f, { 0.75f, 0.93f, 1.0f } },
			{ 1.0f, { 0.94f, 0.98f, 1.0f } } } };
		constexpr std::array<Stop, 4> kShockHeat{ { { 0.0f, { 0.6f, 0.38f, 1.0f } }, { 0.6f, { 0.55f, 0.7f, 1.0f } }, { 0.85f, { 0.75f, 0.84f, 1.0f } },
			{ 1.0f, { 0.95f, 0.96f, 1.0f } } } };

		// Each element's colours from cool (0) to white hot (1).
		Color Heat(Element a_element, float a_h)
		{
			const auto& stops = a_element == Element::kFire ? kFireHeat : (a_element == Element::kFrost ? kFrostHeat : kShockHeat);
			const float h = Unit(a_h);
			for (std::size_t i = 1; i < stops.size(); ++i) {
				if (h <= stops[i].at) {
					const float k = (h - stops[i - 1].at) / (stops[i].at - stops[i - 1].at);
					Color out{};
					for (std::size_t c = 0; c < 3; ++c) {
						out[c] = stops[i - 1].color[c] + (stops[i].color[c] - stops[i - 1].color[c]) * k;
					}
					return out;
				}
			}
			return stops.back().color;
		}

		Part Ring(Element a_element, float a_scale, float a_alpha, float a_heat, float a_z = 0.0f)
		{
			Part part;
			part.at = { 0.0f, 0.0f, a_z };
			part.scale = a_scale;
			part.alpha = a_alpha;
			part.color = Heat(a_element, a_heat);
			return part;
		}

		// A ball of flame that swells and rises out of the middle, then cools and fades; tongues of fire
		// bursting outward; embers thrown out in arcs that settle on the ground; a glowing scorch. Grand:
		// the fireball giant, smaller explosions going off round it, sparks flying up over it.
		void Fire(float a_t, Look& a_look, bool a_grand)
		{
			constexpr auto e = Element::kFire;
			const float    t = a_t;
			auto&          p = a_look.parts;
			p[kFlash] = Ring(e, (a_grand ? 1.35f : 1.0f) * (0.25f + 0.95f * Out(t / 0.08f) + 0.5f * t), Fade(t, 0.06f, 0.34f), 1.0f - 2.0f * t, 40.0f);
			p[kWave] = Ring(e, Out(t / 0.35f), Fade(t, 0.12f, 0.38f), 0.65f, 2.0f);
			p[kDome] = Ring(e, 0.3f + 0.85f * Out(t / 0.25f), 0.55f * Ease(t / 0.03f) * Fade(t, 0.1f, 0.5f), 0.6f - 0.7f * t);
			p[kGround] = Ring(e, 0.3f + 0.7f * Out(t / 0.25f), Ease(t / 0.08f) * Fade(t, 0.9f, 1.4f), 0.45f - 0.35f * Ease(t / 2.0f));
			std::size_t i = kOwn;
			for (int k = 0; k < 7; ++k, ++i) {
				const float angle = k == 0 ? 0.0f : static_cast<float>(k - 1) * (kTurn / 6.0f) + 0.3f * Hash(k, 1);
				const float reach = k == 0 ? 0.0f : 45.0f + 25.0f * Hash(k, 2);
				const float rise = k == 0 ? (a_grand ? 280.0f : 190.0f) : 70.0f + 60.0f * Hash(k, 3);
				const float grow = Out(t / 0.45f);
				const float size = k == 0 ? (a_grand ? 1.95f : 1.15f) : (0.8f + 0.25f * Hash(k, 4)) * (a_grand ? 1.15f : 1.0f);
				auto&       plume = p[i];
				plume.at = { std::cos(angle) * reach * grow, std::sin(angle) * reach * grow, 35.0f + rise * Out(t / 1.3f) };
				plume.yaw = angle + t * (k % 2 ? 0.9f : -0.9f);  // churning as it climbs
				plume.scale = size * (0.35f + 0.75f * grow + 0.2f * t);
				plume.alpha = Ease(t / 0.03f) * Fade(t, 0.55f + 0.15f * Hash(k, 5), 1.1f);
				plume.color = Heat(e, 0.85f - t / 1.8f - 0.1f * Hash(k, 6));  // still orange while it shows: dark red added over the sky reads pink
			}
			for (int k = 0; k < 10; ++k, ++i) {
				const float angle = static_cast<float>(k) * (kTurn / 10.0f) + 0.35f * (Hash(k, 7) - 0.5f);
				const float u = t - 0.03f * Hash(k, 8);
				auto&       tongue = p[i];
				tongue.at = { std::cos(angle) * 14.0f, std::sin(angle) * 14.0f, 0.0f };
				tongue.yaw = angle;
				tongue.tilt = 0.55f + 0.35f * Hash(k, 9) + 0.25f * Out(u / 0.5f);  // bending further out as it goes
				tongue.scale = u > 0.0f ? (0.8f + 0.4f * Hash(k, 10)) * Out(u / 0.11f) * (1.0f - 0.25f * Ease((u - 0.3f) / 0.6f)) : 0.0f;
				tongue.alpha = u > 0.0f ? Fade(u, 0.25f, 0.6f) : 0.0f;
				tongue.color = Heat(e, 0.7f - 0.8f * u);
			}
			for (int k = 0; k < 16; ++k, ++i) {
				constexpr float kGravity = 1100.0f, kFrom = 30.0f, kLow = 3.0f;
				const float     angle = static_cast<float>(k) * (kTurn / 16.0f) + 0.4f * (Hash(k, 11) - 0.5f);
				const float     across = 220.0f + 380.0f * Hash(k, 12);
				const float     up = 280.0f + 420.0f * Hash(k, 13);
				const float     life = 1.1f + 0.8f * Hash(k, 14);
				const float     landed = (up + std::sqrt(up * up + 2.0f * kGravity * (kFrom - kLow))) / kGravity;
				const float     flying = std::min(t, landed);
				const float     reach = 20.0f + across * flying;
				auto&           ember = p[i];
				ember.at = { std::cos(angle) * reach, std::sin(angle) * reach, std::max(kFrom + up * flying - 0.5f * kGravity * flying * flying, kLow) };
				ember.scale = (0.7f + 0.6f * Hash(k, 15)) * (1.0f - 0.5f * Unit(t / life));
				ember.alpha = Ease(t / 0.02f) * Fade(t, life * 0.5f, life * 0.5f);
				ember.color = Heat(e, 0.7f - 0.6f * t / life);
			}
			a_look.light = Out(t / 0.04f) * Fade(t, 0.1f, a_grand ? 1.6f : 1.2f) * (0.88f + 0.12f * std::sin(t * 47.0f));
			a_look.lightColor = { 1.0f, 0.55f, 0.2f };
			if (!a_grand) {
				return;
			}
			// Smaller explosions round the fireball, going off one after another but not in turn.
			constexpr std::array<int, 6> kOrder{ 0, 3, 1, 4, 2, 5 };
			for (int k = 0; k < 6; ++k, ++i) {
				const float angle = (static_cast<float>(k) + 0.5f) * (kTurn / 6.0f) + 0.3f * (Hash(k, 40) - 0.5f);
				const float reach = 135.0f + 50.0f * Hash(k, 41);
				const float u = t - 0.1f - 0.075f * static_cast<float>(kOrder[static_cast<std::size_t>(k)]);
				auto&       burst = p[i];
				burst.at = { std::cos(angle) * reach, std::sin(angle) * reach, 25.0f + 70.0f * Out(u / 0.7f) };
				burst.yaw = angle + u * 1.5f;
				burst.scale = u > 0.0f ? (0.75f + 0.35f * Hash(k, 42)) * (0.3f + 0.8f * Out(u / 0.12f) + 0.3f * u) : 0.0f;
				burst.alpha = u > 0.0f ? Ease(u / 0.02f) * Fade(u, 0.25f, 0.7f) : 0.0f;
				burst.color = Heat(e, 0.9f - 1.2f * u);
			}
			// Sparks fountaining up off the top of the fireball and falling away.
			for (int k = 0; k < 20; ++k, ++i) {
				constexpr float kGravity = 950.0f;
				const float     angle = static_cast<float>(k) * (kTurn / 20.0f) + 0.5f * (Hash(k, 43) - 0.5f);
				const float     u = t - 0.06f - 0.3f * Hash(k, 44);
				const float     flying = std::max(u, 0.0f);
				const float     across = 120.0f + 260.0f * Hash(k, 45);
				const float     up = 450.0f + 350.0f * Hash(k, 46);
				const float     from = 140.0f + 60.0f * Hash(k, 47);
				const float     life = 1.2f + 0.7f * Hash(k, 48);
				const float     reach = 20.0f + across * flying;
				auto&           flare = p[i];
				flare.at = { std::cos(angle) * reach, std::sin(angle) * reach, std::max(from + up * flying - 0.5f * kGravity * flying * flying, 3.0f) };
				flare.yaw = angle;
				flare.tilt = -std::atan2(up - kGravity * flying, across);  // along its flight
				flare.scale = 1.0f;
				flare.alpha = u > 0.0f ? Ease(u / 0.03f) * Fade(u, life * 0.55f, life * 0.45f) : 0.0f;
				flare.color = Heat(e, 0.95f - 0.55f * flying / life);
			}
		}

		// Frost clinging to the plates, moving more slowly than the ice's emergence and shatter.
		void FrostAura(Part& a_part, float a_real, float a_phase, float a_formed, bool a_grand = false)
		{
			const float breath = 0.8f + 0.15f * std::sin(a_real * 2.6f + a_phase);
			a_part.auraAlpha = a_part.alpha * Ease(a_formed / 0.18f) * breath;
			a_part.auraLift = (a_grand ? 11.0f : 6.5f) + (a_grand ? 5.0f : 3.5f) * std::sin(a_real * 1.8f + a_phase + 0.7f);
			a_part.auraYaw = 0.04f * std::sin(a_real * 1.6f + a_phase);
		}

		// A wave of frost racing out over the ground, ice breaking up through it as it passes, round
		// their feet and in a wide ring; a mist wall rolling out; then the ice shatters and sinks. Grand: a
		// huge crystal bursts up in the middle, and shatters last, throwing chunks of ice.
		void Frost(float a_t, Look& a_look, bool a_grand, float a_real)
		{
			constexpr auto e = Element::kFrost;
			const float    t = a_t;
			auto&          p = a_look.parts;
			p[kFlash] = Ring(e, 0.25f + 0.85f * Out(t / 0.07f), Fade(t, 0.04f, 0.26f), 0.9f, 35.0f);
			p[kWave] = Ring(e, Out(t / 0.42f), Fade(t, 0.18f, 0.4f), 0.62f, 2.0f);
			p[kDome] = Ring(e, 0.15f + 0.9f * Out(t / 0.5f), 0.75f * Ease(t / 0.05f) * Fade(t, 0.1f, 0.7f), 0.6f);
			p[kGround] = Ring(e, 0.15f + 0.85f * Out(t / 0.38f), Ease(t / 0.1f) * Fade(t, 1.25f, 0.75f), 0.5f);
			std::size_t i = kOwn;
			for (int k = 0; k < 18; ++k, ++i) {
				const bool  inner = k < 6;
				const int   j = inner ? k : k - 6;
				const float angle = inner ? static_cast<float>(j) * (kTurn / 6.0f) + 0.3f * Hash(k, 16)
				                          : static_cast<float>(j) * (kTurn / 12.0f) + 0.25f * (Hash(k, 16) - 0.5f);
				const float reach = inner ? 48.0f + 10.0f * Hash(k, 17) : (j % 2 ? 195.0f : 150.0f) + 25.0f * Hash(k, 17);
				const float size = inner ? 0.6f + 0.2f * Hash(k, 18) : 0.85f + 0.4f * Hash(k, 18);
				const float u = t - reach / kMeshRadius * 0.24f;  // the frost wave reaching it
				const float shatter = Unit((t - 1.15f - 0.25f * Hash(k, 19)) / 0.22f);
				auto&       shard = p[i];
				shard.at = { std::cos(angle) * reach, std::sin(angle) * reach, -12.0f * Ease(shatter) };
				shard.yaw = angle + 0.3f * (Hash(k, 20) - 0.5f);
				shard.tilt = inner ? 0.45f + 0.2f * Hash(k, 21) : 0.25f + 0.25f * Hash(k, 21);
				shard.scale = u > 0.0f ? size * Pop(u / 0.09f) * (1.0f - Ease(shatter)) : 0.0f;
				shard.alpha = u > 0.0f ? (1.0f - Ease(shatter)) * (0.85f + 0.15f * std::sin(t * 9.0f + static_cast<float>(k))) : 0.0f;
				shard.color = Heat(e, 0.5f + 0.45f * Fade(u, 0.0f, 0.25f));  // white as it forms, then ice blue
				FrostAura(shard, a_real, kTurn * Hash(k, 72), u);
			}
			for (int k = 0; k < 16; ++k, ++i) {
				const float n = static_cast<float>(k);
				const float u = t - 0.12f - 0.25f * Hash(k, 22);
				const float life = 1.3f + 0.4f * Hash(k, 23);
				const float angle = n * (kTurn / 16.0f) + Hash(k, 24) + 0.6f * u;
				const float reach = 40.0f + 160.0f * Hash(k, 25) + 60.0f * std::max(u, 0.0f);
				auto&       flake = p[i];
				flake.at = { std::cos(angle) * reach, std::sin(angle) * reach, 10.0f + 110.0f * Out(u / life) + 8.0f * std::sin(u * 6.0f + n) };
				flake.yaw = u * 3.0f + n;
				flake.scale = (0.6f + 0.6f * Hash(k, 26)) * (0.8f + 0.2f * std::sin(t * 7.0f + n));
				flake.alpha = u > 0.0f ? Ease(u / 0.15f) * Fade(u, life * 0.6f, life * 0.4f) * (0.7f + 0.3f * std::sin(t * 11.0f + n * 1.7f)) : 0.0f;
				flake.color = Heat(e, 0.7f);
			}
			a_look.light = Out(t / 0.05f) * Fade(t, 0.05f, 0.75f);
			a_look.lightColor = { 0.55f, 0.8f, 1.0f };
			if (!a_grand) {
				return;
			}
			constexpr float kBreak = 1.5f;  // when the great crystal shatters
			{
				const float u = t - 0.02f;
				const float shatter = Unit((t - kBreak) / 0.25f);
				auto&       spire = p[i++];
				spire.at = { 0.0f, 0.0f, -6.0f - 30.0f * Ease(shatter) };
				spire.yaw = 0.4f;
				spire.scale = u > 0.0f ? Pop(u / 0.16f) * (1.0f - 0.6f * Ease(shatter)) : 0.0f;
				spire.alpha = u > 0.0f ? (1.0f - Ease(shatter)) * (0.9f + 0.1f * std::sin(t * 6.0f)) : 0.0f;
				spire.color = Heat(e, 0.5f + 0.45f * Fade(u, 0.05f, 0.3f));
				FrostAura(spire, a_real, 0.65f, u, true);
			}
			for (int k = 0; k < 12; ++k, ++i) {
				constexpr float kGravity = 1300.0f;
				const float     u = t - kBreak;
				const float     flying = std::max(u, 0.0f);
				const float     angle = static_cast<float>(k) * (kTurn / 12.0f) + 0.4f * (Hash(k, 50) - 0.5f);
				const float     height = 40.0f + 260.0f * Hash(k, 51);
				const float     across = 180.0f + 260.0f * Hash(k, 52);
				const float     up = 120.0f + 320.0f * Hash(k, 53);
				const float     life = 0.6f + 0.3f * Hash(k, 54);
				const float     reach = 20.0f + across * flying;
				auto&           chip = p[i];
				chip.at = { std::cos(angle) * reach, std::sin(angle) * reach, std::max(height + up * flying - 0.5f * kGravity * flying * flying, 2.0f) };
				chip.yaw = angle + 7.0f * flying;  // tumbling
				chip.tilt = 5.0f * flying + static_cast<float>(k);
				chip.scale = 0.7f + 0.6f * Hash(k, 55);
				chip.alpha = u > 0.0f ? Fade(u, life * 0.5f, life * 0.5f) : 0.0f;
				chip.color = Heat(e, 0.8f);
			}
			a_look.light = std::max(a_look.light, 0.4f * Ease((t - kBreak) / 0.04f) * Fade(t, kBreak + 0.05f, 0.35f));  // a glint as it shatters
		}

		// How strongly lightning strikes a_t into its timeline, and down which of its three channels: the
		// first strike holds a moment as it dims, re-flashing once down a new channel partway, as lightning
		// does; each restrike (a_lag later for a grand blast's second bolt) flashes and is gone. Restrikes
		// alone are added to a_again, for what pulses with them.
		float Strike(float a_t, float a_lag, int& a_channel, float& a_again)
		{
			if (a_t >= 0.0f && a_t <= 0.3f) {
				a_channel = a_t < 0.12f ? 0 : 2;
				return 1.0f - 0.6f * a_t / 0.3f;
			}
			for (std::size_t n = 0; n < kRestrikes.size(); ++n) {
				const float v = a_t - kRestrikes[n] - a_lag;
				if (v >= 0.0f && v <= kRestrikeLength) {
					constexpr std::array<int, 3> kChannel{ 1, 0, 2 };
					a_channel = kChannel[n];
					const float strength = kRestrikeStrength[n] * (1.0f - v / kRestrikeLength);
					a_again += strength;
					return strength;
				}
			}
			a_channel = -1;
			return 0.0f;
		}

		// Lightning out of the sky onto the spot, striking again and again; arcs crawling out over the
		// ground; a flickering ball of plasma; sparks spraying out. Grand: a second thunderbolt strikes with
		// the first, coming in at an angle to meet it on the spot, and strikes again just after it each time.
		void Shock(float a_t, Look& a_look, bool a_grand, float a_real)
		{
			constexpr auto e = Element::kShock;
			const float    t = a_t;
			const int      frame = static_cast<int>(std::floor(a_real * 30.0f));  // the flicker changes thirty times a second, however long it lingers
			int            bolt = -1, other = -1;
			float          again = 0.0f;
			const float    strike = Strike(t, 0.0f, bolt, again);
			const float    twin = a_grand ? Strike(t, kTwinLag, other, again) : 0.0f;
			const float    last = kRestrikes.back() + (a_grand ? kTwinLag : 0.0f);  // the final strike
			auto&          p = a_look.parts;
			p[kFlash] = Ring(e, (a_grand ? 1.25f : 1.0f) * (0.3f + 0.8f * Out(t / 0.05f)), Fade(t, 0.05f, 0.3f) * (0.7f + 0.3f * Hash(frame, 23)) + again, 0.85f, 45.0f);
			p[kWave] = Ring(e, Out(t / 0.22f), Fade(t, 0.08f, 0.3f), 0.7f, 2.0f);
			p[kDome] = Ring(e, 0.4f + 0.6f * Out(t / 0.12f) + 0.1f * std::sin(t * 30.0f),
				(0.32f * Ease(t / 0.02f) * Fade(t, last - 0.3f, 0.5f) + 0.4f * again) * (0.6f + 0.4f * Hash(frame, 24)), 0.4f);
			p[kGround] = Ring(e, 0.4f + 0.6f * Out(t / 0.15f), Ease(t / 0.04f) * Fade(t, last, 0.8f), 0.45f - 0.15f * t);
			std::size_t i = kOwn;
			// One bolt at a time, flickering only in brightness.
			for (int k = 0; k < 3; ++k, ++i) {
				auto& part = p[i];
				part.faces = true;
				part.alpha = k == bolt && strike > 0.0f ? strike * (0.7f + 0.3f * Hash(frame, 22)) : 0.0f;
				part.scale = part.alpha > 0.0f ? 1.0f : 0.0f;
				part.color = Heat(e, 0.45f);  // blue: the core is bright enough to burn white
			}
			for (int k = 0; k < 8; ++k, ++i) {
				const int   hop = static_cast<int>(std::floor(a_real / 0.09f));  // the arcs jump about as they crawl
				const float lit = Hash(k * 17 + frame, 26) > 0.35f ? 1.0f : 0.5f;
				auto&       arc = p[i];
				arc.at = { 0.0f, 0.0f, 6.0f };
				arc.faces = true;
				arc.tilt = kTurn / 4.0f;  // laid along the ground, out from the middle
				arc.yaw = static_cast<float>(k) * (kTurn / 8.0f) + 0.6f * (Hash(k * 31 + hop, 25) - 0.5f);
				arc.scale = (0.75f + 0.3f * Hash(k, 27)) * Out(t / 0.14f);
				arc.alpha = lit * Ease(t / 0.03f) * Fade(t, last - 0.1f, 0.6f);
				arc.color = Heat(e, 0.3f + 0.25f * lit);
			}
			for (int k = 0; k < 16; ++k, ++i) {
				constexpr float kGravity = 900.0f;
				const float     angle = static_cast<float>(k) * (kTurn / 16.0f) + 0.5f * (Hash(k, 29) - 0.5f);
				const float     speed = 700.0f + 900.0f * Hash(k, 30);
				const float     up = 150.0f + 350.0f * Hash(k, 31);
				const float     life = 0.3f + 0.35f * Hash(k, 32);
				const float     u = t - 0.02f * Hash(k, 33);
				const float     flying = std::max(u, 0.0f);
				auto&           spark = p[i];
				spark.at = { std::cos(angle) * speed * flying, std::sin(angle) * speed * flying, std::max(25.0f + up * flying - 0.5f * kGravity * flying * flying, 2.0f) };
				spark.yaw = angle;
				spark.tilt = -std::atan2(up - kGravity * flying, speed);  // along its flight
				spark.scale = 1.0f;
				spark.alpha = u > 0.0f ? Fade(u, life * 0.4f, life * 0.6f) : 0.0f;
				spark.color = Heat(e, 0.8f - 0.4f * flying / life);
			}
			a_look.light = std::max({ strike, twin, 0.35f * Fade(t, 0.1f, last) }) * (0.6f + 0.4f * Hash(frame, 28)) * Fade(t, last + kRestrikeLength, 0.15f);
			a_look.lightColor = { 0.6f, 0.7f, 1.0f };
			if (!a_grand) {
				return;
			}
			for (int k = 0; k < 3; ++k, ++i) {
				auto& part = p[i];
				part.faces = true;
				part.yaw = static_cast<float>(k) * 2.09f + 1.05f;  // its top off to one side of the first's,
				part.tilt = 0.358f;                                // slanting in to meet it on the spot
				part.alpha = k == other && twin > 0.0f ? twin * (0.7f + 0.3f * Hash(frame, 42)) : 0.0f;
				part.scale = part.alpha > 0.0f ? 1.0f : 0.0f;
				part.color = Heat(e, 0.4f);
			}
		}
	}

	float Real(float a_timeline) { return a_timeline <= kSnap ? a_timeline : kSnap + (a_timeline - kSnap) * kLinger; }

	float Seconds(Element a_element, bool a_grand)
	{
		switch (a_element) {
		case Element::kFrost:
			return Real(a_grand ? 2.45f : 2.1f);
		case Element::kShock:
			return Real(a_grand ? 2.1f : 2.0f);
		default:
			return Real(a_grand ? 2.7f : 2.5f);
		}
	}

	std::span<const std::string_view> Parts(Element a_element)
	{
		switch (a_element) {
		case Element::kFrost:
			return kFrostParts;
		case Element::kShock:
			return kShockParts;
		default:
			return kFireParts;
		}
	}

	Look At(Element a_element, float a_age, bool a_grand)
	{
		Look look;
		if (a_age < 0.0f) {
			return look;
		}
		// Its own timeline: the first moment as it comes, the rest stretched so it lingers.
		const float t = a_age <= kSnap ? a_age : kSnap + (a_age - kSnap) / kLinger;
		switch (a_element) {
		case Element::kFrost:
			Frost(t, look, a_grand, a_age);
			break;
		case Element::kShock:
			Shock(t, look, a_grand, a_age);
			break;
		default:
			Fire(t, look, a_grand);
			break;
		}
		for (std::size_t i=0; i<look.parts.size(); ++i) {
			auto& part = look.parts[i];
			// Give grand plumes, ice and bolts height and detail; preserve the ground footprint.
			if (a_grand && i >= kOwn) { part.scale *= 1.18f; part.at.z *= 1.15f; }
			part.scale = std::max(part.scale, 0.0f);
			part.alpha = Unit(part.alpha);
			part.auraAlpha = Unit(part.auraAlpha);
		}
		look.light = Unit(look.light);
		return look;
	}

	Mat Facing(const Part& a_part, Vec a_toCamera)
	{
		const float ct = std::cos(a_part.tilt), st = std::sin(a_part.tilt);
		const Vec   axis{ std::cos(a_part.yaw) * st, std::sin(a_part.yaw) * st, ct };
		// The camera's direction with the part's length taken out of it: as square to the camera as it can turn.
		const float along = a_toCamera.x * axis.x + a_toCamera.y * axis.y + a_toCamera.z * axis.z;
		Vec         face{ a_toCamera.x - axis.x * along, a_toCamera.y - axis.y * along, a_toCamera.z - axis.z * along };
		float       length = std::sqrt(face.x * face.x + face.y * face.y + face.z * face.z);
		if (length < 1e-4f) {
			// Looking straight along it: any face will do.
			face = std::abs(axis.z) < 0.9f ? Vec{ -axis.y, axis.x, 0.0f } : Vec{ 0.0f, axis.z, -axis.y };
			length = std::sqrt(face.x * face.x + face.y * face.y + face.z * face.z);
		}
		face = { face.x / length, face.y / length, face.z / length };
		const Vec side{ face.y * axis.z - face.z * axis.y, face.z * axis.x - face.x * axis.z, face.x * axis.y - face.y * axis.x };
		return { { { side.x, face.x, axis.x }, { side.y, face.y, axis.y }, { side.z, face.z, axis.z } } };
	}

	float Flight(float a_distance) { return std::clamp(a_distance / kSpeed, kFlightShortest, kFlightLongest); }

	Orb OrbAt(Element a_element, Vec a_from, Vec a_to, float a_u)
	{
		const float dx = a_to.x - a_from.x, dy = a_to.y - a_from.y, dz = a_to.z - a_from.z;
		const float arc = std::min(std::sqrt(dx * dx + dy * dy + dz * dz) * 0.1f, 70.0f);
		auto        where = [&](float a_v) {
            const float k = std::pow(Unit(a_v), 1.25f);  // speeding up as it goes
            return Vec{ a_from.x + dx * k, a_from.y + dy * k, a_from.z + dz * k + arc * 4.0f * k * (1.0f - k) };
		};
		const float u = Unit(a_u);
		const Vec   ahead = where(u + 0.01f), behind = where(u - 0.01f);
		const float vx = ahead.x - behind.x, vy = ahead.y - behind.y, vz = ahead.z - behind.z;
		Orb         orb;
		orb.at = where(u);
		orb.yaw = std::atan2(vy, vx);
		orb.tilt = std::atan2(-vz, std::hypot(vx, vy));
		orb.alpha = Ease(u / 0.12f);
		orb.light = 0.35f * orb.alpha;
		orb.color = Heat(a_element, 0.7f);
		return orb;
	}

	float Share(float a_distance, float a_radius)
	{
		if (a_radius <= 0.0f) {
			return 0.0f;
		}
		const float k = Unit((a_distance / a_radius - kCentre) / (1.0f - kCentre));
		return 1.0f - (1.0f - kEdge) * k;
	}
}
