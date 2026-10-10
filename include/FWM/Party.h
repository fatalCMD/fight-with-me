#pragma once

#include "FWM/Orders.h"

// The player's companions: everyone loaded who follows the player, their saved
// orders, and what each one is doing in the current fight. Main thread only.

namespace FWM::Party
{
	struct Member
	{
		RE::ActorHandle handle;
		RE::FormID      id{ 0 };
		std::string     name;
		bool            summon{ false };  // conjured or raised: healed and protected, not commanded
		bool            managedByWalkWithMe{ false };
		Arsenal         arsenal;            // refreshed every few seconds
		bool            arsenalKnown{ false };
		float           arsenalIn{ 0.0f };

		// This fight
		RE::ActorHandle desired;  // the planner's pick, or none
		RE::ActorHandle marked;   // an enemy the player marked for them
		float           tauntReady{ 0.0f };  // seconds until the next taunt is allowed
		float           tauntCooldown{ 0.0f };  // length of the last cooldown, for the menu
		float           healReady{ 0.0f };
		float           inCombatFor{ 0.0f };
		bool            opened{ false };     // the opening taunt is spent
		bool            tauntRequested{ false };
		bool            healRequested{ false };
		int             held{ 0 };           // enemies this tank currently holds
		bool            down{ false };       // bleeding out at the last decision

		// A taunt waiting for its animation to start.
		bool  tauntPending{ false };
		float tauntPendingFor{ 0.0f };

		// A heal being cast by hand: the spell in the left hand, aimed at the patient.
		RE::ActorHandle castPatient;
		RE::SpellItem*  castSpell{ nullptr };
		float           castElapsed{ 0.0f };
		float           castHold{ 0.0f };       // how long the cast is held before release
		float           castHealthAt{ 0.0f };   // the patient's health when it began
		float           castEffectiveness{ 1.0f };
		float           castCost{ 0.0f };
		int             castPhase{ 0 };          // 0 equipping, 1 charging, 2 released
		float           castPhaseFor{ 0.0f };
		float           castExpected{ 0.0f };    // the heal as a fraction of the patient's health
		int             castFurthest{ 0 };       // the furthest caster state reached, for the log
		int             castFailures{ 0 };       // hand casts in a row that did not land, this fight
		float           tauntRollIn{ 0.0f };  // seconds until a ready tank rolls for a taunt again

		const RE::TESForm* rightHand{ nullptr };  // what was in the right hand at the last decision
		bool               armed{ false };        // the start-of-fight weapon check is done
		bool               warnedAmmo{ false };   // told the player this fight that they have no arrows

		// Drawing fire: the shield up and turned to the nearest archer or caster shooting at them.
		bool            shielding{ false };
		float           shieldFor{ 0.0f };
		float           shieldRest{ 0.0f };  // a pause after lowering it, before raising it again
		int             shieldMisses{ 0 };   // checks in a row where the game was not showing the block
		RE::ActorHandle shieldFacing;

		// The target they are closing on, for noticing one they never reach.
		RE::ActorHandle chasing;
		float           chasingFrom{ 0.0f };
		float           chasingFor{ 0.0f };
		bool            chasingLogged{ false };

		// A healer on their way to a downed companion, to heal them up close.
		RE::ActorHandle reviving;
		float           revivingFor{ 0.0f };
		bool            leashOnPatient{ false };  // the leash is centred on them, not the player
	};

	// a_dt ages the arsenal timers.
	void Refresh(float a_dt = 0.0f);

	// Forget the roster and the fight; orders survive.
	void ResetRuntime();

	// Forget everything, orders included (new game, revert before load).
	void Clear();

	[[nodiscard]] std::vector<Member>& Members();
	[[nodiscard]] Member*              Find(RE::FormID a_id);

	// Orders for a follower, creating them on first sight from their loadout (or
	// their NFF role) when guessing is on.
	[[nodiscard]] Orders& OrdersFor(RE::Actor* a_actor);
	[[nodiscard]] const Orders* FindOrders(RE::FormID a_id);
	void                        SetOrders(RE::FormID a_id, const Orders& a_orders);

	[[nodiscard]] std::vector<std::pair<std::uint32_t, Orders>> SavedOrders();
	void LoadOrders(const std::vector<std::pair<std::uint32_t, Orders>>& a_orders);

	// The player plus everyone above, for healing and "attacking the party".
	[[nodiscard]] bool IsPartyActor(RE::Actor* a_actor);

	// Whether a role can be played by this companion right now.
	[[nodiscard]] Lack LackFor(RE::FormID a_id, Role a_role);
}
