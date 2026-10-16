#include "FWM/Paths.h"

#include <cmath>
#include <cstdio>
#include <set>

namespace
{
	int checks = 0;
	int failures = 0;

	void Check(bool a_ok, const char* a_what, int a_line)
	{
		++checks;
		if (!a_ok) {
			++failures;
			std::printf("FAIL line %d: %s\n", a_line, a_what);
		}
	}

	bool Near(float a, float b, float eps = 0.001f) { return std::fabs(a - b) <= eps; }

#define CHECK(x) Check((x), #x, __LINE__)

	using namespace FWM;
	using namespace FWM::Paths;

	std::size_t At(Role a_role, Perk a_perk) { return static_cast<std::size_t>(IndexOf(a_role, a_perk)); }

	void Shape()
	{
		for (std::size_t r = 0; r < kRoles; ++r) {
			const auto  role = static_cast<Role>(r);
			const auto& path = PathOf(role);
			std::set<Perk> seen;
			int            abilityCount = 0, traits = 0, defaults = 0, capstones = 0;
			for (std::size_t i = 0; i < kNodes; ++i) {
				const auto& node = path[i];
				if (node.Empty()) {
					// Only ever the two places a capstone leaves.
					CHECK(i == 10 || i == 11);
					CHECK(path[9].capstone);
					continue;
				}
				CHECK(seen.insert(node.perk).second);
				CHECK(node.most >= 1 && node.most <= 3);
				CHECK(node.base <= node.most);
				CHECK(node.tier <= 2);
				CHECK(!node.name.empty());
				CHECK(node.glyph != 0);
				CHECK(node.cost >= 1 && node.step >= 1);
				if (node.kind == Kind::kAbility) {
					++abilityCount;
					defaults += node.base > 0 ? 1 : 0;
					CHECK(node.ability != Ability::kNone);
					CHECK(node.most == 3);
					if (node.capstone) {
						// Alone in the last ability's place.
						++capstones;
						CHECK(i == 9 && path[10].Empty() && path[11].Empty());
						continue;
					}
					// Both branches follow it and belong to it.
					CHECK(i + 2 < kNodes && path[i + 1].kind == Kind::kBranch && path[i + 2].kind == Kind::kBranch);
					CHECK(path[i + 1].ability == node.ability && path[i + 2].ability == node.ability);
					CHECK(path[i + 1].tier == node.tier && path[i + 2].tier == node.tier);
				}
				if (node.kind == Kind::kTrait) {
					++traits;
					CHECK(node.base == 0);
				}
			}
			CHECK(abilityCount == static_cast<int>(kAbilitiesPerPath));
			CHECK(traits == static_cast<int>(kTraitsPerPath));
			CHECK(defaults == 2);
			CHECK(capstones == (role == Role::kMage ? 1 : 0));
			const auto d = Defaults(role);
			CHECK(d[0] != Ability::kNone && d[1] != Ability::kNone && d[0] != d[1]);
		}
		// Every ability lives on at least one path (Ward on two), but the retired Atronach.
		for (std::size_t a = 1; a < kAbilityCount; ++a) {
			if (static_cast<Ability>(a) == Ability::kAtronachRetired) {
				for (std::size_t r = 0; r < kRoles; ++r) {
					CHECK(IndexOf(static_cast<Role>(r), Ability::kAtronachRetired) < 0);
				}
				continue;
			}
			int on = 0;
			for (std::size_t r = 0; r < kRoles; ++r) {
				on += IndexOf(static_cast<Role>(r), static_cast<Ability>(a)) >= 0 ? 1 : 0;
			}
			CHECK(on >= 1);
			CHECK(!Info(static_cast<Ability>(a)).name.empty());
			CHECK(Info(static_cast<Ability>(a)).glyph != 0);
		}
		CHECK(IndexOf(Role::kHealer, Ability::kWard) >= 0 && IndexOf(Role::kMage, Ability::kWard) >= 0);
		CHECK(Signature() != 0);
	}

	void Words()
	{
		for (std::size_t r = 0; r < kRoles; ++r) {
			for (const auto& node : PathOf(static_cast<Role>(r))) {
				for (int rank = 1; rank <= node.most; ++rank) {
					CHECK(!Describe(node.perk, rank).empty());
				}
			}
		}
		CHECK(Describe(Perk::kWard, 1).find("Oakflesh") != std::string::npos);
		CHECK(Describe(Perk::kWard, 3).find("Ebonyflesh") != std::string::npos);
		CHECK(Describe(Perk::kMagnus, 1).find("never runs dry") != std::string::npos);
		CHECK(Describe(Perk::kMagnus, 3).find("50%") != std::string::npos);
		CHECK(Near(Third(Perk::kMagnus, 3), 50.0f) && Near(Third(Perk::kMagnus, 2), 0.0f));
		CHECK(Describe(Perk::kWardShared, 3).find("Every companion") != std::string::npos);
		CHECK(Near(Value(Perk::kTaunt, 1), 10.0f) && Near(Value(Perk::kTaunt, 2), 15.0f) && Near(Value(Perk::kTaunt, 3), 30.0f));
		CHECK(Near(Value(Perk::kTaunt, 0), 0.0f));
		CHECK(Near(Second(Perk::kWarCry, 3), 1.0f));
	}

	void Points()
	{
		CHECK(Earned(5, 10) == 0);
		CHECK(Earned(10, 10) == 0);
		CHECK(Earned(11, 10) == 1);
		CHECK(Earned(50, 10) == 40);
		CHECK(Earned(50, 0) == 50);
		CHECK(Earned(50, -3) == 50);

		auto record = Fresh();
		CHECK(Spent(record) == 0);
		CHECK(Rank(record, Role::kTank, Ability::kTaunt) == 1);
		CHECK(Rank(record, Role::kTank, Ability::kChallenge) == 0);
		CHECK(record.slots[static_cast<std::size_t>(Role::kTank)][0] == Ability::kTaunt);
		CHECK(record.slots[static_cast<std::size_t>(Role::kTank)][1] == Ability::kBodyguard);
		CHECK(record.slots[static_cast<std::size_t>(Role::kHealer)][0] == Ability::kHeal);
		CHECK(record.slots[static_cast<std::size_t>(Role::kMage)][1] == Ability::kWard);
	}

	void Learning()
	{
		const auto tank = Role::kTank;
		auto       record = Fresh();
		const int  earned = 20;

		// Tier two waits for five points on the path.
		CHECK(CanLearn(record, tank, At(tank, Perk::kChallenge), earned) == Block::kTier);
		// A branch never outranks its ability.
		CHECK(Learn(record, tank, At(tank, Perk::kTauntBrace), earned) == Block::kNone);
		CHECK(CanLearn(record, tank, At(tank, Perk::kTauntBrace), earned) == Block::kParent);
		CHECK(Learn(record, tank, At(tank, Perk::kTaunt), earned) == Block::kNone);
		CHECK(Learn(record, tank, At(tank, Perk::kTaunt), earned) == Block::kNone);
		CHECK(CanLearn(record, tank, At(tank, Perk::kTaunt), earned) == Block::kMaxed);
		CHECK(Learn(record, tank, At(tank, Perk::kTauntBrace), earned) == Block::kNone);
		CHECK(Spent(record, tank) == 4);
		CHECK(CanLearn(record, tank, At(tank, Perk::kChallenge), earned) == Block::kTier);
		CHECK(Learn(record, tank, At(tank, Perk::kIronHide), earned) == Block::kNone);
		CHECK(Spent(record, tank) == 5);
		CHECK(Learn(record, tank, At(tank, Perk::kChallenge), earned) == Block::kNone);
		CHECK(Rank(record, tank, Ability::kChallenge) == 1);
		// Tier three waits for ten below it.
		CHECK(CanLearn(record, tank, At(tank, Perk::kStandFast), earned) == Block::kTier);

		// Giving back.
		CHECK(CanForget(record, tank, At(tank, Perk::kBodyguard)) == Block::kDefault);
		CHECK(CanForget(record, tank, At(tank, Perk::kStandFast)) == Block::kNothing);
		// Iron Hide is one of the five that open tier two, which has a point in it.
		CHECK(CanForget(record, tank, At(tank, Perk::kIronHide)) == Block::kHolds);
		// Taunt at 3 holds Brace at 2.
		CHECK(Learn(record, tank, At(tank, Perk::kTauntBrace), earned) == Block::kNone);
		CHECK(CanForget(record, tank, At(tank, Perk::kTaunt)) == Block::kHolds);
		CHECK(Forget(record, tank, At(tank, Perk::kChallenge)) == Block::kNone);
		CHECK(Forget(record, tank, At(tank, Perk::kIronHide)) == Block::kNone);

		// Out of points.
		auto poor = Fresh();
		CHECK(CanLearn(poor, tank, At(tank, Perk::kTaunt), 0) == Block::kNoPoints);
		CHECK(Learn(poor, tank, At(tank, Perk::kTaunt), 1) == Block::kNone);
		CHECK(CanLearn(poor, tank, At(tank, Perk::kTaunt), 1) == Block::kNoPoints);
		// Points are shared by every path.
		CHECK(CanLearn(poor, Role::kArcher, At(Role::kArcher, Perk::kFletcher), 1) == Block::kNoPoints);
		CHECK(CanLearn(poor, Role::kArcher, At(Role::kArcher, Perk::kFletcher), 2) == Block::kNone);

		// Reset gives everything on a path back and leaves the others.
		Learn(poor, Role::kArcher, At(Role::kArcher, Perk::kFletcher), 2);
		Reset(poor, tank);
		CHECK(Spent(poor, tank) == 0);
		CHECK(Spent(poor) == 1);
		CHECK(Rank(poor, tank, Ability::kTaunt) == 1);
	}

	void Slots()
	{
		const auto fighter = Role::kFighter;
		auto       record = Fresh();
		const auto f = static_cast<std::size_t>(fighter);
		CHECK(Equip(record, fighter, 0, Ability::kRiposte) == Block::kNotLearned);
		CHECK(Equip(record, fighter, 0, Ability::kVolley) == Block::kBadNode);
		// Swapping the two.
		CHECK(Equip(record, fighter, 0, Ability::kSecondWind) == Block::kNone);
		CHECK(record.slots[f][0] == Ability::kSecondWind && record.slots[f][1] == Ability::kDisarm);

		// Learn Riposte (five in tier one first), carry it, then forget it: the slot refills.
		for (int i = 0; i < 2; ++i) {
			Learn(record, fighter, At(fighter, Perk::kDisarm), 30);
			Learn(record, fighter, At(fighter, Perk::kSecondWind), 30);
		}
		Learn(record, fighter, At(fighter, Perk::kFlanker), 30);
		CHECK(Spent(record, fighter) == 5);
		CHECK(Learn(record, fighter, At(fighter, Perk::kRiposte), 30) == Block::kNone);
		CHECK(Equip(record, fighter, 1, Ability::kRiposte) == Block::kNone);
		CHECK(record.slots[f][1] == Ability::kRiposte);
		CHECK(Forget(record, fighter, At(fighter, Perk::kRiposte)) == Block::kNone);
		CHECK(record.slots[f][1] != Ability::kRiposte && record.slots[f][1] != Ability::kNone && record.slots[f][1] != record.slots[f][0]);
	}

	void Repairs()
	{
		const auto mage = Role::kMage;
		const auto m = static_cast<std::size_t>(mage);
		auto       record = Fresh();
		record.ranks[m][At(mage, Perk::kSurge)] = 9;           // past its highest rank
		record.ranks[m][At(mage, Perk::kWard)] = 0;            // below a default's
		record.ranks[m][At(mage, Perk::kWardMantle)] = 1;      // above a parent at 1 is fine
		record.ranks[m][At(mage, Perk::kMagnus)] = 2;          // a capstone with nothing under it
		record.slots[m] = { Ability::kAtronachRetired, Ability::kMagnus };
		CHECK(Repair(record, 40));
		CHECK(Rank(record, mage, Ability::kSurge) == 3);
		CHECK(Rank(record, mage, Ability::kWard) == 1);
		CHECK(Rank(record, mage, Ability::kMagnus) == 0);
		CHECK(record.slots[m][1] != Ability::kMagnus);
		CHECK(record.slots[m][0] != Ability::kAtronachRetired && record.slots[m][0] != Ability::kNone && record.slots[m][1] != record.slots[m][0]);
		CHECK(!Repair(record, 40));

		// Overspent (the level limit went up): the fullest path gives back until it fits.
		auto rich = Fresh();
		for (std::size_t i = 0; i < kNodes; ++i) {
			for (int k = 0; k < 3; ++k) {
				Learn(rich, Role::kArcher, i, 100);
			}
		}
		const int all = Spent(rich, Role::kArcher);
		CHECK(all > 30);
		CHECK(Repair(rich, 12));
		CHECK(Spent(rich) == 12);
		CHECK(Repair(rich, 0));
		CHECK(Spent(rich) == 0);
	}

	void Cooldowns()
	{
		auto record = Fresh();
		CHECK(Near(Cooldown(record, Role::kTank, Ability::kTaunt), 16.0f));
		CHECK(Near(Cooldown(record, Role::kHealer, Ability::kHeal), 60.0f));
		CHECK(Near(Cooldown(record, Role::kHealer, Ability::kRejuvenate), 60.0f));
		for (int i = 0; i < 2; ++i) {
			Learn(record, Role::kHealer, At(Role::kHealer, Perk::kHeal), 30);
		}
		Learn(record, Role::kHealer, At(Role::kHealer, Perk::kHealSwift), 30);
		Learn(record, Role::kHealer, At(Role::kHealer, Perk::kHealSwift), 30);
		CHECK(Near(Cooldown(record, Role::kHealer, Ability::kHeal), 40.0f));
		for (std::size_t a = 1; a < kAbilityCount; ++a) {
			for (std::size_t r = 0; r < kRoles; ++r) {
				if (IndexOf(static_cast<Role>(r), static_cast<Ability>(a)) >= 0) {
					CHECK(Cooldown(record, static_cast<Role>(r), static_cast<Ability>(a)) > 0.0f);
				}
			}
		}
	}

	void Codec()
	{
		auto a = Fresh();
		a.peak = 37;
		Learn(a, Role::kBruiser, At(Role::kBruiser, Perk::kWarCry), 27);
		Learn(a, Role::kBruiser, At(Role::kBruiser, Perk::kWarCryRally), 27);
		Learn(a, Role::kBruiser, At(Role::kBruiser, Perk::kHeavyHands), 27);
		Equip(a, Role::kBruiser, 0, Ability::kBerserk);
		auto b = Fresh();
		b.peak = 12;
		const std::vector<Entry> in{ { 0x0001A2B3u, a }, { 0xFE000801u, b } };
		const auto bytes = Encode(in);
		const auto out = Decode(bytes, kRecordVersion);
		CHECK(out.size() == 2);
		CHECK(out[0].first == 0x0001A2B3u && out[0].second == a);
		CHECK(out[1].first == 0xFE000801u && out[1].second == b);
		// Short or unknown data loads what it can and never reads past the end. The last four bytes
		// are the count of bonus entries (none here).
		auto cut = bytes;
		cut.resize(cut.size() - 4 - 3);
		CHECK(Decode(cut, kRecordVersion).size() == 1);
		auto trailer = bytes;
		trailer.resize(trailer.size() - 2);
		CHECK(Decode(trailer, kRecordVersion).size() == 2);
		CHECK(Decode(bytes, 0).empty());
		CHECK(Decode({}, kRecordVersion).empty());
	}

	// Every node of a path at its highest rank, capstone included.
	void MasterPath(Record& a_record, Role a_role)
	{
		for (int pass = 0; pass < 4; ++pass) {
			for (std::size_t i = 0; i < kNodes; ++i) {
				for (int k = 0; k < 3; ++k) {
					Learn(a_record, a_role, i, kUnlimited);
				}
			}
		}
	}

	void Bonus()
	{
		const auto archer = Role::kArcher;
		const auto a = static_cast<std::size_t>(archer);
		const auto tank = Role::kTank;
		auto       record = Fresh();
		// Not before every node of the path is mastered.
		CHECK(!Mastered(record, archer));
		CHECK(CanInvest(record, archer, 99, 50) == Block::kNotMastered);
		MasterPath(record, archer);
		CHECK(Mastered(record, archer));
		const int tree = Mastery(archer);
		CHECK(OnTree(record, archer) == tree && Spent(record, archer) == tree && Invested(record, archer) == 0);
		// Mage: the capstone too.
		auto mage = Fresh();
		MasterPath(mage, Role::kMage);
		CHECK(Mastered(mage, Role::kMage) && OnTree(mage, Role::kMage) == Mastery(Role::kMage));
		--mage.ranks[static_cast<std::size_t>(Role::kMage)][At(Role::kMage, Perk::kMagnus)];
		CHECK(!Mastered(mage, Role::kMage));

		// Leftover points, one at a time: spent on the path, and of their points in all.
		CHECK(CanInvest(record, archer, tree, 50) == Block::kNoPoints);
		CHECK(Invest(record, archer, tree + 2, 50) == Block::kNone);
		CHECK(Invest(record, archer, tree + 2, 50) == Block::kNone);
		CHECK(Invest(record, archer, tree + 2, 50) == Block::kNoPoints);
		CHECK(Invested(record, archer) == 2 && Spent(record, archer) == tree + 2 && OnTree(record, archer) == tree && Spent(record) == tree + 2);
		// Where it stops is the bonus's to say.
		CHECK(CanInvest(record, archer, 99, 0) == Block::kBonusFull);
		CHECK(CanInvest(record, archer, 99, 1) == Block::kNone);
		// Only that path's bonus.
		CHECK(Invested(record, tank) == 0);
		CHECK(CanInvest(record, tank, 99, 50) == Block::kNotMastered);
		// They take from what can be learned anywhere else.
		CHECK(CanLearn(record, Role::kFighter, At(Role::kFighter, Perk::kFlanker), tree + 2) == Block::kNoPoints);
		CHECK(CanLearn(record, Role::kFighter, At(Role::kFighter, Perk::kFlanker), tree + 3) == Block::kNone);
		// They open no tiers.
		auto partial = Fresh();
		partial.bonus[static_cast<std::size_t>(tank)] = 9;
		CHECK(!TierOpen(partial, tank, 1));
		CHECK(CanLearn(partial, tank, At(tank, Perk::kChallenge), 99) == Block::kTier);
		// Back out one at a time, and all of them when the path starts over.
		CHECK(Divest(record, archer) == Block::kNone);
		CHECK(Invested(record, archer) == 1);
		CHECK(CanDivest(record, Role::kMage) == Block::kNothing);
		Reset(record, archer);
		CHECK(Spent(record) == 0 && Invested(record, archer) == 0);
		CHECK(Divest(record, archer) == Block::kNothing);

		// Overspent after a load: the bonus gives back first, the ranks only after.
		auto over = Fresh();
		MasterPath(over, archer);
		over.bonus[a] = 5;
		CHECK(Repair(over, tree + 2));
		CHECK(Invested(over, archer) == 2 && OnTree(over, archer) == tree);
		CHECK(Repair(over, tree - 1));
		CHECK(Invested(over, archer) == 0 && OnTree(over, archer) == tree - 1);
		// A bonus on a path no longer mastered stays: nothing about it is wrong.
		auto kept = Fresh();
		MasterPath(kept, archer);
		kept.bonus[a] = 3;
		Forget(kept, archer, At(archer, Perk::kSharpshooter));
		CHECK(!Repair(kept, 99));
		CHECK(Invested(kept, archer) == 3);
		// Unlimited points and back: the bonus as it was before.
		auto cheat = kept;
		const auto before = cheat;
		cheat.bonus[a] = 40;
		Restore(cheat, before);
		CHECK(Invested(cheat, archer) == 3);

		// The save keeps them; a version 1 reader still reads everything else.
		auto saved = Fresh();
		MasterPath(saved, archer);
		saved.peak = 70;
		saved.bonus[a] = 7;
		saved.bonus[static_cast<std::size_t>(Role::kMage)] = 300;
		const std::vector<Entry> in{ { 0x14u, saved }, { 0x15u, Fresh() } };
		const auto               bytes = Encode(in);
		const auto               out = Decode(bytes, kRecordVersion);
		CHECK(out.size() == 2 && out[0].second == saved && out[1].second == Fresh());
		const auto old = Decode(bytes, 1);
		CHECK(old.size() == 2 && old[0].second.ranks == saved.ranks && old[0].second.bonus[a] == 0);
		// A version 1 record loads as it always did.
		auto plain = Fresh();
		plain.bonus = {};
		CHECK(Decode(Encode(std::vector<Entry>{ { 0x16u, plain } }), 1).size() == 1);
	}

	// Each companion's points are their own: earned from their own level, spent on their own
	// paths, given back to them alone.
	void Companions()
	{
		const auto              tank = Role::kTank;
		const auto              archer = Role::kArcher;
		constexpr std::uint32_t lydia = 0x000A2C94, aela = 0x0001A696, stranger = 0x00099999;
		Book                    book;
		book.points.above = 10;

		CHECK(book.Meet(stranger, 0) == nullptr);
		CHECK(book.Earned(stranger) == 0 && book.Unspent(stranger) == 0);
		CHECK(book.Learn(stranger, tank, At(tank, Perk::kTaunt)) == Block::kBadNode);
		CHECK(book.Meet(lydia, 30) != nullptr);
		CHECK(book.Meet(aela, 14) != nullptr);
		CHECK(book.Earned(lydia) == 20 && book.Earned(aela) == 4);

		// Spending is theirs alone.
		CHECK(book.Learn(lydia, tank, At(tank, Perk::kTaunt)) == Block::kNone);
		CHECK(book.Unspent(lydia) == 19 && book.Unspent(aela) == 4);
		for (const auto perk : { Perk::kVolley, Perk::kVolley, Perk::kDeadeye, Perk::kDeadeye }) {
			CHECK(book.Learn(aela, archer, At(archer, perk)) == Block::kNone);
		}
		CHECK(book.Unspent(aela) == 0);
		// Out of her own points, with Lydia's untouched: they are not hers to spend.
		CHECK(book.Learn(aela, archer, At(archer, Perk::kFletcher)) == Block::kNoPoints);
		CHECK(book.Unspent(lydia) == 19);

		// Her level rising gives her points, and nobody else; a lower level seen later takes none away.
		book.Meet(aela, 16);
		CHECK(book.Earned(aela) == 6 && book.Earned(lydia) == 20);
		book.Meet(aela, 12);
		CHECK(book.Earned(aela) == 6 && book.Unspent(aela) == 2);

		// Starting over gives back to whoever starts over.
		CHECK(book.Reset(lydia, tank) == Block::kNone);
		CHECK(book.Unspent(lydia) == 20 && book.Unspent(aela) == 2);

		// Unlimited points for one are not for the other, and going back restores only hers.
		CHECK(book.SetUnlimited(aela, true) == Block::kNone);
		CHECK(book.Unlimited(aela) && !book.Unlimited(lydia));
		CHECK(book.Earned(aela) == kUnlimited && book.Earned(lydia) == 20);
		for (int k = 0; k < 3; ++k) {
			CHECK(book.Learn(aela, archer, At(archer, Perk::kFletcher)) == Block::kNone);
		}
		CHECK(book.Learn(lydia, tank, At(tank, Perk::kTaunt)) == Block::kNone);
		CHECK(book.SetUnlimited(aela, false) == Block::kNone);
		CHECK(book.Earned(aela) == 6 && book.Unspent(aela) == 2);
		CHECK(book.Unspent(lydia) == 19);

		// Bonus points: on their own mastered path, out of their own pool.
		book.Meet(lydia, 60);
		CHECK(book.Earned(lydia) == 50);
		for (int pass = 0; pass < 3; ++pass) {
			for (std::size_t i = 0; i < kNodes; ++i) {
				for (int k = 0; k < 3; ++k) {
					(void)book.Learn(lydia, tank, i);
				}
			}
		}
		CHECK(Mastered(*book.Find(lydia), tank));
		const int left = book.Unspent(lydia);
		CHECK(left == 50 - Mastery(tank));
		CHECK(book.Invest(lydia, tank, 5) == Block::kNone);
		CHECK(book.Invest(lydia, tank, 5) == Block::kNone);
		CHECK(book.Invest(lydia, tank, 0) == Block::kBonusFull);
		CHECK(book.Unspent(lydia) == left - 2 && book.Unspent(aela) == 2);
		CHECK(Invested(*book.Find(lydia), tank) == 2 && Invested(*book.Find(aela), tank) == 0);
		CHECK(book.Invest(aela, archer, 5) == Block::kNotMastered);
		CHECK(book.Invest(stranger, tank, 5) == Block::kBadNode);
		CHECK(book.Divest(lydia, tank) == Block::kNone);
		CHECK(book.Divest(aela, tank) == Block::kNothing);
		CHECK(book.Unspent(lydia) == left - 1);

		// Saved and loaded: each record back with its own companion, and their own points.
		CHECK(book.SetUnlimited(lydia, true) == Block::kNone);
		Book loaded;
		loaded.points = book.points;
		CHECK(loaded.Load(book.Records(), book.KeptAside()) == 0);
		CHECK(loaded.Unlimited(lydia) && !loaded.Unlimited(aela));
		CHECK(loaded.Unspent(aela) == 2);
		CHECK(loaded.SetUnlimited(lydia, false) == Block::kNone);
		CHECK(loaded.Unspent(lydia) == left - 1);
		CHECK(*loaded.Find(aela) == *book.Find(aela));

		// A record overspent for its own level loses only its own points.
		auto tampered = loaded.Records();
		for (auto& [id, record] : tampered) {
			if (id == aela) {
				record.peak = 11;
			}
		}
		Book strict;
		strict.points = book.points;
		CHECK(strict.Load(tampered, {}) == 1);
		CHECK(Spent(*strict.Find(aela)) <= 1);
		CHECK(strict.Unspent(lydia) == left - 1);

		// Giving back switched off: no ranks, no bonus points, no starting over.
		strict.points.respec = false;
		CHECK(strict.Forget(lydia, tank, At(tank, Perk::kIronHide)) == Block::kNoRespec);
		CHECK(strict.Divest(lydia, tank) == Block::kNoRespec);
		CHECK(strict.Reset(lydia, tank) == Block::kNoRespec);
		strict.points.unlimited = false;
		CHECK(strict.SetUnlimited(aela, true) == Block::kNoUnlimited);
	}

	void Capstone()
	{
		const auto mage = Role::kMage;
		const auto magnus = At(mage, Perk::kMagnus);
		auto       record = Fresh();
		CHECK(CostOf(mage, magnus, 1) == 5 && CostOf(mage, magnus, 2) == 3 && CostOf(mage, magnus, 3) == 3);
		CHECK(Mastery(mage) == 41);
		CHECK(CanLearn(record, mage, magnus, 99) == Block::kCapstone);
		// A rank in everything else is not enough.
		for (const auto perk : { Perk::kSurgeOverflow, Perk::kSurgeConduit, Perk::kWardSpellward, Perk::kWardMantle, Perk::kArcaneFlow, Perk::kBurst,
				 Perk::kBurstSearing, Perk::kBurstTwin, Perk::kSpellShield, Perk::kSurge, Perk::kUnravel }) {
			CHECK(Learn(record, mage, At(mage, perk), 99) == Block::kNone);
		}
		CHECK(Spent(record, mage) == 11);
		CHECK(CanLearn(record, mage, magnus, 99) == Block::kCapstone);
		// Every rank of everything else is.
		const auto& path = PathOf(mage);
		for (int pass = 0; pass < 3; ++pass) {
			for (std::size_t i = 0; i < kNodes; ++i) {
				if (!path[i].Empty() && !path[i].capstone) {
					Learn(record, mage, i, 99);
				}
			}
		}
		const int others = Mastery(mage) - 11;
		CHECK(Spent(record, mage) == others);
		// One rank short anywhere keeps it shut.
		const auto unravel = At(mage, Perk::kUnravel);
		CHECK(Forget(record, mage, unravel) == Block::kNone);
		CHECK(CanLearn(record, mage, magnus, 99) == Block::kCapstone);
		CHECK(Learn(record, mage, unravel, 99) == Block::kNone);
		// Five points to unlock: four left is not enough.
		CHECK(CanLearn(record, mage, magnus, others + 4) == Block::kNoPoints);
		CHECK(Learn(record, mage, magnus, others + 5) == Block::kNone);
		CHECK(Spent(record, mage) == others + 5);
		// Three for each rank after.
		CHECK(CanLearn(record, mage, magnus, others + 7) == Block::kNoPoints);
		CHECK(Learn(record, mage, magnus, others + 8) == Block::kNone);
		CHECK(Spent(record, mage) == others + 8);
		CHECK(Equip(record, mage, 0, Ability::kMagnus) == Block::kNone);
		// Every rank under it holds it up, not only the first.
		CHECK(CanForget(record, mage, unravel) == Block::kHolds);
		CHECK(CanForget(record, mage, At(mage, Perk::kBurstTwin)) == Block::kHolds);
		// A save where something under it is a rank short: it goes, and its points come back.
		{
			auto shaken = record;
			--shaken.ranks[static_cast<std::size_t>(mage)][unravel];
			CHECK(Repair(shaken, 99));
			CHECK(shaken.ranks[static_cast<std::size_t>(mage)][magnus] == 0);
			CHECK(Spent(shaken, mage) == others - 1);
			CHECK(shaken.slots[static_cast<std::size_t>(mage)][0] != Ability::kMagnus);
		}
		CHECK(CanForget(record, mage, magnus) == Block::kNone);
		CHECK(Forget(record, mage, magnus) == Block::kNone && Forget(record, mage, magnus) == Block::kNone);
		CHECK(Spent(record, mage) == others);
		CHECK(record.slots[static_cast<std::size_t>(mage)][0] != Ability::kMagnus);
		CHECK(CanForget(record, mage, unravel) == Block::kNone);
		// Empty places cannot be learned.
		CHECK(CanLearn(record, mage, 10, 99) == Block::kBadNode);
		// A save from before Magnus: the Atronach's ranks are dropped and the points come back.
		std::vector<std::uint8_t> old{ 1, 0, 0, 0, 0x34, 0x12, 0, 0, 30, 0, 6 };
		for (std::size_t r = 0; r < kRoles; ++r) {
			const bool m = r == static_cast<std::size_t>(mage);
			old.insert(old.end(), { static_cast<std::uint8_t>(m ? Ability::kAtronachRetired : Ability::kNone), 0, static_cast<std::uint8_t>(m ? 1 : 0) });
			if (m) {
				old.insert(old.end(), { static_cast<std::uint8_t>(Perk::kAtronachRetired), 2 });
			}
		}
		auto loaded = Decode(old, kRecordVersion);
		CHECK(loaded.size() == 1);
		if (!loaded.empty()) {
			Repair(loaded[0].second, 20);
			CHECK(Spent(loaded[0].second) == 0);
			CHECK(loaded[0].second.slots[static_cast<std::size_t>(mage)][0] == Ability::kSurge);
		}
	}

	void Unlimited()
	{
		const auto tank = Role::kTank;
		const auto t = static_cast<std::size_t>(tank);
		auto       record = Fresh();
		record.peak = 20;
		Learn(record, tank, At(tank, Perk::kTaunt), 10);
		const auto before = record;
		// Unlimited points: everything bought, Challenge and Stand Fast carried.
		for (std::size_t i = 0; i < kNodes; ++i) {
			for (int k = 0; k < 3; ++k) {
				Learn(record, tank, i, kUnlimited);
			}
		}
		CHECK(Spent(record, tank) == Mastery(tank));
		CHECK(Equip(record, tank, 1, Ability::kChallenge) == Block::kNone);
		CHECK(Equip(record, tank, 0, Ability::kStandFast) == Block::kNone);
		record.peak = 24;
		Restore(record, before);
		CHECK(Spent(record) == 1);
		CHECK(Rank(record, tank, Ability::kTaunt) == 2);
		CHECK(Rank(record, tank, Ability::kChallenge) == 0);
		CHECK(record.peak == 24);
		// Both carried abilities were added in the meantime: back to the two defaults.
		CHECK(record.slots[t][0] == Ability::kTaunt && record.slots[t][1] == Ability::kBodyguard);
		// A default carried throughout stays where it is.
		auto kept = before;
		Learn(kept, tank, At(tank, Perk::kChallenge), kUnlimited);
		for (const auto perk : { Perk::kTauntBrace, Perk::kTauntBrace, Perk::kIronHide, Perk::kIronHide, Perk::kChallenge }) {
			Learn(kept, tank, At(tank, perk), kUnlimited);
		}
		Equip(kept, tank, 0, Ability::kBodyguard);
		Equip(kept, tank, 1, Ability::kChallenge);
		Restore(kept, before);
		CHECK(kept.slots[t][0] == Ability::kBodyguard && kept.slots[t][1] == Ability::kTaunt);
	}
}

int main()
{
	Unlimited();
	Capstone();
	Shape();
	Words();
	Points();
	Learning();
	Slots();
	Repairs();
	Cooldowns();
	Codec();
	Bonus();
	Companions();
	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
