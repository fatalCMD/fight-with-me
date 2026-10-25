#pragma once

#include "FWM/LevelRules.h"

// Companions level up by fighting beside you. When a fight is over, its enemies are worth XP to
// each companion who was in it; each level costs more than the last, nobody passes the player's
// level, and each one brings health, magicka or stamina and skill by the role they fight as.
// Their record's level is taken over (no longer following the player's, no maximum) and given
// back to their plugin's when leveling is off or another game is loaded. Main thread.

namespace FWM::Leveling
{
	struct Progress
	{
		int   level{ 1 };
		float xp{ 0.0f };
		float next{ 1.0f };          // the XP the next level costs
		float sinceLevelUp{ 99.0f };  // seconds, for the menu's flourish
		bool  capped{ false };        // at or above the player's level: no XP until they level up
	};

	// With the roster: takes up a companion first seen, and keeps their record's level where
	// Fight With Me has it (another mod's "level up" changes it back).
	void Refresh();

	// Every frame: companions in a fight are noted as fighting it, enemies who fall are counted,
	// and a fight that is over is paid out. a_fighting: a fight is on.
	void Update(bool a_fighting);

	// A fight has ended. It is paid out a moment later, once every death in it has been reported,
	// unless it picks up again first, when it all counts as one fight.
	void FightOver();

	[[nodiscard]] std::optional<Progress> Find(RE::FormID a_id);

	// Over a companion who has just levelled up: where on screen (0 to 1 from the top left), how
	// long ago, their new level, and the perk points they have to spend (-1 without the Paths
	// add-on, or with unlimited points). Made on the main thread, read by the HUD.
	inline constexpr float kBadgeSeconds = 4.2f;  // the level, then the points
	struct Badge
	{
		float x{ 0.0f };
		float y{ 0.0f };
		float age{ 0.0f };
		int   level{ 1 };
		int   points{ -1 };
	};
	[[nodiscard]] std::vector<Badge> Badges();

	// Enemies falling.
	void InstallEvents();

	// For the co-save.
	constexpr std::uint32_t kSaveVersion = 1;
	[[nodiscard]] std::vector<std::uint8_t> Encode();
	void Decode(const std::vector<std::uint8_t>& a_bytes, std::uint32_t a_version, const std::function<RE::FormID(RE::FormID)>& a_resolve);

	// Before a load or a new game: every record changed goes back to its plugin's level.
	void Reset();
}
