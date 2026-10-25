#pragma once

#include <array>
#include <cstddef>

// Pure poses for the armor-fracture effect. Mark follows the victim's chest while Burst stays
// at the impact. Mesh coordinates are Z-up, +Y-forward; rotations are Rz(yaw)*Ry(tilt)*Rx(roll).
namespace FWM::Sunder
{
	inline constexpr std::size_t kFragments = 28;
	inline constexpr std::size_t kSparks = 16;
	inline constexpr std::size_t kCopies = 4;
	inline constexpr float kHeight = 128.0f;

	struct Vec { float x{ 0 }, y{ 0 }, z{ 0 }; };
	using Color = std::array<float, 3>;
	struct Part
	{
		Vec at{};
		float yaw{ 0 }, tilt{ 0 }, roll{ 0 };
		float scale{ 0 }, alpha{ 0 };
		Color color{ 1, 1, 1 };
	};
	struct Look
	{
		Part cracks{}, grandCracks{}, flash{}, dust{};
		std::array<Part, kFragments> fragments{};
		std::array<Part, kSparks> sparks{};
		float light{ 0 };
	};

	[[nodiscard]] float Seconds(float a_duration);
	[[nodiscard]] Look At(float a_age, float a_duration, bool a_grand = false);
}
