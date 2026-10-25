#pragma once

namespace FWM::RejuvenateField
{
	struct Status { float left{}, length{}; int rank{}; };
	void Begin(RE::Actor* healer, int rank, float seconds, float vigorShare);
	void Tick(float dt);
	void Clear();
	Status On(RE::Actor* actor);
}
