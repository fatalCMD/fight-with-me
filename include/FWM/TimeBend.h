#pragma once

// Slows the world while the battle plan is open (or pauses it, if the INI says
// so) and puts back exactly what was there before.

namespace FWM::TimeBend
{
	void Register();  // at data load: the invisible pausing menu

	void Begin();
	void End();

	// Every frame, main thread. Eases toward the target using real time.
	void Update();

	[[nodiscard]] bool Active() noexcept;
}
