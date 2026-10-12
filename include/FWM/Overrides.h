#pragma once

// Who should be fighting whom, and with what, handed from the planner (main
// thread) to the combat AI hooks (possibly worker threads): the target selector,
// and the checks that let a companion conjure a bound weapon.
//
// Entries are keyed by combat controller. The planner only lists controllers it
// read from live actors, and the hook only dereferences a selector's controller
// once it has matched one of those, so an unexpected selector layout can never
// make it chase a bad pointer.

namespace FWM::Overrides
{
	struct Entry
	{
		std::uintptr_t controller{ 0 };
		std::uint32_t  attacker{ 0 };  // native handle; must still match the controller
		std::uint32_t  target{ 0 };    // native handle; 0 = only watch the engine's choice
		bool           noBound{ false };  // may not conjure a bound weapon (a ranged role)

		// Written by the hook: what the combat AI chose before we looked.
		mutable std::atomic<std::uint32_t> engine{ 0 };

		Entry() = default;
		Entry(std::uintptr_t a_controller, std::uint32_t a_attacker, std::uint32_t a_target, bool a_noBound = false) :
			controller(a_controller), attacker(a_attacker), target(a_target), noBound(a_noBound) {}
		Entry(const Entry& a_other) :
			controller(a_other.controller), attacker(a_other.attacker), target(a_other.target), noBound(a_other.noBound),
			engine(a_other.engine.load(std::memory_order_relaxed)) {}
		Entry& operator=(const Entry& a_other)
		{
			controller = a_other.controller;
			attacker = a_other.attacker;
			target = a_other.target;
			noBound = a_other.noBound;
			engine.store(a_other.engine.load(std::memory_order_relaxed), std::memory_order_relaxed);
			return *this;
		}
	};

	struct Table
	{
		std::vector<Entry> entries;  // sorted by controller

		[[nodiscard]] const Entry* Find(std::uintptr_t a_controller) const;
	};

	// Replaces the table. Pass an empty one to stand down.
	void Publish(std::vector<Entry> a_entries);

	// The combat AI's own pick for a controller in the current table, or 0.
	[[nodiscard]] std::uint32_t EngineChoice(std::uintptr_t a_controller);

	// Whether the combat AI would accept a_target from this controller: alive, enabled,
	// and known to its combat group. An override that fails this is ignored.
	[[nodiscard]] bool Takes(RE::CombatController* a_controller, RE::ActorHandle a_target);

	void Install();

	struct Stats
	{
		std::uint64_t calls{ 0 };
		std::uint64_t matched{ 0 };
		std::uint64_t overridden{ 0 };
		std::uint64_t rejected{ 0 };  // override failed the engine's own validity test
		std::uint64_t bound{ 0 };     // bound weapons a ranged companion was kept from conjuring
	};

	[[nodiscard]] Stats TakeStats();
}
