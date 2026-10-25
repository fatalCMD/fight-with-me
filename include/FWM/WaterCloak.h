#pragma once

#include <array>

// Renewal as numbers: the cloak of water the healer wears while it lasts, pouring from a swirl over
// their head down a clear sheath round them and spiralling streams into a rippling pool at their feet;
// and, with every pulse, the wash of the same water running down each one it heals, head to foot. No
// game types: the plugin poses the real mesh with it, the tests check it, and tools/wolfhead/preview
// plays the same frames.
//
// Its mesh (meshes\FightWithMe\WaterCloak.nif, from tools/watercloak/BuildWaterCloak.js) is in game
// units for someone kHeight tall standing at its origin: "Cloak" holding "Veil" (the sheath), "Inner"
// (light flowing through them), "Streams", "Crown", "Gleam" (the third rank's glow over the head),
// "Pool", "Ripple0".."Ripple2" and "Drop00".."Drop09";
// and "Wash00".."Wash07", each holding "Band" (a ring of water around them, its middle at the node) and
// "Ring" (a ripple at their feet), as "WashNNBand" and "WashNNRing".

namespace FWM::WaterCloak
{
	using Color = std::array<float, 3>;

	inline constexpr float kHeight = 130.0f;      // the mesh fits someone this tall
	inline constexpr int   kRipples = 3;
	inline constexpr int   kDrops = 10;
	inline constexpr int   kWashes = 8;           // at most this many washed at once
	inline constexpr float kComeIn = 0.6f;        // the cloak forms this long
	inline constexpr float kGoOut = 2.0f;         // and falls away this long at the end
	inline constexpr float kFlow = 1.1f;          // the sheath's water runs down this many texture lengths a second
	inline constexpr float kStreamFlow = 2.4f;    // and the streams'
	inline constexpr float kWashRun = 0.8f;       // a wash runs head to foot this long
	inline constexpr float kWashSeconds = 1.5f;   // and is gone, ripple and all, by this
	inline constexpr float kLightReach = 300.0f;
	inline constexpr float kLightFade = 0.9f;     // its light at full strength (a torch is about 1)
	inline constexpr float kGrandSize = 1.24f;    // the third rank's cloak, against the others

	inline constexpr Color kWater{ 0.5f, 0.82f, 1.0f };
	inline constexpr Color kFoam{ 0.82f, 0.96f, 1.0f };
	inline constexpr Color kGold{ 1.0f, 0.78f, 0.34f };       // the third rank's water
	inline constexpr Color kGoldFoam{ 1.0f, 0.93f, 0.68f };

	// The water's colour and its light parts', for the third rank (grand) or the others.
	[[nodiscard]] constexpr const Color& WaterOf(bool a_grand) { return a_grand ? kGold : kWater; }
	[[nodiscard]] constexpr const Color& FoamOf(bool a_grand) { return a_grand ? kGoldFoam : kFoam; }

	// The cloak a_age seconds into a Renewal held for a_seconds (it falls away as it ends), on someone
	// standing still: every part's strength, the turns that keep it moving, the ripples spreading out
	// and the drops falling. The third rank's (grand) is gold, kGrandSize larger, its swirl burning
	// brighter under a glow of its own.
	struct Ripple
	{
		float scale{ 0.0f };
		float alpha{ 0.0f };
	};
	struct Drop
	{
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };
		float size{ 0.0f };
		float alpha{ 0.0f };
	};
	struct Look
	{
		float veil{ 0.0f };
		float inner{ 0.0f };
		float streams{ 0.0f };
		float twist{ 0.0f };      // the streams' turn about up, radians
		float crown{ 0.0f };
		float crownTurn{ 0.0f };
		float gleam{ 0.0f };      // the grand cloak's glow over the head, beating gently
		float gleamScale{ 0.0f };
		float pool{ 0.0f };
		float poolScale{ 0.0f };
		float rise{ 0.0f };       // the whole cloak's scale up from the ground as it forms and falls away
		float light{ 0.0f };      // 0..1
		std::array<Ripple, kRipples> ripples{};
		std::array<Drop, kDrops>     drops{};
	};
	[[nodiscard]] Look At(float a_age, float a_seconds, bool a_grand = false);

	// A wash a_since seconds after a pulse reached them: the band of water sliding down from over their
	// head to their feet (its height up them and its size around them), then the ripple it leaves.
	struct Wash
	{
		float z{ 0.0f };
		float scale{ 0.0f };
		float alpha{ 0.0f };
		float ring{ 0.0f };
		float ringScale{ 0.0f };
	};
	[[nodiscard]] Wash WashAt(float a_since);

	// The scale to draw it at for someone a_height tall.
	[[nodiscard]] float SizeFor(float a_height);
}
