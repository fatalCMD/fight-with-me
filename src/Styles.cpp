#include "FWM/Styles.h"

#include "FWM/Forms.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/Settings.h"
#include "FWM/StyleLayer.h"

namespace FWM::Styles
{
	namespace
	{
		struct Layered
		{
			RE::TESCombatStyle* ours{ nullptr };      // never freed: a controller may still point at it
			RE::TESCombatStyle* original{ nullptr };  // what the controller had before us
			Role                role{ Role::kCount };
			Position            position{ Position::kCount };
			float               weight{ -1.0f };
		};

		std::unordered_map<RE::FormID, Layered> styles;

		StyleData Read(const RE::TESCombatStyle* a_style)
		{
			StyleData d;
			if (!a_style) {
				return d;
			}
			const auto& g = a_style->generalData;
			const auto& m = a_style->meleeData;
			const auto& c = a_style->closeRangeData;
			d.offensive = g.offensiveMult;
			d.defensive = g.defensiveMult;
			d.groupOffensive = g.groupOffensiveMult;
			d.meleeScore = g.meleeScoreMult;
			d.magicScore = g.magicScoreMult;
			d.rangedScore = g.rangedScoreMult;
			d.shoutScore = g.shoutScoreMult;
			d.unarmedScore = g.unarmedScoreMult;
			d.staffScore = g.staffScoreMult;
			d.avoidThreat = g.avoidThreatChance;
			d.attackStaggered = m.attackIncapacitatedMult;
			d.powerAttackStaggered = m.powerAttackIncapacitatedMult;
			d.powerAttackBlocking = m.powerAttackBlockingMult;
			d.bash = m.bashMult;
			d.bashRecoil = m.bashRecoilMult;
			d.bashAttack = m.bashAttackMult;
			d.bashPowerAttack = m.bashPowerAttackMult;
			d.specialAttack = m.specialAttackMult;
			d.circle = c.circleMult;
			d.fallback = c.fallbackMult;
			d.flankDistance = c.flankDistanceMult;
			d.stalkTime = c.stalkTimeMult;
			d.strafe = a_style->longRangeData.strafeMult;
			d.flags = a_style->flags.underlying();
			return d;
		}

		void Write(RE::TESCombatStyle* a_style, const RE::TESCombatStyle* a_source, const StyleData& d)
		{
			if (a_source) {
				a_style->flightData = a_source->flightData;
			}
			auto& g = a_style->generalData;
			auto& m = a_style->meleeData;
			auto& c = a_style->closeRangeData;
			g.offensiveMult = d.offensive;
			g.defensiveMult = d.defensive;
			g.groupOffensiveMult = d.groupOffensive;
			g.meleeScoreMult = d.meleeScore;
			g.magicScoreMult = d.magicScore;
			g.rangedScoreMult = d.rangedScore;
			g.shoutScoreMult = d.shoutScore;
			g.unarmedScoreMult = d.unarmedScore;
			g.staffScoreMult = d.staffScore;
			g.avoidThreatChance = d.avoidThreat;
			m.attackIncapacitatedMult = d.attackStaggered;
			m.powerAttackIncapacitatedMult = d.powerAttackStaggered;
			m.powerAttackBlockingMult = d.powerAttackBlocking;
			m.bashMult = d.bash;
			m.bashRecoilMult = d.bashRecoil;
			m.bashAttackMult = d.bashAttack;
			m.bashPowerAttackMult = d.bashPowerAttack;
			m.specialAttackMult = d.specialAttack;
			c.circleMult = d.circle;
			c.fallbackMult = d.fallback;
			c.flankDistanceMult = d.flankDistance;
			c.stalkTimeMult = d.stalkTime;
			a_style->longRangeData.strafeMult = d.strafe;
			a_style->flags = static_cast<RE::TESCombatStyle::FLAG>(d.flags);
		}

		RE::TESCombatStyle* Create()
		{
			auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::TESCombatStyle>();
			return factory ? static_cast<RE::TESCombatStyle*>(factory->Create()) : nullptr;
		}

		bool Ours(const RE::TESCombatStyle* a_style)
		{
			for (const auto& [id, layered] : styles) {
				if (layered.ours == a_style) {
					return true;
				}
			}
			return false;
		}
	}

	void Apply(RE::Actor* a_follower, const Orders& a_orders, Position a_resolved)
	{
		auto* controller = Game::Controller(a_follower);
		if (!controller) {
			return;
		}
		auto&       layered = styles[a_follower->GetFormID()];
		const float weight = Settings::Get().styleWeight;

		// A controller that does not point at our style has a fresh original: a new
		// fight, or something else changed it.
		const bool fresh = controller->combatStyle != layered.ours;
		if (fresh && controller->combatStyle && !Ours(controller->combatStyle)) {
			if (layered.original != controller->combatStyle) {
				layered.original = controller->combatStyle;
				layered.role = Role::kCount;  // force a rebuild from the new base
			}
		}
		if (!layered.original) {
			auto* base = a_follower->GetActorBase();
			layered.original = base && base->combatStyle ? base->combatStyle : Forms::Get().defaultStyle;
		}

		if (!layered.ours) {
			layered.ours = Create();
			if (!layered.ours) {
				return;
			}
		}
		if (layered.role != a_orders.role || layered.position != a_resolved || layered.weight != weight) {
			Write(layered.ours, layered.original, Layer(Read(layered.original), a_orders.role, a_resolved, weight));
			layered.role = a_orders.role;
			layered.position = a_resolved;
			layered.weight = weight;
			if (Settings::Get().logDecisions) {
				Log::Info("[Style] {} fights as {} ({}), over {:08X}."sv, Game::Name(a_follower), Info(a_orders.role).name,
					Info(a_resolved).name, layered.original ? layered.original->GetFormID() : 0);
			}
		}
		if (fresh) {
			controller->combatStyle = layered.ours;
		}
	}

	void Release(RE::Actor* a_follower)
	{
		const auto it = styles.find(a_follower->GetFormID());
		if (it == styles.end()) {
			return;
		}
		if (auto* controller = Game::Controller(a_follower); controller && controller->combatStyle == it->second.ours) {
			controller->combatStyle = it->second.original;
		}
		it->second.original = nullptr;
	}

	void ReleaseAll()
	{
		for (auto& [id, layered] : styles) {
			if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(id)) {
				if (auto* controller = Game::Controller(actor); controller && controller->combatStyle == layered.ours) {
					controller->combatStyle = layered.original;
				}
			}
			layered.original = nullptr;
		}
	}
}
