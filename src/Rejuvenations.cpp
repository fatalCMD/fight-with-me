#include "FWM/Rejuvenations.h"
#include "FWM/EffectAudio.h"
#include "FWM/Rejuvenation.h"
#include "FWM/Effects.h"
#include "FWM/Log.h"
#include "FWM/Posing.h"
#include "FWM/Settings.h"

namespace FWM::Rejuvenations
{
	namespace {
		struct Part { RE::NiPointer<RE::NiAVObject> node; std::vector<RE::BSShaderProperty*> shaders; };
		struct Circle {
			EffectAudio::ID audio{};
			RE::NiPointer<RE::BSTempEffectParticle> effect;
			std::array<Part, Rejuvenation::kNames.size()> parts;
			std::array<Part, Rejuvenation::kMotes> motes;
			RE::NiPoint3 at;
			float age{}, loading{}, seconds{};
			RE::ActorHandle healer;
			int rank{ 1 };
			std::size_t copy{};
			bool found{};
			Posing::Lamp lamp;
		};
		std::vector<Circle> circles;
		void Finish(Circle& c) { EffectAudio::End(c.audio); Posing::Out(c.lamp); if (c.effect) c.effect->lifetime = 0.0f; }
		bool Find(Circle& c) {
			auto* root = Posing::RootOf(c.effect.get());
			if (!root) return false;
			auto find = [root](Part& part, const char* name) {
				part.node.reset(root->GetObjectByName(name));
				if (!part.node) return false;
				part.shaders = Posing::ShadersOf(part.node.get());
				return true;
			};
			for (std::size_t i = 0; i < c.parts.size(); ++i) if (!find(c.parts[i], Rejuvenation::kNames[i])) return false;
			for (std::size_t i = 0; i < c.motes.size(); ++i) if (!find(c.motes[i], std::format("Mote{:02}", i).c_str())) return false;
			c.found = true;
			Log::Info("[Look] Rejuvenation rank {} seal loaded, {:.0f} radius."sv, c.rank, Rejuvenation::At(1.0f, c.rank).radius);
			return true;
		}
		void Pose(Circle& c) {
			const auto look = Rejuvenation::At(c.age, c.rank, c.seconds);
			const float scale = look.radius / Rejuvenation::kMeshRadius;
			auto* root = Posing::RootOf(c.effect.get());
			for (std::size_t i = 0; i < c.parts.size(); ++i) {
				const auto& f = look.parts[i]; auto& part = c.parts[i];
				RE::NiMatrix3 turn; turn.MakeZRotation(f.turn);
				Posing::PlaceInWorld(root, part.node.get(), c.at + RE::NiPoint3{ 0, 0, 8.0f + f.height }, turn, scale * f.scale);
				Posing::Paint(part.shaders, f.color, f.alpha);
				if ((i >= 2 && i <= 4) || (i >= 6 && i <= 8)) Posing::Drift(part.shaders, -c.age * 0.14f);
				Posing::Refresh(part.node.get());
			}
			const RE::NiMatrix3 still;
			for (std::size_t i = 0; i < c.motes.size(); ++i) {
				const auto& f = look.motes[i]; auto& part = c.motes[i];
				Posing::PlaceInWorld(root, part.node.get(), c.at + RE::NiPoint3{ f.at[0], f.at[1], f.at[2] } * scale, still, f.size * scale);
				Posing::Paint(part.shaders, f.color, f.alpha);
				Posing::Refresh(part.node.get());
			}
			Posing::Shine(c.lamp, root, c.at + RE::NiPoint3{ 0, 0, 50 }, look.lightColor, look.light, look.radius * 1.5f);
		}
	}
	void Begin(RE::Actor* healer, int rank, float seconds) {
		if (!Settings::Get().abilityEffects || !healer || !healer->Is3DLoaded() || !std::isfinite(seconds) || seconds <= 0) return;
		std::size_t copy = 0;
		for (; copy < Rejuvenation::kCopies; ++copy) if (std::ranges::none_of(circles, [copy](const Circle& c) { return c.copy == copy; })) break;
		if (copy == Rejuvenation::kCopies) return;
		Circle c; c.seconds = seconds; c.healer = healer->GetHandle(); c.rank = std::clamp(rank, 1, 3); c.copy = copy; c.at = healer->GetPosition();
		c.effect.reset(Effects::RejuvenationCircle(healer, copy, c.at, c.seconds + 1.5f));
		if (c.effect) { c.audio=EffectAudio::Begin(Soundscape::Kind::Rejuvenation,healer,c.at,c.seconds,c.rank);circles.push_back(std::move(c)); }
	}
	void Tick(float dt) {
		std::erase_if(circles, [dt](Circle& c) {
			const auto owner = c.healer.get();
			if (!owner || owner->IsDead() || owner->IsDisabled() || !Settings::Get().enabled || !Settings::Get().abilityEffects || !c.effect || !c.effect->cell || !c.effect->cell->IsAttached()) { Finish(c); return true; }
			if (!c.found && !Find(c)) {
				c.loading += std::max(dt, 0.0f);
				if (c.loading > 1.0f) { Finish(c); return true; }
				return false;
			}
			c.age += std::max(dt, 0.0f);
			if (c.age >= c.seconds) { Finish(c); return true; }
			Pose(c); return false;
		});
	}
	void Clear() { for (auto& c : circles) Finish(c); circles.clear(); }
}
