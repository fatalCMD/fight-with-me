#include "FWM/PartyBalance.h"
#include "FWM/PartyBalancePolicy.h"
#include "FWM/Enemies.h"
#include "FWM/Game.h"
#include "FWM/Host.h"
#include "FWM/Log.h"
#include "FWM/Party.h"
#include "FWM/Settings.h"

namespace FWM::PartyBalance
{
    namespace
    {
        struct Snapshot { std::vector<RE::FormID> followers; int enemies{}; float share{1}, target{1}; };
        std::atomic<std::shared_ptr<const Snapshot>> state;
        constexpr float reach = 3500.0f;
        bool Fighting(RE::Actor* actor, RE::Actor* player)
        {
            return Game::Usable(actor) && actor->Is3DLoaded() && actor->IsInCombat() &&
                !actor->AsActorState()->IsBleedingOut() && Game::Current(actor, RE::ActorValue::kHealth) > 0 &&
                (actor->GetParentCell() == player->GetParentCell() ||
                 (actor->GetWorldspace() && actor->GetWorldspace() == player->GetWorldspace())) &&
                Game::Distance(actor, player) <= reach;
        }
    }

    void Clear() { state.store(nullptr, std::memory_order_release); }

    void Update(float dt)
    {
        const auto& cfg = Settings::Get();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!Host::Abilities() || !cfg.enabled || !cfg.partyBalance || !player) { Clear(); return; }
        auto next = std::make_shared<Snapshot>();
        for (const auto& member : Party::Members()) {
            auto actor = member.handle.get();
            if (!member.summon && actor && !actor->IsPlayerRef() && !actor->IsCommandedActor() && Fighting(actor.get(), player))
                next->followers.push_back(member.id);
        }
        std::sort(next->followers.begin(), next->followers.end());
        next->followers.erase(std::unique(next->followers.begin(), next->followers.end()), next->followers.end());
        if (next->followers.empty()) { Clear(); return; }
        for (const auto& enemy : Enemies::All()) {
            auto actor = enemy.handle.get();
            if (actor && !Party::IsPartyActor(actor.get()) && Fighting(actor.get(), player)) ++next->enemies;
        }
        next->enemies = std::max(1, next->enemies); // combat groups can lag behind the first attack
        next->target = PartyBalancePolicy::Share(static_cast<int>(next->followers.size()), next->enemies,
            cfg.partyExtraPenalty, cfg.partyAdvantagePenalty, cfg.partyMinimumDamage);
        const auto old = state.load(std::memory_order_acquire);
        next->share = next->followers.size() <= 1 ? 1.0f :
            (old ? PartyBalancePolicy::Ease(old->share, next->target, dt) : next->target);
        if (!old || old->followers != next->followers || old->enemies != next->enemies || old->target != next->target)
            Log::Info("[PartyBalance] {} fighting followers, {} nearby enemies: target {:.0f}% outgoing damage (now {:.0f}%)."sv,
                next->followers.size(), next->enemies, next->target * 100, next->share * 100);
        state.store(std::move(next), std::memory_order_release);
    }

    float Multiplier(RE::Actor* attacker, RE::Actor* target)
    {
        if (!attacker || !target || attacker == target || attacker->IsPlayerRef() ||
            !Settings::Get().enabled || !Settings::Get().partyBalance) return 1.0f;
        const auto snapshot = state.load(std::memory_order_acquire);
        return snapshot && std::binary_search(snapshot->followers.begin(), snapshot->followers.end(), attacker->GetFormID()) ? snapshot->share : 1.0f;
    }

    float Damage(RE::Actor* attacker, RE::Actor* target, float amount)
    {
        if (!target || target->IsDead() || !std::isfinite(amount) || amount <= 0) return 0;
        const float scaled = amount * Multiplier(attacker, target);
        target->AsActorValueOwner()->DamageActorValue(RE::ActorValue::kHealth, scaled);
        return scaled;
    }
}
