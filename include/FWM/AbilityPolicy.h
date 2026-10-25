#pragma once

#include "FWM/Paths.h"
#include <array>
#include <cstdint>
#include <string_view>
#include <unordered_map>

// Plain combat facts and shared pacing, independent of the game for regression tests.
namespace FWM::AbilityPolicy
{
	inline constexpr float kEnemyReach = 1800.0f;
	inline constexpr float kSupportReach = 1400.0f;
	struct Context
	{
		float combatAge{ 0 }, health{ 1 }, magicka{ 1 }, allyHealth{ 1 }, playerHealth{ 1 };
		float healLine{ 0.6f }, targetHealth{ 1 }, targetDistance{ 100000 };
		int enemies{ 0 }, close{ 0 }, cluster{ 0 }, onSelf{ 0 }, onParty{ 0 }, onPlayer{ 0 }, hurt{ 0 };
		bool targetBoss{ false }, targetStrong{ false }, boss{ false }, casterThreat{ false };
		bool criticalAlly{ false };
		bool allyDown{ false }, allyDry{ false }, peel{ false }, threatenedCaster{ false };
	};
	struct Decision
	{
		std::string_view reason;
		bool urgent{ false };
		explicit operator bool() const { return !reason.empty(); }
	};
	[[nodiscard]] Decision Consider(Paths::Ability a_ability, const Context& a_context);
	class Pace
	{
	public:
		void Tick(float a_dt);
		void Clear();
		[[nodiscard]] bool Ready(std::uint32_t a_id, Paths::Ability a_ability, float a_combatAge, bool a_urgent) const;
		void Used(std::uint32_t a_id, Paths::Ability a_ability);
	private:
		double now{ 0 }, partyAt{ -100 };
		std::unordered_map<std::uint32_t, double> castAt;
		std::array<double, Paths::kAbilityCount> abilityUntil{};
	};
}
