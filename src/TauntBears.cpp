#include "FWM/TauntBears.h"
#include "FWM/EffectAudio.h"

#include "FWM/BearRoar.h"
#include "FWM/Effects.h"
#include "FWM/Forms.h"
#include "FWM/Log.h"
#include "FWM/Posing.h"
#include "FWM/Settings.h"

namespace FWM::TauntBears
{
	namespace
	{
		struct Part
		{
			RE::NiPointer<RE::NiAVObject> node;
			std::vector<RE::BSShaderProperty*> shaders;
		};
		struct Bear
		{
			RE::ActorHandle by;
			RE::NiPointer<RE::BSTempEffectParticle> effect;
			Part head;
			RE::NiPointer<RE::NiAVObject> jaw;
			std::array<Part, BearRoar::kWaves> waves;
			RE::NiPoint3 feet;
			float heading{}, age{}, loading{};
			std::size_t copy{};
			bool found{}, sounded{}, grand{}, warCry{}, compact{};
			Posing::Lamp lamp;
		};
		std::vector<Bear> bears;

		RE::NiMatrix3 Turn(float a_heading, float a_pitch = 0.0f)
		{
			const float c = std::cos(a_heading), s = std::sin(a_heading), cp = std::cos(a_pitch), sp = std::sin(a_pitch);
			RE::NiMatrix3 out;
			out.entry[0][0] = c; out.entry[0][1] = s * cp; out.entry[0][2] = -s * sp;
			out.entry[1][0] = -s; out.entry[1][1] = c * cp; out.entry[1][2] = -c * sp;
			out.entry[2][0] = 0.0f; out.entry[2][1] = sp; out.entry[2][2] = cp;
			return out;
		}

		bool Find(Bear& a_bear)
		{
			auto* root = Posing::RootOf(a_bear.effect.get());
			if (!root) return false;
			a_bear.head.node.reset(root->GetObjectByName("Head"));
			a_bear.jaw.reset(root->GetObjectByName("Jaw"));
			if (!a_bear.head.node || !a_bear.jaw) return false;
			a_bear.head.shaders = Posing::ShadersOf(a_bear.head.node.get());
			for (std::size_t i = 0; i < a_bear.waves.size(); ++i) {
				auto& wave = a_bear.waves[i];
				wave.node.reset(root->GetObjectByName(std::format("Wave{}", i)));
				wave.shaders = Posing::ShadersOf(wave.node.get());
			}
			a_bear.found = true;
			Log::Info("[Spectral head] {} head and jaw loaded in {:.2f} s (copy {})."sv, a_bear.warCry ? "War Cry" : "Taunt", a_bear.loading, a_bear.copy + 1);
			return true;
		}

		void Finish(Bear& a_bear)
		{
			Posing::Out(a_bear.lamp);
			if (a_bear.effect) a_bear.effect->lifetime = 0.0f;
		}

		void Pose(Bear& a_bear)
		{
			const auto p = a_bear.warCry ? BearRoar::WarCryAt(a_bear.age, a_bear.grand, a_bear.compact) : BearRoar::At(a_bear.age, a_bear.grand, a_bear.compact);
			auto* root = Posing::RootOf(a_bear.effect.get());
			const RE::NiPoint3 forward{ std::sin(a_bear.heading), std::cos(a_bear.heading), 0.0f };
			const RE::NiPoint3 right{ std::cos(a_bear.heading), -std::sin(a_bear.heading), 0.0f };
			const auto at = a_bear.feet + forward * p.forward + right * p.side + RE::NiPoint3{ 0.0f, 0.0f, p.height };
			Posing::PlaceInWorld(root, a_bear.head.node.get(), at, Turn(a_bear.heading + p.yaw, p.pitch), p.scale);
			a_bear.jaw->local.rotate = Turn(0.0f, -p.jaw);
			// Rank III uses its own crimson palette and lower emission in the mesh.
			Posing::PaintBlended(a_bear.head.shaders, p.tint, p.alpha);
			for (std::size_t i = 0; i < a_bear.waves.size(); ++i) {
				auto& wave = a_bear.waves[i];
				const auto& w = p.waves[i];
				Posing::PlaceInWorld(root, wave.node.get(), a_bear.feet + forward * w.forward + RE::NiPoint3{ 0.0f, 0.0f, a_bear.warCry || a_bear.compact ? p.height : 205.0f }, Turn(a_bear.heading), w.radius);
				Posing::Paint(wave.shaders, p.glow, w.alpha);
			}
			Posing::Shine(a_bear.lamp, root, at, p.glow, p.alpha * (a_bear.grand ? 0.75f : 1.5f), 350.0f);
			Posing::Refresh(root);
			if (!a_bear.sounded && a_bear.age >= BearRoar::kRoar) {
				a_bear.sounded = true;
				if (const auto source=a_bear.by.get()) EffectAudio::Begin(a_bear.warCry ? Soundscape::Kind::WarCry : Soundscape::Kind::Taunt,source.get(),at,3.0f,a_bear.grand?3:1);
				auto* player = RE::PlayerCharacter::GetSingleton();
				auto tank = a_bear.by.get();
				const bool nearbyWorld = player && tank && (player->GetParentCell() == a_bear.effect->cell ||
					(!a_bear.effect->cell->IsInteriorCell() && player->GetWorldspace() == tank->GetWorldspace()));
				if (nearbyWorld) {
					const float strength = BearRoar::Shake(player->GetPosition().GetDistance(at));
					if (strength > 0.01f) RE::ShakeCamera(strength, at, BearRoar::kShakeSeconds);
				}
			}
		}
	}

	static bool Start(RE::Actor* a_tank, bool a_grand, bool a_warCry)
	{
		if (!a_tank || !a_tank->Is3DLoaded() || a_tank->IsDead() || !Settings::Get().enabled || !Settings::Get().abilityEffects) return false;
		std::size_t copy = 0;
		for (; copy < BearRoar::kCopies; ++copy) {
			if (std::ranges::none_of(bears, [copy, a_warCry, a_grand](const Bear& a_other) { return a_other.copy == copy && a_other.warCry == a_warCry && a_other.grand == a_grand; })) break;
		}
		if (copy == BearRoar::kCopies) return false;
		Bear bear;
        bear.compact=Settings::Get().compactEffects;
		bear.copy = copy;
		bear.grand = a_grand;
		bear.warCry = a_warCry;
		bear.by = a_tank->GetHandle();
		bear.feet = a_tank->GetPosition();
		bear.heading = a_tank->GetAngleZ();
		bear.effect.reset(Effects::BearHead(a_tank, copy, bear.feet, BearRoar::kGone + 1.5f, a_grand, a_warCry));
		if (!bear.effect) return false;
		bears.push_back(std::move(bear));
		return true;
	}

	bool Call(RE::Actor* a_tank, bool a_grand) { return Start(a_tank, a_grand, false); }
	bool WarCry(RE::Actor* a_actor, int a_rank) { return Start(a_actor, a_rank >= 3, true); }

	void Tick(float a_dt)
	{
		std::erase_if(bears, [a_dt](Bear& bear) {
			auto tank = bear.by.get();
			if (!Settings::Get().abilityEffects || !tank || tank->IsDead() || !tank->Is3DLoaded() || !bear.effect ||
				!bear.effect->cell || !bear.effect->cell->IsAttached() || tank->GetParentCell() != bear.effect->cell) {
				Finish(bear);
				return true;
			}
			// Start the animation after loading, so disk latency cannot swallow the roar.
			if (!bear.found && !Find(bear)) {
				bear.loading += std::max(a_dt, 0.0f);
				if (bear.loading > 1.0f) {
					EffectAudio::Begin(bear.warCry ? Soundscape::Kind::WarCry : Soundscape::Kind::Taunt, tank.get(), bear.feet, 3.0f, bear.grand ? 3 : 1);
					Log::Warn("[Spectral head] Mesh did not load; kept the ability sound."sv);
					Finish(bear);
					return true;
				}
				return false;
			}
			bear.age += std::max(a_dt, 0.0f);
			if (bear.age >= BearRoar::kGone) {
				Finish(bear);
				return true;
			}
			// Follow the tank while forming, then leave the roaring spirit planted in the world.
			if (bear.warCry || bear.age < BearRoar::kRoar) {
				bear.feet = tank->GetPosition();
				bear.heading = tank->GetAngleZ();
			}
			Pose(bear);
			return false;
		});
	}

	void Clear()
	{
		for (auto& bear : bears) Finish(bear);
		bears.clear();
	}
}
