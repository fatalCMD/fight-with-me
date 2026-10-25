#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "FWM/Orders.h"

// Companions grow by fighting beside you. When a fight is over, every enemy the party brought
// down in it is worth XP to each companion who was in it, whoever struck last: the one worth the
// most in full, each after it a share of the one before, so a crowd is worth more than one enemy
// but never many times more. A level costs what one of the player's would and then some, more
// the higher they are, and they never pass the player's level. No game types here: the tests
// check it.

namespace FWM::Leveling
{
	struct Tuning
	{
		float xpPerEnemyLevel{ 3.0f };  // an even enemy: this × the companion's level
		float bossMult{ 3.0f };
		float levelStep{ 0.05f };  // each level an enemy is above them adds this share, each below takes it off
		float lowShare{ 0.25f };   // an enemy far below them is still worth this share
		float highShare{ 1.75f };  // and one far above them at most this
		float crowd{ 0.75f };      // each enemy after the one worth the most counts this share of the one before
		float costBase{ 75.0f };   // the player's fXPLevelUpBase
		float costPerLevel{ 25.0f };  // and fXPLevelUpMult
		float climb{ 0.05f };  // on top, this share more for every level they already have
	};

	inline constexpr int kNoCap = 65535;  // the player's level is no limit

	// What one enemy is worth to a companion of a_level: an even one xpPerEnemyLevel × their
	// level, more the further above them it is and less the further below, within limits, and a boss
	// several times that.
	[[nodiscard]] float EnemyXP(int a_enemyLevel, int a_level, bool a_boss, const Tuning& a_tuning);

	// One enemy the party brought down.
	struct Foe
	{
		int  level{ 1 };
		bool boss{ false };
	};

	// A whole fight's worth to a companion of a_level: its enemies by worth, the first in full and
	// each after it crowd × the one before. At 0.75 no fight is worth more than four of its best.
	[[nodiscard]] float FightXP(std::span<const Foe> a_foes, int a_level, const Tuning& a_tuning);

	// The XP from a_level to the next: the player's (base + per level × level), times
	// 1 + climb × (level - 1).
	[[nodiscard]] float Cost(int a_level, const Tuning& a_tuning);

	// a_earned on top of a_xp at a_level: the levels it makes and what is left toward the next.
	// Nobody passes a_cap (the player's level): at it they earn nothing, and what takes them to it
	// goes no further.
	struct Gain
	{
		int   levels{ 0 };
		float xp{ 0.0f };
		bool  capped{ false };  // at or above a_cap now
	};
	[[nodiscard]] Gain Earn(int a_level, float a_xp, float a_earned, int a_cap, const Tuning& a_tuning);

	// The skills a level can raise, by what each role fights with.
	enum class Skill : std::uint8_t
	{
		kOneHanded,
		kTwoHanded,
		kArchery,
		kBlock,
		kHeavyArmor,
		kLightArmor,
		kDestruction,
		kRestoration,
		kAlteration,
		kCount
	};

	constexpr int kSkillCap = 100;  // the level has no cap; a skill past 100 only breaks the game's sums

	// What a level brings, by the role they fought as: ten points among health, magicka and
	// stamina, as the player's level gives, and a point in each skill the role lives by.
	struct Growth
	{
		float                 health{ 0.0f };
		float                 magicka{ 0.0f };
		float                 stamina{ 0.0f };
		std::array<Skill, 3>  skills{};
		std::size_t           skillCount{ 0 };
	};
	[[nodiscard]] Growth GrowthFor(Role a_role);
}
