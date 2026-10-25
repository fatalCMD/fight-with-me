#pragma once

#include <array>

// Kynareth's Maw's hold as numbers: the ghostly smoke that bursts out of whoever its bite catches
// and clings to them while they stand frozen mid-stagger, a dark mist round them lit by a ghost's glow,
// wisps curling up off them and a fog at their feet, thinning away once they are let go. Blue for the
// first ranks, red and stronger for the third. No game types: the plugin poses the real mesh with it,
// the tests check it, and tools/wolfhead/preview plays the same frames.
//
// Its mesh (meshes\FightWithMe\Haunt.nif, from tools/wolfhead/BuildHaunt.js) is in game units for
// someone kHeight tall: "Haunt0".."Haunt5", one for each caught by a bite, each holding "HauntNMist"
// (smoke laid over them, not glowing), "HauntNGlow", "HauntNGround" (fog, laid over them too),
// "HauntNRing", "HauntNFlare" and "HauntNWisp0".."HauntNWisp7".

namespace FWM::Haunt
{
	using Color = std::array<float, 3>;

	inline constexpr float kHeight = 130.0f;  // the mesh fits someone this tall
	inline constexpr int   kSlots = 6;        // caught by one bite at once
	inline constexpr int   kWisps = 8;
	inline constexpr float kBurst = 0.25f;    // the smoke bursts out of them this long
	inline constexpr float kLinger = 0.6f;    // and thins away this long once they are let go
	inline constexpr float kRise = 0.45f;     // it drifts up them, texture lengths a second
	inline constexpr float kGrandStrength = 1.35f;

	inline constexpr Color kBlue{ 0.42f, 0.62f, 1.0f };
	inline constexpr Color kBlueSmoke{ 0.05f, 0.07f, 0.13f };
	inline constexpr Color kRed{ 1.0f, 0.16f, 0.1f };
	inline constexpr Color kRedSmoke{ 0.13f, 0.02f, 0.02f };

	// The glow's colour and the smoke's, for the third rank (grand) or the others.
	[[nodiscard]] constexpr const Color& GlowOf(bool a_grand) { return a_grand ? kRed : kBlue; }
	[[nodiscard]] constexpr const Color& SmokeOf(bool a_grand) { return a_grand ? kRedSmoke : kBlueSmoke; }

	// How long it shows on someone held a_held seconds.
	[[nodiscard]] constexpr float Seconds(float a_held) { return a_held + kLinger; }

	struct Wisp
	{
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };
		float size{ 0.0f };
		float alpha{ 0.0f };
	};
	struct Look
	{
		float mist{ 0.0f };   // the dark smoke's strength (it lays over them)
		float glow{ 0.0f };   // the ghost's glow round them
		float ground{ 0.0f };
		float flare{ 0.0f };  // the light bursting out as they are caught
		float scale{ 0.0f };  // its size round them: it swells out of them, then clings
		std::array<Wisp, kWisps> wisps{};
	};

	// The smoke a_since seconds after the bite caught someone it holds a_held seconds.
	[[nodiscard]] Look At(float a_since, float a_held, bool a_grand = false);

	// The scale to draw it at for someone a_height tall.
	[[nodiscard]] float SizeFor(float a_height);

	// It leans with them, caught bent back mid-stagger, along a_up (their feet to their head), but no
	// further than this: a creature's head is no line up its body.
	inline constexpr float kMostLean = 0.5f;
	using Turn = std::array<std::array<float, 3>, 3>;
	[[nodiscard]] Turn Lean(float a_x, float a_y, float a_z);
}
