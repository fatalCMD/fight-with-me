#include "FWM/Haunts.h"

#include "FWM/Effects.h"
#include "FWM/Game.h"
#include "FWM/Haunt.h"
#include "FWM/Log.h"
#include "FWM/Posing.h"
#include "FWM/Settings.h"

namespace FWM::Haunts
{
	namespace
	{
		constexpr float kLongest = 8.0f;  // the effect is made to outlast any hold

		struct Part
		{
			RE::NiPointer<RE::NiAVObject>      node;
			std::vector<RE::BSShaderProperty*> shaders;
		};

		// One held, and the parts of the slot that follows them.
		struct Slot
		{
			RE::ActorHandle                    enemy;
			float                              since{ -1.0f };  // below 0: free
			float                              held{ 0.0f };
			Part                               node, mist, glow, ground, ring, flare;
			std::array<Part, Haunt::kWisps>    wisps;
		};

		struct Bite
		{
			std::uint32_t                           id{ 0 };
			bool                                    grand{ false };
			float                                   age{ 0.0f };
			std::size_t                             copy{ 0 };
			RE::NiPointer<RE::BSTempEffectParticle> effect;
			bool                                    found{ false };
			std::array<Slot, Haunt::kSlots>         slots;
		};
		std::vector<Bite> bites;
		std::uint32_t     next = 1;

		bool Find(Bite& a_bite)
		{
			auto* root = Posing::RootOf(a_bite.effect.get());
			if (!root) {
				return false;
			}
			auto find = [&](Part& a_part, const std::string& a_name) {
				a_part.node.reset(root->GetObjectByName(a_name));
				a_part.shaders = Posing::ShadersOf(a_part.node.get());
			};
			for (std::size_t i = 0; i < a_bite.slots.size(); ++i) {
				auto&      slot = a_bite.slots[i];
				const auto name = std::format("Haunt{}", i);
				find(slot.node, name);
				find(slot.mist, name + "Mist");
				find(slot.glow, name + "Glow");
				find(slot.ground, name + "Ground");
				find(slot.ring, name + "Ring");
				find(slot.flare, name + "Flare");
				for (std::size_t w = 0; w < slot.wisps.size(); ++w) {
					find(slot.wisps[w], std::format("{}Wisp{}", name, w));
				}
				if (slot.node.node) {
					slot.node.node->local.scale = 0.001f;  // out of sight until someone is caught
				}
			}
			a_bite.found = a_bite.slots[0].node.node != nullptr;
			return a_bite.found;
		}

		void Local(Part& a_part, const RE::NiPoint3& a_at, float a_scale)
		{
			if (a_part.node) {
				a_part.node->local.translate = a_at;
				a_part.node->local.scale = std::max(a_scale, 0.001f);
			}
		}

		void Pose(Bite& a_bite)
		{
			auto* root = Posing::RootOf(a_bite.effect.get());
			if (!root || !a_bite.found) {
				return;
			}
			const auto&         glow = Haunt::GlowOf(a_bite.grand);
			const auto&         smoke = Haunt::SmokeOf(a_bite.grand);
			for (auto& slot : a_bite.slots) {
				auto enemy = slot.enemy.get();
				if (!slot.node.node) {
					continue;
				}
				if (slot.since < 0.0f || !enemy || !enemy->Is3DLoaded()) {
					slot.node.node->local.scale = 0.001f;
					continue;
				}
				const auto look = Haunt::At(slot.since, slot.held, a_bite.grand);
				const auto    feet = enemy->GetPosition();
				const auto    up = Game::HeadPosition(enemy.get()) - feet;
				const auto    lean = Haunt::Lean(up.x, up.y, up.z);
				RE::NiMatrix3 turn;
				for (int r = 0; r < 3; ++r) {
					for (int c = 0; c < 3; ++c) {
						turn.entry[r][c] = lean[r][c];
					}
				}
				Posing::PlaceInWorld(root, slot.node.node.get(), feet, turn, Haunt::SizeFor(enemy->GetHeight()) * look.scale);
				Posing::PaintBlended(slot.mist.shaders, smoke, look.mist);
				Posing::Paint(slot.glow.shaders, glow, look.glow);
				Posing::PaintBlended(slot.ground.shaders, smoke, look.ground);
				Posing::Paint(slot.ring.shaders, glow, look.ground * 0.8f);
				Posing::Paint(slot.flare.shaders, glow, look.flare);
				for (std::size_t w = 0; w < slot.wisps.size(); ++w) {
					const auto& wisp = look.wisps[w];
					Local(slot.wisps[w], { wisp.x, wisp.y, wisp.z }, wisp.size);
					Posing::Paint(slot.wisps[w].shaders, glow, wisp.alpha);
				}
				// The smoke creeps up them.
				Posing::Drift(slot.mist.shaders, slot.since * Haunt::kRise);
				Posing::Drift(slot.glow.shaders, slot.since * Haunt::kRise * 1.4f);
				Posing::Drift(slot.ground.shaders, slot.since * Haunt::kRise * 0.3f);
			}
			Posing::Refresh(root);
		}

		void Finish(Bite& a_bite)
		{
			if (a_bite.effect) {
				a_bite.effect->lifetime = 0.0f;
			}
		}
	}

	std::uint32_t Begin(RE::Actor* a_near, bool a_grand)
	{
		if (!a_near || !Settings::Get().abilityEffects) {
			return 0;
		}
		Bite bite;
		bite.id = next++;
		bite.grand = a_grand;
		// The mesh's other name if another wolf's catch still stands in its smoke.
		bite.copy = std::ranges::any_of(bites, [](const Bite& a_other) { return a_other.copy == 0; }) ? 1 : 0;
		bite.effect.reset(Effects::Haunt(a_near, bite.copy, a_near->GetPosition(), kLongest));
		if (!bite.effect) {
			return 0;
		}
		bites.push_back(std::move(bite));
		return bites.back().id;
	}

	void Hold(std::uint32_t a_haunt, RE::Actor* a_enemy, float a_seconds)
	{
		const auto bite = std::ranges::find(bites, a_haunt, &Bite::id);
		if (bite == bites.end() || !a_enemy) {
			return;
		}
		for (auto& slot : bite->slots) {
			if (slot.since < 0.0f) {
				slot.enemy = a_enemy->GetHandle();
				slot.since = 0.0f;
				slot.held = std::min(a_seconds, kLongest - Haunt::kLinger);
				return;
			}
		}
	}

	void Tick(float a_dt)
	{
		for (auto& bite : bites) {
			bite.age += a_dt;
			for (auto& slot : bite.slots) {
				if (slot.since >= 0.0f) {
					slot.since += a_dt;
					// The dead are let go at once (Freeze drops them too), and their smoke thins away.
					if (auto enemy = slot.enemy.get(); (!enemy || enemy->IsDead()) && slot.held > slot.since) {
						slot.held = slot.since;
					}
				}
			}
			if (!bite.found) {
				Find(bite);
			}
			Pose(bite);
		}
		std::erase_if(bites, [](Bite& a_bite) {
			const bool gone = !a_bite.effect || !a_bite.effect->cell || !a_bite.effect->cell->IsAttached();
			const bool showing = std::ranges::any_of(a_bite.slots, [](const Slot& a_slot) { return a_slot.since >= 0.0f && a_slot.since < Haunt::Seconds(a_slot.held); });
			if (!gone && (showing || a_bite.age < 0.5f) && Settings::Get().abilityEffects) {
				return false;
			}
			Finish(a_bite);
			return true;
		});
	}

	void Clear()
	{
		for (auto& bite : bites) {
			Finish(bite);
		}
		bites.clear();
	}
}
