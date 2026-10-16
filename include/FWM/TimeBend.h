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

	// Every frame the plan is open, main thread, even with the world stopped. Menu Framework 3
	// by default freezes the world and blurs it under any window that takes input, the plan
	// included: the slow time stops dead and the fight is lost in the blur. Both are lifted
	// while the plan is open.
	void KeepClear();

	// The plan's window has gone: the freeze and the blur go back to whoever still wants them,
	// such as Menu Framework's own panel left open behind it.
	void HandBack(bool a_menuStillOpen);

	[[nodiscard]] bool Active() noexcept;
}
