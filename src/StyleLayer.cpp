#include "FWM/StyleLayer.h"

#include "FWM/Rules.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace FWM
{
	namespace
	{
		// A role's intent: a target for some values, flags to set and clear. Unset targets leave a
		// value alone. A role only moves them (how far they keep, whether they circle, fall back or
		// work round to the side); what they attack with and how is their own, or their path's.
		struct Intent
		{
			std::optional<float> avoidThreat, circle, fallback, flankDistance, strafe;
			std::uint32_t        set{ 0 }, clear{ 0 };
		};

		// Calibrated against vanilla styles: csAlftandHumanTank, csWEBerserker,
		// csWERangedConjurer, csCWSoldierMissile and the defaults around them.
		Intent For(Role a_role)
		{
			Intent i;
			switch (a_role) {
			case Role::kTank:
				i.avoidThreat = 0.05f;
				i.circle = 0.15f;
				i.fallback = 0.05f;
				i.flankDistance = 0.2f;
				i.strafe = 0.15f;
				i.set = StyleData::kDueling;
				i.clear = StyleData::kFlanking;
				break;
            case Role::kHealer:
				i.avoidThreat = 0.6f;
				i.circle = 0.3f;
				i.fallback = 0.85f;
				i.strafe = 0.5f;
				i.clear = StyleData::kDueling;
				break;
			case Role::kBruiser:
				i.avoidThreat = 0.1f;
				i.circle = 0.1f;
				i.fallback = 0.05f;
				i.set = StyleData::kDueling;
				i.clear = StyleData::kFlanking;
				break;
			case Role::kArcher:
				i.avoidThreat = 0.5f;
				i.circle = 0.35f;
				i.fallback = 0.75f;
				i.strafe = 0.6f;
				break;
			case Role::kMage:
				i.avoidThreat = 0.5f;
				i.fallback = 0.7f;
				i.strafe = 0.5f;
				break;
			case Role::kFighter:
			default:
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
	}

	StyleData Layer(const StyleData& a_base, Role a_role, Position a_position, float a_weight)
	{
		const float w = std::clamp(a_weight, 0.0f, 1.0f);
		const auto  intent = For(a_role);
		StyleData   s = a_base;

		Pull(s.avoidThreat, intent.avoidThreat, w);
		Pull(s.circle, intent.circle, w);
		Pull(s.fallback, intent.fallback, w);
		Pull(s.flankDistance, intent.flankDistance, w);
		Pull(s.strafe, intent.strafe, w);

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

		// A ranged role holding back or guarding keeps its distance whatever comes near; with you,
		// it stays planted at your side.
		if (Rules::IsRanged(a_role)) {
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
        if(a_tweaks.caster) s.meleeScore=s.unarmedScore=0.0f;
        if(a_tweaks.magickaRecovery) {
            s.meleeScore=s.unarmedScore=s.magicScore=s.staffScore=s.rangedScore=0.0f;
            s.fallback=1.0f;
        }
        if (a_tweaks.recovery || a_tweaks.magickaRecovery) {
            s.offensive = 0.0f;
            s.groupOffensive = 0.0f;
            s.defensive = 1.0f;
            s.circle = s.flankDistance = s.stalkTime = s.strafe = 0.0f;
            s.avoidThreat = 1.0f;
            s.flags &= ~(StyleData::kDueling | StyleData::kFlanking);
        }
		unit(s.offensive);
		unit(s.defensive);
		unit(s.avoidThreat);
		unit(s.circle);
		unit(s.fallback);
		unit(s.flankDistance);
		unit(s.strafe);
		return s;
	}

	StyleData PositionIntent(const StyleData& a_style, Role a_role, Position a_position, float a_weight)
	{
		StyleData s = a_style;
		if (a_weight >= 0.5f && a_position == Position::kFrontLine && !Rules::IsRanged(a_role)) {
			// Flanking is a separate engine behavior: reducing circleMult alone leaves
			// a front-line fighter orbiting at a distance, especially in a crowded fight.
			s.circle = 0.0f;
			s.flankDistance = 0.0f;
			s.stalkTime = 0.0f;
			s.flags &= ~(StyleData::kDueling | StyleData::kFlanking);
		}
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
    StyleData TimidSkald(const StyleData& base,bool moving){
        auto s=base;
        s.offensive=.02f;s.groupOffensive=0.f;s.defensive=1.f;
        s.meleeScore=s.rangedScore=s.staffScore=s.shoutScore=0.f;s.unarmedScore=.15f;
        // It holds a spell it never casts (Skald), so the combat AI moves it as it moves
        // a mage: at range, sidestepping, giving ground as its own style says.
        s.magicScore=std::max(base.magicScore,1.f);
        s.flankDistance=s.stalkTime=0.f;
        // Walking back into its leash, nothing may turn it around. Fists keep their
        // score on the way: an AI with nothing usable runs.
        if(moving){s.offensive=s.groupOffensive=0.f;s.avoidThreat=s.fallback=s.circle=s.strafe=0.f;}
        s.attackStaggered=.05f;s.powerAttackStaggered=s.powerAttackBlocking=0.f;
        s.bash=s.bashRecoil=s.bashAttack=s.bashPowerAttack=s.specialAttack=0.f;
        s.flags&=~(StyleData::kDueling|StyleData::kFlanking|StyleData::kAllowDualWielding);
        return s;
    }
}
