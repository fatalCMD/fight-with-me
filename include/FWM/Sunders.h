#pragma once

// Sunder's cracks follow the struck torso while the armor fragments fly from the original impact.
// Visuals only: the strike, armor reduction and stagger remain in Arts. Main thread, never saved.
namespace FWM::Sunders
{
	// False when no custom mesh can be shown; the caller can use the ordinary impact as a fallback.
	[[nodiscard]] bool Break(RE::Actor* a_by, RE::Actor* a_enemy, float a_seconds, bool a_grand);
	void Tick(float a_dt);
	void Clear();
}
