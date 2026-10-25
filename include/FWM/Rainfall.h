#pragma once

// Arrow Rain's look in the game: the flare the archer shoots up to call it, the mark on the ground and
// the glow in the sky (ArrowRain has the numbers), and a trail on every real arrow Arts looses,
// followed in flight down to where it strikes. Main thread.

namespace FWM::Rainfall
{
	// A rain a_archer calls over a_over (followed while they stand), centred at a_centre, falling for
	// a_rain seconds once the flare bursts. Its id for Loose, or 0 when it does not show.
	[[nodiscard]] std::uint32_t Call(RE::Actor* a_archer, RE::Actor* a_over, const RE::NiPoint3& a_centre, float a_rain, bool a_grand);

	// One of its arrows on its way, aimed at a_spot over ground at a_ground: a spectral arrow follows it down.
	void Loose(std::uint32_t a_rain, RE::ProjectileHandle a_arrow, const RE::NiPoint3& a_spot, float a_ground);

	void Tick(float a_dt);
	void Clear();
}
