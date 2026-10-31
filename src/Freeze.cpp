#include "FWM/Freeze.h"
#include "FWM/Skald.h"
#include "FWM/Windsteps.h"
#include "FWM/FreezeClock.h"
#include "FWM/FreezeMotion.h"

#include "FWM/Log.h"

namespace FWM::Freeze
{
	namespace
	{
		struct Holding
		{
			RE::ActorHandle actor;
			RE::FormID      id{ 0 };
			Clock           clock;
			RE::NiPoint3     caughtAt;
			float           drift{ 0.0f };
			bool            caught{ false };
            std::uint32_t owner{0};
		};
		std::vector<Holding> holds;  // main thread only
        std::vector<Holding> anchors;

		// Who is held still now, for the animation update, which may run on any thread.
		std::shared_mutex              stillLock;
		std::unordered_set<RE::FormID> still;
		// Keep the actual controllers alive until their snapshot is replaced. The hooks only compare
		// their addresses; actors and their current controllers are resolved on the main thread.
		using Controllers = std::unordered_map<const RE::bhkCharacterController*, RE::NiPointer<RE::bhkCharacterController>>;
		Controllers                    controllers;
		std::atomic<bool>              anyone{ false };  // so everyone else's update never takes the lock
		std::atomic<std::uint64_t>     animationSteps{ 0 };
		std::atomic<std::uint64_t>     movementSteps{ 0 };

		bool IsStill(RE::FormID a_id)
		{
			const std::shared_lock guard(stillLock);
			return still.contains(a_id);
		}

		bool IsStill(const RE::bhkCharacterController* a_controller)
		{
			const std::shared_lock guard(stillLock);
			return controllers.contains(a_controller);
		}

		bool Usable(RE::Actor* a_actor)
		{
			return a_actor && !a_actor->IsDead() && !a_actor->IsDisabled() && a_actor->Is3DLoaded() &&
				!a_actor->AsActorState()->IsFlying() && !a_actor->IsInRagdollState();
		}

		void Publish()
		{
			std::unordered_set<RE::FormID> now;
			Controllers currentControllers;
			for (const auto& hold : holds) {
				if (auto actor = hold.actor.get(); hold.clock.Active() && Usable(actor.get())) {
					now.insert(hold.id);
					if (auto* controller = actor->GetCharController()) {
						currentControllers.emplace(controller, RE::NiPointer<RE::bhkCharacterController>(controller));
					}
				}
			}
            for (const auto& anchor : anchors) {
                if (auto actor=anchor.actor.get(); Usable(actor.get())) {
                    if (auto* controller=actor->GetCharController()) currentControllers.emplace(controller,RE::NiPointer<RE::bhkCharacterController>(controller));
                }
            }
			{
				const std::unique_lock guard(stillLock);
				still.swap(now);
				controllers.swap(currentControllers);
				anyone = !still.empty() || !controllers.empty();
			}
			// Any old controller references are released after the snapshot lock, never inside it.
		}

		RE::hkVector4 StopTravel(const RE::hkVector4& a_velocity)
		{
			std::array<float, 4> lanes;
			_mm_storeu_ps(lanes.data(), a_velocity.quad);
			const auto stopped = MovementStep(lanes, true);
			return { stopped[0], stopped[1], stopped[2], stopped[3] };
		}

		// Animation timing and Havok movement are independent: freezing a stagger's pose alone leaves
		// its previous controller velocity alive. Remove that velocity as soon as the catch becomes
		// active, then each frame in case an external impulse bypassed the normal velocity setter.
		void StopCarriedMotion(Holding& a_hold, RE::Actor* a_actor)
		{
			const RE::NiPointer<RE::bhkCharacterController> controller(a_actor->GetCharController());
			auto* world = controller ? controller->GetHavokWorld() : nullptr;
			if (!world) return;
			float speed = 0.0f;
			{
				RE::BSWriteLockGuard lock(world->worldLock);
				RE::hkVector4 velocity;
				controller->GetLinearVelocityImpl(velocity);
				std::array<float, 4> lanes;
				_mm_storeu_ps(lanes.data(), velocity.quad);
				speed = std::hypot(lanes[0], lanes[1]) * RE::bhkWorld::GetWorldScaleInverse();
				controller->SetLinearVelocityImpl(StopTravel(velocity));
			}
			const auto at = a_actor->GetPosition();
			if (!a_hold.caught) {
				a_hold.caught = true;
				a_hold.caughtAt = at;
				Log::Info("[Maw] Movement caught for {:08X}: incoming horizontal speed {:.2f}, {:.2f} s left."sv, a_hold.id, speed, a_hold.clock.left);
			} else {
				a_hold.drift = std::max(a_hold.drift, std::hypot(at.x - a_hold.caughtAt.x, at.y - a_hold.caughtAt.y));
			}
		}

		template <int Route>
		struct UpdateAnimation
		{
			static void Thunk(RE::Actor* a_this, float a_delta)
			{
				if (a_this && anyone && IsStill(a_this->GetFormID())) {
					a_delta = AnimationStep(a_delta, true);
					animationSteps.fetch_add(1, std::memory_order_relaxed);
				}
				original(a_this, a_delta);
				Skald::AfterAnimation(a_this);  // a walking bard's playing arms go over the new pose
			}

			static inline REL::Relocation<decltype(Thunk)> original;
		};

		template <int Route>
		struct SetVelocity
		{
			static void Thunk(RE::bhkCharacterController* a_this, const RE::hkVector4& a_velocity)
			{
				if (a_this && anyone && IsStill(a_this)) {
					movementSteps.fetch_add(1, std::memory_order_relaxed);
					// Chain the existing setter in its own engine update context. No actor lookup, world
					// lock or raw physics-field writes occur here; the caller owns that update's lock.
					original(a_this, StopTravel(a_velocity));
				} else {
					original(a_this, Windsteps::Velocity(a_this, a_velocity));
				}
			}
			static inline REL::Relocation<decltype(Thunk)> original;
		};
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_Character[0] };
		UpdateAnimation<0>::original = vtable.write_vfunc(0x7D, UpdateAnimation<0>::Thunk);
		// CommonLib's bhkCharacterController declares SetLinearVelocityImpl at 07. The proxy's
		// controller is its second base/vtable; the rigid-body controller is its first. This also
		// catches new movement written after our main-thread tick, including stagger root motion.
		REL::Relocation<std::uintptr_t> proxyVtable{ RE::VTABLE_bhkCharProxyController[1] };
		REL::Relocation<std::uintptr_t> rigidVtable{ RE::VTABLE_bhkCharRigidBodyController[0] };
		SetVelocity<0>::original = proxyVtable.write_vfunc(0x07, SetVelocity<0>::Thunk);
		SetVelocity<1>::original = rigidVtable.write_vfunc(0x07, SetVelocity<1>::Thunk);
		Log::Info("[Hook] Maw hold: proxy and rigid-body horizontal velocity routes installed; gravity preserved."sv);
		// NPC animation jobs can call the update directly, bypassing Character's virtual slot.
		// RunOneActorAnimationUpdateJob's SE/AE call is also used by Stop On Slash's NPC hook:
		// https://github.com/D7ry/StopOnSlash/blob/NPC-Hitstop/hitStop.h
		// Chain the existing call target so other animation-speed plugins still run.
		if (!REL::Module::IsVR()) {
			const auto site = REL::Relocation<std::uintptr_t>{ RELOCATION_ID(40436, 41453) }.address() + 0x74;
			if (*reinterpret_cast<const std::uint8_t*>(site) == 0xE8) {
				SKSE::AllocTrampoline(14);
				UpdateAnimation<1>::original = SKSE::GetTrampoline().write_call<5>(site, UpdateAnimation<1>::Thunk);
				Log::Info("[Hook] Maw hold: character and background NPC animation routes installed."sv);
			} else {
				Log::Warn("[Hook] Maw background animation call was not recognized; virtual route only."sv);
			}
		}
	}

	void Hold(RE::Actor* a_actor, float a_after, float a_seconds)
	{
		if (!Usable(a_actor) || !std::isfinite(a_after) || !std::isfinite(a_seconds) || a_seconds <= 0.0f) {
			return;
		}
		const auto id = a_actor->GetFormID();
		if (const auto held = std::ranges::find(holds, id, &Holding::id); held != holds.end()) {
			held->clock.Extend(a_after, a_seconds);
			Publish();
			if (held->clock.Active()) StopCarriedMotion(*held, a_actor);
			return;
		}
		holds.push_back({ a_actor->GetHandle(), id, { std::max(a_after, 0.0f), a_seconds } });
		Publish();
		if (holds.back().clock.Active()) StopCarriedMotion(holds.back(), a_actor);
		Log::Info("[Maw] Hold queued for {:08X}: catch after {:.2f} s, hold {:.2f} s."sv, id, a_after, a_seconds);
	}

	bool Held(RE::Actor* a_actor) { return a_actor && IsStill(a_actor->GetFormID()); }

    void Anchor(RE::Actor* actor, std::uint32_t owner) {
        if(!Usable(actor))return;
        if(std::ranges::none_of(anchors,[&](const auto& h){return h.id==actor->GetFormID() && h.owner==owner;})) {
            Holding anchor;anchor.actor=actor->GetHandle();anchor.id=actor->GetFormID();anchor.owner=owner;anchors.push_back(anchor);
        }
        Publish();
        for(auto& anchor:anchors)if(anchor.id==actor->GetFormID())StopCarriedMotion(anchor,actor);
    }
    void ReleaseAnchor(RE::Actor* actor, std::uint32_t owner) {
        if(!actor)return;
        std::erase_if(anchors,[&](const auto& h){return h.id==actor->GetFormID() && h.owner==owner;});Publish();
    }

	void Tick(float a_dt)
	{
		if (holds.empty() && anchors.empty()) {
			return;
		}
		for (auto& hold : holds) {
			hold.clock.Tick(a_dt);
			// The dead and the gone go at once: a corpse must fall as the game drops it.
			auto actor = hold.actor.get();
			if (!Usable(actor.get())) {
				hold.clock.left = 0.0f;
			}
		}
		std::erase_if(holds, [](const Holding& a_hold) {
			if (a_hold.clock.left > 0.0f) return false;
			if (a_hold.caught) Log::Info("[Maw] Released {:08X}: maximum horizontal drift while held {:.2f}."sv, a_hold.id, a_hold.drift);
			return true;
		});
        std::erase_if(anchors,[](const auto& h){return !Usable(h.actor.get().get());});
		Publish();
        for(auto& anchor:anchors)if(auto actor=anchor.actor.get())StopCarriedMotion(anchor,actor.get());
		for (auto& hold : holds) {
			if (auto actor = hold.actor.get(); hold.clock.Active() && Usable(actor.get())) StopCarriedMotion(hold, actor.get());
		}
		if (holds.empty() && anchors.empty()) Log::Info("[Maw] All caught actors released; {} animation updates and {} horizontal velocity writes held."sv,
			animationSteps.exchange(0), movementSteps.exchange(0));
	}

	void Clear()
	{
		holds.clear();
        anchors.clear();
		Publish();
		animationSteps.store(0);
		movementSteps.store(0);
	}
}
