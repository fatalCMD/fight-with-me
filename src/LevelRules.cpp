#include "FWM/LevelRules.h"

#include <algorithm>
#include <functional>
#include <vector>

namespace FWM::Leveling
{
	float EnemyXP(int a_enemyLevel, int a_level, bool a_boss, const Tuning& a_tuning)
	{
		const int   level = std::max(a_level, 1);
		const float gap = static_cast<float>(std::max(a_enemyLevel, 1) - level);
		const float share = std::clamp(1.0f + a_tuning.levelStep * gap, a_tuning.lowShare, a_tuning.highShare);
		return a_tuning.xpPerEnemyLevel * static_cast<float>(level) * share * (a_boss ? a_tuning.bossMult : 1.0f);
	}

	float FightXP(std::span<const Foe> a_foes, int a_level, const Tuning& a_tuning)
	{
		std::vector<float> worth;
		worth.reserve(a_foes.size());
		for (const auto& foe : a_foes) {
			worth.push_back(EnemyXP(foe.level, a_level, foe.boss, a_tuning));
		}
		std::sort(worth.begin(), worth.end(), std::greater<>());
		const float crowd = std::clamp(a_tuning.crowd, 0.0f, 1.0f);
		float       total = 0.0f;
		float       weight = 1.0f;
		for (const float one : worth) {
			total += one * weight;
			weight *= crowd;
		}
		return total;
	}

	float Cost(int a_level, const Tuning& a_tuning)
	{
		const float level = static_cast<float>(std::max(a_level, 1));
		const float player = std::max(a_tuning.costBase + a_tuning.costPerLevel * level, 1.0f);
		return player * (1.0f + std::max(a_tuning.climb, 0.0f) * (level - 1.0f));
	}

	Gain Earn(int a_level, float a_xp, float a_earned, int a_cap, const Tuning& a_tuning)
	{
		Gain gain;
		int  level = std::max(a_level, 1);
		if (level >= a_cap) {
			gain.xp = std::max(a_xp, 0.0f);  // kept, for when the player is ahead again
			gain.capped = true;
			return gain;
		}
		float xp = std::max(a_xp, 0.0f) + std::max(a_earned, 0.0f);
		for (float cost = Cost(level, a_tuning); xp >= cost && level < a_cap; cost = Cost(level, a_tuning)) {
			xp -= cost;
			++level;
			++gain.levels;
		}
		gain.capped = level >= a_cap;
		gain.xp = gain.capped ? 0.0f : xp;
		return gain;
	}

	Growth GrowthFor(Role a_role)
	{
		switch (a_role) {
		case Role::kTank: return { 6.0f, 0.0f, 4.0f, { Skill::kBlock, Skill::kHeavyArmor, Skill::kOneHanded }, 3 };
		case Role::kBruiser: return { 5.0f, 0.0f, 5.0f, { Skill::kTwoHanded, Skill::kHeavyArmor }, 2 };
		case Role::kArcher: return { 4.0f, 0.0f, 6.0f, { Skill::kArchery, Skill::kLightArmor }, 2 };
		case Role::kMage: return { 3.0f, 7.0f, 0.0f, { Skill::kDestruction, Skill::kAlteration }, 2 };
		case Role::kHealer: return { 4.0f, 6.0f, 0.0f, { Skill::kRestoration, Skill::kAlteration }, 2 };
		default: return { 5.0f, 0.0f, 5.0f, { Skill::kOneHanded, Skill::kLightArmor, Skill::kBlock }, 3 };
		}
	}
}
