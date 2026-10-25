#pragma once

namespace FWM::Rejuvenations
{
	void Begin(RE::Actor* a_healer, int a_rank, float a_seconds);
	void Tick(float a_dt);
	void Clear();
}
