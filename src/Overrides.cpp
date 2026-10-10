#include "FWM/Overrides.h"

#include "FWM/Game.h"
#include "FWM/Log.h"

namespace FWM::Overrides
{
	namespace
	{
		// CombatTargetSelector, read from the 1.5.97 constructors (45917, 45924):
		//   +0x10 CombatController*  +0x18 ActorHandle target  +0x1C priority  +0x20 flags
		// CombatController::UpdateTarget (32489) sorts the selectors by priority and
		// takes slot 6 of the first enabled, valid one as the controller's target.
		constexpr std::ptrdiff_t kControllerOffset = 0x10;

		std::atomic<std::shared_ptr<Table>> current;

		std::atomic<std::uint64_t> calls{ 0 }, matched{ 0 }, overridden{ 0 }, rejected{ 0 }, bound{ 0 };

		// The same test the engine applies to its cached target (id 43530 / 43484):
		// alive, enabled, not deleted, and known to the controller's combat group.
		bool Valid(RE::CombatController* a_controller, std::uint32_t a_target)
		{
			const auto handle = Game::FromNative(a_target);
			auto       actor = handle.get();
			if (!actor || actor->IsDead() || actor->IsDisabled() || actor->IsMarkedForDeletion()) {
				return false;
			}
			auto* group = a_controller->combatGroup;
			if (!group) {
				return false;
			}
			RE::BSReadLockGuard lock(group->lock);
			for (const auto& target : group->targets) {
				if (target.targetHandle == handle) {
					return true;
				}
			}
			return false;
		}

		struct SelectTarget
		{
			// Slot 6 of CombatTargetSelectorStandard: writes the current target to a_out.
			static RE::ActorHandle* Thunk(void* a_selector, RE::ActorHandle* a_out)
			{
				auto* result = original(a_selector, a_out);
				calls.fetch_add(1, std::memory_order_relaxed);

				const auto table = current.load(std::memory_order_acquire);
				if (!table || table->entries.empty() || !a_out) {
					return result;
				}
				const auto  address = reinterpret_cast<std::uintptr_t>(a_selector);
				const auto  controllerAddress = *reinterpret_cast<const std::uintptr_t*>(address + kControllerOffset);
				const auto* entry = table->Find(controllerAddress);
				if (!entry) {
					return result;
				}
				auto* controller = reinterpret_cast<RE::CombatController*>(controllerAddress);
				if (controller->attackerHandle.native_handle() != entry->attacker) {
					return result;  // a recycled controller
				}
				matched.fetch_add(1, std::memory_order_relaxed);

				const auto chosen = a_out->native_handle();
				entry->engine.store(chosen, std::memory_order_relaxed);
				if (!entry->target || entry->target == chosen) {
					return result;
				}
				if (!Valid(controller, entry->target)) {
					rejected.fetch_add(1, std::memory_order_relaxed);
					return result;
				}

				// The selector's own cache (+0x18) is left alone: it keeps the engine's
				// pick and its reselect timing, and the override is reapplied on each call.
				*a_out = Game::FromNative(entry->target);
				overridden.fetch_add(1, std::memory_order_relaxed);
				return result;
			}

			static inline REL::Relocation<decltype(Thunk)> original;
		};

		// The table entry for a controller, if it is still the companion it was listed for.
		const Entry* Listed(const Table* a_table, RE::CombatController* a_controller)
		{
			if (!a_table || !a_controller) {
				return nullptr;
			}
			const auto* entry = a_table->Find(reinterpret_cast<std::uintptr_t>(a_controller));
			return entry && a_controller->attackerHandle.native_handle() == entry->attacker ? entry : nullptr;
		}

		bool NoBound(RE::CombatController* a_controller)
		{
			const auto  table = current.load(std::memory_order_acquire);
			const auto* entry = Listed(table.get(), a_controller);
			if (entry && entry->noBound) {
				bound.fetch_add(1, std::memory_order_relaxed);
				return true;
			}
			return false;
		}

		// Bound weapons are for companions told to fight up close. A mage, healer or
		// archer who conjures a sword walks into melee, which is what their role is
		// there to avoid. Two checks, both on the bound-weapon classes only: whether the
		// spell (or scroll, or staff) is worth putting in a hand, and whether to start
		// casting it, for one that is already there.
		template <std::size_t I>
		struct BoundShouldEquip
		{
			// CombatInventoryItem slot 0x0F
			static bool Thunk(RE::CombatInventoryItem* a_item, RE::CombatController* a_controller)
			{
				return !NoBound(a_controller) && original(a_item, a_controller);
			}

			static inline REL::Relocation<decltype(Thunk)> original;
		};

		struct BoundStartCast
		{
			// CombatMagicCaster slot 0x06
			static bool Thunk(RE::CombatMagicCasterBoundItem* a_caster, RE::CombatController* a_controller)
			{
				return !NoBound(a_controller) && original(a_caster, a_controller);
			}

			static inline REL::Relocation<decltype(Thunk)> original;
		};
	}

	const Entry* Table::Find(std::uintptr_t a_controller) const
	{
		const auto it = std::lower_bound(entries.begin(), entries.end(), a_controller,
			[](const Entry& a_entry, std::uintptr_t a_key) { return a_entry.controller < a_key; });
		return it != entries.end() && it->controller == a_controller ? &*it : nullptr;
	}

	void Publish(std::vector<Entry> a_entries)
	{
		std::sort(a_entries.begin(), a_entries.end(), [](const Entry& a, const Entry& b) { return a.controller < b.controller; });
		// Carry the engine's last pick across so the planner never sees a gap.
		if (const auto previous = current.load(std::memory_order_acquire)) {
			for (auto& entry : a_entries) {
				if (const auto* old = previous->Find(entry.controller); old && old->attacker == entry.attacker) {
					entry.engine.store(old->engine.load(std::memory_order_relaxed), std::memory_order_relaxed);
				}
			}
		}
		auto table = std::make_shared<Table>();
		table->entries = std::move(a_entries);
		current.store(std::move(table), std::memory_order_release);
	}

	std::uint32_t EngineChoice(std::uintptr_t a_controller)
	{
		const auto table = current.load(std::memory_order_acquire);
		const auto* entry = table ? table->Find(a_controller) : nullptr;
		return entry ? entry->engine.load(std::memory_order_relaxed) : 0;
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_CombatTargetSelectorStandard[0] };
		SelectTarget::original = vtable.write_vfunc(6, SelectTarget::Thunk);
		Log::Info("[Hook] Combat target selector installed."sv);

		// Bound weapons as a spell, a scroll and a staff each have their own class.
		REL::Relocation<std::uintptr_t> spell{ RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemMagic_CombatMagicCasterBoundItem_[0] };
		REL::Relocation<std::uintptr_t> scroll{ RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemScroll_CombatMagicCasterBoundItem_[0] };
		REL::Relocation<std::uintptr_t> staff{ RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterBoundItem_[0] };
		REL::Relocation<std::uintptr_t> caster{ RE::VTABLE_CombatMagicCasterBoundItem[0] };
		BoundShouldEquip<0>::original = spell.write_vfunc(0x0F, BoundShouldEquip<0>::Thunk);
		BoundShouldEquip<1>::original = scroll.write_vfunc(0x0F, BoundShouldEquip<1>::Thunk);
		BoundShouldEquip<2>::original = staff.write_vfunc(0x0F, BoundShouldEquip<2>::Thunk);
		BoundStartCast::original = caster.write_vfunc(0x06, BoundStartCast::Thunk);
		Log::Info("[Hook] Bound weapons kept for close-combat roles."sv);
	}

	Stats TakeStats()
	{
		return { calls.exchange(0), matched.exchange(0), overridden.exchange(0), rejected.exchange(0), bound.exchange(0) };
	}
}
