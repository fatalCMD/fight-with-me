#pragma once

#include <array>

// Magnus as numbers: the great orb that floats behind the mage while Magnus holds, and the magicka it
// pours into them as from a pool without end. No game types: the plugin poses the real orb with it, the
// tests check it, and tools/wolfhead/preview plays the same frames in a browser.
//
// Its mesh (meshes\FightWithMe\MagnusOrb.nif, from tools/magnus/BuildMagnus.js) is in game units: an
// "Orb" node holding "Core", "Shell", "Swirl", "Halo", "Rim" and "Ring0".."Ring2", kRadius across at full size;
// "Tendril0".."Tendril2", streams kTendrilLength long along +X; "Flow00".."Flow23", motes; and for the
// grand orb of Magnus's third rank, "Veil0".."Veil2", wider streams, and "Wisp00".."Wisp11", comets of
// magicka with their tails back along -X.

namespace FWM::MagnusOrb
{
	struct Vec
	{
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };
	};

	// Rows by columns, turning column vectors, as the game's own matrices do.
	using Mat = std::array<std::array<float, 3>, 3>;
	using Color = std::array<float, 3>;

	inline constexpr float kRadius = 72.0f;         // the orb's shell at full size
	inline constexpr float kGrandSize = 1.62f;       // and the grand orb's against it
	inline constexpr Vec   kHome{ 0.0f, -135.0f, 215.0f };  // where it floats from the mage: behind them (+Y their front) and over their head
	inline constexpr Vec   kGrandHome{ 0.0f, -180.0f, 255.0f };  // the grand one, further back and higher for its size
	inline constexpr float kFollow = 0.6f;          // seconds it takes to catch up most of the way when they move
	inline constexpr float kGrow = 0.9f;            // it comes out of them and swells this long
	inline constexpr float kGo = 0.8f;              // and is drawn back into them this long at the end
	inline constexpr float kChest = 95.0f;          // where the magicka goes, over their feet, when their spine cannot be found
	inline constexpr int   kRings = 3;
	inline constexpr int   kTendrils = 3;
	inline constexpr int   kFlows = 24;
	inline constexpr int   kWisps = 12;
	inline constexpr float kTendrilLength = 100.0f;
	inline constexpr float kDrift = 0.25f;          // the noise drifting over its skins, texture lengths a second
	inline constexpr float kStream = 2.5f;          // and pouring down the streams into the mage
	inline constexpr float kLightReach = 520.0f;
	inline constexpr float kLightFade = 1.6f;       // its light at full strength (a torch is about 1)

	// Where it belongs for a mage standing at a_mage facing a_heading (the game's: 0 north, turning
	// east): behind them and over their head, bobbing gently.
	[[nodiscard]] Vec Home(Vec a_mage, float a_heading, float a_age, bool a_grand = false);

	// One step of following, from a_at toward a_home over a_dt seconds: it drifts after them, never snaps.
	[[nodiscard]] Vec Follow(Vec a_at, Vec a_home, float a_dt);

	// How it looks a_age seconds into Magnus held for a_seconds. Plain, it is Magnus's white blue with a
	// near-white core; grand, it is bigger, its heart red, its skins red and purple, fading to blue at its
	// edge.
	struct Look
	{
		float out{ 0.0f };    // how far it has come out of the mage, 0 in their chest .. 1 at its place (and back again at the end)
		float scale{ 0.0f };  // its size against kRadius
		float alpha{ 0.0f };  // its strength
		float core{ 0.0f };   // the core's beat, 0..1
		float shell{ 0.0f };  // the outer skin's turn about up, radians
		float swirl{ 0.0f };  // and the inner one's, the other way
		std::array<Mat, kRings> rings{};  // each ring's turn: tipped over, spinning
		float light{ 0.0f };  // its light, 0..1
		float flow{ 0.0f };   // the magicka pouring into the mage, 0..1
		float halo{ 0.0f };   // the soft glow all over it, 0..1
		float rim{ 0.0f };    // the glow only round its edge (the grand one's blue fade), 0..1
		// The colour of each part, and of its light.
		Color coreColor{}, swirlColor{}, shellColor{}, haloColor{}, rimColor{}, ringColor{}, streamColor{}, lightColor{};
	};
	[[nodiscard]] Look At(float a_age, float a_seconds, bool a_grand = false);

	// One mote of magicka on its way from the orb (centred at a_orb, a_scale its size against kRadius)
	// into the chest at a_chest: it leaves the orb's surface facing the mage, swings out a little, and
	// speeds in. The grand orb's are bigger, spiral round their way, and are red or purple by turns.
	struct Mote
	{
		Vec   at{};
		float size{ 0.0f };
		float alpha{ 0.0f };
		Color color{};
	};
	[[nodiscard]] Mote FlowAt(int a_index, float a_age, Vec a_orb, float a_scale, Vec a_chest, float a_flow, bool a_grand = false);

	// One stream from the orb's front into the chest: where it starts, its turn (its +X pointing at the
	// chest, twisting about itself), and its length against kTendrilLength. The plain orb pours through
	// the tendrils, the grand one through the wider veils.
	struct Stream
	{
		Vec   from{};
		Mat   turn{};
		float scale{ 0.0f };
		float alpha{ 0.0f };
	};
	[[nodiscard]] Stream TendrilAt(int a_index, float a_age, Vec a_orb, float a_scale, Vec a_chest, float a_flow, bool a_grand = false);
	[[nodiscard]] Stream VeilAt(int a_index, float a_age, Vec a_orb, float a_scale, Vec a_chest, float a_flow, bool a_grand);

	// The grand orb's comets of magicka, spiralling into the mage head first: where, which way they head
	// (their +X), how big, how strongly, in what colour. Never seen for a plain one.
	struct Wisp
	{
		Vec   at{};
		Mat   turn{};
		float size{ 0.0f };
		float alpha{ 0.0f };
		Color color{};
	};
	[[nodiscard]] Wisp WispAt(int a_index, float a_age, Vec a_orb, float a_scale, Vec a_chest, float a_flow, bool a_grand);
}
