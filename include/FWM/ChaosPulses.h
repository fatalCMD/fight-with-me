#pragma once

#include <span>
#include <utility>

namespace FWM::ChaosPulses
{
	// Cosmetic only: the slow is applied independently by Arts.
	void Call(RE::Actor* a_tank, float a_reach, bool a_grand, float a_seconds);
	// Keep is fed the actual slow timers even while visual effects are disabled. Bind records
	// the strongest active rank without replacing or restarting an existing target's restraint.
	void Bind(RE::Actor* a_enemy, float a_seconds, bool a_grand);
	void Keep(std::span<const std::pair<RE::Actor*, float>> a_on);
	void Tick(float a_dt);
	void Clear();
}
