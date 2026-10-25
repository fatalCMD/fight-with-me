#include "FWM/Orbs.h"
#include "FWM/EffectAudio.h"

#include "FWM/Effects.h"
#include "FWM/Forms.h"
#include "FWM/Game.h"
#include "FWM/Log.h"
#include "FWM/MagnusOrb.h"
#include "FWM/Posing.h"
#include "FWM/Settings.h"

namespace FWM::Orbs
{
	namespace
	{
		using MagnusOrb::Vec;

		struct Part
		{
			RE::NiPointer<RE::NiAVObject>      node;
			std::vector<RE::BSShaderProperty*> shaders;
		};

		struct Orb
		{
			RE::ActorHandle                         mage;
			std::string                             name;  // the mage's
			float                                   seconds{ 0.0f };
			float                                   age{ 0.0f };
			bool                                    grand{ false };
			RE::NiPointer<RE::BSTempEffectParticle> effect;
			Vec                                     followed{};
			bool                                    placed{ false };

			// Its parts, found once the mesh has loaded.
			bool                                                 found{ false };
			Part                                                 orb, core, shell, swirl, halo, rim;
			std::array<Part, MagnusOrb::kRings>                  rings;
			std::array<Part, MagnusOrb::kTendrils>               tendrils, veils;
			std::array<Part, MagnusOrb::kFlows>                  flows;
			std::array<Part, MagnusOrb::kWisps>                  wisps;
			std::vector<RE::BSShaderProperty*>                   skins;    // the noise drifting over them
			std::vector<RE::BSShaderProperty*>                   streams;  // and pouring down these

			Posing::Lamp      lamp;
			EffectAudio::ID audio{};
		};
		std::vector<Orb> orbs;

		RE::NiPoint3 Ni(Vec a_v) { return { a_v.x, a_v.y, a_v.z }; }
		Vec          Pure(const RE::NiPoint3& a_p) { return { a_p.x, a_p.y, a_p.z }; }

		RE::NiMatrix3 Ni(const MagnusOrb::Mat& a_m)
		{
			RE::NiMatrix3 out;
			for (int i = 0; i < 3; ++i) {
				for (int j = 0; j < 3; ++j) {
					out.entry[i][j] = a_m[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
				}
			}
			return out;
		}

		RE::NiMatrix3 AboutUp(float a_angle)
		{
			RE::NiMatrix3 turn;
			turn.entry[0][0] = std::cos(a_angle);
			turn.entry[0][1] = -std::sin(a_angle);
			turn.entry[1][0] = std::sin(a_angle);
			turn.entry[1][1] = std::cos(a_angle);
			return turn;
		}

		// Where the magicka goes: their chest, by the spine, else over their feet.
		RE::NiPoint3 ChestOf(RE::Actor* a_mage)
		{
			auto* model = a_mage->Get3D();
			if (auto* spine = model ? model->GetObjectByName("NPC Spine2 [Spn2]") : nullptr) {
				return spine->world.translate;
			}
			return a_mage->GetPosition() + RE::NiPoint3{ 0.0f, 0.0f, MagnusOrb::kChest };
		}

		bool Find(Orb& a_orb)
		{
			auto* root = Posing::RootOf(a_orb.effect.get());
			if (!root) {
				return false;
			}
			auto find = [&](Part& a_part, const std::string& a_name) {
				a_part.node.reset(root->GetObjectByName(a_name));
				a_part.shaders = Posing::ShadersOf(a_part.node.get());
				return a_part.node != nullptr;
			};
			if (!find(a_orb.orb, "Orb")) {
				return false;
			}
			find(a_orb.core, "Core");
			find(a_orb.shell, "Shell");
			find(a_orb.swirl, "Swirl");
			find(a_orb.halo, "Halo");
			find(a_orb.rim, "Rim");
			for (std::size_t i = 0; i < a_orb.rings.size(); ++i) {
				find(a_orb.rings[i], std::format("Ring{}", i));
			}
			for (std::size_t i = 0; i < a_orb.tendrils.size(); ++i) {
				find(a_orb.tendrils[i], std::format("Tendril{}", i));
				find(a_orb.veils[i], std::format("Veil{}", i));
				for (auto* list : { &a_orb.tendrils[i].shaders, &a_orb.veils[i].shaders }) {
					a_orb.streams.insert(a_orb.streams.end(), list->begin(), list->end());
				}
			}
			for (std::size_t i = 0; i < a_orb.flows.size(); ++i) {
				find(a_orb.flows[i], std::format("Flow{:02}", i));
			}
			for (std::size_t i = 0; i < a_orb.wisps.size(); ++i) {
				find(a_orb.wisps[i], std::format("Wisp{:02}", i));
			}
			for (auto* part : { &a_orb.shell, &a_orb.swirl, &a_orb.rim }) {
				std::copy_if(part->shaders.begin(), part->shaders.end(), std::back_inserter(a_orb.skins), Posing::Noisy);
			}
			a_orb.found = true;
			Log::Info("[Paths] {}'s {}Magnus orb shows {:.2f} s in."sv, a_orb.name, a_orb.grand ? "grand " : "", a_orb.age);
			return true;
		}

		// A part placed in the world and given its colour and strength.
		void Set(RE::NiAVObject* a_root, Part& a_part, const RE::NiPoint3& a_at, const RE::NiMatrix3& a_turn, float a_scale, const MagnusOrb::Color& a_color, float a_alpha)
		{
			if (!a_part.node) {
				return;
			}
			Posing::PlaceInWorld(a_root, a_part.node.get(), a_at, a_turn, std::max(a_scale, 0.001f));
			Posing::Paint(a_part.shaders, a_color, a_alpha);
			Posing::Refresh(a_part.node.get());
		}

		void Pose(Orb& a_orb, RE::Actor* a_mage, float a_dt)
		{
			const auto chest = ChestOf(a_mage);
			const auto home = MagnusOrb::Home(Pure(a_mage->GetPosition()), a_mage->GetAngleZ(), a_orb.age, a_orb.grand);
			a_orb.followed = a_orb.placed ? MagnusOrb::Follow(a_orb.followed, home, a_dt) : home;
			a_orb.placed = true;
			const auto look = MagnusOrb::At(a_orb.age, a_orb.seconds, a_orb.grand);
			// Out of their chest to its place, and back at the end.
			const auto at = chest + (Ni(a_orb.followed) - chest) * look.out;
			Posing::Shine(a_orb.lamp, Posing::RootOf(a_orb.effect.get()), at, look.lightColor, look.light * MagnusOrb::kLightFade, MagnusOrb::kLightReach);
			auto* root = Posing::RootOf(a_orb.effect.get());
			if (!root || !a_orb.found) {
				return;
			}
			EffectAudio::Move(a_orb.audio,at);
			const RE::NiMatrix3 still;
			auto& orb = a_orb.orb.node;
			Posing::PlaceInWorld(root, orb.get(), at, still, std::max(look.scale, 0.001f));
			auto local = [](Part& a_part, const RE::NiMatrix3& a_turn, const MagnusOrb::Color& a_color, float a_alpha) {
				if (a_part.node) {
					a_part.node->local.rotate = a_turn;
					Posing::Paint(a_part.shaders, a_color, a_alpha);
				}
			};
			local(a_orb.core, still, look.coreColor, look.alpha * (0.75f + 0.25f * look.core));
			local(a_orb.shell, AboutUp(look.shell), look.shellColor, look.alpha);
			local(a_orb.swirl, AboutUp(look.swirl), look.swirlColor, look.alpha);
			local(a_orb.halo, still, look.haloColor, look.halo);
			local(a_orb.rim, AboutUp(look.shell * -0.55f), look.rimColor, look.rim);
			for (std::size_t i = 0; i < a_orb.rings.size(); ++i) {
				local(a_orb.rings[i], Ni(look.rings[i]), look.ringColor, look.alpha);
			}
			Posing::Refresh(orb.get());
			// The magicka pouring into them.
			const auto from = Pure(at), into = Pure(chest);
			for (int i = 0; i < MagnusOrb::kTendrils; ++i) {
				const auto tendril = MagnusOrb::TendrilAt(i, a_orb.age, from, look.scale, into, look.flow, a_orb.grand);
				const auto veil = MagnusOrb::VeilAt(i, a_orb.age, from, look.scale, into, look.flow, a_orb.grand);
				Set(root, a_orb.tendrils[static_cast<std::size_t>(i)], Ni(tendril.from), Ni(tendril.turn), tendril.scale, look.streamColor, tendril.alpha);
				Set(root, a_orb.veils[static_cast<std::size_t>(i)], Ni(veil.from), Ni(veil.turn), veil.scale, look.streamColor, veil.alpha);
			}
			for (int i = 0; i < MagnusOrb::kFlows; ++i) {
				const auto mote = MagnusOrb::FlowAt(i, a_orb.age, from, look.scale, into, look.flow, a_orb.grand);
				Set(root, a_orb.flows[static_cast<std::size_t>(i)], Ni(mote.at), still, mote.size, mote.color, mote.alpha);
			}
			for (int i = 0; i < MagnusOrb::kWisps; ++i) {
				const auto wisp = MagnusOrb::WispAt(i, a_orb.age, from, look.scale, into, look.flow, a_orb.grand);
				Set(root, a_orb.wisps[static_cast<std::size_t>(i)], Ni(wisp.at), Ni(wisp.turn), wisp.size, wisp.color, wisp.alpha);
			}
			Posing::Drift(a_orb.skins, -a_orb.age * MagnusOrb::kDrift);
			Posing::Drift(a_orb.streams, -a_orb.age * MagnusOrb::kStream);
		}

		void Finish(Orb& a_orb)
		{
			Posing::Out(a_orb.lamp);
			EffectAudio::End(a_orb.audio);
			if (a_orb.effect) {
				a_orb.effect->lifetime = 0.0f;
			}
		}
	}

	void Summon(RE::Actor* a_mage, std::string a_name, float a_seconds, bool a_grand)
	{
		if (!a_mage || a_seconds <= 0.0f || !Settings::Get().abilityEffects) {
			return;
		}
		std::erase_if(orbs, [&](Orb& a_orb) {
			if (a_orb.mage != a_mage->GetHandle()) {
				return false;
			}
			Finish(a_orb);
			return true;
		});
		Orb orb;
		orb.mage = a_mage->GetHandle();
		orb.name = std::move(a_name);
		orb.seconds = a_seconds;
		orb.grand = a_grand;
		orb.effect.reset(Effects::Orb(a_mage, ChestOf(a_mage), a_seconds + 0.5f));
		if (!orb.effect) {
			Log::Info("[Paths] {}'s Magnus orb could not be shown."sv, orb.name);
			return;
		}
		orb.audio=EffectAudio::Begin(Soundscape::Kind::Magnus,a_mage,ChestOf(a_mage),a_seconds,a_grand?3:1);
		Log::Info("[Paths] {} calls up the {}Magnus orb for {:.0f} s."sv, orb.name, a_grand ? "grand " : "", a_seconds);
		orbs.push_back(std::move(orb));
	}

	void Tick(float a_dt)
	{
		for (auto& orb : orbs) {
			orb.age += a_dt;
			auto mage = orb.mage.get();
			if (!mage || mage->IsDead() || !mage->Is3DLoaded()) {
				orb.age = std::max(orb.age, orb.seconds);
				continue;
			}
			if (!orb.found) {
				Find(orb);
			}
			Pose(orb, mage.get(), a_dt);
		}
		std::erase_if(orbs, [](Orb& a_orb) {
			const bool gone = !a_orb.effect || !a_orb.effect->cell || !a_orb.effect->cell->IsAttached();
			if (a_orb.age < a_orb.seconds && !gone && Settings::Get().abilityEffects) {
				return false;
			}
			Finish(a_orb);
			return true;
		});
	}

	void Clear()
	{
		for (auto& orb : orbs) {
			Finish(orb);
		}
		orbs.clear();
	}
}
