#include "FWM/Rules.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace FWM::Rules
{
	int TauntReach(int a_tankLevel, const Tuning& a_tuning)
	{
		const float divisor = std::max(a_tuning.tauntReachDivisor, 1.0f);
		return static_cast<int>(std::floor(std::max(a_tankLevel, 1) / divisor));
	}

	float TauntChance(int a_tankLevel, int a_enemyLevel, bool a_boss, const Tuning& a_tuning)
	{
		const int effective = a_enemyLevel + (a_boss ? a_tuning.bossLevelBonus : 0);
		const int over = effective - (a_tankLevel + TauntReach(a_tankLevel, a_tuning));
		if (over <= 0) {
			return 1.0f;
		}
		if (over > a_tuning.tauntFalloffLevels) {
			return 0.0f;
		}
		return std::clamp(1.0f - a_tuning.tauntFalloffPerLevel * static_cast<float>(over), 0.0f, 1.0f);
	}

	float TauntRadius(float a_block, float a_heavyArmor, const Tuning& a_tuning)
	{
		const float skill = std::clamp(std::max(a_block, a_heavyArmor), 0.0f, 100.0f);
		return std::min(a_tuning.tauntRadiusBase + a_tuning.tauntRadiusPerSkill * skill, a_tuning.tauntRadiusMax);
	}

	float HoldSeconds(int a_tankLevel, int a_enemyLevel, const Tuning& a_tuning)
	{
		float seconds = a_tuning.holdBase + a_tuning.holdPerLevel * static_cast<float>(std::max(a_tankLevel, 1));
		if (a_tankLevel - a_enemyLevel >= a_tuning.cowedLevelGap) {
			seconds *= a_tuning.cowedHoldMult;
		}
		return seconds;
	}

	float TauntCooldown(int a_tankLevel, const Tuning& a_tuning)
	{
		const float cooldown = a_tuning.cooldownBase - a_tuning.cooldownPerLevel * static_cast<float>(std::max(a_tankLevel, 1));
		return std::max(cooldown, a_tuning.cooldownMin);
	}

	int PickHealTarget(std::span<const HealCandidate> a_candidates, float a_allyThreshold, const Tuning& a_tuning)
	{
		auto wanted = [&](const HealCandidate& c) {
			const bool walkedTo = c.bleeding && !c.player && !c.self;
			if (c.distance > (walkedTo ? a_tuning.reviveReach : a_tuning.healRange) || (walkedTo && c.taken)) {
				return false;
			}
			const float threshold = c.self ? std::min(a_tuning.healSelfThreshold, a_allyThreshold) : a_allyThreshold;
			return c.health < threshold || c.bleeding;
		};
		for (std::size_t i = 0; i < a_candidates.size(); ++i) {
			if (a_candidates[i].player && wanted(a_candidates[i])) {
				return static_cast<int>(i);
			}
		}

		int   best = -1;
		float bestUrgency = 0.0f;
		for (std::size_t i = 0; i < a_candidates.size(); ++i) {
			const auto& c = a_candidates[i];
			if (!wanted(c)) {
				continue;
			}
			const float threshold = c.self ? std::min(a_tuning.healSelfThreshold, a_allyThreshold) : a_allyThreshold;
			// How far under the line they are, then who is in the most danger.
			float urgency = threshold - c.health;
			if (c.bleeding) {
				urgency += 0.25f;
			}
			if (c.attacked) {
				urgency += 0.15f;
			}
			urgency += 0.0001f;  // anyone under the line beats nobody
			if (urgency > bestUrgency) {
				bestUrgency = urgency;
				best = static_cast<int>(i);
			}
		}
		return best;
	}

	bool Matches(const EnemyView& a_enemy, Focus a_focus)
	{
		switch (a_focus) {
		case Focus::kCasters:
			return a_enemy.caster;
		case Focus::kHealers:
			return a_enemy.healer;
		case Focus::kArchers:
			return a_enemy.archer;
		case Focus::kBackLine:
			return a_enemy.caster || a_enemy.healer || a_enemy.archer;
		case Focus::kAttackingPlayer:
			return a_enemy.attackingPlayer;
		case Focus::kAttackingParty:
			return a_enemy.attackingParty;
		case Focus::kPlayerTarget:
			return a_enemy.playerTarget;
		case Focus::kMarked:
			return a_enemy.marked;
		case Focus::kStrongest:
		case Focus::kWeakest:
			return true;  // continuous, scored below
		default:
			return false;
		}
	}

	namespace
	{
		bool Allowed(const EnemyView& a_enemy, Position a_position, bool a_ranged, const Tuning& a_tuning)
		{
			switch (a_position) {
			case Position::kGuard:
				return a_enemy.attackingPlayer || a_enemy.distanceToPlayer <= a_tuning.guardRadius ||
				       (a_enemy.attackingSelf && a_enemy.distance <= a_tuning.defendRange);
			case Position::kHoldBack:
				return a_ranged || a_enemy.attackingSelf || a_enemy.distance <= a_tuning.holdBackRadius;
			case Position::kWithMe:
				// At the player's side: whatever comes near them, whoever is on them from close by,
				// and whoever is on the follower within reach.
				return a_enemy.distanceToPlayer <= a_tuning.withMeRadius + 150.0f ||
				       (a_enemy.attackingPlayer && a_enemy.distanceToPlayer <= a_tuning.withMeRadius * 2.0f) ||
				       (a_enemy.attackingSelf && a_enemy.distance <= a_tuning.defendRange);
			default:
				return true;
			}
		}
	}

	Pick PickTarget(std::span<const EnemyView> a_enemies, Focus a_focus, Position a_position, bool a_ranged, const Tuning& a_tuning)
	{
		if (a_enemies.empty()) {
			return {};
		}
		// On foot there is no fighting someone on a ledge or across a gap: sending a melee
		// follower at them leaves them circling below, looking up.
		auto reachable = [&](const EnemyView& e) { return a_ranged || !e.outOfReach; };

		// A marked enemy is an order to go and get them. No position holds a companion back
		// from it: a guard sent across the room goes across the room.
		if (a_focus == Focus::kMarked) {
			for (std::size_t i = 0; i < a_enemies.size(); ++i) {
				if (a_enemies[i].marked && reachable(a_enemies[i])) {
					return { static_cast<int>(i), 400.0f };
				}
			}
		}
		const bool positional = a_position == Position::kGuard || a_position == Position::kFrontLine || a_position == Position::kWithMe ||
		                        (a_position == Position::kHoldBack && !a_ranged);
		// The combat AI settled on someone this follower cannot get to: choose for them.
		bool stuck = false;
		for (const auto& e : a_enemies) {
			stuck = stuck || (!reachable(e) && (e.engineChoice || e.current));
		}
		if (a_focus == Focus::kEngine && !positional && !stuck) {
			return {};
		}

		float maxThreat = 0.0f;
		bool  anyMatch = false;
		bool  anyAllowed = false;
		for (const auto& e : a_enemies) {
			maxThreat = std::max(maxThreat, e.threat);
			if (Allowed(e, a_position, a_ranged, a_tuning) && reachable(e)) {
				anyAllowed = true;
				if (a_focus != Focus::kEngine && Matches(e, a_focus)) {
					anyMatch = true;
				}
			}
		}

		// Guarding, or with the player, and nobody near them: go for whoever is closest to
		// the player, which is what pulls a guard (or a straggler) back to them.
		if (!anyAllowed) {
			if (a_position != Position::kGuard && a_position != Position::kWithMe && !stuck) {
				return {};
			}
			Pick nearest;
			float best = std::numeric_limits<float>::max();
			for (std::size_t i = 0; i < a_enemies.size(); ++i) {
				if (reachable(a_enemies[i]) && a_enemies[i].distanceToPlayer < best) {
					best = a_enemies[i].distanceToPlayer;
					nearest = { static_cast<int>(i), 0.0f };
				}
			}
			return nearest;
		}

		// Nothing fits the focus and the position adds nothing: the combat AI's pick stands.
		if (!anyMatch && !positional && !stuck) {
			return {};
		}

		Pick best;
		best.score = -std::numeric_limits<float>::max();
		for (std::size_t i = 0; i < a_enemies.size(); ++i) {
			const auto& e = a_enemies[i];
			// Distance is never a reason to ignore an enemy: an ordered target is chased
			// wherever it is. Only the position (staying near you, holding back) limits reach,
			// and for melee, whether there is a way to them at all.
			if (!Allowed(e, a_position, a_ranged, a_tuning) || !reachable(e)) {
				continue;
			}
			// Your target and a marked enemy are orders, not leanings: while one is there,
			// nobody else is considered.
			if ((a_focus == Focus::kPlayerTarget || a_focus == Focus::kMarked) && anyMatch && !Matches(e, a_focus)) {
				continue;
			}

			float score = 0.0f;
			switch (a_focus) {
			case Focus::kStrongest:
				score += maxThreat > 0.0f ? 100.0f * (e.threat / maxThreat) : 0.0f;
				break;
			case Focus::kWeakest:
				score += 100.0f * (1.0f - std::clamp(e.health, 0.0f, 1.0f));
				break;
			case Focus::kEngine:
				break;
			default:
				if (Matches(e, a_focus)) {
					score += a_focus == Focus::kMarked ? 400.0f : 100.0f;
				}
				break;
			}

			if (a_position == Position::kFrontLine) {
				score += std::clamp(e.forward, -1500.0f, 1500.0f) * 0.03f;
			}
			if (a_position == Position::kGuard) {
				score -= e.distanceToPlayer * a_tuning.distanceWeight;
			}

			// Nearer is better up to engage range; beyond it distance stops counting, so a far
			// enemy that fits the focus still wins over a near one that does not.
			score -= std::min(e.distance, a_tuning.engageRange) * a_tuning.distanceWeight;
			if (e.current) {
				score += a_tuning.stickiness;
			}
			if (e.engineChoice) {
				score += 10.0f;
			}
			if (e.attackingSelf) {
				score += 20.0f;
			}
			// Someone on them from close by is dealt with first: no leaning is worth being cut
			// down for. Your target and a marked enemy are orders and still stand.
			const bool defending = e.attackingSelf && e.distance <= a_tuning.defendRange && a_focus != Focus::kPlayerTarget &&
			                       a_focus != Focus::kMarked;
			if (defending) {
				score += 150.0f;
			}
			if (e.attackingPlayer) {
				score += 10.0f;
			}
			if (a_focus != Focus::kPlayerTarget && a_focus != Focus::kMarked) {
				score -= a_tuning.spreadPenalty * static_cast<float>(std::max(e.alliesOn, 0));
			}

			if (score > best.score) {
				best = { static_cast<int>(i), score, defending };
			}
		}
		return best;
	}

	int PotionTier(int a_level, int a_from, int a_step, int a_tiers)
	{
		if (a_tiers <= 0) {
			return 0;
		}
		return std::clamp((a_level - a_from) / std::max(a_step, 1), 0, a_tiers - 1);
	}

	Danger DangerFor(int a_followerLevel, int a_enemyLevel, bool a_boss, const Tuning& a_tuning)
	{
		const int gap = a_enemyLevel + (a_boss ? a_tuning.bossLevelBonus : 0) - a_followerLevel;
		if (gap <= -10) {
			return Danger::kTrivial;
		}
		if (gap <= -4) {
			return Danger::kEasy;
		}
		if (gap <= 3) {
			return Danger::kEven;
		}
		if (gap <= std::max(4, TauntReach(a_followerLevel, a_tuning))) {
			return Danger::kHard;
		}
		return Danger::kDeadly;
	}
}
