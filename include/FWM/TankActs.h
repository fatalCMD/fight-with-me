#pragma once

#include "FWM/PathsLink.h"

namespace FWM::TankActs
{
	struct Status { float left{ 0 }, length{ 0 }, percent{ 0 }; };
	bool WeakWill(RE::Actor* actor, const PathsLink::Kit& kit);
	bool LastBastion(RE::Actor* actor, const PathsLink::Kit& kit);
	bool CanSave(RE::Actor* actor, float reach);
	// Called from the native weapon and harmful-health magic hooks, before damage is given.
	// Registers an aggressor striking an active helmet and returns their outgoing multiplier.
	float Attack(RE::Actor* attacker, RE::Actor* target, bool countDamage = true);
	Status WillOn(RE::FormID actor);
	Status BastionOn(RE::FormID actor);
	void Tick(float dt);
	void Clear();
	void Install();
}
