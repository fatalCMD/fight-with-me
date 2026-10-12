#pragma once

#include "FWM/Hotkey.h"
#include "FWM/Rules.h"

namespace FWM::Settings
{
	struct Data
	{
		Rules::Tuning tuning;

		// General
		bool  enabled{ true };
		float styleWeight{ 0.75f };      // how far a role pulls a follower's own style
		float planInterval{ 0.3f };      // seconds between target decisions
		bool  guessRoles{ true };        // first sight: pick a role from loadout (or NFF)
		bool  guardLeash{ true };        // keep guards (and melee holding back) inside an area around the player
		float holdBackLeash{ 1000.0f };  // that area for Hold back
		bool  leaveEmptyFights{ true };  // end a companion's combat once no enemy is left
		float leaveAfter{ 2.5f };
		bool        markGlow{ true };  // in the plan, an enemy someone is sent after is outlined, through walls
		std::string markShader{ "Skyrim.esm|0x0FE68D" };  // GhostWolfSpiritFXShader: edge only; recoloured

		// Battle plan
		Hotkey        planKey{ 71 };        // Numpad 7
		std::uint32_t planGamepadKey{ 2 };  // XInput button pressed with LB held, 0 = none
		// Party orders, laid out like a D-pad on the numpad: Attack! 8, To me! 6,
		// Hold back 4, As you were 2.
		std::array<Hotkey, 4> orderKeys{ { { 72 }, { 77 }, { 75 }, { 80 } } };
		float         timeScale{ 0.1f };
		bool          stopTime{ false };
		float         easeSeconds{ 0.25f };
		bool          timeEffect{ false };
		bool          hideHud{ true };
		bool          cameraGlide{ true };   // ease the camera over to the companion being commanded
		float         cameraDistance{ 260.0f };
		float         cameraHeight{ 70.0f };
		float         portraitDistance{ 95.0f };  // face to face, when no fight is on
		float         uiScale{ 1.0f };
		float         orbitSpeed{ 1.0f };          // right stick and right-drag turning
		Hotkey        overheadKey{ 76 };           // Numpad 5
		std::uint32_t overheadGamepadKey{ 0x80 };  // right stick click
		float         overheadPitch{ 70.0f };      // degrees above the horizon, overhead
		bool          cutaway{ true };             // indoors, overhead: clip everything above the fighters
		float         cutawayMargin{ 60.0f };      // that far above the tallest head

		// Taunt
		bool        taunt{ true };
		bool        tauntOpener{ true };
		bool        shieldWall{ true };  // tanks raise the shield against archers and casters shooting at them
		float       tauntChance{ 0.35f };       // once ready, the chance of taunting at each check
		float       tauntCheckSeconds{ 2.0f };
		float       glowSeconds{ 1.2f };
		std::string glowShader{ "Skyrim.esm|0x0FE68D" };
		std::string tauntSound{ "Skyrim.esm|0x0FF86A" };
		std::string idleShield{ "Skyrim.esm|0x0B8C74" };
		std::string idleWeapon{ "Skyrim.esm|0x0B8C73" };
		std::string idleCry{ "Skyrim.esm|0x0F7C8C" };

		// Heal
		bool        heal{ true };
		bool        healFallback{ false };
		std::string fallbackSpell{ "Skyrim.esm|0x04D3F2" };
		float       concentrationSeconds{ 2.5f };

		// Specials: bodyguard, volley, war cry, ward, disarm
		bool specials{ true };

		// Potions
		bool  drinkPotions{ true };
		float drinkBelow{ 0.35f };   // health they drink a healing potion at
		bool  brewPotions{ true };   // at rest, they brew what they are short of
		bool  brewAnimation{ true }; // and mix it in their hands, if no station is to hand

		// Time effect
		std::string imodIntro{ "Skyrim.esm|0x0486F7" };
		std::string imodHold{ "Skyrim.esm|0x0486F4" };

		// Interface
		bool notices{ true };
		bool sounds{ true };  // the game's own menu sounds in the battle plan
		bool tooltips{ true };  // describe controls on hover and pad focus

		// Debug
		bool logDecisions{ false };
	};

	// Read at load from Data/SKSE/Plugins: FightWithMe.ini (shipped, replaced by every
	// update), then FightWithMe_user.ini (what the player changed in game, never shipped).
	// Every key has a default, so a missing file or key is not an error.
	void Load();

	// The live settings. Safe from any thread; a reference stays valid for the session.
	[[nodiscard]] const Data& Get() noexcept;

	// What FightWithMe.ini says, before the player's own changes: what "Reset" goes back to.
	[[nodiscard]] Data Defaults();

	// Replaces the live settings and saves whatever now differs from FightWithMe.ini to
	// FightWithMe_user.ini. Main thread.
	bool Apply(const Data& a_data);

	// From any thread (the menu): Apply on the main thread soon. Returns the revision.
	std::uint64_t Submit(const Data& a_data);

	[[nodiscard]] std::uint64_t AppliedRevision() noexcept;
	[[nodiscard]] std::uint64_t RequestedRevision() noexcept;
	[[nodiscard]] bool          LastSaveFailed() noexcept;
}
