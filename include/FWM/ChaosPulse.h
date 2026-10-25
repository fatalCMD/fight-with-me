#pragma once

#include <array>
#include <cstddef>

namespace FWM::ChaosPulse
{
	inline constexpr float kRadius = 250.0f, kGone = 4.6f;
	inline constexpr std::size_t kCopies = 4;
	inline constexpr std::size_t kBindingCopies = 16;
	inline constexpr float kBindingRadius = 38.0f;
	inline constexpr std::array<const char*, 5> kBindingNames{ "BindSeal", "BindMist", "BindThreads0", "BindThreads1", "BindFlecks" };
	inline constexpr std::array<const char*, 10> kNames{ "Out", "In", "Seal", "Mist", "Threads", "Core", "Flecks", "Rise0", "Rise1", "Rise2" };
	inline constexpr std::array<const char*, 37> kJoints{
		"Skull", "LeftArm", "LeftForearm", "LeftHand", "RightArm", "RightForearm", "RightHand",
		"LeftFinger00", "LeftFinger01", "LeftFinger02", "LeftFinger10", "LeftFinger11", "LeftFinger12",
		"LeftFinger20", "LeftFinger21", "LeftFinger22", "LeftFinger30", "LeftFinger31", "LeftFinger32",
		"LeftFinger40", "LeftFinger41", "LeftFinger42",
		"RightFinger00", "RightFinger01", "RightFinger02", "RightFinger10", "RightFinger11", "RightFinger12",
		"RightFinger20", "RightFinger21", "RightFinger22", "RightFinger30", "RightFinger31", "RightFinger32",
		"RightFinger40", "RightFinger41", "RightFinger42"
	};
	struct Part
	{
		float scale{ 1.0f }, turn{}, height{}, alpha{};
		std::array<float, 3> color{ 0.60f, 0.25f, 1.0f };
	};
	struct Spirit
	{
		struct Joint
		{
			std::array<float, 3> at{};
			std::array<float, 9> turn{ 1,0,0,0,1,0,0,0,1 };
		};
		float scale{ 5.5f }, height{}, forward{ -100.0f }, alpha{}, lean{}, roll{}, twist{};
		std::array<float, 3> color{ 0.55f, 0.30f, 1.0f };
		std::array<Joint, kJoints.size()> bones{};
	};
	struct Frame { std::array<Part, kNames.size()> parts; float light{}; Spirit spirit; };
	struct Binding { std::array<Part, kBindingNames.size()> parts; };
	// Pure timeline, shared by the plugin, tests and mesh preview.
	[[nodiscard]] Frame At(float a_seconds, bool a_grand = false, float a_duration = kGone);
	// The small, following restraint on an affected enemy. Remaining time comes from the actual
	// slow: refreshing it never restarts its swirl or stacks another mesh on the same target.
	[[nodiscard]] Binding BindAt(float a_age, float a_left, bool a_grand = false);
}
