#include "FWM/Paths.h"

#include <algorithm>
#include <format>

namespace FWM::Paths
{
	using namespace std::string_literals;

	namespace
	{
		const Node* NodeAt(Role a_role, std::size_t a_index)
		{
			if (static_cast<std::size_t>(a_role) >= kRoles || a_index >= kNodes || PathOf(a_role)[a_index].Empty()) {
				return nullptr;
			}
			return &PathOf(a_role)[a_index];
		}

		std::uint8_t& RankAt(Record& a_record, Role a_role, std::size_t a_index)
		{
			return a_record.ranks[static_cast<std::size_t>(a_role)][a_index];
		}

		int RankAt(const Record& a_record, Role a_role, std::size_t a_index)
		{
			return a_record.ranks[static_cast<std::size_t>(a_role)][a_index];
		}

		// Points a node's ranks above its starting one cost: the first its cost, every one after its step.
		int Bought(const Node& a_node, int a_rank)
		{
			int sum = 0;
			for (int r = a_node.base + 1; r <= a_rank; ++r) {
				sum += r == 1 ? a_node.cost : a_node.step;
			}
			return sum;
		}

		// Every node on the path but the capstone at its highest rank.
		bool OthersMastered(const Record& a_record, Role a_role)
		{
			const auto& path = PathOf(a_role);
			for (std::size_t i = 0; i < kNodes; ++i) {
				if (!path[i].Empty() && !path[i].capstone && RankAt(a_record, a_role, i) < path[i].most) {
					return false;
				}
			}
			return true;
		}

		// A capstone with a rank needs every other node on its path mastered.
		bool CapstoneHolds(const Record& a_record, Role a_role)
		{
			const auto& path = PathOf(a_role);
			bool        capped = false;
			for (std::size_t i = 0; i < kNodes; ++i) {
				capped = capped || (path[i].capstone && RankAt(a_record, a_role, i) > 0);
			}
			return !capped || OthersMastered(a_record, a_role);
		}

		// Every tier with points in it still has enough spent below it.
		bool TiersHold(const Record& a_record, Role a_role)
		{
			const auto& path = PathOf(a_role);
			for (std::size_t i = 0; i < kNodes; ++i) {
				const int bought = RankAt(a_record, a_role, i) - path[i].base;
				if (bought > 0 && SpentBelow(a_record, a_role, path[i].tier) < kTierPoints[path[i].tier]) {
					return false;
				}
			}
			return true;
		}

		// A slot left empty, or holding something no longer learned, gets the first learned
		// ability not already carried, defaults first.
		void FillSlots(Record& a_record, Role a_role)
		{
			auto& slots = a_record.slots[static_cast<std::size_t>(a_role)];
			for (std::size_t s = 0; s < kSlots; ++s) {
				if (slots[s] != Ability::kNone && (!Learned(a_record, a_role, slots[s]) || (s == 1 && slots[1] == slots[0]))) {
					slots[s] = Ability::kNone;
				}
			}
			for (std::size_t s = 0; s < kSlots; ++s) {
				if (slots[s] != Ability::kNone) {
					continue;
				}
				for (const auto& node : PathOf(a_role)) {
					if (node.kind != Kind::kAbility || !Learned(a_record, a_role, node.ability)) {
						continue;
					}
					if (std::find(slots.begin(), slots.end(), node.ability) == slots.end()) {
						slots[s] = node.ability;
						break;
					}
				}
			}
		}

		struct Ranks
		{
			float r1{ 0.0f }, r2{ 0.0f }, r3{ 0.0f };
		};

		float Pick(Ranks a_ranks, int a_rank)
		{
			switch (std::clamp(a_rank, 0, 3)) {
			case 1:
				return a_ranks.r1;
			case 2:
				return a_ranks.r2;
			case 3:
				return a_ranks.r3;
			default:
				return 0.0f;
			}
		}

		Ranks Primary(Perk a_perk)
		{
			switch (a_perk) {
			case Perk::kTaunt:
				return { 10, 15, 30 };
			case Perk::kTauntBrace:
			case Perk::kBodyguardInterpose:
			case Perk::kBerserkThickSkull:
				return { 40, 80, 120 };
			case Perk::kTauntIntimidate:
			case Perk::kChallengeDuel:
			case Perk::kStandFastUnbroken:
			case Perk::kHealSunsBane:
			case Perk::kWarCryUnnerve:
			case Perk::kBerserkBloodlust:
			case Perk::kSunderShatter:
			case Perk::kMomentum:
			case Perk::kVolleyRallyFire:
			case Perk::kSharpshooter:
			case Perk::kUnravel:
				return { 10, 20, 30 };
			case Perk::kBodyguard:
				return { 10, 15, 20 };
			case Perk::kBodyguardRush:
			case Perk::kRenewalLifeline:
			case Perk::kEarthshakerAftershock:
			case Perk::kDisarmSweep:
			case Perk::kRiposteDisarming:
			case Perk::kCleaveWhirlwind:
			case Perk::kPinningTwin:
			case Perk::kArrowRainPinning:
			case Perk::kWardMantle:
			case Perk::kBurstTwin:
				return { 1, 1, 1 };
			case Perk::kChallenge:
				return { 8, 12, 16 };
			case Perk::kChallengeHumble:
				return { 15, 25, 35 };
			case Perk::kStandFast:
				return { 30, 60, 90 };
			case Perk::kStandFastBreath:
				return { 20, 35, 50 };
			case Perk::kIronHide:
			case Perk::kSerenity:
			case Perk::kEndurance:
			case Perk::kArcaneFlow:
				return { 25, 50, 75 };
			case Perk::kShieldMaster:
				return { 1.5f, 2.0f, 3.0f };
			case Perk::kLastStand:
				return { 15, 25, 35 };
			case Perk::kHeal:
				return { 20, 30, 40 };
			case Perk::kHealSwift:
			case Perk::kSecondWindUnyielding:
				return { 10, 20, 30 };
			case Perk::kRejuvenate:
				return { 30, 60, 120 };
			case Perk::kRejuvenateWellspring:
			case Perk::kWardSpellward:
			case Perk::kTriage:
			case Perk::kBurstSearing:
				return { 15, 30, 45 };
			case Perk::kRejuvenateVigor:
			case Perk::kSurgeConduit:
				return { 50, 100, 150 };
			case Perk::kWard:
				return { 0, 2, 3 };  // Oakflesh, Ironflesh, Ebonyflesh
			case Perk::kWardShared:
				return { 1, 2, 99 };
			case Perk::kRenewal:
				return { 2, 3, 4 };
			case Perk::kRenewalRoots:
			case Perk::kDeadeyePatience:
				return { 4, 8, 12 };
			case Perk::kGuardianSpirit:
				return { 25, 40, 55 };
			case Perk::kWarCry:
				return { 300, 400, 500 };
			case Perk::kWarCryRally:
			case Perk::kExecutioner:
				return { 15, 30, 45 };
			case Perk::kBerserk:
			case Perk::kDeadeye:
				return { 20, 35, 50 };
			case Perk::kSunder:
				return { 60, 120, 180 };
			case Perk::kSunderCrack:
				return { 0.4f, 0.7f, 1.0f };
			case Perk::kEarthshaker:
				return { 250, 300, 350 };
			case Perk::kEarthshakerFault:
				return { 75, 150, 225 };
			case Perk::kHeavyHands:
			case Perk::kSkirmisher:
				return { 1.5f, 2.0f, 2.5f };
			case Perk::kDisarm:
				return { 5, 10, 15 };
			case Perk::kDisarmExpose:
				return { 50, 100, 150 };
			case Perk::kSecondWind:
				return { 15, 25, 35 };
			case Perk::kSecondWindResolve:
				return { 30, 60, 90 };
			case Perk::kRiposte:
				return { 8, 10, 12 };
			case Perk::kRiposteCounter:
				return { 0.5f, 1.0f, 1.5f };
			case Perk::kCleave:
				return { 1.5f, 2.0f, 2.5f };
			case Perk::kCleaveLacerate:
				return { 4, 7, 10 };
			case Perk::kFlanker:
				return { 5, 10, 15 };
			case Perk::kOpportunist:
			case Perk::kDeadeyeHawkeye:
				return { 20, 40, 60 };
			case Perk::kVolley:
				return { 3, 5, 7 };
			case Perk::kVolleyBroadhead:
				return { 40, 80, 120 };
			case Perk::kPinningShot:
				return { 2, 3, 4 };
			case Perk::kPinningHeavy:
				return { 1, 2, 3 };
			case Perk::kArrowRain:
				return { 8, 12, 16 };
			case Perk::kArrowRainDownpour:
				return { 4, 8, 12 };
			case Perk::kFletcher:
				return { 15, 30, 50 };
			case Perk::kSurge:
				return { 3, 4, 5 };
			case Perk::kSurgeOverflow:
				return { 20, 35, 50 };
			case Perk::kBurst:
				return { 1, 2, 3 };  // Apprentice, Adept, Expert
			case Perk::kMagnus:
				return { 12, 18, 25 };  // seconds of endless magicka
			case Perk::kSpellShield:
				return { 10, 20, 30 };
			default:
				return {};
			}
		}

		Ranks Secondary(Perk a_perk)
		{
			switch (a_perk) {
			case Perk::kWarCry:
				return { 0.6f, 0.8f, 1.0f };  // stagger
			case Perk::kEarthshaker:
				return { 1.0f, 1.5f, 2.0f };  // times their weapon's damage
			case Perk::kVolley:
				return { 6, 8, 10 };  // seconds everyone at range keeps on the target
			case Perk::kSecondWind:
				return { 25, 50, 75 };  // stamina, percent
			case Perk::kPinningHeavy:
				return { 25, 50, 75 };  // chance a boss is held, percent
			case Perk::kSpellShield:
				return { 5, 10, 15 };  // spell absorption, percent
			case Perk::kRejuvenate:
				return { 100, 100, 100 };  // percent faster
			case Perk::kMagnus:
				return { 0, 30, 60 };  // destruction spells strike this much harder, percent
			default:
				return {};
			}
		}

		constexpr std::array<std::string_view, 4> kFlesh{ "Oakflesh", "Stoneflesh", "Ironflesh", "Ebonyflesh" };
		constexpr std::array<std::string_view, 4> kTiers{ "Novice", "Apprentice", "Adept", "Expert" };

		std::string_view Strength(float a_magnitude)
		{
			return a_magnitude < 0.55f ? "lightly" : (a_magnitude < 0.85f ? "firmly" : "hard");
		}

		std::string Number(float a_value)
		{
			return std::abs(a_value - std::round(a_value)) < 0.01f ? std::format("{:.0f}", a_value) : std::format("{:.1f}", a_value);
		}
	}

	int IndexOf(Role a_role, Perk a_perk)
	{
		if (static_cast<std::size_t>(a_role) >= kRoles || a_perk == Perk::kNone) {
			return -1;
		}
		const auto& path = PathOf(a_role);
		for (std::size_t i = 0; i < kNodes; ++i) {
			if (path[i].perk == a_perk) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	int IndexOf(Role a_role, Ability a_ability)
	{
		if (static_cast<std::size_t>(a_role) >= kRoles || a_ability == Ability::kNone) {
			return -1;
		}
		const auto& path = PathOf(a_role);
		for (std::size_t i = 0; i < kNodes; ++i) {
			if (path[i].kind == Kind::kAbility && path[i].ability == a_ability) {
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	int ParentOf(Role a_role, std::size_t a_index)
	{
		const auto* node = NodeAt(a_role, a_index);
		return node && node->kind == Kind::kBranch ? IndexOf(a_role, node->ability) : -1;
	}

	std::array<Ability, kSlots> Defaults(Role a_role)
	{
		std::array<Ability, kSlots> out{};
		std::size_t                 n = 0;
		for (const auto& node : PathOf(a_role)) {
			if (node.kind == Kind::kAbility && node.base > 0 && n < kSlots) {
				out[n++] = node.ability;
			}
		}
		return out;
	}

	Record Fresh()
	{
		Record record;
		for (std::size_t r = 0; r < kRoles; ++r) {
			const auto role = static_cast<Role>(r);
			for (std::size_t i = 0; i < kNodes; ++i) {
				record.ranks[r][i] = PathOf(role)[i].base;
			}
			record.slots[r] = Defaults(role);
		}
		return record;
	}

	int Earned(int a_level, int a_above)
	{
		return std::max(0, a_level - std::max(a_above, 0));
	}

	int SpentBelow(const Record& a_record, Role a_role, int a_tier)
	{
		int sum = 0;
		const auto& path = PathOf(a_role);
		for (std::size_t i = 0; i < kNodes; ++i) {
			if (path[i].tier < a_tier) {
				sum += Bought(path[i], RankAt(a_record, a_role, i));
			}
		}
		return sum;
	}

	bool TierOpen(const Record& a_record, Role a_role, int a_tier)
	{
		return a_tier <= 0 || SpentBelow(a_record, a_role, a_tier) >= kTierPoints[static_cast<std::size_t>(std::min(a_tier, 2))];
	}

	int OnTree(const Record& a_record, Role a_role)
	{
		return SpentBelow(a_record, a_role, 99);
	}

	int Invested(const Record& a_record, Role a_role)
	{
		const auto r = static_cast<std::size_t>(a_role);
		return r < kRoles ? a_record.bonus[r] : 0;
	}

	int Spent(const Record& a_record, Role a_role)
	{
		return OnTree(a_record, a_role) + Invested(a_record, a_role);
	}

	bool Mastered(const Record& a_record, Role a_role)
	{
		if (static_cast<std::size_t>(a_role) >= kRoles) {
			return false;
		}
		const auto& path = PathOf(a_role);
		for (std::size_t i = 0; i < kNodes; ++i) {
			if (!path[i].Empty() && RankAt(a_record, a_role, i) < path[i].most) {
				return false;
			}
		}
		return true;
	}

	int Spent(const Record& a_record)
	{
		int sum = 0;
		for (std::size_t r = 0; r < kRoles; ++r) {
			sum += Spent(a_record, static_cast<Role>(r));
		}
		return sum;
	}

	int Rank(const Record& a_record, Role a_role, Perk a_perk)
	{
		const int index = IndexOf(a_role, a_perk);
		return index < 0 ? 0 : RankAt(a_record, a_role, static_cast<std::size_t>(index));
	}

	int Rank(const Record& a_record, Role a_role, Ability a_ability)
	{
		const int index = IndexOf(a_role, a_ability);
		return index < 0 ? 0 : RankAt(a_record, a_role, static_cast<std::size_t>(index));
	}

	bool Learned(const Record& a_record, Role a_role, Ability a_ability)
	{
		return Rank(a_record, a_role, a_ability) > 0;
	}

	std::string_view Why(Block a_block)
	{
		switch (a_block) {
		case Block::kMaxed:
			return "Already at its highest rank.";
		case Block::kNoPoints:
			return "No perk points left to spend.";
		case Block::kTier:
			return "Needs more points spent on this path first.";
		case Block::kParent:
			return "A branch cannot be ranked above its ability.";
		case Block::kDefault:
			return "Every companion keeps the first rank of this ability.";
		case Block::kHolds:
			return "Other ranks on this path depend on it.";
		case Block::kNotLearned:
			return "Not learned yet.";
		case Block::kNothing:
			return "No rank bought here.";
		case Block::kBadNode:
			return "Not on this path.";
		case Block::kNoRespec:
			return "Giving points back is switched off (FightWithMePaths.ini).";
		case Block::kCapstone:
			return "Every other node on this path must be mastered first, every rank of it.";
		case Block::kNoUnlimited:
			return "Unlimited points are switched off (FightWithMePaths.ini).";
		case Block::kNotMastered:
			return "Points go into the path bonus once every node on this path is mastered.";
		case Block::kBonusFull:
			return "The path bonus is at its highest.";
		default:
			return "";
		}
	}

	Block CanLearn(const Record& a_record, Role a_role, std::size_t a_index, int a_earned)
	{
		const auto* node = NodeAt(a_role, a_index);
		if (!node) {
			return Block::kBadNode;
		}
		const int rank = RankAt(a_record, a_role, a_index);
		if (rank >= node->most) {
			return Block::kMaxed;
		}
		// A capstone asks for everything else on the path mastered, which opens every tier on the way.
		if (node->capstone && !OthersMastered(a_record, a_role)) {
			return Block::kCapstone;
		}
		if (SpentBelow(a_record, a_role, node->tier) < kTierPoints[node->tier]) {
			return Block::kTier;
		}
		if (const int parent = ParentOf(a_role, a_index); parent >= 0 && rank + 1 > RankAt(a_record, a_role, static_cast<std::size_t>(parent))) {
			return Block::kParent;
		}
		if (a_earned - Spent(a_record) < (rank + 1 == 1 ? node->cost : node->step)) {
			return Block::kNoPoints;
		}
		return Block::kNone;
	}

	Block CanForget(const Record& a_record, Role a_role, std::size_t a_index)
	{
		const auto* node = NodeAt(a_role, a_index);
		if (!node) {
			return Block::kBadNode;
		}
		const int rank = RankAt(a_record, a_role, a_index);
		if (rank <= node->base) {
			return node->base > 0 ? Block::kDefault : Block::kNothing;
		}
		Record after = a_record;
		RankAt(after, a_role, a_index) = static_cast<std::uint8_t>(rank - 1);
		if (node->kind == Kind::kAbility) {
			const auto& path = PathOf(a_role);
			for (std::size_t i = 0; i < kNodes; ++i) {
				if (path[i].kind == Kind::kBranch && path[i].ability == node->ability && RankAt(after, a_role, i) > rank - 1) {
					return Block::kHolds;
				}
			}
		}
		return TiersHold(after, a_role) && CapstoneHolds(after, a_role) ? Block::kNone : Block::kHolds;
	}

	Block Learn(Record& a_record, Role a_role, std::size_t a_index, int a_earned)
	{
		const auto block = CanLearn(a_record, a_role, a_index, a_earned);
		if (block == Block::kNone) {
			++RankAt(a_record, a_role, a_index);
			FillSlots(a_record, a_role);
		}
		return block;
	}

	Block Forget(Record& a_record, Role a_role, std::size_t a_index)
	{
		const auto block = CanForget(a_record, a_role, a_index);
		if (block == Block::kNone) {
			--RankAt(a_record, a_role, a_index);
			FillSlots(a_record, a_role);
		}
		return block;
	}

	void Reset(Record& a_record, Role a_role)
	{
		const auto r = static_cast<std::size_t>(a_role);
		if (r >= kRoles) {
			return;
		}
		for (std::size_t i = 0; i < kNodes; ++i) {
			a_record.ranks[r][i] = PathOf(a_role)[i].base;
		}
		a_record.slots[r] = Defaults(a_role);
		a_record.bonus[r] = 0;
	}

	Block CanInvest(const Record& a_record, Role a_role, int a_earned, int a_room)
	{
		if (static_cast<std::size_t>(a_role) >= kRoles) {
			return Block::kBadNode;
		}
		if (!Mastered(a_record, a_role)) {
			return Block::kNotMastered;
		}
		if (a_room <= 0 || Invested(a_record, a_role) >= 0xFFFF) {
			return Block::kBonusFull;
		}
		if (a_earned - Spent(a_record) < 1) {
			return Block::kNoPoints;
		}
		return Block::kNone;
	}

	Block Invest(Record& a_record, Role a_role, int a_earned, int a_room)
	{
		const auto block = CanInvest(a_record, a_role, a_earned, a_room);
		if (block == Block::kNone) {
			++a_record.bonus[static_cast<std::size_t>(a_role)];
		}
		return block;
	}

	Block CanDivest(const Record& a_record, Role a_role)
	{
		if (static_cast<std::size_t>(a_role) >= kRoles) {
			return Block::kBadNode;
		}
		return Invested(a_record, a_role) > 0 ? Block::kNone : Block::kNothing;
	}

	Block Divest(Record& a_record, Role a_role)
	{
		const auto block = CanDivest(a_record, a_role);
		if (block == Block::kNone) {
			--a_record.bonus[static_cast<std::size_t>(a_role)];
		}
		return block;
	}

	Block Equip(Record& a_record, Role a_role, std::size_t a_slot, Ability a_ability)
	{
		if (static_cast<std::size_t>(a_role) >= kRoles || a_slot >= kSlots || IndexOf(a_role, a_ability) < 0) {
			return Block::kBadNode;
		}
		if (!Learned(a_record, a_role, a_ability)) {
			return Block::kNotLearned;
		}
		auto&             slots = a_record.slots[static_cast<std::size_t>(a_role)];
		const std::size_t other = 1 - a_slot;
		if (slots[other] == a_ability) {
			slots[other] = slots[a_slot];
		}
		slots[a_slot] = a_ability;
		FillSlots(a_record, a_role);
		return Block::kNone;
	}

	void Restore(Record& a_record, const Record& a_before)
	{
		const auto peak = std::max(a_record.peak, a_before.peak);
		const auto slots = a_record.slots;
		a_record = a_before;
		a_record.slots = slots;
		a_record.peak = peak;
		for (std::size_t r = 0; r < kRoles; ++r) {
			FillSlots(a_record, static_cast<Role>(r));
		}
	}

	bool Repair(Record& a_record, int a_earned)
	{
		const Record before = a_record;
		for (std::size_t r = 0; r < kRoles; ++r) {
			const auto  role = static_cast<Role>(r);
			const auto& path = PathOf(role);
			for (std::size_t i = 0; i < kNodes; ++i) {
				a_record.ranks[r][i] = std::clamp(a_record.ranks[r][i], path[i].base, path[i].most);
			}
			for (std::size_t i = 0; i < kNodes; ++i) {
				if (const int parent = ParentOf(role, i); parent >= 0) {
					a_record.ranks[r][i] = std::min(a_record.ranks[r][i], a_record.ranks[r][static_cast<std::size_t>(parent)]);
				}
			}
			// A tier that lost its footing gives its points back, from the top.
			while (!TiersHold(a_record, role)) {
				bool dropped = false;
				for (std::size_t i = kNodes; i-- > 0 && !dropped;) {
					if (a_record.ranks[r][i] > path[i].base && path[i].tier > 0) {
						--a_record.ranks[r][i];
						dropped = true;
					}
				}
				if (!dropped) {
					break;
				}
			}
			for (std::size_t i = 0; i < kNodes; ++i) {
				if (const int parent = ParentOf(role, i); parent >= 0) {
					a_record.ranks[r][i] = std::min(a_record.ranks[r][i], a_record.ranks[r][static_cast<std::size_t>(parent)]);
				}
			}
			// A capstone that lost its footing goes last, once tiers and branches are set right,
			// since either can take a rank from a node it stands on.
			if (!CapstoneHolds(a_record, role)) {
				for (std::size_t i = 0; i < kNodes; ++i) {
					if (path[i].capstone) {
						a_record.ranks[r][i] = 0;
					}
				}
			}
			FillSlots(a_record, role);
		}
		// More spent than earned: points in a bonus are the leftovers and come back first, from
		// whichever bonus has the most.
		while (Spent(a_record) > std::max(a_earned, 0)) {
			const auto most = std::max_element(a_record.bonus.begin(), a_record.bonus.end());
			if (*most == 0) {
				break;
			}
			--*most;
		}
		// Still more: the path with the most on it gives back from the top.
		while (Spent(a_record) > std::max(a_earned, 0)) {
			std::size_t most = 0;
			for (std::size_t r = 1; r < kRoles; ++r) {
				if (Spent(a_record, static_cast<Role>(r)) > Spent(a_record, static_cast<Role>(most))) {
					most = r;
				}
			}
			const auto role = static_cast<Role>(most);
			bool       dropped = false;
			for (std::size_t i = kNodes; i-- > 0 && !dropped;) {
				if (CanForget(a_record, role, i) == Block::kNone) {
					Forget(a_record, role, i);
					dropped = true;
				}
			}
			if (!dropped) {
				Reset(a_record, role);
			}
		}
		return !(a_record == before);
	}

	std::uint32_t Signature()
	{
		std::uint32_t hash = 2166136261u;
		auto          mix = [&](std::uint32_t a_value) {
			for (int i = 0; i < 4; ++i) {
				hash ^= (a_value >> (8 * i)) & 0xFF;
				hash *= 16777619u;
			}
		};
		mix(static_cast<std::uint32_t>(kAbilityCount));
		mix(static_cast<std::uint32_t>(kPerkCount));
		for (const auto& path : paths) {
			for (const auto& node : path) {
				mix(static_cast<std::uint32_t>(node.perk) | static_cast<std::uint32_t>(node.kind) << 8 | static_cast<std::uint32_t>(node.ability) << 16);
				mix(static_cast<std::uint32_t>(node.tier) | static_cast<std::uint32_t>(node.most) << 8 | static_cast<std::uint32_t>(node.base) << 16);
				mix(static_cast<std::uint32_t>(node.cost) | static_cast<std::uint32_t>(node.step) << 8 | static_cast<std::uint32_t>(node.capstone) << 16);
			}
		}
		for (const int points : kTierPoints) {
			mix(static_cast<std::uint32_t>(points));
		}
		return hash;
	}

	std::vector<std::uint8_t> Encode(std::span<const Entry> a_entries)
	{
		std::vector<std::uint8_t> out;
		auto                      put = [&](std::uint32_t a_value, int a_bytes) {
            for (int i = 0; i < a_bytes; ++i) {
                out.push_back(static_cast<std::uint8_t>(a_value >> (8 * i)));
            }
		};
		put(static_cast<std::uint32_t>(a_entries.size()), 4);
		for (const auto& [id, record] : a_entries) {
			put(id, 4);
			put(record.peak, 2);
			put(static_cast<std::uint32_t>(kRoles), 1);
			for (std::size_t r = 0; r < kRoles; ++r) {
				const auto& path = paths[r];
				put(static_cast<std::uint32_t>(record.slots[r][0]), 1);
				put(static_cast<std::uint32_t>(record.slots[r][1]), 1);
				std::uint32_t bought = 0;
				for (std::size_t i = 0; i < kNodes; ++i) {
					bought += record.ranks[r][i] > path[i].base ? 1 : 0;
				}
				put(bought, 1);
				for (std::size_t i = 0; i < kNodes; ++i) {
					if (record.ranks[r][i] > path[i].base) {
						put(static_cast<std::uint32_t>(path[i].perk), 1);
						put(record.ranks[r][i], 1);
					}
				}
			}
		}
		// The bonus points, after every entry: (follower, a count for each path) for anyone with any.
		std::uint32_t invested = 0;
		for (const auto& [id, record] : a_entries) {
			invested += std::any_of(record.bonus.begin(), record.bonus.end(), [](std::uint16_t a_points) { return a_points > 0; }) ? 1 : 0;
		}
		put(invested, 4);
		for (const auto& [id, record] : a_entries) {
			if (std::none_of(record.bonus.begin(), record.bonus.end(), [](std::uint16_t a_points) { return a_points > 0; })) {
				continue;
			}
			put(id, 4);
			put(static_cast<std::uint32_t>(kRoles), 1);
			for (const auto points : record.bonus) {
				put(points, 2);
			}
		}
		return out;
	}

	std::vector<Entry> Decode(std::span<const std::uint8_t> a_bytes, std::uint32_t a_version)
	{
		std::vector<Entry> out;
		if (a_version < 1) {
			return out;
		}
		std::size_t at = 0;
		bool        ok = true;
		auto        get = [&](int a_size) {
			std::uint32_t value = 0;
			if (!ok || at + static_cast<std::size_t>(a_size) > a_bytes.size()) {
				ok = false;
				return value;
			}
			for (int i = 0; i < a_size; ++i) {
				value |= static_cast<std::uint32_t>(a_bytes[at + static_cast<std::size_t>(i)]) << (8 * i);
			}
			at += static_cast<std::size_t>(a_size);
			return value;
		};
		const auto count = get(4);
		for (std::uint32_t n = 0; n < count && ok; ++n) {
			const auto id = get(4);
			Record     record = Fresh();
			record.peak = static_cast<std::uint16_t>(get(2));
			const auto roleCount = get(1);
			for (std::uint32_t r = 0; r < roleCount && ok; ++r) {
				const auto first = get(1);
				const auto second = get(1);
				const auto bought = get(1);
				const bool known = r < kRoles;
				if (known) {
					record.slots[r] = { first < kAbilityCount ? static_cast<Ability>(first) : Ability::kNone,
						second < kAbilityCount ? static_cast<Ability>(second) : Ability::kNone };
				}
				for (std::uint32_t b = 0; b < bought && ok; ++b) {
					const auto perk = get(1);
					const auto rank = get(1);
					if (!known || perk >= kPerkCount) {
						continue;  // a role or node this version does not have
					}
					if (const int index = IndexOf(static_cast<Role>(r), static_cast<Perk>(perk)); index >= 0) {
						record.ranks[r][static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(std::min<std::uint32_t>(rank, 3));
					}
				}
			}
			if (ok) {
				out.emplace_back(id, record);
			}
		}
		if (a_version >= 2 && ok && at < a_bytes.size()) {
			const auto invested = get(4);
			for (std::uint32_t n = 0; n < invested && ok; ++n) {
				const auto id = get(4);
				const auto roleCount = get(1);
				const auto entry = std::find_if(out.begin(), out.end(), [id](const Entry& a_entry) { return a_entry.first == id; });
				for (std::uint32_t r = 0; r < roleCount && ok; ++r) {
					const auto points = get(2);
					if (ok && r < kRoles && entry != out.end()) {
						entry->second.bonus[r] = static_cast<std::uint16_t>(points);
					}
				}
			}
		}
		return out;
	}

	Record* Book::Meet(std::uint32_t a_id, int a_level)
	{
		auto it = records.find(a_id);
		if (it == records.end()) {
			if (a_level <= 0) {
				return nullptr;
			}
			auto record = Fresh();
			record.peak = static_cast<std::uint16_t>(std::clamp(a_level, 1, 0xFFFF));
			it = records.emplace(a_id, record).first;
		} else if (a_level > it->second.peak) {
			it->second.peak = static_cast<std::uint16_t>(std::clamp(a_level, 1, 0xFFFF));
		}
		return &it->second;
	}

	const Record* Book::Find(std::uint32_t a_id) const
	{
		const auto it = records.find(a_id);
		return it != records.end() ? &it->second : nullptr;
	}

	int Book::EarnedAt(int a_level) const
	{
		return Paths::Earned(a_level, points.above) + std::max(points.extra, 0);
	}

	int Book::Earned(std::uint32_t a_id) const
	{
		if (before.contains(a_id)) {
			return kUnlimited;
		}
		const auto* record = Find(a_id);
		return record ? EarnedAt(record->peak) : 0;
	}

	int Book::Unspent(std::uint32_t a_id) const
	{
		const auto* record = Find(a_id);
		return record ? std::max(Earned(a_id) - Spent(*record), 0) : 0;
	}

	bool Book::Unlimited(std::uint32_t a_id) const
	{
		return before.contains(a_id);
	}

	Block Book::Learn(std::uint32_t a_id, Role a_role, std::size_t a_node)
	{
		const auto it = records.find(a_id);
		return it == records.end() ? Block::kBadNode : Paths::Learn(it->second, a_role, a_node, Earned(a_id));
	}

	Block Book::Forget(std::uint32_t a_id, Role a_role, std::size_t a_node)
	{
		if (!points.respec) {
			return Block::kNoRespec;
		}
		const auto it = records.find(a_id);
		return it == records.end() ? Block::kNothing : Paths::Forget(it->second, a_role, a_node);
	}

	Block Book::Reset(std::uint32_t a_id, Role a_role)
	{
		if (!points.respec) {
			return Block::kNoRespec;
		}
		const auto it = records.find(a_id);
		if (it == records.end()) {
			return Block::kNothing;
		}
		if (static_cast<std::size_t>(a_role) >= kRoles) {
			return Block::kBadNode;
		}
		Paths::Reset(it->second, a_role);
		return Block::kNone;
	}

	Block Book::Equip(std::uint32_t a_id, Role a_role, std::size_t a_slot, Ability a_ability)
	{
		const auto it = records.find(a_id);
		return it == records.end() ? Block::kBadNode : Paths::Equip(it->second, a_role, a_slot, a_ability);
	}

	Block Book::Invest(std::uint32_t a_id, Role a_role, int a_room)
	{
		const auto it = records.find(a_id);
		return it == records.end() ? Block::kBadNode : Paths::Invest(it->second, a_role, Earned(a_id), a_room);
	}

	Block Book::Divest(std::uint32_t a_id, Role a_role)
	{
		if (!points.respec) {
			return Block::kNoRespec;
		}
		const auto it = records.find(a_id);
		return it == records.end() ? Block::kNothing : Paths::Divest(it->second, a_role);
	}

	Block Book::SetUnlimited(std::uint32_t a_id, bool a_on)
	{
		const auto it = records.find(a_id);
		if (it == records.end()) {
			return Block::kBadNode;
		}
		if (a_on == before.contains(a_id)) {
			return Block::kNone;
		}
		if (a_on) {
			if (!points.unlimited) {
				return Block::kNoUnlimited;
			}
			before[a_id] = it->second;
			return Block::kNone;
		}
		Restore(it->second, before[a_id]);
		before.erase(a_id);
		Repair(it->second, EarnedAt(it->second.peak));
		return Block::kNone;
	}

	std::vector<Entry> Book::Records() const
	{
		std::vector<Entry> out(records.begin(), records.end());
		std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.first < b.first; });
		return out;
	}

	std::vector<Entry> Book::KeptAside() const
	{
		std::vector<Entry> out(before.begin(), before.end());
		std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.first < b.first; });
		return out;
	}

	int Book::Load(std::vector<Entry> a_records, std::vector<Entry> a_keptAside)
	{
		Clear();
		// What was kept aside first: a companion with unlimited points keeps every rank bought with them.
		for (auto& [id, record] : a_keptAside) {
			if (id) {
				Repair(record, EarnedAt(record.peak));
				before[id] = record;
			}
		}
		int repaired = 0;
		for (auto& [id, record] : a_records) {
			if (!id) {
				continue;
			}
			repaired += Repair(record, before.contains(id) ? kUnlimited : EarnedAt(record.peak)) ? 1 : 0;
			records[id] = record;
		}
		std::erase_if(before, [&](const auto& a_entry) { return !records.contains(a_entry.first); });
		return repaired;
	}

	void Book::Clear()
	{
		records.clear();
		before.clear();
	}

	float Value(Perk a_perk, int a_rank)
	{
		return Pick(Primary(a_perk), a_rank);
	}

	float Second(Perk a_perk, int a_rank)
	{
		return Pick(Secondary(a_perk), a_rank);
	}

	float Third(Perk a_perk, int a_rank)
	{
		return a_perk == Perk::kMagnus && a_rank >= 3 ? 50.0f : 0.0f;
	}

	int CostOf(Role a_role, std::size_t a_index, int a_rank)
	{
		const auto* node = NodeAt(a_role, a_index);
		return node ? (a_rank <= 1 ? node->cost : node->step) : 0;
	}

	int Mastery(Role a_role)
	{
		int sum = 0;
		for (const auto& node : PathOf(a_role)) {
			sum += node.Empty() ? 0 : Bought(node, node.most);
		}
		return sum;
	}

	float Cooldown(const Record& a_record, Role a_role, Ability a_ability)
	{
		const int rank = std::max(Rank(a_record, a_role, a_ability), 1);
		auto      branch = [&](Perk a_perk) { return Value(a_perk, Rank(a_record, a_role, a_perk)); };
		switch (a_ability) {
		case Ability::kTaunt:
			return rank == 1 ? 16.0f : (rank == 2 ? 13.0f : 10.0f);
		case Ability::kBodyguard:
		case Ability::kWarCry:
		case Ability::kRiposte:
		case Ability::kCleave:
			return 30.0f;
		case Ability::kChallenge:
		case Ability::kSunder:
		case Ability::kPinningShot:
			return 25.0f;
		case Ability::kStandFast:
		case Ability::kWard:
		case Ability::kRenewal:
		case Ability::kBerserk:
		case Ability::kVolley:
			return 45.0f;
		case Ability::kHeal:
			return 60.0f - branch(Perk::kHealSwift);
		case Ability::kRejuvenate:
			return 2.0f * Value(Perk::kRejuvenate, rank);
		case Ability::kEarthshaker:
		case Ability::kDeadeye:
		case Ability::kSurge:
			return 40.0f;
		case Ability::kDisarm:
			return rank == 1 ? 40.0f : (rank == 2 ? 35.0f : 30.0f);
		case Ability::kSecondWind:
			return 60.0f - branch(Perk::kSecondWindUnyielding);
		case Ability::kArrowRain:
			return 50.0f;
		case Ability::kBurst:
			return 20.0f;
		case Ability::kMagnus:
			return 120.0f;
		default:
			return 0.0f;
		}
	}

	std::string Measure(Perk a_perk, int a_rank)
	{
		const int   r = std::clamp(a_rank, 1, 3);
		const float v = Value(a_perk, r);
		const float s = Second(a_perk, r);
		const auto  n = Number(v);
		switch (a_perk) {
		case Perk::kTauntBrace:
		case Perk::kBodyguardInterpose:
		case Perk::kStandFast:
		case Perk::kIronHide:
		case Perk::kBerserkThickSkull:
		case Perk::kSecondWindResolve:
			return std::format("+{} armor", n);
		case Perk::kSunder:
		case Perk::kDisarmExpose:
		case Perk::kVolleyBroadhead:
			return std::format("-{} armor", n);
		case Perk::kTauntIntimidate:
		case Perk::kChallengeHumble:
		case Perk::kWarCryUnnerve:
			return std::format("-{}% damage", n);
		case Perk::kChallengeDuel:
		case Perk::kBerserk:
		case Perk::kExecutioner:
		case Perk::kFlanker:
		case Perk::kOpportunist:
		case Perk::kVolleyRallyFire:
		case Perk::kDeadeye:
		case Perk::kDeadeyeHawkeye:
		case Perk::kSharpshooter:
		case Perk::kTriage:
			return std::format("+{}%", n);
		case Perk::kTaunt:
		case Perk::kBodyguard:
		case Perk::kChallenge:
		case Perk::kRejuvenate:
		case Perk::kRiposte:
		case Perk::kPinningShot:
			return std::format("{} s", n);
		case Perk::kRenewalRoots:
		case Perk::kDeadeyePatience:
		case Perk::kPinningHeavy:
			return std::format("+{} s", n);
		case Perk::kHealSwift:
		case Perk::kSecondWindUnyielding:
			return std::format("-{} s", n);
		case Perk::kHeal:
		case Perk::kSecondWind:
		case Perk::kGuardianSpirit:
		case Perk::kLastStand:
		case Perk::kBerserkBloodlust:
		case Perk::kMomentum:
			return std::format("{}% health", n);
		case Perk::kHealSunsBane:
			return std::format("-{}% undead", n);
		case Perk::kRejuvenateWellspring:
		case Perk::kSurgeOverflow:
			return std::format("{}% magicka", n);
		case Perk::kStandFastBreath:
		case Perk::kWarCryRally:
			return std::format("{}% stamina", n);
		case Perk::kRejuvenateVigor:
		case Perk::kEndurance:
			return std::format("+{}% stamina", n);
		case Perk::kSerenity:
		case Perk::kArcaneFlow:
		case Perk::kSurgeConduit:
			return std::format("+{}% magicka", n);
		case Perk::kWard:
			return std::string(kFlesh[static_cast<std::size_t>(std::clamp(static_cast<int>(v), 0, 3))]);
		case Perk::kWardShared:
			return v >= 99.0f ? "everyone"s : std::format("{} more", n);
		case Perk::kWardSpellward:
		case Perk::kStandFastUnbroken:
			return std::format("+{}% resist", n);
		case Perk::kRenewal:
			return std::format("{}% a second", n);
		case Perk::kWarCry:
		case Perk::kEarthshaker:
			return std::format("{} reach", n);
		case Perk::kEarthshakerFault:
			return std::format("+{} reach", n);
		case Perk::kSunderCrack:
			return v < 0.55f ? "light stagger"s : (v < 0.85f ? "firm stagger"s : "heavy stagger"s);
		case Perk::kSunderShatter:
		case Perk::kBurstSearing:
		case Perk::kUnravel:
			return std::format("-{}% resists", n);
		case Perk::kHeavyHands:
		case Perk::kShieldMaster:
		case Perk::kSkirmisher:
			return std::format("{}x as often", n);
		case Perk::kDisarm:
			return std::format("+{} levels", n);
		case Perk::kRiposteCounter:
		case Perk::kCleave:
			return std::format("{}x damage", n);
		case Perk::kCleaveLacerate:
			return std::format("{} a second", n);
		case Perk::kVolley:
			return std::format("{} arrows, {} s", n, Number(s));
		case Perk::kArrowRain:
		case Perk::kFletcher:
			return std::format("{} arrows", n);
		case Perk::kArrowRainDownpour:
			return std::format("+{} arrows", n);
		case Perk::kSurge:
			return std::format("{}x magicka", n);
		case Perk::kBurst:
			return std::string(kTiers[static_cast<std::size_t>(std::clamp(static_cast<int>(v), 0, 3))]);
		case Perk::kMagnus:
			return r == 1 ? std::format("{} s", n) : (r == 2 ? std::format("{} s  ·  +{}%", n, Number(s)) : std::format("{} s  ·  +{}%  ·  ward", n, Number(s)));
		case Perk::kSpellShield:
			return std::format("{}% / {}%", n, Number(s));
		case Perk::kBodyguardRush:
			return "staggers"s;
		case Perk::kRenewalLifeline:
			return "back up"s;
		case Perk::kEarthshakerAftershock:
			return "a second tremor"s;
		case Perk::kDisarmSweep:
			return "knocked down"s;
		case Perk::kRiposteDisarming:
			return "disarms"s;
		case Perk::kCleaveWhirlwind:
			return "all around"s;
		case Perk::kPinningTwin:
			return "a second arrow"s;
		case Perk::kArrowRainPinning:
			return "staggers"s;
		case Perk::kWardMantle:
			return "Flame Cloak"s;
		case Perk::kBurstTwin:
			return "twice"s;
		default:
			return n;
		}
	}

	std::string Describe(Perk a_perk, int a_rank)
	{
		const int   r = std::clamp(a_rank, 1, 3);
		const float v = Value(a_perk, r);
		const float s = Second(a_perk, r);
		const auto  n = Number(v);
		switch (a_perk) {
		// Tank
		case Perk::kTaunt:
			return std::format("Challenges every enemy in reach. Those it holds turn on them for {} s.", n);
		case Perk::kTauntBrace:
			return std::format("Each taunt braces them with +{} armor for as long as it holds.", n);
		case Perk::kTauntIntimidate:
			return std::format("Enemies a taunt holds deal {}% less damage.", n);
		case Perk::kBodyguard:
			return std::format("Runs to your side when you are in trouble and takes on whoever is on you, for {} s.", n);
		case Perk::kBodyguardInterpose:
			return std::format("While they guard you, you have +{} armor.", n);
		case Perk::kBodyguardRush:
			return std::format("Arriving at your side, they stagger every enemy within {:.0f} of you.", kRushReach);
		case Perk::kChallenge:
			return std::format("One enemy, any level, bosses too: it turns on them and stays on them for {} s.", n);
		case Perk::kChallengeHumble:
			return std::format("The challenged enemy deals {}% less damage while it holds.", n);
		case Perk::kChallengeDuel:
			return std::format("While their challenge holds, they deal {}% more damage.", n);
		case Perk::kStandFast:
			return std::format("A battle cry that steels the party: everyone within {:.0f} has +{} armor for {:.0f} s.", kStandFastReach, n, kStandFastSeconds);
		case Perk::kStandFastUnbroken:
			return std::format("Stand Fast also gives {}% resistance to magic.", n);
		case Perk::kStandFastBreath:
			return std::format("Stand Fast also gives back {}% of everyone's stamina.", n);
		case Perk::kIronHide:
			return std::format("+{} armor while they fight.", n);
		case Perk::kShieldMaster:
			return std::format("They bash {}x as often and raise the shield sooner.", n);
		case Perk::kLastStand:
			return std::format("Once a fight, falling below a quarter of their health, they recover {}% of it and gain +{:.0f} armor for {:.0f} s.", n,
				kLastStandArmor, kLastStandSeconds);
		// Healer
		case Perk::kHeal:
			return std::format("Heals you, every companion and their summons within reach by {}% of each one's health. Not a spell: no magicka.", n);
		case Perk::kHealSwift:
			return std::format("Heal is ready {} s sooner.", n);
		case Perk::kHealSunsBane:
			return std::format("Undead within reach of a heal lose {}% of their health to it (a boss half that).", n);
		case Perk::kRejuvenate:
			return std::format("Your magicka and every companion's comes back twice as fast for {} s.", n);
		case Perk::kRejuvenateWellspring:
			return std::format("Rejuvenate also restores {}% of everyone's magicka at once.", n);
		case Perk::kRejuvenateVigor:
			return std::format("While Rejuvenate lasts, stamina comes back {}% faster too.", n);
		case Perk::kWard:
			return std::format("Hardens your skin with {} for {:.0f} s.", kFlesh[static_cast<std::size_t>(std::clamp(static_cast<int>(v), 0, 3))], kWardSeconds);
		case Perk::kWardShared:
			return v >= 99.0f ? "Every companion is warded too."s :
			                    std::format("{} other {} warded too, the most hurt first.", n, v > 1.0f ? "companions are" : "companion is");
		case Perk::kWardSpellward:
			return std::format("Whoever is warded resists magic by {}% while it lasts.", n);
		case Perk::kWardMantle:
			return std::format("Whoever is warded is also wrapped in Flame Cloak for {:.0f} s.", kWardSeconds);
		case Perk::kRenewal:
			return std::format("Everyone within reach regains {}% of their health every second for {:.0f} s.", n, kRenewalSeconds);
		case Perk::kRenewalRoots:
			return std::format("Renewal lasts {} s longer.", n);
		case Perk::kRenewalLifeline:
			return "Anyone down within reach when Renewal starts gets straight back up with a third of their health."s;
		case Perk::kTriage:
			return std::format("Heal and Renewal give {}% more to anyone below a third of their health.", n);
		case Perk::kSerenity:
			return std::format("Their own magicka comes back {}% faster while they fight.", n);
		case Perk::kGuardianSpirit:
			return std::format("Once a fight, when your health falls below {:.0f}%, they heal you for {}% of it at once, wherever they are.", kGuardianLine * 100.0f, n);
		// Bruiser
		case Perk::kWarCry:
			return std::format("A roar that staggers every enemy within {} {}.", n, Strength(s));
		case Perk::kWarCryRally:
			return std::format("The roar gives allies within {:.0f} back {}% of their stamina.", kRallyReach, n);
		case Perk::kWarCryUnnerve:
			return std::format("Enemies it staggers deal {}% less damage for {:.0f} s.", n, kUnnerveSeconds);
		case Perk::kBerserk:
			return std::format("{:.0f} s of fury: {}% more damage, and a power attack at every chance.", kBerserkSeconds, n);
		case Perk::kBerserkBloodlust:
			return std::format("Each kill while berserk heals {}% of their health and adds 3 s.", n);
		case Perk::kBerserkThickSkull:
			return std::format("+{} armor while berserk.", n);
		case Perk::kSunder:
			return std::format("A blow that breaks their target's guard: {} less armor for {:.0f} s, so everyone hits it harder.", n, kSunderSeconds);
		case Perk::kSunderCrack:
			return std::format("The blow also staggers them {}.", Strength(v));
		case Perk::kSunderShatter:
			return std::format("Sunder also strips {}% of its resistance to magic, fire, frost and shock.", n);
		case Perk::kEarthshaker:
			return std::format("Slams the ground: every enemy within {} is knocked off their feet and takes {}x their weapon's damage.", n, Number(s));
		case Perk::kEarthshakerAftershock:
			return "A second tremor 1.5 s later staggers everyone still standing."s;
		case Perk::kEarthshakerFault:
			return std::format("Earthshaker reaches {} further.", n);
		case Perk::kHeavyHands:
			return std::format("They power attack {}x as often.", n);
		case Perk::kMomentum:
			return std::format("Each kill heals {}% of their health.", n);
		case Perk::kExecutioner:
			return std::format("{}% more damage against enemies below {:.0f}% health.", n, kExecuteLine * 100.0f);
		// Fighter
		case Perk::kDisarm:
			return std::format("Knocks the weapon out of their opponent's hands, up to {} levels above their own.", n);
		case Perk::kDisarmExpose:
			return std::format("A disarmed enemy has {} less armor for {:.0f} s.", n, kExposeSeconds);
		case Perk::kDisarmSweep:
			return "Disarm also sweeps them off their feet."s;
		case Perk::kSecondWind:
			return std::format("Catches their breath mid-fight: {}% of their health and {}% of their stamina back.", n, Number(s));
		case Perk::kSecondWindResolve:
			return std::format("+{} armor for {:.0f} s after.", n, kResolveSeconds);
		case Perk::kSecondWindUnyielding:
			return std::format("Second Wind is ready {} s sooner.", n);
		case Perk::kRiposte:
			return std::format("For {} s, every blow they block staggers whoever struck it.", n);
		case Perk::kRiposteCounter:
			return std::format("Each riposte also deals {}x their weapon's damage.", n);
		case Perk::kRiposteDisarming:
			return "The first blow they turn aside also disarms the attacker, if the weapon can be taken."s;
		case Perk::kCleave:
			return std::format("A sweeping power attack: every enemy within {:.0f} in front of them takes {}x their weapon's damage and staggers.", kCleaveReach, n);
		case Perk::kCleaveWhirlwind:
			return "Cleave hits all around them, not only in front."s;
		case Perk::kCleaveLacerate:
			return std::format("Everyone Cleave hits bleeds {} health a second for {:.0f} s.", n, kLacerateSeconds);
		case Perk::kFlanker:
			return std::format("They work round to their opponent's side, and deal {}% more damage.", n);
		case Perk::kEndurance:
			return std::format("Stamina comes back {}% faster while they fight.", n);
		case Perk::kOpportunist:
			return std::format("{}% more damage against an opponent who is staggered or disarmed.", n);
		// Archer
		case Perk::kVolley:
			return std::format("Looses {} arrows in quick succession at their target; every archer and mage turns on it for {} s.", n, Number(s));
		case Perk::kVolleyRallyFire:
			return std::format("You and the party deal {}% more weapon damage while a volley lasts.", n);
		case Perk::kVolleyBroadhead:
			return std::format("The volley's target has {} less armor while it lasts.", n);
		case Perk::kDeadeye:
			return std::format("{:.0f} s of steady aim: {}% more damage, and they hold their ground.", kDeadeyeSeconds, n);
		case Perk::kDeadeyeHawkeye:
			return std::format("While Deadeye lasts, {}% more on top against casters and archers.", n);
		case Perk::kDeadeyePatience:
			return std::format("Deadeye lasts {} s longer.", n);
		case Perk::kPinningShot:
			return std::format("An arrow that pins their target in place, staggering it again and again for {} s. A boss shrugs it off.", n);
		case Perk::kPinningHeavy:
			return std::format("The pin lasts {} s longer, and holds a boss {}% of the time.", n, Number(s));
		case Perk::kPinningTwin:
			return "A second arrow pins the nearest enemy beside the first."s;
		case Perk::kArrowRain:
			return std::format("{} arrows fall around their target over {:.0f} s, within {:.0f}. Conjured: none leave the quiver.", n, kRainSeconds, kRainRadius);
		case Perk::kArrowRainDownpour:
			return std::format("{} more arrows fall.", n);
		case Perk::kArrowRainPinning:
			return "Every enemy under the rain staggers."s;
		case Perk::kFletcher:
			return std::format("Out of a fight, they make arrows of the kind they shoot until they carry {}.", n);
		case Perk::kSkirmisher:
			return std::format("They step back from melee to keep shooting, {}x as readily.", n);
		case Perk::kSharpshooter:
			return std::format("{}% more weapon damage while they fight.", n);
		// Mage
		case Perk::kSurge:
			return std::format("{:.0f} s of arcane surge: their magicka flows back {}x as fast.", kSurgeSeconds, n);
		case Perk::kSurgeOverflow:
			return std::format("Arcane Surge also restores {}% of their magicka at once.", n);
		case Perk::kSurgeConduit:
			return std::format("While it lasts, your magicka and the party's flows back {}% faster too.", n);
		case Perk::kBurst:
			return std::format("Hurls a free {} destruction spell of their element at their target: fire, frost or shock.",
				kTiers[static_cast<std::size_t>(std::clamp(static_cast<int>(v), 0, 3))]);
		case Perk::kBurstSearing:
			return std::format("The enemy it strikes loses {}% of its fire, frost and shock resistance for {:.0f} s.", n, kSearingSeconds);
		case Perk::kBurstTwin:
			return "Casts it twice, the second at another enemy if one is near."s;
		case Perk::kMagnus:
			if (r == 1) {
				return std::format("The power of Magnus: for {} s their magicka never runs dry.", n);
			}
			if (r == 2) {
				return std::format("For {} s their magicka never runs dry, and their destruction spells strike {}% harder.", n, Number(s));
			}
			return std::format("For {} s their magicka never runs dry, their destruction spells strike {}% harder, and they resist magic by {}%.", n, Number(s),
				Number(Third(a_perk, r)));
		case Perk::kArcaneFlow:
			return std::format("Magicka comes back {}% faster while they fight.", n);
		case Perk::kSpellShield:
			return std::format("Resists magic by {}%, and absorbs {}% of the spells cast at them.", n, Number(s));
		case Perk::kUnravel:
			return std::format("Whoever they fight loses {}% of its fire, frost and shock resistance while they do.", n);
		default:
			return {};
		}
	}
}
