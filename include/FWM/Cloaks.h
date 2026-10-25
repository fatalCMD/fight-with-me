#pragma once

// Renewal's look in the game: the cloak of water on the healer while it lasts, following them, and the
// wash of the same water down each one a pulse heals (WaterCloak has the numbers). Main thread.

namespace FWM::Cloaks
{
	// The cloak on a_healer for a_seconds, gold for Renewal's third rank (a_grand). Its id for Wash and End,
	// or 0 when it does not show.
	[[nodiscard]] std::uint32_t Wear(RE::Actor* a_healer, float a_seconds, bool a_grand);

	// A pulse of it reaching a_ally: water runs down them, head to foot.
	void Wash(std::uint32_t a_cloak, RE::Actor* a_ally);

	// Renewal cut short: the cloak falls away now.
	void End(std::uint32_t a_cloak);

	// Every cloak falls away: the fight is over.
	void EndAll();

	void Tick(float a_dt);
	void Clear();
}
