#pragma once
#include "FWM/Soundscape.h"

namespace FWM::EffectAudio
{
	// All handles are transient and owned here. Ground sounds stay at their origin;
	// attached sounds follow an actor and stop on death, unload, or a cell change.
	using ID = std::uint32_t;
	ID Begin(Soundscape::Kind kind, RE::Actor* source, const RE::NiPoint3& at, float seconds, int rank=1, bool follow=false, int element=0);
	void End(ID id);
	void Move(ID id, const RE::NiPoint3& at);
	void Tick(float dt);
	void Clear();
}
