#include "FWM/Paths.h"
#include "FWM/WindstepFx.h"
#include "FWM/ArrowArtsFx.h"
#include "FWM/RiposteFx.h"
#include "FWM/DragonBreath.h"
#include "FWM/RejuvenatePolicy.h"
#include "FWM/MoonFx.h"

#include "FWM/Blast.h"
#include "FWM/ChaosDown.h"
#include "FWM/HealCircle.h"

#include "FWM/Text.h"

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

		// Total investment, including each ability rank at its own price.
		int Bought(const Node& a_node, int a_rank)
		{
			int sum = 0;
			for (int r = a_node.base + 1; r <= a_rank; ++r) {
				sum += RankCost(a_node, r);
			}
			return sum;
		}

		// Every nonempty node other than Magnus needs its first purchased rank.
		bool OthersUnlocked(const Record& a_record, Role a_role)
		{
			const auto& path = PathOf(a_role);
			for (std::size_t i = 0; i < kNodes; ++i) {
				if (!path[i].Empty() && !path[i].capstone && RankAt(a_record, a_role, i) < 1) {
					return false;
				}
			}
			return true;
		}

		// Higher ranks can be refunded, but Magnus requires each other node to stay unlocked.
		bool CapstoneHolds(const Record& a_record, Role a_role)
		{
			const auto& path = PathOf(a_role);
			bool        capped = false;
			for (std::size_t i = 0; i < kNodes; ++i) {
				capped = capped || (path[i].capstone && RankAt(a_record, a_role, i) > 0);
			}
			return !capped || OthersUnlocked(a_record, a_role);
		}

		// Refunds elsewhere never close an ability's level gate.
		bool TiersHold(const Record& a_record, Role a_role)
		{
			const auto& path = PathOf(a_role);
			for (std::size_t i = 0; i < kNodes; ++i) {
				const int bought = RankAt(a_record, a_role, i) - path[i].base;
				if (bought > 0 && path[i].kind != Kind::kTrait && a_record.peak < RequiredLevel(path[i], RankAt(a_record, a_role, i))) {
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
			case Perk::kDragonBreath: return {20,30,45};
			case Perk::kBreathReach: return {10,20,30};
			case Perk::kBreathLingering: return {.5f,1,1.5f};
			case Perk::kTaunt:
				return { 10, 15, 30 };
			case Perk::kTauntBrace:
			case Perk::kBerserkThickSkull:
				return { 40, 80, 120 };
			case Perk::kTauntIntimidate:
			case Perk::kWarCryUnnerve:
			case Perk::kBerserkBloodlust:
			case Perk::kMomentum:
			case Perk::kVolleyRallyFire:
			case Perk::kUnravel:
				return { 10, 20, 30 };
			case Perk::kSunderShatter:
				return { 5, 10, 15 };
			case Perk::kBodyguard:
				return { 20, 30, 40 };
			case Perk::kBodyguardInterpose:
				return { 2, 4, 6 };
			case Perk::kStandFastUnbroken:
				return { 100, 200, 300 };
			case Perk::kBodyguardRush:
			case Perk::kRenewalLifeline:
			case Perk::kEarthshakerAftershock:
			case Perk::kDisarmSweep:
			case Perk::kRiposteDisarming:
			case Perk::kWolfMaul:
			case Perk::kPinningTwin:
			case Perk::kArrowRainPinning:
			case Perk::kWardMantle:
			case Perk::kBurstTwin:
				return { 1, 1, 1 };
			case Perk::kChaosDown:
				return { 25, 40, 55 };
			case Perk::kChaosDownWide:
				return { 50, 100, 150 };
			case Perk::kChaosDownLingering:
				return { 2, 4, 6 };
			case Perk::kStandFast:
				return { 60, 70, 80 };
			case Perk::kStandFastBreath:
				return { 2, 4, 6 };
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
				return { 2.5f, 3.5f, 5.0f };  // of each one's health a second, in the circle
			case Perk::kHealInvigorate:
				return { 3, 5, 8 };  // of their stamina a second
			case Perk::kHealWide:
				return { 60, 120, 180 };  // more radius
			case Perk::kSecondWindUnyielding:
				return { 10, 20, 30 };
			case Perk::kRejuvenate:
				return { 8, 10, 12 };
			case Perk::kRejuvenateWellspring:
				return { 5, 10, 15 };
			case Perk::kWardSpellward:
				return { 2, 4, 6 };
			case Perk::kTriage:
				return { 15, 30, 45 };
			case Perk::kBurstSearing:
				return { 10, 15, 20 };
			case Perk::kRejuvenateVigor:
				return { .5f, 1.0f, 1.5f };
			case Perk::kSurgeConduit:
				return { 50, 100, 150 };
			case Perk::kWard:
				return { 20, 25, 30 };
			case Perk::kWardShared:
				return { 140, 280, 420 };
			case Perk::kRenewal:
				return { 2, 3, 4 };
			case Perk::kRenewalRoots:
				return { 4, 8, 12 };
            case Perk::kDeadeyePatience: return {10,20,30};
			case Perk::kGuardianSpirit:
				return { 25, 40, 55 };
			case Perk::kWarCry:
				return { 300, 400, 500 };
			case Perk::kWarCryRally:
			case Perk::kExecutioner:
				return { 15, 30, 45 };
			case Perk::kBerserk:
				return { 15, 25, 35 };
            case Perk::kDeadeye: return {ArrowArtsFx::Damage(1),ArrowArtsFx::Damage(2),ArrowArtsFx::Damage(3)};
			case Perk::kSunder:
				return { 40, 80, 120 };
			case Perk::kSunderCrack:
				return { 0.4f, 0.7f, 1.0f };
			case Perk::kEarthshaker:
				return { 250, 300, 350 };
			case Perk::kEarthshakerFault:
				return { 75, 150, 225 };
			case Perk::kHeavyHands:
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
			case Perk::kSpectralWolf:
				return { 1.5f, 2.0f, 2.5f };  // times their weapon's damage
			case Perk::kWolfHemorrhage:
				return { 4, 8, 12 };  // seconds more bleeding
			case Perk::kFlanker:
				return { 5, 10, 15 };
			case Perk::kOpportunist:
				return { 20, 40, 60 };
            case Perk::kDeadeyeHawkeye: return {.5f,1.f,1.5f};
			case Perk::kVolley:
				return { 3, 4, 5 };
			case Perk::kVolleyBroadhead:
				return { 40, 80, 120 };
			case Perk::kPinningShot:
				return { 2, 3, 4 };
			case Perk::kPinningHeavy:
				return { 1, 2, 3 };
			case Perk::kArrowRain:
				return { 24, 36, 48 };
			case Perk::kArrowRainDownpour:
				return { 8, 16, 24 };
			case Perk::kFineArrows:
				return { 1, 2, 3 };
			case Perk::kSpellTouched:
				return { 10, 15, 20 };
			case Perk::kSteadyBreath:
				return { 25, 50, 75 };
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
			case Perk::kBodyguard:
				return { 8, 10, 12 };
			case Perk::kWarCry:
				return { 0.6f, 0.8f, 1.0f };  // stagger
			case Perk::kEarthshaker:
				return { 0.75f, 1.0f, 1.25f };  // times their weapon's damage
			case Perk::kVolley:
				return { 6, 8, 10 };  // seconds everyone at range keeps on the target
			case Perk::kSecondWind:
				return { 25, 50, 75 };  // stamina, percent
			case Perk::kPinningHeavy:
				return {};  // legacy Heavy Pin boss chance no longer used
			case Perk::kSpellShield:
				return { 5, 10, 15 };  // spell absorption, percent
			case Perk::kRejuvenate:
				return { 100, 100, 100 };  // percent faster
			case Perk::kMagnus:
				return { 0, 15, 30 };  // destruction spells strike this much harder, percent
			case Perk::kSpectralWolf:
				return { 6, 9, 12 };  // of the bitten one's health, bled away, percent
			default:
				return {};
			}
		}

		constexpr std::array<std::string_view, 4> kTiers{ Ntr("Novice"), Ntr("Apprentice"), Ntr("Adept"), Ntr("Expert") };

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

	bool TierOpen(const Record& a_record, Role, int a_tier)
	{
		return a_tier <= 0 || a_record.peak >= kAbilityLevels[static_cast<std::size_t>(std::min(a_tier, 2))];
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

    std::string Requirement(Block block, const Node& node, int nextRank)
    {
        if(block==Block::kParent)return Trf("Requires {} Rank {}",Tr(Info(node.ability).name),nextRank);
        if(block==Block::kLevel || block==Block::kTier)return Trf("Requires level {}",RequiredLevel(node,nextRank));
        return std::string(Why(block));
    }

	std::string_view Why(Block a_block)
	{
		switch (a_block) {
		case Block::kMaxed:
			return Tr("Maximum rank reached");
		case Block::kNoPoints:
			return Tr("Not enough perk points");
		case Block::kTier:
			return Tr("Follower level too low");
		case Block::kParent:
			return Tr("Requires the matching ability rank");
		case Block::kDefault:
			return Tr("Rank 1 cannot be removed");
		case Block::kHolds:
			return Tr("Refund dependent perks first");
		case Block::kNotLearned:
			return Tr("Ability not unlocked");
		case Block::kNothing:
			return Tr("No ranks to refund");
		case Block::kBadNode:
			return Tr("Invalid perk");
		case Block::kNoRespec:
			return "Perk refunds disabled";
		case Block::kCapstone:
			return Tr("Requires all other perks unlocked");
		case Block::kNoUnlimited:
			return "Debug Mode disabled";
		case Block::kNotMastered:
			return Tr("Requires all perks at maximum rank");
		case Block::kLevel:
			return Tr("Follower level too low");
		case Block::kBonusFull:
			return Tr("Maximum path bonus reached");
		default:
			return "";
		}
	}

	Block CanLearn(const Record& a_record, Role a_role, std::size_t a_index, int a_earned, bool a_debug)
	{
		const auto* node = NodeAt(a_role, a_index);
		if (!node) {
			return Block::kBadNode;
		}
		const int rank = RankAt(a_record, a_role, a_index);
		if (rank >= node->most) {
			return Block::kMaxed;
		}
		if (a_debug) return Block::kNone;
		if (a_record.peak < RequiredLevel(*node, rank + 1)) {
			return Block::kLevel;
		}
		if (node->capstone && !OthersUnlocked(a_record, a_role)) {
			return Block::kCapstone;
		}
		if (const int parent = ParentOf(a_role, a_index); parent >= 0 && rank + 1 > RankAt(a_record, a_role, static_cast<std::size_t>(parent))) {
			return Block::kParent;
		}
		if (a_earned - Spent(a_record) < RankCost(*node, rank + 1)) {
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

	Block Learn(Record& a_record, Role a_role, std::size_t a_index, int a_earned, bool a_debug)
	{
		const auto block = CanLearn(a_record, a_role, a_index, a_earned, a_debug);
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

	bool Repair(Record& a_record, int a_earned, bool a_debug)
	{
		const Record before = a_record;
		if (a_debug) {
			for (std::size_t r=0; r<kRoles; ++r) {
				const auto role=static_cast<Role>(r);
				const auto& path=PathOf(role);
				for (std::size_t i=0; i<kNodes; ++i) a_record.ranks[r][i]=std::clamp(a_record.ranks[r][i],path[i].base,path[i].most);
				FillSlots(a_record,role);
			}
			return a_record != before;
		}
		for (std::size_t r = 0; r < kRoles; ++r) {
			const auto  role = static_cast<Role>(r);
			const auto& path = PathOf(role);
			for (std::size_t i = 0; i < kNodes; ++i) {
				a_record.ranks[r][i] = std::clamp(a_record.ranks[r][i], path[i].base, path[i].most);
				while (a_record.ranks[r][i] > 0 && a_record.peak < RequiredLevel(path[i], a_record.ranks[r][i])) {
					--a_record.ranks[r][i];
				}
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
					if (a_record.ranks[r][i] > path[i].base && path[i].kind != Kind::kTrait && path[i].tier > 0) {
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
		mix(kRecordVersion);
		mix(static_cast<std::uint32_t>(kAbilityCount));
		mix(static_cast<std::uint32_t>(kPerkCount));
		for (const auto& path : paths) {
			for (const auto& node : path) {
				mix(static_cast<std::uint32_t>(node.perk) | static_cast<std::uint32_t>(node.kind) << 8 | static_cast<std::uint32_t>(node.ability) << 16);
				mix(static_cast<std::uint32_t>(node.tier) | static_cast<std::uint32_t>(node.most) << 8 | static_cast<std::uint32_t>(node.base) << 16);
				mix(static_cast<std::uint32_t>(node.cost) | static_cast<std::uint32_t>(node.step) << 8 | static_cast<std::uint32_t>(node.capstone) << 16);
			}
		}
		for (const int level : kTraitLevels) {
			mix(static_cast<std::uint32_t>(level));
		}
		for (const int level : kAbilityLevels) {
			mix(static_cast<std::uint32_t>(level));
		}
		mix(1); // Magnus requires unlocked nodes rather than fully mastered nodes.
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
		if (a_version < 3) {
			// Both active records and unlimited-point snapshots migrate, so old ranks cannot return.
			for (auto& [id, record] : out) {
				const auto peak = record.peak;
				record = Fresh();
				record.peak = peak;
			}
		}
        if (a_version == 3) {
            for (auto& [id, record] : out) {
                for (const auto perk : { Perk::kWard, Perk::kWardShared, Perk::kWardSpellward }) {
                    const auto index = IndexOf(Role::kHealer, perk);
                    if (index >= 0) record.ranks[static_cast<std::size_t>(Role::kHealer)][static_cast<std::size_t>(index)] = 0;
                }
                // Spent() is derived from ranks, so removing Ward refunds its purchases.
                FillSlots(record, Role::kHealer);
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
		return Unlimited(a_id) ? kUnlimited : record ? std::max(Earned(a_id) - Spent(*record), 0) : 0;
	}

	bool Book::Unlimited(std::uint32_t a_id) const
	{
		return before.contains(a_id);
	}

	Block Book::Learn(std::uint32_t a_id, Role a_role, std::size_t a_node)
	{
		const auto it = records.find(a_id);
		return it == records.end() ? Block::kBadNode : Paths::Learn(it->second, a_role, a_node, Earned(a_id), Unlimited(a_id));
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
			repaired += Repair(record, before.contains(id) ? kUnlimited : EarnedAt(record.peak), before.contains(id)) ? 1 : 0;
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
		return node && a_rank >= 1 && a_rank <= node->most ? RankCost(*node, a_rank) : 0;
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
			return 40.0f;
		case Ability::kWarCry:
		case Ability::kRiposte:
			return 30.0f;
		case Ability::kChaosDown:
		case Ability::kSunder:
		case Ability::kPinningShot:
			return 25.0f;
		case Ability::kStandFast:
			return 60.0f;
		case Ability::kWard:
			return 90.0f;
		case Ability::kRenewal:
		case Ability::kBerserk:
		case Ability::kVolley:
			return 45.0f;
		case Ability::kHeal:
			return 40.0f;
		case Ability::kRejuvenate:
			return RejuvenatePolicy::Cooldown(Value(Perk::kRejuvenate, rank));
		case Ability::kEarthshaker:
		case Ability::kDeadeye:
		case Ability::kSurge:
		case Ability::kSpectralWolf:
			return 40.0f;
		case Ability::kDisarm:
			return rank == 1 ? 40.0f : (rank == 2 ? 35.0f : 30.0f);
		case Ability::kSecondWind:
			return 60.0f - branch(Perk::kSecondWindUnyielding);
		case Ability::kArrowRain:
			return 50.0f;
		case Ability::kDragonBreath:
			return 38.0f;
		case Ability::kBurst:
			return 30.0f;
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
		case Perk::kIronHide:
		case Perk::kBerserkThickSkull:
		case Perk::kSecondWindResolve:
			return Trf("+{} armor", n);
		case Perk::kSunder:
		case Perk::kDisarmExpose:
        case Perk::kDeadeye: return Trf("{} damage",n);
        case Perk::kDeadeyeHawkeye: return Trf("+{} m",n);
		case Perk::kVolleyBroadhead:
			return Trf("-{} armor", n);
		case Perk::kTauntIntimidate:
		case Perk::kWarCryUnnerve:
			return Trf("-{}% damage", n);
		case Perk::kBerserk:
		case Perk::kExecutioner:
		case Perk::kFlanker:
		case Perk::kOpportunist:
		case Perk::kVolleyRallyFire:
		case Perk::kDeadeyePatience:
		case Perk::kTriage:
			return std::format("+{}%", n);
		case Perk::kTaunt:
		case Perk::kRejuvenate:
		case Perk::kRiposte:
		case Perk::kPinningShot:
			return std::format("{} s", n);
		case Perk::kBodyguardInterpose:
		case Perk::kStandFastBreath:
		case Perk::kRenewalRoots:
		case Perk::kChaosDownLingering:
		case Perk::kPinningHeavy:
			return std::format("+{} s", n);
		case Perk::kSecondWindUnyielding:
			return std::format("-{} s", n);
		case Perk::kHeal:
			return Trf("{}% health a second", n);
		case Perk::kHealInvigorate:
			return Trf("{}% stamina a second", n);
		case Perk::kHealWide:
		case Perk::kChaosDownWide:
			return Trf("+{} wide", n);
		case Perk::kChaosDown:
			return Trf("-{}% movement and attack speed", n);
		case Perk::kSecondWind:
		case Perk::kGuardianSpirit:
		case Perk::kLastStand:
		case Perk::kBerserkBloodlust:
		case Perk::kMomentum:
			return Trf("{}% health", n);
		case Perk::kRejuvenateWellspring:
		case Perk::kSurgeOverflow:
			return Trf("{}% magicka", n);
		case Perk::kWarCryRally:
			return Trf("{}% stamina", n);
		case Perk::kRejuvenateVigor:
			return Trf("{:.1f}% stamina/s", v);
		case Perk::kEndurance:
			return Trf("+{}% stamina", n);
		case Perk::kSerenity:
		case Perk::kArcaneFlow:
		case Perk::kSurgeConduit:
			return Trf("+{}% magicka", n);
		case Perk::kWard:
			return Trf("{}% protection chance", n);
		case Perk::kWardShared:
			return Trf("+{:g} m reach", v / 70.0f);
		case Perk::kWardSpellward:
			return Trf("+{} s blessing", n);
		case Perk::kRenewal:
			return Trf("{}% a second", n);
		case Perk::kWarCry:
		case Perk::kEarthshaker:
			return Trf("{} reach", n);
		case Perk::kStandFastUnbroken:
		case Perk::kEarthshakerFault:
			return Trf("+{} reach", n);
		case Perk::kSunderCrack:
			return v < 0.55f ? std::string(Tr("light stagger")) : (v < 0.85f ? std::string(Tr("firm stagger")) : std::string(Tr("heavy stagger")));
		case Perk::kSunderShatter:
		case Perk::kBurstSearing:
		case Perk::kUnravel:
			return Trf("-{}% resists", n);
		case Perk::kHeavyHands:
		case Perk::kShieldMaster:
			return Trf("{}x as often", n);
		case Perk::kDisarm:
			return Trf("+{} levels", n);
		case Perk::kRiposteCounter:
			return Trf("{}x damage", n);
		case Perk::kSpectralWolf:
			return Trf("{}x  Â·  {}% bleed", n, Number(s));
		case Perk::kWolfHemorrhage:
			return Trf("+{} s bleed", n);
		case Perk::kVolley:
			return Trf("{} arrows, {} s", n, Number(s));
		case Perk::kArrowRain:
			return Trf("{} arrows", n);
		case Perk::kFineArrows:
			return std::string(v < 1.5f ? Tr("Steel arrows") : (v < 2.5f ? Tr("Elven arrows") : Tr("Ebony arrows")));
		case Perk::kSpellTouched:
			return Trf("{}% elemental chance", n);
		case Perk::kSteadyBreath:
			return Trf("+{}% stamina recovery", n);
		case Perk::kArrowRainDownpour:
			return Trf("+{} arrows", n);
		case Perk::kSurge:
			return Trf("{}x magicka", n);
		case Perk::kDragonBreath: return Trf("{} damage/s Â· {} reach", n, Number(DragonBreath::Reach(r)));
		case Perk::kBreathReach: return Trf("+{}% reach", n);
		case Perk::kBreathLingering: return Trf("+{} s", n);
		case Perk::kBurst:
			return Trf("{}  Â·  {} reach", Tr(kTiers[static_cast<std::size_t>(std::clamp(static_cast<int>(v), 0, 3))]),
				Number(Blast::kRadius[static_cast<std::size_t>(std::clamp(static_cast<int>(v), 1, 3) - 1)]));
		case Perk::kMagnus:
			return r == 1 ? std::format("{} s", n) : (r == 2 ? std::format("{} s  Â·  +{}%", n, Number(s)) : Trf("{} s  Â·  +{}%  Â·  ward", n, Number(s)));
		case Perk::kSpellShield:
			return std::format("{}% / {}%", n, Number(s));
		case Perk::kBodyguard:
			return Trf("-{}% damage, {} s", n, Number(s));
		case Perk::kStandFast:
			return Trf("{}% absorbed", n);
		case Perk::kBodyguardRush:
			return "+4 s"s;
		case Perk::kRenewalLifeline:
			return std::string(Tr("back up"));
		case Perk::kEarthshakerAftershock:
			return std::string(Tr("a second tremor"));
		case Perk::kDisarmSweep:
			return std::string(Tr("knocked down"));
		case Perk::kRiposteDisarming:
			return "disarms"s;
		case Perk::kWolfMaul:
			return Trf("+{:.1f} s frozen", kWolfMaulHold);
		case Perk::kPinningTwin:
			return std::string(Tr("+10% speed"));
		case Perk::kArrowRainPinning:
			return "staggers"s;
		case Perk::kWardMantle:
			return std::string(Tr("Flame Cloak"));
		case Perk::kBurstTwin:
			return Trf("second burst: {}%", Number(kBurstEchoShare * 100.0f));
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
			return Trf("Challenges every enemy in reach. Those it holds turn on them for {} s.", n);
		case Perk::kTauntBrace:
			return Trf("Each taunt braces them with +{} armor for as long as it holds.", n);
		case Perk::kTauntIntimidate:
			return Trf("Enemies a taunt holds deal {}% less damage.", n);
		case Perk::kBodyguard:
			return Trf("For {:.0f} s, they and enemies who strike them deal {}% less weapon and spell damage. Enemy weakness lasts {:.0f} s after striking; the strongest Weak Will applies. The spectral helmet fades into lingering wisps.", s, n, s);
		case Perk::kBodyguardInterpose:
			return Trf("Weak Will remains active for {} s longer.", n);
		case Perk::kBodyguardRush:
			return std::string(Tr("Enemies struck by Weak Will remain weakened for 4 s longer after their last attack."));
		case Perk::kChaosDown:
			return Trf("Slows movement and weapon attacks of enemies within {:.0f} of the tank by {}% for {:.0f} s. Repeated casts refresh the slow instead of stacking it.", ChaosDown::kReach, n, ChaosDown::kSeconds);
		case Perk::kChaosDownWide:
			return Trf("Chaos Down reaches {} further around the tank.", n);
		case Perk::kChaosDownLingering:
			return Trf("Chaos Down slows enemies for {} s longer.", n);
		case Perk::kStandFast:
			return Trf("When a living ally within {:.0f} falls below 25% health in combat, become their guardian for {:.0f} s. Absorbs {}% of nearby party damage after their defenses. Only one Last Bastion can protect the party at a time; absorbed damage can down or kill the tank.", kStandFastReach, kStandFastSeconds, n);
		case Perk::kStandFastUnbroken:
			return Trf("Last Bastion protects allies {} further away.", n);
		case Perk::kStandFastBreath:
			return Trf("Last Bastion lasts {} s longer.", n);
		case Perk::kIronHide:
			return Trf("+{} armor while they fight.", n);
		case Perk::kShieldMaster:
			return Trf("They bash {}x as often and raise the shield sooner.", n);
		case Perk::kLastStand:
			return Trf("Once a fight, falling below a quarter of their health, they recover {}% of it and gain +{:.0f} armor for {:.0f} s.", n,
				kLastStandArmor, kLastStandSeconds);
		// Healer
		case Perk::kHeal:
			return Trf("Raises a circle of healing light on the ground where it helps most, for {:.0f} s: you, the companions and their summons in it heal {}% of their health a second, the undead in it burn for 1/1.5/2% a second by rank (a boss half), and hurt companions walk into it. Spends magicka when activated.",
				HealCircle::kSeconds, n);
		case Perk::kHealInvigorate:
			return Trf("In the circle, stamina comes back {}% of its most a second too.", n);
		case Perk::kHealWide:
			return Trf("The circle reaches {} further.", n);
		case Perk::kRejuvenate:
			return Trf("Restores {:g}% of maximum magicka each second (up to {:.0f}) while inside the circle for {} s. Overlapping circles do not stack.", RejuvenatePolicy::Rate(r)*100, RejuvenatePolicy::Cap(r), n);
		case Perk::kRejuvenateWellspring:
			return Trf("Rejuvenate restores {}% magicka at once to nearby allies, up to 30. Allies already in a circle do not receive another burst.", n);
		case Perk::kRejuvenateVigor:
			return Trf("Inside Rejuvenate, restores {:.1f}% maximum stamina per second too, up to 6.", v);
		case Perk::kWard:
			return Trf("A spectral Wispmother blesses you and companions within {:g} m for {:g} s. Each hostile hit has a {}% chance to deal no damage. Blessings do not stack.", MoonFx::Radius(r)/70.0f, MoonFx::Duration(r), n);
		case Perk::kWardShared:
			return Trf("Azura's Moon reaches {:g} m farther.", v / 70.0f);
		case Perk::kWardSpellward:
			return Trf("Azura's blessing lasts {} s longer.", n);
		case Perk::kWardMantle:
			return Trf("Whoever is warded is also wrapped in Flame Cloak for {:.0f} s.", kWardSeconds);
		case Perk::kRenewal:
			return Trf("Everyone within reach regains {}% of their health every second for {:.0f} s.", n, kRenewalSeconds);
		case Perk::kRenewalRoots:
			return Trf("Renewal lasts {} s longer.", n);
		case Perk::kRenewalLifeline:
			return std::string(Tr("Anyone down within reach when Renewal starts gets straight back up with a third of their health."));
		case Perk::kTriage:
			return Trf("The healing circle and Renewal give {}% more to anyone below a third of their health.", n);
		case Perk::kSerenity:
			return Trf("Their own magicka comes back {}% faster while they fight.", n);
		case Perk::kGuardianSpirit:
			return Trf("Once a fight, when your health falls below {:.0f}%, they heal you for {}% of it at once, wherever they are.", kGuardianLine * 100.0f, n);
		// Bruiser
		case Perk::kWarCry:
			return Trf("A roar that staggers every enemy within {} {}.", n, Strength(s));
		case Perk::kWarCryRally:
			return Trf("The roar gives allies within {:.0f} back {}% of their stamina.", kRallyReach, n);
		case Perk::kWarCryUnnerve:
			return Trf("Enemies it staggers deal {}% less damage for {:.0f} s.", n, kUnnerveSeconds);
		case Perk::kBerserk:
			return Trf("{:.0f} s of fury: {}% more damage, and a power attack at every chance.", kBerserkSeconds, n);
		case Perk::kBerserkBloodlust:
			return Trf("Each kill while berserk heals {}% of their health and adds 3 s.", n);
		case Perk::kBerserkThickSkull:
			return Trf("+{} armor while berserk.", n);
		case Perk::kSunder:
			return Trf("A blow that breaks their target's guard: {} less armor for {:.0f} s, so everyone hits it harder.", n, kSunderSeconds);
		case Perk::kSunderCrack:
			return Trf("The blow also staggers them {}.", Strength(v));
		case Perk::kSunderShatter:
			return Trf("Sunder also strips {}% of its resistance to magic, fire, frost and shock.", n);
		case Perk::kEarthshaker:
			return Trf("Slams the ground and rocks burst out of it: every enemy within {} falls and takes {}x their weapon's damage, and stays down a while.", n, Number(s)) +
				(r == 3 ? " "s + std::string(Tr("Those light enough are thrown into the air.")) : ""s);
		case Perk::kEarthshakerAftershock:
			return std::string(Tr("A second tremor 1.5 s later staggers everyone still standing."));
		case Perk::kEarthshakerFault:
			return Trf("Earthshaker reaches {} further.", n);
		case Perk::kHeavyHands:
			return Trf("They power attack {}x as often.", n);
		case Perk::kMomentum:
			return Trf("Each kill heals {}% of their health.", n);
		case Perk::kExecutioner:
			return Trf("{}% more damage against enemies below {:.0f}% health.", n, kExecuteLine * 100.0f);
		// Fighter
		case Perk::kDisarm:
			return Trf("Knocks the weapon out of their opponent's hands, up to {} levels above their own.", n);
		case Perk::kDisarmExpose:
			return Trf("A disarmed enemy has {} less armor for {:.0f} s.", n, kExposeSeconds);
		case Perk::kDisarmSweep:
			return std::string(Tr("Disarm also sweeps them off their feet."));
		case Perk::kSecondWind:
			return Trf("Catches their breath mid-fight: {}% of their health and {}% of their stamina back.", n, Number(s));
		case Perk::kSecondWindResolve:
			return Trf("+{} armor for {:.0f} s after.", n, kResolveSeconds);
		case Perk::kSecondWindUnyielding:
			return Trf("Second Wind is ready {} s sooner.", n);
		case Perk::kRiposte:
			return Trf("Guard the next melee blow for up to {} s. Counter and push the attacker down for {} s, dealing {}x weapon base damage on landing.", n, RiposteFx::DownSeconds(a_rank), RiposteFx::ImpactMultiplier(a_rank));
		case Perk::kRiposteCounter:
			return Trf("The counter strike also deals {}x weapon base damage before the knockback.", n);
		case Perk::kRiposteDisarming:
			return std::string(Tr("The first blow they turn aside also disarms the attacker, if the weapon can be taken."));
		case Perk::kSpectralWolf:
			return Trf("Only while they stand before their target: a giant spectral wolf's head takes shape in front of it, jaws open round it, and bites down. Whatever its jaws close on takes {}x their weapon's damage, is held frozen mid-stagger in a ghostly smoke for {:.1f} s, and bleeds {}% of its health over {:.0f} s (a boss half as long and half as much).", n, kWolfHold[static_cast<std::size_t>(r - 1)], Number(s), kWolfBleedSeconds);
		case Perk::kWolfMaul:
			return Trf("The wolf's bite holds whoever it catches frozen {:.1f} s longer.", kWolfMaulHold);
		case Perk::kWolfHemorrhage:
			return Trf("The wolf's wounds bleed {} s longer, as fast as ever.", n);
		case Perk::kFlanker:
			return Trf("They work round to their opponent's side, and deal {}% more damage.", n);
		case Perk::kEndurance:
			return Trf("Stamina comes back {}% faster while they fight.", n);
		case Perk::kOpportunist:
			return Trf("{}% more damage against an opponent who is staggered or disarmed.", n);
		// Archer
		case Perk::kVolley:
			return Trf("Looses {} arrows in quick succession at their target; every archer and mage turns on it for {} s.", n, Number(s));
		case Perk::kVolleyRallyFire:
			return Trf("You and the party deal {}% more weapon damage while a volley lasts.", n);
		case Perk::kVolleyBroadhead:
			return Trf("The volley's target has {} less armor while it lasts.", n);
		case Perk::kDeadeye:
			return Trf("Primes the next bow shot for 12 s. The arrow explodes wherever it lands, dealing up to {} magic damage and staggering enemies within {:.1f} m. Damage falls with distance and magic resistance.", n, ArrowArtsFx::Radius(a_rank)/70.f);
		case Perk::kDeadeyeHawkeye:
			return Trf("The explosion reaches {} m farther.", n);
		case Perk::kDeadeyePatience:
			return Trf("Explosive Arrow deals {}% more explosion damage.", n);
		case Perk::kPinningShot:
			return Trf("Retreats up to {:.1f} m. A spectral archer distracts enemies attacking them for {:.0f} s, then fades. Moves 30% faster for {} s and takes {:.0f}% less damage. Rank III ends with a nearby stagger gust.", WindstepFx::Reach(a_rank)/70.f, WindstepFx::EchoHold(a_rank), n, WindstepFx::Protection(a_rank)*100);
		case Perk::kPinningHeavy:
			return Trf("Windstep movement speed and protection last {} s longer.", n);
		case Perk::kPinningTwin:
			return std::string(Tr("Windstep grants 40% movement speed instead of 30%."));
		case Perk::kArrowRain:
			return Trf("A portal fires {} fast arrows ahead over {:.1f} s, covering enemies within {:.0f} of the target. Near misses graze for half damage. No ammunition is consumed.", n, kRainSeconds, kRainRadius);
		case Perk::kArrowRainDownpour:
			return Trf("{} more arrows fall.", n);
		case Perk::kArrowRainPinning:
			return std::string(Tr("Every enemy under the rain staggers."));
		case Perk::kSteadyBreath:
			return Trf("Stamina recovers {}% faster in combat, helping them keep shooting and repositioning.", n);
		case Perk::kFineArrows:
			return Trf("Provides unlimited {} for their bow. Better arrows you give them are kept.", v < 1.5f ? Tr("steel arrows") : (v < 2.5f ? Tr("elven arrows") : Tr("ebony arrows")));
		case Perk::kSpellTouched:
			return Trf("Arrows have a {}% chance to carry fire, frost or lightning in flight. An unblocked hit deals 8 extra damage of that element, reduced by resistance.", n);
		// Mage
		case Perk::kSurge:
			return Trf("{:.0f} s of arcane surge: their magicka flows back {}x as fast.", kSurgeSeconds, n);
		case Perk::kSurgeOverflow:
			return Trf("Arcane Surge also restores {}% of their magicka at once.", n);
		case Perk::kSurgeConduit:
			return Trf("While it lasts, your magicka and the party's flows back {}% faster too.", n);
		case Perk::kDragonBreath:
			return Trf("A spectral dragon opens its jaws and breathes their element for {} s, reaching {} and dealing {} damage per second to enemies inside the stream. Frost also drains stamina; lightning drains magicka. Rank III summons red Alduin.", Number(DragonBreath::Duration(r)), Number(DragonBreath::Reach(r)), n);
		case Perk::kBreathReach: return Trf("Dragon's Breath reaches {}% farther, widening with its reach.", n);
		case Perk::kBreathLingering: return Trf("Dragon's Breath continues for {} additional seconds.", n);
		case Perk::kBurst:
			return Trf("A spark of their element flies to the target's feet and bursts in fire, frost or lightning, striking every enemy within {} for {} damage at the middle, falling off toward the edge.",
				Number(Blast::kRadius[static_cast<std::size_t>(std::clamp(static_cast<int>(v), 1, 3) - 1)]), Number(kBurstDamage[static_cast<std::size_t>(r - 1)])) +
				(r == 3 ? " "s + std::string(Tr("Grand: a giant fireball ringed by smaller explosions with sparks flying over it, a huge crystal of ice in the middle, or a second thunderbolt.")) : ""s);
		case Perk::kBurstSearing:
			return Trf("After the hit, every enemy struck loses {}% of its fire, frost and shock resistance for {:.0f} s.", n, kSearingSeconds);
		case Perk::kBurstTwin:
			return Trf("A second burst follows for {}% damage, at another enemy if one is near.", Number(kBurstEchoShare * 100.0f));
		case Perk::kMagnus:
			if (r == 1) {
				return Trf("The power of Magnus: for {} s their magicka never runs dry.", n);
			}
			if (r == 2) {
				return Trf("For {} s their magicka never runs dry, and their destruction spells strike {}% harder.", n, Number(s));
			}
			return Trf("For {} s their magicka never runs dry, their destruction spells strike {}% harder, and they resist magic by {}%.", n, Number(s),
				Number(Third(a_perk, r)));
		case Perk::kArcaneFlow:
			return Trf("Magicka comes back {}% faster while they fight.", n);
		case Perk::kSpellShield:
			return Trf("Resists magic by {}%, and absorbs {}% of the spells cast at them.", n, Number(s));
		case Perk::kUnravel:
			return Trf("Whoever they fight loses {}% of its fire, frost and shock resistance while they do.", n);
		default:
			return {};
		}
	}
}
