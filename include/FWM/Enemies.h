#pragma once

// Everyone the party is fighting, with what they are: caster, healer, archer,
// boss, and how dangerous. Built from the combat groups of the player and the
// party; the slow parts (spell lists, equipment) are refreshed every few seconds.

namespace FWM::Enemies
{
	struct Profile
	{
		RE::ActorHandle handle;
		RE::FormID      id{ 0 };
		std::string     name;
		int             level{ 1 };
		float           health{ 1.0f };  // 0..1
		float           threat{ 0.0f };
		bool            caster{ false };
		bool            healer{ false };
		bool            archer{ false };
		bool            boss{ false };
		RE::ActorHandle target;  // whom they are fighting
		float           refreshIn{ 0.0f };
		bool            seen{ false };
	};

	// Rebuild the list. a_dt advances the per-enemy refresh timers.
	void Update(float a_dt);
	void Clear();

	[[nodiscard]] std::vector<Profile>& All();
	[[nodiscard]] Profile*              Find(RE::FormID a_id);
	[[nodiscard]] Profile*              Find(const RE::ActorHandle& a_handle);
}
