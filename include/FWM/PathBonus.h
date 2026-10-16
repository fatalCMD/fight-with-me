#pragma once

// What every point spent on a role's path is worth (Rules::PathBonus): faster blows for a
// fighter, less damage taken for a tank, more healing from a healer's abilities, spells and potions,
// harder blows for a bruiser, arrows that are not used up for an archer, cheaper spells for a
// mage. Only the path of the role a companion has now counts, and only with the Paths add-on.
// Main thread, except the hooks, which read a table published here.

namespace FWM::PathBonus
{
	// The source its lifts carry (Abilities::Keep), apart from every Paths::Perk.
	inline constexpr std::uint16_t kSource = 0x8000;

	// Every roster refresh, in a fight or out of it: who gains what.
	void Publish();

	// The planner's tick in a fight: attack speed and damage kept up, arrows given back.
	void Fight();

	// The fight is over: what the archers kept, for the log.
	void StandDown();

	// Switched off, a load or a new game: nobody gains anything until the next Publish.
	void Clear();

	// A healer's own healing: what their abilities restore is multiplied by this. 1 for anyone else.
	[[nodiscard]] float Healing(RE::FormID a_id);

	// After the data has loaded.
	void Install();

	struct Stats
	{
		std::uint64_t heals{ 0 };  // healing effects from healers made stronger
		std::uint64_t costs{ 0 };  // spell costs worked out cheaper for mages
		std::uint64_t blows{ 0 };  // weapon hits on tanks made lighter
		std::uint64_t harms{ 0 };  // harmful spells on tanks made weaker
	};
	[[nodiscard]] Stats TakeStats();
}
