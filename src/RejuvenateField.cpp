#include "FWM/RejuvenateField.h"
#include "FWM/RejuvenatePolicy.h"
#include "FWM/Game.h"
#include "FWM/Party.h"
#include "FWM/Settings.h"

namespace FWM::RejuvenateField
{
	namespace {
		struct Field { RE::ActorHandle owner; RE::NiPoint3 at; RE::FormID cell{}; float radius{}, left{}, length{}, vigor{}; int rank{}; };
		std::vector<Field> fields;
		bool Contains(const Field& field, RE::Actor* actor) {
			if (!Game::Usable(actor) || !actor->GetParentCell() || actor->GetParentCell()->GetFormID() != field.cell) return false;
			const auto delta = actor->GetPosition() - field.at;
			return RejuvenatePolicy::Inside(delta.x, delta.y, delta.z, field.radius);
		}
		bool Alive(const Field& field) {
			const auto owner = field.owner.get();
			return field.left > 0 && Game::Usable(owner.get()) && owner->GetParentCell() && owner->GetParentCell()->GetFormID() == field.cell;
		}
	}
	void Begin(RE::Actor* healer, int rank, float seconds, float vigorShare) {
		if (!healer || !healer->GetParentCell()) return;
		const float duration = RejuvenatePolicy::Seconds(seconds);
		fields.push_back({healer->GetHandle(), healer->GetPosition(), healer->GetParentCell()->GetFormID(), RejuvenatePolicy::Radius(rank), duration, duration, vigorShare, rank});
	}
	Status On(RE::Actor* actor) {
		Status result;
		for (const auto& field : fields) if (Alive(field) && Contains(field, actor) && (field.rank > result.rank || (field.rank == result.rank && field.left > result.left))) result = {field.left, field.length, field.rank};
		return result;
	}
	void Tick(float dt) {
		if (!Settings::Get().enabled || !Settings::Get().rejuvenate) { Clear(); return; }
		if (!std::isfinite(dt) || dt <= 0) return;
		std::erase_if(fields, [](const Field& field) { return !Alive(field); });
		std::unordered_set<RE::FormID> visited;
		auto restore = [&](RE::Actor* actor) {
			if (!actor || !visited.insert(actor->GetFormID()).second) return;
			RejuvenatePolicy::Pulse pulse;
			float magicka = 0;
			for (const auto& field : fields) if (Contains(field, actor)) {
				pulse.Include(dt, field.left, field.vigor);
				magicka = std::max(magicka, RejuvenatePolicy::Restore(Game::Maximum(actor, RE::ActorValue::kMagicka), Game::Current(actor, RE::ActorValue::kMagicka), RejuvenatePolicy::Rate(field.rank), std::min(dt, field.left), RejuvenatePolicy::Cap(field.rank)));
			}
			const float activeSeconds = pulse.magickaSeconds;
			if (activeSeconds <= 0) return;
			auto give = [&](RE::ActorValue value, float share, float seconds) {
				const float amount = RejuvenatePolicy::Restore(Game::Maximum(actor, value), Game::Current(actor, value), share, seconds);
				if (amount > 0) actor->AsActorValueOwner()->RestoreActorValue(value, amount);
			};
			if (magicka > 0) actor->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kMagicka, magicka);
			give(RE::ActorValue::kStamina, pulse.staminaShareSeconds / activeSeconds, activeSeconds);
		};
		restore(RE::PlayerCharacter::GetSingleton());
		for (const auto& member : Party::Members()) if (!member.summon) { const auto actor = member.handle.get(); restore(actor.get()); }
		for (auto& field : fields) field.left -= dt;
		std::erase_if(fields, [](const Field& field) { return field.left <= 0; });
	}
	void Clear() { fields.clear(); }
}
