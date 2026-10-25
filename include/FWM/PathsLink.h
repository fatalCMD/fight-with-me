#pragma once

#include "FWM/Paths.h"

// Fight With Me's side of the Paths add-on. With FightWithMePaths.dll loaded (and built from
// the same trees), every companion carries the two abilities their role's path has slotted,
// at the ranks they bought. Without it, each role has its abilities as before, growing with
// level. Main thread.

namespace FWM::PathsLink
{
	// After all plugins have loaded.
	void Connect();

	// The add-on is here and agrees on the trees.
	[[nodiscard]] bool Active();

	// What a companion carries into a fight, and at what ranks.
	struct Kit
	{
		Role                                     role{ Role::kFighter };
		bool                                     tree{ false };  // from a path; otherwise each role's own abilities, by level
		std::array<Paths::Ability, Paths::kSlots> slots{};
		const Paths::Record*                     record{ nullptr };

		[[nodiscard]] bool Carries(Paths::Ability a_ability) const;
		[[nodiscard]] int  Rank(Paths::Perk a_perk) const;
		[[nodiscard]] int  Rank(Paths::Ability a_ability) const;
		// The perk's value at its rank on this path: 0 when not bought, or not on a path.
		[[nodiscard]] float Value(Paths::Perk a_perk) const;
		[[nodiscard]] float Second(Paths::Perk a_perk) const;
		[[nodiscard]] float Cooldown(Paths::Ability a_ability) const;
	};

	[[nodiscard]] Kit KitOf(RE::FormID a_id, Role a_role);
	// Main-thread, synchronous ability test; never changes the saved role or progression.
	class ScopedDebugCast {
	public:
		ScopedDebugCast(RE::FormID id, const Kit& kit);
		~ScopedDebugCast();
		ScopedDebugCast(const ScopedDebugCast&) = delete;
		ScopedDebugCast& operator=(const ScopedDebugCast&) = delete;
	private:
		RE::FormID previousID;
		const Kit* previousKit;
	};
	[[nodiscard]] bool DebugCasting(RE::FormID id);
	[[nodiscard]] RE::NiPoint3 DebugAim(RE::Actor* actor);

	// Whether they use an ability without being told. On a path, each ability has its own
	// switch (Orders::manual); without one, the role's switches as before.
	[[nodiscard]] bool AutoOn(const Orders& a_orders, const Kit& a_kit, Paths::Ability a_ability);
	[[nodiscard]] constexpr std::uint32_t Bit(Paths::Ability a_ability) { return 1u << static_cast<std::uint32_t>(a_ability); }

	// Without the add-on, the abilities each role has had all along.
	[[nodiscard]] bool ClassicCarries(Role a_role, Paths::Ability a_ability);

	struct Progress
	{
		Paths::Record record;
		int           earned{ 0 };
		int           spent{ 0 };
		bool          unlimited{ false };  // unlimited points are on
		[[nodiscard]] int Unspent() const { return unlimited ? Paths::kUnlimited : std::max(earned - spent, 0); }
	};

	// The latest copy for a companion, read from the add-on when missing or stale.
	[[nodiscard]] const Progress* Find(RE::FormID a_id);

	// Read from the add-on now, never the copy: right after a level the copy is a level behind.
	[[nodiscard]] const Progress* ReadNow(RE::FormID a_id);

	// Reads everyone in the party again (every roster refresh), and tells the player when
	// someone has new points to spend.
	void Refresh();

	// Spending, giving back, starting over, carrying. Answered in a sentence for the plan.
	struct Outcome
	{
		bool        done{ false };
		std::string text;
	};
	Outcome Learn(RE::FormID a_id, Role a_role, std::size_t a_node);
	Outcome Forget(RE::FormID a_id, Role a_role, std::size_t a_node);
	Outcome Reset(RE::FormID a_id, Role a_role);
	Outcome Equip(RE::FormID a_id, Role a_role, std::size_t a_slot, Paths::Ability a_ability);
	// A leftover point into a mastered path's bonus, while it still raises it (a_room more), and
	// one back out. The companion's own points, on their own path.
	Outcome Invest(RE::FormID a_id, Role a_role, int a_room);
	Outcome Divest(RE::FormID a_id, Role a_role);

	// Unlimited points for one companion, or back to what they had before.
	Outcome SetUnlimited(RE::FormID a_id, bool a_on);
	[[nodiscard]] bool Unlimited(RE::FormID a_id);
	[[nodiscard]] bool UnlimitedAllowed();

	[[nodiscard]] bool RespecAllowed();
	[[nodiscard]] int  PointsAboveLevel();

	// A load or a new game: forget the copies and the notices given.
	void Clear();
}
