#include "FWM/Cooldowns.h"
#include "FWM/Hotkey.h"
#include "FWM/OrdersCodec.h"
#include "FWM/Rules.h"
#include "FWM/StyleLayer.h"

#include <cmath>
#include <cstdio>
#include <vector>

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
	using namespace FWM::Rules;

	void Taunt()
	{
		const Tuning t;
		CHECK(TauntReach(12, t) == 3);
		CHECK(TauntReach(20, t) == 5);
		CHECK(TauntReach(40, t) == 10);
		CHECK(TauntReach(0, t) == 0);

		// Level 30 tank: certain up to 37, 64% at 40, nothing at 46.
		CHECK(Near(TauntChance(30, 37, false, t), 1.0f));
		CHECK(Near(TauntChance(30, 40, false, t), 0.64f));
		CHECK(Near(TauntChance(30, 45, false, t), 0.04f));
		CHECK(Near(TauntChance(30, 46, false, t), 0.0f));
		CHECK(Near(TauntChance(30, 10, false, t), 1.0f));
		// A boss counts five levels higher.
		CHECK(Near(TauntChance(30, 35, true, t), 0.64f));

		CHECK(Near(TauntRadius(0, 0, t), 800.0f));
		CHECK(Near(TauntRadius(50, 20, t), 950.0f));
		CHECK(Near(TauntRadius(20, 100, t), 1100.0f));
		CHECK(Near(TauntRadius(200, 0, t), 1100.0f));  // skills clamp at 100

		// Ten seconds, fifteen from level 30, thirty from level 60.
		CHECK(Near(HoldSeconds(1, 1, t), 10.0f));
		CHECK(Near(HoldSeconds(29, 29, t), 10.0f));
		CHECK(Near(HoldSeconds(30, 30, t), 15.0f));
		CHECK(Near(HoldSeconds(59, 55, t), 15.0f));
		CHECK(Near(HoldSeconds(60, 60, t), 30.0f));
		CHECK(Near(HoldSeconds(81, 80, t), 30.0f));
		CHECK(Near(HoldSeconds(40, 30, t), 22.5f));  // cowed: ten below

		CHECK(Near(TauntCooldown(20, t), 13.0f));
		CHECK(Near(TauntCooldown(200, t), 8.0f));

		// Bracing: 10 + 1.5 a level, up to 120.
		CHECK(Near(TauntArmor(1, t), 11.5f));
		CHECK(Near(TauntArmor(30, t), 55.0f));
		CHECK(Near(TauntArmor(100, t), 120.0f));
		CHECK(TauntArmor(40, t) > TauntArmor(20, t));
	}

	void Magicka()
	{
		// Out once the cheapest spell is out of reach.
		CHECK(!OutOfMagicka(40.0f, 200.0f, 30.0f, false));
		CHECK(OutOfMagicka(29.0f, 200.0f, 30.0f, false));
		// Back once there is enough for two casts or a third of the pool, whichever is more.
		CHECK(OutOfMagicka(60.0f, 200.0f, 30.0f, true));
		CHECK(!OutOfMagicka(67.0f, 200.0f, 30.0f, true));
		CHECK(OutOfMagicka(110.0f, 200.0f, 60.0f, true));
		CHECK(!OutOfMagicka(120.0f, 200.0f, 60.0f, true));
		// A spell dearer than half the pool: a full pool brings them back.
		CHECK(OutOfMagicka(140.0f, 150.0f, 100.0f, true));
		CHECK(!OutOfMagicka(150.0f, 150.0f, 100.0f, true));
		// Nothing to cast, or no magicka at all to speak of: never out.
		CHECK(!OutOfMagicka(0.0f, 200.0f, 0.0f, true));
		CHECK(!OutOfMagicka(0.0f, 0.0f, 30.0f, false));
	}

	void Spells()
	{
		CHECK(TierFromSkill(0) == SpellTier::kNovice && TierFromSkill(25) == SpellTier::kApprentice && TierFromSkill(50) == SpellTier::kAdept);
		CHECK(TierFromSkill(75) == SpellTier::kExpert && TierFromSkill(100) == SpellTier::kMaster && TierFromSkill(250) == SpellTier::kMaster);
		CHECK(TierFromSkill(-5) == SpellTier::kNovice);

		// Mages: anything but a heal, whatever their level.
		CHECK(SpellAllowed(Role::kMage, false, true, SpellTier::kMaster, 1, false));
		CHECK(SpellAllowed(Role::kMage, false, false, SpellTier::kMaster, 1, false));  // a ward or a cloak as much as an attack
		CHECK(!SpellAllowed(Role::kMage, true, false, SpellTier::kNovice, 50, false));

		// Healers: every heal and every other kind of spell; attack spells climb with level and stop at Adept.
		CHECK(SpellAllowed(Role::kHealer, true, false, SpellTier::kMaster, 1, false));
		CHECK(SpellAllowed(Role::kHealer, false, false, SpellTier::kExpert, 1, false));  // a ward, armor, a summons: no cap
		CHECK(SpellAllowed(Role::kHealer, false, true, SpellTier::kNovice, 1, false));
		CHECK(!SpellAllowed(Role::kHealer, false, true, SpellTier::kApprentice, 9, false));
		CHECK(SpellAllowed(Role::kHealer, false, true, SpellTier::kApprentice, 10, false));
		CHECK(!SpellAllowed(Role::kHealer, false, true, SpellTier::kAdept, 24, false));
		CHECK(SpellAllowed(Role::kHealer, false, true, SpellTier::kAdept, 25, false));
		CHECK(!SpellAllowed(Role::kHealer, false, true, SpellTier::kExpert, 81, false));
		CHECK(!SpellAllowed(Role::kHealer, false, true, SpellTier::kMaster, 81, false));
		CHECK(SpellAllowed(Role::kHealer, false, true, SpellTier::kMaster, 5, true));  // the "any spell" setting

		// Nobody else chooses spells.
		CHECK(!SpellAllowed(Role::kFighter, false, true, SpellTier::kNovice, 50, true));
		CHECK(!SpellAllowed(Role::kTank, true, false, SpellTier::kNovice, 50, true));
	}

	void Heal()
	{
		const Tuning t;
		// The team heal: 40 and 3.5 a level, never past 400.
		CHECK(Near(TeamHealAmount(1, t), 43.5f));
		CHECK(Near(TeamHealAmount(30, t), 145.0f));
		CHECK(Near(TeamHealAmount(60, t), 250.0f));
		CHECK(Near(TeamHealAmount(200, t), 400.0f));
		CHECK(Near(TeamHealAmount(0, t), 43.5f));  // level 0 counts as 1

		// Rejuvenate: 30 s, a minute from level 30, three from 60; ready again after twice that.
		CHECK(Near(RejuvenateSeconds(1, t), 30.0f) && Near(RejuvenateCooldown(1, t), 60.0f));
		CHECK(Near(RejuvenateSeconds(29, t), 30.0f));
		CHECK(Near(RejuvenateSeconds(30, t), 60.0f) && Near(RejuvenateCooldown(30, t), 120.0f));
		CHECK(Near(RejuvenateSeconds(59, t), 60.0f));
		CHECK(Near(RejuvenateSeconds(60, t), 180.0f) && Near(RejuvenateCooldown(60, t), 360.0f));
		CHECK(Near(RejuvenateSeconds(81, t), 180.0f));
	}

	EnemyView Enemy(float a_distance)
	{
		EnemyView e;
		e.distance = a_distance;
		e.distanceToPlayer = a_distance;
		return e;
	}

	void Targeting()
	{
		const Tuning t;

		// Their choice with no positional order: leave it to the combat AI.
		std::vector<EnemyView> e{ Enemy(300), Enemy(500) };
		CHECK(PickTarget(e, Focus::kEngine, Position::kFree, false, t).index == -1);

		// Casters: the mage at 900 beats the warrior at 300.
		e[1].distance = 900;
		e[1].caster = true;
		CHECK(PickTarget(e, Focus::kCasters, Position::kFree, false, t).index == 1);

		// No caster in the fight: the combat AI decides.
		e[1].caster = false;
		CHECK(PickTarget(e, Focus::kCasters, Position::kFree, false, t).index == -1);

		// Far away is no reason to ignore an enemy that fits the focus.
		e[1].caster = true;
		e[1].distance = 4000;
		CHECK(PickTarget(e, Focus::kCasters, Position::kFree, false, t).index == 1);
		e[1].current = true;
		CHECK(PickTarget(e, Focus::kCasters, Position::kFree, false, t).index == 1);
		// Your target across the field is still your target.
		std::vector<EnemyView> farTarget{ Enemy(200), Enemy(5200) };
		farTarget[1].playerTarget = true;
		CHECK(PickTarget(farTarget, Focus::kPlayerTarget, Position::kFrontLine, false, t).index == 1);
		CHECK(PickTarget(farTarget, Focus::kPlayerTarget, Position::kFree, false, t).index == 1);

		// Casters and archers: anyone fighting from behind their line.
		std::vector<EnemyView> b{ Enemy(300), Enemy(800), Enemy(900) };
		CHECK(PickTarget(b, Focus::kBackLine, Position::kFree, false, t).index == -1);
		b[2].healer = true;
		CHECK(PickTarget(b, Focus::kBackLine, Position::kFree, false, t).index == 2);
		b[1].archer = true;
		CHECK(PickTarget(b, Focus::kBackLine, Position::kFree, false, t).index == 1);

		// With me: whatever comes near the player, whoever is on them from close by, whoever
		// is on the follower within reach. Nothing in reach: whoever is nearest the player,
		// which brings a straggler back through the fight rather than out of it.
		std::vector<EnemyView> w{ Enemy(900), Enemy(600) };
		w[0].distanceToPlayer = 1200;
		w[1].distanceToPlayer = 800;
		CHECK(PickTarget(w, Focus::kAttackingPlayer, Position::kWithMe, false, t).index == 1);
		w[1].distanceToPlayer = 500;
		CHECK(PickTarget(w, Focus::kAttackingPlayer, Position::kWithMe, false, t).index == 1);
		w[1].distanceToPlayer = 800;
		w[1].attackingPlayer = true;  // on the player, from just out of arm's reach
		CHECK(PickTarget(w, Focus::kAttackingPlayer, Position::kWithMe, false, t).index == 1);
		w[1].attackingPlayer = false;
		w[0].attackingSelf = true;
		w[0].distance = 450;
		CHECK(PickTarget(w, Focus::kAttackingPlayer, Position::kWithMe, false, t).index == 0);
		w[0].distance = 900;  // on them, but far off: the one nearest the player instead
		CHECK(PickTarget(w, Focus::kAttackingPlayer, Position::kWithMe, false, t).index == 1);

		// With me, shooting from the player's side: anyone, the nearest first, however far.
		std::vector<EnemyView> wr{ Enemy(1800), Enemy(1200), Enemy(300) };
		wr[0].distanceToPlayer = 1700;
		wr[1].distanceToPlayer = 1100;
		wr[2].distanceToPlayer = 350;
		CHECK(PickTarget(wr, Focus::kAttackingPlayer, Position::kWithMe, true, t).index == 2);
		wr[2].distance = wr[2].distanceToPlayer = 2600;  // nobody near: still the nearest, not nothing
		CHECK(PickTarget(wr, Focus::kAttackingPlayer, Position::kWithMe, true, t).index == 1);
		wr[0].attackingPlayer = true;  // the leaning still counts: whoever is on you first
		CHECK(PickTarget(wr, Focus::kAttackingPlayer, Position::kWithMe, true, t).index == 0);
		// A blade at your side still only takes what reaches you.
		wr[0].attackingPlayer = false;
		wr[1].distanceToPlayer = 450;
		CHECK(PickTarget(wr, Focus::kAttackingPlayer, Position::kWithMe, false, t).index == 1);

		// Fighting back: an enemy on them from close by comes first, whatever the leaning.
		std::vector<EnemyView> fb{ Enemy(1000), Enemy(120) };
		fb[0].health = 0.2f;
		fb[0].current = true;
		CHECK(PickTarget(fb, Focus::kWeakest, Position::kFree, false, t).index == 0);  // nobody on them
		fb[1].attackingSelf = true;
		const auto defend = PickTarget(fb, Focus::kWeakest, Position::kFree, false, t);
		CHECK(defend.index == 1 && defend.defending);
		CHECK(PickTarget(fb, Focus::kStrongest, Position::kFree, false, t).index == 1);
		fb[1].distance = 1500;  // on them, but from across the field: the leaning stands
		CHECK(PickTarget(fb, Focus::kWeakest, Position::kFree, false, t).index == 0);
		fb[1].distance = 120;
		fb[0].playerTarget = true;  // an order still stands
		CHECK(PickTarget(fb, Focus::kPlayerTarget, Position::kFree, false, t).index == 0);
		// A guard far from the player fights back too, rather than only watching the player.
		std::vector<EnemyView> gd{ Enemy(150), Enemy(900) };
		gd[0].distanceToPlayer = 1500;
		gd[0].attackingSelf = true;
		gd[1].distanceToPlayer = 300;
		CHECK(PickTarget(gd, Focus::kEngine, Position::kGuard, false, t).index == 0);

		// Your target is an order, not a leaning: a much closer enemy does not win.
		std::vector<EnemyView> pt{ Enemy(150), Enemy(2400) };
		pt[0].current = true;
		pt[1].playerTarget = true;
		CHECK(PickTarget(pt, Focus::kPlayerTarget, Position::kFree, false, t).index == 1);

		// A marked enemy is chased from any position, even one that would never reach them:
		// a guard, a melee companion holding back, a companion at your side.
		std::vector<EnemyView> mk{ Enemy(150), Enemy(2600) };
		mk[0].attackingSelf = true;
		mk[0].attackingPlayer = true;
		mk[1].marked = true;
		for (const auto position : { Position::kGuard, Position::kHoldBack, Position::kWithMe, Position::kFree, Position::kFrontLine }) {
			CHECK(PickTarget(mk, Focus::kMarked, position, false, t).index == 1);
			CHECK(PickTarget(mk, Focus::kMarked, position, true, t).index == 1);
		}
		// Nothing marked any more: the position decides again.
		mk[1].marked = false;
		CHECK(PickTarget(mk, Focus::kMarked, Position::kGuard, false, t).index == 0);

		// Out of reach on foot (an archer on a ledge): melee goes for someone they can reach,
		// even against an order; at range the order stands.
		std::vector<EnemyView> ledge{ Enemy(1100), Enemy(700) };
		ledge[0].playerTarget = true;
		ledge[0].archer = true;
		ledge[0].outOfReach = true;
		ledge[0].current = true;  // they were sent at it, and stand below looking up
		CHECK(PickTarget(ledge, Focus::kPlayerTarget, Position::kFree, false, t).index == 1);
		CHECK(PickTarget(ledge, Focus::kPlayerTarget, Position::kFree, true, t).index == 0);
		ledge[0].marked = true;
		CHECK(PickTarget(ledge, Focus::kMarked, Position::kFree, false, t).index == 1);
		CHECK(PickTarget(ledge, Focus::kMarked, Position::kFree, true, t).index == 0);
		ledge[0].marked = false;
		ledge[0].current = false;
		// The combat AI's own pick is the one on the ledge: moved off it.
		ledge[0].engineChoice = true;
		CHECK(PickTarget(ledge, Focus::kEngine, Position::kFree, false, t).index == 1);
		ledge[0].engineChoice = false;
		ledge[1].engineChoice = true;
		CHECK(PickTarget(ledge, Focus::kEngine, Position::kFree, false, t).index == -1);  // their pick stands
		// Nobody can be reached: nothing to choose.
		ledge[1].outOfReach = true;
		ledge[1].engineChoice = false;
		ledge[0].engineChoice = true;
		CHECK(PickTarget(ledge, Focus::kEngine, Position::kFree, false, t).index == -1);

		// Strongest and weakest.
		std::vector<EnemyView> s{ Enemy(400), Enemy(400), Enemy(400) };
		s[0].threat = 50;
		s[1].threat = 120;
		s[2].threat = 80;
		s[2].health = 0.1f;
		CHECK(PickTarget(s, Focus::kStrongest, Position::kFree, false, t).index == 1);
		CHECK(PickTarget(s, Focus::kWeakest, Position::kFree, false, t).index == 2);

		// Guard: only enemies near the player or attacking them.
		std::vector<EnemyView> g{ Enemy(200), Enemy(600) };
		g[0].distanceToPlayer = 1500;
		g[1].distanceToPlayer = 300;
		CHECK(PickTarget(g, Focus::kEngine, Position::kGuard, false, t).index == 1);
		g[1].distanceToPlayer = 1200;
		g[0].attackingPlayer = true;
		CHECK(PickTarget(g, Focus::kEngine, Position::kGuard, false, t).index == 0);
		// Nobody near the player: go for whoever is closest to them.
		g[0].attackingPlayer = false;
		CHECK(PickTarget(g, Focus::kEngine, Position::kGuard, false, t).index == 1);

		// Hold back, melee: only what comes close. Ranged: anyone.
		std::vector<EnemyView> h{ Enemy(1200), Enemy(1500) };
		h[1].caster = true;
		CHECK(PickTarget(h, Focus::kCasters, Position::kHoldBack, false, t).index == -1);
		CHECK(PickTarget(h, Focus::kCasters, Position::kHoldBack, true, t).index == 1);
		h[0].attackingSelf = true;
		CHECK(PickTarget(h, Focus::kCasters, Position::kHoldBack, false, t).index == 0);

		// Spread: two equal enemies, one already has two allies on it.
		std::vector<EnemyView> sp{ Enemy(400), Enemy(400) };
		sp[0].attackingParty = sp[1].attackingParty = true;
		sp[0].alliesOn = 2;
		CHECK(PickTarget(sp, Focus::kAttackingParty, Position::kFree, false, t).index == 1);

		// A marked enemy wins over everything else in range.
		std::vector<EnemyView> m{ Enemy(200), Enemy(1500) };
		m[0].attackingSelf = true;
		m[0].current = true;
		m[1].marked = true;
		CHECK(PickTarget(m, Focus::kMarked, Position::kFree, false, t).index == 1);

		// Front line prefers the enemy ahead of the player.
		std::vector<EnemyView> f{ Enemy(500), Enemy(500) };
		f[0].forward = -400;
		f[1].forward = 900;
		CHECK(PickTarget(f, Focus::kEngine, Position::kFrontLine, false, t).index == 1);
	}

	void DangerTiers()
	{
		const Tuning t;
		CHECK(DangerFor(30, 15, false, t) == Rules::Danger::kTrivial);
		CHECK(DangerFor(30, 25, false, t) == Rules::Danger::kEasy);
		CHECK(DangerFor(30, 33, false, t) == Rules::Danger::kEven);
		CHECK(DangerFor(30, 37, false, t) == Rules::Danger::kHard);
		CHECK(DangerFor(30, 38, false, t) == Rules::Danger::kDeadly);
		CHECK(DangerFor(30, 30, true, t) == Rules::Danger::kHard);
	}

	void Styles()
	{
		StyleData base;  // vanilla-ish defaults
		base.offensive = 0.6f;
		base.defensive = 0.3f;
		base.flags = StyleData::kFlanking;

		const auto none = Layer(base, Role::kTank, Position::kFree, 0.0f);
		CHECK(Near(none.offensive, 0.6f) && Near(none.defensive, 0.3f) && none.flags == base.flags);

		const auto tank = Layer(base, Role::kTank, Position::kFree, 1.0f);
		CHECK(Near(tank.offensive, 0.45f));
		CHECK(Near(tank.defensive, 0.85f));
		CHECK(Near(tank.bash, 5.5f));
		CHECK(Near(tank.meleeScore, 1.5f));
		CHECK((tank.flags & StyleData::kDueling) != 0);
		CHECK((tank.flags & StyleData::kFlanking) == 0);

		const auto half = Layer(base, Role::kTank, Position::kFree, 0.5f);
		CHECK(Near(half.defensive, 0.575f));

		// Sent after someone (or held by a taunt): no circling, falling back, dueling or
		// strafing, and the shield comes down; what they fight with is untouched.
		const auto committed = Committed(tank);
		CHECK(Near(committed.circle, 0.0f) && Near(committed.fallback, 0.0f) && Near(committed.strafe, 0.0f));
		CHECK(Near(committed.offensive, 1.0f) && committed.defensive <= 0.15f);
		CHECK((committed.flags & (StyleData::kDueling | StyleData::kFlanking)) == 0);
		CHECK(Near(committed.meleeScore, tank.meleeScore) && Near(committed.bash, tank.bash));

		// A follower who never uses magic does not start because of a role.
		base.magicScore = 0.0f;
		CHECK(Near(Layer(base, Role::kMage, Position::kFree, 1.0f).magicScore, 0.0f));
		base.magicScore = 1.0f;
		CHECK(Near(Layer(base, Role::kMage, Position::kFree, 1.0f).magicScore, 2.5f));
		base.rangedScore = 5.0f;
		CHECK(Near(Layer(base, Role::kArcher, Position::kFree, 1.0f).rangedScore, 10.0f));  // capped

		const auto back = Layer(base, Role::kFighter, Position::kHoldBack, 1.0f);
		const auto free = Layer(base, Role::kFighter, Position::kFree, 1.0f);
		CHECK(back.fallback > free.fallback);
		CHECK(back.offensive < free.offensive);

		// An archer keeps the bow wherever they stand: point blank, the engine's x10 for a
		// dagger in reach against x0.1 for a bow must still lose. Only the front line
		// lets the dagger out.
		base.meleeScore = 0.83f;
		const auto archerBack = Layer(base, Role::kArcher, Position::kHoldBack, 0.75f);
		const auto archerGuard = Layer(base, Role::kArcher, Position::kGuard, 0.75f);
		const auto archerFree = Layer(base, Role::kArcher, Position::kFree, 0.75f);
		const auto archerFront = Layer(base, Role::kArcher, Position::kFrontLine, 0.75f);
		CHECK(archerBack.meleeScore * 10.0f < archerBack.rangedScore * 2.0f * 0.1f);
		CHECK(archerFree.meleeScore * 10.0f < archerFree.rangedScore * 2.0f * 0.1f);
		CHECK(archerGuard.meleeScore < 0.01f);
		CHECK(archerFront.meleeScore > 0.2f);
		CHECK(archerBack.fallback >= 0.67f);
		CHECK(archerFree.fallback < archerBack.fallback);  // free to move as they like
		// With me: planted at your side, never backing off or circling away to cover; still no dagger.
		const auto archerWithMe = Layer(base, Role::kArcher, Position::kWithMe, 0.75f);
		CHECK(archerWithMe.fallback < archerFree.fallback && archerWithMe.avoidThreat < archerBack.avoidThreat);
		CHECK(archerWithMe.meleeScore < 0.01f && archerWithMe.circle <= 0.2f);
		CHECK(Layer(base, Role::kFighter, Position::kWithMe, 1.0f).fallback < free.fallback);
		CHECK(Layer(base, Role::kMage, Position::kFree, 0.75f).meleeScore < 0.01f);
		CHECK(Layer(base, Role::kFighter, Position::kHoldBack, 0.75f).meleeScore > 0.5f);
	}

	void CodecRoundTrip()
	{
		std::vector<FWM::Codec::Entry> in{
			{ 0x00012345, ForRole(Role::kTank) },
			{ 0xFE000801, Orders{ Role::kHealer, Focus::kAttackingPlayer, Position::kGuard, false, 40 } },
		};
		const auto bytes = FWM::Codec::Encode(in);
		const auto out = FWM::Codec::Decode(bytes, FWM::Codec::kVersion);
		CHECK(out.size() == 2);
		CHECK(out.size() == 2 && out[0] == in[0] && out[1] == in[1]);

		// Truncated: keep the whole entries only.
		const std::vector<std::uint8_t> cut(bytes.begin(), bytes.end() - 3);
		CHECK(FWM::Codec::Decode(cut, FWM::Codec::kVersion).size() == 1);

		// Out-of-range values fall back instead of dropping the follower.
		auto bad = bytes;
		bad[4 + 4] = 99;   // role
		bad[4 + 8] = 200;  // threshold
		const auto fixed = FWM::Codec::Decode(bad, FWM::Codec::kVersion);
		CHECK(fixed.size() == 2 && fixed[0].second.role == Role::kFighter && fixed[0].second.healThreshold == 95);

		CHECK(FWM::Codec::Decode(bytes, 0).empty());

		// The special and Rejuvenate switches ride in the flags byte, stored as "off": a save from
		// before them (flags 0 or 1) reads with both on.
		Orders quiet = ForRole(Role::kArcher);
		quiet.autoSpecial = false;
		quiet.autoRejuvenate = false;
		const std::vector<FWM::Codec::Entry> switched{ { 0x00000D62, quiet } };
		const auto back = FWM::Codec::Decode(FWM::Codec::Encode(switched), FWM::Codec::kVersion);
		CHECK(back.size() == 1 && back[0].second == quiet);
		auto old = FWM::Codec::Encode(switched);
		old[4 + 7] = 1;  // what the first version wrote: only "on their own"
		const auto upgraded = FWM::Codec::Decode(old, FWM::Codec::kVersion);
		CHECK(upgraded.size() == 1 && upgraded[0].second.autoSpecial && upgraded[0].second.autoAbility && upgraded[0].second.autoRejuvenate);
		// The old Potions switch (bit 2) in a save is passed over, and never written again.
		auto potionsOff = FWM::Codec::Encode(switched);
		potionsOff[4 + 7] |= 1 << 2;
		const auto ignored = FWM::Codec::Decode(potionsOff, FWM::Codec::kVersion);
		CHECK(ignored.size() == 1 && ignored[0].second == quiet);
		CHECK((FWM::Codec::Encode(ignored)[4 + 7] & (1 << 2)) == 0);

		// Chosen spells: (follower, left, right) round-trip; a short record keeps what is whole.
		const std::vector<FWM::Codec::HandsEntry> hands{ { 0x00000D62, { 0x0001C789, 0 } }, { 0xFE001234, { 0x0002B96B, 0x0002B96C } } };
		auto handBytes = FWM::Codec::EncodeHands(hands);
		CHECK(FWM::Codec::DecodeHands(handBytes, FWM::Codec::kHandsVersion) == hands);
		handBytes.resize(handBytes.size() - 3);
		const auto shortHands = FWM::Codec::DecodeHands(handBytes, FWM::Codec::kHandsVersion);
		CHECK(shortHands.size() == 1 && shortHands[0] == hands[0]);
		CHECK(FWM::Codec::DecodeHands(handBytes, 0).empty());

		// Every role has its own special.
		CHECK(SpecialOf(Role::kTank) == Special::kBodyguard && SpecialOf(Role::kArcher) == Special::kVolley);
		CHECK(SpecialOf(Role::kBruiser) == Special::kWarCry && SpecialOf(Role::kFighter) == Special::kDisarm);
		CHECK(SpecialOf(Role::kMage) == Special::kWard && SpecialOf(Role::kHealer) == Special::kWard);
	}

	void RoleGating()
	{
		Arsenal sword{ true, false, false, false };
		Arsenal bare{};
		Arsenal mage{ false, false, true, false };
		Arsenal priest{ false, false, false, true };
		Arsenal ranger{ true, true, false, false };

		// No weapon: no melee roles; an empty-handed companion can still brawl.
		CHECK(LackFor(Role::kTank, mage) == Lack::kWeapon);
		CHECK(LackFor(Role::kBruiser, mage) == Lack::kWeapon);
		CHECK(LackFor(Role::kFighter, mage) == Lack::kWeapon);
		CHECK(LackFor(Role::kFighter, bare) == Lack::kNothing);
		CHECK(LackFor(Role::kTank, bare) == Lack::kWeapon);
		CHECK(LackFor(Role::kTank, sword) == Lack::kNothing);

		// A healer's heal is their own, not a spell: anyone can be one.
		CHECK(LackFor(Role::kHealer, sword) == Lack::kNothing);
		CHECK(LackFor(Role::kHealer, bare) == Lack::kNothing);
		CHECK(LackFor(Role::kHealer, priest) == Lack::kNothing);
		CHECK(LackFor(Role::kArcher, sword) == Lack::kBow);
		CHECK(LackFor(Role::kArcher, ranger) == Lack::kNothing);
		CHECK(LackFor(Role::kMage, sword) == Lack::kSpells);

		// Falling back to the closest playable role.
		CHECK(Playable(Role::kTank, mage) == Role::kMage);
		CHECK(Playable(Role::kTank, priest) == Role::kHealer);
		CHECK(Playable(Role::kHealer, sword) == Role::kHealer);
		CHECK(Playable(Role::kArcher, sword) == Role::kFighter);
		CHECK(Playable(Role::kMage, ranger) == Role::kFighter);
		CHECK(Playable(Role::kTank, bare) == Role::kFighter);
		CHECK(Playable(Role::kTank, sword) == Role::kTank);
	}

	void RoleDefaults()
	{
		CHECK(ForRole(Role::kTank).focus == Focus::kAttackingParty);
		CHECK(ForRole(Role::kTank).position == Position::kGuard);
		CHECK(ForRole(Role::kArcher).focus == Focus::kBackLine);
		CHECK(Resolve(Position::kFormation, Formation::kLead) == Position::kFrontLine);
		CHECK(Resolve(Position::kFormation, Formation::kRear) == Position::kHoldBack);
		CHECK(Resolve(Position::kFormation, Formation::kRelax) == Position::kFree);
		CHECK(Resolve(Position::kGuard, Formation::kLead) == Position::kGuard);

		// Changing role moves only what still matched the old role's suggestion.
		const auto fighter = ForRole(Role::kFighter);
		CHECK(Retask(fighter, Role::kArcher) == ForRole(Role::kArcher));
		auto archer = ForRole(Role::kArcher);
		archer.position = Position::kGuard;
		const auto mage = Retask(archer, Role::kMage);
		CHECK(mage.role == Role::kMage && mage.position == Position::kGuard && mage.focus == Focus::kWeakest);
		CHECK(!AsSuggested(mage) && AsSuggested(ForRole(Role::kTank)));
		auto keeps = ForRole(Role::kTank);
		keeps.autoAbility = false;
		keeps.healThreshold = 40;
		const auto healer = Retask(keeps, Role::kHealer);
		CHECK(!healer.autoAbility && healer.healThreshold == 40 && healer.position == Position::kHoldBack);

		// Party orders replace focus and position and leave role and abilities alone.
		auto own = ForRole(Role::kArcher);
		own.healThreshold = 35;
		own.position = Position::kGuard;
		own.focus = Focus::kWeakest;
		CHECK(Shouted(own, Shout::kNone).position == Position::kHoldBack && Shouted(own, Shout::kNone).focus == Focus::kBackLine);
		const auto attack = Shouted(own, Shout::kAttack);
		CHECK(attack.focus == Focus::kPlayerTarget && attack.position == Position::kFree && attack.role == own.role);
		const auto toMe = Shouted(own, Shout::kToMe);
		CHECK(toMe.position == Position::kWithMe && toMe.focus == Focus::kAttackingPlayer && toMe.healThreshold == 35);
		CHECK(Shouted(ForRole(Role::kHealer), Shout::kToMe).focus == Focus::kAttackingParty);
		const auto back = Shouted(own, Shout::kHoldBack);
		CHECK(back.position == Position::kHoldBack && back.focus == Focus::kAttackingParty);
	}

	void Hotkeys()
	{
		// Either side of a modifier is the same modifier; other keys are none.
		CHECK(ModifierOf(0x36) == kShift && ModifierOf(0x9D) == kCtrl && ModifierOf(0xB8) == kAlt && ModifierOf(0x48) == 0);

		// A modifier is only kept with a real key, never on an unbound one or on itself.
		CHECK((Normalized({ 0x48, 0x9D }) == Hotkey{ 0x48, kCtrl }));
		CHECK((Normalized({ 0, kCtrl }) == Hotkey{}));
		CHECK((Normalized({ 0x38, kShift }) == Hotkey{ 0x38, 0 }));
		CHECK((Normalized({ 0x48, 0x48 }) == Hotkey{ 0x48, 0 }));

		// Ctrl + 8 beats a plain 8 while Ctrl is held; the plain one still works with it held.
		const Hotkey plain{ 0x48 }, combo{ 0x48, kCtrl };
		CHECK(Fit(combo, 0x48, true) > Fit(plain, 0x48, true) && Fit(plain, 0x48, true) > 0);
		CHECK(Fit(combo, 0x48, false) == 0 && Fit(plain, 0x48, false) == 1);
		CHECK(Fit(plain, 0x49, false) == 0 && Fit(Hotkey{}, 0, false) == 0);
	}

	void Sight()
	{
		// Auto battle: everyone but those holding back starts a fight, and nobody while you sneak.
		CHECK(Rules::StartsFight(Position::kFree, false) && Rules::StartsFight(Position::kGuard, false) && Rules::StartsFight(Position::kWithMe, false));
		CHECK(!Rules::StartsFight(Position::kHoldBack, false));
		CHECK(!Rules::StartsFight(Position::kFrontLine, true));

		// Weapons drawn on sight go away once the view is clear for long enough, but follow yours when out.
		CHECK(!Rules::SheathesAfterSight(2.0f, 5.0f, false));
		CHECK(Rules::SheathesAfterSight(5.0f, 5.0f, false));
		CHECK(!Rules::SheathesAfterSight(30.0f, 5.0f, true));
	}

	void PathBonus()
	{
		const Rules::Tuning t;
		// A percent a point by default, for every role.
		CHECK(Rules::PathBonus(Role::kFighter, 17, t) == 17.0f);
		CHECK(Rules::PathBonus(Role::kTank, 0, t) == 0.0f);
		CHECK(Rules::PathBonus(Role::kHealer, -3, t) == 0.0f);
		// The archer's chance and the mage's cut stop short of free.
		CHECK(Rules::PathBonus(Role::kArcher, 200, t) == Rules::kMostArrowsKept);
		CHECK(Rules::PathBonus(Role::kMage, 200, t) == Rules::kMostCostCut);
		CHECK(Rules::PathBonus(Role::kTank, 200, t) == Rules::kMostDamageCut);
		CHECK(Rules::PathBonus(Role::kBruiser, 200, t) == 200.0f);
		// Each role has its own rate.
		Rules::Tuning half;
		half.bonusDamage = 0.5f;
		CHECK(Rules::PathBonus(Role::kBruiser, 9, half) == 4.5f);
		CHECK(Rules::PathBonus(Role::kFighter, 9, half) == 9.0f);
		// As it reads.
		CHECK(Rules::PathBonusText(Role::kFighter, 17.0f) == "+17% attack speed");
		CHECK(Rules::PathBonusText(Role::kArcher, 12.0f) == "12% arrows kept");
		CHECK(Rules::PathBonusText(Role::kMage, 30.0f) == "-30% magicka cost");
		CHECK(Rules::PathBonusText(Role::kMage, 0.0f) == "0% magicka cost");
		CHECK(Rules::PathBonusText(Role::kTank, 14.0f) == "-14% damage taken");
		CHECK(Rules::PathBonusNumber(Role::kBruiser, 4.5f) == "+4.5%");
		CHECK(Rules::Percent(16.96f) == "17%");

		// Room left for points put straight into the bonus: up to the cap, none past it.
		CHECK(Rules::PathBonusRoom(Role::kMage, 30, t) == 45);
		CHECK(Rules::PathBonusRoom(Role::kMage, 75, t) == 0);
		CHECK(Rules::PathBonusRoom(Role::kMage, 200, t) == 0);
		CHECK(Rules::PathBonusRoom(Role::kArcher, 0, t) == 90);
		CHECK(Rules::PathBonusRoom(Role::kTank, 74, t) == 1);
		CHECK(Rules::PathBonusRoom(Role::kFighter, 30, t) == 970);
		// The point that reaches the cap still counts, even part way: 0.7 a point caps the mage at 108.
		Rules::Tuning odd;
		odd.bonusMagickaCost = 0.7f;
		CHECK(Rules::PathBonusRoom(Role::kMage, 0, odd) == 108);
		CHECK(Rules::PathBonus(Role::kMage, 107, odd) < Rules::kMostCostCut && Rules::PathBonus(Role::kMage, 108, odd) == Rules::kMostCostCut);
		odd.bonusHealing = 0.0f;
		CHECK(Rules::PathBonusRoom(Role::kHealer, 0, odd) == 0);
		CHECK(Rules::PathBonusCap(Role::kBruiser) == Rules::kMostOtherBonus);
	}

	void CooldownClock()
	{
		using namespace FWM::Cooldowns;
		CHECK(Near(Down(5.0f, 2.0f), 3.0f));
		CHECK(Down(1.0f, 2.0f) == 0.0f);
		CHECK(Near(Down(1.0f, -3.0f), 1.0f));

		// Hours between two readings: whole days from the days (which carry the day's fraction, as
		// GameDaysPassed does), the rest from the hour.
		CHECK(std::abs(Clock::HoursBetween(100.0f + 23.5f / 24.0f, 23.5f, 101.0f + 0.5f / 24.0f, 0.5f) - 1.0) < 1e-4);
		CHECK(std::abs(Clock::HoursBetween(400.25f, 6.0f, 400.25f, 6.0f)) < 1e-6);
		CHECK(std::abs(Clock::HoursBetween(10.0f + 8.0f / 24.0f, 8.0f, 12.0f + 20.0f / 24.0f, 20.0f) - 60.0) < 1e-4);

		// Frames as the calendar runs with them, late in a game where the days alone cannot show a
		// minute: only the frames count.
		Clock clock;
		float days = 512.0f, hour = 10.0f, total = 0.0f;
		CHECK(Near(clock.Advance(0.05f, days, hour, 20.0f), 0.05f));
		for (int i = 0; i < 1200; ++i) {
			hour += 0.05f * 20.0f / 3600.0f;
			days += 0.05f * 20.0f / 86400.0f;  // a float this size loses every one of these
			total += clock.Advance(0.05f, days, hour, 20.0f);
		}
		CHECK(Near(total, 60.0f, 0.01f));

		// An hour's wait in one frame: 3600 game seconds at a timescale of 20 is 180 world seconds.
		hour += 1.0f;
		days += 1.0f / 24.0f;
		const float waited = clock.Advance(0.016f, days, hour, 20.0f);
		CHECK(waited > 175.0f && waited < 182.0f);
		// Asleep past midnight: 14 hours, the hour wrapping round.
		hour = std::fmod(hour + 14.0f, 24.0f);
		days += 14.0f / 24.0f;
		CHECK(hour < 2.0f);
		CHECK(Near(clock.Advance(0.0f, days, hour, 20.0f), 2520.0f, 1.0f));

		// A load into an earlier day: counting starts again, and only the frame counts.
		CHECK(Near(clock.Advance(0.02f, 40.0f, 9.0f, 20.0f), 0.02f));
		CHECK(Near(clock.Advance(0.02f, 40.0f, 9.0f + 0.02f * 20.0f / 3600.0f, 20.0f), 0.02f));
		// Nothing readable: frames only, and never backwards.
		Clock blind;
		CHECK(Near(blind.Advance(0.1f, 1.0f, 8.0f, 0.0f), 0.1f));
		CHECK(blind.Advance(-1.0f, 1.0f, 8.0f, 20.0f) == 0.0f);

		// The save record: what is running comes back; what is over or damaged does not.
		const std::vector<Wait> waits{ { 0x14, kTaunt, 12.5f, 30.0f }, { 0xFF000801, 18, 3.0f, 45.0f }, { 0x14, kSpecial, 60.0f, 40.0f } };
		const auto back = Decode(Encode(waits), kVersion);
		CHECK(back.size() == 3);
		CHECK(back.size() == 3 && back[0] == waits[0] && back[1] == waits[1]);
		CHECK(back.size() == 3 && back[2].length == 60.0f);  // a wait is never longer than its whole
		const std::vector<Wait> bad{ { 0, kHeal, 5.0f, 5.0f }, { 0x14, kHeal, 0.0f, 5.0f }, { 0x14, kHeal, 99999.0f, 5.0f },
			{ 0x14, kHeal, std::nanf(""), 5.0f } };
		CHECK(Decode(Encode(bad), kVersion).empty());
		auto bytes = Encode(waits);
		bytes.resize(bytes.size() - 5);
		CHECK(Decode(bytes, kVersion).size() == 2);  // a short record keeps what is whole
		CHECK(Decode(Encode(waits), 0).empty());
	}
}

int main()
{
	Taunt();
	Magicka();
	Spells();
	Heal();
	Targeting();
	DangerTiers();
	Styles();
	CodecRoundTrip();
	RoleDefaults();
	RoleGating();
	Hotkeys();
	Sight();
	PathBonus();
	CooldownClock();
	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
