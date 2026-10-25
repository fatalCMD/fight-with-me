#include "FWM/ArcherAmmo.h"
#include "FWM/SpellArrows.h"
#include "FWM/Abilities.h"
#include "FWM/Effects.h"
#include "FWM/Forms.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/Party.h"
#include "FWM/PathsLink.h"
#include "FWM/Settings.h"

#include <mutex>
#include <unordered_map>

namespace FWM::ArcherAmmo
{
    namespace
    {
        // Publish on the main thread; the engine's shot hook never reads the party or Paths API.
        std::mutex lock;
        std::unordered_map<RE::FormID, float> archers;
        float refreshIn = 0;
        struct Shots
        {
            static std::uint32_t Thunk(RE::Actor* actor, std::uint32_t count)
            {
                if (Unlimited(actor) && actor->GetCurrentAmmo()) return count;
                return original(actor, count);
            }
            static inline REL::Relocation<decltype(Thunk)> original;
        };

        void Seed(Party::Member& member, RE::Actor* actor, int rank)
        {
            auto* current = actor->GetCurrentAmmo();
            if (current && (rank <= 0 || current->IsBolt())) return;
            auto* supplied = rank > 0 ? Forms::Get().pathArrows[static_cast<std::size_t>(std::clamp(rank, 1, 3) - 1)] : Forms::Get().ironArrow;
            if (!supplied) return;
            bool bow = false;
            bool haveSupplied = false;
            RE::TESAmmo* best = current;
            for (const auto& [object, entry] : actor->GetInventory([](RE::TESBoundObject& item) { return item.IsWeapon() || item.IsAmmo(); })) {
                if (!object || entry.first <= 0) continue;
                if (object == supplied) haveSupplied = true;
                if (auto* ammo = object->As<RE::TESAmmo>(); ammo && !ammo->IsBolt()) {
                    if (rank <= 0) return;
                    if (!best || ammo->GetRuntimeData().data.damage > best->GetRuntimeData().data.damage) best = ammo;
                }
                if (auto* weapon = object->As<RE::TESObjectWEAP>(); weapon && weapon->IsBow()) bow = true;
            }
            if (!bow) return;
            auto* chosen = best && best->GetRuntimeData().data.damage >= supplied->GetRuntimeData().data.damage ? best : supplied;
            if (chosen == supplied && !haveSupplied) {
                actor->AddObjectToContainer(supplied, nullptr, 1, nullptr);
                Log::Info("[Paths] {} receives {} for their unlimited quiver."sv, member.name, supplied->GetName());
                member.arsenalKnown = false;
                member.arsenalIn = 0;
            }
            if (chosen != current) {
                if (auto* equip = RE::ActorEquipManager::GetSingleton()) equip->EquipObject(actor, chosen);
            }
        }
    }

    bool Unlimited(RE::Actor* actor)
    {
        if (!actor || actor->IsPlayerRef() || actor->IsDead()) return false;
        const std::scoped_lock guard(lock);
        return archers.contains(actor->GetFormID());
    }

    float ElementalChance(RE::FormID actor)
    {
        const std::scoped_lock guard(lock);
        const auto found = archers.find(actor);
        return found == archers.end() ? 0.0f : found->second;
    }

    void Clear()
    {
        SpellArrows::Clear();
        const std::scoped_lock guard(lock);
        archers.clear();
        refreshIn = 0;
    }

    void Refresh(float dt)
    {
        if (!Settings::Get().enabled || !PathsLink::Active()) { Clear(); return; }
        SpellArrows::Tick(dt);
        refreshIn -= dt;
        if (refreshIn > 0) return;
        refreshIn = 0.5f;
        std::unordered_map<RE::FormID, float> next;
        for (auto& member : Party::Members()) {
            const auto* orders = Party::FindOrders(member.id);
            if (!member.CanTakeRole() || !orders || orders->role != Role::kArcher) continue;
            auto actor = member.handle.get();
            const auto kit = PathsLink::KitOf(member.id, orders->role);
            if (!Game::Usable(actor.get()) || actor->IsPlayerRef() || !kit.tree) continue;
            next.emplace(member.id, kit.Value(Paths::Perk::kSpellTouched));
            Seed(member, actor.get(), kit.Rank(Paths::Perk::kFineArrows));
        }
        const std::scoped_lock guard(lock);
        archers.swap(next);
    }

    void Install()
    {
        static bool installed = false;
        if (installed) return;
        installed = true;
        REL::Relocation<std::uintptr_t> npc{ RE::VTABLE_Character[0] };
        Shots::original = npc.write_vfunc(REL::Module::IsVR() ? 0xD4 : 0xD2, Shots::Thunk);
        SpellArrows::Install();
    }
}
