#include "FWM/Sunders.h"

#include "FWM/Effects.h"
#include "FWM/Log.h"
#include "FWM/Posing.h"
#include "FWM/Settings.h"
#include "FWM/Sunder.h"

namespace FWM::Sunders
{
	namespace
	{
		struct Part
		{
			RE::NiPointer<RE::NiAVObject> node;
			std::vector<RE::BSShaderProperty*> shaders;
		};

		struct Breakage
		{
			RE::ActorHandle enemy;
			RE::NiPointer<RE::BSTempEffectParticle> effect;
			RE::NiPointer<RE::NiAVObject> model, chest, mark, burst;
			RE::NiMatrix3 chestInverse, markFacing, impactFacing;
			RE::NiPoint3 impact;
			Part cracks, grandCracks, flash, dust;
			std::array<Part, Sunder::kFragments> fragments;
			std::array<Part, Sunder::kSparks> sparks;
			Posing::Lamp lamp;
			float age{}, loading{}, duration{}, scale{ 1.0f };
			std::size_t copy{};
			bool grand{}, found{}, humanoid{};
		};
		std::vector<Breakage> breaks;

		RE::NiMatrix3 Facing(RE::Actor* a_enemy)
		{
			RE::NiMatrix3 turn;
			// Skyrim's heading turns +Y toward +X; the mark's mesh uses +Y as its front.
			turn.MakeZRotation(a_enemy->GetAngleZ());
			return turn;
		}

		RE::NiMatrix3 InitialLean(RE::Actor* a_enemy, RE::NiAVObject* a_model, RE::NiAVObject* a_chest)
		{
			const auto facing = Facing(a_enemy);
			auto* neck = a_model ? a_model->GetObjectByName("NPC Neck [Neck]") : nullptr;
			if (!neck || !a_chest) return facing;
			auto up = neck->world.translate - a_chest->world.translate;
			if (!std::isfinite(up.x) || !std::isfinite(up.y) || !std::isfinite(up.z) || up.z < 2.0f) return facing;
			const float across = std::hypot(up.x, up.y);
			if (across > up.z * 0.75f) {
				const float bound = up.z * 0.75f / across;
				up.x *= bound; up.y *= bound;
			}
			up.Unitize();
			auto right = facing.GetVectorY().Cross(up);
			right.Unitize();
			const auto forward = up.Cross(right);
			// The vector constructor takes rows; our local basis belongs in columns.
			return RE::NiMatrix3(right, forward, up).Transpose();
		}

		void BindChest(Breakage& a_break, RE::Actor* a_enemy)
		{
			auto* model = a_enemy->Get3D();
			if (a_break.model.get() == model) return;
			a_break.model.reset(model);
			a_break.chest.reset(model && a_break.humanoid ? model->GetObjectByName("NPC Spine2 [Spn2]") : nullptr);
			a_break.markFacing = InitialLean(a_enemy, model, a_break.chest.get());
			if (a_break.chest) a_break.chestInverse = a_break.chest->world.rotate.Transpose();
		}

		RE::NiPoint3 ChestAt(Breakage& a_break, RE::Actor* a_enemy)
		{
			BindChest(a_break, a_enemy);
			if (a_break.chest) return a_break.chest->world.translate;
			// Unknown skeletons get a compact mark near the body, not an actor-sized shell.
			const float height = std::clamp(a_enemy->GetHeight(), 40.0f, 240.0f);
			return a_enemy->GetPosition() + RE::NiPoint3{ 0.0f, 0.0f, height * (a_break.humanoid ? 0.625f : 0.55f) };
		}

		RE::NiMatrix3 ChestTurn(const Breakage& a_break, RE::Actor* a_enemy)
		{
			// Capture the bone's starting axes once. Its later world rotation follows stagger and turning
			// without assuming every skeleton author gave Spine2 the same local axes as our mesh.
			return a_break.chest ? a_break.chest->world.rotate * a_break.chestInverse * a_break.markFacing : Facing(a_enemy);
		}

		RE::NiMatrix3 Turn(const Sunder::Part& a_part)
		{
			RE::NiMatrix3 yaw, tilt, roll;
			// NiMatrix3's helpers turn clockwise; the pure sampler and preview use standard Rz*Ry*Rx.
			yaw.MakeZRotation(-a_part.yaw);
			tilt.MakeYRotation(-a_part.tilt);
			roll.MakeXRotation(-a_part.roll);
			return yaw * tilt * roll;
		}

		void Local(Part& a_part, const Sunder::Part& a_look, bool a_blended)
		{
			a_part.node->local.translate = { a_look.at.x, a_look.at.y, a_look.at.z };
			a_part.node->local.rotate = Turn(a_look);
			a_part.node->local.scale = std::max(a_look.scale, 0.001f);
			if (a_blended) Posing::PaintBlended(a_part.shaders, a_look.color, a_look.alpha);
			else Posing::Paint(a_part.shaders, a_look.color, a_look.alpha);
		}

		bool Find(Breakage& a_break)
		{
			auto* root = Posing::RootOf(a_break.effect.get());
			if (!root) return false;
			a_break.mark.reset(root->GetObjectByName("Mark"));
			a_break.burst.reset(root->GetObjectByName("Burst"));
			if (!a_break.mark || !a_break.burst) return false;
			auto find = [root](Part& a_part, const std::string& a_name) {
				a_part.node.reset(root->GetObjectByName(a_name));
				return a_part.node != nullptr;
			};
			if (!find(a_break.cracks, "Cracks") || !find(a_break.grandCracks, "GrandCracks") ||
				!find(a_break.flash, "Flash") || !find(a_break.dust, "Dust")) return false;
			for (std::size_t i = 0; i < a_break.fragments.size(); ++i) {
				if (!find(a_break.fragments[i], std::format("Fragment{:02}", i))) return false;
			}
			for (std::size_t i = 0; i < a_break.sparks.size(); ++i) {
				if (!find(a_break.sparks[i], std::format("Spark{:02}", i))) return false;
			}
			// Only collect once the complete mesh exists: each leaf gets its own material exactly once.
			auto own = [](Part& a_part) { a_part.shaders = Posing::ShadersOf(a_part.node.get()); };
			own(a_break.cracks); own(a_break.grandCracks); own(a_break.flash); own(a_break.dust);
			for (auto& part : a_break.fragments) own(part);
			for (auto& part : a_break.sparks) own(part);
			a_break.found = true;
			if (auto enemy = a_break.enemy.get()) {
				Log::Info("[Look] Sunder{} armor fracture loaded on {:08X}, copy {}, scale {:.2f}."sv,
					a_break.grand ? " III" : "", enemy->GetFormID(), a_break.copy + 1, a_break.scale);
			}
			return true;
		}

		void Pose(Breakage& a_break, RE::Actor* a_enemy)
		{
			auto* root = Posing::RootOf(a_break.effect.get());
			if (!root || !a_break.found) return;
			const auto look = Sunder::At(a_break.age, a_break.duration, a_break.grand);
			const auto chest = ChestAt(a_break, a_enemy);
			Posing::PlaceInWorld(root, a_break.mark.get(), chest, ChestTurn(a_break, a_enemy), a_break.scale);
			Posing::PlaceInWorld(root, a_break.burst.get(), a_break.impact, a_break.impactFacing, a_break.scale);
			Local(a_break.cracks, look.cracks, false);
			Local(a_break.grandCracks, look.grandCracks, false);
			Local(a_break.flash, look.flash, false);
			Local(a_break.dust, look.dust, true);
			for (std::size_t i = 0; i < a_break.fragments.size(); ++i) Local(a_break.fragments[i], look.fragments[i], true);
			for (std::size_t i = 0; i < a_break.sparks.size(); ++i) Local(a_break.sparks[i], look.sparks[i], false);
			Posing::Drift(a_break.dust.shaders, a_break.age * 0.09f);
			Posing::Refresh(a_break.mark.get());
			Posing::Refresh(a_break.burst.get());
			Posing::Shine(a_break.lamp, root, a_break.impact, look.flash.color, look.light, 150.0f * a_break.scale);
		}

		void Finish(Breakage& a_break)
		{
			Posing::Out(a_break.lamp);
			if (a_break.effect) a_break.effect->lifetime = 0.0f;
		}

		void Start(Breakage& a_break, RE::Actor* a_enemy, float a_seconds, bool a_grand)
		{
			a_break.age = 0.0f;
			a_break.loading = 0.0f;
			a_break.duration = std::isfinite(a_seconds) ? std::clamp(a_seconds, 0.0f, 60.0f) : 0.0f;
			a_break.grand = a_grand;
			a_break.humanoid = a_enemy->GetRace() && a_enemy->GetRace()->HasKeywordString("ActorTypeNPC");
			a_break.scale = std::clamp(a_enemy->GetHeight() / Sunder::kHeight, a_break.humanoid ? 0.65f : 0.5f, a_break.humanoid ? 1.45f : 1.0f);
			a_break.impact = ChestAt(a_break, a_enemy);
			a_break.impactFacing = Facing(a_enemy);
		}
	}

	bool Break(RE::Actor* a_by, RE::Actor* a_enemy, float a_seconds, bool a_grand)
	{
		if (!a_by || !a_enemy || a_enemy->IsDead() || a_enemy->IsDisabled() || !a_enemy->Is3DLoaded() || !Settings::Get().enabled || !Settings::Get().abilityEffects) return false;
		const auto handle = a_enemy->GetHandle();
		// A repeated strike refreshes this enemy's mark and replays its fragments, without stacking shells.
		for (auto& old : breaks) {
			if (old.enemy != handle || !old.effect || !old.effect->cell || !old.effect->cell->IsAttached() || old.effect->age >= old.effect->lifetime) continue;
			Start(old, a_enemy, a_seconds, a_grand);
			old.effect->lifetime = old.effect->age + Sunder::Seconds(old.duration) + 1.5f;
			if (old.found) Pose(old, a_enemy);
			return true;
		}
		Breakage fresh;
		fresh.enemy = handle;
		Start(fresh, a_enemy, a_seconds, a_grand);
		for (std::size_t copy = 0; copy < Sunder::kCopies; ++copy) {
			if (std::ranges::any_of(breaks, [copy](const Breakage& other) { return other.copy == copy; })) continue;
			fresh.effect.reset(Effects::SunderBreak(a_enemy, copy, fresh.impact, Sunder::Seconds(fresh.duration) + 1.5f));
			if (!fresh.effect) continue;  // A missing copy must not silently reuse an occupied one.
			fresh.copy = copy;
			if (Find(fresh)) Pose(fresh, a_enemy);
			breaks.push_back(std::move(fresh));
			return true;
		}
		return false;
	}

	void Tick(float a_dt)
	{
		const float dt = std::isfinite(a_dt) ? std::max(a_dt, 0.0f) : 0.0f;
		std::erase_if(breaks, [dt](Breakage& live) {
			auto enemy = live.enemy.get();
			if (!Settings::Get().enabled || !Settings::Get().abilityEffects || !enemy || enemy->IsDead() || enemy->IsDisabled() || !enemy->Is3DLoaded() ||
				!live.effect || !live.effect->cell || !live.effect->cell->IsAttached() || enemy->GetParentCell() != live.effect->cell ||
				live.effect->age >= live.effect->lifetime) {
				Finish(live); return true;
			}
			live.age += dt;  // Loading does not delay the debuff's visual countdown or its brief final fade.
			if (live.age >= Sunder::Seconds(live.duration)) { Finish(live); return true; }
			if (!live.found && !Find(live)) {
				live.loading += dt;
				if (live.loading > 1.0f) {
					Effects::Flash(enemy.get(), Effects::Look::kSunderImpact);
					Effects::Sparks(enemy.get());
					Log::Warn("[Look] Sunder fracture copy {} did not load; using its ordinary impact."sv, live.copy + 1);
					Finish(live); return true;
				}
				return false;
			}
			Pose(live, enemy.get());
			return false;
		});
	}

	void Clear()
	{
		for (auto& live : breaks) Finish(live);
		breaks.clear();
	}
}
