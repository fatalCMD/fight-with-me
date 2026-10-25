#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

// Elemental Burst as numbers: the little light that flies to the target's feet, and the blast that
// goes off there in fire, frost or lightning. No game types: the plugin poses the real meshes with
// it, the tests check it, and tools/wolfhead/preview plays the same frames in a browser.
//
// Each element has a mesh (meshes\FightWithMe\ElementalBlast<Fire|Frost|Shock>.nif, from
// tools/blast/BuildBlast.js) with an "Orb" node, the flying light, and a "Blast" node holding one
// node per name in Parts(). The blast is kMeshRadius across in its own units, its middle on the
// ground at the origin.

namespace FWM::Blast
{
	enum class Element : std::uint8_t
	{
		kFire,
		kFrost,
		kShock,
		kCount
	};

	inline constexpr float kMeshRadius = 250.0f;
	// Its reach by tier (Apprentice, Adept, Expert): the whole blast is drawn to it, so each tier is
	// bigger as well as wider. The third is grand: it adds a showpiece of its own (At).
	inline constexpr std::array<float, 3> kRadius{ 200.0f, 280.0f, 380.0f };
	inline constexpr std::size_t kGrandTier = 2;
	inline constexpr float kSpeed = 2600.0f;           // how fast the light flies
	inline constexpr float kFlightShortest = 0.18f;    // so it is seen even point blank
	inline constexpr float kFlightLongest = 0.5f;
	inline constexpr float kCentre = 0.3f;             // within this share of the reach, the full blow
	inline constexpr float kEdge = 0.6f;               // and at the very edge, this much of it
	inline constexpr float kStaggerWithin = 0.5f;      // enemies this close to the middle stagger
	inline constexpr float kFlow = 0.6f;               // the noise texture drifts up this many texture lengths a second
	inline constexpr float kLightReach = 2.4f;         // the flash of light reaches this many times the blast's reach
	inline constexpr float kLightFade = 2.5f;          // and is this strong at its brightest (a torch is about 1)
	inline constexpr float kSnap = 0.15f;              // its first moment, the flash and the burst, runs at its own pace
	inline constexpr float kLinger = 1.35f;            // and everything after it this much slower, so it stays a while
	inline constexpr int   kMaxParts = 72;

	// Lightning strikes again down new channels this far into its own timeline, each fainter.
	inline constexpr std::array<float, 3> kRestrikes{ 0.42f, 0.8f, 1.15f };
	inline constexpr std::array<float, 3> kRestrikeStrength{ 0.8f, 0.7f, 0.6f };
	inline constexpr float                kRestrikeLength = 0.14f;
	inline constexpr float                kTwinLag = 0.06f;  // a grand blast's second bolt strikes this much after the first, every time

	struct Vec
	{
		float x{ 0.0f };
		float y{ 0.0f };
		float z{ 0.0f };
	};

	using Color = std::array<float, 3>;

	// Rows by columns, turning column vectors, as the game's own matrices do.
	using Mat = std::array<std::array<float, 3>, 3>;

	// One part of a blast: where its node is in the mesh's units, how it is turned (its own +Z leant
	// by tilt toward the heading yaw, which also dips its +X), how big, how strongly it shows, and in
	// what colour. A part that faces (the lightning's bolts and arcs, strips in their own XZ plane along
	// +Z) keeps only that +Z: it turns about it to face the camera (Facing), as the game's beams do.
	struct Part
	{
		Vec   at{};
		float yaw{ 0.0f };
		float tilt{ 0.0f };
		float scale{ 0.0f };
		float alpha{ 0.0f };
		Color color{ 1.0f, 1.0f, 1.0f };
		bool  faces{ false };
		// Frost's mist moves under its ice node, with its own fading light and gentle lift.
		float auraAlpha{ 0.0f };
		float auraLift{ 0.0f };
		float auraYaw{ 0.0f };
	};

	struct Look
	{
		std::array<Part, kMaxParts> parts{};  // in the order of Parts()
		float light{ 0.0f };                  // the flash of real light it throws, 0..1
		Color lightColor{ 1.0f, 1.0f, 1.0f };
	};

	// How long a blast of an element lasts, from the moment it goes off.
	// The moment a_timeline seconds into its own timeline falls, a_timeline after kSnap stretched by kLinger.
	[[nodiscard]] float Real(float a_timeline);

	[[nodiscard]] float Seconds(Element a_element, bool a_grand = false);

	// The nodes under "Blast" that move, by name.
	[[nodiscard]] std::span<const std::string_view> Parts(Element a_element);

	// How it looks a_age seconds after it went off; before that, nothing shows. A grand blast (the third
	// tier) adds its own parts, which otherwise never show: for fire a giant fireball ringed by smaller
	// explosions, sparks flying up over it; for frost a huge crystal in the middle that shatters at the
	// end; for lightning a second thunderbolt, coming in at an angle to meet the first.
	[[nodiscard]] Look At(Element a_element, float a_age, bool a_grand = false);

	// A facing part's turn: its +Z along its length (yaw and tilt), turned about it until its face (+Y)
	// looks as near a_toCamera, from the part, as it can.
	[[nodiscard]] Mat Facing(const Part& a_part, Vec a_toCamera);

	// Seconds the light takes to fly a_distance.
	[[nodiscard]] float Flight(float a_distance);

	// The light on its way, a_u of the way (0..1) from the hand to the feet: a shallow arc that
	// speeds up and dives in. Its trail lies along its own -X, so yaw and tilt point it where it goes.
	struct Orb
	{
		Vec   at{};
		float yaw{ 0.0f };
		float tilt{ 0.0f };
		float alpha{ 0.0f };
		float light{ 0.0f };
		Color color{ 1.0f, 1.0f, 1.0f };
	};
	[[nodiscard]] Orb OrbAt(Element a_element, Vec a_from, Vec a_to, float a_u);

	// The share of the blow an enemy takes a_distance from the middle of a blast reaching a_radius.
	[[nodiscard]] float Share(float a_distance, float a_radius);
}
