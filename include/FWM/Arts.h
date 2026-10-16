#pragma once

#include "FWM/Cooldowns.h"
#include "FWM/PathsLink.h"
#include "FWM/StyleLayer.h"
#include "FWM/View.h"

// The abilities and traits the Paths add-on brings, beyond the eight every role had before:
//   Tank     Challenge, Stand Fast; Iron Hide, Shield Master, Last Stand
//   Healer   Renewal; Triage (in Heal), Serenity, Guardian Spirit
//   Bruiser  Berserk, Sunder, Earthshaker; Heavy Hands, Momentum, Executioner
//   Fighter  Second Wind, Riposte, Cleave; Flanker, Endurance, Opportunist
//   Archer   Deadeye, Pinning Shot, Arrow Rain; Fletcher, Skirmisher, Sharpshooter
//   Mage     Arcane Surge, Elemental Burst, Magnus; Arcane Flow, Spell Shield, Unravel
// Only with a path: without the add-on nobody carries any of them. Main thread.

namespace FWM::Arts
{
	// The planner's tick, in a fight: the abilities a path carries, and the traits.
	void Update();
	// Every frame of a fight: blows landing after their swing, arrows falling, pins, bleeding,
	// Renewal's pulses, last-ditch saves.
	void Tick(float a_dt);
	// Out of a fight: a fletcher's arrows.
	void Rest(float a_dt);
	// The fight is over.
	void Clear();
	// A load or a new game.
	void Reset();
	void InstallEvents();

	// The looks that last as long as an ability does: a bruiser's fury, an archer's focus, a
	// mage's surge, a pinned enemy. With the planner's other glows.
	void Show();

	// Not one of the eight abilities Fight With Me had before the paths.
	[[nodiscard]] bool IsNew(Paths::Ability a_ability);

	// The player asked for one now.
	void Request(RE::FormID a_member, Paths::Ability a_ability);
	[[nodiscard]] bool Asked(RE::FormID a_member);

	struct Clock
	{
		float ready{ 0.0f };     // seconds until it can be used again
		float cooldown{ 0.0f };  // length of the last wait, for the badge
		bool  requested{ false };
	};
	[[nodiscard]] Clock ClockOf(RE::FormID a_member, Paths::Ability a_ability);

	// Every frame, in a fight or out of it: every wait runs down by these world seconds.
	void Cool(float a_seconds);

	// Waits still running, for the co-save, and back from it.
	[[nodiscard]] std::vector<Cooldowns::Wait> SavedWaits();
	void LoadWaits(std::span<const Cooldowns::Wait> a_waits);

	// What their path's traits and the abilities running on them change in how they fight.
	[[nodiscard]] StyleTweaks TweaksFor(RE::FormID a_member, const PathsLink::Kit& a_kit);

	// Buffs from these abilities for a companion's crest.
	void Statuses(RE::FormID a_member, std::vector<View::Status>& a_out);
}
