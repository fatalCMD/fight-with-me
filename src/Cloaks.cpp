#include "FWM/Cloaks.h"
#include "FWM/EffectAudio.h"

#include "FWM/Effects.h"
#include "FWM/Log.h"
#include "FWM/Posing.h"
#include "FWM/Settings.h"
#include "FWM/WaterCloak.h"

namespace FWM::Cloaks
{
	namespace
	{
		struct Part
		{
			RE::NiPointer<RE::NiAVObject>      node;
			std::vector<RE::BSShaderProperty*> shaders;
		};

		// Someone a pulse reached, and how long ago.
		struct Washed
		{
			RE::ActorHandle ally;
			float           since{ -1.0f };  // below 0: free
		};

		struct Worn
		{
			EffectAudio::ID audio{};
			std::uint32_t                           id{ 0 };
			RE::ActorHandle                         healer;
			float                                   seconds{ 0.0f };
			float                                   age{ 0.0f };
			bool                                    grand{ false };  // the third rank's: gold, larger, glowing over the head
			std::size_t                             copy{ 0 };
			RE::NiPointer<RE::BSTempEffectParticle> effect;
			std::array<Washed, WaterCloak::kWashes> washed{};
			Posing::Lamp                            lamp;

			// Its parts, found once the mesh has loaded.
			bool                                         found{ false };
			Part                                         cloak, veil, inner, streams, crown, gleam, pool;
			std::array<Part, WaterCloak::kRipples>       ripples;
			std::array<Part, WaterCloak::kDrops>         drops;
			std::array<Part, WaterCloak::kWashes>        washes, bands, rings;
		};
		std::vector<Worn> worn;
		std::uint32_t     next = 1;

		RE::NiMatrix3 AboutUp(float a_angle)
		{
			RE::NiMatrix3 turn;
			turn.entry[0][0] = std::cos(a_angle);
			turn.entry[0][1] = -std::sin(a_angle);
			turn.entry[1][0] = std::sin(a_angle);
			turn.entry[1][1] = std::cos(a_angle);
			return turn;
		}

		float SizeOf(RE::Actor* a_actor) { return WaterCloak::SizeFor(a_actor->GetHeight()); }

		bool Find(Worn& a_worn)
		{
			auto* root = Posing::RootOf(a_worn.effect.get());
			if (!root) {
				return false;
			}
			auto find = [&](Part& a_part, const std::string& a_name) {
				a_part.node.reset(root->GetObjectByName(a_name));
				a_part.shaders = Posing::ShadersOf(a_part.node.get());
			};
			find(a_worn.cloak, "Cloak");
			if (!a_worn.cloak.node) {
				return false;
			}
			find(a_worn.veil, "Veil");
			find(a_worn.inner, "Inner");
			find(a_worn.streams, "Streams");
			find(a_worn.crown, "Crown");
			find(a_worn.gleam, "Gleam");
			find(a_worn.pool, "Pool");
			for (std::size_t i = 0; i < a_worn.ripples.size(); ++i) {
				find(a_worn.ripples[i], std::format("Ripple{}", i));
			}
			for (std::size_t i = 0; i < a_worn.drops.size(); ++i) {
				find(a_worn.drops[i], std::format("Drop{:02}", i));
			}
			for (std::size_t i = 0; i < a_worn.washes.size(); ++i) {
				find(a_worn.washes[i], std::format("Wash{:02}", i));
				find(a_worn.bands[i], std::format("Wash{:02}Band", i));
				find(a_worn.rings[i], std::format("Wash{:02}Ring", i));
				if (a_worn.washes[i].node) {
					a_worn.washes[i].node->local.scale = 0.001f;  // out of sight until a pulse takes it
				}
			}
			a_worn.found = true;
			Log::Info("[Paths] Renewal's cloak of water shows {:.2f} s in."sv, a_worn.age);
			return true;
		}

		void Local(Part& a_part, const RE::NiMatrix3& a_turn, const RE::NiPoint3& a_at, float a_scale, const WaterCloak::Color& a_color, float a_alpha)
		{
			if (a_part.node) {
				a_part.node->local.rotate = a_turn;
				a_part.node->local.translate = a_at;
				a_part.node->local.scale = std::max(a_scale, 0.001f);
				Posing::Paint(a_part.shaders, a_color, a_alpha);
			}
		}

		void Pose(Worn& a_worn, RE::Actor* a_healer)
		{
			const auto  look = WaterCloak::At(a_worn.age, a_worn.seconds, a_worn.grand);
			const auto& water = WaterCloak::WaterOf(a_worn.grand);
			const auto& foam = WaterCloak::FoamOf(a_worn.grand);
			auto*       root = Posing::RootOf(a_worn.effect.get());
			Posing::Shine(a_worn.lamp, root, a_healer->GetPosition() + RE::NiPoint3{ 0.0f, 0.0f, 90.0f }, water, look.light * WaterCloak::kLightFade, WaterCloak::kLightReach);
			if (!root || !a_worn.found) {
				return;
			}
			const RE::NiMatrix3 still;
			const RE::NiPoint3  middle;
			const float grand = a_worn.grand ? WaterCloak::kGrandSize : 1.0f;
			Posing::PlaceInWorld(root, a_worn.cloak.node.get(), a_healer->GetPosition(), still, SizeOf(a_healer) * grand * look.rise);
			Local(a_worn.veil, still, middle, 1.0f, water, look.veil);
			Local(a_worn.inner, still, middle, 1.0f, foam, look.inner);
			Local(a_worn.streams, AboutUp(look.twist), middle, 1.0f, water, look.streams);
			Local(a_worn.crown, AboutUp(look.crownTurn), middle, 1.0f, foam, look.crown);
			Local(a_worn.gleam, still, middle, std::max(look.gleamScale, 0.001f), foam, look.gleam);
			Local(a_worn.pool, still, middle, look.poolScale, water, look.pool);
			for (std::size_t i = 0; i < a_worn.ripples.size(); ++i) {
				Local(a_worn.ripples[i], still, middle, look.ripples[i].scale, foam, look.ripples[i].alpha);
			}
			for (std::size_t i = 0; i < a_worn.drops.size(); ++i) {
				const auto& drop = look.drops[i];
				Local(a_worn.drops[i], still, { drop.x, drop.y, drop.z }, drop.size, foam, drop.alpha);
			}
			// The water running down: fast over the sheath and the swirl, slower through them, quickest in the streams.
			Posing::Drift(a_worn.veil.shaders, -a_worn.age * WaterCloak::kFlow);
			Posing::Drift(a_worn.crown.shaders, -a_worn.age * WaterCloak::kFlow);
			Posing::Drift(a_worn.inner.shaders, -a_worn.age * WaterCloak::kFlow * 0.6f);
			Posing::Drift(a_worn.streams.shaders, -a_worn.age * WaterCloak::kStreamFlow);
			Posing::Drift(a_worn.pool.shaders, -a_worn.age * 0.15f);

			// Each one washed: a band of it running down them, the ripple at their feet.
			for (std::size_t i = 0; i < a_worn.washed.size(); ++i) {
				auto& washed = a_worn.washed[i];
				auto  ally = washed.ally.get();
				auto& node = a_worn.washes[i].node;
				if (!node) {
					continue;
				}
				if (washed.since < 0.0f || !ally || !ally->Is3DLoaded()) {
					node->local.scale = 0.001f;
					continue;
				}
				const auto wash = WaterCloak::WashAt(washed.since);
				Posing::PlaceInWorld(root, node.get(), ally->GetPosition(), still, SizeOf(ally.get()));
				Local(a_worn.bands[i], still, { 0.0f, 0.0f, wash.z }, wash.scale, water, wash.alpha);
				Local(a_worn.rings[i], still, middle, wash.ringScale, foam, wash.ring);
				Posing::Drift(a_worn.bands[i].shaders, -washed.since * WaterCloak::kFlow * 2.0f);
			}
			Posing::Refresh(root);
		}

		void Finish(Worn& a_worn)
		{
			EffectAudio::End(a_worn.audio);
			Posing::Out(a_worn.lamp);
			if (a_worn.effect) {
				a_worn.effect->lifetime = 0.0f;
			}
		}
	}

	std::uint32_t Wear(RE::Actor* a_healer, float a_seconds, bool a_grand)
	{
		if (!a_healer || a_seconds <= 0.0f || !Settings::Get().abilityEffects) {
			return 0;
		}
		Worn cloak;
		cloak.id = next++;
		cloak.healer = a_healer->GetHandle();
		cloak.seconds = a_seconds;
		cloak.grand = a_grand;
		// The mesh's other name if another healer's cloak still plays.
		cloak.copy = std::ranges::any_of(worn, [](const Worn& a_other) { return a_other.copy == 0; }) ? 1 : 0;
		cloak.effect.reset(Effects::Cloak(a_healer, cloak.copy, a_healer->GetPosition(), a_seconds + WaterCloak::kWashSeconds + 0.3f));
		if (!cloak.effect) {
			return 0;
		}
		cloak.audio=EffectAudio::Begin(Soundscape::Kind::Renewal,a_healer,a_healer->GetPosition(),a_seconds,a_grand?3:1,true);
		worn.push_back(std::move(cloak));
		return worn.back().id;
	}

	void Wash(std::uint32_t a_cloak, RE::Actor* a_ally)
	{
		const auto cloak = std::ranges::find(worn, a_cloak, &Worn::id);
		if (cloak == worn.end() || !a_ally) {
			return;
		}
		const auto ally = a_ally->GetHandle();
		Washed*    free = nullptr;
		for (auto& washed : cloak->washed) {
			if (washed.since >= 0.0f && washed.since < WaterCloak::kWashRun && washed.ally == ally) {
				return;  // still running down them
			}
			if (!free && (washed.since < 0.0f || washed.since >= WaterCloak::kWashSeconds)) {
				free = &washed;
			}
		}
		if (free) {
			*free = { ally, 0.0f };
		}
	}

	void End(std::uint32_t a_cloak)
	{
		if (const auto cloak = std::ranges::find(worn, a_cloak, &Worn::id); cloak != worn.end()) {
			EffectAudio::End(cloak->audio);
			cloak->seconds = std::min(cloak->seconds, cloak->age + WaterCloak::kGoOut);
		}
	}

	void EndAll()
	{
		for (auto& cloak : worn) {
			cloak.seconds = std::min(cloak.seconds, cloak.age + WaterCloak::kGoOut);
			EffectAudio::End(cloak.audio);
		}
	}

	void Tick(float a_dt)
	{
		for (auto& cloak : worn) {
			cloak.age += a_dt;
			for (auto& washed : cloak.washed) {
				if (washed.since >= 0.0f) {
					washed.since += a_dt;
				}
			}
			auto healer = cloak.healer.get();
			if (!healer || healer->IsDead() || !healer->Is3DLoaded()) {
				cloak.seconds = std::min(cloak.seconds, cloak.age);
				continue;
			}
			if (!cloak.found) {
				Find(cloak);
			}
			Pose(cloak, healer.get());
		}
		std::erase_if(worn, [](Worn& a_worn) {
			const bool gone = !a_worn.effect || !a_worn.effect->cell || !a_worn.effect->cell->IsAttached();
			if (!gone && a_worn.age < a_worn.seconds + WaterCloak::kWashSeconds && Settings::Get().abilityEffects) {
				return false;
			}
			Finish(a_worn);
			return true;
		});
	}

	void Clear()
	{
		for (auto& cloak : worn) {
			Finish(cloak);
		}
		worn.clear();
	}
}
