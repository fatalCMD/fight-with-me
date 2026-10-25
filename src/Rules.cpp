#include "FWM/Rules.h"
#include "FWM/RejuvenatePolicy.h"

#include "FWM/Text.h"

#include <algorithm>
#include <cmath>
#include <format>
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
		float seconds = a_tankLevel >= 60 ? a_tuning.holdSecondsAt60 : (a_tankLevel >= 30 ? a_tuning.holdSecondsAt30 : a_tuning.holdSeconds);
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

	float TauntArmor(int a_tankLevel, const Tuning& a_tuning)
	{
		const float armor = a_tuning.tauntArmorBase + a_tuning.tauntArmorPerLevel * static_cast<float>(std::max(a_tankLevel, 1));
		return std::clamp(armor, 0.0f, std::max(a_tuning.tauntArmorMax, 0.0f));
	}

	bool OutOfMagicka(float a_magicka, float a_maximum, float a_cost, bool a_wasOut)
	{
		if (a_cost <= 0.0f || a_maximum <= 0.0f) {
			return false;
		}
		if (!a_wasOut) {
			return a_magicka < a_cost;
		}
		const float back = std::min(std::max(a_cost * 2.0f, a_maximum / 3.0f), a_maximum);
		return a_magicka < back;
	}

	SpellTier TierFromSkill(int a_minimumSkill)
	{
		return static_cast<SpellTier>(std::clamp(a_minimumSkill / 25, 0, static_cast<int>(SpellTier::kMaster)));
	}

	SpellTier HealerTierCap(int a_level)
	{
		return a_level >= 25 ? SpellTier::kAdept : (a_level >= 10 ? SpellTier::kApprentice : SpellTier::kNovice);
	}

	bool SpellAllowed(Role a_role, bool a_healing, bool a_attack, SpellTier a_tier, int a_level, bool a_healersAny)
	{
		switch (a_role) {
		case Role::kMage:
			return !a_healing;
		case Role::kHealer:
			return a_healing || !a_attack || a_healersAny || a_tier <= HealerTierCap(a_level);
		default:
			return false;
		}
	}

	float TeamHealAmount(int a_level, const Tuning& a_tuning)
	{
		const float amount = a_tuning.healAmountBase + a_tuning.healAmountPerLevel * static_cast<float>(std::max(a_level, 1));
		return std::clamp(amount, 0.0f, std::max(a_tuning.healAmountMax, 0.0f));
	}

	float RejuvenateSeconds(int a_level, const Tuning& a_tuning)
	{
		return RejuvenatePolicy::Seconds(a_level >= 60 ? a_tuning.rejuvenateSecondsAt60 : (a_level >= 30 ? a_tuning.rejuvenateSecondsAt30 : a_tuning.rejuvenateSeconds));
	}

	float RejuvenateCooldown(int a_level, const Tuning& a_tuning)
	{
		return RejuvenatePolicy::Cooldown(RejuvenateSeconds(a_level, a_tuning));
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
				// At the player's side. A bow or a spell reaches from there, so anyone will do,
				// nearest first. A blade only reaches whatever comes near them, whoever is on them
				// from close by, and whoever is on the follower within reach.
				if (a_ranged) {
					return true;
				}
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
			// Shooting from the player's side: the nearest enemy first, however far, as long as
			// the leaning has nothing better.
			if (a_position == Position::kWithMe && a_ranged) {
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

	float PathBonusPerPoint(Role a_role, const Tuning& a_tuning)
	{
		switch (a_role) {
		case Role::kFighter:
			return a_tuning.bonusAttackSpeed;
		case Role::kTank:
			return a_tuning.bonusDamageTaken;
		case Role::kHealer:
			return a_tuning.bonusHealing;
		case Role::kBruiser:
			return a_tuning.bonusDamage;
		case Role::kArcher:
			return a_tuning.bonusArcherStamina;
		case Role::kMage:
			return a_tuning.bonusMagickaCost;
		default:
			return 0.0f;
		}
	}

	float PathBonusCap(Role a_role)
	{
		switch (a_role) {
		case Role::kArcher:
			return kMostArcherStamina;
		case Role::kMage:
			return kMostCostCut;
		case Role::kTank:
			return kMostDamageCut;
		default:
			return kMostOtherBonus;
		}
	}

	float PathBonus(Role a_role, int a_spent, const Tuning& a_tuning)
	{
		const float percent = std::max(PathBonusPerPoint(a_role, a_tuning), 0.0f) * static_cast<float>(std::max(a_spent, 0));
		return std::min(percent, PathBonusCap(a_role));
	}

	int PathBonusRoom(Role a_role, int a_points, const Tuning& a_tuning)
	{
		const float per = PathBonusPerPoint(a_role, a_tuning);
		if (per <= 0.0f) {
			return 0;
		}
		// The first point count at which the bonus reaches its cap; a hair under, so 75 / 1 is 75.
		const int most = static_cast<int>(std::ceil(PathBonusCap(a_role) / per - 0.001f));
		return std::max(most - std::max(a_points, 0), 0);
	}

	std::string Percent(float a_percent)
	{
		const float tenths = std::round(a_percent * 10.0f) / 10.0f;
		return std::abs(tenths - std::round(tenths)) < 0.05f ? std::format("{:.0f}%", tenths) : std::format("{:.1f}%", tenths);
	}

	std::string PathBonusNumber(Role a_role, float a_percent)
	{
		const auto amount = Percent(a_percent);
		switch (a_role) {
		case Role::kTank:
		case Role::kMage:
			return a_percent > 0.0f ? "-" + amount : amount;
		default:
			return "+" + amount;
		}
	}

	std::string_view PathBonusName(Role a_role)
	{
		switch (a_role) {
		case Role::kFighter:
			return Tr("attack speed");
		case Role::kTank:
			return Tr("damage taken");
		case Role::kHealer:
			return Tr("healing");
		case Role::kBruiser:
			return Tr("damage");
		case Role::kArcher:
			return Tr("stamina recovery");
		case Role::kMage:
			return Tr("magicka cost");
		default:
			return "";
		}
	}

	std::string PathBonusText(Role a_role, float a_percent)
	{
		return std::format("{} {}", PathBonusNumber(a_role, a_percent), PathBonusName(a_role));
	}
}
