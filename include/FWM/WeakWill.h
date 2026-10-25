#pragma once
#include <array>
#include <cstddef>

namespace FWM::WeakWill
{
	inline constexpr std::size_t kCopies = 4;
	inline constexpr std::array<const char*, 4> kNames{ "Helmet", "Rim", "Mist", "Motes" };
	inline constexpr float kFade = 0.65f;
	inline constexpr float kHelmetHold = 1.35f;
	inline constexpr float kHelmetEnd = 3.25f;
	struct Part
	{
		float scale{ 1 }, height{}, forward{}, turn{}, tilt{}, alpha{};
		std::array<float, 3> color{ 0.80f, 0.91f, 1.0f };
	};
	struct Frame { std::array<Part, kNames.size()> parts; float light{}; };
	// Geometry is Skyrim's steel helmet (I/II) or ebony helmet (III), in original local units. The pose is shared by the
	// native overlay and browser preview; duration is the actual active ability duration.
	[[nodiscard]] Frame At(float age, float duration, int rank);
}
