#include "FWM/PartyBalance.h"
#include "FWM/SpellArrows.h"
#include "FWM/ArrowArts.h"
#include "FWM/ArcherAmmo.h"
#include "FWM/Archery.h"
#include "FWM/Abilities.h"
#include "FWM/Effects.h"
#include "FWM/Forms.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/Party.h"
#include "FWM/Posing.h"
#include "FWM/Settings.h"

#include <mutex>
#include <random>
#include <unordered_map>

namespace FWM::SpellArrows
{
    namespace
    {
        struct Entry
        {
            RE::ProjectileHandle arrow;
            RE::ActorHandle archer;
            Archery::Shot shot;
            float born{};
        };
        struct Hit
        {
            RE::ActorHandle archer, target;
            int element;
        };
        std::mutex lock;
        std::unordered_map<RE::FormID, Entry> shots;
        std::vector<Hit> hits;
        std::mt19937 random{ std::random_device{}() };
        std::atomic<float> clock{ 0 };
        thread_local RE::FormID impacting = 0;

        // This is called before native flight and before native impact processing, so even
        // point-blank shots roll once. Only cached, thread-safe perk data is read here.
        void Notice(RE::ArrowProjectile* arrow)
        {
            ArrowArts::OnFlight(arrow);
            auto& data = arrow->GetProjectileRuntimeData();
            if (data.livingTime > 12 || data.flags.any(RE::Projectile::Flags::kProcessedImpacts, RE::Projectile::Flags::kDestroyed)) return;
            auto shooter = data.shooter.get();
            auto* actor = shooter ? shooter->As<RE::Actor>() : nullptr;
            if (!actor || actor->IsPlayerRef()) return;
            const float chance = ArcherAmmo::ElementalChance(actor->GetFormID());
            if (chance <= 0) return;
            const std::scoped_lock guard(lock);
            const auto id = arrow->GetFormID();
            if (shots.contains(id) || shots.size() >= 1024) return;
            const bool imbued = Archery::Imbued(chance, std::uniform_real_distribution<float>(0, 100)(random));
            const int element = imbued ? std::uniform_int_distribution<int>(0, 2)(random) : -1;
            shots.emplace(id, Entry{ RE::ProjectileHandle{ arrow }, actor->GetHandle(), { element, false }, clock.load() });
        }

        struct FlightHook
        {
            static void Thunk(RE::ArrowProjectile* arrow, float delta)
            {
                Notice(arrow);
                original(arrow, delta);
            }
            static inline REL::Relocation<decltype(Thunk)> original;
        };
        struct ImpactHook
        {
            static bool Thunk(RE::ArrowProjectile* arrow)
            {
                Notice(arrow);
                // TESHitEvent is dispatched synchronously while native impacts are processed.
                // Keep the actual projectile identity, never guess by shooter/arrow base form.
                const auto previous = impacting;
                impacting = arrow->GetFormID();
                ArrowArts::OnImpact(arrow);
                const bool result = original(arrow);
                impacting = previous;
                return result;
            }
            static inline REL::Relocation<decltype(Thunk)> original;
        };
        struct HitSink final : RE::BSTEventSink<RE::TESHitEvent>
        {
            RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* event, RE::BSTEventSource<RE::TESHitEvent>*) override
            {
                ArrowArts::OnHit(impacting,event);
                if (!impacting || !event || !event->cause || !event->target || !event->projectile) return RE::BSEventNotifyControl::kContinue;
                auto* target = event->target->As<RE::Actor>();
                if (!target) return RE::BSEventNotifyControl::kContinue;
                const std::scoped_lock guard(lock);
                auto found = shots.find(impacting);
                if (found == shots.end()) return RE::BSEventNotifyControl::kContinue;
                auto archer = found->second.archer.get();
                if (!archer || archer->GetFormID() != event->cause->GetFormID()) return RE::BSEventNotifyControl::kContinue;
                const int element = found->second.shot.Resolve(!event->flags.any(RE::TESHitEvent::Flag::kHitBlocked, RE::TESHitEvent::Flag::kBashAttack));
                if (element >= 0 && hits.size() < 256) hits.push_back({ found->second.archer, target->GetHandle(), element });
                return RE::BSEventNotifyControl::kContinue;
            }
        } hitSink;

        struct Visual
        {
            RE::NiPointer<RE::BSTempEffectParticle> effect;
            RE::NiPointer<RE::NiAVObject> trail;
            std::vector<RE::BSShaderProperty*> core, veil;
            std::array<RE::NiPointer<RE::NiAVObject>, 12> motes;
            std::array<std::vector<RE::BSShaderProperty*>, 12> moteShaders;
            RE::NiPoint3 at;
            RE::NiMatrix3 turn;
            float age = 0, fade = -1;
            int element = 0;
        };
        std::unordered_map<RE::FormID, Visual> visuals;
        constexpr std::array<std::array<float, 3>, 3> colors{ {
            { 1.0f, 0.26f, 0.035f }, { 0.32f, 0.72f, 1.0f }, { 0.42f, 0.40f, 1.0f }
        } };

        RE::NiMatrix3 Along(RE::NiPoint3 forward)
        {
            RE::NiMatrix3 result;
            const float length = forward.Length();
            if (length < 1) return result;
            forward /= length;
            const RE::NiPoint3 up = std::abs(forward.z) > 0.95f ? RE::NiPoint3{ 1, 0, 0 } : RE::NiPoint3{ 0, 0, 1 };
            auto right = forward.Cross(up);
            right /= right.Length();
            const auto vertical = right.Cross(forward);
            for (int row = 0; row < 3; ++row) {
                result.entry[row][0] = right[row];
                result.entry[row][1] = forward[row];
                result.entry[row][2] = vertical[row];
            }
            return result;
        }

        void Draw(Visual& visual, float dt)
        {
            visual.age += dt;
            if (visual.fade >= 0) visual.fade += dt;
            auto* root = Posing::RootOf(visual.effect.get());
            if (!root) return;
            if (!visual.trail) {
                visual.trail.reset(root->GetObjectByName("Trail"));
                visual.core = Posing::ShadersOf(root->GetObjectByName("Filaments"));
                visual.veil = Posing::ShadersOf(root->GetObjectByName("Veil"));
                for (std::size_t i = 0; i < 12; ++i) {
                    visual.motes[i].reset(root->GetObjectByName(std::format("Mote{}", i)));
                    visual.moteShaders[i] = Posing::ShadersOf(visual.motes[i].get());
                }
            }
            const auto index = static_cast<std::size_t>(visual.element);
            const float fade = visual.fade < 0 ? 1.0f : std::max(0.0f, 1.0f - visual.fade / 0.18f);
            const float flicker = visual.element == 2 ? (0.45f + 0.55f * std::abs(std::sin(visual.age * 47))) : (0.82f + 0.18f * std::sin(visual.age * 19));
            Posing::Paint(visual.core, colors[index], fade * flicker);
            Posing::Paint(visual.veil, colors[index], fade * (visual.element == 1 ? 0.48f : 0.28f));
            Posing::Drift(visual.core, -visual.age * (visual.element == 0 ? 3.2f : 1.5f));
            Posing::Drift(visual.veil, -visual.age * 0.8f);
            for (std::size_t i = 0; i < 12; ++i) {
                if (!visual.motes[i]) continue;
                const float f = std::fmod(visual.age * (visual.element == 2 ? 3.0f : 1.6f) + static_cast<float>(i) / 12, 1.0f);
                const float angle = static_cast<float>(i) * 2.4f + visual.age * 2;
                const float radius = 2 + f * 7;
                visual.motes[i]->local.translate = { std::cos(angle) * radius, 15 - f * 90, std::sin(angle) * radius };
                visual.motes[i]->local.scale = 0.5f + f;
                Posing::Paint(visual.moteShaders[i], colors[index], fade * std::sin(f * 3.14159265f) * 0.6f);
            }
            Posing::PlaceInWorld(root, visual.trail.get(), visual.at, visual.turn, 1);
            Posing::Refresh(root);
        }

        void Apply(const Hit& hit)
        {
            auto actor = hit.archer.get(), target = hit.target.get();
            if (!actor || !Game::Usable(target.get()) || target->IsPlayerRef() || Party::Find(target->GetFormID()) ||
                !ArcherAmmo::Unlimited(actor.get()) || !actor->IsHostileToActor(target.get())) return;
            const auto element = static_cast<std::size_t>(hit.element);
            constexpr std::array resists{ RE::ActorValue::kResistFire, RE::ActorValue::kResistFrost, RE::ActorValue::kResistShock };
            constexpr std::array names{ "fire", "frost", "lightning" };
            auto* values = target->AsActorValueOwner();
            const float damage = Archery::ElementalDamage(values->GetActorValue(RE::ActorValue::kResistMagic), values->GetActorValue(resists[element]));
            Effects::Struck(target.get(), element, 0.8f);
            if (Settings::Get().abilityEffects) {
                auto* spell = Forms::Get().bursts[element][0];
                auto* effect = spell ? spell->GetCostliestEffectItem() : nullptr;
                if (effect && effect->baseEffect) for (const auto& sound : effect->baseEffect->effectSounds) {
                    if (sound.id == RE::MagicSystem::SoundID::kHit && sound.sound) { Abilities::Play(sound.sound, target.get()); break; }
                }
            }
            const float dealt = PartyBalance::Damage(actor.get(), target.get(), damage);
            Log::Info("[Paths] {}'s Spell-Touched {} arrow hits {} for {:.1f} after resistance."sv, actor->GetName(), names[element], target->GetName(), dealt);
        }
    }

    void Clear()
    {
        for (auto& [id, visual] : visuals) if (visual.effect) visual.effect->lifetime = 0;
        visuals.clear();
        const std::scoped_lock guard(lock);
        shots.clear();
        hits.clear();
    }

    void Tick(float dt)
    {
        const float now = clock.load() + std::max(dt, 0.0f);
        clock.store(now);
        std::vector<Hit> pending;
        std::vector<std::pair<RE::FormID, Entry>> flying;
        {
            const std::scoped_lock guard(lock);
            pending.swap(hits);
            for (const auto& shot : shots) flying.push_back(shot);
        }
        for (const auto& hit : pending) Apply(hit);
        std::vector<RE::FormID> finished;
        for (const auto& [id, entry] : flying) {
            auto arrow = entry.arrow.get();
            bool ended = !arrow;
            if (arrow) {
                auto& data = arrow->GetProjectileRuntimeData();
                ended = data.livingTime >= 12 || data.flags.any(RE::Projectile::Flags::kProcessedImpacts, RE::Projectile::Flags::kDestroyed) || !data.impacts.empty();
            }
            if (entry.shot.element >= 0 && !ended && Settings::Get().abilityEffects && !visuals.contains(id)) {
                Visual visual;
                visual.element = entry.shot.element;
                visual.effect.reset(Effects::ArrowTrail(arrow->GetParentCell(), arrow->GetPosition(), static_cast<std::size_t>(visual.element)));
                if (visual.effect) visuals.emplace(id, std::move(visual));
            }
            if (auto found = visuals.find(id); found != visuals.end()) {
                if (arrow) {
                    found->second.at = arrow->GetPosition();
                    found->second.turn = Along(arrow->GetProjectileRuntimeData().velocity);
                }
                if ((ended || !Settings::Get().abilityEffects) && found->second.fade < 0) found->second.fade = 0;
            }
            // Keep the decision through impact dispatch, including blocked/missed shots.
            // A collision can be recorded before the engine sends its hit event.
            if (!arrow || now - entry.born > 15) finished.push_back(id);
        }
        {
            const std::scoped_lock guard(lock);
            for (const auto id : finished) shots.erase(id);
        }
        for (auto& [id, visual] : visuals) Draw(visual, std::max(dt, 0.0f));
        std::erase_if(visuals, [&](const auto& item) {
            const auto& visual = item.second;
            const bool gone = !visual.effect || !visual.effect->cell || !visual.effect->cell->IsAttached() || visual.fade > 0.18f;
            if (gone && visual.effect) visual.effect->lifetime = 0;
            return gone;
        });
    }

    void Install()
    {
        REL::Relocation<std::uintptr_t> table{ RE::VTABLE_ArrowProjectile[0] };
        FlightHook::original = table.write_vfunc(REL::Module::IsVR() ? 0xAC : 0xAB, FlightHook::Thunk);
        ImpactHook::original = table.write_vfunc(REL::Module::IsVR() ? 0xAD : 0xAC, ImpactHook::Thunk);
        if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) events->AddEventSink(&hitSink);
    }
}
