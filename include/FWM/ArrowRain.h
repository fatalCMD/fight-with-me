#pragma once

#include "FWM/Blast.h"

#include <array>

// Arrow Rain as numbers: the flare the archer shoots into the sky to call it, the hunter's mark it
// lays on the ground, the glow up there the arrows fall out of, and each arrow's trail and the hit
// where it lands. The arrows themselves are the game's, followed in flight. No game types: the plugin
// poses the real mesh with it, the tests check it, and tools/wolfhead/preview plays the same frames
// with stand-in arrows (DemoArrow).
//
// Its mesh (meshes\FightWithMe\ArrowRain.nif, from tools/arrowrain/BuildArrowRain.js) is in game
// units, the mark kRadius across round its origin: "Mark" holding "Ring", "Dashes", "Heads" (arrowheads
// round the rim pointing in), "Outer" (grand) and "Floor"; "Sky", a glow facing down, holding "Halo",
// "Swirl" and "Crown" (grand); "Burst", the flare going off; "Flare", a head with its trail back along
// -X; "Arrow00".."Arrow27", each laid over one of the game's arrows, its node at the arrow's nock (where
// the game puts a flying arrow) and its +Z back up the way it came, turned to the camera as the
// lightning is (Blast::Facing): "ArrowNNShape", a spectral arrow (broadhead down at -kTip, fletching at
// the nock), "ArrowNNTrail", a faint streak up behind it, and "ArrowNNGlint" at its point; and
// "Hit00".."Hit27", each holding "Spark", "Ripple" and "Dust", the last laid over the ground rather than
// glowing.

namespace FWM::ArrowRain
{
	using Blast::Color;
	using Blast::Mat;
	using Blast::Vec;

	inline constexpr float kRadius = 300.0f;    // the mark's reach: Paths::kRainRadius
	inline constexpr float kSky = 1100.0f;      // the arrows start this high over their mark
	inline constexpr float kPortalForward=85.0f, kPortalHeight=100.0f, kPortalRadius=65.0f;
    inline constexpr float kCall = 0.45f;       // the flare's climb: the first arrow falls once it bursts
	inline constexpr int   kMaxArrows = 96;     // visual capacity, allowing older/custom counts
	inline constexpr float kTrail = 300.0f;     // the streak's length in the mesh, up from the nock
	inline constexpr float kTip = 61.0f;        // the game's arrows reach this far ahead of their nock
	inline constexpr float kSink = 12.0f;       // and a struck one's head goes this far in
	inline constexpr float kSpeed = 12000.0f;    // how fast DemoArrow's arrows fall (in game, the arrows' own)
	inline constexpr float kLanding = 0.35f;    // allowed for the last arrow to come down before the mark goes
	inline constexpr float kDrift = 0.35f;      // the sky's swirl drifting, texture lengths a second
	inline constexpr float kStroke = 0.25f;     // the arrowheads pulse in this share of the mark's reach

	inline constexpr Color kTrailColor{ 0.86f, 0.96f, 1.0f };  // silver, faintly cool
	inline constexpr Color kMarkColor{ 0.42f, 1.0f, 0.78f };   // a hunter's green
	inline constexpr Color kSkyColor{ 0.7f, 1.0f, 0.9f };
	inline constexpr Color kDustColor{ 0.66f, 0.61f, 0.53f };  // pale enough to read over the ground it rises from

	// How long the whole of it shows for a rain of a_rain seconds, from the moment it is called.
	[[nodiscard]] float Seconds(float a_rain);

	// The mark, the sky and the burst a_age seconds after the archer called a rain of a_rain seconds.
	// Grand (the third rank) adds a ring to the mark and a crown to the sky, and burns brighter.
	struct Look
	{
		float mark{ 0.0f };        // the mark's strength, 0..1
		float markScale{ 0.0f };   // its size against kRadius: it sweeps in from wider as it is laid
		float ring{ 0.0f };        // the outer ring's turn about up, radians
		float dashes{ 0.0f };      // and the dashed ring's, the other way
		float heads{ 0.0f };       // how far in the arrowheads have pulsed, 0 at the rim .. 1 a kStroke in
		float headsAlpha{ 0.0f };  // and how strongly (they fade as they go in)
		float outer{ 0.0f };       // the grand mark's extra ring
		float floor{ 0.0f };       // the glow over the ground inside it
		float sky{ 0.0f };         // the glow the arrows fall out of
		float skyScale{ 0.0f };
		float swirl{ 0.0f };       // its swirl's turn
		float crown{ 0.0f };       // the grand sky's ring
		float crownTurn{ 0.0f };
		float burst{ 0.0f };       // the flare going off up there
		float burstScale{ 0.0f };
	};
	[[nodiscard]] Look At(float a_age, float a_rain, bool a_grand = false);

	// The flare a_age seconds after it left the bow at a_from for the sky over the mark at a_to: up
	// steeply, curving over, slowing as it climbs. Its trail lies along its own -X, so yaw and tilt point
	// it where it goes.
	struct Flare
	{
		Vec   at{};
		float yaw{ 0.0f };
		float tilt{ 0.0f };
		float alpha{ 0.0f };
	};
	[[nodiscard]] Flare FlareAt(Vec a_from, Vec a_to, float a_age);

	// An arrow a_since seconds after it was loosed and a_landed after it struck (below 0 while it
	// flies): the spectral arrow over it, stuck where it lands a moment before it fades; the streak
	// drawing out behind it, and once it strikes shrinking down into it; the glint at its point. Grand
	// ones burn brighter, their streaks wider.
	struct Trail
	{
		float arrow{ 0.0f };   // the spectral arrow's strength
		float length{ 0.0f };  // the streak's, against kTrail
		float width{ 0.0f };   // and its scale across
		float alpha{ 0.0f };
		float head{ 0.0f };    // the glint at its point
	};
	[[nodiscard]] Trail TrailAt(float a_since, float a_landed, bool a_grand = false);

	// The trail of an arrow flying along a_velocity as a facing part: its +Z back up the way it came.
	[[nodiscard]] Blast::Part TrailAxis(Vec a_velocity);

	// And its turn with its head at a_head, turned about that +Z to face the camera at a_camera.
	[[nodiscard]] Mat TrailTurn(Vec a_head, Vec a_velocity, Vec a_camera);

	// Where an arrow struck, a_since seconds on: a spark, a ripple over the ground, and dust thrown up,
	// though no dust when it struck something off the ground (a_ground false).
	struct Hit
	{
		float spark{ 0.0f };
		float sparkScale{ 0.0f };
		float ripple{ 0.0f };
		float rippleScale{ 0.0f };
		float dust{ 0.0f };
		float dustScale{ 0.0f };
		float dustRise{ 0.0f };  // how far the dust has risen
	};
	[[nodiscard]] Hit HitAt(float a_since, bool a_ground = true);

	// The preview's and the tests' stand-in for the game's arrows: the a_index-th of a_count over a
	// rain of a_rain seconds round a mark at the origin. Loosed when Arts looses the game's (the first
	// as the flare bursts, then evenly), from kSky up over somewhere near where it lands.
	struct Arrow
	{
		Vec   from{};
		Vec   to{};
		float loosed{ 0.0f };
		float lands{ 0.0f };
	};
	[[nodiscard]] Arrow DemoArrow(int a_index, int a_count, float a_rain);

	// Where a DemoArrow's nock is a_age seconds after the rain was called: held, once it lands, with its
	// head kSink in the ground at its mark.
	[[nodiscard]] Vec DemoHead(const Arrow& a_arrow, float a_age);

	// Where the point is of an arrow whose nock is at a_nock, flying along a_velocity.
	[[nodiscard]] Vec TipOf(Vec a_nock, Vec a_velocity);
}
