#include "FWM/Bastions.h"
#include "FWM/Bastion.h"
#include "FWM/Effects.h"
#include "FWM/Forms.h"
#include "FWM/Posing.h"
#include "FWM/Settings.h"
#include "FWM/TankActs.h"

namespace FWM::Bastions
{
	namespace
	{
		struct Part { RE::NiPointer<RE::NiAVObject> node; std::vector<RE::BSShaderProperty*> shaders; };
		struct Fortress
		{
			RE::ActorHandle actor;
			RE::NiPointer<RE::BSTempEffectParticle> effect;
			std::array<Part, Bastion::kNames.size()> parts;
			RE::NiPoint3 origin;
			float angle{}, age{}, duration{}, scale{ 1 }, loading{};
			int rank{ 1 };
			bool found{}, struck{}, compact{};
			std::array<RE::BSSoundHandle, Bastion::kSounds.size()> sounds;
			std::array<bool, Bastion::kSounds.size()> sounded{}, faded{};
			Posing::Lamp lamp;
		};
		std::optional<Fortress> active;
		bool Find(Fortress& fort)
		{
			auto* root = Posing::RootOf(fort.effect.get());
			if (!root) return false;
			for (std::size_t i = 0; i < fort.parts.size(); ++i) {
				fort.parts[i].node.reset(root->GetObjectByName(Bastion::kNames[i]));
				if (!fort.parts[i].node) return false;
			}
			for (auto& part : fort.parts) part.shaders = Posing::ShadersOf(part.node.get());
			fort.found = true; return true;
		}
		void Sound(Fortress& fort)
		{
			for (std::size_t i = 0; i < fort.sounds.size(); ++i) {
				const auto& cue = Bastion::kSounds[i];
				auto& sound = fort.sounds[i];
				if (!fort.sounded[i] && fort.age >= cue.at) {
					fort.sounded[i] = true;
					// Do not catch up stale sounds after a load hitch or a cancelled guardian state.
					if (fort.age >= cue.at + cue.seconds || fort.age >= fort.duration) continue;
					auto* descriptor = (fort.rank>=3 ? Forms::Get().bastionGrandSounds : Forms::Get().bastionSounds)[i];
					if (auto* audio = RE::BSAudioManager::GetSingleton(); audio && descriptor && audio->GetSoundHandle(sound, descriptor)) {
						sound.SetPosition(fort.origin);
						if(auto* spatial=RE::TESForm::LookupByID<RE::BGSSoundOutput>(0x0F1B12))sound.SetOutputModel(spatial);
						sound.SetVolume(cue.volume[fort.rank - 1]);
						sound.Play();
					}
				}
				if (!fort.faded[i] && (fort.age >= cue.at + cue.seconds - .20f || fort.age >= fort.duration)) {
					fort.faded[i] = true;
					if (sound.IsValid()) sound.FadeOutAndRelease(200);
				}
			}
		}
	}
	void Clear()
	{
		if (active) {
			for (auto& sound : active->sounds) if (sound.IsValid()) sound.Stop();
			Posing::Out(active->lamp); if (active->effect) active->effect->lifetime = 0; active.reset();
		}
	}
	void Call(RE::Actor* actor, float seconds, int rank)
	{
		if (!actor || !actor->Is3DLoaded() || !std::isfinite(seconds) || seconds <= 0 || !Settings::Get().abilityEffects) return;
		Clear();
		Fortress fort;
        fort.compact=Settings::Get().compactEffects;
		fort.actor = actor->GetHandle(); fort.origin = actor->GetPosition(); fort.angle = actor->GetAngleZ();
		fort.duration = seconds; fort.rank = std::clamp(rank, 1, 3);
		const float height = actor->GetHeight();
		fort.scale = std::isfinite(height) ? std::clamp(height / 128.0f, .75f, 1.35f) : 1;
		fort.effect.reset(Effects::BastionCastle(actor, fort.origin, seconds + Bastion::kFade + 1.0f, rank >= 3));
		if (fort.effect) active = std::move(fort);
	}
	void Tick(float dt)
	{
		if (!active || !std::isfinite(dt) || dt <= 0) return;
		auto& fort = *active; fort.age += dt;
		auto actor = fort.actor.get();
		if (!Settings::Get().enabled || !Settings::Get().abilityEffects || !actor || actor->IsDead() || actor->IsDisabled() || !actor->Is3DLoaded() ||
			!fort.effect->cell || !fort.effect->cell->IsAttached() || fort.effect->cell != actor->GetParentCell()) { Clear(); return; }
		if (TankActs::BastionOn(actor->GetFormID()).left <= 0) fort.duration = std::min(fort.duration, fort.age);
		if (fort.age >= fort.duration + Bastion::kFade) { Clear(); return; }
		if (!fort.found && !Find(fort)) { fort.loading += dt; if (fort.loading > 1.5f) Clear(); return; }
		Sound(fort);
		if (!fort.struck && fort.age >= Bastion::kImpact && fort.age < fort.duration) {
			fort.struck = true;
			auto* player = RE::PlayerCharacter::GetSingleton();
			const bool nearbyWorld = player && (player->GetParentCell() == fort.effect->cell ||
				(!fort.effect->cell->IsInteriorCell() && player->GetWorldspace() == actor->GetWorldspace()));
			if (nearbyWorld && fort.age < Bastion::kImpact + Bastion::kShakeSeconds) {
				const float strength = Bastion::Shake(player->GetPosition().GetDistance(fort.origin), fort.rank);
				if (strength > .005f) RE::ShakeCamera(strength, fort.origin, Bastion::kShakeSeconds);
			}
		}
		auto* root = Posing::RootOf(fort.effect.get());
		if (!root) { Clear(); return; }
		const auto frame = Bastion::At(fort.age, fort.duration, fort.rank, fort.compact);
		for (std::size_t i = 0; i < fort.parts.size(); ++i) {
			auto& part = fort.parts[i]; const auto& pose = frame.parts[i];
			const bool follows = i >= 4;
			const auto at = (follows ? actor->GetPosition() : fort.origin) + RE::NiPoint3{ 0, 0, pose.height * fort.scale };
			RE::NiMatrix3 turn; turn.MakeZRotation((follows ? actor->GetAngleZ() : fort.angle) + pose.turn);
			Posing::PlaceInWorld(root, part.node.get(), at, turn, pose.scale * fort.scale);
			if (i == 0 || i == 4) Posing::PaintBlended(part.shaders, pose.color, pose.alpha);
			else Posing::Paint(part.shaders, pose.color, pose.alpha);
			part.node->SetAppCulled(pose.alpha <= 0.0f);
			if (i == 3 || i == 4) Posing::Drift(part.shaders, -.10f * fort.age);
		}
		Posing::Shine(fort.lamp, root, actor->GetPosition() + RE::NiPoint3{ 0, 0, 55 }, frame.parts[1].color, frame.light, 210 * fort.scale);
		Posing::Refresh(root);
	}
}
