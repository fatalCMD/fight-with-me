#pragma once

#include "FWM/Orders.h"
#include "FWM/LoadoutPolicy.h"

// Who should be fighting whom, and with what, handed from the planner (main
// thread) to the combat AI hooks (possibly worker threads): the target selector,
// the checks that let a companion conjure a bound weapon, and which spell each of a
// mage's or healer's hands may hold.
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
		bool           resting{ false };  // a caster out of magicka: no weapon, not even fists, so they stand off
		bool           noMelee{ false };  // a mage or healer: spells, not a blade, a bow or fists (a conjured weapon is still theirs)
		// Written by the hook: what the combat AI chose before we looked.
		mutable std::atomic<std::uint32_t> engine{ 0 };

		Entry() = default;
		Entry(std::uintptr_t a_controller, std::uint32_t a_attacker, std::uint32_t a_target, bool a_noBound = false, bool a_resting = false, bool a_noMelee = false) :
			controller(a_controller), attacker(a_attacker), target(a_target), noBound(a_noBound), resting(a_resting), noMelee(a_noMelee) {}
		Entry(const Entry& a_other) :
			controller(a_other.controller), attacker(a_other.attacker), target(a_other.target), noBound(a_other.noBound), resting(a_other.resting), noMelee(a_other.noMelee),
			engine(a_other.engine.load(std::memory_order_relaxed)) {}
		Entry& operator=(const Entry& a_other)
		{
			controller = a_other.controller;
			attacker = a_other.attacker;
			target = a_other.target;
			noBound = a_other.noBound;
			resting = a_other.resting;
			noMelee = a_other.noMelee;
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

	// The spells chosen for mages' and healers' hands, by actor (native handle), kept up in
	// and out of a fight: the combat AI picks its gear the moment a fight begins, before it
	// has a controller in the table above, and is held to these from that first pick.
	// The kinds of caster the combat AI wraps a spell in, in the order of its classes. Only
	// kOffensive is a spell it attacks with; the rest it casts on itself, an ally or the ground,
	// or now and then at an enemy for an effect (stagger, paralysis, fear and the like).
	enum class Caster : std::uint8_t
	{
		kArmor,
		kBoundItem,
		kCloak,
		kDisarm,
		kInvisibility,
		kLight,
		kOffensive,
		kParalyze,
		kReanimate,
		kRestore,
		kScript,
		kStagger,
		kSummon,
		kTargetEffect,
		kWard,
		kCount
	};
	[[nodiscard]] std::string_view CasterName(Caster a_caster);

	struct HandsEntry
	{
		std::uint32_t actor{ 0 };
		const RE::Actor* body{ nullptr };  // compared, never followed: for hooks on the AI's threads
		Hands         hands{};
        LoadoutPolicy::Pools pools;
        bool twoHandedOnly{false};
        bool battleLocked{false};
        bool staffLocked{false};
        bool roleRestricted{false};
        bool caster{false};
        bool recovering{false};
        std::array<bool,2> explicitSpells{};

		// Written by the hand hook (any thread): what the combat AI made of each chosen spell, as
		// bits of Caster, and whether it has looked at this companion's magic at all yet.
		struct Seen
		{
			std::array<std::atomic<std::uint16_t>, 2> casters{};
			std::atomic<bool>                         looked{ false };
		};
		std::shared_ptr<Seen> seen{ std::make_shared<Seen>() };
	};
	void PublishHands(std::vector<HandsEntry> a_entries);

	// What the combat AI made of a companion's chosen spells since their hands were last
	// published (a hand's bits are 0 if it never took that spell up at all); nothing until it
	// has looked at their magic, which it does only in a fight.
	[[nodiscard]] std::optional<std::array<std::uint16_t, 2>> SeenHands(std::uint32_t a_actor);

	// For the log: everything in a combat AI's inventory (each item's kind, name, hand and
	// score, by category) and what it has equipped.
	[[nodiscard]] std::string DescribeInventory(RE::CombatController* a_controller);

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
		std::uint64_t spells{ 0 };    // other magic kept out of a hand that holds a chosen spell
		std::uint64_t rested{ 0 };    // weapons kept from a caster resting their magicka
		std::uint64_t melee{ 0 };     // weapons (blades, bows, fists) kept from mages and healers
		std::uint64_t skill{ 0 };     // chosen spells let past the combat AI's skill check
	};

	[[nodiscard]] Stats TakeStats();
}
