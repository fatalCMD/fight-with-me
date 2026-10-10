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

		CHECK(Near(HoldSeconds(40, 38, t), 7.0f));
		CHECK(Near(HoldSeconds(40, 30, t), 10.5f));  // cowed: ten below

		CHECK(Near(TauntCooldown(20, t), 13.0f));
		CHECK(Near(TauntCooldown(200, t), 8.0f));
	}

	void Heal()
	{
		const Tuning                t;
		std::vector<HealCandidate> c(3);
		c[0] = { 0.9f, 100, false, false, true, false };   // player, fine
		c[1] = { 0.5f, 300, false, false, false, false };  // follower under 55%
		c[2] = { 0.5f, 300, true, false, false, false };   // same, but being hit
		CHECK(PickHealTarget(c, 0.55f, t) == 2);

		c[2].attacked = false;
		c[0].health = 0.5f;  // the player at the same health wins the tie
		CHECK(PickHealTarget(c, 0.55f, t) == 0);

		// The healer holds themselves to a lower line.
		std::vector<HealCandidate> self{ { 0.45f, 0, false, true, false, false } };
		CHECK(PickHealTarget(self, 0.55f, t) == -1);
		self[0].health = 0.35f;
		CHECK(PickHealTarget(self, 0.55f, t) == 0);

		// Out of range is ignored; bleeding out is always worth a heal.
		std::vector<HealCandidate> far{ { 0.1f, 5000, false, false, false, false }, { 0.8f, 200, false, false, false, true } };
		CHECK(PickHealTarget(far, 0.55f, t) == 1);

		std::vector<HealCandidate> fine{ { 0.9f, 100, false, false, true, false } };
		CHECK(PickHealTarget(fine, 0.55f, t) == -1);

		// The player under the line comes first, even before a companion who is down.
		std::vector<HealCandidate> first{ { 0.5f, 600, false, false, true, false }, { 0.0f, 300, true, false, false, true } };
		CHECK(PickHealTarget(first, 0.55f, t) == 0);
		first[0].health = 0.8f;  // the player is fine: the downed companion
		CHECK(PickHealTarget(first, 0.55f, t) == 1);
		first[0] = { 0.3f, 1500, false, false, true, false };  // under the line but out of reach
		CHECK(PickHealTarget(first, 0.55f, t) == 1);

		// A downed companion is walked to from much further than a heal is cast.
		std::vector<HealCandidate> reach{ { 0.2f, 1500, false, false, false, false }, { 0.3f, 2000, false, false, false, true } };
		CHECK(PickHealTarget(reach, 0.55f, t) == 1);
		reach[1].taken = true;  // another healer is on the way: nobody else goes
		CHECK(PickHealTarget(reach, 0.55f, t) == -1);
		reach[1].distance = 3000;
		reach[1].taken = false;
		CHECK(PickHealTarget(reach, 0.55f, t) == -1);
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
	}

	void RoleGating()
	{
		Arsenal sword{ true, false, false, false };
		Arsenal bare{};
		Arsenal mage{ false, false, true, false };
		Arsenal priest{ false, false, false, true };
		Arsenal ranger{ true, true, false, false };

		// No weapon: no melee roles; an empty-handed companion can still brawl.
		CHECK(LackFor(Role::kTank, mage, false) == Lack::kWeapon);
		CHECK(LackFor(Role::kBruiser, mage, false) == Lack::kWeapon);
		CHECK(LackFor(Role::kFighter, mage, false) == Lack::kWeapon);
		CHECK(LackFor(Role::kFighter, bare, false) == Lack::kNothing);
		CHECK(LackFor(Role::kTank, bare, false) == Lack::kWeapon);
		CHECK(LackFor(Role::kTank, sword, false) == Lack::kNothing);

		// Healer needs a heal of their own unless the borrowed heal is on.
		CHECK(LackFor(Role::kHealer, sword, false) == Lack::kHeal);
		CHECK(LackFor(Role::kHealer, sword, true) == Lack::kNothing);
		CHECK(LackFor(Role::kHealer, priest, false) == Lack::kNothing);
		CHECK(LackFor(Role::kArcher, sword, false) == Lack::kBow);
		CHECK(LackFor(Role::kArcher, ranger, false) == Lack::kNothing);
		CHECK(LackFor(Role::kMage, sword, false) == Lack::kSpells);

		// Falling back to the closest playable role.
		CHECK(Playable(Role::kTank, mage, false) == Role::kMage);
		CHECK(Playable(Role::kTank, priest, false) == Role::kHealer);
		CHECK(Playable(Role::kHealer, sword, false) == Role::kFighter);
		CHECK(Playable(Role::kArcher, sword, false) == Role::kFighter);
		CHECK(Playable(Role::kMage, ranger, false) == Role::kFighter);
		CHECK(Playable(Role::kTank, bare, false) == Role::kFighter);
		CHECK(Playable(Role::kTank, sword, false) == Role::kTank);
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
}

int main()
{
	Taunt();
	Heal();
	Targeting();
	DangerTiers();
	Styles();
	CodecRoundTrip();
	RoleDefaults();
	RoleGating();
	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
