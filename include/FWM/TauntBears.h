#pragma once

namespace FWM::TauntBears
{
	// Main thread. False when the appearance is disabled or unavailable; caller keeps its old cue.
	[[nodiscard]] bool Call(RE::Actor* a_tank, bool a_grand);
	[[nodiscard]] bool WarCry(RE::Actor* a_actor, int a_rank);
	void Tick(float a_dt);
	void Clear();
}
