#include "FWM/AbilityAuto.h"
#include "FWM/ArrowArtsFx.h"
#include "FWM/AbilityPolicy.h"
#include "FWM/Abilities.h"
#include "FWM/Enemies.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/TankActs.h"
#include "FWM/TankPolicy.h"

namespace FWM::AbilityAuto
{
	namespace
	{
		AbilityPolicy::Pace pace;
		std::unordered_map<RE::FormID, std::string_view> reasons;
	}
	bool Visible(RE::Actor* from, RE::Actor* to, float reach)
	{
		if (!from || !to || !Game::Usable(to) || !to->Is3DLoaded() || Game::Distance(from, to) > reach) return false;
		if (from == to) return true;
		bool unused = false;
		return from->HasLineOfSight(to, unused);
	}
	void Tick(float dt) { pace.Tick(dt); }
	void Clear() { pace.Clear(); reasons.clear(); }
	bool Allow(const Party::Member& member, RE::Actor* actor, Paths::Ability ability)
	{
		using namespace AbilityPolicy;
		Context c;
		c.combatAge = member.inCombatFor;
		c.health = Game::Fraction(actor, RE::ActorValue::kHealth);
		c.magicka = member.drained ? 0.0f : Game::Fraction(actor, RE::ActorValue::kMagicka);
		if (const auto* orders = Party::FindOrders(member.id)) c.healLine = static_cast<float>(orders->healThreshold) / 100.0f;
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto ally = [&](RE::Actor* who, bool caster) {
			if (!Visible(actor, who, kSupportReach)) return;
			const float hp = Game::Fraction(who, RE::ActorValue::kHealth);
			c.allyHealth = std::min(c.allyHealth, hp);
			c.hurt += hp < 0.70f ? 1 : 0;
			c.allyDown = c.allyDown || who->AsActorState()->IsBleedingOut();
			c.allyDry = c.allyDry || (caster && Game::Fraction(who, RE::ActorValue::kMagicka) < 0.30f && Abilities::RejuvenatedFor(who->GetFormID()) <= 0.0f);
			if (who == player) c.playerHealth = hp;
		};
		ally(player, true);
		for (const auto& each : Party::Members()) {
			const auto who = each.handle.get();
			const auto* orders = Party::FindOrders(each.id);
			ally(who.get(), (orders && ChoosesSpells(orders->role)) || each.arsenal.casts);
		}
		auto* controller = Game::Controller(actor);
		auto chosen = member.marked.get();
		if (!chosen || chosen->IsDead() || !Enemies::Find(chosen->GetFormID()) || ability == Paths::Ability::kBurst || ability == Paths::Ability::kDragonBreath || ability == Paths::Ability::kDisarm)
			chosen = controller ? controller->targetHandle.get() : RE::NiPointer<RE::Actor>{};
		std::vector<RE::NiPointer<RE::Actor>> local;
		for (const auto& profile : Enemies::All()) {
			auto enemy = profile.handle.get();
			if (!Visible(actor, enemy.get(), kEnemyReach) || enemy->AsActorState()->IsBleedingOut() || Party::IsPartyActor(enemy.get())) continue;
			const float distance = Game::Distance(actor, enemy.get());
			const float hp = Game::Fraction(enemy.get(), RE::ActorValue::kHealth);
			++c.enemies;
			c.close += distance <= 400.0f ? 1 : 0;
			c.boss = c.boss || (profile.boss && hp > 0.35f);
			if (chosen == enemy) {
				c.targetDistance = distance;
				c.targetHealth = hp;
				c.targetBoss = profile.boss;
				c.targetStrong = profile.level >= actor->GetLevel() + 5;
			}
			auto victim = profile.target.get();
			if (victim && Visible(actor, victim.get(), kSupportReach) && Party::IsPartyActor(victim.get())) {
				++c.onParty;
				c.onSelf += victim.get() == actor && distance <= 500.0f ? 1 : 0;
				c.onPlayer += victim.get() == player ? 1 : 0;
				c.casterThreat = c.casterThreat || profile.caster || profile.archer;
				const auto* orders = Party::FindOrders(victim->GetFormID());
				const bool vulnerable = orders && ChoosesSpells(orders->role);
				c.threatenedCaster = c.threatenedCaster || vulnerable;
				c.peel = c.peel || (victim.get() != actor && distance <= 900.0f);
			}
			local.push_back(std::move(enemy));
		}
		float clusterReach=250.f;
        if(ability==Paths::Ability::kDeadeye){const auto* order=Party::FindOrders(member.id);const auto kit=PathsLink::KitOf(member.id,order?order->role:Role::kArcher);clusterReach=ArrowArtsFx::Radius(kit.Rank(ability))+70.f*kit.Value(Paths::Perk::kDeadeyeHawkeye);}
        // Count clusters around actual potential impact points, not unrelated distant enemies.
		for (const auto& centre : local) {
			if (ability == Paths::Ability::kDeadeye || ability == Paths::Ability::kBurst || ability == Paths::Ability::kDragonBreath || ability == Paths::Ability::kSpectralWolf) {
				if (centre != chosen) continue;
			}
			int count = 0;
			for (const auto& enemy : local) count += Game::Distance(centre.get(), enemy.get()) <= clusterReach ? 1 : 0;
			c.cluster = std::max(c.cluster, count);
		}
		if (ability == Paths::Ability::kStandFast) {
			const auto* orders = Party::FindOrders(member.id);
			const auto kit = PathsLink::KitOf(member.id, orders ? orders->role : Role::kTank);
			c.criticalAlly = TankActs::CanSave(actor, TankPolicy::kBastionReach + kit.Value(Paths::Perk::kStandFastUnbroken));
		}
		const auto decision = Consider(ability, c);
		if (!decision || !pace.Ready(member.id, ability, c.combatAge, decision.urgent)) return false;
		reasons[member.id] = decision.reason;
		return true;
	}
	void Used(RE::FormID id, RE::Actor* actor, Paths::Ability ability, bool ordered)
	{
		pace.Used(id, ability);
		const auto it = reasons.find(id);
		Log::Info("[Ability] {} uses {}: {}."sv, Game::Name(actor), Paths::Info(ability).name,
			ordered ? "ordered"sv : (it != reasons.end() ? it->second : "situational support"sv));
		reasons.erase(id);
	}
}
