#pragma once

#include "FWM/Orders.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// Paths: a perk tree for each role. A companion earns a point for every level past a set
// one and spends it on their role's path: new abilities, ranks in the ones they have,
// branches that add to them, and traits. Two abilities are carried into a fight at a time.
//
// No game types here. Fight With Me and the Paths add-on both compile this file, the tests
// check it, and the battle plan preview draws from it.

namespace FWM::Paths
{
	// Every ability a companion can carry. Stored in saves by value: append only.
	enum class Ability : std::uint8_t
	{
		kNone,
		kTaunt,
		kBodyguard,  // Weak Will; saved slot
		kChaosDown,  // was Challenge: keep its saved ability value
		kStandFast,  // Last Bastion; saved slot
		kHeal,
		kRejuvenate,
		kWard,  // serialized ID retained for Azura's Moon
		kRenewal,
		kWarCry,
		kBerserk,
		kSunder,
		kEarthshaker,
		kDisarm,
		kSecondWind,
		kRiposte,
		kSpectralWolf,  // was Cleave: saves carrying Cleave carry the wolf
		kVolley,
		kDeadeye, // serialized ID retained for Explosive Arrow
		kPinningShot,  // Windstep: preserve serialized ordinal from Pinning Shot
		kArrowRain,
		kSurge,
		kBurst,
		kAtronachRetired,  // on no path any more; kept so the values after it keep their meaning
		kMagnus,
		kDragonBreath,
		kCount
	};

	inline constexpr std::size_t kAbilityCount = static_cast<std::size_t>(Ability::kCount);

	// Icons that are pictures rather than font glyphs (Interface/FightWithMe/*.svg, from
	// game-icons.net by Lorc), under codepoints of their own in the private use area the font
	// leaves alone. The menu draws the picture, or the glyph after "or" if it cannot be loaded.
	inline constexpr std::uint32_t kGlyphWolfHead = 0xEF00;   // or 0xF6D3, a dog
	inline constexpr std::uint32_t kGlyphBarbedSun = 0xEF01;  // or 0xF4BE, a hand holding a heart
	inline constexpr std::uint32_t kGlyphSteelHelm = 0xEF02;
	inline constexpr std::uint32_t kGlyphBastion = 0xEF03;
	inline constexpr std::size_t kSlots = 2;

	struct AbilityInfo
	{
		std::string_view name;
		std::uint32_t    glyph;  // Font Awesome 6 solid
		std::string_view own;    // what "on their own" means for it
	};

	inline constexpr std::array<AbilityInfo, kAbilityCount> abilities{ {
		{ "", 0, "" },
		{ "Taunt", 0xF0A1, "When nearby enemies pressure an ally who needs protection." },
		{ "Weak Will", kGlyphSteelHelm, "When several enemies attack them, or they are hurt under pressure." },
		{ "Chaos Down", 0xF6D1, "When two or more enemies are close around them." },
		{ "Last Bastion", kGlyphBastion, "When a nearby living ally falls below 25% health in combat." },
		{ "Healing Circle", kGlyphBarbedSun, "When anyone is down or drops below the line." },
		{ "Rejuvenate", 0xF5BB, "When a nearby caster is nearly out of magicka during a pressured fight." },
		{ "Azura's Moon", 0xF186, "When the party is under heavy attack or a wounded ally needs protection." },
		{ "Renewal", 0xF4D8, "When two or more of the party are below 70% health." },
		{ "War cry", 0xF028, "When three or more enemies close in on them, or two when they are hurt." },
		{ "Berserk", 0xF6DE, "When healthy enough to commit against a durable opponent or several close enemies." },
		{ "Sunder", 0xF0E3, "Against a healthy boss or stronger enemy within melee reach." },
		{ "Earthshaker", 0xF6E3, "When three or more enemies are close around them." },
		{ "Disarm", 0xF256, "Against a dangerous nearby weapon user, especially when hurt." },
		{ "Second Wind", 0xF72E, "When their health falls below 35%." },
		{ "Riposte", 0xF2F1, "When surrounded in melee, or hurt while facing a boss." },
		{ "Kynareth's Maw", kGlyphWolfHead, "When close in front of a durable opponent or a group, saving it on dying enemies." },
		{ "Volley", 0xF140, "Against a nearby boss or stronger enemy with enough health to justify the arrows." },
		{ "Explosive Arrow", 0xF1E2, "When enemies cluster around the target, or against a durable opponent." },
		{ "Windstep", 0xF72E, "When enemies crowd the archer; retreats into clear space." },
		{ "Arrow Rain", 0xF740, "When three or more enemies are bunched together." },
		{ "Arcane Surge", 0xE2CA, "When their magicka runs low in a fight." },
		{ "Elemental Burst", 0xF753, "Against a nearby cluster or durable enemy, with time between party abilities." },
		{ "", 0, "" },
		{ "Magnus", 0xF185, "When magicka is low against a nearby boss or several enemies." },
		{ "Dragon's Breath", 0xF6D5, "Against a durable enemy or nearby cluster in front, with room for the stream." },
	} };

	[[nodiscard]] constexpr const AbilityInfo& Info(Ability a_ability) { return abilities[static_cast<std::size_t>(a_ability)]; }

	// Every node of every path, by a key that never changes: saves store (role, perk, rank).
	// Append only. A perk can appear on more than one path (Ward and Spellward do), and is
	// ranked on each separately.
	enum class Perk : std::uint8_t
	{
		kNone,
		// Tank
		kTaunt,
		kTauntBrace,
		kTauntIntimidate,
		kBodyguard,  // Weak Will; saved slot
		kBodyguardInterpose,
		kBodyguardRush,
		kChaosDown,  // was Challenge; Humble and Duel become the two branches below
		kChaosDownWide,
		kChaosDownLingering,
		kStandFast,  // Last Bastion; saved slot
		kStandFastUnbroken,
		kStandFastBreath,
		kIronHide,
		kShieldMaster,
		kLastStand,
		// Healer
		kHeal,
		kHealInvigorate,  // the two were Swift Mending and Sun's Bane: ranks bought in them carry over
		kHealWide,
		kRejuvenate,
		kRejuvenateWellspring,
		kRejuvenateVigor,
		kWard,
		kWardShared,
		kWardSpellward,
		kRenewal,
		kRenewalRoots,
		kRenewalLifeline,
		kTriage,
		kSerenity,
		kGuardianSpirit,
		// Bruiser
		kWarCry,
		kWarCryRally,
		kWarCryUnnerve,
		kBerserk,
		kBerserkBloodlust,
		kBerserkThickSkull,
		kSunder,
		kSunderCrack,
		kSunderShatter,
		kEarthshaker,
		kEarthshakerAftershock,
		kEarthshakerFault,
		kHeavyHands,
		kMomentum,
		kExecutioner,
		// Fighter
		kDisarm,
		kDisarmExpose,
		kDisarmSweep,
		kSecondWind,
		kSecondWindResolve,
		kSecondWindUnyielding,
		kRiposte,
		kRiposteCounter,
		kRiposteDisarming,
		kSpectralWolf,    // the three were Cleave, Whirlwind and Lacerate: ranks bought in them carry over
		kWolfMaul,
		kWolfHemorrhage,
		kFlanker,
		kEndurance,
		kOpportunist,
		// Archer
		kVolley,
		kVolleyRallyFire,
		kVolleyBroadhead,
		kDeadeye, // serialized ID retained for Explosive Arrow
		kDeadeyeHawkeye,
		kDeadeyePatience,
		kPinningShot,  // Windstep: preserve serialized ordinal from Pinning Shot
		kPinningHeavy,
		kPinningTwin,
		kArrowRain,
		kArrowRainDownpour,
		kArrowRainPinning,
		kSteadyBreath,  // was Fletcher; retain the serialized perk ID
		kFineArrows,
		kSpellTouched,
		// Mage
		kSurge,
		kSurgeOverflow,
		kSurgeConduit,
		kWardMantle,
		kBurst,
		kBurstSearing,
		kBurstTwin,
		kAtronachRetired,
		kAtronachBondRetired,
		kAtronachStormRetired,
		kArcaneFlow,
		kSpellShield,
		kUnravel,
		kMagnus,
		kDragonBreath,
		kBreathReach,
		kBreathLingering,
		kCount
	};

	inline constexpr std::size_t kPerkCount = static_cast<std::size_t>(Perk::kCount);

	enum class Kind : std::uint8_t
	{
		kAbility,  // learned once, then ranked: carried into a fight in one of the two slots
		kBranch,   // adds to the ability above it; never ranked past it
		kTrait     // always on
	};

	struct Node
	{
		Perk          perk{ Perk::kNone };  // kNone: an empty place in the path
		Kind          kind{ Kind::kAbility };
		Ability       ability{ Ability::kNone };  // an ability node's own, or a branch's ability
		std::uint8_t  tier{ 0 };                  // 0: first two abilities; 1: level 35; 2: level 50
		std::uint8_t  most{ 3 };                  // highest rank
		std::uint8_t  base{ 0 };                  // no ranks are granted for free
		std::string_view name;
		std::uint32_t    glyph{ 0 };
		std::uint8_t     cost{ 1 };   // points for the first rank
		std::uint8_t     step{ 1 };   // additional points for each successive ability rank
		bool             capstone{ false };  // every other node on the path must be unlocked

		[[nodiscard]] constexpr bool Empty() const { return perk == Perk::kNone; }
	};

	// Each path, in a fixed order: four abilities, each followed by its two branches, then
	// three traits. The first two abilities are available to purchase immediately. A
	// capstone takes the fourth ability's place on its own, its two branch places left empty.
	inline constexpr std::size_t kNodes = 15;
	inline constexpr std::size_t kAbilitiesPerPath = 4;
	inline constexpr std::size_t kTraitsPerPath = 3;
	inline constexpr std::size_t kRoles = static_cast<std::size_t>(Role::kCount);
	inline constexpr std::array<int, 3> kAbilityLevels{ 0, 35, 50 };
	inline constexpr std::array<int, 3> kTraitLevels{ 0, 25, 50 };
	[[nodiscard]] constexpr int RequiredLevel(const Node& a_node, int a_rank)
	{
		if (a_node.kind == Kind::kTrait) return a_rank >= 1 && a_rank <= 3 ? kTraitLevels[a_rank - 1] : 0;
		return a_node.tier < kAbilityLevels.size() ? kAbilityLevels[a_node.tier] : 0;
	}
	[[nodiscard]] constexpr int RankCost(const Node& a_node, int a_rank)
	{
		return a_node.kind == Kind::kAbility ? a_node.cost + (a_rank - 1) * a_node.step : a_node.cost;
	}

	using Path = std::array<Node, kNodes>;

	namespace detail
	{
		constexpr Node A(Perk a_perk, Ability a_ability, std::uint8_t a_tier, std::string_view a_name, std::uint32_t a_glyph)
		{
			return { a_perk, Kind::kAbility, a_ability, a_tier, 3, 0, a_name, a_glyph };
		}
		constexpr Node B(Perk a_perk, Ability a_ability, std::uint8_t a_tier, std::uint8_t a_most, std::string_view a_name, std::uint32_t a_glyph)
		{
			return { a_perk, Kind::kBranch, a_ability, a_tier, a_most, 0, a_name, a_glyph };
		}
		constexpr Node T(Perk a_perk, std::uint8_t a_tier, std::string_view a_name, std::uint32_t a_glyph)
		{
			return { a_perk, Kind::kTrait, Ability::kNone, a_tier, 3, 0, a_name, a_glyph };
		}
		// The one ability at the top of a path, gated by mastery of its other nodes.
		constexpr Node C(Perk a_perk, Ability a_ability, std::uint8_t a_cost, std::uint8_t a_step, std::string_view a_name, std::uint32_t a_glyph)
		{
			return { a_perk, Kind::kAbility, a_ability, 2, 3, 0, a_name, a_glyph, a_cost, a_step, true };
		}
		constexpr Node E() { return { Perk::kNone, Kind::kBranch, Ability::kNone, 2, 0, 0, "", 0 }; }
	}

	// In Role order: Fighter, Tank, Healer, Bruiser, Archer, Mage.
	inline constexpr std::array<Path, kRoles> paths{ {
		// Fighter: the Blade
		{ {
			detail::A(Perk::kDisarm, Ability::kDisarm, 0, "Disarm", 0xF256),
			detail::B(Perk::kDisarmExpose, Ability::kDisarm, 0, 3, "Expose", 0xF3C1),
			detail::B(Perk::kDisarmSweep, Ability::kDisarm, 0, 1, "Sweep", 0xF063),
			detail::A(Perk::kSecondWind, Ability::kSecondWind, 0, "Second Wind", 0xF72E),
			detail::B(Perk::kSecondWindResolve, Ability::kSecondWind, 0, 3, "Resolve", 0xF3ED),
			detail::B(Perk::kSecondWindUnyielding, Ability::kSecondWind, 0, 3, "Unyielding", 0xF254),
			detail::A(Perk::kRiposte, Ability::kRiposte, 1, "Riposte", 0xF2F1),
			detail::B(Perk::kRiposteCounter, Ability::kRiposte, 1, 3, "Counter", 0xF0E7),
			detail::B(Perk::kRiposteDisarming, Ability::kRiposte, 1, 1, "Disarming Parry", 0xF256),
			detail::A(Perk::kSpectralWolf, Ability::kSpectralWolf, 2, "Kynareth's Maw", kGlyphWolfHead),
			detail::B(Perk::kWolfMaul, Ability::kSpectralWolf, 2, 1, "Maul", 0xF5C9),
			detail::B(Perk::kWolfHemorrhage, Ability::kSpectralWolf, 2, 3, "Hemorrhage", 0xF043),
			detail::T(Perk::kFlanker, 0, "Flanker", 0xF074),
			detail::T(Perk::kEndurance, 1, "Endurance", 0xF21E),
			detail::T(Perk::kOpportunist, 2, "Opportunist", 0xF140),
		} },
		// Tank: the Shield
		{ {
			detail::A(Perk::kTaunt, Ability::kTaunt, 0, "Taunt", 0xF0A1),
			detail::B(Perk::kTauntBrace, Ability::kTaunt, 0, 3, "Brace", 0xF3ED),
			detail::B(Perk::kTauntIntimidate, Ability::kTaunt, 0, 3, "Intimidate", 0xF54C),
			detail::A(Perk::kBodyguard, Ability::kBodyguard, 0, "Weak Will", kGlyphSteelHelm),
			detail::B(Perk::kBodyguardInterpose, Ability::kBodyguard, 0, 3, "Lingering Dread", 0xF505),
			detail::B(Perk::kBodyguardRush, Ability::kBodyguard, 0, 1, "Unyielding Will", 0xF70C),
			detail::A(Perk::kChaosDown, Ability::kChaosDown, 1, "Chaos Down", 0xF6D1),
			detail::B(Perk::kChaosDownWide, Ability::kChaosDown, 1, 3, "Widening Chaos", 0xF0B2),
			detail::B(Perk::kChaosDownLingering, Ability::kChaosDown, 1, 3, "Lingering Chaos", 0xF252),
			detail::A(Perk::kStandFast, Ability::kStandFast, 2, "Last Bastion", kGlyphBastion),
			detail::B(Perk::kStandFastUnbroken, Ability::kStandFast, 2, 3, "Sheltering Reach", 0xF0D0),
			detail::B(Perk::kStandFastBreath, Ability::kStandFast, 2, 3, "Hold the Line", 0xF72E),
			detail::T(Perk::kIronHide, 0, "Iron Hide", 0xF3ED),
			detail::T(Perk::kShieldMaster, 1, "Shield Master", 0xF132),
			detail::T(Perk::kLastStand, 2, "Last Stand", 0xF5D7),
		} },
		// Healer: the Mender
		{ {
			detail::A(Perk::kHeal, Ability::kHeal, 0, "Healing Circle", kGlyphBarbedSun),
			detail::B(Perk::kHealInvigorate, Ability::kHeal, 0, 3, "Invigorate", 0xF70C),
			detail::B(Perk::kHealWide, Ability::kHeal, 0, 3, "Wide Circle", 0xF0B2),
			detail::A(Perk::kRenewal, Ability::kRenewal, 0, "Renewal", 0xF4D8),
			detail::B(Perk::kRenewalRoots, Ability::kRenewal, 0, 3, "Deep Roots", 0xF06C),
			detail::B(Perk::kRenewalLifeline, Ability::kRenewal, 0, 1, "Lifeline", 0xF0C1),
			detail::A(Perk::kRejuvenate, Ability::kRejuvenate, 1, "Rejuvenate", 0xF5BB),
			detail::B(Perk::kRejuvenateWellspring, Ability::kRejuvenate, 1, 3, "Wellspring", 0xF043),
			detail::B(Perk::kRejuvenateVigor, Ability::kRejuvenate, 1, 3, "Vigor", 0xF21E),
			detail::A(Perk::kWard, Ability::kWard, 2, "Azura's Moon", 0xF186),
			detail::B(Perk::kWardShared, Ability::kWard, 2, 3, "Moon's Reach", 0xF0C0),
			detail::B(Perk::kWardSpellward, Ability::kWard, 2, 3, "Lingering Grace", 0xF0D0),
			detail::T(Perk::kTriage, 0, "Triage", 0xF479),
			detail::T(Perk::kSerenity, 1, "Serenity", 0xF186),
			detail::T(Perk::kGuardianSpirit, 2, "Guardian Spirit", 0xF6E2),
		} },
		// Bruiser: the Hammer
		{ {
			detail::A(Perk::kWarCry, Ability::kWarCry, 0, "War cry", 0xF028),
			detail::B(Perk::kWarCryRally, Ability::kWarCry, 0, 3, "Rallying Roar", 0xF0C0),
			detail::B(Perk::kWarCryUnnerve, Ability::kWarCry, 0, 3, "Unnerve", 0xF54C),
			detail::A(Perk::kBerserk, Ability::kBerserk, 0, "Berserk", 0xF6DE),
			detail::B(Perk::kBerserkBloodlust, Ability::kBerserk, 0, 3, "Bloodlust", 0xF043),
			detail::B(Perk::kBerserkThickSkull, Ability::kBerserk, 0, 3, "Thick Skull", 0xF5D7),
			detail::A(Perk::kSunder, Ability::kSunder, 1, "Sunder", 0xF0E3),
			detail::B(Perk::kSunderCrack, Ability::kSunder, 1, 3, "Crack", 0xF0E7),
			detail::B(Perk::kSunderShatter, Ability::kSunder, 1, 3, "Shatter", 0xF3A5),
			detail::A(Perk::kEarthshaker, Ability::kEarthshaker, 2, "Earthshaker", 0xF6E3),
			detail::B(Perk::kEarthshakerAftershock, Ability::kEarthshaker, 2, 1, "Aftershock", 0xF2F1),
			detail::B(Perk::kEarthshakerFault, Ability::kEarthshaker, 2, 3, "Fault Line", 0xF6FC),
			detail::T(Perk::kHeavyHands, 0, "Heavy Hands", 0xF6E3),
			detail::T(Perk::kMomentum, 1, "Momentum", 0xF70C),
			detail::T(Perk::kExecutioner, 2, "Executioner", 0xF714),
		} },
		// Archer: the Arrow
		{ {
			detail::A(Perk::kVolley, Ability::kVolley, 0, "Volley", 0xF140),
			detail::B(Perk::kVolleyRallyFire, Ability::kVolley, 0, 3, "Rally Fire", 0xF0C0),
			detail::B(Perk::kVolleyBroadhead, Ability::kVolley, 0, 3, "Broadhead", 0xF3C1),
			detail::A(Perk::kDeadeye, Ability::kDeadeye, 0, "Explosive Arrow", 0xF1E2),
			detail::B(Perk::kDeadeyeHawkeye, Ability::kDeadeye, 0, 3, "Blast Radius", 0xF065),
			detail::B(Perk::kDeadeyePatience, Ability::kDeadeye, 0, 3, "High Explosive", 0xF0E7),
			detail::A(Perk::kPinningShot, Ability::kPinningShot, 1, "Windstep", 0xF72E),
			detail::B(Perk::kPinningHeavy, Ability::kPinningShot, 1, 3, "Tailwind", 0xF72E),
			detail::B(Perk::kPinningTwin, Ability::kPinningShot, 1, 1, "Fleet Footed", 0xF54B),
			detail::A(Perk::kArrowRain, Ability::kArrowRain, 2, "Arrow Rain", 0xF740),
			detail::B(Perk::kArrowRainDownpour, Ability::kArrowRain, 2, 3, "Downpour", 0xF73D),
			detail::B(Perk::kArrowRainPinning, Ability::kArrowRain, 2, 1, "Pinning Rain", 0xF601),
			detail::T(Perk::kSteadyBreath, 0, "Steady Breath", 0xF72E),
			detail::T(Perk::kFineArrows, 1, "Fine Arrows", 0xF124),
			detail::T(Perk::kSpellTouched, 2, "Spell-Touched", 0xF0D0),
		} },
		// Mage: the Storm
		{ {
			detail::A(Perk::kSurge, Ability::kSurge, 0, "Arcane Surge", 0xE2CA),
			detail::B(Perk::kSurgeOverflow, Ability::kSurge, 0, 3, "Overflow", 0xF043),
			detail::B(Perk::kSurgeConduit, Ability::kSurge, 0, 3, "Conduit", 0xF0C0),
			detail::A(Perk::kBurst, Ability::kBurst, 0, "Elemental Burst", 0xF753),
			detail::B(Perk::kBurstSearing, Ability::kBurst, 0, 3, "Searing", 0xF7E4),
			detail::B(Perk::kBurstTwin, Ability::kBurst, 0, 1, "Twin Cast", 0xF0C1),
			detail::A(Perk::kDragonBreath, Ability::kDragonBreath, 1, "Dragon's Breath", 0xF6D5),
			detail::B(Perk::kBreathReach, Ability::kDragonBreath, 1, 3, "Dragon's Reach", 0xF337),
			detail::B(Perk::kBreathLingering, Ability::kDragonBreath, 1, 3, "Lingering Breath", 0xF252),
			detail::C(Perk::kMagnus, Ability::kMagnus, 1, 1, "Magnus", 0xF185),
			detail::E(),
			detail::E(),
			detail::T(Perk::kArcaneFlow, 0, "Arcane Flow", 0xF043),
			detail::T(Perk::kSpellShield, 1, "Spell Shield", 0xF0D0),
			detail::T(Perk::kUnravel, 2, "Unravel", 0xF7E4),
		} },
	} };

	[[nodiscard]] constexpr const Path& PathOf(Role a_role) { return paths[static_cast<std::size_t>(a_role)]; }

	struct PathInfo
	{
		std::string_view title;  // "Path of the Shield"
		std::string_view lead;   // one line on what the path is for
	};

	inline constexpr std::array<PathInfo, kRoles> pathInfo{ {
		{ "Path of the Blade", "Steel and nerve: take their weapons, turn their blows, loose the wolf spirit." },
		{ "Path of the Shield", "Hold the line: draw every eye, stand between you and harm, steel the party." },
		{ "Path of the Mender", "Keep everyone standing: a circle of healing light, quickened magicka, wards and slow renewal." },
		{ "Path of the Hammer", "Break them: roars that stagger, fury, shattered guards and a shaking earth." },
		{ "Path of the Arrow", "Death from range: volleys, explosive arrows, windsteps and a rain of arrows." },
		{ "Path of the Storm", "Spell pressure: surging magicka, wards, elemental bursts, and at its height, Magnus." },
	} };

	[[nodiscard]] constexpr const PathInfo& Info(Role a_role) { return pathInfo[static_cast<std::size_t>(a_role)]; }

	// Index of a perk on a path, or -1.
	[[nodiscard]] int IndexOf(Role a_role, Perk a_perk);

	// Index of an ability's node on a path, or -1 when the path has no such ability.
	[[nodiscard]] int IndexOf(Role a_role, Ability a_ability);

	// The ability node a branch hangs from, or -1 for anything else.
	[[nodiscard]] int ParentOf(Role a_role, std::size_t a_index);

	// The two abilities a path starts with.
	[[nodiscard]] std::array<Ability, kSlots> Defaults(Role a_role);

	// The five abilities Fight With Me had as each role's special before the paths.
	[[nodiscard]] constexpr Ability AbilityOf(Special a_special)
	{
		switch (a_special) {
		case Special::kBodyguard:
			return Ability::kBodyguard;
		case Special::kVolley:
			return Ability::kVolley;
		case Special::kWarCry:
			return Ability::kWarCry;
		case Special::kWard:
			return Ability::kWard;
		case Special::kDisarm:
			return Ability::kDisarm;
		default:
			return Ability::kNone;
		}
	}

	[[nodiscard]] constexpr Special SpecialFor(Ability a_ability)
	{
		switch (a_ability) {
		case Ability::kBodyguard:
			return Special::kBodyguard;
		case Ability::kVolley:
			return Special::kVolley;
		case Ability::kWarCry:
			return Special::kWarCry;
		case Ability::kWard:
			return Special::kWard;
		case Ability::kDisarm:
			return Special::kDisarm;
		default:
			return Special::kNone;
		}
	}

	// --- One companion's progress -----------------------------------------------

	struct Record
	{
		std::array<std::array<std::uint8_t, kNodes>, kRoles> ranks{};  // purchased ranks
		std::array<std::array<Ability, kSlots>, kRoles>      slots{};  // what each path carries into a fight
		std::uint16_t                                        peak{ 0 };  // highest level seen
		std::array<std::uint16_t, kRoles>                    bonus{};  // points put straight into each path's bonus

		friend bool operator==(const Record&, const Record&) = default;
	};

	// A record for someone just met: no ranks learned and no abilities carried.
	[[nodiscard]] Record Fresh();

	// Points earned at this level: one for each level past a_above.
	[[nodiscard]] int Earned(int a_level, int a_above);

	// Points on a path: its ranks and what went straight into its bonus. Every path's together
	// is what a companion has spent of their own points.
	[[nodiscard]] int Spent(const Record& a_record, Role a_role);
	[[nodiscard]] int Spent(const Record& a_record);
	// Only the ranks: how far along mastering the path they are.
	[[nodiscard]] int OnTree(const Record& a_record, Role a_role);
	// Only what went into the bonus.
	[[nodiscard]] int Invested(const Record& a_record, Role a_role);
	// Every node on the path at its highest rank, the capstone included.
	[[nodiscard]] bool Mastered(const Record& a_record, Role a_role);

	// Ability ranks cost 1/2/3 points; branches and traits cost one per rank.
	[[nodiscard]] int CostOf(Role a_role, std::size_t a_index, int a_rank);
	// Points to bring everything on a path to its highest rank.
	[[nodiscard]] int Mastery(Role a_role);

	// Points on a path spent in tiers below a_tier, and whether that tier has opened.
	[[nodiscard]] int  SpentBelow(const Record& a_record, Role a_role, int a_tier);
	[[nodiscard]] bool TierOpen(const Record& a_record, Role a_role, int a_tier);

	[[nodiscard]] int Rank(const Record& a_record, Role a_role, Perk a_perk);
	[[nodiscard]] int Rank(const Record& a_record, Role a_role, Ability a_ability);
	[[nodiscard]] bool Learned(const Record& a_record, Role a_role, Ability a_ability);

	enum class Block : std::uint8_t
	{
		kNone,
		kMaxed,       // at its highest rank
		kNoPoints,    // nothing left to spend
		kTier,        // not enough spent on this path yet
		kParent,      // a branch cannot outrank its ability
		kDefault,     // the starting rank of a default ability cannot be given back
		kHolds,       // other ranks on this path stand on this one
		kNotLearned,  // equip: not learned
		kNothing,     // forget: no rank bought
		kBadNode,
		kNoRespec,    // giving points back is switched off
		kCapstone,    // every other node on the path must be mastered first
		kNoUnlimited, // unlimited points are switched off
		kNotMastered, // points go into the bonus only once the whole path is mastered
		kBonusFull,   // the bonus is at its highest: another point would change nothing
		kLevel       // the next innate trait rank needs level 25 or 50
	};

	[[nodiscard]] std::string_view Why(Block a_block);
	[[nodiscard]] std::string Requirement(Block block, const Node& node, int nextRank);

	// Whether one more rank can be bought, with a_earned points earned in all.
	[[nodiscard]] Block CanLearn(const Record& a_record, Role a_role, std::size_t a_index, int a_earned, bool a_debug = false);
	// Whether one rank can be given back and the path still stands.
	[[nodiscard]] Block CanForget(const Record& a_record, Role a_role, std::size_t a_index);

	Block Learn(Record& a_record, Role a_role, std::size_t a_index, int a_earned, bool a_debug = false);
	Block Forget(Record& a_record, Role a_role, std::size_t a_index);
	// Every point on a path back; all ranks and carried slots are cleared.
	void  Reset(Record& a_record, Role a_role);

	// Carries an ability in a slot. Already carried in the other slot, the two swap places.
	Block Equip(Record& a_record, Role a_role, std::size_t a_slot, Ability a_ability);

	// A leftover point straight into a mastered path's bonus. a_room is how many more the bonus
	// still takes (Fight With Me knows what a point is worth and where the bonus stops).
	[[nodiscard]] Block CanInvest(const Record& a_record, Role a_role, int a_earned, int a_room);
	Block Invest(Record& a_record, Role a_role, int a_earned, int a_room);
	// One point back out of a path's bonus.
	[[nodiscard]] Block CanDivest(const Record& a_record, Role a_role);
	Block Divest(Record& a_record, Role a_role);

	// Back to an earlier copy's ranks: the record as it was before unlimited points. What is carried
	// stays where it is still learned; a slot whose ability is gone takes the first learned ability
	// not already carried, defaults first. The higher level seen is kept.
	void Restore(Record& a_record, const Record& a_before);

	// Points a companion with unlimited points has: more than every path could ever take.
	inline constexpr int kUnlimited = 9999;

	// After a load: ranks clamped to the current tree and trait level gates, slots holding
	// only learned abilities of the path, and, if more is spent than earned (the level limit
	// was raised in the INI), points come back until it fits: first those in a bonus, then
	// from the top of the path with the most spent. True when anything changed.
	bool Repair(Record& a_record, int a_earned, bool a_debug = false);

	// Changes whenever the shape of the trees does, so two DLLs built from different versions
	// of this file can tell they disagree.
	[[nodiscard]] std::uint32_t Signature();

	// The co-save record: (follower, record) pairs. Nodes are stored by perk, so a tree that
	// gains, loses or reorders nodes still loads; run Repair after. Version 2 adds the bonus
	// points after every entry, where version 1 readers never look.
	// Version 3 resets older investments once, retaining earned levels and refunding all points.
	inline constexpr std::uint32_t kRecordVersion = 4;
	using Entry = std::pair<std::uint32_t, Record>;
	[[nodiscard]] std::vector<std::uint8_t> Encode(std::span<const Entry> a_entries);
	[[nodiscard]] std::vector<Entry>        Decode(std::span<const std::uint8_t> a_bytes, std::uint32_t a_version);

	// --- Every companion's record --------------------------------------------------------

	// What the add-on keeps: a record for each companion, and with it their own points, earned
	// from their own level and spent only on their own paths. Nothing here is shared between two.
	class Book
	{
	public:
		struct Points
		{
			int  above{ 10 };        // a point for every level past this one
			int  extra{ 0 };         // points every companion has on top
			bool respec{ true };     // ranks and bonus points can be given back
			bool unlimited{ true };  // unlimited points can be switched on
		};
		Points points;

		// The record, made on first sight with every point their level has already earned. A
		// level above the highest seen is noted. a_level 0: only find it.
		Record*                     Meet(std::uint32_t a_id, int a_level);
		[[nodiscard]] const Record* Find(std::uint32_t a_id) const;

		[[nodiscard]] int  EarnedAt(int a_level) const;
		[[nodiscard]] int  Earned(std::uint32_t a_id) const;  // kUnlimited while unlimited points are on
		[[nodiscard]] int  Unspent(std::uint32_t a_id) const;
		[[nodiscard]] bool Unlimited(std::uint32_t a_id) const;

		Block Learn(std::uint32_t a_id, Role a_role, std::size_t a_node);
		Block Forget(std::uint32_t a_id, Role a_role, std::size_t a_node);
		Block Reset(std::uint32_t a_id, Role a_role);
		Block Equip(std::uint32_t a_id, Role a_role, std::size_t a_slot, Ability a_ability);
		Block Invest(std::uint32_t a_id, Role a_role, int a_room);
		Block Divest(std::uint32_t a_id, Role a_role);
		// Switched on, their record as it is now is kept aside; switched off, they go back to it.
		Block SetUnlimited(std::uint32_t a_id, bool a_on);

		// For the co-save: every record, and those kept aside under unlimited points.
		[[nodiscard]] std::vector<Entry> Records() const;
		[[nodiscard]] std::vector<Entry> KeptAside() const;
		// From it, with ids already resolved: each record set right against its own points.
		// Returns how many needed it.
		int Load(std::vector<Entry> a_records, std::vector<Entry> a_keptAside);
		void Clear();

	private:
		std::unordered_map<std::uint32_t, Record> records;
		std::unordered_map<std::uint32_t, Record> before;  // with unlimited points on: what they had
	};

	// --- What each rank does -----------------------------------------------------

	// Seconds an ability waits before it can be used again, with its ranks and branches.
	[[nodiscard]] float Cooldown(const Record& a_record, Role a_role, Ability a_ability);

	// One rank of one node, in a sentence: what it does at that rank. Rank 0 describes rank 1.
	[[nodiscard]] std::string Describe(Perk a_perk, int a_rank);

	// The same rank in a few words, for a ladder of ranks side by side: "+60 armor", "15 s".
	[[nodiscard]] std::string Measure(Perk a_perk, int a_rank);

	// The value a perk's rank sets, for the code that carries it out. 0 at rank 0.
	[[nodiscard]] float Value(Perk a_perk, int a_rank);

	// A second number some perks set (War cry's stagger, Earthshaker's damage, Volley's
	// seconds, Second Wind's stamina, Spell Shield's absorption).
	[[nodiscard]] float Second(Perk a_perk, int a_rank);

	// A third: Magnus's resistance to magic, at its last rank.
	[[nodiscard]] float Third(Perk a_perk, int a_rank);

	// Fixed numbers the abilities share with their descriptions.
	inline constexpr float kStandFastSeconds = 8.0f;
	inline constexpr float kStandFastReach = 800.0f;
	inline constexpr float kRallyReach = 800.0f;
	inline constexpr float kUnnerveSeconds = 8.0f;
	inline constexpr float kBerserkSeconds = 10.0f;
	inline constexpr float kSunderSeconds = 12.0f;
	inline constexpr float kExposeSeconds = 10.0f;
	inline constexpr float kResolveSeconds = 10.0f;
	inline constexpr float kWolfReach = 700.0f;       // how far off the target the fighter may stand
	inline constexpr float kWolfArc = 60.0f;          // degrees either side of where the target faces
	inline constexpr float kWolfBleedSeconds = 8.0f;  // before Hemorrhage
	inline constexpr float kWolfBossBleed = 0.5f;     // a boss bleeds this share of what others do
	inline constexpr std::array<float, 3> kWolfHold{ 2.5f, 2.5f, 3.3f };  // seconds the bite holds them frozen, by rank
	inline constexpr float kWolfMaulHold = 1.5f;      // and Maul holds them this much longer
	inline constexpr float kWolfBossHold = 0.5f;      // a boss is held this share of it
	inline constexpr float kWolfCatch = 0.2f;         // seconds into their stagger they are caught still
	inline constexpr float kRainRadius = 300.0f;
	inline constexpr float kRainSeconds = 1.5f;
	inline constexpr float kSurgeSeconds = 12.0f;
	inline constexpr float kSearingSeconds = 12.0f;
	inline constexpr std::array<float, 3> kBurstDamage{ 24.0f, 36.0f, 48.0f };
	inline constexpr float kBurstEchoShare = 0.50f;
	inline constexpr float kRenewalSeconds = 12.0f;
	inline constexpr float kWardSeconds = 60.0f;
	inline constexpr float kLastStandArmor = 100.0f;
	inline constexpr float kLastStandSeconds = 8.0f;
	inline constexpr float kGuardianLine = 0.2f;
	inline constexpr float kTriageLine = 0.33f;
	inline constexpr float kExecuteLine = 0.3f;
	inline constexpr float kRushReach = 300.0f;
	inline constexpr float kStormcallReach = 400.0f;
	inline constexpr float kLifelineHealth = 0.33f;
}
