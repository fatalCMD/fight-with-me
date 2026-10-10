#pragma once

// Hides the game's interface (the HUD, TrueHUD and any other open Scaleform
// movie) while the battle plan is up, and shows exactly what was hidden again.
// Main thread only.

namespace FWM::Hud
{
	void Hide();
	void Restore();
}
