#include "FWM/AbilityPolicy.h"
#include "FWM/DragonBreath.h"
#include <algorithm>
#include <cmath>

namespace FWM::AbilityPolicy
{
	Decision Consider(Paths::Ability a, const Context& c)
	{
		using A = Paths::Ability;
		// Healing may rescue someone even when every enemy is behind cover.
		if (a == A::kHeal && (c.allyDown || c.allyHealth < c.healLine)) {
			return { "nearby ally needs healing", c.allyDown || c.allyHealth < 0.30f };
		}
		if (a == A::kSecondWind && c.health < 0.35f) {
			return { "self preservation", true };
		}
		if (a == A::kRenewal && (c.allyDown || c.hurt >= 2)) {
			return { "several allies wounded", c.allyDown || c.allyHealth < 0.30f };
		}
		if (a == A::kStandFast && c.criticalAlly) return { "rescue critically wounded nearby ally", true };
		if (c.enemies == 0) { return {}; }
		// Give an equipped rescue/defensive skill priority over spending an offensive cast.
		if (c.health < 0.30f && (a == A::kBurst || a == A::kDragonBreath || a == A::kSpectralWolf || a == A::kVolley ||
			a == A::kDeadeye || a == A::kArrowRain || a == A::kSunder)) return {};
		const bool durable = c.targetHealth > 0.35f && (c.targetBoss || c.targetStrong);
		const bool engaged = c.enemies >= 2 || c.boss || c.allyHealth < 0.65f;
		switch (a) {
		case A::kTaunt:
			if (c.peel && (c.enemies >= 2 || c.allyHealth < 0.65f || c.threatenedCaster))
				return { "pull pressure off an ally", c.allyHealth < 0.30f };
			break;
		case A::kBodyguard:
			if (c.onSelf >= 2 || (c.onSelf > 0 && (c.health < 0.65f || c.boss)))
				return { "weaken attackers pressing the tank", c.health < 0.30f };
			break;
		case A::kChaosDown:
			if (c.close >= 2 && c.onParty > 0) return { "slow nearby attackers" };
			break;
		case A::kWarCry:
			if (c.close >= 3 || (c.close >= 2 && c.health < 0.60f)) return { "surrounded in melee" };
			break;
		case A::kWard:
			if (c.onParty >= 3 || (c.onParty > 0 && (c.allyHealth < 0.65f || c.playerHealth < 0.65f || c.boss))) return { "protect party under pressure", c.allyHealth < 0.30f };
			break;
		case A::kRejuvenate:
			if (c.allyDry && engaged) return { "nearby caster running dry" };
			break;
		case A::kSurge:
			if (c.magicka < 0.30f) return { "recover depleted magicka" };
			break;
		case A::kMagnus:
			if ((c.boss && c.magicka < 0.50f) || (c.enemies >= 3 && c.magicka < 0.40f)) return { "magicka needed for a sustained fight" };
			break;
		case A::kEarthshaker:
			if (c.close >= 3) return { "break up surrounding enemies" };
			break;
		case A::kRiposte:
			if (c.onSelf >= 2 || (c.onSelf > 0 && c.health < 0.60f)) return { "defend against melee pressure" };
			break;
		case A::kDisarm:
			if (c.targetDistance <= 350.0f && c.targetHealth > 0.25f && (durable || c.health < 0.65f)) return { "disarm a dangerous opponent" };
			break;
		case A::kSunder:
			if (c.targetDistance <= 350.0f && durable) return { "weaken a durable opponent" };
			break;
		case A::kSpectralWolf:
			if (c.targetDistance <= 600.0f && c.targetHealth > 0.35f && (durable || c.cluster >= 2)) return { "hold a strong or grouped opponent" };
			break;
		case A::kBerserk:
			if (c.health > 0.40f && (c.close >= 2 || (durable && c.targetDistance <= 600.0f))) return { "commit to a sustained melee fight" };
			break;
		case A::kDeadeye:
            if(c.targetDistance<=kEnemyReach&&(c.cluster>=2||durable))return {"detonate on a group or durable opponent"};
            break;
		case A::kVolley:
			if (c.targetDistance <= kEnemyReach && durable) return { "focus a durable opponent" };
			break;
		case A::kPinningShot:
			if (c.close > 0 && (c.onSelf > 0 || c.close >= 2 || c.health < .55f)) return { "escape close pressure", c.health < .35f || c.onSelf >= 2 };
			break;
		case A::kArrowRain:
			if (c.cluster >= 3) return { "strike a clustered group" };
			break;
		case A::kDragonBreath:
			// Prefer room for the entire spectral head; Cast also checks the hard 2 m safety radius.
			if (c.targetDistance >= 350 && c.targetDistance <= 1860 && c.targetHealth > .4f && (durable || c.cluster >= 2)) return { "breathe through a durable or grouped target" };
			break;
		case A::kBurst:
			if (c.targetDistance <= kEnemyReach && c.targetHealth > 0.30f && (durable || c.cluster >= 2)) return { "burst a cluster or durable target" };
			break;
		default: break;
		}
		return {};
	}

	void Pace::Tick(float dt) { if (std::isfinite(dt) && dt > 0) now += dt; }
	void Pace::Clear() { now = 0; partyAt = -100; castAt.clear(); abilityUntil.fill(0); }
	bool Pace::Ready(std::uint32_t id, Paths::Ability a, float age, bool urgent) const
	{
		const auto slot = static_cast<std::size_t>(a);
		if (slot == 0 || slot >= abilityUntil.size()) return false;
		const auto it = castAt.find(id);
		const double since = it == castAt.end() ? 100.0 : now - it->second;
		// Critical support can interrupt the normal pause, but never chain in the same instant.
		if (urgent) return since >= 2.0 && now - partyAt >= 0.65;
		const float variation = static_cast<float>((id ^ (id >> 8)) % 7) / 6.0f;
		return age >= 3.5f + 2.5f * variation && since >= 7.0 + 2.0 * variation &&
			now - partyAt >= 2.5 && now >= abilityUntil[slot];
	}
	void Pace::Used(std::uint32_t id, Paths::Ability a)
	{
		castAt[id] = now;
		partyAt = now;
		const auto slot = static_cast<std::size_t>(a);
		if (slot < abilityUntil.size()) abilityUntil[slot] = now + 8.0;
	}
}
