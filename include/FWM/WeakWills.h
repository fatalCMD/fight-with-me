#pragma once

namespace FWM::WeakWills
{
	void Call(RE::Actor* actor, float seconds, int rank);
	void End(RE::FormID actor);
	void Tick(float dt);
	void Clear();
}
