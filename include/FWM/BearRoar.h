#pragma once

#include <array>
#include <cstddef>

// Taunt's appearance, shared by Skyrim and the preview. No game types or gameplay changes.
namespace FWM::BearRoar
{
	inline constexpr float kRoar = 0.55f;
	inline constexpr float kLeft = 0.95f;
	inline constexpr float kRight = 2.55f;
	inline constexpr float kSettle = 2.95f;
	inline constexpr float kFade = 3.35f;
	inline constexpr float kGone = 4.05f;
	inline constexpr float kScale = 5.0f;
	inline constexpr float kGrandScale = 1.45f;
	inline constexpr float kShakeSeconds = 0.9f;
	inline constexpr float kShakeReach = 2200.0f;
	inline constexpr std::size_t kCopies = 4;
	inline constexpr std::size_t kWaves = 3;
	inline constexpr std::array<float, 3> kAmber{ 1.0f, 0.68f, 0.3f };
	inline constexpr std::array<float, 3> kRed{ 1.0f, 0.10f, 0.06f };
	struct Wave
	{
		float forward{}, radius{ 1.0f }, alpha{};
	};
	struct Pose
	{
		float side{}, forward{}, height{}, pitch{}, yaw{}, scale{ kScale }, jaw{}, alpha{};
		std::array<float, 3> tint{ 1.0f, 1.0f, 1.0f }, glow{ kAmber };
		std::array<Wave, kWaves> waves{};
	};
	[[nodiscard]] Pose At(float a_age, bool a_grand = false, bool compact = false);
	[[nodiscard]] Pose WarCryAt(float a_age, bool a_grand = false, bool compact = false);
	[[nodiscard]] float Shake(float a_distance);
}
