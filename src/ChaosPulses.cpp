#include "FWM/ChaosPulses.h"
#include "FWM/EffectAudio.h"

#include "FWM/ChaosPulse.h"
#include "FWM/Effects.h"
#include "FWM/Posing.h"
#include "FWM/Settings.h"

namespace FWM::ChaosPulses
{
	namespace
	{
		struct Part
		{
			RE::NiPointer<RE::NiAVObject> node;
			std::vector<RE::BSShaderProperty*> shaders;
		};
		struct Pulse
		{
			EffectAudio::ID audio{};
			RE::NiPointer<RE::BSTempEffectParticle> effect;
			std::array<Part, ChaosPulse::kNames.size()> parts;
			Part spirit;
			std::array<RE::NiPointer<RE::NiAVObject>, ChaosPulse::kJoints.size()> joints;
			RE::NiPoint3 feet;
			float scale{}, age{}, loading{}, heading{}, seconds{};
			std::size_t copy{};
			bool found{}, grand{};
			Posing::Lamp lamp;
		};
		std::vector<Pulse> pulses;
		struct Target
		{
			RE::ActorHandle enemy;
			RE::NiPointer<RE::BSTempEffectParticle> effect;
			std::array<Part, ChaosPulse::kBindingNames.size()> parts;
			float age{}, left{}, loading{}, retry{}, scale{ 1.0f };
			std::size_t copy{ ChaosPulse::kBindingCopies };
			bool grand{}, found{};
		};
		std::vector<Target> targets;
		void Drop(Target& target)
		{
			if (target.effect) target.effect->lifetime = 0;
			target.effect.reset();
			for (auto& part : target.parts) { part.node.reset(); part.shaders.clear(); }
			target.found = false;
			target.loading = 0;
			target.copy = ChaosPulse::kBindingCopies;
		}
		bool Find(Target& target)
		{
			auto* root = Posing::RootOf(target.effect.get());
			if (!root) return false;
			for (std::size_t i = 0; i < target.parts.size(); ++i) {
				target.parts[i].node.reset(root->GetObjectByName(ChaosPulse::kBindingNames[i]));
				if (!target.parts[i].node) return false;
			}
			for (auto& part : target.parts) part.shaders = Posing::ShadersOf(part.node.get());
			target.found = true;
			return true;
		}
		void Spawn(Target& target, RE::Actor* enemy)
		{
			if (target.effect || target.retry > 0) return;
			const float raw = enemy->GetHeight();
			target.scale = (std::isfinite(raw) ? std::clamp(raw / 128.0f, 0.55f, 1.8f) : 1.0f) * (target.grand ? 1.08f : 1.0f);
			for (std::size_t copy = 0; copy < ChaosPulse::kBindingCopies; ++copy) {
				if (std::ranges::any_of(targets, [copy](const Target& other) { return other.effect && other.copy == copy; })) continue;
				target.effect.reset(Effects::ChaosBinding(enemy, copy, enemy->GetPosition(), target.left + 1.5f));
				if (!target.effect) continue;
				target.copy = copy;
				target.loading = 0;
				return;
			}
			// Missing assets or a full visual pool never restore the unrelated floating rune.
			target.retry = 1.0f;
		}
		void Pose(Target& target, RE::Actor* enemy)
		{
			auto* root = Posing::RootOf(target.effect.get());
			if (!root || !target.found) return;
			const auto look = ChaosPulse::BindAt(target.age, target.left, target.grand);
			const auto feet = enemy->GetPosition();
			for (std::size_t i = 0; i < target.parts.size(); ++i) {
				auto& part = target.parts[i];
				const auto& frame = look.parts[i];
				RE::NiMatrix3 turn;
				turn.MakeZRotation(-frame.turn);
				Posing::PlaceInWorld(root, part.node.get(), feet + RE::NiPoint3{ 0, 0, 2.0f + frame.height * target.scale }, turn, frame.scale * target.scale);
				Posing::Paint(part.shaders, frame.color, frame.alpha);
				if (i >= 1 && i <= 3) Posing::Drift(part.shaders, -target.age * (i == 1 ? 0.13f : 0.23f));
			}
			Posing::Refresh(root);
		}
		bool Find(Pulse& pulse)
		{
			auto* root = Posing::RootOf(pulse.effect.get());
			if (!root) return false;
			for (std::size_t i = 0; i < pulse.parts.size(); ++i) {
				pulse.parts[i].node.reset(root->GetObjectByName(ChaosPulse::kNames[i]));
				if (!pulse.parts[i].node) return false;
			}
			for (auto& part : pulse.parts) part.shaders = Posing::ShadersOf(part.node.get());
			pulse.spirit.node.reset(root->GetObjectByName("Spirit"));
			if (!pulse.spirit.node) return false;
			for (std::size_t i = 0; i < pulse.joints.size(); ++i) {
				pulse.joints[i].reset(root->GetObjectByName(ChaosPulse::kJoints[i]));
				if (!pulse.joints[i]) return false;
			}
			pulse.spirit.shaders = Posing::ShadersOf(pulse.spirit.node.get());
			pulse.found = true;
			return true;
		}
		void Finish(Pulse& pulse)
		{
			EffectAudio::End(pulse.audio);
			Posing::Out(pulse.lamp);
			if (pulse.effect) pulse.effect->lifetime = 0.0f;
		}
		void Pose(Pulse& pulse)
		{
			const auto p = ChaosPulse::At(pulse.age, pulse.grand, pulse.seconds);
			auto* root = Posing::RootOf(pulse.effect.get());
			for (std::size_t i = 0; i < pulse.parts.size(); ++i) {
				auto& part = pulse.parts[i];
				const auto& f = p.parts[i];
				RE::NiMatrix3 turn;
				const float c = std::cos(f.turn), s = std::sin(f.turn);
				turn.entry[0][0] = c; turn.entry[0][1] = -s;
				turn.entry[1][0] = s; turn.entry[1][1] = c;
				Posing::PlaceInWorld(root, part.node.get(), pulse.feet + RE::NiPoint3{ 0.0f, 0.0f, 3.0f + f.height }, turn, pulse.scale * f.scale);
				Posing::Paint(part.shaders, f.color, f.alpha);
				Posing::Drift(part.shaders, pulse.age * -0.15f);
			}
			const auto& s = p.spirit;
			RE::NiMatrix3 yaw, lean, roll, twist;
			yaw.MakeZRotation(-pulse.heading);
			lean.MakeXRotation(s.lean);
			roll.MakeYRotation(s.roll);
			twist.MakeZRotation(s.twist);
			const RE::NiPoint3 forward{ std::sin(pulse.heading), std::cos(pulse.heading), 0.0f };
			Posing::PlaceInWorld(root, pulse.spirit.node.get(), pulse.feet + forward * s.forward + RE::NiPoint3{ 0.0f, 0.0f, s.height }, yaw * twist * roll * lean, s.scale);
			for (std::size_t i = 0; i < pulse.joints.size(); ++i) {
				const auto& joint = s.bones[i];
				pulse.joints[i]->local.translate = { joint.at[0], joint.at[1], joint.at[2] };
				for (std::size_t row=0; row<3; ++row) for (std::size_t col=0; col<3; ++col)
					pulse.joints[i]->local.rotate.entry[row][col] = joint.turn[row*3+col];
			}
			Posing::Paint(pulse.spirit.shaders, s.color, s.alpha);
			Posing::Shine(pulse.lamp, root, pulse.feet + RE::NiPoint3{ 0.0f, 0.0f, 40.0f }, p.parts[0].color, p.light, 280.0f * pulse.scale);
			Posing::Refresh(root);
		}
	}
	void Call(RE::Actor* tank, float reach, bool grand, float seconds)
	{
		if (!std::isfinite(seconds) || seconds <= 0 || !tank || !tank->Is3DLoaded() || !std::isfinite(reach) || reach <= 0.0f) return;
		std::size_t copy = 0;
		for (; copy < ChaosPulse::kCopies; ++copy) {
			if (std::ranges::none_of(pulses, [copy](const Pulse& p) { return p.copy == copy; })) break;
		}
		if (copy == ChaosPulse::kCopies) return;
		Pulse pulse;
		pulse.copy = copy;
		pulse.seconds = std::max(seconds, ChaosPulse::kGone);
		pulse.grand = grand;
		pulse.heading = tank->GetAngleZ();
		pulse.scale = reach / ChaosPulse::kRadius;
		pulse.feet = tank->GetPosition();
		pulse.effect.reset(Effects::ChaosGround(tank, copy, pulse.feet, pulse.seconds + 1.5f, grand));
		if (pulse.effect) { pulse.audio=EffectAudio::Begin(Soundscape::Kind::Chaos,tank,pulse.feet,pulse.seconds,grand?3:1);pulses.push_back(std::move(pulse)); }
	}
	void Bind(RE::Actor* enemy, float seconds, bool grand)
	{
		if (!enemy || enemy->IsDead() || enemy->IsDisabled() || !std::isfinite(seconds) || seconds <= 0) return;
		const auto handle = enemy->GetHandle();
		for (auto& target : targets) {
			if (target.enemy != handle) continue;
			target.left = std::max(target.left, seconds);
			if (grand && !target.grand && target.effect) target.scale *= 1.08f;
			target.grand = target.grand || grand;
			if (target.effect) target.effect->lifetime = target.effect->age + target.left + 1.5f;
			return;
		}
		Target target;
		target.enemy = handle;
		target.left = seconds;
		target.grand = grand;
		targets.push_back(std::move(target));
	}
	void Keep(std::span<const std::pair<RE::Actor*, float>> on)
	{
		std::erase_if(targets, [&](Target& target) {
			auto enemy = target.enemy.get();
			const auto found = std::ranges::find_if(on, [&](const auto& item) { return enemy && item.first == enemy.get() && std::isfinite(item.second) && item.second > 0; });
			if (found == on.end()) { Drop(target); return true; }
			target.left = found->second;
			if (target.effect) target.effect->lifetime = target.effect->age + target.left + 1.5f;
			return false;
		});
		for (const auto& [enemy, left] : on) {
			if (!enemy || !std::isfinite(left) || left <= 0) continue;
			const auto handle = enemy->GetHandle();
			if (std::ranges::none_of(targets, [&](const Target& target) { return target.enemy == handle; })) Bind(enemy, left, false);
		}
	}
	void Tick(float dt)
	{
		dt = std::isfinite(dt) ? std::max(dt, 0.0f) : 0.0f;
		std::erase_if(targets, [dt](Target& target) {
			auto enemy = target.enemy.get();
			target.age += dt;
			target.left = std::max(target.left - dt, 0.0f);
			target.retry = std::max(target.retry - dt, 0.0f);
			if (!Settings::Get().enabled || !enemy || enemy->IsDead() || enemy->IsDisabled() || target.left <= 0) { Drop(target); return true; }
			return false;
		});
		for (auto& target : targets) {
			auto enemy = target.enemy.get();
			if (!enemy) continue;
			if (!Settings::Get().abilityEffects || !enemy->Is3DLoaded()) {
				Drop(target); continue;  // Keep rank/timer while only the visible body is absent.
			}
			if (target.effect && (!target.effect->cell || !target.effect->cell->IsAttached() || target.effect->cell != enemy->GetParentCell() ||
				target.effect->age >= target.effect->lifetime)) Drop(target);
			Spawn(target, enemy.get());
			if (!target.effect) continue;
			if (!target.found && !Find(target)) {
				target.loading += dt;
				if (target.loading > 1.0f) { Drop(target); target.retry = 1.0f; }
				continue;
			}
			Pose(target, enemy.get());
		}
		std::erase_if(pulses, [dt](Pulse& pulse) {
			if (!Settings::Get().enabled || !Settings::Get().abilityEffects || !pulse.effect || !pulse.effect->cell || !pulse.effect->cell->IsAttached()) {
				Finish(pulse); return true;
			}
			if (!pulse.found && !Find(pulse)) {
				pulse.loading += std::max(dt, 0.0f);
				if (pulse.loading > 1.0f) { Finish(pulse); return true; }
				return false;
			}
			pulse.age += std::max(dt, 0.0f);
			if (pulse.age >= pulse.seconds) { Finish(pulse); return true; }
			Pose(pulse);
			return false;
		});
	}
	void Clear()
	{
		for (auto& target : targets) Drop(target);
		targets.clear();
		for (auto& pulse : pulses) Finish(pulse);
		pulses.clear();
	}
}
