#include "FWM/TreeNavigation.h"
#include "FWM/TargetPolicy.h"
#include "FWM/TreeEntrance.h"
#include "FWM/LoadoutPolicy.h"
#include "FWM/AbilityResource.h"
#include "FWM/Cooldowns.h"
#include "FWM/Hotkey.h"
#include "FWM/LevelRules.h"
#include "FWM/OrdersCodec.h"
#include "FWM/Rules.h"
#include "FWM/StyleLayer.h"
#include "FWM/Text.h"
#include "FWM/TraitRules.h"
#include "FWM/WolfBite.h"
#include "FWM/HealCircle.h"
#include "FWM/Rejuvenation.h"
#include "FWM/RejuvenatePolicy.h"
#include "FWM/Blast.h"
#include "FWM/MagnusOrb.h"
#include "FWM/Quake.h"
#include "FWM/ArrowRain.h"
#include "FWM/WaterCloak.h"
#include "FWM/Haunt.h"
#include "FWM/Paths.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
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

	void RejuvenationTimeline()
	{
		using namespace FWM::Rejuvenation;
		CHECK(At(2.0f, 3).radius > At(2.0f, 2).radius * 1.3f);
		CHECK(At(2.0f, 2).radius > At(2.0f, 1).radius);
		for (int rank = 1; rank <= 3; ++rank) {
			for (float age : { -1.0f, 0.0f, kSeconds, kSeconds + 5.0f }) {
				const auto look = At(age, rank);
				CHECK(Near(look.light, 0.0f));
				for (const auto& part : look.parts) CHECK(Near(part.alpha, 0.0f));
				for (const auto& mote : look.motes) CHECK(Near(mote.alpha, 0.0f));
			}
			for (int frame = 0; frame <= 360; ++frame) {
				const auto look = At(static_cast<float>(frame) / 30.0f, rank);
				for (const auto& part : look.parts) {
					CHECK(std::isfinite(part.scale) && part.scale > 0.0f);
					CHECK(std::isfinite(part.turn) && part.alpha >= 0.0f && part.alpha <= 1.0f);
				}
				for (const auto& mote : look.motes) {
					CHECK(std::isfinite(mote.at[0]) && std::isfinite(mote.at[1]) && std::isfinite(mote.at[2]));
					CHECK(mote.alpha >= 0.0f && mote.alpha <= 1.0f && mote.size > 0.0f);
				}
			}
		}
		const auto count = [](int rank) {
			const auto look = At(2.0f, rank);
			return std::count_if(look.motes.begin(), look.motes.end(), [](const auto& m) { return m.alpha > 0.0f; });
		};
		CHECK(count(3) > count(2) && count(2) > count(1));
		CHECK(Near(At(2.0f, 1).parts[5].alpha, 0.0f));
		CHECK(At(2.0f, 3).parts[5].alpha > 0.0f);
	}

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
		Tuning t;
		// The team heal: 40 and 3.5 a level, never past 400.
		CHECK(Near(TeamHealAmount(1, t), 43.5f));
		CHECK(Near(TeamHealAmount(30, t), 145.0f));
		CHECK(Near(TeamHealAmount(60, t), 250.0f));
		CHECK(Near(TeamHealAmount(200, t), 400.0f));
		CHECK(Near(TeamHealAmount(0, t), 43.5f));  // level 0 counts as 1

		CHECK(Near(RejuvenateSeconds(1, t), 8.0f) && Near(RejuvenateCooldown(1, t), 50.0f));
		CHECK(Near(RejuvenateSeconds(29, t), 8.0f));
		CHECK(Near(RejuvenateSeconds(30, t), 10.0f) && Near(RejuvenateCooldown(30, t), 55.0f));
		CHECK(Near(RejuvenateSeconds(59, t), 10.0f));
		CHECK(Near(RejuvenateSeconds(60, t), 12.0f) && Near(RejuvenateCooldown(60, t), 60.0f));
		t.rejuvenateSecondsAt60 = 180; // Old INI settings cannot restore the two-minute buff.
		CHECK(Near(RejuvenateSeconds(81, t), 12.0f));
		CHECK(Near(RejuvenatePolicy::Restore(140,0,.02f,12),33.6f));
		CHECK(Near(RejuvenatePolicy::Restore(10000,0,.02f,12),72.f));
		CHECK(Near(RejuvenatePolicy::Restore(140,139,.02f,12),1.f));
		CHECK(Near(RejuvenatePolicy::Restore(140,140,.02f,12),0.f));
		CHECK(Near(RejuvenatePolicy::Restore(140,0,.02f,-1),0.f));
		CHECK(Near(RejuvenatePolicy::Restore(140,0,.02f,.25f)*4,RejuvenatePolicy::Restore(140,0,.02f,1)));
		CHECK(Near(RejuvenatePolicy::Restore(140,0,RejuvenatePolicy::Rate(1),8,RejuvenatePolicy::Cap(1)),22.4f));
		CHECK(Near(RejuvenatePolicy::Restore(140,0,RejuvenatePolicy::Rate(2),10,RejuvenatePolicy::Cap(2)),49.f));
		CHECK(Near(RejuvenatePolicy::Restore(140,0,RejuvenatePolicy::Rate(3),12,RejuvenatePolicy::Cap(3)),84.f));
		CHECK(Near(RejuvenatePolicy::Restore(10000,0,RejuvenatePolicy::Rate(3),12,RejuvenatePolicy::Cap(3)),240.f));
		CHECK(RejuvenatePolicy::Inside(280,0,180,280));
		CHECK(!RejuvenatePolicy::Inside(281,0,0,280));
		CHECK(!RejuvenatePolicy::Inside(0,0,181,280));
		CHECK(!RejuvenatePolicy::Inside(200,200,0,280));
		RejuvenatePolicy::Pulse overlap;
		overlap.Include(1,8,.005f); overlap.Include(1,12,.015f);
		CHECK(Near(overlap.magickaSeconds,1) && Near(overlap.staminaShareSeconds,.015f));
		RejuvenatePolicy::Pulse ending;
		ending.Include(1,.25f,.01f);
		CHECK(Near(ending.magickaSeconds,.25f) && Near(ending.staminaShareSeconds,.0025f));
		RejuvenatePolicy::Pulse expired;
		expired.Include(1,0,.015f); expired.Include(1,-1,.015f);
		CHECK(Near(expired.magickaSeconds,0) && Near(expired.staminaShareSeconds,0));
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
        const std::array<std::uint32_t,2> skalds{10,11};
        CHECK(TargetPolicy::Redirect(10,20,skalds));
        CHECK(TargetPolicy::Redirect(11,20,skalds));
        CHECK(!TargetPolicy::Redirect(30,20,skalds)); // leave native choices of other allies alone
        CHECK(!TargetPolicy::Redirect(10,0,skalds));  // nobody else is reachable
        CHECK(!TargetPolicy::Redirect(20,20,skalds));
        CHECK(TargetPolicy::Redirect(30,20,{}));     // explicit taunts still override normally
        const auto bard=TimidSkald(Committed(StyleData{}));
        CHECK(bard.offensive<.05f&&bard.defensive==1.f);
        // Holds its stance spell like a mage; no other weapon or magic.
        CHECK(bard.unarmedScore>0.f&&bard.magicScore>=1.f&&bard.meleeScore==0.f&&bard.rangedScore==0.f&&bard.staffScore==0.f&&bard.shoutScore==0.f);
        CHECK((bard.flags&(StyleData::kDueling|StyleData::kFlanking))==0);
        StyleData fleeing;fleeing.fallback=fleeing.avoidThreat=fleeing.strafe=fleeing.circle=1.f;
        StyleTweaks recovering;recovering.recovery=true;
        const auto travellingBard=TimidSkald(Tweaked(fleeing,recovering),true);
        CHECK(travellingBard.fallback==0.f&&travellingBard.avoidThreat==0.f); // walking back, nothing turns it around
        CHECK(travellingBard.strafe==0.f&&travellingBard.circle==0.f&&travellingBard.unarmedScore>0.f); // never left with nothing usable
        CHECK(travellingBard.offensive==0.f&&travellingBard.groupOffensive==0.f);
        // Playing, it moves as its own style moves a mage.
        StyleData mage;mage.fallback=.6f;mage.avoidThreat=.4f;mage.strafe=.5f;mage.circle=.3f;mage.magicScore=0.f;
        const auto settledBard=TimidSkald(mage,false);
        CHECK(settledBard.fallback==.6f&&settledBard.avoidThreat==.4f&&settledBard.strafe==.5f&&settledBard.circle==.3f&&settledBard.magicScore>=1.f);
		StyleData base;  // vanilla-ish defaults
		base.offensive = 0.6f;
		base.defensive = 0.3f;
		base.flags = StyleData::kFlanking;

		const auto none = Layer(base, Role::kTank, Position::kFree, 0.0f);
		CHECK(Near(none.offensive, 0.6f) && Near(none.defensive, 0.3f) && none.flags == base.flags);

		// A role only moves them: what they attack with, and how, stays their own.
		const auto tank = Layer(base, Role::kTank, Position::kFree, 1.0f);
		CHECK(Near(tank.offensive, 0.6f) && Near(tank.defensive, 0.3f));
		CHECK(Near(tank.bash, base.bash) && Near(tank.powerAttackStaggered, base.powerAttackStaggered));
		CHECK(Near(tank.meleeScore, base.meleeScore));
		CHECK(Near(tank.circle, 0.15f) && Near(tank.fallback, 0.05f));
		CHECK((tank.flags & StyleData::kDueling) != 0);
		CHECK((tank.flags & StyleData::kFlanking) == 0);

		const auto half = Layer(base, Role::kTank, Position::kFree, 0.5f);
		CHECK(Near(half.circle, 0.175f));

		// Sent after someone (or held by a taunt): no circling, falling back, dueling or
		// strafing, and the shield comes down; what they fight with is untouched.
		const auto committed = Committed(tank);
		CHECK(Near(committed.circle, 0.0f) && Near(committed.fallback, 0.0f) && Near(committed.strafe, 0.0f));
		CHECK(Near(committed.offensive, 1.0f) && committed.defensive <= 0.15f);
		CHECK((committed.flags & (StyleData::kDueling | StyleData::kFlanking)) == 0);
		CHECK(Near(committed.meleeScore, tank.meleeScore) && Near(committed.bash, tank.bash));

		// No role or position changes what they fight with.
		base.magicScore = 0.3f;
		base.rangedScore = 5.0f;
		base.meleeScore = 0.83f;
		for (const auto role : { Role::kFighter, Role::kTank, Role::kHealer, Role::kBruiser, Role::kArcher, Role::kMage }) {
			for (const auto position : { Position::kFree, Position::kGuard, Position::kWithMe, Position::kHoldBack, Position::kFrontLine }) {
				const auto layered = Layer(base, role, position, 1.0f);
				CHECK(Near(layered.meleeScore, base.meleeScore) && Near(layered.magicScore, base.magicScore) && Near(layered.rangedScore, base.rangedScore));
				CHECK(Near(layered.bash, base.bash) && Near(layered.powerAttackBlocking, base.powerAttackBlocking) && Near(layered.defensive, base.defensive));
			}
		}

		const auto back = Layer(base, Role::kFighter, Position::kHoldBack, 1.0f);
		const auto free = Layer(base, Role::kFighter, Position::kFree, 1.0f);
		CHECK(back.fallback > free.fallback);
		CHECK(back.offensive < free.offensive);

		// Front-line melee closes even when its base or a learned perk prefers to flank.
		// The change must not strip a tank's blocking or push an archer into melee.
		StyleTweaks flank;
		flank.flank = 1.4f;
		for (const auto role : { Role::kFighter, Role::kTank, Role::kBruiser }) {
			const auto layered = Tweaked(Layer(base, role, Position::kFrontLine, 0.75f), flank);
			const auto closing = PositionIntent(layered, role, Position::kFrontLine, 0.75f);
			CHECK(Near(closing.circle, 0) && Near(closing.flankDistance, 0) && Near(closing.stalkTime, 0));
			CHECK((closing.flags & (StyleData::kFlanking | StyleData::kDueling)) == 0);
			CHECK(Near(closing.defensive, layered.defensive) && Near(closing.bash, layered.bash));
			CHECK(Near(closing.meleeScore, layered.meleeScore) && Near(closing.magicScore, layered.magicScore));
		}
		for (const auto position : { Position::kFree, Position::kGuard, Position::kWithMe, Position::kHoldBack }) {
			const auto original = Tweaked(Layer(base, Role::kFighter, position, 0.75f), flank);
			const auto kept = PositionIntent(original, Role::kFighter, position, 0.75f);
			CHECK(Near(kept.circle, original.circle) && kept.flags == original.flags);
		}
		for (const auto role : { Role::kArcher, Role::kMage, Role::kHealer }) {
			const auto original = Layer(base, role, Position::kFrontLine, 0.75f);
			const auto kept = PositionIntent(original, role, Position::kFrontLine, 0.75f);
			CHECK(Near(kept.circle, original.circle) && kept.flags == original.flags);
		}
		const auto untouched = PositionIntent(base, Role::kFighter, Position::kFrontLine, 0.0f);
		CHECK(Near(untouched.circle, base.circle) && untouched.flags == base.flags);

		const auto archerBack = Layer(base, Role::kArcher, Position::kHoldBack, 0.75f);
		const auto archerGuard = Layer(base, Role::kArcher, Position::kGuard, 0.75f);
		const auto archerFree = Layer(base, Role::kArcher, Position::kFree, 0.75f);
		const auto archerFront = Layer(base, Role::kArcher, Position::kFrontLine, 0.75f);
		CHECK(archerGuard.fallback >= 0.67f && archerFront.fallback < archerBack.fallback);
		CHECK(archerBack.fallback >= 0.67f);
		CHECK(archerFree.fallback < archerBack.fallback);  // free to move as they like
		// With me: planted at your side, never backing off or circling away to cover.
		const auto archerWithMe = Layer(base, Role::kArcher, Position::kWithMe, 0.75f);
		CHECK(archerWithMe.fallback < archerFree.fallback && archerWithMe.avoidThreat < archerBack.avoidThreat);
		CHECK(archerWithMe.circle <= 0.2f);
		CHECK(Layer(base, Role::kFighter, Position::kWithMe, 1.0f).fallback < free.fallback);
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

		// A save from before the formation position was retired fights Free.
		auto formation = bytes;
		formation[4 + 6] = 0;
		const auto retired = FWM::Codec::Decode(formation, FWM::Codec::kVersion);
		CHECK(retired.size() == 2 && retired[0].second.position == Position::kFree);

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
		CHECK(LackFor(Role::kBruiser, mage) == Lack::kTwoHanded);
        CHECK(LackFor(Role::kBruiser, sword) == Lack::kTwoHanded);
        Arsenal greatsword=sword;greatsword.twoHanded=true;
        CHECK(LackFor(Role::kBruiser,greatsword)==Lack::kNothing);
        // Role weapons off: any melee weapon makes a Bruiser, but not none at all.
        CHECK(LackFor(Role::kBruiser,sword,false)==Lack::kNothing);
        CHECK(LackFor(Role::kBruiser,mage,false)==Lack::kWeapon);
        CHECK(Playable(Role::kBruiser,sword,false)==Role::kBruiser&&Playable(Role::kBruiser,sword)!=Role::kBruiser);
        greatsword.twoHanded=false;
        CHECK(Playable(Role::kBruiser,greatsword)==Role::kFighter);
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
		CHECK(ForRole(Role::kFighter).position == Position::kFree);
		CHECK(Orders{}.position == Position::kFree);
		// Walk With Me's formation is no position any more: saves holding it fight Free.
		CHECK(Live(Position::kRetired) == Position::kFree);
		CHECK(Live(Position::kGuard) == Position::kGuard);

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

		// Weapons come out on sight only with auto battle and the switch on, and not while you sneak.
		CHECK(Rules::DrawsOnSight(true, true, false));
		CHECK(!Rules::DrawsOnSight(false, true, false));
		CHECK(!Rules::DrawsOnSight(true, false, false));
		CHECK(!Rules::DrawsOnSight(true, true, true));

		// Weapons drawn on sight go away once the view is clear for long enough, but follow yours when out.
		CHECK(!Rules::SheathesAfterSight(2.0f, 5.0f, false));
		CHECK(Rules::SheathesAfterSight(5.0f, 5.0f, false));
		CHECK(!Rules::SheathesAfterSight(30.0f, 5.0f, true));

		// A grazing elk is no enemy; a wolf, a cornered elk or an unaggressive bandit is.
		CHECK(Rules::KeepsToItself(true, 0.0f, false));
		CHECK(!Rules::KeepsToItself(true, 1.0f, false));
		CHECK(!Rules::KeepsToItself(true, 0.0f, true));
		CHECK(!Rules::KeepsToItself(false, 0.0f, false));
	}

	void PathBonus()
	{
		const Rules::Tuning t;
		// A percent a point by default, for every role.
		CHECK(Rules::PathBonus(Role::kFighter, 17, t) == 17.0f);
		CHECK(Rules::PathBonus(Role::kTank, 0, t) == 0.0f);
		CHECK(Rules::PathBonus(Role::kHealer, -3, t) == 0.0f);
		// Archer stamina recovery and the mage cost reduction have separate caps.
		CHECK(Rules::PathBonus(Role::kArcher, 200, t) == Rules::kMostArcherStamina);
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
		CHECK(Rules::PathBonusText(Role::kArcher, 12.0f) == "+12% stamina recovery");
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

	void Levels()
	{
		using namespace FWM::Leveling;
		FWM::Leveling::Tuning t;
		t.climb = 0.0f;
		CHECK(Near(Cost(30, t), 825.0f));  // the player's: 75 + 25 x 30
		CHECK(Near(Cost(1, t), 100.0f));
		CHECK(Near(EnemyXP(30, 30, false, t), 90.0f));         // an even enemy: 3 x their level
		CHECK(Near(EnemyXP(30, 30, true, t), 270.0f));         // a boss
		CHECK(Near(EnemyXP(40, 30, false, t), 135.0f, 0.01f));  // ten above: half as much again
		CHECK(Near(EnemyXP(90, 30, false, t), 157.5f, 0.01f));  // far above: 1.75 times, no more
		CHECK(Near(EnemyXP(20, 30, false, t), 45.0f, 0.01f));   // ten below: half
		CHECK(Near(EnemyXP(1, 30, false, t), 22.5f, 0.01f));    // far below: a quarter, no less
		CHECK(EnemyXP(0, 0, false, t) > 0.0f);

		// A fight: the one worth the most in full, each after it 0.75 of the one before.
		const std::vector<Foe> lone{ { 30, false } };
		const std::vector<Foe> four(4, Foe{ 30, false });
		CHECK(Near(FightXP(lone, 30, t), 90.0f));
		CHECK(Near(FightXP(four, 30, t), 90.0f * (1.0f + 0.75f + 0.5625f + 0.421875f), 0.01f));
		const std::vector<Foe> horde(200, Foe{ 30, false });
		CHECK(FightXP(horde, 30, t) <= 4.0f * 90.0f + 0.01f);  // never more than four of its best
		CHECK(FightXP(horde, 30, t) > FightXP(four, 30, t));    // but more than a few
		const std::vector<Foe> mixed{ { 10, false }, { 30, true }, { 30, false } };  // the boss counts in full wherever it fell
		CHECK(Near(FightXP(mixed, 30, t), 270.0f + 0.75f * 90.0f + 0.5625f * EnemyXP(10, 30, false, t), 0.01f));
		CHECK(Near(FightXP({}, 30, t), 0.0f));

		const auto none = Earn(30, 100.0f, 50.0f, kNoCap, t);
		CHECK(none.levels == 0 && Near(none.xp, 150.0f) && !none.capped);
		const auto one = Earn(30, 800.0f, 50.0f, kNoCap, t);
		CHECK(one.levels == 1 && Near(one.xp, 25.0f));
		const auto many = Earn(1, 0.0f, 100.0f + 125.0f + 150.0f + 10.0f, kNoCap, t);  // levels 1, 2 and 3 paid for
		CHECK(many.levels == 3 && Near(many.xp, 10.0f));
		CHECK(Earn(200, 0.0f, 99999.0f, kNoCap, t).levels > 0);  // with the cap off, none

		// Never past the player's level: at it nothing is earned, and what reaches it stops there.
		const auto atCap = Earn(30, 100.0f, 5000.0f, 30, t);
		CHECK(atCap.levels == 0 && atCap.capped && Near(atCap.xp, 100.0f));
		const auto above = Earn(35, 0.0f, 5000.0f, 30, t);
		CHECK(above.levels == 0 && above.capped);
		const auto reach = Earn(28, 0.0f, 1.0e6f, 30, t);
		CHECK(reach.levels == 2 && reach.capped && Near(reach.xp, 0.0f));
		const auto short_ = Earn(28, 0.0f, 50.0f, 30, t);
		CHECK(short_.levels == 0 && !short_.capped && Near(short_.xp, 50.0f));

		const FWM::Leveling::Tuning steep;  // the default climb
		CHECK(Near(Cost(1, steep), 100.0f));                // the first level is the player's
		CHECK(Near(Cost(21, steep), 2.0f * Cost(21, t)));   // twice the player's by 21
		CHECK(Near(Cost(41, steep), 3.0f * Cost(41, t)));   // three times by 41
		// What made it climb: even fights per level rise with level instead of holding steady.
		const auto fights = [&](int a_level) { return Cost(a_level, steep) / FightXP(std::vector<Foe>(4, Foe{ a_level, false }), a_level, steep); };
		CHECK(fights(50) > fights(10) * 1.5f);
		CHECK(fights(100) > fights(50) * 1.5f);
		CHECK(fights(10) > 4.0f && fights(30) > 7.0f);  // slower than one level every few fights
		CHECK(Earn(500, 0.0f, 1.0e9f, kNoCap, steep).levels > 0);
		FWM::Leveling::Tuning wild = t;
		wild.climb = -1.0f;
		CHECK(Near(Cost(30, wild), Cost(30, t)));  // a climb below 0 never makes a level cheaper

		for (int i = 0; i < static_cast<int>(Role::kCount); ++i) {
			const auto g = GrowthFor(static_cast<Role>(i));
			CHECK(Near(g.health + g.magicka + g.stamina, 10.0f));  // ten points a level, as the player's
			CHECK(g.skillCount >= 1 && g.skillCount <= g.skills.size());
		}
		CHECK(GrowthFor(Role::kMage).magicka > GrowthFor(Role::kMage).health);
		CHECK(GrowthFor(Role::kTank).skills[0] == Skill::kBlock);
	}

	void UniqueTraits()
	{
		using namespace FWM::Traits;
		CHECK(RarityFor(0) == Rarity::kCommon);
		CHECK(RarityFor(1) == Rarity::kUncommon);
		CHECK(RarityFor(2) == Rarity::kRare);
		CHECK(RarityFor(3) == Rarity::kEpic);
		CHECK(RarityFor(4) == Rarity::kLegendary);
		CHECK(RarityFor(9) == Rarity::kLegendary);
		std::vector<Trait> some{ { "A" }, { "B" }, { "C" } };
		some[2].locked = true;
		CHECK(Counted(some) == 2);  // a locked one shows but does not count
		CHECK(SignatureSource("Katana.esp", "katana.ESP"));
		CHECK(!SignatureSource("Ordinator - Perks of Skyrim.esp", "Katana.esp"));
		CHECK(!SignatureSource("Bloodstains.esp", "Katana.esp"));
		CHECK(!SignatureSource("FollowerFramework.esp", "Katana.esp"));
		CHECK(!SignatureSource("", "Katana.esp"));
		CHECK(!SignatureSource("", ""));
		CHECK(SignatureSource("FollowerAddon.esp", "Katana.esp", true));  // explicitly documented
		CHECK(CardVisible(0) == 0 && CardVisible(4) == 4);
		CHECK(CardVisible(5) == 3 && CardVisible(27) == 3);

		// Katana's moves, as her plugin names them.
		CHECK(PickName("Katana Opportunity Spell", "Katana Attack Opportunity", "Katana") == "Opportunity");
		CHECK(PickName("Mist Raven Dash", "Mist Raven", "Katana") == "Mist Raven Dash");
		CHECK(PickName("", "Katana Spectral Perk", "Katana") == "Spectral");
		CHECK(CleanName("Lucien's Arcane Ward Effect", "Lucien Flavius") == "Arcane Ward");
		CHECK(CleanName("Spell", "Katana").empty());
		CHECK(CleanName("Katana", "Katana") == "Katana");  // a name that is only theirs stays

		CHECK(Describe({ Moment::kPowerAttack, 18 }) == "On a power attack, 18% of the time");
		CHECK(Describe({ Moment::kSwing, 40, 0.4f }) == "On a swing while below 40% health, 40% of the time");
		CHECK(Describe({ Moment::kFightStart, 25, -1.0f, 0.95f }) == "When a fight starts against a target above 95% health, 25% of the time");
		CHECK(Describe({ Moment::kHit }) == "When their blow lands");

		CHECK(LosesNerve(0) && LosesNerve(2) && !LosesNerve(3) && !LosesNerve(4) && !LosesNerve(-1));
		CHECK(NerveText(2) == "Runs when badly hurt");
		CHECK(NerveText(3).empty());

		// A move both a script and a perk start is one trait.
		std::vector<Trait> found{ { "Shadow Step", "When a fight starts", "", Kind::kStrike },
			{ "Mist Raven", "On a swing, 18% of the time", "", Kind::kEvade },
			{ "shadow step", "On a power attack, 18% of the time", "Strikes from behind.", Kind::kStrike, true } };
		Merge(found);
		CHECK(found.size() == 2);
		CHECK(found[0].when == "When a fight starts, or on a power attack, 18% of the time");
		CHECK(found[0].what == "Strikes from behind.");
		CHECK(!found[0].locked);  // one part open is enough

		// Repeated perk entries must not grow either the count or the trigger text.
		found.push_back({ "Shadow Step", "On a power attack, 18% of the time" });
		found.push_back({ "SHADOW STEP", "When a fight starts" });
		Merge(found);
		CHECK(found.size() == 2 && Counted(found) == 2);
		CHECK(found[0].when == "When a fight starts, or on a power attack, 18% of the time");

		// Katana's three real moves stay Epic despite duplicate sources or a drawback.
		found.push_back({ "Spectral Warband", "", "", Kind::kSummon });
		found.push_back({ "Loses nerve", "", "", Kind::kNerve });
		Merge(found);
		CHECK(RarityFor(Counted(found)) == Rarity::kEpic);
		found[2].locked = true;
		CHECK(RarityFor(Counted(found)) == Rarity::kRare);
		CHECK(Counted({ { "Loses nerve", "", "", Kind::kNerve } }) == 0);

		// Megara's buff choices form one move; her shadow response is a second.
		std::vector<Trait> megara{ { "Battle Boon", "In a fight", "", Kind::kAura },
			{ "Battle Boon", "In a fight", "", Kind::kAura },
			{ "Shadow Cloak", "When combat starts in Shadow mode", "", Kind::kEvade } };
		Merge(megara);
		CHECK(megara.size() == 2 && RarityFor(Counted(megara)) == Rarity::kRare);

		// Compact presentation cannot lower rarity, and puts usable moves before locks.
		std::vector<Trait> large{ { "Locked move", "", "", Kind::kStrike, true },
			{ "Loses nerve", "", "", Kind::kNerve }, { "A" }, { "B" }, { "C" }, { "D" }, { "E" } };
		Merge(large);
		CHECK(large.front().name == "A" && large.back().kind == Kind::kNerve);
		CHECK(CardVisible(large.size()) == 3 && Counted(large) == 5);
		CHECK(RarityFor(Counted(large)) == Rarity::kLegendary);
	}

	void Translations()
	{
		using namespace FWM::Text;
		Clear();
		// Nothing loaded: the English, the very same text.
		constexpr std::string_view taunt = "Taunt now";
		CHECK(Tr(taunt).data() == taunt.data());
		CHECK(Trf("{} learns {}.", "Lydia", "Ward") == "Lydia learns Ward.");
		CHECK(Trn(1, "{} point", "{} points") == "1 point" && Trn(4, "{} point", "{} points") == "4 points");

		// A file: a byte order mark, comments, blank lines, escapes, a line left English, a line
		// without a tab.
		const auto loaded = Add("\xEF\xBB\xBF# a comment\r\n\r\nTaunt now\tProvocar ahora\r\n"
								"Line one\\nline two\tL\xC3\xADnea uno\\nl\xC3\xADnea dos\n"
								"Left as it is\t\n"
								"no tab here\n"
								"{} learns {}.\t{1} est appris par {0}.\n"
								"{} point\tun point\n"
								"{} points\t{} points (pl.)\n"
								"{} has {}.\t{} a {} {}.\n");
		CHECK(loaded.entries == 6);
		CHECK(loaded.unreadable == 1);
		CHECK(Tr("Taunt now") == "Provocar ahora");
		CHECK(Tr("Line one\nline two") == "L\xC3\xADnea uno\nl\xC3\xADnea dos");
		CHECK(Tr("Left as it is") == "Left as it is");
		CHECK(std::string(Trc("Taunt now")) == "Provocar ahora");
		// The values can change places.
		CHECK(Trf("{} learns {}.", "Lydia", "Ward") == "Ward est appris par Lydia.");
		CHECK(Trn(1, "{} point", "{} points") == "un point" && Trn(3, "{} point", "{} points") == "3 points (pl.)");
		// A translation with a {} too many: the English, not a crash.
		CHECK(Trf("{} has {}.", "Aela", "a bow") == "Aela has a bow.");

		// UTF-16 with its mark, as the game's own translation files are saved.
		Clear();
		std::string wide{ "\xFF\xFE" };
		for (const char16_t c : std::u16string_view(u"Heal\tSoigner\r\n")) {
			wide.push_back(static_cast<char>(c & 0xFF));
			wide.push_back(static_cast<char>(c >> 8));
		}
		CHECK(Add(wide).entries == 1);
		CHECK(Tr("Heal") == "Soigner");
		Clear();
		CHECK(Tr("Heal") == "Heal");

		// Whole characters, and capitals beyond ASCII.
		const std::string_view mixed = "a\xC3\xA9\xD0\xB4\xE4\xB8\xAD";  // a, e acute, Cyrillic de, a CJK character
		CHECK(CharLength(mixed, 0) == 1 && CharLength(mixed, 1) == 2 && CharLength(mixed, 3) == 2 && CharLength(mixed, 5) == 3);
		CHECK(CharLength("\xC3", 0) == 1);  // cut short: one byte, never past the end
		CHECK(Upper("caf\xC3\xA9 \xD0\xB4\xD0\xBE\xD0\xBC") == "CAF\xC3\x89 \xD0\x94\xD0\x9E\xD0\x9C");
		CHECK(Upper("Paths") == "PATHS");
		CHECK(Escape("a\tb\nc\\d") == "a\\tb\\nc\\\\d");
	}

	// Kynareth's Maw's bite: it is in front of the enemy from the start, their middle between its
	// jaws; the jaws are open until they slam shut on the bite; it fades in and out.
	// The healer's circle: it goes where it covers the most need, you and the downed counting
	// double; it rises, stands and sinks.
	void HealingCircle()
	{
		using namespace FWM::HealCircle;
		{
			const std::vector<Ally> alone{ { 0.0f, 0.0f, 0.5f, true, false } };
			CHECK(Place(alone, kRadius) == 0);
			const std::vector<Ally> fine{ { 0.0f, 0.0f, 1.0f, true, false }, { 100.0f, 0.0f, 1.0f, false, false } };
			CHECK(Place(fine, kRadius) == -1);
			// Two companions badly hurt together outweigh you a little hurt on your own.
			const std::vector<Ally> cluster{ { 0.0f, 0.0f, 0.9f, true, false }, { 900.0f, 0.0f, 0.4f, false, false }, { 950.0f, 60.0f, 0.4f, false, false } };
			CHECK(Place(cluster, kRadius) == 1 || Place(cluster, kRadius) == 2);
			// Someone down counts double: one downed companion beats one companion at half.
			const std::vector<Ally> down{ { 0.0f, 0.0f, 0.5f, false, false }, { 900.0f, 0.0f, 0.1f, false, true } };
			CHECK(Place(down, kRadius) == 1);
			// You first on a tie.
			const std::vector<Ally> tie{ { 0.0f, 0.0f, 0.5f, false, false }, { 100.0f, 0.0f, 0.75f, true, false } };
			CHECK(Place(tie, kRadius) == 1);
		}
		CHECK(At(0.0f, kSeconds).ring < 0.01f);
		CHECK(At(1.0f, kSeconds).ring > 0.99f);
		CHECK(At(kSeconds, kSeconds).ring < 0.01f);
		// The aura rises out of the ground, stands, and is gone at the end; its two lines turn opposite ways.
		CHECK(At(0.0f, kSeconds).aura < 0.01f);
		CHECK(At(kRise * 0.5f, kSeconds).aura > 0.2f);
		const auto standing = At(kRise + 0.2f, kSeconds);
		CHECK(standing.aura > 0.95f && standing.aura < 1.13f);
		CHECK(At(kSeconds, kSeconds).aura < 0.01f);
		const auto later = At(kRise + 1.2f, kSeconds);
		CHECK(later.waves[0] > standing.waves[0] && later.waves[1] < standing.waves[1]);
		// A rising edge loses light with height, and its cycle wraps while invisible.
		const auto lowPulse = At(0.8f, kSeconds), highPulse = At(2.4f, kSeconds);
		CHECK(highPulse.edgeHeight[0] > lowPulse.edgeHeight[0]);
		CHECK(highPulse.edgeAlpha[0] < lowPulse.edgeAlpha[0] * 0.2f);
		CHECK(At(3.199f, kSeconds).edgeAlpha[0] < 0.001f);
		CHECK(At(3.2f, kSeconds).edgeAlpha[0] < 0.001f);
		for (float t : { 0.0f, kSeconds }) for (float alpha : At(t, kSeconds).edgeAlpha) CHECK(Near(alpha, 0.0f));
		// Wide Circle III's own show only when it is grand: its inner circle, white heart and specks,
		// the specks anywhere inside it; none of it before or after, and the rest of it the same.
		for (float t = 0.0f; t <= kSeconds; t += 0.1f) {
			const auto plain = At(t, kSeconds), grand = At(t, kSeconds, true);
			CHECK(plain.inner == 0.0f && plain.heart == 0.0f);
			CHECK(Near(plain.ring, grand.ring) && Near(plain.aura, grand.aura) && Near(plain.turn, grand.turn));
			for (std::size_t i = 0; i < grand.glints.size(); ++i) {
				CHECK(plain.glints[i].alpha == 0.0f);
				CHECK(std::hypot(grand.glints[i].x, grand.glints[i].y) <= kMeshRadius * 0.92f);
				CHECK(grand.glints[i].z >= 0.0f);
			}
		}
		const auto wide = At(3.0f, kSeconds, true);
		CHECK(wide.inner > 0.99f && wide.heart > 0.75f);
		int lit = 0;
		for (const auto& glint : wide.glints) {
			lit += glint.alpha > 0.1f ? 1 : 0;
		}
		CHECK(lit > kGlints / 3);
		CHECK(At(0.0f, kSeconds, true).inner < 0.01f && At(kSeconds, kSeconds, true).inner < 0.01f && At(kSeconds, kSeconds, true).heart < 0.01f);
	}

	void WolfBiteTimeline()
	{
		using namespace FWM::WolfBite;
		auto gap = [](Vec a_a, Vec a_b) { return std::hypot(a_a.x - a_b.x, a_a.y - a_b.y, a_a.z - a_b.z); };
		const Vec   at{ 100.0f, 200.0f, 10.0f };
		const float chest = 70.0f;
		const Vec   middle{ at.x, at.y, at.z + chest };
		// Their middle under the jaws: right in front of them, lifted only to keep the head off the ground.
		auto across = [](Vec a_a, Vec a_b) { return std::hypot(a_a.x - a_b.x, a_a.y - a_b.y); };
		const auto settled = At(kRise, 0.0f, at, chest);
		CHECK(across(Place(settled, kGrip), middle) < 3.0f);
		CHECK(Place(settled, kGrip).z >= middle.z - 3.0f);
		const auto snap = At(kSnap, 0.0f, at, chest);
		CHECK(snap.jaw < 0.05f);
		CHECK(across(Place(snap, kGrip), middle) < 0.15f * kHeadLength * kHeadScale);
		CHECK(At(kTense, 0.0f, at, chest).jaw > 0.7f);
		CHECK(At(0.0f, 0.0f, at, chest).alpha < 0.01f);
		CHECK(At(kTense, 0.0f, at, chest).alpha > 0.99f);
		CHECK(At(kGone, 0.0f, at, chest).alpha < 0.01f);
		// Facing north, the muzzle is north of the skull; facing east, east of it.
		CHECK(Place(settled, kHeadFront).y > Place(settled, kHeadBack).y);
		const auto east = At(kRise, 1.5707964f, at, chest);
		CHECK(Place(east, kHeadFront).x > Place(east, kHeadBack).x);
		// The head never starts far off (no lunge from behind) and never dips into the ground.
		for (float t = 0.0f; t <= kGone; t += 0.05f) {
			const auto pose = At(t, 0.0f, at, chest);
			CHECK(across(Place(pose, kGrip), middle) < 0.6f * kHeadLength * kHeadScale);
			CHECK(Lowest(pose) >= at.z + kClearance - 0.01f);
		}
	}

	void ElementalBlast()
	{
		using namespace FWM::Blast;
		CHECK(Flight(0.0f) == kFlightShortest);
		CHECK(Flight(100000.0f) == kFlightLongest);
		CHECK(Near(Share(0.0f, 250.0f), 1.0f));
		CHECK(Near(Share(250.0f, 250.0f), kEdge));
		CHECK(Share(200.0f, 250.0f) < Share(120.0f, 250.0f));
		CHECK(kRadius[0] < kRadius[1] && kRadius[1] < kRadius[2]);
		// It lingers: past its first moment, everything runs kLinger slower.
		CHECK(Near(Real(0.1f), 0.1f) && Near(Real(1.15f), kSnap + 1.0f * kLinger));
		CHECK(Seconds(Element::kFire) > 2.4f);
		// A facing strip keeps its length and turns its face to the camera, whatever the camera does.
		{
			Part bolt;
			bolt.faces = true;
			for (const Vec camera : { Vec{ 500.0f, 0.0f, 100.0f }, Vec{ 0.0f, -300.0f, 900.0f }, Vec{ -40.0f, 80.0f, 0.0f } }) {
				const auto m = Facing(bolt, camera);
				CHECK(Near(m[2][2], 1.0f, 0.001f));                                                  // still along +Z
				CHECK(m[0][1] * camera.x + m[1][1] * camera.y + m[2][1] * camera.z >= 0.0f);       // facing the camera
				const float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
				                  m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
				CHECK(Near(det, 1.0f, 0.001f));
			}
			Part arc;
			arc.faces = true;
			arc.tilt = 1.5707964f;
			arc.yaw = 0.5f;
			const auto m = Facing(arc, Vec{ 0.0f, 0.0f, 0.0f });  // looking straight down its length still gives a turn
			CHECK(Near(m[0][2] * m[0][2] + m[1][2] * m[1][2] + m[2][2] * m[2][2], 1.0f, 0.001f));
			CHECK(Near(m[0][2], std::cos(0.5f), 0.001f) && Near(m[1][2], std::sin(0.5f), 0.001f));
		}
		// The grand blast's own parts: what a plain one never shows.
		auto grandOnly = [](std::string_view a_name) {
			return a_name.starts_with("Burst") || a_name.starts_with("Flare") || a_name.starts_with("Spire") || a_name.starts_with("Chip") || a_name.starts_with("Twin");
		};
		for (int e = 0; e < static_cast<int>(Element::kCount); ++e) {
			const auto element = static_cast<Element>(e);
			const auto names = Parts(element);
			CHECK(names.size() <= static_cast<std::size_t>(kMaxParts));
			for (std::size_t i = 0; i < names.size(); ++i) {
				for (std::size_t j = i + 1; j < names.size(); ++j) {
					CHECK(names[i] != names[j]);
				}
			}
			for (const bool grand : { false, true }) {
				const float seconds = Seconds(element, grand);
				CHECK(seconds >= Seconds(element));
				// Nothing before it goes off, a bright flash and light at once, the shockwave out to the
				// full reach, and nothing left at the end.
				for (const auto& part : At(element, -0.01f, grand).parts) {
					CHECK(part.alpha == 0.0f);
				}
				CHECK(At(element, 0.02f, grand).parts[0].alpha > 0.8f);
				CHECK(At(element, 0.03f, grand).light > 0.5f);
				float wave = 0.0f;
				for (float t = 0.0f; t <= 0.5f; t += 0.01f) {
					wave = std::max(wave, At(element, t, grand).parts[1].scale);
				}
				CHECK(wave > 0.97f && wave <= 1.0f);
				const auto end = At(element, seconds, grand);
				CHECK(end.light < 0.01f);
				for (std::size_t i = 0; i < names.size(); ++i) {
					CHECK(end.parts[i].alpha < 0.01f);
				}
				std::vector<float> brightest(names.size(), 0.0f);
				for (float t = 0.0f; t <= seconds; t += 1.0f / 60.0f) {
					const auto look = At(element, t, grand);
					int        bolts = 0, twins = 0;
					for (std::size_t i = 0; i < names.size(); ++i) {
						const auto& part = look.parts[i];
						CHECK(part.alpha >= 0.0f && part.alpha <= 1.0f);
						brightest[i] = std::max(brightest[i], part.alpha);
						// Nothing thrown out goes under the ground (the frost's ice sinks into it as it shatters).
						if (!names[i].starts_with("Shard") && names[i] != "Spire") {
							CHECK(part.at.z >= 0.0f);
						}
						bolts += names[i].starts_with("Bolt") && part.alpha > 0.0f ? 1 : 0;
						twins += names[i].starts_with("Twin") && part.alpha > 0.0f ? 1 : 0;
					}
					CHECK(bolts <= 1);  // one bolt at a time: the flicker picks which
					CHECK(twins <= 1);
				}
				// Every part shows at some point, but the grand blast's own only in a grand one.
				for (std::size_t i = 0; i < names.size(); ++i) {
					if (grandOnly(names[i]) && !grand) {
						CHECK(brightest[i] == 0.0f);
					} else {
						CHECK(brightest[i] > 0.3f);
					}
				}
			}
			// The light leaves the hand, lands on the feet, and dives at the end.
			const Vec  hand{ -600.0f, -300.0f, 120.0f }, feet{ 0.0f, 0.0f, 0.0f };
			const auto start = OrbAt(element, hand, feet, 0.0f), land = OrbAt(element, hand, feet, 1.0f);
			CHECK(Near(start.at.x, hand.x, 0.5f) && Near(start.at.y, hand.y, 0.5f) && Near(start.at.z, hand.z, 0.5f));
			CHECK(Near(land.at.x, feet.x, 0.5f) && Near(land.at.y, feet.y, 0.5f) && Near(land.at.z, feet.z, 0.5f));
			CHECK(start.alpha < 0.01f && land.alpha > 0.99f);
			CHECK(land.tilt > 0.1f);
			CHECK(std::cos(land.yaw - std::atan2(feet.y - hand.y, feet.x - hand.x)) > 0.99f);
		}
	}

	void QuakeTimeline()
	{
		using namespace FWM::Quake;
		const auto names = Parts();
		CHECK(names.size() <= static_cast<std::size_t>(kMaxParts));
		for (std::size_t i = 0; i < names.size(); ++i) {
			for (std::size_t j = i + 1; j < names.size(); ++j) {
				CHECK(names[i] != names[j]);
			}
		}
		// The third rank's throw peaks at about fifteen feet and comes down in about two seconds.
		CHECK(Near(LaunchSpeed() * LaunchSpeed() / (2.0f * kGravity), kLaunchHeight, 0.5f));
		CHECK(Airborne() > 1.5f && Airborne() < 2.5f);
		// The fallen stay down longer at each rank, a boss half as long.
		CHECK(Downed(1, false) < Downed(2, false) && Downed(2, false) < Downed(3, false));
		CHECK(Near(Downed(3, true), Downed(3, false) * kBossDowned));
		for (const bool grand : { false, true }) {
			const float end = Seconds(grand);
			for (const auto& part : At(-0.01f, grand).parts) {
				CHECK(part.scale == 0.0f && part.alpha == 0.0f);
			}
			CHECK(At(0.03f, grand).parts[0].alpha > 0.8f);  // the flash at once
			const auto risen = At(0.6f, grand);
			for (std::size_t i = 0; i < names.size(); ++i) {
				// The rocks are up, but only their tops out of the ground: low, never towering.
				if (names[i].starts_with("Slab") || (grand && names[i].starts_with("Spire"))) {
					const auto& rock = risen.parts[i];
					CHECK(rock.scale > 0.7f && rock.at.z < 0.0f && rock.at.z > -rock.scale * kRock * 0.5f);
					CHECK(rock.scale * kRock * 0.58f + rock.at.z < 60.0f);  // its top (0.58 of its breadth up) under about knee height
				}
				if (!grand && names[i].starts_with("Spire")) {
					CHECK(risen.parts[i].scale == 0.0f);
				}
			}
			// At the end every rock has sunk out of sight (below its own height) or shrunk away, and the glows are gone.
			const auto gone = At(end, grand);
			for (std::size_t i = 0; i < names.size(); ++i) {
				const auto& part = gone.parts[i];
				const bool  rock = names[i].starts_with("Slab") || names[i].starts_with("Spire") || names[i].starts_with("Chunk");
				CHECK(rock ? (part.scale < 0.01f || part.at.z < -part.scale * kRock) : part.alpha < 0.01f);
			}
			for (float t = 0.0f; t <= end; t += 1.0f / 30.0f) {
				const auto look = At(t, grand);
				for (std::size_t i = 0; i < names.size(); ++i) {
					if (names[i].starts_with("Chunk") || names[i].starts_with("Dust")) {
						CHECK(look.parts[i].at.z >= 0.0f);
					}
				}
			}
		}
		CHECK(Seconds(true) > Seconds(false));
	}

	void WaterCloakLook()
	{
		using namespace FWM::WaterCloak;
		constexpr float seconds = FWM::Paths::kRenewalSeconds;
		// Nothing before or after; it pours in from the top, the swirl first and the pool last.
		CHECK(At(-0.01f, seconds).veil == 0.0f && At(seconds + 0.01f, seconds).veil == 0.0f);
		const auto early = At(0.15f, seconds);
		CHECK(early.crown > 0.4f && early.pool == 0.0f && early.veil < 0.05f);
		const auto held = At(seconds / 2.0f, seconds);
		CHECK(held.veil > 0.7f && held.streams > 0.99f && held.pool > 0.99f && held.light > 0.7f && Near(held.rise, 1.0f));
		// It falls away as Renewal ends, sinking into its pool.
		CHECK(At(seconds - 0.01f, seconds).veil < 0.01f && At(seconds - 0.1f, seconds).rise < 1.0f);
		// The drops fall from the sheath into the pool, close round them, never below the ground.
		for (float t = 0.5f; t < seconds; t += 0.05f) {
			for (const auto& drop : At(t, seconds).drops) {
				CHECK(drop.alpha == 0.0f || (drop.z >= 2.0f && drop.z <= 115.0f && std::hypot(drop.x, drop.y) < 40.0f));
			}
			for (const auto& ripple : At(t, seconds).ripples) {
				CHECK(ripple.alpha >= 0.0f && ripple.alpha <= 1.0f && ripple.scale <= 2.61f);
			}
		}
		// A wash runs from over their head to their feet, leaves a ripple, and is gone.
		CHECK(WashAt(0.0f).z > 140.0f && WashAt(kWashRun).z < 10.0f && WashAt(0.3f).alpha > 0.9f);
		CHECK(WashAt(0.3f).ring == 0.0f && WashAt(kWashRun + 0.05f).ring > 0.5f);
		CHECK(WashAt(kWashSeconds).alpha == 0.0f && WashAt(kWashSeconds).ring == 0.0f && WashAt(-0.1f).alpha == 0.0f);
		// Drawn to their height, within reason.
		CHECK(Near(SizeFor(kHeight), 1.0f) && SizeFor(10.0f) == 0.5f && SizeFor(1000.0f) == 2.5f);
		// The third rank's is gold, a little larger, its top burning brighter under a glow only it has.
		const auto gold = At(seconds / 2.0f, seconds, true);
		CHECK(held.gleam == 0.0f && gold.gleam > 0.7f && gold.crown > held.crown && gold.light > held.light);
		CHECK(WaterOf(true) != WaterOf(false) && WaterOf(true)[0] > WaterOf(true)[2] && kGrandSize > 1.0f && kGrandSize < 1.25f);
		CHECK(At(seconds - 0.01f, seconds, true).gleam < 0.01f);
	}

	void ArrowRainLook()
	{
		using namespace FWM::ArrowRain;
		constexpr float rain = FWM::Paths::kRainSeconds;
		CHECK(kRadius == FWM::Paths::kRainRadius);
		// Nothing before it is called; the flare climbs first, and the mark is laid as it bursts.
		const auto before = At(-0.01f, rain);
		CHECK(before.mark == 0.0f && before.sky == 0.0f && before.burst == 0.0f);
		CHECK(At(kCall * 0.5f, rain).mark == 0.0f && FlareAt({}, { 0.0f, 0.0f, kSky }, kCall * 0.5f).alpha > 0.9f);
		CHECK(At(kCall + 0.03f, rain).burst > 0.5f && At(kCall + 0.5f, rain).mark > 0.99f);
		// The flare climbs from the bow to the sky over the mark, never below where it left.
		const Vec bow{ -500.0f, -600.0f, 110.0f }, high{ 0.0f, 0.0f, kSky };
		for (float t = 0.0f; t <= kCall; t += 0.01f) {
			CHECK(FlareAt(bow, high, t).at.z >= bow.z - 0.01f);
		}
		const auto top = FlareAt(bow, high, kCall - 0.001f).at;
		CHECK(Near(top.x, high.x, 1.0f) && Near(top.y, high.y, 1.0f) && Near(top.z, high.z, 1.0f));
		// The mark holds through the rain and the last arrow's fall, then goes; all of it is gone by the end.
		CHECK(At(kCall + rain + kLanding, rain).mark > 0.99f && At(Seconds(rain), rain).mark == 0.0f);
		CHECK(At(Seconds(rain), rain).sky == 0.0f && At(Seconds(rain), rain).burst == 0.0f);
		// Only the grand mark and sky wear their extra rings.
		CHECK(At(1.0f, rain).outer == 0.0f && At(1.0f, rain).crown == 0.0f && At(1.0f, rain, true).outer > 0.5f && At(1.0f, rain, true).crown > 0.5f);
		// A trail draws out behind its arrow, then shrinks into the hit and is gone.
		CHECK(TrailAt(0.2f, -1.0f).length > 0.95f && TrailAt(0.2f, -1.0f).alpha > 0.4f && TrailAt(0.2f, -1.0f).alpha < TrailAt(0.2f, -1.0f).arrow);  // a faint streak behind a bright arrow
		CHECK(TrailAt(0.2f + 0.2f, 0.2f).alpha == 0.0f && TrailAt(0.2f + 0.2f, 0.2f).length == 0.0f);
		CHECK(TrailAt(0.2f, -1.0f, true).width > TrailAt(0.2f, -1.0f).width);
		// The spectral arrow stands where it struck a moment, then fades and leaves the game's own.
		CHECK(TrailAt(0.2f, -1.0f).arrow > 0.8f && TrailAt(0.2f + 0.25f, 0.25f).arrow > 0.8f && TrailAt(0.2f + 0.85f, 0.85f).arrow == 0.0f);
		// Its +Z points back up the way it came, and it faces the camera.
		const auto turn = TrailTurn({}, { 0.0f, 0.0f, -4000.0f }, { 600.0f, 0.0f, 0.0f });
		CHECK(Near(turn[2][2], 1.0f) && Near(turn[0][1], 1.0f));
		// The hit: a spark at once, dust only off the ground, everything gone within a second.
		CHECK(HitAt(0.02f).spark > 0.5f && HitAt(-1.0f).spark == 0.0f);
		CHECK(HitAt(0.3f).dust > 0.3f && HitAt(0.3f, false).dust == 0.0f && HitAt(0.3f, false).ripple == 0.0f);
		const auto gone = HitAt(1.0f);
		CHECK(gone.spark == 0.0f && gone.ripple == 0.0f && gone.dust == 0.0f);
		// The stand-in arrows fall inside the mark, loosed as Arts looses the game's, landing within the show.
		for (int k = 0; k < kMaxArrows; ++k) {
			const auto arrow = DemoArrow(k, kMaxArrows, rain);
			CHECK(std::abs(arrow.to.x) <= kRadius + 0.01f && arrow.from.y == kPortalForward && arrow.to.y > arrow.from.y);
			CHECK(arrow.loosed >= kCall && arrow.loosed < kCall + rain && arrow.lands - arrow.loosed < kLanding);
			CHECK(arrow.lands + 1.0f <= Seconds(rain));
			// Landed, its point is kSink into the ground at its mark.
			const Vec along{ arrow.to.x - arrow.from.x, arrow.to.y - arrow.from.y, arrow.to.z - arrow.from.z };
			const auto tip = TipOf(DemoHead(arrow, arrow.lands + 0.5f), along);
			CHECK(Near(std::hypot(tip.x - arrow.to.x, tip.y - arrow.to.y, tip.z - arrow.to.z), kSink, 0.5f) && tip.y > arrow.to.y);
		}
	}

	void MagnusOrbTimeline()
	{
		using namespace FWM::MagnusOrb;
		auto gap = [](Vec a, Vec b) { return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z); };
		const Vec mage{ 100.0f, 200.0f, 10.0f };
		// Behind them and over their head, whichever way they face.
		const auto north = Home(mage, 0.0f, 0.0f), east = Home(mage, 1.5707964f, 0.0f);
		CHECK(north.y < mage.y - 100.0f && std::abs(north.x - mage.x) < 10.0f && north.z > mage.z + 150.0f);
		CHECK(east.x < mage.x - 100.0f && std::abs(east.y - mage.y) < 10.0f);
		// It drifts after them and settles, never overshooting.
		Vec   at = north;
		float last = gap(at, east);
		for (int i = 0; i < 180; ++i) {
			at = Follow(at, east, 1.0f / 60.0f);
			const float now = gap(at, east);
			CHECK(now <= last + 0.001f);
			last = now;
		}
		CHECK(last < 0.02f * gap(north, east));
		// Out of them, standing, and drawn back in; the magicka pours only while it stands.
		constexpr float kHeld = 12.0f;
		CHECK(At(0.0f, kHeld).out < 0.01f && At(0.0f, kHeld).flow < 0.01f);
		const auto standing = At(6.0f, kHeld);
		CHECK(standing.out > 0.99f && standing.scale > 0.95f && standing.scale < 1.09f && standing.alpha > 0.99f && standing.flow > 0.99f);
		const auto gone = At(kHeld, kHeld);
		CHECK(gone.out < 0.01f && gone.alpha < 0.01f && gone.flow < 0.01f);
		for (const auto& ring : standing.rings) {
			const float det = ring[0][0] * (ring[1][1] * ring[2][2] - ring[1][2] * ring[2][1]) - ring[0][1] * (ring[1][0] * ring[2][2] - ring[1][2] * ring[2][0]) +
			                  ring[0][2] * (ring[1][0] * ring[2][1] - ring[1][1] * ring[2][0]);
			CHECK(Near(det, 1.0f, 0.001f));
		}
		// The motes run from the orb into the chest and never wander far off that way; the streams end in it.
		const Vec orb = north, chest{ mage.x, mage.y, mage.z + kChest };
		for (float t = 0.0f; t <= 3.0f; t += 1.0f / 30.0f) {
			for (int i = 0; i < kFlows; ++i) {
				const auto mote = FlowAt(i, t, orb, 1.0f, chest, 1.0f);
				CHECK(mote.alpha >= 0.0f && mote.alpha <= 1.0f);
				CHECK(gap(mote.at, chest) <= gap(orb, chest) + kRadius + 1.0f);
				CHECK(FlowAt(i, t, orb, 1.0f, chest, 0.0f).alpha == 0.0f);
			}
			for (int i = 0; i < kTendrils; ++i) {
				const auto s = TendrilAt(i, t, orb, 1.0f, chest, 1.0f);
				const float reach = kTendrilLength * s.scale;
				const Vec   end{ s.from.x + s.turn[0][0] * reach, s.from.y + s.turn[1][0] * reach, s.from.z + s.turn[2][0] * reach };
				CHECK(gap(end, chest) < 1.0f);
				CHECK(gap(s.from, orb) < kRadius);
				// A plain orb pours through its tendrils, never its veils.
				CHECK(s.alpha > 0.0f && VeilAt(i, t, orb, 1.0f, chest, 1.0f, false).alpha == 0.0f);
			}
			for (int i = 0; i < kWisps; ++i) {
				CHECK(WispAt(i, t, orb, 1.0f, chest, 1.0f, false).alpha == 0.0f);
			}
		}
		// The grand orb: bigger, further back and higher, red at heart and blue at its edge, pouring through
		// its veils and wisps, its motes bigger and spiralling, still all into the chest.
		const auto grand = At(6.0f, kHeld, true);
		CHECK(grand.scale > kGrandSize * 0.95f && grand.scale < kGrandSize * 1.09f);
		CHECK(grand.coreColor[0] > 0.8f && grand.coreColor[1] < 0.3f && grand.rimColor[2] > 0.9f && grand.rimColor[0] < 0.5f && grand.rim > 0.99f);
		CHECK(standing.rim == 0.0f && standing.halo > 0.99f);
		CHECK(standing.coreColor[0] > 0.8f && standing.coreColor[1] > 0.8f);
		const auto high = Home(mage, 0.0f, 0.0f, true);
		CHECK(high.y < north.y - 20.0f && high.z > north.z + 20.0f);
		for (float t = 0.0f; t <= 3.0f; t += 1.0f / 30.0f) {
			for (int i = 0; i < kFlows; ++i) {
				const auto mote = FlowAt(i, t, high, grand.scale, chest, 1.0f, true);
				CHECK(gap(mote.at, chest) <= gap(high, chest) + kRadius * grand.scale + 20.0f);
				CHECK(mote.size >= FlowAt(i, t, high, grand.scale, chest, 1.0f, false).size * 1.6f);
			}
			for (int i = 0; i < kTendrils; ++i) {
				CHECK(TendrilAt(i, t, high, grand.scale, chest, 1.0f, true).alpha == 0.0f);
				const auto veil = VeilAt(i, t, high, grand.scale, chest, 1.0f, true);
				const float reach = kTendrilLength * veil.scale;
				const Vec   end{ veil.from.x + veil.turn[0][0] * reach, veil.from.y + veil.turn[1][0] * reach, veil.from.z + veil.turn[2][0] * reach };
				CHECK(veil.alpha > 0.0f && gap(end, chest) < 1.0f);
			}
			for (int i = 0; i < kWisps; ++i) {
				const auto wisp = WispAt(i, t, high, grand.scale, chest, 1.0f, true);
				CHECK(wisp.alpha >= 0.0f && wisp.alpha <= 1.0f);
				CHECK(gap(wisp.at, chest) <= gap(high, chest) + kRadius * grand.scale + 20.0f);
				CHECK(Near(wisp.turn[0][0] * wisp.turn[0][0] + wisp.turn[1][0] * wisp.turn[1][0] + wisp.turn[2][0] * wisp.turn[2][0], 1.0f, 0.001f));
			}
		}
	}

	// The third rank's red wolf holds on longer but bites the same; the smoke on whoever it catches.
	void WolfHold()
	{
		using namespace FWM::WolfBite;
		const Vec   at{ 100.0f, 200.0f, 10.0f };
		const float chest = 70.0f;
		CHECK(ShakeEnd(true) > ShakeEnd(false) && Gone(true) > Gone(false));
		CHECK(Near(Gone(true) - ShakeEnd(true), Gone(false) - ShakeEnd(false)));
		for (float t = 0.0f; t <= kSnap; t += 0.05f) {
			const auto plain = At(t, 0.0f, at, chest), grand = At(t, 0.0f, at, chest, true);
			CHECK(Near(plain.jaw, grand.jaw) && Near(plain.alpha, grand.alpha) && Near(plain.translate.x, grand.translate.x) && Near(plain.translate.z, grand.translate.z));
		}
		CHECK(At(kGone, 0.0f, at, chest, true).alpha > 0.5f);
		CHECK(At(Gone(true), 0.0f, at, chest, true).alpha < 0.01f);

		namespace Paths = FWM::Paths;
		CHECK(Paths::kWolfHold[2] > Paths::kWolfHold[1] && Paths::kWolfHold[1] >= Paths::kWolfHold[0]);
		CHECK(Paths::kWolfCatch > 0.0f && Paths::kWolfCatch < 0.5f);
		CHECK(Paths::kWolfBossHold > 0.0f && Paths::kWolfBossHold < 1.0f);

		using namespace FWM::Haunt;
		const float held = Paths::kWolfCatch + Paths::kWolfHold[0];
		// Nothing before the catch or once it has thinned away; a burst of light at once, then the smoke.
		CHECK(At(-0.01f, held).mist == 0.0f && At(Seconds(held) + 0.01f, held).glow == 0.0f);
		CHECK(At(0.02f, held).flare > 0.5f && At(0.6f, held).flare < 0.01f);
		const auto clinging = At(held / 2.0f, held);
		CHECK(clinging.mist > 0.4f && clinging.glow > 0.4f && clinging.ground > 0.4f);
		CHECK(At(held + kLinger * 0.5f, held).mist < clinging.mist && At(Seconds(held) - 0.01f, held).mist < 0.05f);
		// The third rank's is stronger and lasts longer.
		const float longer = Paths::kWolfCatch + Paths::kWolfHold[2];
		CHECK(Seconds(longer) > Seconds(held));
		CHECK(At(held / 2.0f, longer, true).mist > clinging.mist && At(0.02f, longer, true).flare > At(0.02f, held).flare);
		CHECK(At(held + 0.3f, longer, true).mist > 0.4f);
		CHECK(GlowOf(true)[0] > GlowOf(true)[2] && GlowOf(false)[2] > GlowOf(false)[0]);
		CHECK(SmokeOf(true)[0] < 0.3f && SmokeOf(false)[2] < 0.3f);
		// The wisps stay round them, on screen and within reach of the mesh.
		for (float t = 0.0f; t <= Seconds(longer); t += 0.05f) {
			for (const bool grand : { false, true }) {
				const float span = grand ? longer : held;
				const auto  look = At(t, span, grand);
				CHECK(t > Seconds(span) || (look.scale >= 1.0f && look.scale < 1.3f));
				for (const auto& wisp : look.wisps) {
					CHECK(wisp.alpha >= 0.0f && wisp.alpha <= 1.0f);
					CHECK(std::hypot(wisp.x, wisp.y) <= 50.0f && wisp.z >= 0.0f && wisp.z <= kHeight + 40.0f);
				}
			}
		}
		CHECK(Near(SizeFor(kHeight), 1.0f) && SizeFor(1000.0f) == 3.0f && SizeFor(10.0f) == 0.5f);
		// Leaning with them: upright stays upright, a small lean is followed, a creature's long neck is not.
		auto upOf = [](const Turn& a_turn) { return std::array{ a_turn[0][2], a_turn[1][2], a_turn[2][2] }; };
		CHECK(Near(Lean(0.0f, 0.0f, 120.0f)[2][2], 1.0f) && Near(Lean(0.0f, 0.0f, 120.0f)[0][0], 1.0f));
		const auto back = upOf(Lean(0.0f, 30.0f, 110.0f));
		const float length = std::hypot(30.0f, 110.0f);
		CHECK(Near(back[0], 0.0f) && Near(back[1], 30.0f / length) && Near(back[2], 110.0f / length));
		const auto side = upOf(Lean(-20.0f, 10.0f, 100.0f));
		const float sideLength = std::hypot(-20.0f, 10.0f, 100.0f);
		CHECK(Near(side[0], -20.0f / sideLength) && Near(side[1], 10.0f / sideLength) && Near(side[2], 100.0f / sideLength));
		CHECK(Near(upOf(Lean(80.0f, 0.0f, 40.0f))[2], std::cos(kMostLean)));
		CHECK(Near(Lean(5.0f, 0.0f, -40.0f)[2][2], 1.0f));
		const auto turn = Lean(17.0f, -9.0f, 60.0f);
		for (int r = 0; r < 3; ++r) {
			for (int c = 0; c < 3; ++c) {
				const float dot = turn[0][r] * turn[0][c] + turn[1][r] * turn[1][c] + turn[2][r] * turn[2][c];
				CHECK(Near(dot, r == c ? 1.0f : 0.0f));
			}
		}
	}
}

int main()
{
    CHECK(!FWM::TreeEntrance::At(0.f).visible);
    CHECK(FWM::TreeEntrance::At(.65f).black==1.f);
    CHECK(!FWM::TreeEntrance::At(.77f).visible);
    CHECK(FWM::TreeEntrance::At(.78f).visible && FWM::TreeEntrance::At(.78f).black==1.f);
    CHECK(!FWM::TreeEntrance::At(1.f).interactive);
    CHECK(FWM::TreeEntrance::At(1.25f).interactive && FWM::TreeEntrance::At(1.25f).black==0.f);
    // The visible companion/tree changes only while the veil fully covers the screen.
    CHECK(FWM::TreeEntrance::ExitBlack(0.f)==0.f);
    CHECK(FWM::TreeEntrance::ExitBlack(FWM::TreeEntrance::kExitSwap)==1.f);
    CHECK(FWM::TreeEntrance::ExitBlack(FWM::TreeEntrance::kExitReveal)==1.f);
    CHECK(FWM::TreeEntrance::ExitBlack(FWM::TreeEntrance::kExitComplete)==0.f);
    CHECK(FWM::TreeEntrance::SwitchWipe(0.f).right==0.f);
    CHECK(FWM::TreeEntrance::SwitchWipe(FWM::TreeEntrance::kSwitchSwap).left==0.f);
    CHECK(FWM::TreeEntrance::SwitchWipe(FWM::TreeEntrance::kSwitchSwap).right==1.f);
    CHECK(FWM::TreeEntrance::SwitchWipe(FWM::TreeEntrance::kSwitchReveal).left==0.f);
    CHECK(FWM::TreeEntrance::SwitchWipe(FWM::TreeEntrance::kSwitchComplete).left==1.f);

    // Custom NPC races stay eligible; animal/creature tags override an NPC tag.
    CHECK(FWM::RoleAllowed(true, false, false));
    CHECK(!FWM::RoleAllowed(false, true, false));
    CHECK(!FWM::RoleAllowed(true, true, false));
    CHECK(!FWM::RoleAllowed(false, false, false));
    CHECK(!FWM::RoleAllowed(true, false, true));
    {
        using namespace FWM::TreeNavigation;
        const std::array<Node, 9> nodes{{{100,1,true},{70,2,true},{130,2,true},{300,1,true},{270,2,true},{330,2,true},{500,1,true},{470,2,true},{530,2,true}}};
        CHECK(Move(nodes,0,1,0)==3); // right stays on the ability row
        CHECK(Move(nodes,1,1,0)==2); // branches stay on their own row
        CHECK(Move(nodes,2,1,0)==4);
        CHECK(Move(nodes,0,0,1)==1); // down enters the attached upgrades
        CHECK(Move(nodes,2,0,-1)==0);
        CHECK(Move(nodes,6,1,0)==6); // edge never falls into a different row
        CHECK(Row(9)==0 && Row(10)==0 && Row(12)==3);
        auto sparse=nodes; sparse[3].enabled=false;
        CHECK(Move(sparse,0,1,0)==6);
        CHECK(Move(nodes,0,0,0)==0);
    }
    {
        using FWM::HealCircle::Seek;
        CHECK(Seek(.29f,false,600,20,10,true));
        CHECK(!Seek(.6f,false,600,20,10,true));
        CHECK(Seek(.6f,true,600,20,10,true));
        CHECK(!Seek(.9f,true,100,20,10,true));
        CHECK(!Seek(.2f,false,600,20,3,true));
        CHECK(!Seek(.2f,false,600,300,10,true));
        CHECK(!Seek(.2f,false,600,20,10,false));
        FWM::StyleData aggressive; aggressive.offensive=1; aggressive.flags|=FWM::StyleData::kFlanking;
        FWM::StyleTweaks retreat; retreat.recovery=true;
        const auto style=FWM::Tweaked(aggressive,retreat);
        CHECK(style.offensive==0 && style.defensive==1 && style.circle==0);
        CHECK((style.flags & FWM::StyleData::kFlanking)==0);
        CHECK(style.magicScore==aggressive.magicScore && style.meleeScore==aggressive.meleeScore);
        CHECK(FWM::Tweaked(aggressive,{}).offensive==1);
    }

    {
        using namespace FWM::LoadoutPolicy;
        Saved a{42,{{{100,101},{200},{300,301}}}};
        const auto bytes=Encode({a});const auto loaded=Decode(bytes);
        CHECK(loaded.size()==1 && loaded[0].actor==42 && loaded[0].pools==a.pools);
        for(std::size_t cut=1;cut<bytes.size();cut++)CHECK(Decode(std::span(bytes).first(cut)).empty());
        auto bad=bytes;bad[4]=255;CHECK(Decode(bad).empty());
        CHECK(Allows(a.pools[0],101) && !Allows(a.pools[0],200));
        CHECK(Allows({},999));
        Saved empty{42,{}};CHECK(Decode(Encode({empty}))[0].pools==Pools{});
        CHECK(BattleRole(WeaponKind::TwoHanded,Role::kTank,false)==Role::kTank);
        CHECK(BattleRole(WeaponKind::TwoHanded,Role::kFighter,false)==Role::kBruiser);
        CHECK(BattleRole(WeaponKind::OneHanded,Role::kMage,true)==Role::kTank);
        CHECK(BattleRole(WeaponKind::OneHanded,Role::kBruiser,false)==Role::kFighter);
        CHECK(BattleRole(WeaponKind::Bow,Role::kTank,true)==Role::kArcher);
        CHECK(BattleRole(WeaponKind::Staff,Role::kHealer,false)==Role::kHealer);
        CHECK(BattleRole(WeaponKind::Staff,Role::kFighter,false)==Role::kMage);
        BattleChoice choice;
        CHECK(choice.Lock(300,400,WeaponKind::TwoHanded,Role::kTank));
        CHECK(choice.weapon==300 && choice.shield==0 && choice.role==Role::kTank);
        CHECK(!choice.Lock(301,401,WeaponKind::OneHanded,Role::kFighter));
        CHECK(choice.weapon==300 && choice.role==Role::kTank);
        choice={};CHECK(choice.Lock(301,401,WeaponKind::OneHanded,Role::kFighter));
        CHECK(choice.shield==401 && choice.role==Role::kTank);
        choice={};CHECK(!choice.Lock(0,0,WeaponKind::OneHanded,Role::kMage));
        CHECK(!choice.Lock(301,0,WeaponKind::OneHanded,Role::kFighter));

        // Paths constrains a pool without changing the selected role; standalone
        // Fight With Me keeps the weapon-derived role behavior above.
        CHECK(!RoleAllows(RoleWeapons(Role::kMage),WeaponKind::OneHanded));
        CHECK(RoleAllows(RoleWeapons(Role::kMage),WeaponKind::Staff));
        CHECK(RoleAllows(RoleWeapons(Role::kHealer),WeaponKind::Staff));
        CHECK(RoleAllows(RoleWeapons(Role::kTank),WeaponKind::OneHanded));
        CHECK(RoleAllows(RoleWeapons(Role::kTank),WeaponKind::TwoHanded));
        CHECK(RoleAllows(RoleWeapons(Role::kTank),WeaponKind::Shield));
        CHECK(!RoleAllows(RoleWeapons(Role::kTank),WeaponKind::Bow));
        CHECK(RoleAllows(RoleWeapons(Role::kFighter),WeaponKind::OneHanded));
        CHECK(!RoleAllows(RoleWeapons(Role::kFighter),WeaponKind::TwoHanded));
        CHECK(RoleAllows(RoleWeapons(Role::kArcher),WeaponKind::Bow));
        CHECK(!RoleAllows(RoleWeapons(Role::kArcher),WeaponKind::Staff));
        CHECK(RoleAllows(RoleWeapons(Role::kBruiser),WeaponKind::TwoHanded));
        CHECK(!RoleAllows(RoleWeapons(Role::kBruiser),WeaponKind::Shield));
        CHECK(RoleWeapons(Role::kCount)==0);
        CHECK(RoleWeapons(Role::kSkald)==0 && !ChoosesSpells(Role::kSkald));
        CHECK(!SpellAllowed(Role::kSkald,true,false,SpellTier::kNovice,50,true));
        CHECK(!SpellAllowed(Role::kSkald,false,true,SpellTier::kMaster,50,true));
        choice={};CHECK(choice.Lock(300,0,WeaponKind::TwoHanded,Role::kTank,true));
        CHECK(choice.role==Role::kTank && choice.weapon==300);
        CHECK(!choice.Lock(301,0,WeaponKind::OneHanded,Role::kTank,true));
        choice={};CHECK(choice.Lock(302,0,WeaponKind::Staff,Role::kHealer,true));
        CHECK(choice.role==Role::kHealer);
        const Pool savedPool{11,12}; // dagger and staff retained across a role change
        const auto magePool=Compatible(savedPool,[](auto id){return id==12;});
        CHECK(magePool==Pool{12} && savedPool.size()==2);
        const auto fighterPool=Compatible(savedPool,[](auto id){return id==11;});
        CHECK(fighterPool==Pool{11});
        const auto unusable=Compatible(savedPool,[](auto){return false;});
        CHECK(!unusable.empty() && !Allows(unusable,11) && !Allows(unusable,12));
        CHECK(Compatible(Pool{},[](auto){return false;}).empty());
        const Pool eligibleStaff{12};
        CHECK(Resolve({},eligibleStaff)==eligibleStaff);
        CHECK(!Allows(Resolve({},eligibleStaff),11));
        CHECK(Resolve(savedPool,eligibleStaff)==eligibleStaff);
        CHECK(!Allows(Resolve({},{}),11));
        CHECK(!Allows(Resolve(Pool{11},eligibleStaff),11));
        CHECK(!Allows(Resolve(Pool{11},eligibleStaff),12));
        // A later role change resolves afresh without corrupting saved selections.
        CHECK(Resolve({},Pool{11})==Pool{11});
        CHECK(savedPool.size()==2);
        FWM::StyleData hostile;hostile.meleeScore=8;hostile.unarmedScore=12;hostile.offensive=1;hostile.fallback=0;
        FWM::StyleTweaks caster;caster.caster=true;
        const auto casting=FWM::Tweaked(hostile,caster);
        CHECK(casting.unarmedScore==0 && casting.meleeScore==0 && casting.magicScore==hostile.magicScore);
        caster.magickaRecovery=true;
        const auto resting=FWM::Tweaked(hostile,caster);
        CHECK(resting.unarmedScore==0 && resting.meleeScore==0 && resting.magicScore==0);
        CHECK(resting.offensive==0 && resting.groupOffensive==0 && resting.defensive==1 && resting.fallback==1);
        CHECK((resting.flags & (FWM::StyleData::kFlanking|FWM::StyleData::kDueling))==0);
        caster.magickaRecovery=false;
        CHECK(FWM::Tweaked(hostile,caster).magicScore==hostile.magicScore);
        CHECK(FWM::Tweaked(hostile,{}).unarmedScore==12);


        CHECK(FWM::AbilityResource::Cost(200,1)==20);
        CHECK(FWM::AbilityResource::Cost(200,3)==40);
        CHECK(!FWM::AbilityResource::Enough(39,200,3));
        CHECK(FWM::AbilityResource::Enough(40,200,3));
        CHECK(!FWM::AbilityResource::Enough(0,0,1));
        CHECK(!FWM::AbilityResource::Enough(std::numeric_limits<float>::quiet_NaN(),200,1));
    }
	RejuvenationTimeline();
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
	UniqueTraits();
	Levels();
	Translations();
	WolfBiteTimeline();
	HealingCircle();
	ElementalBlast();
	MagnusOrbTimeline();
	QuakeTimeline();
	ArrowRainLook();
	WaterCloakLook();
	WolfHold();
	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
