#pragma once

#include "FWM/Orders.h"
#include "FWM/Rules.h"

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

	struct Member
	{
		RE::FormID  id{ 0 };
		std::string name;
		int         level{ 1 };
		bool        summon{ false };
		bool        walkWithMe{ false };
		Arsenal     arsenal;
		bool        arsenalKnown{ false };
		Orders      orders;
		Position    resolved{ Position::kFree };
		float       health{ 1.0f };
		float       magicka{ 1.0f };
		float       stamina{ 1.0f };
		bool        inCombat{ false };
		bool        down{ false };  // bleeding out
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

		bool        canHeal{ false };
		std::string healSpell;
		bool        healBorrowed{ false };
		float       healAmount{ 0.0f };
		float       healCost{ 0.0f };
		float       healReady{ 0.0f };

		Special     special{ Special::kNone };  // the role's special, and how soon it is ready
		float       specialReady{ 0.0f };
		float       specialCooldown{ 0.0f };
		bool        guarding{ false };          // a bodyguard at your side

		int  potionHealing{ 0 }, potionMagicka{ 0 }, potionStamina{ 0 }, potionOther{ 0 };
		bool brewing{ false };

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
		bool                lendHeal{ false };  // companions with no heal may still be Healers
		Formation           formation{ Formation::kUnknown };
		int                 playerLevel{ 1 };
		std::vector<Member> party;  // companions first, then summons
		std::vector<Enemy>  enemies;
		Rules::Tuning       tuning;
	};
}
