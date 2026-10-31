#pragma once

#include "FWM/Orders.h"
#include "FWM/LoadoutPolicy.h"
#include "FWM/Paths.h"
#include "FWM/Rules.h"
#include "FWM/TraitRules.h"

// What the battle plan draws: a copy of the fight taken on the main thread, so the
// render thread never touches a game object.

namespace FWM::View
{
	struct Point
	{
		float x{ 0.0f };
		float y{ 0.0f };
		bool  on{ false };  // in front of the camera and on screen
	};

	// A spell a mage or healer may hold: one of theirs, or one of yours they would be taught.
	struct Spell
	{
		RE::FormID        id{ 0 };
		std::string       name;
		Rules::School     school{ Rules::School::kOther };
		Rules::SpellTier  tier{ Rules::SpellTier::kNovice };
		float             cost{ 0.0f };
		bool              known{ true };
        bool weapon{false};
        bool allowed{true};  // visible, but cannot be added to this role's pool
        bool shield{false};
        int count{0};
		bool              healing{ false };
		bool              left{ true };   // fits the left hand
		bool              right{ true };  // fits the right hand
		bool              twoHanded{ false };  // takes both hands at once
	};

	// Something one of the party's abilities has put on a companion for a while, good or bad: a
	// taunt's armor, Rejuvenate, a bodyguard run, running out of magicka. A small icon on their
	// crest, its ring running down.
	struct Status
	{
		enum class Tint : std::uint8_t
		{
			kBuff,
			kDebuff,
			kArmor,
			kMagicka
		};

		std::string   name;
		std::string   detail;  // what it does
		std::uint32_t glyph{ 0 };
		Tint          tint{ Tint::kBuff };
		float         left{ -1.0f };      // seconds; below zero it has no clock
		float         length{ 0.0f };     // all of it, for the ring
		float         progress{ -1.0f };  // without a clock: how far along, 0..1, for the ring
	};

	struct Member
	{
		RE::FormID  id{ 0 };
		std::string name;
		int         level{ 1 };
		bool        summon{ false };
        bool        creature{ false };
        [[nodiscard]] bool CanTakeRole() const { return RoleAllowed(true, creature, summon); }
		bool        walkWithMe{ false };
		Arsenal     arsenal;
		bool        arsenalKnown{ false };
        bool        hasLute{ false };
        Lack        skaldLack{ Lack::kSkaldLocked };
		Orders      orders;
		Position    resolved{ Position::kFree };
		float       health{ 1.0f };
		float       magicka{ 1.0f };
		float       stamina{ 1.0f };
		bool        inCombat{ false };
		bool        down{ false };  // bleeding out
		bool        drained{ false };  // out of magicka: fighting with a weapon or a bow until it comes back
		RE::FormID  target{ 0 };
		std::string targetName;
		RE::FormID  marked{ 0 };

		float tauntReady{ 0.0f };
		float tauntCooldown{ 0.0f };
		int   held{ 0 };
		float tauntRadius{ 0.0f };
		int   tauntCertain{ 0 };
		int   tauntNever{ 0 };
		float tauntHold{ 0.0f };
		float tauntStamina{ 0.0f };
		float tauntArmor{ 0.0f };  // braced with after each taunt
		float tauntArmorSeconds{ 0.0f };

		// Mages and healers: the spell chosen for each hand (id 0: their own choice), and
		// what they may choose from. The list is filled only out of a fight.
		std::array<Spell, 2> hands{};
		std::vector<Spell>   spellOptions;
        std::vector<Spell> weaponOptions;
        LoadoutPolicy::Pools pools;

		float       healAmount{ 0.0f };  // a healer's circle: health for someone in it all along
		float       healReady{ 0.0f };
		float       rejuvenateReady{ 0.0f };
		float       rejuvenateCooldown{ 0.0f };
		float       rejuvenateSeconds{ 0.0f };  // how long theirs would last
		float       rejuvenated{ 0.0f };        // seconds left on a Rejuvenate on them, 0 for none
		std::vector<Status> statuses;  // buffs and debuffs from the party's abilities

		Special     special{ Special::kNone };  // the role's special, and how soon it is ready
		float       specialReady{ 0.0f };
		float       specialCooldown{ 0.0f };
		bool        guarding{ false };          // a bodyguard at your side

		// What their own mod gave them on top of the combat AI, and how rare that makes them.
		std::vector<Traits::Trait> traits;
		Traits::Rarity             rarity{ Traits::Rarity::kCommon };

		// Levels earned by fighting: XP toward the next, and how long since the last one came.
		bool  leveling{ false };
		float xp{ 0.0f };
		float xpNext{ 1.0f };
		float levelUpAge{ 99.0f };
		bool  levelCapped{ false };  // at or above the player's level: no XP until they level up

		// With the Paths add-on: every path's ranks and slots, the points, and the two
		// abilities carried, each with its own clock.
		struct Carried
		{
			Paths::Ability ability{ Paths::Ability::kNone };
			float          ready{ 0.0f };
			float          cooldown{ 0.0f };
		};
		bool                                 paths{ false };
		Paths::Record                        record;
		int                                  pointsEarned{ 0 };
		int                                  pointsSpent{ 0 };
		int                                  pointsAbove{ 10 };
		bool                                 respec{ false };
		bool                                 unlimited{ false };         // unlimited points are on: Debug Mode
		bool                                 unlimitedAllowed{ false };  // the button is offered
		std::array<Carried, Paths::kSlots>   carried{};

		bool        asked{ false };      // a taunt or heal the player asked for, not yet answered
		std::string answer;              // what came of the last one
		bool        answerDone{ false };  // done, rather than refused
		float       answerAge{ 99.0f };  // seconds since

		Point              head;
		std::vector<Point> ring;  // the taunt radius on the ground, for tanks
		Point              foot;  // overhead: where they stand, and a ring around it
		std::vector<Point> footRing;

		// The character sheet, for the party view.
		std::string                               race;
		std::string                               className;
		float                                     healthNow{ 0.0f }, healthMax{ 0.0f };
		float                                     magickaNow{ 0.0f }, magickaMax{ 0.0f };
		float                                     staminaNow{ 0.0f }, staminaMax{ 0.0f };
		float                                     armor{ 0.0f };
		float                                     damage{ 0.0f };  // the better hand's weapon, base
		std::string                               rightHand, leftHand;
		bool                                      bow{ false };   // carries a bow or crossbow
		std::string                               ammoName;       // what they would shoot with it; empty if nothing
		int                                       ammoCount{ 0 };
		std::vector<std::pair<std::string, int>>  skills;  // combat skills, best first
		std::array<float, 5>                      resist{};  // fire, frost, shock, poison, magic, in percent
	};

	struct Enemy
	{
		RE::FormID  id{ 0 };
		std::string name;
		int         level{ 1 };
		float       health{ 1.0f };
		float       magicka{ 1.0f };
		float       stamina{ 1.0f };
		float       healthNow{ 0.0f }, healthMax{ 0.0f };
		float       magickaNow{ 0.0f }, magickaMax{ 0.0f };
		float       staminaNow{ 0.0f }, staminaMax{ 0.0f };
		bool        caster{ false };
		bool        healer{ false };
		bool        archer{ false };
		bool        boss{ false };
		RE::FormID  target{ 0 };   // whom they are fighting
		RE::FormID  heldBy{ 0 };   // the tank holding them
		bool        targetsPlayer{ false };
		Point       head;
		Point       foot;
		std::vector<Point> footRing;
	};

	struct Snapshot
	{
		Shout               shout{ Shout::kNone };  // the party order in force
		std::uint64_t       serial{ 0 };
		bool                inGame{ false };
		bool                inCombat{ false };
		bool                driving{ false };   // the camera follows the plan (the glide is on)
		bool                overhead{ false };  // the fight is seen from above
		Point               playerHead;
		Point               playerFoot;
		std::vector<Point>  playerRing;
		bool                walkWithMe{ false };
		bool                autoBattle{ false };    // companions start fights with enemies they see
		bool                drawOnSight{ true };    // and bring weapons out at the sight of one
		bool                paths{ false };         // Paths - Follower Ability Skilltree is loaded and agrees on the trees
		bool                pathsAlone{ false };    // Paths on its own, without Fight With Me: its panel, not the battle plan
		RE::FormID          aimed{ 0 };             // the companion under the crosshair, if any
		int                 playerLevel{ 1 };
		std::vector<Member> party;  // companions first, then summons
		std::vector<Enemy>  enemies;
		Rules::Tuning       tuning;
	};
}
