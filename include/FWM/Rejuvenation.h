#pragma once

#include <array>

// Rejuvenation's persistent seal. Its lifespan comes from the actual magicka buff.
// Shared by the game and preview, so rank sizes, particles and fades cannot diverge.
namespace FWM::Rejuvenation
{
	using Color = std::array<float, 3>;
	inline constexpr float kSeconds = 30.0f;
	inline constexpr float kMeshRadius = 280.0f;
	inline constexpr int kMotes = 72;
	inline constexpr int kCopies = 4;
	inline constexpr std::array<const char*, 10> kNames{ "Seal", "Bloom", "Mist0", "Mist1", "Filaments", "GrandSeal", "EdgePulse0", "EdgePulse1", "EdgePulse2", "Heart" };
	struct Part { float scale{ 1 }, turn{}, height{}, alpha{}; Color color{}; };
	struct Mote { std::array<float, 3> at{}; float size{}, alpha{}; Color color{}; };
	struct Look {
		float radius{}, light{};
		Color lightColor{};
		std::array<Part, kNames.size()> parts;
		std::array<Mote, kMotes> motes;
	};
	[[nodiscard]] Look At(float a_age, int a_rank, float a_seconds = kSeconds);
}
