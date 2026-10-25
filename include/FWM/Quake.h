#pragma once

#include <array>
#include <span>
#include <string_view>

// Earthshaker as numbers: the slam on the ground and the rocks it throws up out of the earth, how high
// its third rank throws enemies and how long the fallen stay down. No game types: the plugin poses the
// real mesh with it, the tests check it, and tools/wolfhead/preview plays the same frames in a browser.
//
// Its mesh (meshes\FightWithMe\Earthshaker.nif, from tools/quake/BuildQuake.js) has a "Quake" node
// holding one node per name in Parts(), kMeshRadius across in its own units, its middle on the ground
// at the origin. The rocks are the game's own boulders (meshes\landscape\rocks), lit as in the world,
// each brought to kRock across at scale 1 with its foot on the ground.

namespace FWM::Quake
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

	inline constexpr float kMeshRadius = 300.0f;
	inline constexpr float kRock = 100.0f;          // a rock's breadth at scale 1
	inline constexpr int   kMaxParts = 48;
	inline constexpr float kLaunchHeight = 320.0f;  // how high the third rank throws them: about fifteen feet
	inline constexpr float kGravity = 686.0f;       // the game's, in its units a second each second
	inline constexpr float kThrowOut = 120.0f;      // and how fast it throws them away from the slam
	inline constexpr float kLaunchBreadth = 60.0f;  // nothing broader is thrown (giants, mammoths, dragons only fall)
	inline constexpr float kFell = 0.6f;            // the knock that fells them where they stand
	inline constexpr std::array<float, 3> kDowned{ 2.5f, 3.0f, 4.0f };   // seconds the fallen are held down, by rank
	inline constexpr float kBossDowned = 0.5f;      // a boss, this share of it
	inline constexpr std::array<float, 3> kShake{ 0.45f, 0.6f, 0.85f };  // the camera's shake at the slam, by rank

	// Every part of it: a node of the same name, where it is in the mesh's units, how it is turned, how
	// big, and (for the glows and the dust; the rocks are lit, not faded) how strongly it shows and in
	// what colour.
	struct Part
	{
		Vec   at{};
		Mat   turn{ { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } } };
		float scale{ 0.0f };
		float alpha{ 0.0f };
		Color color{ 1.0f, 1.0f, 1.0f };
	};

	struct Look
	{
		std::array<Part, kMaxParts> parts{};  // in the order of Parts()
	};

	// How long it lasts from the slam. Grand is the third rank's: bigger rocks out of the middle.
	[[nodiscard]] float Seconds(bool a_grand = false);

	// The nodes under "Quake" that move, by name; the grand ones ("SpireN") last.
	[[nodiscard]] std::span<const std::string_view> Parts();

	// How it looks a_age seconds after the slam; before it, nothing shows.
	[[nodiscard]] Look At(float a_age, bool a_grand = false);

	// The third rank's throw: straight up fast enough to reach kLaunchHeight, and the time it takes to
	// come down again.
	[[nodiscard]] float LaunchSpeed();
	[[nodiscard]] float Airborne();

	// How long someone it fells stays down.
	[[nodiscard]] float Downed(int a_rank, bool a_boss);
}
