#pragma once

#include "FWM/Orders.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

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
		float holdSeconds{ 10.0f };      // seconds an enemy stays on the tank
		float holdSecondsAt30{ 15.0f };  // from tank level 30
		float holdSecondsAt60{ 30.0f };  // from tank level 60
		float cooldownBase{ 14.0f };
		float cooldownPerLevel{ 0.05f };
		float cooldownMin{ 8.0f };
		float tauntStamina{ 25.0f };
		float holdBreakDistance{ 3000.0f };
		float tauntRangedReach{ 3000.0f };  // archers and casters on the player are drawn from this far
		float tauntArmorBase{ 10.0f };      // armor a taunt braces the tank with, before level
		float tauntArmorPerLevel{ 1.5f };
		float tauntArmorMax{ 120.0f };

		// Heal: the whole party at once, not a spell
		float healCooldown{ 60.0f };
		float healAmountBase{ 40.0f };     // health given to each, before level
		float healAmountPerLevel{ 3.5f };  // more for each of the healer's levels
		float healAmountMax{ 400.0f };
		float healReach{ 4000.0f };        // everyone in the party this close to the healer

		// Rejuvenate: everyone's magicka comes back faster
		float rejuvenateSeconds{ 30.0f };
		float rejuvenateSecondsAt30{ 60.0f };   // from healer level 30
		float rejuvenateSecondsAt60{ 180.0f };  // from healer level 60
		float rejuvenateBonus{ 100.0f };        // percent faster

		// Targeting
		float engageRange{ 2500.0f };    // distance counts against a candidate up to here; it rules nobody out
		float guardRadius{ 700.0f };     // Guard you: enemies this close to the player
		float holdBackRadius{ 450.0f };  // Hold back, melee: enemies this close to the follower
		float withMeRadius{ 400.0f };    // With me: kept this close to the player, fighting what comes near
		float defendRange{ 500.0f };     // an enemy on the follower this close is fought first, whatever the focus
		float stickiness{ 25.0f };       // bonus for the current target
		float spreadPenalty{ 15.0f };    // per ally already on a candidate
		float distanceWeight{ 0.04f };   // score lost per unit of distance

		// Paths: percent per point spent on the companion's own role's path
		float bonusAttackSpeed{ 1.0f };  // fighter
		float bonusDamageTaken{ 1.0f };  // tank: less damage from every blow and harmful spell
		float bonusHealing{ 1.0f };      // healer
		float bonusDamage{ 1.0f };       // bruiser
		float bonusArrowsKept{ 1.0f };   // archer: the chance an arrow shot is not used up
		float bonusMagickaCost{ 1.0f };  // mage: less magicka for every spell
	};

	// --- Taunt -------------------------------------------------------------

	// Levels above their own a tank holds for certain.
	[[nodiscard]] int TauntReach(int a_tankLevel, const Tuning& a_tuning);

	// 0..1. Bosses and dragons count as bossLevelBonus higher.
	[[nodiscard]] float TauntChance(int a_tankLevel, int a_enemyLevel, bool a_boss, const Tuning& a_tuning);

	[[nodiscard]] float TauntRadius(float a_block, float a_heavyArmor, const Tuning& a_tuning);

	// Seconds an enemy stays on the tank: holdSeconds, longer from tank level 30 and again
	// from 60. Enemies far below the tank (cowed) stay longer still.
	[[nodiscard]] float HoldSeconds(int a_tankLevel, int a_enemyLevel, const Tuning& a_tuning);

	[[nodiscard]] float TauntCooldown(int a_tankLevel, const Tuning& a_tuning);

	// Armor a tank braces with after each taunt, for as long as the taunt holds: a little at
	// first, more as they rise, never past tauntArmorMax.
	[[nodiscard]] float TauntArmor(int a_tankLevel, const Tuning& a_tuning);

	// --- Magicka -----------------------------------------------------------

	// A caster out of magicka: not enough for the cheapest spell they would cast. Once out, they
	// stay out until there is enough for two casts or a third of their magicka, whichever is
	// more (a full pool at most), so they do not step back and forward every second.
	// No spell to cast (a_cost 0) is never out.
	[[nodiscard]] bool OutOfMagicka(float a_magicka, float a_maximum, float a_cost, bool a_wasOut);

	// --- Spells ------------------------------------------------------------

	enum class SpellTier : std::uint8_t
	{
		kNovice,
		kApprentice,
		kAdept,
		kExpert,
		kMaster,
		kCount
	};

	inline constexpr std::array<std::string_view, static_cast<std::size_t>(SpellTier::kCount)> tierNames{ { "Novice", "Apprentice", "Adept", "Expert",
		"Master" } };

	enum class School : std::uint8_t
	{
		kDestruction,
		kRestoration,
		kAlteration,
		kConjuration,
		kIllusion,
		kOther,
		kCount
	};

	inline constexpr std::array<std::string_view, static_cast<std::size_t>(School::kCount)> schoolNames{ { "Destruction", "Restoration", "Alteration",
		"Conjuration", "Illusion", "Other" } };

	// From the minimum skill of a spell's main effect: 0, 25, 50, 75 or 100.
	[[nodiscard]] SpellTier TierFromSkill(int a_minimumSkill);

	// The strongest attack spell a healer holds: Novice, Apprentice from level 10, Adept from
	// 25. Never Expert or Master.
	[[nodiscard]] SpellTier HealerTierCap(int a_level);

	// Whether a companion of this role may hold a spell in a hand. Mages: anything but a
	// heal. Healers: anything, with attack spells (a_attack: any hostile effect) up to
	// HealerTierCap unless a_healersAny. No other role chooses spells.
	[[nodiscard]] bool SpellAllowed(Role a_role, bool a_healing, bool a_attack, SpellTier a_tier, int a_level, bool a_healersAny);

	// --- Heal and Rejuvenate -----------------------------------------------

	// Health a healer's heal gives everyone it reaches: healAmountBase and more with each of
	// their levels, never past healAmountMax.
	[[nodiscard]] float TeamHealAmount(int a_level, const Tuning& a_tuning);

	// How long Rejuvenate quickens everyone's magicka: rejuvenateSeconds, longer from healer
	// level 30 and again from 60.
	[[nodiscard]] float RejuvenateSeconds(int a_level, const Tuning& a_tuning);

	// Twice as long as it lasts.
	[[nodiscard]] float RejuvenateCooldown(int a_level, const Tuning& a_tuning);

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
		bool  outOfReach{ false };    // this follower found no way to them on foot, lately
		int   alliesOn{ 0 };          // other party members already on it
	};

	struct Pick
	{
		int   index{ -1 };  // -1: leave it to the combat AI
		float score{ 0.0f };
		bool  defending{ false };  // chosen because they are on the follower from close by
	};

	// a_ranged: the follower fights at range (Archer, Mage, Healer), so Hold back
	// is about where they stand, not whom they shoot. A melee follower never gets an
	// enemy out of their reach, not even an ordered one, and is moved off one the
	// combat AI chose when anyone else can be reached.
	[[nodiscard]] Pick PickTarget(std::span<const EnemyView> a_enemies, Focus a_focus, Position a_position, bool a_ranged, const Tuning& a_tuning);

	[[nodiscard]] bool Matches(const EnemyView& a_enemy, Focus a_focus);

	// --- Out of a fight ----------------------------------------------------

	// Auto battle: whether a companion who sees an enemy starts the fight. Hold back means
	// waiting for it to come, and a sneaking player is not to be given away.
	[[nodiscard]] constexpr bool StartsFight(Position a_resolved, bool a_playerSneaking)
	{
		return a_resolved != Position::kHoldBack && !a_playerSneaking;
	}

	// Weapons drawn on sight go away once nothing has been in view for a_after seconds,
	// unless the player's are out: then theirs follow the player's, as they always have.
	[[nodiscard]] constexpr bool SheathesAfterSight(float a_clearFor, float a_after, bool a_playerDrawn)
	{
		return a_clearFor >= a_after && !a_playerDrawn;
	}

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

	// --- Path bonus --------------------------------------------------------

	// Every point spent on a role's path makes a companion of that role a little better at
	// what the role is for, a percent per point:
	//   Fighter  attack speed            Tank     less damage taken (75% at most)
	//   Healer   healing                 Bruiser  damage
	//   Archer   the chance an arrow shot is not used up (90% at most)
	//   Mage     less magicka for every spell (75% off at most)
	inline constexpr float kMostArrowsKept = 90.0f;
	inline constexpr float kMostCostCut = 75.0f;
	inline constexpr float kMostDamageCut = 75.0f;
	inline constexpr float kMostOtherBonus = 1000.0f;

	[[nodiscard]] float PathBonusPerPoint(Role a_role, const Tuning& a_tuning);
	// a_spent counts every point on the path: its ranks and the points put straight into the bonus.
	[[nodiscard]] float PathBonus(Role a_role, int a_spent, const Tuning& a_tuning);
	// The most a role's bonus reaches.
	[[nodiscard]] float PathBonusCap(Role a_role);
	// How many more points would still raise the bonus, with a_points already on the path: 0 once
	// another would change nothing.
	[[nodiscard]] int PathBonusRoom(Role a_role, int a_points, const Tuning& a_tuning);

	// A percent as the bonus shows it: whole when it is whole, else to a tenth.
	[[nodiscard]] std::string Percent(float a_percent);
	// The number with its sign: "+17%", "17%" (arrows kept), "-17%" (damage taken, magicka cost).
	[[nodiscard]] std::string PathBonusNumber(Role a_role, float a_percent);
	// What it is: "attack speed", "damage taken", "healing", "damage", "arrows kept", "magicka cost".
	[[nodiscard]] std::string_view PathBonusName(Role a_role);
	// Both: "+17% attack speed", "17% arrows kept", "-17% damage taken".
	[[nodiscard]] std::string PathBonusText(Role a_role, float a_percent);
}
