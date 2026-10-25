#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "FWM/Text.h"

// A follower's unique traits: what their own mod gave them on top of the combat AI. A perk
// that casts something on a swing, a script that answers a fight starting, a nerve that breaks
// when they are hurt. Rarity counts distinct, unlocked abilities, never drawbacks. No game types
// here: the tests check it.

namespace FWM::Traits
{
	enum class Kind : std::uint8_t
	{
		kStrike,    // a move made at the enemy: a scripted strike, a blink behind them
		kEvade,     // out of harm's way for a moment: unseen, untouchable
		kSummon,    // help called in
		kAura,      // something worn around them
		kOnHit,     // their blows carry something extra
		kInstinct,  // their mod's own script answers the fight
		kNerve,     // they may run when hurt
		kCount
	};

	enum class Rarity : std::uint8_t
	{
		kCommon,
		kUncommon,
		kRare,
		kEpic,
		kLegendary,
		kCount
	};

	// One trait, as found and as named.
	struct Trait
	{
		std::string   name;
		std::string   when;  // how it comes about, in the player's language
		std::string   what;  // what it does, when a notes file says
		Kind          kind{ Kind::kStrike };
		bool          locked{ false };  // their mod keeps it back for now (a quest not done yet)
		std::uint32_t key{ 0 };         // the perk, spell or quest it came from
	};

	[[nodiscard]] constexpr Rarity RarityFor(std::size_t a_count)
	{
		return a_count >= 4 ? Rarity::kLegendary : static_cast<Rarity>(a_count);
	}

	// Shared overhaul/framework records are not signature abilities. Notes can explicitly
	// identify a companion's ability supplied by another plugin.
	[[nodiscard]] bool SignatureSource(std::string_view a_source, std::string_view a_owner, bool a_noted = false);

	// Locked abilities and morale drawbacks are shown but do not increase rarity.
	[[nodiscard]] std::size_t Counted(const std::vector<Trait>& a_traits);

	// Two rows of two chips at most. Reserve the last chip for overflow when needed;
	// this affects presentation only, never the rarity calculation.
	inline constexpr std::size_t kCardSlots = 4;
	[[nodiscard]] constexpr std::size_t CardVisible(std::size_t a_count)
	{
		return a_count > kCardSlots ? kCardSlots - 1 : a_count;
	}

	[[nodiscard]] constexpr const char* RarityName(Rarity a_rarity)
	{
		switch (a_rarity) {
		case Rarity::kUncommon: return Ntr("Uncommon");
		case Rarity::kRare: return Ntr("Rare");
		case Rarity::kEpic: return Ntr("Epic");
		case Rarity::kLegendary: return Ntr("Legendary");
		default: return Ntr("Common");
		}
	}

	[[nodiscard]] constexpr const char* KindName(Kind a_kind)
	{
		switch (a_kind) {
		case Kind::kEvade: return Ntr("Evade");
		case Kind::kSummon: return Ntr("Summon");
		case Kind::kAura: return Ntr("Aura");
		case Kind::kOnHit: return Ntr("On hit");
		case Kind::kInstinct: return Ntr("Instinct");
		case Kind::kNerve: return Ntr("Nerve");
		default: return Ntr("Strike");
		}
	}

	// When a perk's spell goes off, read from the perk's conditions.
	enum class Moment : std::uint8_t
	{
		kSwing,
		kPowerAttack,
		kHit,
		kBash,
		kFightStart,
		kInFight
	};

	struct Trigger
	{
		Moment moment{ Moment::kSwing };
		int    chance{ 100 };                 // percent
		float  healthBelow{ -1.0f };          // theirs, 0..1; below zero: no limit
		float  targetHealthAbove{ -1.0f };    // their target's
	};

	// "On a power attack, 18% of the time", "On a swing while below 40% health, 40% of the time".
	[[nodiscard]] std::string Describe(const Trigger& a_trigger);

	// The game's Confidence, 0 Cowardly to 4 Foolhardy: below Brave its morale check sends them
	// running once hurt badly enough, the lower the sooner.
	[[nodiscard]] constexpr bool LosesNerve(int a_confidence) { return a_confidence >= 0 && a_confidence < 3; }
	[[nodiscard]] std::string NerveText(int a_confidence);

	// A name for the player from the names a plugin gave the spell and the perk: the follower's
	// own name and words such as "Spell", "Perk" or "Effect" taken off, the spell's name first.
	[[nodiscard]] std::string CleanName(std::string_view a_name, std::string_view a_owner);
	[[nodiscard]] std::string PickName(std::string_view a_spell, std::string_view a_perk, std::string_view a_owner);

	// Traits of the same name are one trait (a move a script starts and a perk starts too): their
	// moments joined. Available moves come first, then locked moves and drawbacks;
	// discovery order is preserved within each group. Locked only if every part is.
	void Merge(std::vector<Trait>& a_traits);
}
