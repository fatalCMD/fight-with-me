#pragma once

#include "FWM/Orders.h"

#include <cstdint>
#include <span>

// The numbers behind every order. Pure functions over plain values, so the
// battle plan can show exactly what the game will do and the tests can check it.

namespace FWM::Rules
{
	struct Tuning
	{
		// Taunt
		float tauntRadiusBase{ 800.0f };
		float tauntRadiusPerSkill{ 3.0f };  // per point of max(Block, Heavy Armor)
		float tauntRadiusMax{ 1200.0f };
		float tauntReachDivisor{ 4.0f };      // reach = level / divisor
		float tauntFalloffPerLevel{ 0.12f };  // chance lost per level over reach
		int   tauntFalloffLevels{ 8 };        // no chance beyond reach + this
		int   bossLevelBonus{ 5 };            // bosses and dragons count as this much higher
		int   cowedLevelGap{ 10 };            // this far below the tank: held longer
		float cowedHoldMult{ 1.5f };
		float holdBase{ 5.0f };
		float holdPerLevel{ 0.05f };
		float cooldownBase{ 14.0f };
		float cooldownPerLevel{ 0.05f };
		float cooldownMin{ 8.0f };
		float tauntStamina{ 25.0f };
		float holdBreakDistance{ 3000.0f };
		float tauntRangedReach{ 3000.0f };  // archers and casters on the player are drawn from this far

		// Heal
		float healSelfThreshold{ 0.40f };
		float healCooldown{ 15.0f };  // seconds between a healer's casts; never under 10
		float healRange{ 1000.0f };       // heals cast from where the healer stands
		float reviveReach{ 2500.0f };     // a downed companion this far is walked to
		float reviveDistance{ 200.0f };   // close enough to heal a downed companion by hand

		// Targeting
		float engageRange{ 2500.0f };    // distance counts against a candidate up to here; it rules nobody out
		float guardRadius{ 700.0f };     // Guard you: enemies this close to the player
		float holdBackRadius{ 450.0f };  // Hold back, melee: enemies this close to the follower
		float withMeRadius{ 400.0f };    // With me: kept this close to the player, fighting what comes near
		float defendRange{ 500.0f };     // an enemy on the follower this close is fought first, whatever the focus
		float stickiness{ 25.0f };       // bonus for the current target
		float spreadPenalty{ 15.0f };    // per ally already on a candidate
		float distanceWeight{ 0.04f };   // score lost per unit of distance
	};

	// --- Taunt -------------------------------------------------------------

	// Levels above their own a tank holds for certain.
	[[nodiscard]] int TauntReach(int a_tankLevel, const Tuning& a_tuning);

	// 0..1. Bosses and dragons count as bossLevelBonus higher.
	[[nodiscard]] float TauntChance(int a_tankLevel, int a_enemyLevel, bool a_boss, const Tuning& a_tuning);

	[[nodiscard]] float TauntRadius(float a_block, float a_heavyArmor, const Tuning& a_tuning);

	// Seconds an enemy stays on the tank.
	[[nodiscard]] float HoldSeconds(int a_tankLevel, int a_enemyLevel, const Tuning& a_tuning);

	[[nodiscard]] float TauntCooldown(int a_tankLevel, const Tuning& a_tuning);

	// --- Heal --------------------------------------------------------------

	struct HealCandidate
	{
		float health{ 1.0f };  // 0..1 of max
		float distance{ 0.0f };
		bool  attacked{ false };
		bool  self{ false };
		bool  player{ false };
		bool  bleeding{ false };
		bool  taken{ false };  // down, and another healer is already on their way
	};

	// The player under their line, always; otherwise the ally in the most trouble
	// under theirs, or -1. A downed companion counts from reviveReach, since the
	// healer walks to them; everyone else from healRange.
	[[nodiscard]] int PickHealTarget(std::span<const HealCandidate> a_candidates, float a_allyThreshold, const Tuning& a_tuning);

	// --- Targeting ---------------------------------------------------------

	struct EnemyView
	{
		float distance{ 0.0f };          // from the follower
		float distanceToPlayer{ 0.0f };  // from the player
		float forward{ 0.0f };           // along the player's facing; positive is ahead
		float threat{ 0.0f };
		float health{ 1.0f };  // 0..1
		bool  caster{ false };
		bool  healer{ false };
		bool  archer{ false };
		bool  attackingPlayer{ false };
		bool  attackingParty{ false };  // the player or any follower
		bool  attackingSelf{ false };
		bool  playerTarget{ false };
		bool  marked{ false };
		bool  current{ false };       // the follower's current target
		bool  engineChoice{ false };  // what the combat AI would pick
		int   alliesOn{ 0 };          // other party members already on it
	};

	struct Pick
	{
		int   index{ -1 };  // -1: leave it to the combat AI
		float score{ 0.0f };
		bool  defending{ false };  // chosen because they are on the follower from close by
	};

	// a_ranged: the follower fights at range (Archer, Mage, Healer), so Hold back
	// is about where they stand, not whom they shoot.
	[[nodiscard]] Pick PickTarget(std::span<const EnemyView> a_enemies, Focus a_focus, Position a_position, bool a_ranged, const Tuning& a_tuning);

	[[nodiscard]] bool Matches(const EnemyView& a_enemy, Focus a_focus);

	// --- Danger ------------------------------------------------------------

	enum class Danger : std::uint8_t
	{
		kTrivial,
		kEasy,
		kEven,
		kHard,
		kDeadly
	};

	// How an enemy compares with one follower, using the same reach a taunt uses.
	[[nodiscard]] Danger DangerFor(int a_followerLevel, int a_enemyLevel, bool a_boss, const Tuning& a_tuning);

	[[nodiscard]] constexpr bool IsRanged(Role a_role)
	{
		return a_role == Role::kArcher || a_role == Role::kMage || a_role == Role::kHealer;
	}
}
