#pragma once

#include "FWM/Blast.h"

// Elemental Burst in the world: a little light flies from the mage's hand to the target's feet and
// bursts there in fire, frost or lightning, striking every enemy within its reach. It throws real
// light, plays the element's sounds, and marks those it strikes; fire keeps burning a moment. How it
// looks over time comes from Blast, which the tests and the preview share. Main thread.

namespace FWM::Blasts
{
	struct Strike
	{
		Blast::Element element{ Blast::Element::kFire };
		float          damage{ 0.0f };          // to an enemy at its middle, before their resistances
		float          radius{ 250.0f };        // its reach
		bool           grand{ false };          // the third tier's, with its showpiece (Blast::At)
		float          stagger{ 0.0f };         // how hard those near its middle stagger, 0 not at all
		float          searing{ 0.0f };         // fire, frost and shock resistance taken from each enemy struck (Searing)
		float          searingSeconds{ 0.0f };
		std::uint16_t  source{ 0 };             // what Searing's lifts are known by
		RE::BGSSoundDescriptorForm* release{ nullptr };  // the spell's own sound as the light leaves the hand
	};

	// Sends the light from a_mage's hand at a_target's feet. False if either is missing.
	bool Cast(RE::Actor* a_mage, std::string a_name, RE::Actor* a_target, const Strike& a_strike, const RE::NiPoint3* a_point = nullptr);

	// Every frame of a fight.
	void Tick(float a_dt);

	// Every blast and burn out: the fight is over, or a load.
	void Clear();
}
