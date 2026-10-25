#pragma once

#include <array>

// Kynareth's Maw's bite as numbers: when each part of it happens, and where the giant head and
// its jaw are at any moment. No game types: the plugin poses the real head with it, the tests check
// it, and tools/wolfhead/preview plays the same frames in a browser.
//
// The head's own units are those of meshes\FightWithMe\SpectralWolfHead.nif, from the head's
// middle, facing +Y with +Z up (tools/wolfhead/BuildWolfHead.js prints its measurements).

namespace FWM::WolfBite
{
	struct Vec
	{
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };
	};

	// Rows by columns, turning column vectors, as the game's own matrices do.
	using Mat = std::array<std::array<float, 3>, 3>;

	inline constexpr Mat kIdentity{ { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } } };

	struct Pose
	{
		Mat   rotate{ kIdentity };
		Vec   translate{};
		float scale{ 1.0f };
		float jaw{ 0.0f };    // radians the jaw stands open
		float alpha{ 1.0f };  // 0 unseen, 1 whole
	};

	inline constexpr float kHeadScale = 10.0f;
	inline constexpr float kRise = 0.45f;     // taking shape, jaws opening round them
	inline constexpr float kTense = 0.95f;    // rearing, trembling
	inline constexpr float kSnap = 1.05f;     // the jaws slam shut: the bite
	inline constexpr float kShake = 2.0f;     // shaking what it caught
	inline constexpr float kGone = 2.7f;      // let go and faded
	inline constexpr float kJawOpen = 0.85f;  // radians
	inline constexpr float kSnarl = kTense - 0.2f;  // its snarl, as the jaws start to close
	inline constexpr float kSwing = kSnap - 0.3f;   // when the fighter's own swing starts, to land with the bite
	inline constexpr float kGrandHold = 0.8f;       // the third rank's wolf shakes its catch this much longer

	// When the shaking ends and when the head is gone, for the third rank (grand) or the others.
	[[nodiscard]] constexpr float ShakeEnd(bool a_grand) { return kShake + (a_grand ? kGrandHold : 0.0f); }
	[[nodiscard]] constexpr float Gone(bool a_grand) { return kGone + (a_grand ? kGrandHold : 0.0f); }

	inline constexpr float kHitStop = 0.12f;       // real seconds the world all but stops as the jaws close
	inline constexpr float kHitStopSpeed = 0.12f;  // how fast it runs meanwhile

	// The mesh: skull back to muzzle tip along +Y, half the head's width, where the teeth meet, and
	// where the enemy's middle sits (the front half of the head between the jaws).
	inline constexpr Vec   kHeadBack{ 0.0f, -9.0f, 1.0f };
	inline constexpr Vec   kHeadFront{ 0.0f, 10.0f, -3.0f };
	inline constexpr float kHeadRadius = 9.0f;
	inline constexpr Vec   kTeeth{ 0.0f, 6.0f, -4.0f };
	inline constexpr Vec   kGrip{ 0.0f, 4.0f, -3.0f };
	inline constexpr float kHeadLength = 24.0f;
	inline constexpr Vec   kJawHinge{ 0.0f, -9.17f, -1.4f };  // the jaw node, from the head's middle

	// The visible head's lower outline (tools/wolfhead/hull.js): the skull's points from its middle,
	// the jaw's from its hinge. However the jaw opens and the head tilts, its lowest point is one of
	// these, give or take the fur. The head is lifted so that point stays above the ground under the
	// enemy: a giant head rears over them rather than open its jaw into the floor.
	inline constexpr std::array<Vec, 6> kSkullHull{ { { 0.0f, 9.7f, 1.7f }, { 0.0f, 10.2f, 1.1f }, { 0.0f, 10.1f, 0.3f }, { 0.0f, 8.8f, -1.5f }, { 0.0f, 7.6f, -2.7f }, { 0.0f, -12.4f, -9.8f } } };
	inline constexpr std::array<Vec, 4> kJawHull{ { { 0.0f, 14.4f, -4.4f }, { 0.0f, 13.8f, -7.0f }, { 0.0f, 13.1f, -7.4f }, { 0.0f, 12.1f, -7.5f } } };
	inline constexpr float kFur = 2.5f;        // the fur below the outline, in the head's units
	inline constexpr float kClearance = 4.0f;  // world units kept between the head and the ground

	// The head at a_age seconds into the bite, facing a_heading (the game's: clockwise from north)
	// at an enemy standing at a_at whose middle is a_chest above their feet. The third rank's (grand)
	// holds on and shakes its catch kGrandHold longer.
	[[nodiscard]] Pose At(float a_age, float a_heading, Vec a_at, float a_chest, bool a_grand = false);

	[[nodiscard]] Pose Display(Pose a_pose, float a_ground, bool a_grand, bool compact = false);

	// A point of the head's mesh, in the world.
	[[nodiscard]] Vec Place(const Pose& a_pose, Vec a_local);

	// How low the head reaches, fur included, in the world.
	[[nodiscard]] float Lowest(const Pose& a_pose);

	// The jaw's own turn for an opening of a_radians, relative to its rest.
	[[nodiscard]] Mat JawTurn(float a_radians);
}
