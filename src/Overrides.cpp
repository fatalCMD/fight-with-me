#include "FWM/Overrides.h"

#include "FWM/Game.h"
#include "FWM/Log.h"

#ifdef GetObject
#	undef GetObject  // the Windows macro, not the default object lookup
#endif

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

		std::atomic<std::shared_ptr<const std::vector<HandsEntry>>> chosenHands;

		std::atomic<std::uint64_t> calls{ 0 }, matched{ 0 }, overridden{ 0 }, rejected{ 0 }, bound{ 0 }, kept{ 0 }, rested{ 0 }, melee{ 0 }, skill{ 0 };

		// The two hand slots, read once the data has loaded.
		const RE::BGSEquipSlot* leftSlot{ nullptr };
		const RE::BGSEquipSlot* rightSlot{ nullptr };

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

		// Which hands an item takes. The combat inventory keeps one item per hand, that hand in
		// itemSlot.equipSlot, and the AI equips through it (46958 turns it into left 0 or right
		// 1). A shield's slot has the left hand as its parent; two-handed things have both
		// parents and use all of them; "either hand" has both and uses one.
		enum class Takes : std::uint8_t
		{
			kLeft,
			kRight,
			kBoth,
			kEither
		};

		Takes TakesOf(const RE::BGSEquipSlot* a_slot)
		{
			if (a_slot == leftSlot) {
				return Takes::kLeft;
			}
			if (a_slot == rightSlot) {
				return Takes::kRight;
			}
			if (a_slot) {
				const auto& parents = a_slot->parentSlots;
				const bool  left = std::find(parents.begin(), parents.end(), leftSlot) != parents.end();
				const bool  right = std::find(parents.begin(), parents.end(), rightSlot) != parents.end();
				if (left != right) {
					return left ? Takes::kLeft : Takes::kRight;
				}
				if (left && !a_slot->flags.all(RE::BGSEquipSlot::Flag::kUseAllParents)) {
					return Takes::kEither;
				}
			}
			return Takes::kBoth;  // two hands, or a slot we do not know: strict
		}

		bool HandAllows(const RE::CombatInventoryItem* a_item, const Hands& a_hands)
		{
			if (a_hands == Hands{}) {
				return true;
			}
			const auto id = a_item->item ? a_item->item->GetFormID() : 0;
			auto       fits = [&](std::size_t a_hand) { return !a_hands[a_hand] || a_hands[a_hand] == id; };
			switch (TakesOf(a_item->itemSlot.equipSlot)) {
			case Takes::kLeft:
				return fits(kLeftHand);
			case Takes::kRight:
				return fits(kRightHand);
			case Takes::kEither:
				return fits(kLeftHand) || fits(kRightHand);
			default:
				return fits(kLeftHand) && fits(kRightHand);
			}
		}

		// By the actor the controller fights for, so it holds from the first pick of a fight. On
		// the way, notes what the combat AI made of a chosen spell (a_caster: its kind of caster).
		bool KeepsHands(RE::CombatInventoryItem* a_item, RE::CombatController* a_controller, std::size_t a_caster)
		{
			const auto table = chosenHands.load(std::memory_order_acquire);
			if (!table || table->empty() || !a_controller) {
				return true;
			}
			const auto actor = a_controller->attackerHandle.native_handle();
			for (const auto& entry : *table) {
				if (entry.actor == actor) {
					if (entry.seen) {
						entry.seen->looked.store(true, std::memory_order_relaxed);
						const auto id = a_item->item ? a_item->item->GetFormID() : 0;
						for (const auto hand : { kLeftHand, kRightHand }) {
							if (id && entry.hands[hand] == id) {
								entry.seen->casters[hand].fetch_or(static_cast<std::uint16_t>(1u << a_caster), std::memory_order_relaxed);
							}
						}
					}
					if (HandAllows(a_item, entry.hands)) {
						return true;
					}
					kept.fetch_add(1, std::memory_order_relaxed);
					return false;
				}
			}
			return true;
		}

		// Before the combat AI takes a spell into its inventory, a CombatMagicItemSkillChecker
		// (43936) walks its effects (43937) and keeps it only if the caster's skill in each
		// effect's school is at least that effect's minimum. A follower taught Firebolt with
		// Destruction below 25 never has it to cast, however it is held. A spell chosen for a
		// mage's or healer's hand passes anyway: it was chosen, or taught, to be cast.
		struct SkillCheck
		{
			struct Checker
			{
				void*          vtable;
				std::uint64_t  pad08;
				RE::Actor*     actor;  // 10
				RE::MagicItem* spell;  // 18
				bool           keep;   // 20
			};
			static_assert(offsetof(Checker, actor) == 0x10 && offsetof(Checker, spell) == 0x18 && offsetof(Checker, keep) == 0x20);

			static bool Chosen(const RE::Actor* a_actor, const RE::MagicItem* a_spell)
			{
				const auto table = chosenHands.load(std::memory_order_acquire);
				if (!table || !a_actor || !a_spell) {
					return false;
				}
				const auto id = a_spell->GetFormID();
				for (const auto& entry : *table) {
					if (entry.body == a_actor) {
						return entry.hands[kLeftHand] == id || entry.hands[kRightHand] == id;
					}
				}
				return false;
			}

			// CombatMagicItemSkillChecker slot 1, once per effect.
			static RE::BSContainer::ForEachResult Thunk(Checker* a_this, RE::Effect* a_effect)
			{
				const auto result = original(a_this, a_effect);
				if (!a_this->keep && Chosen(a_this->actor, a_this->spell)) {
					a_this->keep = true;
					skill.fetch_add(1, std::memory_order_relaxed);
					return RE::BSContainer::ForEachResult::kContinue;
				}
				return result;
			}

			static inline REL::Relocation<decltype(Thunk)> original;
		};

		// A mage or healer given a spell for a hand casts that one from it and no other spell,
		// staff or scroll. A hand left to them is theirs to fill, and weapons stay theirs: when
		// the spell cannot be cast (no magicka, nothing to heal) a weapon is what keeps them
		// fighting rather than backing away with nothing. Whether the spell is worth casting
		// right now stays the combat AI's call.
		template <std::size_t I>
		struct HandShouldEquip
		{
			// CombatInventoryItem slot 0x0F
			static bool Thunk(RE::CombatInventoryItem* a_item, RE::CombatController* a_controller)
			{
				// The classes run spell, scroll, staff, each through every kind of caster in order.
				return KeepsHands(a_item, a_controller, I % static_cast<std::size_t>(Caster::kCount)) && original(a_item, a_controller);
			}

			static inline REL::Relocation<decltype(Thunk)> original;
		};

		// Every class the combat AI wraps a spell, a scroll or a staff in: one per kind of caster.
		constexpr std::array<REL::VariantID, 45> handItems{ {
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemMagic_CombatMagicCasterArmor_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemMagic_CombatMagicCasterBoundItem_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemMagic_CombatMagicCasterCloak_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemMagic_CombatMagicCasterDisarm_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemMagic_CombatMagicCasterInvisibility_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemMagic_CombatMagicCasterLight_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemMagic_CombatMagicCasterOffensive_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemMagic_CombatMagicCasterParalyze_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemMagic_CombatMagicCasterReanimate_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemMagic_CombatMagicCasterRestore_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemMagic_CombatMagicCasterScript_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemMagic_CombatMagicCasterStagger_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemMagic_CombatMagicCasterSummon_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemMagic_CombatMagicCasterTargetEffect_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemMagic_CombatMagicCasterWard_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemScroll_CombatMagicCasterArmor_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemScroll_CombatMagicCasterBoundItem_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemScroll_CombatMagicCasterCloak_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemScroll_CombatMagicCasterDisarm_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemScroll_CombatMagicCasterInvisibility_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemScroll_CombatMagicCasterLight_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemScroll_CombatMagicCasterOffensive_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemScroll_CombatMagicCasterParalyze_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemScroll_CombatMagicCasterReanimate_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemScroll_CombatMagicCasterRestore_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemScroll_CombatMagicCasterScript_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemScroll_CombatMagicCasterStagger_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemScroll_CombatMagicCasterSummon_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemScroll_CombatMagicCasterTargetEffect_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemScroll_CombatMagicCasterWard_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterArmor_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterBoundItem_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterCloak_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterDisarm_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterInvisibility_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterLight_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterOffensive_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterParalyze_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterReanimate_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterRestore_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterScript_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterStagger_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterSummon_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterTargetEffect_[0],
			RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterWard_[0],
		} };

		template <std::size_t... I>
		void InstallHands(std::index_sequence<I...>)
		{
			((HandShouldEquip<I>::original = REL::Relocation<std::uintptr_t>{ handItems[I] }.write_vfunc(0x0F, HandShouldEquip<I>::Thunk)), ...);
		}

		// A mage or healer out of magicka is to stand off and let it come back. Left any weapon,
		// their fists included, the combat AI walks them into the fight with it; left nothing it
		// can use, it keeps them at a distance (their style is built to fall back). Only while
		// resting, and only weapons: spells, staves, scrolls and potions are untouched.
		template <std::size_t I>
		struct WeaponShouldEquip
		{
			// CombatInventoryItem slot 0x0F
			static bool Thunk(RE::CombatInventoryItem* a_item, RE::CombatController* a_controller)
			{
				const auto  table = current.load(std::memory_order_acquire);
				const auto* entry = Listed(table.get(), a_controller);
				if (entry && entry->resting) {
					rested.fetch_add(1, std::memory_order_relaxed);
					return false;
				}
				// A mage or healer fights with spells: no blade, no bow, not their fists. A weapon
				// they conjured is magic too.
				if (entry && entry->noMelee) {
					const auto* weapon = a_item->item ? a_item->item->As<RE::TESObjectWEAP>() : nullptr;
					if (!weapon || !weapon->IsBound()) {
						melee.fetch_add(1, std::memory_order_relaxed);
						return false;
					}
				}
				return original(a_item, a_controller);
			}

			static inline REL::Relocation<decltype(Thunk)> original;
		};

		constexpr std::array<REL::VariantID, 6> weaponItems{ {
			RE::VTABLE_CombatInventoryItemMelee[0],
			RE::VTABLE_CombatInventoryItemRanged[0],
			RE::VTABLE_CombatInventoryItemOneHandedBlock[0],
			RE::VTABLE_CombatInventoryItemShield[0],
			RE::VTABLE_CombatInventoryItemTorch[0],
			RE::VTABLE_CombatInventoryItemComposite[0],
		} };

		template <std::size_t... I>
		void InstallWeapons(std::index_sequence<I...>)
		{
			((WeaponShouldEquip<I>::original = REL::Relocation<std::uintptr_t>{ weaponItems[I] }.write_vfunc(0x0F, WeaponShouldEquip<I>::Thunk)), ...);
		}
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

	void PublishHands(std::vector<HandsEntry> a_entries)
	{
		const auto previous = chosenHands.load(std::memory_order_acquire);
		if (previous && previous->size() == a_entries.size() &&
			std::equal(a_entries.begin(), a_entries.end(), previous->begin(),
				[](const HandsEntry& a, const HandsEntry& b) { return a.actor == b.actor && a.body == b.body && a.hands == b.hands; })) {
			return;  // nothing changed: keep the one the hooks already have
		}
		chosenHands.store(std::make_shared<const std::vector<HandsEntry>>(std::move(a_entries)), std::memory_order_release);
	}

	std::optional<std::array<std::uint16_t, 2>> SeenHands(std::uint32_t a_actor)
	{
		const auto table = chosenHands.load(std::memory_order_acquire);
		if (!table) {
			return std::nullopt;
		}
		for (const auto& entry : *table) {
			if (entry.actor == a_actor && entry.seen && entry.seen->looked.load(std::memory_order_relaxed)) {
				return std::array<std::uint16_t, 2>{ entry.seen->casters[kLeftHand].load(std::memory_order_relaxed),
					entry.seen->casters[kRightHand].load(std::memory_order_relaxed) };
			}
		}
		return std::nullopt;
	}

	namespace
	{
		// One item as the log needs it, copied out so nothing of the AI's is touched afterwards.
		struct ItemFacts
		{
			std::uintptr_t          vtable{ 0 };
			RE::TESForm*            form{ nullptr };
			float                   score{ 0.0f };
			const RE::BGSEquipSlot* slot{ nullptr };
			std::uint8_t            category{ 0 };  // 0xFF: from the equipped list
		};

		// The combat AI runs on its own threads and may be rebuilding the inventory as this reads
		// it: plain copies only, under SEH. A bad read just ends the list.
		std::size_t ReadInventory(const RE::CombatInventory* a_inventory, ItemFacts* a_out, std::size_t a_max)
		{
			std::size_t count = 0;
			__try {
				for (std::uint8_t c = 0; c < 8; ++c) {
					const auto* list = c < 7 ? &a_inventory->inventoryItems[c] : nullptr;
					if (list) {
						for (std::uint32_t i = 0; i < list->size() && count < a_max; ++i) {
							const auto* item = (*list)[i].get();
							if (item) {
								a_out[count++] = { *reinterpret_cast<const std::uintptr_t*>(item), item->item, item->itemScore, item->itemSlot.equipSlot, c };
							}
						}
					} else {
						for (std::uint32_t i = 0; i < a_inventory->equippedItems.size() && count < a_max; ++i) {
							const auto* item = a_inventory->equippedItems[i].item.get();
							if (item) {
								a_out[count++] = { *reinterpret_cast<const std::uintptr_t*>(item), item->item, item->itemScore, item->itemSlot.equipSlot, 0xFF };
							}
						}
					}
				}
			} __except (EXCEPTION_EXECUTE_HANDLER) {
			}
			return count;
		}
	}

	std::string DescribeInventory(RE::CombatController* a_controller)
	{
		auto* inventory = a_controller ? a_controller->inventory : nullptr;
		if (!inventory) {
			return "no combat inventory";
		}
		std::array<ItemFacts, 96> facts{};
		const auto                read = ReadInventory(inventory, facts.data(), facts.size());
		auto kind = [](std::uintptr_t a_vtable) -> std::string {
			const auto                                       vtable = a_vtable;
			static constexpr std::array<std::string_view, 3> sources{ { "spell", "scroll", "staff" } };
			for (std::size_t i = 0; i < handItems.size(); ++i) {
				if (REL::Relocation<std::uintptr_t>{ handItems[i] }.address() == vtable) {
					return std::format("{} as {}", sources[i / static_cast<std::size_t>(Caster::kCount)], CasterName(static_cast<Caster>(i % static_cast<std::size_t>(Caster::kCount))));
				}
			}
			static constexpr std::array<std::string_view, 6> weapons{ { "melee", "ranged", "one-handed + block", "shield", "torch", "composite" } };
			for (std::size_t i = 0; i < weaponItems.size(); ++i) {
				if (REL::Relocation<std::uintptr_t>{ weaponItems[i] }.address() == vtable) {
					return std::string(weapons[i]);
				}
			}
			return "other";
		};
		auto hand = [](const RE::BGSEquipSlot* a_slot) {
			switch (TakesOf(a_slot)) {
			case Takes::kLeft:
				return "left"sv;
			case Takes::kRight:
				return "right"sv;
			case Takes::kEither:
				return "either"sv;
			default:
				return "both"sv;
			}
		};
		auto name = [](const RE::TESForm* a_form) { return a_form && a_form->GetName() && *a_form->GetName() ? std::string(a_form->GetName()) : "?"s; };
		std::string out, equipped;
		for (std::size_t i = 0; i < read; ++i) {
			const auto& f = facts[i];
			if (f.category == 0xFF) {
				equipped += std::format("{}{} ({})", equipped.empty() ? "" : ", ", name(f.form), kind(f.vtable));
			} else {
				out += std::format("{}[{}] {} ({}, {} hand, score {:.2f})", out.empty() ? "" : "; ", f.category, name(f.form), kind(f.vtable), hand(f.slot), f.score);
			}
		}
		return std::format("{}; equipped by the AI: {}", out.empty() ? "nothing"s : out, equipped.empty() ? "nothing"s : equipped);
	}

	std::string_view CasterName(Caster a_caster)
	{
		static constexpr std::array<std::string_view, static_cast<std::size_t>(Caster::kCount)> names{ {
			"armor on themselves", "a bound weapon", "a cloak", "disarming", "invisibility", "light", "an attack", "paralysis", "raising the dead",
			"healing", "a scripted spell", "a stagger", "a summons", "an effect on their target", "a ward" } };
		const auto i = static_cast<std::size_t>(a_caster);
		return i < names.size() ? names[i] : "something else"sv;
	}

	std::uint32_t EngineChoice(std::uintptr_t a_controller)
	{
		const auto table = current.load(std::memory_order_acquire);
		const auto* entry = table ? table->Find(a_controller) : nullptr;
		return entry ? entry->engine.load(std::memory_order_relaxed) : 0;
	}

	bool Takes(RE::CombatController* a_controller, RE::ActorHandle a_target)
	{
		return a_controller && a_target && Valid(a_controller, a_target.native_handle());
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

		// After the bound-weapon checks, which these call on through for those classes. A bound
		// spell chosen for a hand lifts the bound-weapon rule for that companion (Battle).
		if (auto* defaults = RE::BGSDefaultObjectManager::GetSingleton()) {
			leftSlot = defaults->GetObject<RE::BGSEquipSlot>(RE::DEFAULT_OBJECTS::kLeftHandEquip);
			rightSlot = defaults->GetObject<RE::BGSEquipSlot>(RE::DEFAULT_OBJECTS::kRightHandEquip);
		}
		REL::Relocation<std::uintptr_t> checker{ RE::VTABLE_CombatMagicItemSkillChecker[0] };
		SkillCheck::original = checker.write_vfunc(1, SkillCheck::Thunk);
		InstallHands(std::make_index_sequence<handItems.size()>{});
		Log::Info("[Hook] Chosen spells kept to their hands ({} combat item classes) and let past the skill check for casting them{}."sv, handItems.size(),
			leftSlot && rightSlot ? ""sv : "; the hand slots were not found, so a chosen spell may land in either hand"sv);

		InstallWeapons(std::make_index_sequence<weaponItems.size()>{});
		Log::Info("[Hook] Weapons kept from casters resting their magicka, and every weapon but a conjured one from mages and healers ({} combat item classes)."sv, weaponItems.size());
	}

	Stats TakeStats()
	{
		return { calls.exchange(0), matched.exchange(0), overridden.exchange(0), rejected.exchange(0), bound.exchange(0), kept.exchange(0), rested.exchange(0), melee.exchange(0), skill.exchange(0) };
	}
}
