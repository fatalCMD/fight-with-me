#pragma once

#include "FWM/Orders.h"
#include "FWM/View.h"

// The planner. Runs on the main thread from the player update: keeps the roster
// and the enemy list current, picks each follower's target, runs the abilities
// and hands the result to the target selector hook. Also owns the battle plan's
// open state and the queue of orders coming back from the menu.

namespace FWM::Battle
{
	void Update(float a_dt);

	// New game or a load: forget the fight.
	void Reset();

	void InstallEvents();

	// --- Battle plan ---------------------------------------------------------

	// Safe from any thread; the work happens on the main thread.
	void RequestOpen();
	void RequestClose();
	[[nodiscard]] bool PlanOpen() noexcept;

	// The plan's window has gone (it fades out after RequestClose). From the render thread;
	// a_menuStillOpen: another Menu Framework window that takes input is still up.
	void PlanWindowClosed(bool a_menuStillOpen);

	enum class Preset : std::uint8_t
	{
		kRoles,      // everyone back to what their role is for
		kProtectMe,
		kAllOut,
		kHoldBack
	};

	struct Command
	{
		enum class Type : std::uint8_t
		{
			kSetOrders,
			kMark,
			kTauntNow,
			kHealNow,
			kRejuvenateNow,  // a healer quickens everyone's magicka
			kSpecialNow,  // the role's special: bodyguard, volley, war cry, ward, disarm
			kPreset,
			kShout,    // a party order for this fight
			kSetSpell,  // a mage's or healer's hand: a spell, or 0 for their own choice
			kAutoBattle,  // the party starts fights with enemies it sees (on), or waits for them (off)
			kAbilityNow,  // with the Paths add-on: one of the two abilities carried, now
			kLearn,       // a rank of a node on a path
			kForget,      // a rank given back
			kResetPath,   // every point on a path given back
			kEquip,       // an ability carried in a slot
			kUnlimited,   // unlimited points for one companion (on), or back to what they had (off)
			kInvest,      // a leftover point straight into a mastered path's bonus
			kDivest       // one back out of it
		};

		Type        type{ Type::kSetOrders };
		RE::FormID  member{ 0 };
		Orders      orders;
		RE::FormID  enemy{ 0 };
		Preset      preset{ Preset::kRoles };
		Shout       shout{ Shout::kNone };
		std::size_t hand{ kLeftHand };
		RE::FormID  spell{ 0 };
		bool        on{ false };
		// Paths: which path, which node or slot, which ability.
		Role           path{ Role::kFighter };
		std::size_t    node{ 0 };
		std::size_t    slot{ 0 };
		Paths::Ability ability{ Paths::Ability::kNone };
	};

	// From the render thread. Applied on the main thread before the next view.
	void Queue(const Command& a_command);

	// Ask for a fresh view; posts a main-thread task unless one is pending. Works
	// while the game is paused.
	void RefreshView();

	[[nodiscard]] std::shared_ptr<const View::Snapshot> ReadView();
}
