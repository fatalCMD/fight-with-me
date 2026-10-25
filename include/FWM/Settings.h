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

		// Out of a fight: an enemy in view brings weapons out, and with auto battle the
		// companion who sees it starts the fight instead of waiting to be attacked.
		bool  drawOnSight{ true };
		bool  autoBattle{ false };
		float sightRange{ 2500.0f };

		// Companions level up by fighting beside you, XP from every enemy the party brings down.
		bool  leveling{ true };
		float xpPerEnemyLevel{ 3.0f };  // an even enemy: this × the companion's level
		float xpBossMult{ 3.0f };
		float xpClimb{ 0.05f };  // each level costs this share more than the player's, per level they have
		float xpCrowd{ 0.75f };  // each enemy after a fight's best counts this share of the one before
		bool  levelCap{ true };  // nobody passes the player's level

        // Party damage balance (Paths only): percentages, no permanent actor-value changes.
        bool partyBalance{ true };
        float partyExtraPenalty{ 5.0f };
        float partyAdvantagePenalty{ 10.0f };
        float partyMinimumDamage{ 50.0f };

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
		bool        brace{ true };  // a taunt braces the tank with armor while it holds
		float       glowSeconds{ 1.2f };  // the gold flash on a tank as they taunt
		std::string tauntSound{ "Skyrim.esm|0x0FF86A" };
		std::string idleShield{ "Skyrim.esm|0x0B8C74" };
		std::string idleWeapon{ "Skyrim.esm|0x0B8C73" };
		std::string idleCry{ "Skyrim.esm|0x0F7C8C" };

		// Heal
		bool heal{ true };              // a healer's heal for the whole party
		bool healersAnySpell{ false };  // besides heals, any spell in their hands; off: only weaker ones, by level
		bool rejuvenate{ true };        // a healer quickens everyone's magicka

		// Specials: bodyguard, volley, war cry, ward, disarm
		bool specials{ true };

		// What abilities look like in the world: the glows on a tank and the enemies a taunt holds,
		// the gesture and sparks of a healer's or mage's ability, a volley's target, and so on.
		bool abilityEffects{ true };

		// Time effect
		std::string imodIntro{ "Skyrim.esm|0x0486F7" };
		std::string imodHold{ "Skyrim.esm|0x0486F4" };

		// Interface
		int uiTheme{ 1 };  // 0 Vanilla, 1 Oxblood (default), 2 Nordic; shared with Paths
		bool notices{ true };
		bool sounds{ true };  // the game's own menu sounds in the battle plan
		bool tooltips{ true };  // describe controls on hover and pad focus
		bool pathsGuideSeen{ false };  // the paths guide has opened once by itself
		std::string language;  // a translation to read, by the game's name for it (FRENCH); empty: the game's own

		// Debug
		bool logDecisions{ false };
	};

	// Read at load from Data/SKSE/Plugins: FightWithMe.ini (shipped, replaced by every
	// update), then FightWithMe_user.ini (what the player changed in game, never shipped).
	// Every key has a default, so a missing file or key is not an error.
	void Load();

	// The live settings. Safe from any thread; a reference stays valid for the session.
	[[nodiscard]] const Data& Get() noexcept;
	// Live theme, including Fight With Me's current choice when both plugins are loaded.
	[[nodiscard]] int Theme() noexcept;

	// The player's own: what the settings pages edit and what is saved, so copy this, not Get(),
	// to change one and Apply or Submit. The same as Get() except while Paths - Follower Ability
	// Skilltree is installed: the role settings (the Tanks and Healers pages) in effect are then
	// FightWithMe.ini's, and the player's own wait in FightWithMe_user.ini for when it is removed.
	[[nodiscard]] const Data& Chosen() noexcept;
	void                      LockRoles(bool a_locked);  // main thread
	[[nodiscard]] bool        RolesLocked() noexcept;

	// What FightWithMe.ini says, before the player's own changes: what "Reset" goes back to.
	[[nodiscard]] Data Defaults();

	// Replaces the live settings and saves whatever now differs from FightWithMe.ini to
	// FightWithMe_user.ini. Main thread.
	bool Apply(const Data& a_data);

	// From any thread (the menu): Apply on the main thread soon. Returns the revision.
	std::uint64_t Submit(const Data& a_data, bool shareTheme = false);
    // Main thread: change only this plugin's theme, preserving all other settings.
    bool ApplyTheme(int theme);

	[[nodiscard]] std::uint64_t AppliedRevision() noexcept;
	[[nodiscard]] std::uint64_t RequestedRevision() noexcept;
	[[nodiscard]] bool          LastSaveFailed() noexcept;
}
