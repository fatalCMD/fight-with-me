#pragma once
#include "FWM/Party.h"
#include "FWM/Paths.h"

namespace FWM::AbilityAuto
{
	void Tick(float a_dt);
	void Clear();
	[[nodiscard]] bool Visible(RE::Actor* a_from, RE::Actor* a_to, float a_reach);
	[[nodiscard]] bool Allow(const Party::Member& a_member, RE::Actor* a_actor, Paths::Ability a_ability);
	void Used(RE::FormID a_id, RE::Actor* a_actor, Paths::Ability a_ability, bool a_ordered);
}
