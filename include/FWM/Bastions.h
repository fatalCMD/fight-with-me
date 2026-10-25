#pragma once
namespace FWM::Bastions
{
	void Call(RE::Actor* actor, float seconds, int rank);
	void Tick(float dt);
	void Clear();
}
