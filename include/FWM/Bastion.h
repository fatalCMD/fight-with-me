#pragma once
#include <array>
#include <cstdint>

namespace FWM::Bastion
{
	inline constexpr float kImpact = .96f, kStoneGone = 4.6f, kFade = .65f;
	inline constexpr float kShakeSeconds = .34f, kShakeReach = 1100.0f;
	struct SoundCue
	{
		std::uint32_t form;
		std::uint32_t grandForm;
		float at, seconds;
		std::array<float, 3> volume;
	};
	// Skyrim's ward forming, heavy ground impact, loose stone, and soft ward release.
	// Short, bounded tails keep the guardian state quiet after the castle dissolves.
	inline constexpr std::array<SoundCue, 4> kSounds{{
		{ 0x03D118, 0x04E792, .08f, 1.1f, { .36f, .40f, .55f } },
		{ 0x0C4E30, 0x03F1F4, kImpact, 1.8f, { .65f, .72f, .85f } },
		{ 0x03EA9C, 0x101F41, kImpact + .04f, 1.75f, { .18f, .22f, .38f } },
		{ 0x04FA3B, 0x086EFE, 2.55f, 1.4f, { .24f, .27f, .30f } }
	}};
	inline constexpr std::array<const char*, 7> kNames{ "Stone", "Tracery", "Impact", "Dust", "Ward", "WardGlow", "Sparks" };
	struct Part
	{
		float scale{ 1 }, height{}, turn{}, alpha{};
		std::array<float, 3> color{ .55f, .76f, .95f };
	};
	struct Frame { std::array<Part, kNames.size()> parts; float light{}; };
	// Architecture stays at the cast point; the final three parts follow the guardian.
	[[nodiscard]] Frame At(float age, float duration, int rank);
	[[nodiscard]] float Shake(float distance, int rank);
}
