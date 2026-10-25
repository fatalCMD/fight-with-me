#pragma once

#include <array>
#include <cmath>
#include <span>

// The healer's circle as numbers: how long it stands, how big it is, how it looks over its life,
// and where it goes. No game types: the plugin raises the real one with it, the tests check it,
// and tools/wolfhead/preview plays the same frames in a browser.
//
// Its mesh (meshes\FightWithMe\HealingCircle.nif, from tools/healcircle/BuildHealCircle.js) is
// kMeshRadius across in its own units, lies flat on the ground, with a ring of runes that turns, two
// wavy aura lines round its edge that turn each their own way, and a node per mote.

namespace FWM::HealCircle
{
	inline constexpr float kSeconds = 12.0f;      // how long it stands
	inline constexpr float kRadius = 280.0f;      // world units, before Wide Circle
	inline constexpr float kMeshRadius = 100.0f;  // the mesh's own
	inline constexpr int   kMotes = 24;          // glowing balls drifting up round the edge
	inline constexpr int   kGlints = 32;         // Wide Circle III: specks of light rising all over it
	inline constexpr float kRise = 0.6f;          // the aura takes this long to rise out of the ground
	inline constexpr float kSink = 2.4f;          // and this long to sink back at the end
	inline constexpr float kAuraSunk = 22.0f;     // how far under the ground (mesh units) it rises from and sinks to
	inline constexpr float kAuraFlow = 0.12f;     // the textured wisps drifting up through each pulse
	inline constexpr float kPullBelow = 0.5f;     // companions under this share of health walk into it
	inline constexpr float kPullUntil = 0.9f;     // and stay until they are back to this

    // Only choose circles on a usable floor with enough time to reach them.
    inline bool Seek(float health, bool returning, float distance, float height, float remaining, bool sameSpace) {
        return sameSpace && std::isfinite(health) && health > 0 && health < (returning ? kPullUntil : kPullBelow) &&
            distance <= 2500.0f && std::abs(height) <= 250.0f && remaining > 2.0f &&
            (returning || remaining > distance / 250.0f + 2.0f);
    }


	// One glowing ball: where it is in the mesh's own units (the circle's middle on the ground at
	// the origin), how big against its mesh, and how strongly it shows.
	struct Mote
	{
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };
		float size{ 1.0f };
		float alpha{ 0.0f };
	};

	// How it looks at a_age seconds into a circle that stands a_seconds.
	struct Look
	{
		float ring{ 0.0f };   // the ring's and the floor glow's strength, 0..1
		float turn{ 0.0f };   // the runes' slow turn, radians
		float pulse{ 0.0f };  // a gentle beat on top, 0..1
		float aura{ 0.0f };                  // the aura round the edge: 0 sunk and unseen, 1 standing (a little over as it surges up)
		std::array<float, 2> waves{};        // the turn of its two wavy lines, radians, one each way
		std::array<float, 2> edgeHeight{};   // rising pulses, in mesh units
		std::array<float, 2> edgeAlpha{};    // each pulse dissolves as it rises
		std::array<Mote, kMotes>   motes{};

		// Wide Circle III's own, nothing unless it is grand: a smaller circle within, turning against the
		// runes; a white glow fading out from the middle; and specks of light rising all over it.
		float                     inner{ 0.0f };
		float                     innerTurn{ 0.0f };
		float                     heart{ 0.0f };
		std::array<Mote, kGlints> glints{};
	};
	[[nodiscard]] Look At(float a_age, float a_seconds, bool a_grand = false);

	// Someone the circle could be raised for: where they stand, how hurt, and how much they count.
	struct Ally
	{
		float x{ 0.0f };
		float y{ 0.0f };
		float health{ 1.0f };  // 0..1
		bool  player{ false };
		bool  down{ false };
	};

	// Where to raise it so it covers the most need: each ally's missing health, the player's and the
	// downed counting double, summed over those inside. Centred on one of the hurt, the player first
	// on a tie. Returns the index of the ally to centre on, or -1 when nobody is hurt.
	[[nodiscard]] int Place(std::span<const Ally> a_allies, float a_radius);
}
