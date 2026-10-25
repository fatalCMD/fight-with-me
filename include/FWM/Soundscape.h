#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

namespace FWM::Soundscape
{
	enum class Kind { Circle, Rejuvenation, Renewal, WeakWill, Chaos, Sunder, Rain, Arrow,
		SecondWind, Riposte, Deadeye, Volley, Pin, Surge, Berserk, Ward, WarCry, Magnus, Wolf, Taunt, Impact, Disarm, DragonBreath, RiposteImpact, ExplosiveImpact, Windstep, WindstepBurst, Count };
	inline constexpr std::string_view kNames[]{ "circle", "rejuvenation", "renewal", "weakwill", "chaos", "sunder", "rain", "arrow",
		"secondwind", "riposte", "deadeye", "volley", "pin", "surge", "berserk", "ward", "warcry", "magnus", "wolf", "taunt", "impact", "disarm", "dragonbreath", "riposte-impact", "explosive-impact", "windstep", "windstep-burst" };
	struct Cue
	{
		std::uint32_t form{};
		float at{}, seconds{}, volume{};
		bool loop{};
		float pitch{ 1.0f };
        bool channel{}; // prominent attack loop, not a quiet persistent circle
	};
	// A negative start is relative to expiry; zero seconds holds until expiry.
	[[nodiscard]] std::vector<Cue> Plan(Kind kind, int rank, int element=0);
	[[nodiscard]] float Start(const Cue& cue, float duration);
	[[nodiscard]] float End(const Cue& cue, float duration);
	[[nodiscard]] float Gain(const Cue& cue, float age, float duration);
	[[nodiscard]] float DistanceGain(float distance, bool loop);
}
