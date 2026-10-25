#include "FWM/Quakes.h"
#include "FWM/EffectAudio.h"

#include "FWM/Abilities.h"
#include "FWM/Effects.h"
#include "FWM/Forms.h"
#include "FWM/Log.h"
#include "FWM/Paths.h"
#include "FWM/Posing.h"
#include "FWM/Quake.h"
#include "FWM/Settings.h"

namespace FWM::Quakes
{
	namespace
	{
		constexpr float kHoldAfterFall = 0.6f;    // once felled, they are held down from this soon after
		constexpr float kLaunchWait = 0.6f;       // how long a throw waits for its body to go loose before giving up
		constexpr float kShakeReach = 1500.0f;    // the camera shakes for a slam this close to you
		constexpr float kCrumbleAt = 0.9f;        // seconds before the end the rocks start sinking back

		struct Part
		{
			RE::NiPointer<RE::NiAVObject>      node;
			std::vector<RE::BSShaderProperty*> shaders;
			bool                               blended{ false };  // dust and cracks, laid over the ground rather than glowing
		};

		struct Shake
		{
			RE::NiPointer<RE::BSTempEffectParticle> effect;
			RE::NiPoint3                            at;
			float                                   reach{ 0.0f };
			bool                                    grand{ false };
			float                                   age{ 0.0f };
			bool                                    found{ false };
			bool                                    crumbled{ false };
			RE::NiPointer<RE::NiAVObject>           body;   // "Quake"
			std::vector<Part>                       parts;  // in Quake::Parts() order
			std::vector<RE::BSShaderProperty*>      dust;   // its noise drifting
		};
		std::vector<Shake> slams;

		struct Throw
		{
			RE::ActorHandle enemy;
			RE::NiPoint3    velocity;  // game units a second
			float           waited{ 0.0f };
			int             pushes{ 0 };  // set on two frames running, so nothing in between takes it away
			float           held{ 0.0f };
		};
		std::vector<Throw> throws;

		struct Hold
		{
			RE::ActorHandle enemy;
			float           in{ 0.0f };
			float           seconds{ 0.0f };
		};
		std::vector<Hold> holds;

		const std::uint16_t kSource = static_cast<std::uint16_t>(Paths::Perk::kEarthshaker);

		RE::NiMatrix3 Ni(const Quake::Mat& a_m)
		{
			RE::NiMatrix3 out;
			for (int i = 0; i < 3; ++i) {
				for (int j = 0; j < 3; ++j) {
					out.entry[i][j] = a_m[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
				}
			}
			return out;
		}

		void Knock(RE::Actor* a_enemy, const RE::NiPoint3& a_from, float a_force)
		{
			if (auto* process = a_enemy->GetActorRuntimeData().currentProcess) {
				process->KnockExplosion(a_enemy, a_from, a_force);
			}
		}

		// Half an enemy's breadth, from its bounds.
		float Breadth(RE::Actor* a_actor)
		{
			const auto low = a_actor->GetBoundMin();
			const auto high = a_actor->GetBoundMax();
			return 0.25f * ((high.x - low.x) + (high.y - low.y));
		}

		// Every loose body of a ragdoll set moving at a_velocity together, so the whole of them flies.
		void Push(RE::Actor* a_enemy, const RE::NiPoint3& a_velocity)
		{
			auto* model = a_enemy->Get3D();
			auto* cell = a_enemy->GetParentCell();
			auto* world = cell ? cell->GetbhkWorld() : nullptr;
			if (!model || !world) {
				return;
			}
			const float         scale = RE::bhkWorld::GetWorldScale();
			const RE::hkVector4 velocity(a_velocity.x * scale, a_velocity.y * scale, a_velocity.z * scale, 0.0f);
			RE::BSWriteLockGuard lock(world->worldLock);
			RE::BSVisit::TraverseScenegraphCollision(model, [&](RE::bhkNiCollisionObject* a_object) {
				if (auto* body = a_object && a_object->body ? netimmerse_cast<RE::bhkRigidBody*>(a_object->body.get()) : nullptr) {
					body->SetLinearVelocity(velocity);
				}
				return RE::BSVisit::BSVisitControl::kContinue;
			});
		}

		bool Find(Shake& a_slam)
		{
			auto* root = Posing::RootOf(a_slam.effect.get());
			if (!root) {
				return false;
			}
			a_slam.body.reset(root->GetObjectByName("Quake"));
			if (!a_slam.body) {
				return false;
			}
			for (const auto name : Quake::Parts()) {
				Part part;
				part.node.reset(root->GetObjectByName(std::string(name)));
				part.shaders = Posing::ShadersOf(part.node.get());
				part.blended = !Posing::AddsLight(part.node.get());
				std::copy_if(part.shaders.begin(), part.shaders.end(), std::back_inserter(a_slam.dust), Posing::Noisy);
				a_slam.parts.push_back(std::move(part));
			}
			a_slam.found = true;
			return true;
		}

		void Pose(Shake& a_slam)
		{
			auto* root = Posing::RootOf(a_slam.effect.get());
			if (!root || !a_slam.found) {
				return;
			}
			const auto look = Quake::At(a_slam.age, a_slam.grand);
			Posing::PlaceInWorld(root, a_slam.body.get(), a_slam.at, {}, a_slam.reach / Quake::kMeshRadius);
			for (std::size_t i = 0; i < a_slam.parts.size(); ++i) {
				auto&       part = a_slam.parts[i];
				const auto& p = look.parts[i];
				if (!part.node) {
					continue;
				}
				part.node->local.translate = { p.at.x, p.at.y, p.at.z };
				part.node->local.rotate = Ni(p.turn);
				part.node->local.scale = std::max(p.scale, 0.0001f);
				// The rocks are lit: only the glows and the dust take this.
				if (part.blended) {
					Posing::PaintBlended(part.shaders, p.color, p.alpha);
				} else {
					Posing::Paint(part.shaders, p.color, p.alpha);
				}
			}
			Posing::Drift(a_slam.dust, -a_slam.age * 0.2f);
			Posing::Refresh(a_slam.body.get());
		}
	}

	void Slam(RE::Actor* a_by, const RE::NiPoint3& a_at, float a_reach, bool a_grand)
	{
		Posing::PlayAt(Forms::Get().quakeRocks, a_at);
		Posing::PlayAt(Forms::Get().quakeRumble, a_at);
		if (a_grand) EffectAudio::Begin(Soundscape::Kind::Impact,a_by,a_at,2.0f,3);
		if (!a_by || !Settings::Get().abilityEffects) {
			return;
		}
		// The ground shakes under you too, less the further off the slam.
		auto* player = RE::PlayerCharacter::GetSingleton();
		const float away = player ? player->GetPosition().GetDistance(a_at) : kShakeReach;
		if (away < kShakeReach) {
			const float strength = Quake::kShake[a_grand ? 2 : (a_reach > 300.0f ? 1 : 0)] * (1.0f - 0.6f * away / kShakeReach);
			RE::ShakeCamera(strength, a_at, a_grand ? 0.9f : 0.6f);
		}
		Shake slam;
		slam.at = a_at;
		slam.reach = a_reach;
		slam.grand = a_grand;
		slam.effect.reset(Effects::Quake(a_by, a_at, Quake::Seconds(a_grand) + 0.3f));
		if (slam.effect) {
			slams.push_back(std::move(slam));
		}
	}

	bool Down(RE::Actor* a_enemy, const RE::NiPoint3& a_from, int a_rank, bool a_boss)
	{
		if (!a_enemy || a_enemy->IsDead()) {
			return false;
		}
		const float held = Quake::Downed(a_rank, a_boss);
		if (a_rank >= 3 && !a_boss && Breadth(a_enemy) <= Quake::kLaunchBreadth) {
			// Loose first, then thrown: up fast enough for fifteen feet, a little away from the slam.
			const auto at = a_enemy->GetPosition();
			Knock(a_enemy, at - RE::NiPoint3{ 0.0f, 0.0f, 40.0f }, 0.2f);
			RE::NiPoint3 out{ at.x - a_from.x, at.y - a_from.y, 0.0f };
			const float  length = std::hypot(out.x, out.y);
			out = length > 1.0f ? out * (Quake::kThrowOut / length) : RE::NiPoint3{};
			throws.push_back({ a_enemy->GetHandle(), out + RE::NiPoint3{ 0.0f, 0.0f, Quake::LaunchSpeed() }, 0.0f, 0, held + Quake::Airborne() });
			return true;
		}
		Knock(a_enemy, a_from, Quake::kFell);
		holds.push_back({ a_enemy->GetHandle(), kHoldAfterFall, held });
		return false;
	}

	void Tick(float a_dt)
	{
		for (auto& slam : slams) {
			slam.age += a_dt;
			if (!slam.found) {
				Find(slam);
			}
			if (!slam.crumbled && slam.age >= Quake::Seconds(slam.grand) - kCrumbleAt) {
				slam.crumbled = true;
				Posing::PlayAt(Forms::Get().quakeCrumble, slam.at);
			}
			Pose(slam);
		}
		std::erase_if(slams, [](Shake& a_slam) {
			const bool gone = !a_slam.effect->cell || !a_slam.effect->cell->IsAttached();
			if (a_slam.age < Quake::Seconds(a_slam.grand) && !gone && Settings::Get().abilityEffects) {
				return false;
			}
			a_slam.effect->lifetime = 0.0f;
			return true;
		});
		// The thrown: once their bodies have gone loose, sent up, then held down until well after they land.
		std::erase_if(throws, [a_dt](Throw& a_throw) {
			auto enemy = a_throw.enemy.get();
			a_throw.waited += a_dt;
			if (!enemy || enemy->IsDead() || a_throw.waited > kLaunchWait) {
				return true;
			}
			if (!enemy->IsInRagdollState()) {
				return false;
			}
			Push(enemy.get(), a_throw.velocity);
			if (++a_throw.pushes < 2) {
				return false;
			}
			holds.push_back({ a_throw.enemy, 0.1f, a_throw.held });
			return true;
		});
		std::erase_if(holds, [a_dt](Hold& a_hold) {
			a_hold.in -= a_dt;
			if (a_hold.in > 0.0f) {
				return false;
			}
			if (auto enemy = a_hold.enemy.get(); enemy && !enemy->IsDead()) {
				Abilities::Lift(enemy->GetFormID(), RE::ActorValue::kParalysis, 1.0f, a_hold.seconds, kSource);
			}
			return true;
		});
	}

	void Clear()
	{
		for (auto& slam : slams) {
			slam.effect->lifetime = 0.0f;
		}
		slams.clear();
		throws.clear();
		holds.clear();
	}
}
