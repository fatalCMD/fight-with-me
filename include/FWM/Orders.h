#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// What the player asks of one follower. No game types here: the rules, the save
// format and the tests all share these.

namespace FWM
{
    // Creature keywords win over NPC on mixed/custom races. Missing race data fails closed.
    [[nodiscard]] constexpr bool RoleAllowed(bool a_npcRace, bool a_creatureRace, bool a_summon)
    {
        return a_npcRace && !a_creatureRace && !a_summon;
    }

	enum class Role : std::uint8_t
	{
		kFighter,
		kTank,
		kHealer,
		kBruiser,
		kArcher,
		kMage,
		kSkald,
		kCount
	};

	enum class Focus : std::uint8_t
	{
		kEngine,
		kCasters,
		kHealers,
		kArchers,
		kStrongest,
		kWeakest,
		kAttackingPlayer,
		kAttackingParty,
		kPlayerTarget,
		kMarked,
		kBackLine,  // casters, healers and archers: whoever fights from behind their line
		kCount
	};

	enum class Position : std::uint8_t
	{
		kRetired,  // was Walk With Me's formation; kept so saved values keep their meaning, read as Free
		kFree,
		kGuard,
		kHoldBack,
		kFrontLine,
		kWithMe,  // at your side and staying there: a blade fights what reaches you, a bow or spell anyone
		kCount
	};

	struct Orders
	{
		Role         role{ Role::kFighter };
		Focus        focus{ Focus::kEngine };
		Position     position{ Position::kFree };
		bool         autoAbility{ true };  // taunt or heal without being told
		std::uint8_t healThreshold{ 55 };  // percent, for a healer
		bool         autoSpecial{ true };  // the role's special ability without being told
		bool         autoRejuvenate{ true };  // a healer's Rejuvenate without being told
		std::uint32_t manual{ 0 };         // with the Paths add-on: abilities used only when told, one bit per Paths::Ability

		friend bool operator==(const Orders&, const Orders&) = default;
	};

	// The spell a mage or healer holds in each hand, by form id: left, then right. 0 leaves
	// that hand to their own choice.
	using Hands = std::array<std::uint32_t, 2>;
	inline constexpr std::size_t kLeftHand = 0;
	inline constexpr std::size_t kRightHand = 1;

	[[nodiscard]] constexpr bool ChoosesSpells(Role a_role) { return a_role == Role::kMage || a_role == Role::kHealer; }

	// Each role's special ability: a button in the battle plan, and a switch for using it
	// without being told.
	enum class Special : std::uint8_t
	{
		kNone,
		kBodyguard,  // tank: Weak Will (saved enum slot)
		kVolley,     // archer: everyone at range fires at one enemy
		kWarCry,     // bruiser: staggers everything around them
		kWard,       // mage, healer: hardens your skin with alteration
		kDisarm,     // fighter: knocks the weapon out of their opponent's hands
		kCount
	};

	struct SpecialInfo
	{
		std::string_view name;
		std::string_view summary;  // what it does
		std::string_view own;      // what "on their own" means for it
		std::uint32_t    glyph;    // Font Awesome 6 solid
	};

	inline constexpr std::array<SpecialInfo, static_cast<std::size_t>(Special::kCount)> specials{ {
		{ "", "", "", 0 },
		{ "Weak Will", "A spectral helmet weakens their own damage and that of enemies who attack them.", "When several enemies attack them, or they are hurt under pressure.", 0xEF02 },
		{ "Volley", "Looses five arrows in quick succession at their target; every other archer and mage turns on it too.", "When a boss or an enemy well above their level joins the fight.",
			0xF140 },
		{ "War cry", "A roar that staggers every enemy close around them.", "When three or more enemies close in on them, or two when they are hurt.",
			0xF028 },
		{ "Azura's Moon", "A spectral Wispmother grants a chance to ignore hostile damage to you and nearby companions.",
			"When the party is under heavy attack or a wounded ally needs protection.", 0xF186 },
		{ "Disarm", "Staggers their opponent and knocks the weapon out of their hands, as the Disarm shout does, up to ten levels above their own.",
			"When they meet a boss, a two-handed weapon, or someone at or above their level.", 0xF256 },
	} };

	[[nodiscard]] constexpr const SpecialInfo& Info(Special a_special) { return specials[static_cast<std::size_t>(a_special)]; }

	// Seconds a special waits before it can be used again.
	[[nodiscard]] constexpr float SpecialCooldown(Special a_special)
	{
		switch (a_special) {
		case Special::kBodyguard:
			return 40.0f;
		case Special::kWarCry:
			return 30.0f;
		case Special::kDisarm:
			return 40.0f;
		case Special::kVolley:
			return 45.0f;
		case Special::kWard:
			return 90.0f;
		default:
			return 0.0f;
		}
	}

	[[nodiscard]] constexpr Special SpecialOf(Role a_role)
	{
		switch (a_role) {
		case Role::kTank:
			return Special::kBodyguard;
		case Role::kArcher:
			return Special::kVolley;
		case Role::kBruiser:
			return Special::kWarCry;
		case Role::kMage:
		case Role::kHealer:
			return Special::kWard;
		case Role::kFighter:
			return Special::kDisarm;
		default:
			return Special::kNone;
		}
	}

	struct RoleInfo
	{
		std::string_view name;
		std::string_view summary;
		Focus            focus;
		Position         position;
		std::uint32_t    glyph;  // Font Awesome 6 solid
	};

	inline constexpr std::array<RoleInfo, static_cast<std::size_t>(Role::kCount)> roles{ {
		{ "Fighter", "Melee. Works round to the side of whoever the fight puts in front of them.", Focus::kEngine, Position::kFree, 0xF6DE },
		{ "Tank", "Holds the line before you and turns on whoever attacks the party.", Focus::kAttackingParty, Position::kGuard, 0xF3ED },
		{ "Healer", "Stays back out of reach, watching whoever attacks the party.", Focus::kAttackingParty, Position::kHoldBack, 0xF4BE },
		{ "Bruiser", "Requires a two-handed melee weapon. Into the front line after the strongest enemy.", Focus::kStrongest, Position::kFrontLine, 0xF0E3 },
		{ "Archer", "Keeps range and picks off casters and other archers first.", Focus::kBackLine, Position::kHoldBack, 0xF05B },
		{ "Mage", "Keeps range and finishes off whoever is weakest.", Focus::kWeakest, Position::kHoldBack, 0xE2CA },
		{ "Skald", "A support bard requiring a lute and a 10-point unlock. Holds back, plays slowing notes between interruptible Ljodhs, and draws less enemy attention when other party targets are available.", Focus::kAttackingParty, Position::kHoldBack, 0xF001 },
	} };

	struct ChoiceInfo
	{
		std::string_view name;
		std::string_view summary;
		std::uint32_t    glyph;
	};

	inline constexpr std::array<ChoiceInfo, static_cast<std::size_t>(Focus::kCount)> focuses{ {
		{ "Their choice", "Whoever the combat AI would pick anyway.", 0xF074 },
		{ "Casters", "Enemies with offensive spells or a staff.", 0xE2CA },
		{ "Healers", "Enemies who can heal their allies.", 0xF21E },
		{ "Archers", "Enemies with a bow or crossbow out.", 0xF05B },
		{ "Strongest", "The most dangerous enemy: level, health, armor and weapon.", 0xF521 },
		{ "Weakest", "Whoever has the least health left. Finish them.", 0xF54C },
		{ "Attacking you", "Whoever is fighting you.", 0xF007 },
		{ "Attacking the party", "Whoever is fighting you or another companion.", 0xF0C0 },
		{ "Your target", "The enemy you are fighting or aiming at.", 0xF140 },
		{ "Marked enemy", "One enemy you mark on the battlefield.", 0xF024 },
		{ "Casters and archers", "Enemies who fight from behind their line: spells, a staff, healing or a bow.", 0xF0D0 },
	} };

	inline constexpr std::array<ChoiceInfo, static_cast<std::size_t>(Position::kCount)> positions{ {
		{ "Free", "Go wherever the fight takes them.", 0xF0B2 },  // retired; never offered
		{ "Free", "Go wherever the fight takes them.", 0xF0B2 },
		{ "Guard you", "Only fight enemies near you or attacking you.", 0xF505 },
		{ "Hold back", "Stay behind and fight only what comes close.", 0xF2EA },
		{ "Front line", "Meet the enemies furthest forward first.", 0xF124 },
		{ "With me", "Stay at your side, weapons out, and fight from there: melee takes whatever comes near you, ranged shoots the nearest enemy.", 0xF076 },
	} };

	[[nodiscard]] constexpr const RoleInfo& Info(Role a_role) { return roles[static_cast<std::size_t>(a_role)]; }
	[[nodiscard]] constexpr const ChoiceInfo& Info(Focus a_focus) { return focuses[static_cast<std::size_t>(a_focus)]; }
	[[nodiscard]] constexpr const ChoiceInfo& Info(Position a_position) { return positions[static_cast<std::size_t>(a_position)]; }

	// Picking a role resets focus and position to what the role is for; the player
	// can override either afterwards.
	[[nodiscard]] constexpr Orders ForRole(Role a_role, Orders a_keep = {})
	{
		Orders orders = a_keep;
		orders.role = a_role;
		orders.focus = Info(a_role).focus;
		orders.position = Info(a_role).position;
		return orders;
	}

	// A new role brings its suggested focus and position, but never over a choice
	// the player made: only values still at the old role's suggestion move.
	[[nodiscard]] constexpr Orders Retask(Orders a_orders, Role a_role)
	{
		const auto& before = Info(a_orders.role);
		const auto& after = Info(a_role);
		if (a_orders.focus == before.focus) {
			a_orders.focus = after.focus;
		}
		if (a_orders.position == before.position) {
			a_orders.position = after.position;
		}
		a_orders.role = a_role;
		return a_orders;
	}

	// Focus and position both where the role suggests.
	[[nodiscard]] constexpr bool AsSuggested(const Orders& a_orders)
	{
		return a_orders.focus == Info(a_orders.role).focus && a_orders.position == Info(a_orders.role).position;
	}

	// Orders for the whole party. Each one replaces every companion's own orders, so they
	// are for the moments that call for it; "As you were" puts everyone back to their role.
	enum class Shout : std::uint8_t
	{
		kNone,  // as you were
		kAttack,
		kToMe,
		kHoldBack,
		kCount
	};

	struct ShoutInfo
	{
		std::string_view name;
		std::string_view summary;
	};

	inline constexpr std::array<ShoutInfo, static_cast<std::size_t>(Shout::kCount)> shouts{ {
		{ "As you were", "Everyone back to what their role suggests." },
		{ "Attack!", "Everyone goes all out for your target." },
		{ "To me!", "Everyone comes back to your side and fights there." },
		{ "Hold back", "Everyone stays back and meets only what comes for the party." },
	} };

	[[nodiscard]] constexpr const ShoutInfo& Info(Shout a_shout) { return shouts[static_cast<std::size_t>(a_shout)]; }

	[[nodiscard]] constexpr Orders Shouted(Orders a_orders, Shout a_shout)
	{
		switch (a_shout) {
		case Shout::kAttack:
			a_orders.position = Position::kFree;
			a_orders.focus = Focus::kPlayerTarget;
			break;
		case Shout::kToMe:
			a_orders.position = Position::kWithMe;
			a_orders.focus = a_orders.role == Role::kHealer ? Focus::kAttackingParty : Focus::kAttackingPlayer;
			break;
		case Shout::kHoldBack:
			a_orders.position = Position::kHoldBack;
			a_orders.focus = Focus::kAttackingParty;
			break;
		default:
			a_orders.focus = Info(a_orders.role).focus;
			a_orders.position = Info(a_orders.role).position;
			break;
		}
		return a_orders;
	}

	// What a companion can actually fight with: equipped or carried weapons, and the
	// spells they know. A role needs the means to play it.
	struct Arsenal
	{
		bool  melee{ false };     // a sword, axe, mace, dagger or two-hander
		bool  ranged{ false };    // a bow or crossbow
		bool  casts{ false };     // offensive spells or a staff
		bool  heals{ false };     // a spell that heals someone else
		bool  ammo{ true };       // arrows or bolts for the bow or crossbow they carry
		bool  staff{ false };     // a staff, which casts without magicka
		float castCost{ 0.0f };   // magicka for the cheapest spell they would fight or heal with; 0 for none

        bool twoHanded{ false }; // carried greatsword, battleaxe or warhammer

		friend bool operator==(const Arsenal&, const Arsenal&) = default;
	};

	enum class Lack : std::uint8_t
	{
		kNothing,
		kWeapon,
        kTwoHanded,
		kBow,
		kSpells,
        kLute,
        kSkaldLocked
	};

    [[nodiscard]] constexpr Lack SkaldLack(bool hasLute, bool unlocked) {
        return !hasLute ? Lack::kLute : !unlocked ? Lack::kSkaldLocked : Lack::kNothing;
    }

	// What stops a companion taking a role, if anything. Someone with no weapon and no
	// magic at all can still brawl as a Fighter. A healer's heal is their own, not a spell,
	// so anyone can be one.
	// With role weapons off, a Bruiser needs only a melee weapon.
	[[nodiscard]] constexpr Lack LackFor(Role a_role, const Arsenal& a_arsenal, bool a_roleWeapons = true)
	{
		const bool empty = !a_arsenal.melee && !a_arsenal.ranged && !a_arsenal.casts && !a_arsenal.heals;
		switch (a_role) {
		case Role::kFighter:
			return a_arsenal.melee || empty ? Lack::kNothing : Lack::kWeapon;
		case Role::kTank:
			return a_arsenal.melee ? Lack::kNothing : Lack::kWeapon;
        case Role::kBruiser:
            if (!a_roleWeapons) return a_arsenal.melee ? Lack::kNothing : Lack::kWeapon;
            return a_arsenal.twoHanded ? Lack::kNothing : Lack::kTwoHanded;
		case Role::kArcher:
			return a_arsenal.ranged ? Lack::kNothing : Lack::kBow;
		case Role::kMage:
			return a_arsenal.casts ? Lack::kNothing : Lack::kSpells;
		default:
			return Lack::kNothing;
		}
	}

	[[nodiscard]] constexpr std::string_view Why(Lack a_lack)
	{
		switch (a_lack) {
        case Lack::kLute:
            return "Requires a lute in this companion inventory.";
        case Lack::kSkaldLocked:
            return "Unlock the Skald tree with 10 earned skill points first.";
		case Lack::kTwoHanded:
            return "Requires a two-handed melee weapon in inventory.";
		case Lack::kWeapon:
			return "Carries no melee weapon.";
		case Lack::kBow:
			return "Carries no bow or crossbow.";
		case Lack::kSpells:
			return "Knows no attack spell and carries no staff.";
		default:
			return "";
		}
	}

	// The preferred role if it can be played, otherwise the closest one that can.
	[[nodiscard]] constexpr Role Playable(Role a_preferred, const Arsenal& a_arsenal, bool a_roleWeapons = true)
	{
		if (LackFor(a_preferred, a_arsenal, a_roleWeapons) == Lack::kNothing) {
			return a_preferred;
		}
		if (a_arsenal.melee) {
			return Role::kFighter;
		}
		if (a_arsenal.ranged) {
			return Role::kArcher;
		}
		if (a_arsenal.casts) {
			return Role::kMage;
		}
		if (a_arsenal.heals) {
			return Role::kHealer;
		}
		return Role::kFighter;
	}

	// Walk With Me formation modes, as WayfarerAPI numbers them.
	enum class Formation : std::int32_t
	{
		kUnknown = -1,
		kNatural = 0,
		kLead = 1,
		kCompanion = 2,
		kRear = 3,
		kRelax = 4,
		kVanilla = 5,
		kCustom = 6
	};

	// The positions on offer, in the order the plan shows them.
	inline constexpr int kFirstPosition = static_cast<int>(Position::kFree);
	inline constexpr int kPositionChoices = static_cast<int>(Position::kCount) - kFirstPosition;

	// A position as orders keep it: anything unknown or retired fights Free.
	[[nodiscard]] constexpr Position Live(Position a_position)
	{
		return a_position == Position::kRetired || a_position >= Position::kCount ? Position::kFree : a_position;
	}
}
