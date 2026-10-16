#include "FWM/StyleLayer.h"

#include "FWM/Rules.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace FWM
{
	namespace
	{
		// A role's intent: a target for some values, a scale for the score
		// multipliers, flags to set and clear. Unset targets leave a value alone.
		struct Intent
		{
			std::optional<float> offensive, defensive, groupOffensive, avoidThreat;
			std::optional<float> attackStaggered, powerAttackStaggered, powerAttackBlocking, bash, bashRecoil, bashAttack, bashPowerAttack;
			std::optional<float> circle, fallback, flankDistance, strafe;
			float                meleeScale{ 1.0f }, magicScale{ 1.0f }, rangedScale{ 1.0f }, staffScale{ 1.0f };
			std::uint32_t        set{ 0 }, clear{ 0 };
		};

		// Calibrated against vanilla styles: csAlftandHumanTank, csWEBerserker,
		// csWERangedConjurer, csCWSoldierMissile and the defaults around them.
		Intent For(Role a_role)
		{
			Intent i;
			switch (a_role) {
			case Role::kTank:
				i.offensive = 0.45f;
				i.defensive = 0.85f;
				i.groupOffensive = 0.5f;
				i.avoidThreat = 0.05f;
				i.attackStaggered = 2.5f;
				i.powerAttackStaggered = 2.5f;
				i.powerAttackBlocking = 3.0f;
				i.bash = 5.5f;
				i.bashRecoil = 3.0f;
				i.bashAttack = 5.0f;
				i.bashPowerAttack = 3.0f;
				i.circle = 0.15f;
				i.fallback = 0.05f;
				i.flankDistance = 0.2f;
				i.strafe = 0.15f;
				i.meleeScale = 1.5f;
				i.set = StyleData::kDueling;
				i.clear = StyleData::kFlanking;
				break;
			case Role::kHealer:
				i.offensive = 0.3f;
				i.defensive = 0.8f;
				i.avoidThreat = 0.6f;
				i.circle = 0.3f;
				i.fallback = 0.85f;
				i.strafe = 0.5f;
				i.magicScale = 2.0f;
				i.staffScale = 1.5f;
				i.meleeScale = 0.5f;
				i.clear = StyleData::kDueling;
				break;
			case Role::kBruiser:
				i.offensive = 0.85f;
				i.defensive = 0.25f;
				i.groupOffensive = 0.85f;
				i.avoidThreat = 0.1f;
				i.attackStaggered = 4.0f;
				i.powerAttackStaggered = 6.0f;
				i.powerAttackBlocking = 6.0f;
				i.bashPowerAttack = 4.0f;
				i.circle = 0.1f;
				i.fallback = 0.05f;
				i.meleeScale = 1.5f;
				i.set = StyleData::kDueling;
				i.clear = StyleData::kFlanking;
				break;
			case Role::kArcher:
				i.offensive = 0.5f;
				i.defensive = 0.4f;
				i.avoidThreat = 0.5f;
				i.circle = 0.35f;
				i.fallback = 0.75f;
				i.strafe = 0.6f;
				i.rangedScale = 3.0f;
				i.meleeScale = 0.6f;
				break;
			case Role::kMage:
				i.offensive = 0.6f;
				i.defensive = 0.4f;
				i.avoidThreat = 0.5f;
				i.fallback = 0.7f;
				i.strafe = 0.5f;
				i.magicScale = 2.5f;
				i.staffScale = 1.5f;
				i.meleeScale = 0.5f;
				break;
			case Role::kFighter:
			default:
				i.offensive = 0.65f;
				i.defensive = 0.45f;
				i.groupOffensive = 0.7f;
				i.circle = 0.6f;
				i.fallback = 0.25f;
				i.flankDistance = 0.6f;
				i.set = StyleData::kFlanking;
				break;
			}
			return i;
		}

		void Pull(float& a_value, const std::optional<float>& a_target, float a_weight)
		{
			if (a_target) {
				a_value += (*a_target - a_value) * a_weight;
			}
		}

		void Scale(float& a_value, float a_scale, float a_weight)
		{
			const float factor = 1.0f + (a_scale - 1.0f) * a_weight;
			// Never below 0.2 of the original: a role leans on a habit, it does not erase it.
			a_value = std::clamp(a_value * std::max(factor, 0.2f), 0.0f, 10.0f);
		}
	}

	StyleData Layer(const StyleData& a_base, Role a_role, Position a_position, float a_weight)
	{
		const float w = std::clamp(a_weight, 0.0f, 1.0f);
		const auto  intent = For(a_role);
		StyleData   s = a_base;

		Pull(s.offensive, intent.offensive, w);
		Pull(s.defensive, intent.defensive, w);
		Pull(s.groupOffensive, intent.groupOffensive, w);
		Pull(s.avoidThreat, intent.avoidThreat, w);
		Pull(s.attackStaggered, intent.attackStaggered, w);
		Pull(s.powerAttackStaggered, intent.powerAttackStaggered, w);
		Pull(s.powerAttackBlocking, intent.powerAttackBlocking, w);
		Pull(s.bash, intent.bash, w);
		Pull(s.bashRecoil, intent.bashRecoil, w);
		Pull(s.bashAttack, intent.bashAttack, w);
		Pull(s.bashPowerAttack, intent.bashPowerAttack, w);
		Pull(s.circle, intent.circle, w);
		Pull(s.fallback, intent.fallback, w);
		Pull(s.flankDistance, intent.flankDistance, w);
		Pull(s.strafe, intent.strafe, w);

		Scale(s.meleeScore, intent.meleeScale, w);
		Scale(s.magicScore, intent.magicScale, w);
		Scale(s.rangedScore, intent.rangedScale, w);
		Scale(s.staffScore, intent.staffScale, w);

		if (w >= 0.5f) {
			s.flags = (s.flags | intent.set) & ~intent.clear;
		}

		switch (a_position) {
		case Position::kHoldBack:
			s.fallback = std::min(s.fallback + 0.2f * w, 1.0f);
			s.offensive *= 1.0f - 0.15f * w;
			break;
		case Position::kFrontLine:
			// Into the fight: press, close the distance, give no ground.
			s.offensive = std::min(s.offensive + 0.3f * w, 1.0f);
			s.circle *= 1.0f - 0.5f * w;
			s.fallback *= 1.0f - 0.8f * w;
			break;
		case Position::kGuard:
			s.groupOffensive *= 1.0f - 0.2f * w;
			s.avoidThreat *= 1.0f - 0.3f * w;
			break;
		case Position::kWithMe:
			// At your side and staying there: no pressing out after anyone, and no giving
			// ground, circling or looking for cover either, which only takes them away from you.
			s.offensive *= 1.0f - 0.3f * w;
			s.groupOffensive *= 1.0f - 0.5f * w;
			s.fallback *= 1.0f - 0.8f * w;
			s.avoidThreat *= 1.0f - 0.6f * w;
			s.circle *= 1.0f - 0.6f * w;
			break;
		default:
			break;
		}

		// Inside 256 units the engine scores a bow at a tenth and a dagger at ten times
		// (fCombatInventoryMinRangeScoreMult, fCombatInventoryDesiredRangeScoreMultMax),
		// and a dagger once drawn stays out for five seconds with 384 more reach: an
		// archer who gets knocked about draws it and charges, and keeps at it. So a
		// ranged role's melee is all but gone wherever they stand, short of the front
		// line; the bow wins even point blank, and steel comes out only when there is
		// nothing to shoot with.
		if (Rules::IsRanged(a_role)) {
			const float melee = std::pow(a_position == Position::kFrontLine ? 0.2f : 0.001f, w);
			s.meleeScore = a_base.meleeScore * melee;
			s.unarmedScore = a_base.unarmedScore * melee;
			if (a_position == Position::kHoldBack || a_position == Position::kGuard) {
				s.fallback = std::max(s.fallback, 0.9f * w);
				s.avoidThreat = std::max(s.avoidThreat, 0.7f * w);
				s.circle = std::min(s.circle, 0.2f);
			} else if (a_position == Position::kWithMe) {
				// Planted at your side and shooting: a step back is a step away from you.
				s.circle = std::min(s.circle, 0.2f);
				s.strafe *= 1.0f - 0.5f * w;
			}
		}

		auto unit = [](float& v) { v = std::clamp(v, 0.0f, 1.0f); };
		unit(s.offensive);
		unit(s.defensive);
		unit(s.groupOffensive);
		unit(s.avoidThreat);
		unit(s.circle);
		unit(s.fallback);
		unit(s.flankDistance);
		unit(s.stalkTime);
		unit(s.strafe);
		return s;
	}

	StyleData Tweaked(const StyleData& a_style, const StyleTweaks& a_tweaks)
	{
		StyleData s = a_style;
		auto      unit = [](float& a_value) { a_value = std::clamp(a_value, 0.0f, 1.0f); };
		s.bash *= a_tweaks.bash;
		s.bashRecoil *= a_tweaks.bash;
		s.bashAttack *= a_tweaks.bash;
		s.bashPowerAttack *= a_tweaks.bash * a_tweaks.power;
		s.powerAttackStaggered *= a_tweaks.power;
		s.powerAttackBlocking *= a_tweaks.power;
		s.specialAttack *= a_tweaks.power;
		s.flankDistance *= a_tweaks.flank;
		if (a_tweaks.flank > 1.0f) {
			s.flags |= StyleData::kFlanking;
		}
		s.circle *= a_tweaks.circle;
		s.fallback *= a_tweaks.fallback;
		s.strafe *= a_tweaks.strafe;
		s.avoidThreat *= a_tweaks.avoidThreat;
		s.offensive += a_tweaks.offensive;
		s.defensive += a_tweaks.defensive;
		unit(s.offensive);
		unit(s.defensive);
		unit(s.avoidThreat);
		unit(s.circle);
		unit(s.fallback);
		unit(s.flankDistance);
		unit(s.strafe);
		return s;
	}

	StyleData Committed(const StyleData& a_style)
	{
		StyleData s = a_style;
		// Circle chance runs 25-75% on circleMult each time the combat AI is out of reach
		// (fCombatCircleDistantChanceMin/Max); a dueling style circles up close as well, and a
		// high defensive mult keeps a shield up against arrows instead of closing under them.
		s.offensive = 1.0f;
		s.groupOffensive = 1.0f;
		s.defensive = std::min(s.defensive, 0.15f);
		s.avoidThreat = std::min(s.avoidThreat, 0.05f);
		s.circle = 0.0f;
		s.fallback = 0.0f;
		s.flankDistance = 0.0f;
		s.stalkTime = 0.0f;
		s.strafe = 0.0f;
		s.flags &= ~(StyleData::kDueling | StyleData::kFlanking);
		return s;
	}
}
