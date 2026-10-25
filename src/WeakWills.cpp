#include "FWM/WeakWills.h"
#include "FWM/EffectAudio.h"
#include "FWM/WeakWill.h"
#include "FWM/Effects.h"
#include "FWM/Posing.h"
#include "FWM/Settings.h"

namespace FWM::WeakWills
{
	namespace
	{
		struct Part { RE::NiPointer<RE::NiAVObject> node; std::vector<RE::BSShaderProperty*> shaders; };
		struct Helm
		{
			EffectAudio::ID audio{};
			RE::ActorHandle actor;
			RE::NiPointer<RE::BSTempEffectParticle> effect;
			RE::NiPointer<RE::NiAVObject> model, head;
			RE::NiMatrix3 inverse, facing;
			std::array<Part, WeakWill::kNames.size()> parts;
			Posing::Lamp lamp;
			float age{}, duration{}, loading{}, retry{}, bodyScale{ 1 };
			int rank{ 1 };
			std::size_t copy{ WeakWill::kCopies };
			bool found{};
		};
		std::vector<Helm> helms;
		void Drop(Helm& helm)
		{
			EffectAudio::End(helm.audio);
			Posing::Out(helm.lamp);
			if (helm.effect) helm.effect->lifetime = 0;
			helm.effect.reset();
			for (auto& part : helm.parts) { part.node.reset(); part.shaders.clear(); }
			helm.copy = WeakWill::kCopies; helm.found = false; helm.loading = 0;
		}
		void BindHead(Helm& helm, RE::Actor* actor)
		{
			auto* model = actor->Get3D();
			if (helm.model.get() == model) return;
			helm.model.reset(model);
			helm.head.reset(model ? model->GetObjectByName("NPC Head [Head]") : nullptr);
			helm.facing.MakeZRotation(actor->GetAngleZ());
			if (helm.head) helm.inverse = helm.head->world.rotate.Transpose();
		}
		bool Find(Helm& helm)
		{
			auto* root = Posing::RootOf(helm.effect.get());
			if (!root) return false;
			for (std::size_t i = 0; i < helm.parts.size(); ++i) {
				helm.parts[i].node.reset(root->GetObjectByName(WeakWill::kNames[i]));
				if (!helm.parts[i].node) return false;
			}
			for (auto& part : helm.parts) part.shaders = Posing::ShadersOf(part.node.get());
			helm.found = true;
			return true;
		}
		void Spawn(Helm& helm, RE::Actor* actor)
		{
			if (helm.effect || helm.retry > 0) return;
			for (std::size_t copy = 0; copy < WeakWill::kCopies; ++copy) {
				if (std::ranges::any_of(helms, [copy](const Helm& other) { return other.effect && other.copy == copy; })) continue;
				helm.effect.reset(Effects::WeakWillHelmet(actor, copy, actor->GetPosition(), helm.duration - helm.age + WeakWill::kFade + 1.5f, helm.rank >= 3));
				if (!helm.effect) continue;
				helm.copy = copy;
				const float height = actor->GetHeight();
				helm.bodyScale = std::isfinite(height) ? std::clamp(height / 128.0f, .7f, 1.4f) : 1;
				return;
			}
			helm.retry = 1;
		}
		void Pose(Helm& helm, RE::Actor* actor)
		{
			auto* root = Posing::RootOf(helm.effect.get());
			if (!root || !helm.found) return;
			BindHead(helm, actor);
			const auto point = helm.head ? helm.head->world.translate : actor->GetPosition() + RE::NiPoint3{ 0, 0, 112 * helm.bodyScale };
			RE::NiMatrix3 facing;
			if (helm.head) facing = helm.head->world.rotate * helm.inverse * helm.facing;
			else facing.MakeZRotation(actor->GetAngleZ());
			const auto frame = WeakWill::At(helm.age, helm.duration, helm.rank);
			for (std::size_t i = 0; i < helm.parts.size(); ++i) {
				auto& part = helm.parts[i]; const auto& pose = frame.parts[i];
				RE::NiMatrix3 yaw, tilt;
				yaw.MakeZRotation(-pose.turn); tilt.MakeXRotation(-pose.tilt);
				const auto at = point + facing * RE::NiPoint3{ 0, pose.forward * helm.bodyScale, pose.height * helm.bodyScale };
				Posing::PlaceInWorld(root, part.node.get(), at, facing * yaw * tilt, pose.scale * helm.bodyScale);
				// Both helmet layers dissolve by opacity. Palette remapping can ignore an RGB fade.
				if (i <= 1) Posing::PaintBlended(part.shaders, pose.color, pose.alpha);
				else Posing::Paint(part.shaders, pose.color, pose.alpha);
				part.node->SetAppCulled(pose.alpha <= 0.0f);
				if (i == 2) Posing::Drift(part.shaders, -.13f * helm.age);
			}
			const std::array<float, 3> light = helm.rank >= 3 ? std::array<float, 3>{ .57f, .09f, .90f } : std::array<float, 3>{ .40f, .65f, .92f };
			Posing::Shine(helm.lamp, root, point, light, frame.light, 135 * helm.bodyScale);
			Posing::Refresh(root);
		}
	}
	void Call(RE::Actor* actor, float seconds, int rank)
	{
		if (!actor || actor->IsDead() || actor->IsDisabled() || !std::isfinite(seconds) || seconds <= 0) return;
		const auto handle = actor->GetHandle();
		for (auto& helm : helms) if (helm.actor == handle) {
			if (helm.rank < 3 && rank >= 3) Drop(helm);
			helm.rank = std::clamp(std::max(helm.rank, rank), 1, 3);
			helm.duration = std::max(helm.duration, helm.age + seconds);
			if (helm.effect) helm.effect->lifetime = helm.effect->age + (helm.duration - helm.age) + WeakWill::kFade + 1.5f;
			return;
		}
		Helm helm; helm.actor = handle; helm.duration = seconds; helm.rank = std::clamp(rank, 1, 3);
		helm.audio=EffectAudio::Begin(Soundscape::Kind::WeakWill,actor,actor->GetPosition(),seconds,rank,true);
		helms.push_back(std::move(helm));
	}
	void End(RE::FormID actor)
	{
		for (auto& helm : helms) if (auto live = helm.actor.get(); live && live->GetFormID() == actor)
				{ helm.duration = std::min(helm.duration, helm.age); EffectAudio::End(helm.audio); }
	}
	void Tick(float dt)
	{
		dt = std::isfinite(dt) ? std::max(dt, 0.0f) : 0;
		std::erase_if(helms, [dt](Helm& helm) {
			helm.age += dt; helm.retry = std::max(helm.retry - dt, 0.0f);
			auto actor = helm.actor.get();
			if (!Settings::Get().enabled || !actor || actor->IsDead() || actor->IsDisabled() || helm.age >= helm.duration + WeakWill::kFade) { Drop(helm); return true; }
			return false;
		});
		for (auto& helm : helms) {
			auto actor = helm.actor.get(); if (!actor) continue;
			if (!Settings::Get().abilityEffects || !actor->Is3DLoaded()) { Drop(helm); continue; }
			if (helm.effect && (!helm.effect->cell || !helm.effect->cell->IsAttached() || helm.effect->cell != actor->GetParentCell() || helm.effect->age >= helm.effect->lifetime)) Drop(helm);
			Spawn(helm, actor.get()); if (!helm.effect) continue;
			if (!helm.found && !Find(helm)) {
				helm.loading += dt;
				if (helm.loading > 1) { Drop(helm); helm.retry = 1; }
				continue;
			}
			Pose(helm, actor.get());
		}
	}
	void Clear() { for (auto& helm : helms) Drop(helm); helms.clear(); }
}
